/* Media file container and utilities
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
#include "media.h"
#include "cvutil.h"
//#include "opencv2/highgui/highgui.hpp"

#include "hamm.h"
#include "ioutil.h"
#include "qtutil.h"
#include "videocontext.h"

//#include <QtNetwork/QtNetwork>  // httpRequest
//#include <memory>

#include "exiv2/exiv2.hpp"
#include "quazip/quazip.h"
#include "quazip/quazipfile.h"

void Media::setDefaults() {
  _id = 0;
  _width = -1;
  _height = -1;
  _compressionRatio = 1;
  _origSize = 0;
  _matchFlags = 0;
  _dctHash = 0;
  _score = -1;
  _position = -1;
  _type = TypeImage;
}

Media::Media(const QImage& qImg, int originalSize) {
  setDefaults();
  _type = TypeImage;

  _img = qImg;
  _width = _img.width();
  _height = _img.height();
  _origSize = originalSize;
  _path = QString("qimage://%1").arg(qImg.cacheKey(), 0, 16, QChar('0'));

  imageHash();
}

void Media::imageHash() {
  if (!_img.isNull()) {
    // note: don't take a copy since we allocated it ourselves
    try {
      cv::Mat cvImg;
      qImageToCvImg(_img, cvImg);

      _dctHash = dctHash64(cvImg);
      ::colorDescriptor(cvImg, _colorDescriptor);
    } catch (...) {
      qCritical("hash failed with exception");
    }
  } else
    qCritical("_img is unset, nothing to do");
}

Media::Media(const QString& path, int type, int width, int height) {
  setDefaults();
  _path = path;
  _type = type;
  _width = width;
  _height = height;
}

Media::Media(const QString& path, int type, int width, int height,
             const QString& md5, const uint64_t dctHash) {
  setDefaults();
  _type = type;
  _path = path;
  _width = width;
  _height = height;
  _md5 = md5;
  _dctHash = dctHash;
}

Media::Media(const QString& path, int type, int width, int height,
             const QString& md5, const uint64_t dctHash,
             const ColorDescriptor& colorDesc, const KeyPointList& keyPoints,
             const KeyPointDescriptors& descriptors) {
  setDefaults();
  _path = path;
  _type = type;
  _width = width;
  _height = height;
  _md5 = md5;
  _dctHash = dctHash;
  _colorDescriptor = colorDesc;
  _keyPoints = keyPoints;
  _descriptors = descriptors;
}

void Media::print(const Media& media) {
  qInfo("------------------------------------");
  qInfo() << "id    =" << media.id();
  qInfo() << "path  =" << media.path();
  qInfo() << "md5   =" << media.md5();
  qInfo() << "dct   =" << Qt::hex << media.dctHash();
  qInfo() << "size  =" << media.width() << "x" << media.height();
  qInfo() << "type  =" << media.type();
  qInfo() << "isZip =" << media.isArchived();
  qInfo("score = %d rangeIn={%d, %d, %d}", media.score(),
        media.matchRange().srcIn, media.matchRange().dstIn,
        media.matchRange().len);

  qInfo() << "image =" << media.image();
  qInfo() << "dataSz=" << media.data().size();
  qInfo() << "cmpSz =" << media.originalSize();
  qInfo() << "memSz =" << media.memSize();
  qInfo() << "cmpRto=" << media.compressionRatio();

  qInfo() << "attr  =" << media.attributes();
  qInfo() << "mime  =" << media.contentType();
  qInfo() << "roi   =" << media.roi();
  qInfo() << "xform =" << media.transform();
  qInfo() << "pos   =" << media.position();
  qInfo() << "color =" << media.matchColor();
  qInfo() << "flags =" << Qt::hex << media.matchFlags();
}

void Media::printGroup(const MediaGroup& group) {
  qInfo("====================================");
  for (const Media& m : group) print(m);
}

void Media::printGroupList(const MediaGroupList& list) {
  for (const MediaGroup& group : list) printGroup(group);
}

bool Media::groupCompareByContents(const MediaGroup& s1, const MediaGroup& s2) {
  if (s1.count() == s2.count()) {
    QSet<QString> paths;
    for (const Media& m : s1) paths.insert(m.path());

    int count = 0;
    for (const Media& m : s2)
      if (!paths.contains(m.path()))
        return false;
      else
        count++;

    return count == s1.count();
  }

  return false;
}

int Media::indexInGroupByPath(MediaGroup& group, const QString& path) {
  for (int i = 0; i < group.size(); i++)
    if (group[i].path() == path) return i;
  return -1;
}

void Media::mergeGroupList(MediaGroupList& list) {
  // merge 1-connected matches
  // e.g. if a matches b and b matches c, then a matches c;
  // fixme: probably want to find something better than n*n
  for (int i = 0; i < list.count(); i++) {
    MediaGroup& a = list[i];
    for (int j = 0; j < list.count(); j++)
      if (i != j) {
        MediaGroup& b = list[j];
        if (b.count() > 0 && a.contains(b[0])) {
          // merge b into a, the match scores could be bogus now
          for (int k = 1; k < b.count(); k++)
            if (!a.contains(b[k])) a.append(b[k]);
          b.clear();
          std::sort(a.begin(), a.end());
        }
      }
  }

  // remove empty sets resulting from merge
  MediaGroupList final;
  for (MediaGroup& g : list)
    if (!g.isEmpty()) final.append(g);
  list = final;
}

void Media::expandGroupList(MediaGroupList& list) {
  MediaGroupList expanded;
  for (auto& g : list)
    for (int i = 1; i < g.count(); ++i) expanded.append({g[0], g[i]});
  list = expanded;
}

void Media::sortGroupList(MediaGroupList& list, const QString& key) {
  QCollator collator;
  auto f = propertyFunc(key);
  auto numericStringCmp = [&](const MediaGroup& a, const MediaGroup& b) {
    if (a.count() <= 0) return true;
    if (b.count() <= 0) return false;
    const QString sa = f(a.first()).toString();
    const QString sb = f(b.first()).toString();
    return 0 > qNumericSubstringCompare(collator, sa, sb);
  };
  if (key == "path")
    std::stable_sort(list.begin(), list.end(), numericStringCmp);
  else
    qFatal("unsupported sort key \"%s\"", qPrintable(key));
}

void Media::sortGroup(MediaGroup& group, const QString& key, bool reverse) {
  auto f = propertyFunc(key);
  QCollator collator;
  collator.setNumericMode(true);

  auto cmp = [&](const Media& a, const Media& b) {
      const QString sa = f(a).toString();
      const QString sb = f(b).toString();
      return reverse ^ (0 > qNumericSubstringCompare(collator, sa, sb));
    };

  std::stable_sort(group.begin(), group.end(), cmp);
}

static QString greatestPrefix(const QStringList& list) {
  QString prefix;
  const int count = list.count();
  if (count > 0) {
    prefix = list[0];
    for (int i = 1; i < count; ++i) {
      const QString& path = list[i];
      const int len = std::min(path.length(), prefix.length());
      int j = 0;
      for (j = 0; j < len; ++j)
        if (prefix[j] != path[j]) break;
      prefix.truncate(j);
    }
  }

  // remove trailing part up to next directory (like dirname)
  int j = prefix.length()-1;
  for (; j >= 0; --j)
    if (prefix[j] == '/') break;
  prefix.truncate(j+1);

  return prefix;
}

QString Media::greatestPathPrefix(const MediaGroupList& gl) {
  QStringList list;
  // fixme: path could be http:// or @tag or data-url://
  for (const MediaGroup& g : gl)
    list.append(greatestPathPrefix(g));
  return greatestPrefix(list);
}

QString Media:: greatestPathPrefix(const MediaGroup& group) {
  // fixme: path could be http:// or @tag or data-url://
  QStringList list;
  for (const Media& m : group)
    if (QFileInfo(m.path()).isAbsolute()) list.append(m.path());
  return greatestPrefix(list);
}

MediaGroupList Media::splitGroup(const MediaGroup& group, int chunkSize) {
  MediaGroupList list;
  for (int i = 0; i < group.count(); i += chunkSize)
    list.append(group.mid(i, chunkSize));
  return list;
}

std::function<QVariant(const QVariant&)> Media::unaryFunc(const QString& expr) {

    // unary functions can be chained
    const QStringList calls = expr.split(lc('#'));
    if (calls.count() > 1) {
      QVector< std::function<QVariant(const QVariant&)> > chain;
      for (auto& expr : calls)
        chain.append(unaryFunc(expr)); // recursion ok, # is stripped out
      return [chain](const QVariant& v) {
        QVariant r = v;
        for (auto& f : qAsConst(chain))
          r = f(r);
        return r;
      };
    }

    const QStringList call = expr.split(",");
    QString fn = call[0];

    // string functions
    if (fn == "mid") {
      if (call.count() != 3)
        qFatal("mid() has two integer arguments (begin, length)");
      bool ok;
      int start = call[1].toInt(&ok);
      if (!ok)
        qFatal("first argument to mid() is not an integer");
      int len = call[2].toInt(&ok);
      if (!ok)
        qFatal("second argument to mid() is not an integer");
      return [start,len](const QVariant& v) {
        const QString k = v.toString();
        return QVariant(k.mid(start, len));
      };
    }
    if (fn == "trim") {
      if (call.count() != 1)
        qFatal("trim() has no arguments");
      return [](const QVariant&v) { return v.toString().trimmed(); };
    }
    if (fn == "upper") {
      if (call.count() != 1)
        qFatal("upper() has no arguments");
      return [](const QVariant& v) { return v.toString().toUpper(); };
    }
    if (fn == "lower") {
      if (call.count() != 1)
        qFatal("lower() has no arguments");
      return [](const QVariant& v) { return v.toString().toLower(); };
    }
    if (fn == "title") {
      if (call.count() != 1)
        qFatal("title() has no arguments");
      return [](const QVariant& v) {
        auto s = v.toString().toLower();
        if (s.length() > 0) s[0] = s[0].toUpper();
        return s;
      };
    }
    if (fn == "pad") {
      if (call.count() != 2)
        qFatal("pad() takes one integer argument (length)");
      bool ok;
      const int len = call[1].toInt(&ok);
      if (!ok)
        qFatal("pad() length argument is not an integer");
      return [len](const QVariant& v) {
        bool ok;
        int num = v.toInt(&ok);
        if (!ok) qFatal("pad() input is not integer");
        return QString("%1").arg(num, len, 10, QLatin1Char('0'));
      };
    }
    // list functions
    if (fn == "split") {
      if (call.count() != 2)
        qFatal("split() takes one string argument (separator)");
      auto& arg = call[1];
      QRegularExpression exp(arg);
      if (exp.isValid())
        return [exp](const QVariant& v) {
          return v.toString().split(exp);
        };

      return [arg](const QVariant& v) {
        return v.toString().split(arg);
      };
    }
    if (fn == "join") {
      if (call.count() != 2)
        qFatal("join() takes one string argument (glue)");
      auto& arg = call[1];
      return [arg](const QVariant& v) {
        return v.toStringList().join(arg);
      };
    }
    if (fn == "camelsplit") {
      if (call.count() != 1)
        qFatal("camelsplit() takes no arguments");

      return [](const QVariant& v) {
        static const QRegularExpression exp("[a-z][A-Z]");
        QStringList parts;
        QString str = v.toString();
        int pos = str.indexOf(exp);
        while (pos >= 0) {
          parts.append(str.mid(0, pos+1));
          str = str.mid(pos+1);
          pos = str.indexOf(exp);
        }
        if (!str.isEmpty())
          parts.append(str);
        return parts;
      };
    }
    if (fn == "push") {
      if (call.count() != 2)
        qFatal("push() takes one string argument (value)");
      auto& arg = call[1];
      return [arg](const QVariant& v) {
        auto r = v.toList();
        r.append(arg);
        return r;
      };
    }
    if (fn == "pop") {
      if (call.count() != 1)
        qFatal("pop() has no arguments");
      return [](const QVariant& v) {
        auto r = v.toList();
        r.removeLast();
        return r;
      };
    }
    if (fn == "shift") {
      if (call.count() != 1)
        qFatal("shift() has no arguments");
      return [](const QVariant& v) {
        auto r = v.toList();
        r.removeFirst();
        return r;
      };
    }
    if (fn == "peek") {
      if (call.count() != 2)
        qFatal("peek() takes one argument (index)");
      bool ok;
      int index = call.at(1).toInt(&ok);
      if (!ok)
        qFatal("argument to peek() is not an integer");
      return [index](const QVariant& v) {
        auto r = v.toList();
        int i = index;
        if (i < 0) // negative index from end
          i += r.count();
        if (i < 0 || i >= r.count())
          qFatal("argument to peek() is invalid index");
        return r.at(i);
      };
    }
    if (fn == "foreach") {
      if (call.count() < 2)
        qFatal("foreach() takes at least one function expression (<func>[|<func>|<func>...]])");
      // recombine call to associate ',' correctly
      const QStringList expr = call.sliced(1).join(",").split("|");
      QVector< std::function<QVariant(const QVariant&) > > functions;
      for (auto& e : expr)
        functions.append( unaryFunc(e) );
      return [functions](const QVariant& v) {
        QVariantList list = v.toList();
        for (auto& v : list)
          for (auto& f : qAsConst(functions))
            v = f(v);
        return list;
      };
    }

    // math functions
    if (fn == "add") {
      if (call.count() != 2)
        qFatal("add() takes one integer argument");
      bool ok;
      int num = call[1].toInt(&ok);
      if (!ok)
        qFatal("add() argument is not an integer");
      return [num](const QVariant& v) { return v.toInt() + num; };
    }

    // date function shortcuts append an argument
    auto dateCall = call;
    if (fn == "year") {
      fn = "date";
      dateCall.append("yyyy");
    } else if (fn == "month") {
      fn = "date";
      dateCall.append("yyyy-MM");
    } else if (fn == "day") {
      fn = "date";
      dateCall.append("yyyy-MM-dd");
    }

    if (fn == "date") {
      if (dateCall.count() != 2)
        qFatal("date() takes one string argument (QDateTime format)");
      const QString dateFormat = dateCall.at(1);
      return [dateFormat](const QVariant& v) {
        QDateTime d = v.toDateTime(); // should work for exif date tags
        if (!d.isValid()) d = QDateTime::fromString(v.toString(), Qt::DateFormat::ISODate);
        return d.toString(dateFormat);
      };
    }
    qFatal("invalid function: %s", qPrintable(fn));
}

QList<QPair<const char*, const char*>> Media::propertyList() {
  return {
      {"id", "unique id"},
      {"isValid", "1 if id != 0"},
      {"md5", "checksum"},
      {"type", "1=image,2=video,3=audio"},
      {"path", "file path"},
      {"parentPath", "archivePath if archive, or dirPath"},
      {"dirPath", "parent directory path"},
      {"relPath", "relative file path to cwd"},
      {"name", "file name"},
      {"completeBaseName", "file name w/o suffix"},
      {"archivePath", "archive/zip path, or empty if non-archive"},
      {"suffix", "file suffix"},
      {"isArchived", "1 if archive member"},
      {"archiveCount", "number of archive members"},
      {"contentType", "mime content type"},
      {"width", "pixel width"},
      {"height", "pixel height"},
      {"aspectRatio", "pixel width/height"},
      {"resolution", "width*height"},
      {"res", "max of width, height"},
      {"compressionRatio", "resolution / file size"},
      {"isWeed", "1 if tagged as weed (after query)"},
      {"score", "match score"},
      {"matchFlags", "match flags (Media::matchFlags)"},
      {"exif#<tag1[,tagN]>",
       "comma-separated EXIF tags, first available tag is used (\"Exif.\" prefix optional)"},
      {"iptc#<tag1[,tagN]>",
       "comma-separated IPTC tags, first available tag is used (\"Iptc.\" prefix optional)"},
      {"xmp#<tag1[,tagN]>",
       "comma-separated XMP tags, first available tag is used (\"Xmp.\" prefix optional)"},
      {"ffmeta#<tag1[,tagN]", "comma-separated ffmpeg metadata tags, first available tag is used"},
  };
}

std::function<QVariant(const Media&)> Media::propertyFunc(const QString& expr) {

  static QHash<QString, QVariant> propCache;
  static QMutex* cacheMutex = new QMutex;

  std::function<QVariant(const Media&)> select;

  // lookup table for stateless properties
#define PAIR(prop) { #prop ,  [](const Media& m) { return m.prop(); } }
  static const QHash<QString,decltype(select)> props({
      PAIR(id),
      PAIR(isValid),
      PAIR(md5),
      PAIR(type),
      PAIR(path),
      PAIR(dirPath),
      PAIR(name),
      PAIR(completeBaseName),
      PAIR(suffix),
      PAIR(score),
      PAIR(width),
      PAIR(height),
      PAIR(aspectRatio),
      PAIR(resolution),
      PAIR(compressionRatio),
      PAIR(contentType),
      PAIR(matchFlags),
      PAIR(isArchived),
      PAIR(archiveCount),
      PAIR(isWeed),
      { "res",     [](const Media& m) { return qMax(m.width(),m.height()); } },
      { "relPath", [](const Media& m) { return QDir().relativeFilePath(m.path()); }},
      { "archive", [](const Media& m) {
          if (m.isArchived()) {
            QString a;
            m.archivePaths(&a);
            return a;
          }
          return QString();
      }},
      { "parentPath", [](const Media& m) {
          if (m.isArchived()) {
            QString a;
            m.archivePaths(&a);
            return a;
          }
          return m.dirPath();
      }},

      /// todo: attr(), VideoContext::metadata
  });

  // prop#args#unaryFunc[#unaryFunc]...
  // prop#unaryFunc[#unaryFunc]...
  QStringList args = expr.split("#");
  const QString field = args.front();
  args.pop_front();

  auto it = props.find(field);
  if (it != props.end()) {
    select = *it;
  }
  else if (field == "exif" || field == "iptc" || field == "xmp") {
    if (args.count() == 0) qFatal("exif/iptc/xmp require tag name(s)");
    QStringList exifKeys = args.front().split(",");
    args.pop_front();

    select = [=](const Media& m) {
      const QString cacheKey = m.path() + ":" + field + exifKeys.join(",");
      {
        QMutexLocker locker(cacheMutex);
        auto it = propCache.find(cacheKey);
        if (it != propCache.end()) return it.value();
      }

      QVariant result;
      auto values = m.readEmbeddedMetadata(exifKeys, field);
      for (auto& v : values)
        if (!v.isNull()) {
          result = v;
          break;
        }

      {
        QMutexLocker locker(cacheMutex);
        propCache.insert(cacheKey, result);
      }
      return result;
    };
  } else if (field == "ffmeta") {
    if (args.count() == 0)
      qFatal("ffmeta sort requires metadata field name(s)");
    QStringList ffKeys = args.front().split(",");
    args.pop_front();
    select = [=](const Media& m) {
      auto values = VideoContext::readMetaData(m.path(), ffKeys);
      for (auto& v : values)
        if (!v.isNull()) return v;

      return QVariant();
    };
  } else
    qFatal("invalid property: %s", qPrintable(field));

  if (args.count() > 0) {
      auto func = unaryFunc(args.join(lc('#')));
      return [=](const Media& m) {
        return func(select(m));
      };
  } else
    return select;

  Q_UNREACHABLE();
}

void Media::recordMatch(const Media& match, int matchIndex,
                        int numMatches) const {
  QString line;

  if (match.path() != "") {
    line = QString("\"%1\",\"%2\",%3,%4,%5,%6\n")
               .arg(path())
               .arg(match.path())
               .arg(matchIndex)
               .arg(match.score())
               .arg(match.position())
               .arg(numMatches);
  } else
    line = QString("%1,,0,,,%2\n").arg(path()).arg(numMatches);

  QFile f("match.csv");
  f.open(QFile::WriteOnly | QFile::Append);
  f.write(line.toLatin1());
  f.close();
}

size_t Media::memSize() const {
  size_t total = sizeof(*this);

  total += size_t(_data.size());

  total += VECTOR_SIZE(_kpHashes);
  total += VECTOR_SIZE(_keyPoints);
  total += _videoIndex.memSize();
  total += VECTOR_SIZE(_kpRects);
  total += CVMAT_SIZE(_descriptors);

  total += size_t(_img.bytesPerLine() * _img.height());

  // todo: matchlist, matchrange

  return total;
}

void Media::makeKeyPoints(const cv::Mat& cvImg, int numKeyPoints) {
  cv::OrbFeatureDetector detector(numKeyPoints, 1.2f, 12, 31, 0, 2,
                                  cv::OrbFeatureDetector::HARRIS_SCORE, 31);

  _keyPoints.clear();

  // todo: use mask to exclude borders/watermarks etc
  detector.detect(cvImg, _keyPoints);
}

void Media::makeKeyPointDescriptors(const cv::Mat& cvImg) {
  cv::OrbDescriptorExtractor extractor;

  _descriptors.empty();

  extractor.compute(cvImg, _keyPoints, _descriptors);
}

void Media::makeKeyPointHashes(const cv::Mat& cvImg) {
  _kpRects.clear();
  _kpHashes.clear();

  for (const cv::KeyPoint& kp : _keyPoints) {
    float size = kp.size;

    // printf("kp size=%.2f octave=%d class=%d\n", size, kp.octave,
    // kp.class_id); size*=8; size *= scale;

    // if resulting rectangle is too small dct hash is worthless
    if (size < 31) continue;

    // fixme: if size is kp diameter and point is center,
    // this should be centered?
    float x0 = kp.pt.x;  // + xOffset; //+ _roi.x;
    float y0 = kp.pt.y;  // + yOffset; //+ _roi.y;
    float x1 = x0 + size;
    float y1 = y0 + size;

    if (x0 > 0 && y0 > 0 && x1 < cvImg.cols - 2 && y1 < cvImg.rows - 2) {
      int x = int(floor(x0));
      int y = int(floor(y0));
      int s = int(ceil(size));

      _kpRects.push_back(cv::Rect(x, y, s, s));
    }
  }

  // rectangles to hashes
  for (const cv::Rect& r : _kpRects) {
    // extract sub-image and store hash
    cv::Mat sub =
        cvImg.colRange(r.x, r.x + r.width).rowRange(r.y, r.y + r.height);

    uint64_t hash = dctHash64(sub);

    /* we could drop near hashes, but typically not many
    for (uint64_t h : hashes)
        if (hamm64(h, hash) < 5)
        {
            printf("skip near hash\n");
            continue;
        }
    */

    _kpHashes.push_back(hash);
  }
}

