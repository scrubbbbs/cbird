/* Directory scanning and indexing
   Copyright (C) 2021 scrubbbbs
   Contact: screubbbebs@gemeaile.com =~ s/e//g
   Project: https://github.com/scrubbbbs/cbird

   This file is part of cbird.

   cbird is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   cbird is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with cbird; if not, see
   <https://www.gnu.org/licenses/>.  */
#include "scanner.h"

#include "cvutil.h"
#include "fsutil.h"
#include "index.h"
#include "ioutil.h"
#include "media.h"
#include "qtutil.h"
#include "videocontext.h"

#include "opencv2/features2d.hpp"
#include "quazip/quazip.h"

Scanner::Scanner() {
  // clang-format off
  _imageTypes << "jpg" << "jpeg" << "jfif" << "png" << "bmp" << "gif";
  _jpegTypes << "jpg" << "jpeg" << "jfif";
  _videoTypes << "mp4" << "wmv" << "asf" << "flv" << "mpg" << "mpeg"  << "mov" << "vob" << "ogv"
              << "rm" << "ram" << "webm"<< "f4v" << "m4v" << "avi" << "qt" << "mkv"
              << "ts" << "mts"  << "m2t";
  _archiveTypes << "zip";
  // clang-format on

  for (auto& suffix : QImageReader::supportedImageFormats())
    if (!_imageTypes.contains(suffix)) _imageTypes << suffix;

  // Because ffmpeg demuxers do not have to publish extensions or mime types,
  // and they do not indicate what types of content they may contain, we
  // can't really get this list from ffmpeg. Some extensions could be
  // eliminated from this list, for those demuxers that do, with reasonable
  // certainty. Some can also be guessed by looking at the corresponding
  // muxer, which does publish default codecs
}

Scanner::~Scanner() { flush(); }

void Scanner::scanDirectory(const QString& path, QSet<QString>& expected,
                            const QDateTime& modifiedSince) {
  if (_params.indexThreads <= 0) _params.indexThreads = QThread::idealThreadCount();
  if (_params.decoderThreads <= 0) _params.decoderThreads = QThread::idealThreadCount();

#ifdef Q_OS_WIN
  if (!_params.dupInodes)
    qWarning() << "duplicate inode check (-i.dups 0) can be extremely slow on network volumes";
#endif

  // todo: subdirectory limiter for large indexes
  // if (!_params.subdir.isEmpty())

  _gpuPool.setMaxThreadCount(_params.gpuThreads);
  _videoPool.setMaxThreadCount(_params.indexThreads);
  //_imagePool.setMaxThreadCount(_params.indexThreads);

  _topDirPath = path;
  _existingFiles = 0;
  _ignoredFiles = 0;
  _modifiedFiles = 0;
  _processedFiles = 0;
  _modifiedSince = modifiedSince;
  _inodes.clear();
  readDirectory(path, expected);
  scanProgress(path);

  // estimate the cost of each video, to process longest-job-first (LJF),
  // - this is slow; so try to avoid it
  // - pointless if codecs are all multithreaded
  // - little difference if there are a lot of jobs
  if (_params.estimateCost && _params.algos & SearchParams::AlgoVideo &&
      _videoQueue.count() <= _params.indexThreads) {
    QMap<QString, float> cost;
    for (auto& path : qAsConst(_videoQueue)) {
      cost[path] = -1.0f;

      const QString context = path.mid(_topDirPath.length() + 1);
      const MessageContext mc(context);

      // todo: cost could be better by considering codec/decoder
      VideoContext v;
      if (v.open(path) < 0) continue;

      VideoContext::Metadata d = v.metadata();
      cost[path] =
          (d.frameRate * d.duration * d.frameSize.width() * d.frameSize.height()) / v.threadCount();
    }

    std::sort(_videoQueue.begin(), _videoQueue.end(),
              [&cost](const QString& a, const QString& b) { return cost[a] > cost[b]; });

    for (auto path : _videoQueue)
      qDebug("estimate cost=%.2f path=%s", double(cost[path]), qUtf8Printable(path));
  }

  if (_params.dryRun) {
    qInfo() << "dry run, flushing queues";
    flush(false);
  }

  if (_imageQueue.count() > 0 || _videoQueue.count() > 0) {
    qInfo() << "scan completed, indexing" << remainingWork() << "additions...";
    QTimer::singleShot(1, this, &Scanner::processOne);
  } else {
    qInfo() << "scan completed, nothing to index";
    QTimer::singleShot(1, this, [&] { emit scanCompleted(); });
  }
}

