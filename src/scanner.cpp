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
  if (_params.decoderThreads <= 0) _params.decoderThreads = qMin(_params.indexThreads, QThread::idealThreadCount());

#ifdef Q_OS_WIN
  if (!_params.dupInodes)
    qWarning() << "duplicate inode check (-i.dups 0) can be extremely slow on network volumes";
#endif

  if (_params.decoderThreads > _params.indexThreads) {
    qWarning() << "index threads must be >= decoder threads";
    _params.decoderThreads = _params.indexThreads;
  }

  // even if this is false, underutilization is still possible by mixing
  // single-threaded and parallel codecs
  if (_params.indexThreads % _params.decoderThreads != 0)
    qWarning() << "index threads are not a multiple of decoder threads, expect underutilization";

  // TODO: subdirectory limiter for large indexes
  // if (!_params.subdir.isEmpty())

  _gpuPool.setMaxThreadCount(_params.gpuThreads);

  _topDirPath = path;
  _existingFiles = 0;
  _ignoredFiles = 0;
  _modifiedFiles = 0;
  _processedFiles = 0;
  _queuedFiles = 0;
  _modifiedSince = modifiedSince;
  _inodes.clear();
  _startTime = QDateTime::currentDateTime();

  // index zipped files for the zip modtime optimization
  QMap<QString, QStringList> zipFiles;
  for (const QString& path : qAsConst(expected)) {
    if (!Media::isArchived(path)) continue;
    QString zipFile;
    Media::archivePaths(path, &zipFile);
    zipFiles[zipFile].append(path);
  }

  readDirectory(path, zipFiles, expected);
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

      // TODO: cost could be better by considering codec/decoder
      VideoContext v;
      if (v.open(path) < 0) continue;

      VideoContext::Metadata d = v.metadata();
      cost[path] = d.frameRate * d.duration * d.frameSize.width() * d.frameSize.height();
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

  _queuedFiles = _imageQueue.count() + _videoQueue.count();
  if (_imageQueue.count() > 0 || _videoQueue.count() > 0) {
    qInfo() << "scan completed, removing" << expected.count() << "file(s), adding"
            << _imageQueue.count() << "image(s)," << _videoQueue.count() << "video(s)";
    QTimer::singleShot(1, this, &Scanner::processOne);
  } else {
    qInfo() << "scan completed, no changes";
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

    // TODO: setting for ignored folder names
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
      QString::asprintf("<NC>checking %s$<PL> new{i:%lld v:%lld} ignored:%d modified:%d ok:%d <EL>%s",
                        qUtf8Printable(_topDirPath), _imageQueue.count(), _videoQueue.count(),
                        _ignoredFiles, _modifiedFiles, _existingFiles, qUtf8Printable(elided));
  qInfo().noquote() << status;
}