void Media::makeVideoIndex(VideoContext& video, int threshold) {
  _videoIndex.hashes.clear();
  _videoIndex.frames.clear();

  VideoIndex index;

  int numFrames = 0;
  int curFrames = 0;

  int corruptFrames = 0;
  int nearFrames = 0;
  int filteredFrames = 0;

  qint64 then = QDateTime::currentMSecsSinceEpoch();

  std::vector<uint64_t> window;

  cv::Mat img;

  const int totalFrames =
      int(video.metadata().frameRate * video.metadata().duration);

  _width = video.width();
  _height = video.height();

  const QString cwd = QFileInfo(QDir::current().absolutePath()).absoluteFilePath();
  QString path = video.path();
  if (path.startsWith(cwd)) path = path.mid(cwd.length() + 1);

  if (video.nextFrame(img)) {
    // fixme: index settings
    autocrop(img, 20);
    uint64_t hash = dctHash64(img);
    _videoIndex.hashes.push_back(hash);
    _videoIndex.frames.push_back(numFrames & 0xFFFF);
    numFrames++;
  }

  while (video.nextFrame(img)) {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - then > 5000) {
      qDebug("%dx%d %dpx %d:1 %s(%d) %dfps %d%% ", _width, _height,
             qMax(img.cols, img.rows),
            numFrames / std::max(numFrames - nearFrames, 1),
             (video.isHardware() ? "GPU" : "CPU"), video.threadCount(),
            int(curFrames * 1000 / (now - then)),
             numFrames * 100 / std::max(totalFrames, 1));
      curFrames = 0;
      then = now;
    }

    // de-letterbox prior to p-hashing
    // fixme: index settings
    autocrop(img, 20);

    uint64_t hash = dctHash64(img);

    // compress hash list, since nearby hashes
    // are likely be similar
    if (Q_LIKELY(threshold > 0)) {
      size_t close = 0;
      for (uint64_t prev : window)
        if (hamm64(prev, hash) < threshold) close++;

      if (close != window.size()) {
        window.clear();
        _videoIndex.hashes.push_back(hash);
        _videoIndex.frames.push_back(numFrames & 0xFFFF);
      } else
        nearFrames++;

      window.push_back(hash);
    } else {
      _videoIndex.hashes.push_back(hash);
      _videoIndex.frames.push_back(numFrames & 0xFFFF);
    }

    numFrames++;
    curFrames++;

    if (numFrames > 0xFFFF) {
      qCritical() << _path << "greater than 64k frames unsupported, quitting";
      break;
    }
  }

  // always include the last frame so it can be used as a reference
  if (_videoIndex.frames.size() > 0 &&
      _videoIndex.frames.back() != numFrames - 1) {
    _videoIndex.hashes.push_back(window.back());
    _videoIndex.frames.push_back((numFrames - 1) & 0xFFFF);
  }

  qDebug("%s nframes=%d near=%d filt=%d corrupt=%d", qUtf8Printable(video.path()),
         numFrames, nearFrames, filteredFrames, corruptFrames);
}