void Scanner::readArchive(const QString& path, QSet<QString>& expected) {
  QuaZip zip(path);
  if (!zip.open(QuaZip::mdUnzip)) {
    setError(path, Scanner::ErrorOpen);
    return;
  }

  // it seems a zip can contain duplicate file names (corrupt zip?)
  // so we need to remove from skip list after iterating
  QStringList skipped;

  const auto list = zip.getFileInfoList();
  for (const auto& entry : list) {
    QString file = entry.name;
    if (file.endsWith("/")) continue;

    // todo: setting for ignored folder names
    const QString zipPath = Media::virtualPath(path, file);
    if (file.startsWith(".") || file.startsWith("__MACOSX")) {
      _ignoredFiles++;
      setError(zipPath, ErrorZipFilter, _params.showIgnored);
      continue;
    }
    if (expected.contains(zipPath)) {
      if (entry.dateTime < _modifiedSince) {
        skipped.append(zipPath);
        _existingFiles++;
        continue;
      } else {
        _modifiedFiles++;
      }
    }

    QFileInfo info(file);
    const QString type = info.suffix().toLower();

    if ((_params.types & IndexParams::TypeImage) && _imageTypes.contains(type)) {
      if (!isQueued(zipPath)) {
        _imageQueue.append(zipPath);
        _queuedWork.insert(zipPath);
      }
    } else {
      _ignoredFiles++;
      setError(zipPath, ErrorZipUnsupported, _params.showIgnored);
    }
  }

  for (const auto& zipPath : skipped) expected.remove(zipPath);
}

void Scanner::setError(const QString& path, const QString& error, bool print) {
  QMutexLocker locker(staticMutex());
  QStringList& list = (*errors())[path];
  if (!list.contains(error)) list.append(error);
  if (print)
    qWarning() << path << error;
}

QMap<QString, QStringList>* Scanner::errors() {
  static QMap<QString, QStringList> map;
  return &map;
}

QMutex* Scanner::staticMutex() {
  static QMutex mutex;
  return &mutex;
}

void Scanner::scanProgress(const QString& path) const {
  const QString elided = qElide(path.mid(_topDirPath.length() + 1), 80);

  QString status =
      QString::asprintf("<NC>%s<PL> i:%lld v:%lld ign:%d mod:%d ok:%d <EL>%s",
                        qUtf8Printable(_topDirPath), _imageQueue.count(), _videoQueue.count(),
                        _ignoredFiles, _modifiedFiles, _existingFiles, qUtf8Printable(elided));
  qInfo().noquote() << status;
}