void Scanner::readDirectory(const QString& dirPath, const QMap<QString,QStringList> zipFiles, QSet<QString>& expected) {
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
      // metadataChangeTime() could be used but will re-index
      // changes that don't modify the file content
      if (entry.lastModified() < _modifiedSince) {
        expected.remove(path);
        _existingFiles++;
        continue;
      }
      _modifiedFiles++;

       // files with invalid modtimes will always be re-indexed
      if (entry.lastModified() > _startTime)
        qWarning() << "future modtime:" << path;
    }

    if (entry.isFile()) {
      //            printf("considering: images=%d videos=%d %s \r",
      //                   _imageQueue.count(), _videoQueue.count(),
      //                   qPrintable(path));
      //            fflush(stdout);
      if (_activeWork.contains(path)) {
        qDebug() << "skipping active work" << path;
        continue;
      }

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
        // skip deep scan of zip files
        // use metadataChangeTime() since lastModified() will not detect the case
        // where a zip is replaced with an older zip with the same name
        // FIXME: metadataChangeTime() may not be available on all filesystems, must validate!
        if (entry.metadataChangeTime() < _modifiedSince) {
          int removed = 0;
          const auto it = zipFiles.find(path);
          if (it != zipFiles.end())
            for (auto& path : it.value()) {
              expected.remove(path);
              removed++;
            }

          _existingFiles += removed;
          if (removed > 0) continue;
        }
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
    for (int i = 0; i < dirs.count(); i++) readDirectory(dirs[i], zipFiles, expected);
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

  // TODO: clear started slow-running jobs (process video)

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
    const int pendingFiles = _imageQueue.count() + _videoQueue.count() + _activeWork.count();

    if (_queuedFiles > 0) {
      int finished = (_queuedFiles - pendingFiles);
      int progress = finished * 100 / _queuedFiles;

      QStringList vList;
      if (_videoProgress.count() > 0)
        for (int percent : qAsConst(_videoProgress).values()) {
          if (percent == 100) continue; // we don't need to see quick jobs
          vList += qq("%1%").arg(percent);
        }

      const QString vProgress = vList.count() ? ", videos{" + vList.join(',') + '}' : "";

      int gpuJobs = _gpuPool.activeThreadCount();
      int cpuJobs = QThreadPool::globalInstance()->activeThreadCount();
      int threads = totalThreadCount();

      QString runningStatus;
      if (gpuJobs)
        runningStatus = qq("running{gpu:%1 cpu:%2}").arg(gpuJobs).arg(cpuJobs);
      else
        runningStatus = qq("running:%1").arg(cpuJobs);

      QString status = QString::asprintf(
          "<NC>indexing %s$<PL> waiting{i:%lld v:%lld} %s threads:%d "
          "%d%% %d indexed<EL>%s",
          qUtf8Printable(_topDirPath), _imageQueue.count(), _videoQueue.count(),
          qUtf8Printable(runningStatus),
          threads,
          progress, finished, qUtf8Printable(vProgress));
      qInfo().noquote() << status;

      QStringList vDone;
      for (const QString& k : qAsConst(_videoProgress).keys())
        if (_videoProgress.value(k) >= 100)
          vDone += k;
      for (const QString& k : qAsConst(vDone))
        _videoProgress.remove(k);
    }

    if (pendingFiles == 0) {
      timer.stop();
      loop.exit(0);
      return;
    }
    timer.setInterval(100);
  });

  timer.setInterval(1);
  timer.start();
  loop.exec();
}

int Scanner::totalThreadCount() const {
//  int extraThreads = 0;
//  for (const auto* w : _work)
//    if (w->future().isRunning())
//      extraThreads += w->property("childThreads").toInt();

  return QThreadPool::globalInstance()->activeThreadCount() + _extraThreads;
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
    QString path;         // file path
    int childThreads = 0; // additional threads used by job
    if (!_videoQueue.empty()) {
      path = _videoQueue.first();
      const MessageContext mc(path.mid(_topDirPath.length() + 1));

      const bool tryGpu = _params.useHardwareDec &&
                          _gpuPool.activeThreadCount() < _gpuPool.maxThreadCount();

      const int activeThreads = totalThreadCount();
      const int availThreads = _params.indexThreads - activeThreads;
      Q_ASSERT(availThreads >= 0);

      // try to process even if we don't have enough threads, which will max
      // out the cpu now, at the expense of possibly underutilizing later
      // BUG: after some time #jobs > indexThreads/decoderThreads
      //      even when all jobs are mt
      int cpuThreads = qMin(availThreads, _params.decoderThreads);

      // last video can have all the threads to reduce starvation
      if (_videoQueue.count() == 1)
        cpuThreads = availThreads;

      // qWarning() << "threads" << activeThreads << availThreads << cpuThreads;

      if (tryGpu || cpuThreads > 0) {
        VideoContext* v = initVideoProcess(path, tryGpu, cpuThreads);
        if (v) {
          QThreadPool* pool = nullptr;
          if (v->isHardware()) {
            pool = &_gpuPool;
          } else if (cpuThreads > 0) {
            pool = QThreadPool::globalInstance();

            // subtract the job thread, which means actual threads used are greater
            // - if indexThreads is divisible by decoderThreads we get expected number of parallel jobs
            // - the job thread isn't doing much compared to the decoder
            // - the total utilization is usually much less than 100% of threadCount threads.
            childThreads += v->threadCount() - 1;
          }

          if (!pool && tryGpu) {
            // stop gpu from retrying the same file
            // FIXME: disable gpu after too many fails
            // FIXME: search queue for possibly compatible files
            _videoQueue.removeFirst();
            _videoQueue.append(path);
          }

          if (pool) {
            f = QtConcurrent::run(pool, &Scanner::processVideo, this, v);
            _videoQueue.removeFirst();
          }
        } else
          _videoQueue.removeFirst();  // failed to open
      }
    } else if (!_imageQueue.empty()) {
      path = _imageQueue.takeFirst();
      _queuedWork.remove(path);
      f = QtConcurrent::run(&Scanner::processImageFile, this, path, QByteArray());
      queuedImage = true;
    }

    if (!f.isCanceled()) {
      _activeWork.insert(path);
      QFutureWatcher<IndexResult>* w = new QFutureWatcher<IndexResult>;
      connect(w, SIGNAL(started()), this, SLOT(processStarted()));
      connect(w, SIGNAL(finished()), this, SLOT(processFinished()));
      w->setFuture(f);
      w->setProperty("path", path);
      w->setProperty("childThreads", childThreads);
      _work.append(w);
    }
  }

  // if the queues are not empty, call processOne again.
  // fixme: seems delay is no longer needed
  int delay = -1;
  if (!_videoQueue.empty())
    delay = 100;
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