void VideoIndex::save(const QString& file) const {
  MessageContext ctx(file);
  FILE* indexFile = fopen(qUtf8Printable(file), "wb");
  if (!indexFile) qFatal("failed to open");

  uint16_t numFrames = frames.size() & 0xFFFF;
  if (1 != fwrite(&numFrames, sizeof(numFrames), 1, indexFile))
    qFatal("write fail at start");

  for (uint16_t frame : frames)
    if (1 != fwrite(&frame, sizeof(frame), 1, indexFile))
      qFatal("write fail on frames");

  for (uint64_t hash : hashes)
    if (1 != fwrite(&hash, sizeof(hash), 1, indexFile))
      qFatal("write fail on hashes");

  fclose(indexFile);
}

void VideoIndex::load(const QString& file) {
  FILE* indexFile = fopen(qPrintable(file), "rb");
  if (!indexFile) qFatal("failed to open: %s", qPrintable(file));

  frames.clear();
  hashes.clear();

  uint16_t numFrames = 0;
  if (1 != fread(&numFrames, sizeof(numFrames), 1, indexFile))
    qFatal("read fail at index start: %s", qPrintable(file));

  frames.reserve(numFrames);
  hashes.reserve(numFrames);

  // spec says reserve() could give more than we wanted
  Q_ASSERT(frames.capacity() == numFrames);
  Q_ASSERT(hashes.capacity() == numFrames);

  // todo: it seems this could be read with one operation
  for (int i = 0; i < numFrames; i++) {
    uint16_t frame;
    if (1 != fread(&frame, sizeof(frame), 1, indexFile))
      qFatal("read fail at frame index %d: %s", i, qPrintable(file));

    frames.push_back(frame);
  }

  for (int i = 0; i < numFrames; i++) {
    uint64_t hash;
    if (1 != fread(&hash, sizeof(hash), 1, indexFile))
      qFatal("read fail at hash index %d: %s", i, qPrintable(file));

    hashes.push_back(hash);
  }

  fclose(indexFile);
}