void Scanner::readDirectory(const QString& dirPath, QSet<QString>& expected) {
  const QDir dir(dirPath);
  if (!dir.exists()) {
    qWarning("%s does not exist", qUtf8Printable(dirPath));
    return;
  }

  QStringList dirs;
  scanProgress(dirPath);

  QDir::Filters filters = QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot;

  for (const QString& name : dir.entryList(filters)) {
    QString path = dirPath + "/" + name;
    const QFileInfo entry(path);

    // junctions are effectively symlinks
    if (!_params.followSymlinks && (entry.isSymLink() || entry.isJunction())) {
      _ignoredFiles++;
      setError(path, ErrorNoLinks, _params.showIgnored);
      continue;
    }

    if (!_params.dupInodes) {
      // if we see the same inode twice, ignore it
      // stops false duplicates and link recursion
      FileId id(path);
      if (id.isValid()) {
        const auto& hash = _inodes;
        auto it = hash.find(id);
        if (it != hash.end()) {
          if (_params.showIgnored) {
            qWarning() << "ignoring dup inode:" << path;
            qWarning() << "    first instance:" << it.value();
          }
          _ignoredFiles++;
          setError(path, ErrorDupInode, _params.showIgnored);
          continue;
        } else
          _inodes.insert(id, path);
      }
    }

    // prefer not to store symlinks in db
    // - if the link is broken or renamed, forces reindex
    // - allows links to be used for organizing, without re-indexing
    if (_params.resolveLinks && (entry.isSymLink() || entry.isJunction())) {
      QString canonical;
#ifdef Q_OS_WIN
      if (entry.isJunction())  // qt will not resolve it ...
        canonical = resolveJunction(path);
      else
#endif
        canonical = entry.canonicalFilePath();
      if (canonical.startsWith(_topDirPath)) {
        path = canonical;
        _imageQueue.removeOne(path);
        _videoQueue.removeOne(path);
        _queuedWork.remove(path);
      }
    }

    if (expected.contains(path)) {
      // fixme: database should store modification date?
      // metadataChangeTime() could be used but will re-index
      // anything we touch ourselves
      if (entry.lastModified() < _modifiedSince) {
        expected.remove(path);
        _existingFiles++;
        continue;
      }
      _modifiedFiles++;
    }

    if (entry.isFile() && !_activeWork.contains(path)) {
      //            printf("considering: images=%d videos=%d %s \r",
      //                   _imageQueue.count(), _videoQueue.count(),
      //                   qPrintable(path));
      //            fflush(stdout);
      const QString type = entry.suffix().toLower();
      if (type.isEmpty()) {
        _ignoredFiles++;
        setError(path, ErrorNoType, _params.showIgnored);
        continue;
      }

      if ((_params.types & IndexParams::TypeImage) && _imageTypes.contains(type)) {
        if (entry.size() < _params.minFileSize) {
          _ignoredFiles++;
          setError(path, ErrorTooSmall, _params.showIgnored);
        } else if (!isQueued(path)) {
          _imageQueue.append(path);
          _queuedWork.insert(path);
        }
      } else if ((_params.types & IndexParams::TypeVideo) && _videoTypes.contains(type)) {
        if (entry.size() < _params.minFileSize) {
          _ignoredFiles++;
          setError(path, ErrorTooSmall, _params.showIgnored);
        } else if (!isQueued(path))
          _videoQueue.append(path);
      } else if (_archiveTypes.contains(type)) {
        // todo: attempt to skip deep scan of zip files... this is slow
        // 1. the zip modified date is before _modifiedSince
        // 2. the expected list contains the zip members
        // 3. remove all from expected list
        // if (entry.metadataChangeTime() >= _modifiedSince)
        scanProgress(path);
        readArchive(path, expected);
      } else {
        _ignoredFiles++;
        setError(path, ErrorUnsupported, _params.showIgnored);
      }
    } else if (entry.fileName() != INDEX_DIRNAME && entry.isDir()) {
      dirs.push_back(path);
    }
  }

  if (_params.recursive)
    for (int i = 0; i < dirs.count(); i++) readDirectory(dirs[i], expected);
}

void Scanner::flush(bool wait) {
  static bool inProgress = false;
  if (inProgress) {
    qCritical() << "recursion thwarted";
    return;
  }

  inProgress = true;

  // empty waiting queues
  _imageQueue.clear();
  _videoQueue.clear();

  // remove unstarted jobs from threadpool (cleanup in processFinished())
  int cancelled = 0;
  for (auto* w : _work) {
    cancelled++;
    w->cancel();
  }

  // todo: clear started slow-running jobs (process video)

  // in case this is called for no reason
  if (cancelled <= 0 && _activeWork.count() <= 0) qDebug() << "nothing to flush";

  if (wait) {
    // it isn't ideal to block, but the nature of flush() is that it must,
    // alternative is not to use it at all somehow
    qInfo() << "cleaning up" << cancelled << "workers";

    QEventLoop loop;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
      if (_activeWork.count() <= 0)
        loop.exit(0);
      else {
        qWarning() << "waiting for workers...";
        timer.start(1000);
      }
    });
    timer.start();
    loop.exec();
    qInfo() << "complete";
  }
  inProgress = false;
}