void Scanner::processStarted() {
  auto w = dynamic_cast<QFutureWatcher<IndexResult>*>(sender());
  if (!w) return;

  _extraThreads += w->property("childThreads").toInt();
}

void Scanner::processFinished() {
  auto w = dynamic_cast<QFutureWatcher<IndexResult>*>(sender());
  if (!w) return;

  _extraThreads -= w->property("childThreads").toInt();
  Q_ASSERT(_extraThreads >= 0);

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
      delete v;
      result.context = nullptr;
    }

    // TODO: indicate when done with a type so caller (engine) can commit early
    // for example there are no images left and long-running video is holding
    // up the commit
  }

  // printf("%c", result.ok ? '+' : 'X');
  // fflush(stdout);

  _activeWork.remove(result.path);
  _work.removeOne(w);
  w->deleteLater();

  if (_activeWork.empty() && _imageQueue.empty() && _videoQueue.empty()) {
    qDebug() << "indexing completed";
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

    cv::Mat cvColor, cvGray;
    qImageToCvImg(qImg, cvColor);  // fixme: can this use nocopy?
    grayscale(cvColor, cvGray);

    // note: this should probably only be used for algos without features
    // threshold 20 may be a bit high
    // TODO: setting for indexer autocrop threshold
    if (_params.algos && _params.autocrop) autocrop(cvGray, 20);

    uint64_t dctHash = 0;
    if (_params.algos & (1 << SearchParams::AlgoDCT)) dctHash = dctHash64(cvGray);

    result.media = Media(path, Media::TypeImage, width, height, digest, dctHash);
    Media& m = result.media;

    if (_params.retainImage) m.setImage(qImg);

    if (_params.algos & (1 << SearchParams::AlgoColor)) {
      ColorDescriptor colorDesc;
      ColorDescriptor::create(cvColor, colorDesc);
      m.setColorDescriptor(colorDesc);
    }

    if (_params.algos & (1 << SearchParams::AlgoDCTFeatures | 1 << SearchParams::AlgoCVFeatures)) {
      sizeLongestSide(cvGray, _params.resizeLongestSide);

      KeyPointList keyPoints;
      m.makeKeyPoints(cvGray, _params.numFeatures, keyPoints);

      KeyPointDescriptors kpDescriptors;
      if (_params.algos & (1 << SearchParams::AlgoCVFeatures)) {
        m.makeKeyPointDescriptors(cvGray, keyPoints, kpDescriptors);
        m.setKeyPointDescriptors(kpDescriptors);
      }
      if (_params.algos & (1 << SearchParams::AlgoDCTFeatures)) {
        KeyPointHashList kpHashes;
        m.makeKeyPointHashes(cvGray, keyPoints, kpHashes);
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
  opt.fast = true; // enable speeds ok for indexing
  opt.gray = true; // only look at the "Y" channel, dct algo is grayscale
  if (video->open(path, opt) < 0) {
    setError(path, ErrorLoad);
    delete video;
    return nullptr;
  }

  Q_ASSERT(video->threadCount() > 0); // required for thread counting

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

    const auto progressCb = [this,m](int percent) {
      Scanner* nonConst = const_cast<Scanner*>(this);            // unfortunate
      QMetaObject::invokeMethod(nonConst, [nonConst,m,percent] { // call to scanner's thread
        nonConst->_videoProgress.insert(m.path(), percent);
      });
    };
    VideoIndex index;
    m.makeVideoIndex(*video, _params.videoThreshold, index, progressCb);
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

  add({"ignored", "Log all ignored files", Value::Bool, counter++, SET_BOOL(showIgnored),
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