void Media::playSideBySide(const Media& left, float seekLeft,
                           const Media& right, float seekRight) {
  DesktopHelper::playSideBySide(left.path(), double(seekLeft), right.path(),
                                double(seekRight));
}

void Media::openMedia(const Media& m, float seek) {
  if (m.type() == Media::TypeVideo) {
#ifdef __APPLE__
    QString script = QString(
                         "tell application \"VLC\"\n"
                         "activate\n"
                         "open \"%1\"\n"
                         "set current time to %2\n"
                         "end tell")
                         .arg(m.path())
                         .arg(seek);

    QProcess process;
    process.start("osascript");
    process.write(script.toUtf8());
    process.closeWriteChannel();
    process.waitForFinished(10000);
#else
    qDebug() << "open video: " << m.path();
    DesktopHelper::openVideo(QFileInfo(m.path()).absoluteFilePath(),
                             double(seek));
#endif

#if 0
        // start/activate vlc, it needs to be setup for single-instance mode or this
        // will spawn another copy
        QProcess process;
        process.startDetached("vlc");

        // recursion is possible since httpRequest() starts event loop,
        // try to prevent that from happening
        //this->setEnabled(false);

        int seconds = (int)seek;

        QNetworkAccessManager nam;

        QUrl url("http://:password@localhost:8080/requests/status.xml");
        QUrlQuery query;
        QString xml;

        // empty playlist which will stop current playing item if any
        query.addQueryItem("command", "pl_empty");
        url.setQuery(query);
        (void)httpRequest(nam, url);

        // wait for stopped state, otherwise seek doesn't work
        query.clear();
        url.setQuery(query);
        xml = httpRequest(nam, url);
        while (!xml.contains("<state>stopped</state>"))
        {
            printf("wait stop\n");
            xml = httpRequest(nam, url);
        }

        // add to playlist and start
        query.clear();
        query.addQueryItem("command", "in_play");
        query.addQueryItem("input", "file://"+m.path());
        url.setQuery(query);
        (void)httpRequest(nam, url);

        // wait for picture to be displayed
//        query.clear();
//        url.setQuery(query);
//        xml = httpRequest(nam,url);
//        while (!xml.contains("<displayedpictures>"))
//        {
//            printf("wait start\n");
//            xml = httpRequest(nam, url);
//        }

        // seek
        query.clear();
        query.addQueryItem("command", "seek");
        query.addQueryItem("val", QString::number(seconds));
        url.setQuery(query);
        (void)httpRequest(nam, url);

        //this->setEnabled(true);

    /*
        // requires a separate instance of VLC, if there is
        // a running instance it will not seek.Instances tend
        // to pile up and be a pain.
        QStringList args;
        args.append("--start-time");
        args.append(QString::number(m.matchRange().dstIn/fps));
        args.append(m.path());

        QProcess process;
        process.startDetached("vlc", args);
        //process.waitForFinished(10000);
    */
#endif
  } else if (m.isArchived()) {
    QString child;
    m.archivePaths(nullptr, &child);

    // todo: open file within archive...have it be browseable
    //       there are various ways this is done depending on
    //       the viewer
    // nomacs: zipfile.zipdIrChAr/child.jpg => opens but not browseable
    // - use our own browser
    // - move this to DesktopHelper::openImage
    QIODevice* io = m.ioDevice();
    if (io && io->open(QIODevice::ReadOnly)) {
      child = child.split("/").last();
      QFileInfo info(child);
      QString temporaryName = DesktopHelper::tempName(info.completeBaseName() +
                                                      ".unzipped.XXXXXX." + info.suffix());

      if (temporaryName.isEmpty()) {
        qWarning() << "open archived file: cannot get temporary file";
        return;
      }

      QFile f(temporaryName);
      if (!f.open(QFile::WriteOnly|QFile::Truncate)) {
        qWarning() << "open archived file: cannot write temporary file";
        return;
      }

      f.write(io->readAll());
      f.close(); // necessary because file was opened excl mode (win32)

      QDesktopServices::openUrl(QUrl::fromLocalFile(temporaryName));
    }
    delete io;
  } else {
    QUrl url(m.path());
    if (url.scheme().length() < 2) { // empty or a drive letter
      QString path = m.path();
      QFileInfo info(path);
      if (info.isFile()) path = info.absoluteFilePath();
      url = QUrl::fromLocalFile(path);
    }
    qDebug() << "QDesktopServices::openUrl" << url;

    QDesktopServices::openUrl(url);
  }
}