void Scanner::finish() {
  QEventLoop loop;
  QTimer timer;
  connect(&timer, &QTimer::timeout, [=, &timer, &loop]() {
    if (_imageQueue.empty() && _videoQueue.empty() && _activeWork.empty()) {
      timer.stop();
      loop.exit(0);
      return;
    }
    QString status = QString::asprintf(
        "<NC>queued:<PL>image=%lld,video=%lld:batch=%lld,threadpool:gpu=%d,video=%d,global=%"
        "d    ",
        _imageQueue.count(), _videoQueue.count(), _activeWork.count(), _gpuPool.activeThreadCount(),
        _videoPool.activeThreadCount(), QThreadPool::globalInstance()->activeThreadCount());
    qInfo().noquote() << status;
    timer.setInterval(100);
  });

  timer.setInterval(1);
  timer.start();
  loop.exec();
}

void Scanner::processOne() {
  QFuture<IndexResult> f;
  bool queuedImage = false;

  // job scheduler
  // - runs in main thread when a job completes or until
  //   queue limits are reached.
  // - process longest jobs first (video before images), better utilization
  // - video decoder can be multithreaded, decreases # of parallel jobs
  //
  // queue enough work to keep thread pool full
  // - queue up to _params.writeBatchSize for images to hide database write latency
  // - queue up to _params.indexThreads for videos to save memory
  //
  int queueLimit = _params.indexThreads;

  if (_videoQueue.empty()) {
    queueLimit = _params.writeBatchSize;

    if (_activeWork.count() < _params.indexThreads &&  // not enough work queued
        _imageQueue.size() > queueLimit &&             // there is enough available
        _processedFiles > queueLimit)                  // we have already processed some
      qWarning() << "worker starvation, maybe increase writeBatchSize (-i.bsize)";
  }

  if (_activeWork.count() < queueLimit) {
    QString path;
    if (!_videoQueue.empty()) {
      path = _videoQueue.first();
      const MessageContext mc(path.mid(_topDirPath.length() + 1));

      bool tryGpu =
          _params.useHardwareDec && _gpuPool.activeThreadCount() < _gpuPool.maxThreadCount();
      const int availThreads = _videoPool.maxThreadCount() - _videoPool.activeThreadCount();

      int cpuThreads = 0;
      if (availThreads >= _params.decoderThreads) cpuThreads = _params.decoderThreads;

      // not mp-aware-codec gets 1 thread
      if (QFileInfo(path).suffix().toLower() == ll("wmv")) cpuThreads = qMin(availThreads, 1);

      // something is wrong with this logic, we end up with 1 thread on everything
      //      if (_videoQueue.count() > _videoPool.maxThreadCount()) {
      //        // try fully utilizing machine when there are a lot of videos
      //        // this will take longer if one video is very long and the others are short
      //        cpuThreads = 1;
      //      }
      //      else

      // there is one video left, it can have all the available threads
      if (_videoQueue.count() == 1) {
        if (availThreads > _params.decoderThreads) cpuThreads = availThreads;
        // else
        //   cpuThreads = _videoPool.maxThreadCount() - _videoPool.activeThreadCount();
      }

      if (tryGpu || cpuThreads > 0) {
        VideoContext* v = initVideoProcess(path, tryGpu, cpuThreads);
        if (v) {
          QThreadPool* pool = nullptr;
          if (v->isHardware()) {
            pool = &_gpuPool;
            // qDebug() << "gpu pool" << v->threadCount();
          } else if (cpuThreads > 0) {
            pool = &_videoPool;
            // qDebug() << "cpu pool" << v->threadCount();
            if (v->threadCount() > 1)
              pool->setMaxThreadCount(qMax(1, pool->maxThreadCount() - v->threadCount() + 1));
          }

          if (!pool && tryGpu) {
            // stop gpu from retrying the same file
            // fixme: disable gpu after too many fails
            // fixme: search queue for things that will possibly work
            _videoQueue.removeFirst();
            _videoQueue.append(path);
          }

          if (pool) {
            f = QtConcurrent::run(pool, &Scanner::processVideo, this, v);
            _videoQueue.removeFirst();
          }
        } else
          _videoQueue.removeFirst();  // failed to open
        // printf("v");
        // fflush(stdout);
      }
    } else if (!_imageQueue.empty()) {
      path = _imageQueue.takeFirst();
      _queuedWork.remove(path);
      f = QtConcurrent::run(&Scanner::processImageFile, this, path, QByteArray());
      queuedImage = true;
      // printf("i");
      // fflush(stdout);
    } else {
      // printf(".");
      // fflush(stdout);
    }

    if (!f.isCanceled()) {
      _activeWork.insert(path);
      QFutureWatcher<IndexResult>* w = new QFutureWatcher<IndexResult>;
      connect(w, SIGNAL(finished()), this, SLOT(processFinished()));
      w->setFuture(f);
      w->setProperty("path", path);
      _work.append(w);
    }
  }

  // if the queues are not empty, call processOne again.
  // fixme: seems delay is no longer needed
  int delay = -1;
  if (!_videoQueue.empty())
    delay = 10;
  else if (!_imageQueue.empty()) {
    // if we did not queue an image, sleep 1ms to reduce polling,
    // otherwise run immediately to top-up queue
    if (queuedImage)
      delay = 0;
    else
      delay = 1;
  }
  if (delay >= 0) QTimer::singleShot(delay, this, &Scanner::processOne);
}

void Scanner::processFinished() {
  auto w = dynamic_cast<QFutureWatcher<IndexResult>*>(sender());
  if (!w) return;

  IndexResult result;
  if (w->future().isCanceled()) {
    // if cancelled we cannot call .result()
    result.path = w->property("path").toString();
    result.ok = false;
  } else {
    _processedFiles++;
    result = w->future().result();
    Media& m = result.media;
    if (result.ok) emit mediaProcessed(m);

    VideoContext* v = result.context;
    if (v) {
      if (!v->isHardware() && v->threadCount() > 1) {
        int threads =
            qMin(_params.indexThreads, _videoPool.maxThreadCount() + v->threadCount() - 1);
        _videoPool.setMaxThreadCount(threads);
      }

      delete v;
      result.context = nullptr;
    }

    // todo: indicate when done with a type so caller (engine) can commit early
    // for example there are no images left and long-running video is holding
    // up the commit
  }

  // printf("%c", result.ok ? '+' : 'X');
  // fflush(stdout);

  _activeWork.remove(result.path);
  _work.removeOne(w);
  w->deleteLater();

  if (_activeWork.empty() && _imageQueue.empty() && _videoQueue.empty()) {
    qInfo() << "indexing completed";
    emit scanCompleted();
  }
}

IndexResult Scanner::processImage(const QString& path, const QString& digest,
                                  const QImage& qImg) const {
  IndexResult result;
  result.path = path;

  // opencv throws exceptions
  try {
    const QString shortPath = path.mid(_topDirPath.length() + 1);
    const MessageContext mc(shortPath);
    const CVErrorLogger cvLogger(shortPath);

    int width = qImg.width();
    int height = qImg.height();

    // qImg could be rescaled, store the original w/h
    QString imgText = qImg.text(Media::ImgKey_FileWidth);
    if (!imgText.isEmpty()) {
      bool ok;
      width = imgText.toInt(&ok);
      Q_ASSERT(ok);
      imgText = qImg.text(Media::ImgKey_FileHeight);
      height = imgText.toInt(&ok);
      Q_ASSERT(ok);
    }

    cv::Mat cvImg;
    qImageToCvImg(qImg, cvImg);  // can this use nocopy?

    // note: this should probably only be used
    // for algos without features
    // threshold 20 may be a bit high...
    // todo: setting for indexer autocrop threshold
    if (_params.algos && _params.autocrop) autocrop(cvImg, 20);

    uint64_t dctHash = 0;
    if (_params.algos & (1 << SearchParams::AlgoDCT)) dctHash = dctHash64(cvImg);

    result.media = Media(path, Media::TypeImage, width, height, digest, dctHash);
    Media& m = result.media;

    if (_params.retainImage) m.setImage(qImg);

    if (_params.algos & (1 << SearchParams::AlgoColor)) {
      ColorDescriptor colorDesc;
      ColorDescriptor::create(cvImg, colorDesc);
      m.setColorDescriptor(colorDesc);
    }

    if (_params.algos & (1 << SearchParams::AlgoDCTFeatures | 1 << SearchParams::AlgoCVFeatures)) {
      sizeLongestSide(cvImg, _params.resizeLongestSide);

      KeyPointList keyPoints;
      m.makeKeyPoints(cvImg, _params.numFeatures, keyPoints);

      KeyPointDescriptors kpDescriptors;
      if (_params.algos & (1 << SearchParams::AlgoCVFeatures)) {
        m.makeKeyPointDescriptors(cvImg, keyPoints, kpDescriptors);
        m.setKeyPointDescriptors(kpDescriptors);
      }
      if (_params.algos & (1 << SearchParams::AlgoDCTFeatures)) {
        KeyPointHashList kpHashes;
        m.makeKeyPointHashes(cvImg, keyPoints, kpHashes);
        m.setKeyPointHashes(kpHashes);
      }
    }

    result.ok = true;
    return result;
  } catch (std::exception& e) {
    setError(path, QString("std::exception: ") + e.what());
    return result;
  } catch (...) {
    setError(path, "unknown exception");
    return result;
  }
}