void Media::revealMedia(const Media& m) {
  QString path = m.path();
  if (m.isArchived())
    m.archivePaths(&path);

  DesktopHelper::revealPath(path);
}

QColor Media::matchColor() const {

  // no match gets dark yellow
  QColor c = QColor(Qt::yellow).darker();

  if (score() >= 0) {
    int flags = matchFlags();

    // perfect match is green, no other tests needed
    if (flags & MatchExact)
      c = QColor(Qt::green).darker();
    else {
      // shades from orange to red, red being the presumed best matches
      int h, s, v;
      h = 330 + 60;
      s = 255;
      v = 180;

      c.setHsv(h % 360, s, v);

      if (flags & MatchBiggerDimensions) {
        h -= 20;
        c.setHsv(h % 360, s, v);
      }

      if (flags & MatchBiggerFile) {
        h -= 20;
        c.setHsv(h % 360, s, v);
      }

      if (flags & MatchLessCompressed) {
        h -= 20;
        c.setHsv(h % 360, s, v);
      }
    }
  }

  return c;
}

QIcon Media::loadIcon(const QSize& size) const {
  return QIcon(QPixmap::fromImage(loadImage(size)));
}

int Media::archiveCount() const {
  int count = -1;
  if (isArchived()) {
    QString zipPath;
    archivePaths(&zipPath);

    if (QFileInfo::exists(zipPath)) {
      QuaZip zip(zipPath);
      zip.open(QuaZip::mdUnzip);
      count = zip.getFileNameList().count();
    }
  }

  return count;
}