QString Scanner::hash(const QString& path, int type, qint64* bytesRead) {
  QString md5;
  std::unique_ptr<QIODevice> io;
  io.reset(Media(path).ioDevice());
  if (bytesRead) *bytesRead = 0;

  if (!io || !io->open(QIODevice::ReadOnly | QIODevice::Unbuffered)) {
    setError(path, ErrorOpen);
  } else if (type == Media::TypeImage) {
    QByteArray bytes = io->readAll();
    if (bytesRead) *bytesRead = bytes.length();
    if (findJpegMarker(bytes, path)) bytes = jpegPayload(bytes);
    QBuffer buf(&bytes);
    buf.open(QBuffer::ReadOnly);
    md5 = fullMd5(buf);
  } else {
    if (bytesRead) *bytesRead = io->size();
    md5 = fullMd5(*io);
  }

  return md5;
}

QByteArray Scanner::jpegPayload(const QByteArray& bytes) {
  // jpeg markers start with 0xFF and are not followed by 0xFF or 0x00
  auto ptr = reinterpret_cast<const uchar*>(bytes.constData());
  int i = 0;
  int payloadStart = 0;
  const int size = bytes.size();
  while (i < size) {
    if (ptr[i++] == 0xFF) {
      if (i < size) {
        int code = ptr[i];
        if (code != 0xFF && code != 0x00 &&
            ((code >= 0xD0 && code <= 0xDD) || (code >= 0xE0 && code <= 0xEF))) {
          int start = i - 1;
          // qDebug("marker@%d 0xFF%.2x", start, code);

          if (code >= 0xE1 && code <= 0xEF) {
            // skip non-JFIF application segment, (e.g. exif)
            // it could contain jpeg thumbnail and we would get
            // the wrong offset
            int appLen = ptr[i + 1] << 8 | ptr[i + 2];
            // this could overflow if jpeg is corrupt; but
            // top check prevents it
            i += appLen;
          } else if (code == 0xDA && payloadStart == 0) {
            // hash from the first scanline to the end
            payloadStart = start;
          }
        }
        // if we got 0xFF followed by 0xFF, need to check it again
        if (code != 0xFF) i++;
      }
    }
  }

  if (payloadStart) return bytes.mid(payloadStart);

  return bytes;
}

bool Scanner::findJpegMarker(const QByteArray& bytes, const QString& path) {
  bool isJpeg = false;
  if (bytes.length() > 3) {
    auto data = reinterpret_cast<const uint8_t*>(bytes.data());
    int size = bytes.size();

    if (data[0] == 0xFF && data[1] == 0xD8) isJpeg = true;

    if (isJpeg && (data[size - 2] != 0xFF || data[size - 1] != 0xD9))
      setError(path, ErrorJpegTruncated);
  }
  return isJpeg;
}

IndexResult Scanner::processImageFile(const QString& path, const QByteArray& data) const {
  IndexResult result;
  result.path = path;

  QByteArray bytes = data;

  if (bytes.isEmpty()) {
    QIODevice* io = Media(path).ioDevice();
    if (!io || !io->open(QIODevice::ReadOnly)) {
      setError(path, ErrorOpen);
      return result;
    }

    bytes = io->readAll();
    delete io;
  }

  // jpeg needs extra handling
  bool isJpeg = findJpegMarker(bytes, path);

  // decompress, may perform exif orientation
  QImage qImg;
  QSize size(-1, -1);
  if (_params.algos) {
    ImageLoadOptions opt;
    opt.fastJpegIdct = true;
    opt.readScaled = true;
    opt.minSize = _params.resizeLongestSide;
    opt.maxSize = opt.minSize * 1.5;
    qImg = Media::loadImage(bytes, QSize(), path, nullptr, opt);
    if (qImg.isNull()) {
      setError(path, ErrorLoad);
      return result;
    }
  } else {
    // we only want the md5, get size w/o decoding
    QBuffer buffer(&bytes);
    QImageReader reader;
    reader.setDevice(&buffer);
    if (reader.canRead() && reader.supportsOption(QImageIOHandler::Size)) size = reader.size();
  }

  // hash the payload of the jpeg, ignoring exif
  if (isJpeg) bytes = jpegPayload(bytes);

  // md5
  QString digest = QString(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());

  if (!_params.algos) {
    result.media = Media(path, Media::TypeImage, size.width(), size.height(), digest, 0);
    result.ok = true;
    return result;
  }

  // release the memory now, process will take a while and we could use it
  bytes.clear();
  result = processImage(path, digest, qImg);
  return result;
}

VideoContext* Scanner::initVideoProcess(const QString& path, bool tryGpu, int cpuThreads) const {
  VideoContext* video = new VideoContext;

  int deviceIndex = -1;

  VideoContext::DecodeOptions opt;
  opt.threads = cpuThreads;
  opt.gpu = tryGpu;
  opt.deviceIndex = deviceIndex;
  opt.maxH = 128;  // need just enough to detect/crop borders
  opt.maxW = 128;
  opt.fast = true;  // enable speeds ok for indexing
  opt.gray = true;  // only look at the "Y" channel, dct algo is grayscale
  if (video->open(path, opt) < 0) {
    setError(path, ErrorLoad);
    delete video;
    return nullptr;
  }

  // qDebug("decode using %s, threads=%d\n\n", video->isHardware() ? "gpu" : "cpu",
  //       video->threadCount());

  return video;
}

IndexResult Scanner::processVideo(VideoContext* video) const {
  const QString context = video->path().mid(_topDirPath.length() + 1);
  const CVErrorLogger cvLogger("processVideo:" + context);
  const MessageContext mc(context);

  IndexResult result;
  result.path = video->path();
  result.ok = false;
  result.context = video;

  QString md5 = "";
  {
    QFile f(result.path);
    if (!f.open(QFile::ReadOnly)) {
      setError(result.path, ErrorOpen);
      return result;
    }
    md5 = fullMd5(f);
  }

  result.media = Media(result.path, Media::TypeVideo, 0, 0, md5, 0);
  Media& m = result.media;

  m.setWidth(video->width());
  m.setHeight(video->height());

  if (!(_params.algos & (1 << SearchParams::AlgoVideo))) {
    // qWarning("video index disabled, storing md5 and metadata");
  } else {
    int64_t start = QDateTime::currentMSecsSinceEpoch();

    VideoIndex index;
    m.makeVideoIndex(*video, _params.videoThreshold, index);
    m.setVideoIndex(index);

    int64_t end = QDateTime::currentMSecsSinceEpoch();

    VideoContext::Metadata d = video->metadata();
    float framePixelsPerMs =
        (d.duration * d.frameRate * video->width() * video->height()) / (end - start);

    qDebug("perf codec=%s bitrate=%d pixels/ms=%.1f", qUtf8Printable(d.videoCodec), d.videoBitrate,
           double(framePixelsPerMs));
  }

  result.ok = true;
  return result;
}

IndexResult Scanner::processVideoFile(const QString& path) const {
  VideoContext* video = initVideoProcess(path, _params.useHardwareDec, _params.decoderThreads);

  if (!video) {
    IndexResult result;
    result.path = path;
    result.ok = false;
    return result;
  }

  IndexResult result = processVideo(video);

  result.context = nullptr;
  delete video;

  return result;
}

#include "paramsdefs.h"