QStringList Media::listArchive(const QString& path) {
  QStringList list;

  QuaZip zip(path);
  if (!zip.open(QuaZip::mdUnzip)) {
    qCritical() << "failed to open:" << path;
    return list;
  }

  const auto zipList = zip.getFileNameList();
  for (auto& file : zipList) {
    // todo: setting for ignored dirnames (same as scanner...);
    if (file.endsWith("/") || file.startsWith(".") ||
        file.startsWith("__MACOSX"))
      continue;

    list.append(Media::virtualPath(path, file));
  }
  return list;
}

QIODevice* Media::ioDevice() const {
  QIODevice* io = nullptr;
  const QByteArray& data = this->data();

  if (data.size() > 0) {
    QBuffer* buf = new QBuffer;
    buf->setBuffer(const_cast<QByteArray*>(&data));
    io = buf;
  } else if (isArchived()) {
    QString zipPath, fileName;
    archivePaths(&zipPath, &fileName);
    QFileInfo info(zipPath);
    if (info.isFile()) {
      QuaZip zip(zipPath);
      zip.open(QuaZip::mdUnzip);
      zip.setCurrentFile(fileName);
      QuaZipFile file(&zip);
      if (file.open(QIODevice::ReadOnly)) {
        QBuffer* buf = new QBuffer;
        buf->setData(file.readAll());
        io = buf;
      }
      else {
        qWarning() << "failed to unzip" << zipPath << "for" << fileName;
      }
    } else {
      qWarning() << "zip file does not exist" << zipPath << "for" << fileName;
      qWarning() << "maybe illegal path on this system" << zipPath;
    }
  } else {
    QFile* file = new QFile(path());
    if (!file->exists()) {
      qWarning() << "file does not exist" << _path;
      qWarning() << "maybe illegal path on this system" << _path;
    }
    io = file;
  }

  return io;
}

QImage Media::loadImage(const QByteArray& data, const QSize& size,
                        const QString& name, const QFuture<void>* future,
                        const ImageLoadOptions& options) {

  const QString fileName = QFileInfo(name).fileName();
  qMessageContext().setLocalData(QLatin1String("QImageReader: ") + fileName);

  // safe to cast away const since we do not write the buffer
  QBuffer* buffer = new QBuffer(const_cast<QByteArray*>(&data));
  std::unique_ptr<QIODevice> io;

  // svg loader does not like having io terminated early, deadlocks
  if (future) {
    const auto lcName = name.toLower();
    if (lcName.endsWith(QLatin1String(".svg")) ||
        lcName.endsWith(QLatin1String(".svgz")))
      future = nullptr;
  }

  if (future)
    io.reset( new QCancelableIODevice(buffer, future) );
  else
    io.reset(buffer);

  QImageReader reader;
  reader.setDevice(io.get());

  auto format = reader.format();

  if (options.fastJpegIdct && format == "jpeg")
    reader.setQuality(0); // use libjpeg JDCT_IFAST

  QSize origSize = reader.size();
  const int maxDim = qMax(origSize.width(), origSize.height());
  if (options.readScaled && origSize.isValid() &&
      maxDim > options.maxSize &&
      reader.supportsOption(QImageIOHandler::ScaledSize)) {

      // jpeg idct scaling supports n/8 for n=[16-1]
      // for downscaling we get 1/8, 2/8, ... 7/8
      int numerator;
      for (numerator = 7; numerator > 1; --numerator) {
        int scaled = maxDim * numerator / 8;
        if (scaled >= options.minSize && scaled <= options.maxSize) break;
      }

      // could get something < min if range is too narrow
      if (maxDim * numerator / 8 < options.minSize) {
        ;//qWarning() << "ignoring readScaled, no suitable scale factor. maybe increase range?";
      }
      else {

        //
        // qjpeg shenanigans...
        // see: qtbase/src/plugins/imageformats/jpeg/qjpeghandler.cpp
        //
        // qjpeghandler will use (n+1) idct and scale down if
        // we don't set the size just right. This is bad since
        // qt scaler seems to add noise/aliasing that affects
        // search algorithms
        //
        // qt will *always* invoke the scaler, however if we are
        // already really close the noise/aliasing seems to be reduced
        // or insignificant
        //
        // note: can verify the correct scale was selected from
        // profiler, e.g.  jpeg_idct7 for the 7/8 downscale
        //
        int rw = origSize.width() & ~0x7;
        int rh = origSize.height() & ~0x7;

        QSize ssize(int(rw / 8 * numerator), int(rh / 8 * numerator));
        reader.setScaledSize(ssize);

        //qDebug() << "downscale " << rw << rh << ssize << numerator;
      }
  }
  QImage img = reader.read();

  if (future && future->isCanceled()) // io could have been canceled, assume incomplete image
    img = QImage();

  img.setText(ImgKey_FileSize, QString::number(data.length()));
  img.setText(ImgKey_FileName, name);
  img.setText(ImgKey_FileFormat, reader.format());

  if (!origSize.isValid()) {
    if (reader.scaledSize().isEmpty())
        origSize = img.size();
    else
        qWarning("%s: unknown original dimensions", format.data());
  }

  img.setText(ImgKey_FileWidth,  QString::number(origSize.width()));
  img.setText(ImgKey_FileHeight, QString::number(origSize.height()));

  qMessageContext().setLocalData(fileName);

  // setAutoTransform() will pull orientation from thumbnail IFD if it is not present in the image
  // IFD, resulting in incorrect rotation
  long exifOrientation = 0;
  if (format == "jpeg" &&
      reader.transformation() != 0) {

    auto exif = Exiv2::ImageFactory::open(
        reinterpret_cast<const Exiv2::byte*>(data.constData()), data.size());
    if (exif.get()) {
      exif->readMetadata();
      auto& exifData = exif->exifData();
      auto it = exifData.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
      if (it != exifData.end()) exifOrientation = it->value().toLong();
    }
  }

  // fixme: skipping exif mirror orientations (2,4,5,7)
  qreal rotate = 0;
  switch (exifOrientation) {
    case 1:
      exifOrientation = 0;
      break;
    case 3:
      rotate = 180;
      break;
    case 6:
      rotate = 90;
      break;
    case 8:
      rotate = -90;
      break;
  }

  if (exifOrientation) img = img.transformed(QTransform().rotate(rotate));

  // fixme: is size before or after orientation?
  if (size != QSize()) img = constrainedResize(img, size);

  if (reader.error())
    qWarning("%s: xform=0x%x orient=%d size=%dx%d error=%s",
             format.data(), int(reader.transformation()),
             int(exifOrientation), img.width(), img.height(),
             qPrintable(reader.errorString()));

  qMessageContext().setLocalData(QString());

  return img;
}