IndexParams::IndexParams() {
  static const QVector<NamedValue> emptyValues;
  static const QVector<int> emptyRange;
  // static const QVector<int> percent{1, 100};
  static const QVector<int> positive{0, INT_MAX};
  static const QVector<int> nonzero{1, INT_MAX};

  int counter = 0;
  {
    // note: identical to SearchParams::algo except bit shift
    static const QVector<NamedValue> bits{
        {1 << SearchParams::AlgoDCT, "dct", "DCT image hash"},
        {1 << SearchParams::AlgoDCTFeatures, "fdct", "DCT image hashes of features"},
        {1 << SearchParams::AlgoCVFeatures, "orb", "ORB descriptors of features"},
        {1 << SearchParams::AlgoColor, "color", "Color histogram"},
        {1 << SearchParams::AlgoVideo, "video", "DCT image hashes of video frames"}};
    add({"algos", "Enabled algorithms", Value::Flags, counter++, SET_FLAGS("algos", algos, bits),
         GET(algos), GET_CONST(bits), NO_RANGE});
  }

  {
    static const QVector<NamedValue> bits{{TypeImage, "i", "Image files"},
                                          {TypeVideo, "v", "Video files"},
                                          {TypeAudio, "a", "Audio files"}};
    add({"types", "Enabled media types", Value::Flags, counter++, SET_FLAGS("types", types, bits),
         GET(types), GET_CONST(bits), NO_RANGE});
  }

  add({"dirs", "Enable indexing of subdirectories", Value::Bool, counter++, SET_BOOL(recursive),
       GET(recursive), NO_NAMES, NO_RANGE});

  add({"ignored", "Log all ignored files", Value::Bool, counter++, SET_INT(showIgnored),
       GET(showIgnored), NO_NAMES, NO_RANGE});

  add({"links", "Follow symlinks to files and directories", Value::Bool, counter++,
       SET_BOOL(followSymlinks), GET(followSymlinks), NO_NAMES, NO_RANGE});

  add({"resolve", "Resolve symlinks, store canonical path if possible", Value::Bool, counter++,
       SET_BOOL(resolveLinks), GET(resolveLinks), NO_NAMES, NO_RANGE});

  add({"dups", "Follow duplicate inodes (hard links, soft links etc)", Value::Bool, counter++,
       SET_BOOL(dupInodes), GET(dupInodes), NO_NAMES, NO_RANGE});

  add({"ljf", "Estimate job cost and process longest jobs first", Value::Bool, counter++,
       SET_BOOL(estimateCost), GET(estimateCost), NO_NAMES, NO_RANGE});

  add({"dryrun", "Dry run, only show what would be done", Value::Bool, counter++, SET_BOOL(dryRun),
       GET(dryRun), NO_NAMES, NO_RANGE});

  add({"fsize", "Minimum file size in bytes, ignore smaller files", Value::Int, counter++,
       SET_INT(minFileSize), GET(minFileSize), NO_NAMES, GET_CONST(positive)});

  add({"bsize", "Size of database write batches", Value::Int, counter++, SET_INT(writeBatchSize),
       GET(writeBatchSize), NO_NAMES, GET_CONST(nonzero)});

  add({"crop", "Enable border detect/crop of video", Value::Bool, counter++, SET_BOOL(autocrop),
       GET(autocrop), NO_NAMES, NO_RANGE});

  add({"nfeat", "Number of features per image", Value::Int, counter++, SET_INT(numFeatures),
       GET(numFeatures), NO_NAMES, GET_CONST(positive)});

  add({"rsize", "Dimension for prescaling images before processing", Value::Int, counter++,
       SET_INT(resizeLongestSide), GET(resizeLongestSide), NO_NAMES, GET_CONST(nonzero)});

  add({"vht", "Video index threshold for discarding hashes", Value::Int, counter++,
       SET_INT(videoThreshold), GET(videoThreshold), NO_NAMES, GET_CONST(nonzero)});

  add({"gpu", "Enable gpu video decoding (Nvidia)", Value::Bool, counter++,
       SET_BOOL(useHardwareDec), GET(useHardwareDec), NO_NAMES, NO_RANGE});

  add({"decthr", "Max threads for video decoding (0==auto)", Value::Int, counter++,
       SET_INT(decoderThreads), GET(decoderThreads), NO_NAMES, GET_CONST(positive)});

  add({"idxthr", "Max threads for all jobs (0==auto)", Value::Int, counter++, SET_INT(indexThreads),
       GET(indexThreads), NO_NAMES, GET_CONST(positive)});

  add({"gputhr", "Max decoders per gpu", Value::Int, counter++, SET_INT(gpuThreads),
       GET(gpuThreads), NO_NAMES, GET_CONST(nonzero)});
}