QImage Media::loadImage(const QSize& size, QFuture<void>* future) const {
  // if the full-size image is loaded(cached),
  // use it, otherwise load and possibly rescale,
  // but do not cache anything
  QImage img = image();

  if (img.isNull()) {
    std::unique_ptr<QIODevice> io(ioDevice());
    if (future && future->isCanceled()) return img;
    if (io && io->open(QIODevice::ReadOnly)) {
      QByteArray data = io.get()->readAll();
      if (future && future->isCanceled()) return img;
      img = loadImage(data, size, path(), future);
    }
  } else if (size != QSize())
    img = constrainedResize(img, size);

  return img;
}

bool Media::isReloadable() const {
  return type() == Media::TypeImage &&
         (data().length() > 0 || id() > 0 || isArchived() || QFileInfo(path()).exists());
}

QImage Media::constrainedResize(const QImage& img, const QSize& size) {
  if (img.isNull()) return img;

  int width = size.width();
  int height = size.height();

  if (width <= 0 && height > 0)
    width = img.width() * height / img.height();
  else if (width > 0 && height <= 0)
    height = img.height() * width / img.width();

  QSize scaled(width, height);
  if (scaled != img.size())
    return img.scaled(scaled, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

  return img;
}

void Media::readMetadata() {
  if (isArchived()) {
    QString sizeText = _img.text(Media::ImgKey_FileSize);
    if (!sizeText.isEmpty()) {
      _origSize = _img.text(Media::ImgKey_FileSize).toLongLong();
    } else {
      QString zipPath, fileName;
      archivePaths(&zipPath, &fileName);

      bool ok = false;
      if (QFileInfo::exists(zipPath)) {
        QuaZip zip(zipPath);
        zip.open(QuaZip::mdUnzip);

        auto infoList = zip.getFileInfoList();
        for (auto& info : infoList)
          if (info.name == fileName) {
            _origSize = info.uncompressedSize;
            ok = true;
            break;
          }
      }
      if (!ok) qWarning() << "file not found in archive" << zipPath << fileName;
    }
  } else if (_data.isEmpty()) {
    QFileInfo info(path());
    if (info.exists()) _origSize = info.size();
  }

  if (_origSize && !_img.isNull())
    _compressionRatio = float(_img.sizeInBytes()) / _origSize;
}

QStringList Media::exifVersion() {
  QStringList list;
  list << Exiv2::version();
  list << EXV_PACKAGE_VERSION;
  return list;
}

QVariantList Media::readEmbeddedMetadata(const QStringList& keys, const QString& type) const {
  const MessageContext mc(QFileInfo(path()).fileName());

  QVariantList values;
  for (int i = 0; i < keys.count(); ++i)
    values.append(QVariant());

  try {
    std::unique_ptr<Exiv2::Image> image;
    QByteArray data = _data;

    if (data.isEmpty() && isArchived()) {
      QIODevice* io = ioDevice();
      if (io) {
        io->open(QIODevice::ReadOnly);
        data = io->readAll();
        delete io;
      }
    }

    if (!data.isEmpty())
      image = Exiv2::ImageFactory::open(
          reinterpret_cast<const Exiv2::byte*>(data.constData()),
          data.size());
    else
      image = Exiv2::ImageFactory::open(qUtf8Printable(path()));

    if (!image.get()) return values;

    image->readMetadata();
    std::function<QVariant(const QString&)> findKey = [](const QString&) { return QVariant(); };

    if (type == "exif") {
      const auto& exifData = image->exifData();
      if (exifData.empty()) return values;
      findKey=[&exifData](const QString& key) {
        QVariant v;
        auto it = exifData.findKey(Exiv2::ExifKey(qPrintable(key)));
        if (it == exifData.end()) return v;
        v = it->value().toString().c_str();
        if (key.contains("Date"))
          v = QDateTime::fromString(v.toString(), "yyyy:MM:dd HH:mm:ss");
        return v;
      };
    }
    else if (type == "iptc") {
      const auto& iptcData = image->iptcData();
      if (iptcData.empty()) return values;
      findKey=[&iptcData](const QString& key) {
        QVariant v;
        auto it = iptcData.findKey(Exiv2::IptcKey(qPrintable(key)));
        if (it == iptcData.end()) return v;
        v = it->value().toString().c_str();
        switch (it->value().typeId()) {
          case Exiv2::TypeId::date: v = QDate::fromString(v.toString(), Qt::ISODate); break;
          case Exiv2::TypeId::time: v = QTime::fromString(v.toString(), Qt::ISODate); break;
          default: ;
        }
        return v;
      };
    }
    else if (type == "xmp") {
      const auto& xmpData = image->xmpData();
      if (xmpData.empty()) return values;
      findKey=[&xmpData](const QString& key) {
        QVariant v;
        auto it = xmpData.findKey(Exiv2::XmpKey(qPrintable(key)));
        if (it == xmpData.end()) return v;
        v = it->value().toString().c_str();
        if (key.contains("Date"))
          v = QDateTime::fromString(v.toString(), Qt::ISODate);
        return v;
      };
    }
    else
      qWarning("Invalid metadata prefix");

    for (int i = 0; i < keys.count(); ++i) {
      QString key = keys[i];
      QString prefix = type;
      prefix[0] = prefix[0].toUpper();
      if (!key.startsWith(prefix)) key = prefix + "." + key;
      key[0] = key[0].toUpper(); // first part is caps (Exif.,Iptc.,Xmp. etc)
      values[i] = findKey(key);
    }

  } catch (std::exception& e) {
    qWarning() << "exif exception:" << path() << e.what();
  }

  return values;
}
