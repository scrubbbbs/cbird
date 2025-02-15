/* Video file hash storage 
   Copyright (C) 2021-2025 scrubbbbs
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
#include "videoindex.h"
#include "dctvideoindex.h"
#include "git.h"
#include "ioutil.h"
#include "media.h"
#include "qtutil.h"
#include "scanner.h"

bool VideoIndex::upgradeMessageShown = false;

void VideoIndex::save(const QString& file) const {
  MessageContext ctx(file);
  Q_ASSERT(hashes.size() == frames.size());
  SimpleIO io;
  if (io.open(file, false) && !save_v2(io)) QFile::remove(file);
}

int VideoIndex::getVersion(SimpleIO& io) {
  int version = 1;
  {
    char buffer[6] = {0};
    if (!io.read(buffer, 2, "magic")) return 1; // v1 2-bytes header

    if (!io.read(&buffer[2], 3, "magic")) return 1;

    if (buffer == QString("cbird")) version = 2;
  }

  if (version == 1) {
    // do not show when resuming after -migrate
    if (QFileInfo(io.filePath()).completeBaseName().startsWith("resume-")) return version;

    if (!upgradeMessageShown) {
      upgradeMessageShown = true;
      qInfo()
          << "<NC>\n"
             "    cbird: <YEL>old video index format in use (limited to 65k frames/videos)\n<RESET>"
             "        (1) pass <MAG>-i.dryrun true -migrate<RESET> to test/review changes\n"
             "        (2) pass <MAG>-migrate<RESET> to update files\n"
             "        (3) pass <MAG>-update<RESET> to reprocess affected files\n";
    }
  }

  return version;
}

void VideoIndex::load(const QString& file) {
  MessageContext ctx(file);
  Q_ASSERT(hashes.size() == 0 && frames.size() == 0);

  SimpleIO io;
  if (!io.open(file, true)) return;

  int version = getVersion(io);
  io.rewind();

  bool ok = false;
  if (version == 1) ok = load_v1(io);
  if (version == 2) ok = load_v2(io);

  if (!ok) {
    hashes.clear();
    frames.clear();
  }
}

bool VideoIndex::isValid(const QString& file) {
  MessageContext ctx(file);

  SimpleIO io;
  if (!io.open(file, true)) return true; // not an error since we couldn't even look at it

  int version = getVersion(io);
  io.rewind();
  bool ok = false;
  if (version == 1) ok = verify_v1(io);
  if (version == 2) ok = verify_v2(io);
  return ok;
}

void VideoIndex::migrate(const MediaGroup& media, const QString& root, const IndexParams& params) {
  upgradeMessageShown = true; // don't need to see that here

  if (params.dryRun) qInfo("dry run, checking conversion with temp file");

  PROGRESS_LOGGER(pl, "checking:<PL> %percent %step files, %1 updated, %2 removed", media.count());
  int i = 0, updated = 0, removed = 0;

  pl.showLast();
  QElapsedTimer timer;
  timer.start();
  for (auto& m : media) {
    Q_ASSERT(m.type() == Media::TypeVideo);

    if (timer.elapsed() > 100) {
      pl.step(i, {updated, removed});
      timer.start();
    }
    i++;

    const QString path = qq("%1/%2.vdx").arg(root).arg(m.id());
    if (!QFile::exists(path)) {
      // this is fine; it just means vindex was disabled
      continue;
    }

    MessageContext ctx(QFileInfo(path).fileName());

    SimpleIO io;
    if (!io.open(path, true)) continue;

    int version = getVersion(io);
    io.rewind();
    if (version != 1) continue;

    if (!verify_v1(io)) {
      qInfo() << "removing invalid file:" << path;
      if (!params.dryRun && QFile::remove(path)) removed++;
      continue;
    }
    io.rewind();

    VideoIndex v1;
    if (!v1.load_v1(io)) {
      qInfo() << "removing file with errors:" << path;
      if (!params.dryRun && QFile::remove(path)) removed++;
      removed++;
      continue;
    }

    if (v1.frames.size() && v1.frames[v1.frames.size() - 1] == UINT16_MAX) {
      qInfo() << "re-indexing for >65k frames:" << m.name();
      io.close();

      // copy old index to file that can be picked up by scanner
      QString resumePath = qq("%1/resume-%2.vdx").arg(root).arg(m.md5());

      qDebug() << "copying to:" << resumePath;
      if (params.dryRun) continue;

      QFile::remove(resumePath);
      if (QFile::copy(path, resumePath))
        if (QFile::remove(path)) removed++;

      continue;
    }

    QString tmpPath = QDir::tempPath() + "/cbird-dryrun.vdx";
    if (!params.dryRun) tmpPath = root + "/migrate-" + QString::number(m.id()) + ".vdx";

    qDebug() << "writing to" << tmpPath;

    if (!io.open(tmpPath, false)) return;

    if (!v1.save_v2(io)) return;

    io.close();

    VideoIndex v2;
    if (!isValid(tmpPath)) {
      qCritical() << "aborting: invalid file after conversion";
      return;
    }

    v2.load(tmpPath);
    if (v1.frames.size() != v2.frames.size() || v1.hashes.size() != v2.hashes.size()) {
      qCritical() << "aborting: count mismatch";
      return;
    }
    for (size_t i = 0; i < v1.hashes.size(); ++i)
      if (v1.frames[i] != v2.frames[i] || v1.hashes[i] != v2.hashes[i]) {
        qCritical() << "aborting: data mismatch";
        return;
      }

    if (!params.dryRun) {
      QString backup = path + ".bak";
      if (QFile::rename(path, backup)) {
        if (QFile::rename(tmpPath, path)) {
          qDebug() << "update successful";
          QFile::remove(backup);
          updated++;
        } else {
          qCritical() << "aborting: failed to rename file";
          QFile::rename(backup, path);
          return;
        }
      }
    } else
      qDebug() << "dry run: upgrade successful";

    QFile::remove(tmpPath);
  }

  pl.end(0, {updated, removed});
  if (updated > 0 || removed > 0) qInfo() << "index was updated";
  if (removed > 0) qInfo() << "run -update to refresh index";
}

bool VideoIndex::checkHeader_v2(const QList<QByteArray>& header) {
  if (header.count() != 8) {
    qCritical() << "missing header";
    return false;
  }

  if (header[0] != "cbird video index") {
    qCritical() << "not a cbird video index";
    return false;
  }

  if (header[2].toInt() != 2 || header[4].toInt() != sizeof(uint8_t)
      || header[5].toInt() != sizeof(dcthash_t)) {
    qCritical() << "unsupported format, written by cbird version:" << header[1];
    return false;
  }

  if (header[3].toInt() != QSysInfo::ByteOrder) {
    qCritical() << "written with different endianness"; // TODO: have to byteswap everything
    return false;
  }

  return true;
}

bool VideoIndex::verify_v2(SimpleIO& io) {
  char rawHeader[256] = {0};
  if (!io.readline(rawHeader, 255, "header")) return false;

  QList<QByteArray> header = QByteArray(rawHeader).split(':');
  if (!checkHeader_v2(header)) return false;

  if (header[6].toInt() == 0) {
    qWarning() << "no frames stored, remove file to re-attempt indexing";
    return true;
  }

  char trailer[5] = {0};
  if (!io.readEnd(trailer, 4, "trailer")) return false;

  if (trailer != QLatin1String("cbir")) {
    qWarning() << "truncated file, missing trailer";
    return false;
  }

  return true;
}

bool VideoIndex::save_v2(SimpleIO& io) const {
  auto header = QStringLiteral("cbird video index:%1:%2:%3:%4:%5:%6:\n")
                    .arg(CBIRD_VERSION)
                    .arg(2)
                    .arg(QSysInfo::ByteOrder)
                    .arg(sizeof(uint8_t))   // size of frame numbers
                    .arg(sizeof(dcthash_t)) // size of hashes
                    .arg(frames.size());

  if (!io.write(header.toLatin1().data(), header.length(), "header")) return false;

  // possible we did not read any frames from the file
  if (frames.size() == 0) return true;

  // write frame offsets instead of frame numbers, so we use
  // less space than v1 and there is no upper bound on number of frames

  // use 7-bit offsets, the 8th bit tells if the offset was
  // shifted from a larger value. e.g. a 14-bit offset would
  // take 2 bytes in the output, while a 15-bit offset would require 3.
  std::vector<uint8_t> packed;
  packed.reserve(frames.size());

  if (frames[0] != 0) {
    qCritical() << "first frame must be 0"; // required for encoding
    return false;
  }

  int prevFrame = frames[0];
  int nextByte = prevFrame;

  for (size_t i = 1; i < frames.size(); ++i) {
    int offset = frames[i] - prevFrame;
    prevFrame = frames[i];
    if (offset < 1) {
      qCritical() << "non-sequential frame number, corrupt file?" << offset << i << frames[i - 1]
                  << frames[i];
      qDebug() << frames;
      return false;
    }

    while (offset > 0) {
      packed.push_back(nextByte);
      int lsb = offset & 0x7F;
      offset = offset >> 7;
      nextByte = lsb | (offset == 0 ? 0x00 : 0x80);
    }
  }
  packed.push_back(nextByte);

  if (packed.size() > UINT32_MAX) {
    qCritical() << "too many frames";
    return false;
  }

  // make reading easier
  {
    uint32_t len = packed.size();
    if (!io.write(&len, 1, "len")) return false;
  }

  // if we ever want to try mmap, the hashes should be properly aligned
  {
    qint64 here = header.length() + 4 + packed.size();
    int align = sizeof(dcthash_t);
    int pad = align - (here % align);
    if (pad == align) pad = 0;
    packed.resize(packed.size() + pad);
  }

  if (!io.write(packed.data(), packed.size(), "frames")) return false;

  if (!io.write(hashes.data(), hashes.size(), "hashes")) return false;

  // eof marker for fast verification
  if (!io.write("cbir", 4, "trailer")) return false;

  return true;
}

bool VideoIndex::load_v2(SimpleIO& io) {
  // 1 million frames is only ~10MB so go ahead and reduce IOPS
  if (!io.bufferAll()) return false;

  char line[256] = {0};
  if (!io.readline(line, 255, "header")) return false;

  const QByteArray rawHeader(line);
  const QList<QByteArray> header = rawHeader.split(':');

  if (!checkHeader_v2(header)) return false;

  uint32_t numFrames = header[6].trimmed().toUInt();
  if (numFrames == 0) return true;

  // if numframes exceeds what tree supports reduce it,
  // also prevents potential ddos from corrupt files
  bool reduced = false;
  if (numFrames > MAX_FRAMES_PER_VIDEO) {
    numFrames = MAX_FRAMES_PER_VIDEO;
    qWarning() << "max frames exceeded, limiting to" << numFrames;
    reduced = true;
  }

  frames.reserve(numFrames);
  hashes.resize(numFrames);

  uint32_t packedLen = 0;
  if (!io.read(&packedLen, 1, "len")) return false;

  if (packedLen < numFrames) {
    qCritical() << "invalid file, unexpected packed size:" << packedLen << numFrames;
    return false;
  }

  std::vector<uint8_t> packed;
  packed.resize(packedLen);
  if (!io.read(packed.data(), packedLen, "packed frames")) return false;

  int frame = 0;
  int jump = 0;
  int shift = 0;
  for (uint8_t byte : packed) {
    if (0 == (byte & 0x80)) {
      frame += jump | (byte << shift);
      jump = 0;
      shift = 0;
      frames.push_back(frame);
      if (reduced && frames.size() == numFrames) break;
    } else {
      jump |= (byte & 0x7F) << shift;
      shift += 7;
    }
  }

  if (jump) {
    qCritical() << "unresolved offset, possibly corrupt file";
    return false;
  }

  if (frames.size() != numFrames) {
    qCritical() << "failed to read expected number of frames:" << numFrames << frames.size();
    return false;
  }

  // read padding for (maybe future) memory mapping
  {
    qint64 here = rawHeader.length() + 4 + packedLen;
    int align = sizeof(dcthash_t);
    int pad = align - (here % align);
    if (pad == align) pad = 0;
    std::vector<uint8_t> buffer(pad);
    if (!io.read(buffer.data(), buffer.size(), "padding")) return false;
  }

  if (!io.read(hashes.data(), numFrames, "hashes")) return false;

  return true;
}

bool VideoIndex::verify_v1(SimpleIO& io) {
  uint16_t numFrames = 0;

  if (!io.read(&numFrames, 1, "header")) return false;

  // TODO: indexParam.removeEmptyFiles, could return false here
  if (numFrames == 0) qWarning() << "no frames stored, remove file to re-attempt indexing";

  size_t size = sizeof(uint16_t) + sizeof(uint16_t) * numFrames + sizeof(uint64_t) * numFrames;
  if (io.fileSize() != size) {
    qWarning() << "invalid file size";
    return false;
  }

  return true;
}

bool VideoIndex::save_v1(SimpleIO& io) const {
  // alarm for changes to hash size
  static_assert(sizeof(dcthash_t) == 8, "v1 format used 64-bit hashes");

  uint16_t numFrames = qMin((size_t) INT16_MAX, frames.size());
  if (numFrames < frames.size())
    qWarning() << "maximum 65k frames stored per video, dropping the rest";

  std::vector<uint16_t> int16Frames;
  int16Frames.reserve(numFrames);

  for (int i = 0; i < numFrames; ++i) {
    if (frames[i] > UINT16_MAX) {
      qWarning() << "maximum video frame number exceeded, dropping the rest" << int16Frames.back();
      numFrames = int16Frames.size();
      break;
    }

    int16Frames.push_back(frames[i]);
  }

  if (!io.write(&numFrames, 1, "header")) return false;

  if (!io.write(int16Frames.data(), numFrames, "frame numbers")) return false;

  if (!io.write(hashes.data(), numFrames, "hashes")) return false;

  return true;
}

bool VideoIndex::load_v1(SimpleIO& io) {
  // with 64k frame limit v1 files can't get very big
  if (!io.bufferAll()) return false;

  uint16_t numFrames = 0;
  if (!io.read(&numFrames, 1, "header")) return false;

  if (numFrames == 0) return true;

  // we do not need zeroed memory, but std::vector won't allow it
  // TODO: use std::span (c++ 20) instead which works with mmap too
  frames.resize(numFrames);
  hashes.resize(numFrames);

  {
    std::vector<uint16_t> int16Frames;
    int16Frames.resize(numFrames);

    if (!io.read(int16Frames.data(), numFrames, "frame numbers")) return false;

    uint16_t last = 0;
    for (int i = 0; i < numFrames; ++i) {
      // an old version wrote frames past 65k and wrapped,
      // prevent those from going through successfully
      uint16_t frame = int16Frames[i];
      if (frame < last) {
        qDebug() << i << last << frame;
        if (last > 65000) {
          // probably wrapped due to having too many frames
          // if it ends on max frame we assume it needs re-indexing
          qDebug() << "fixing 65k wrapping bug:" << numFrames << i << last;
          if (last != UINT16_MAX) {
            frames[i] = UINT16_MAX;
            i++;
          }

          numFrames = i;
          frames.resize(numFrames);
          hashes.resize(numFrames);
          break;
        } else {
          qWarning() << "non-sequential frame number (corrupt file?)";
          return false;
        }
      }
      last = frame;
      frames[i] = frame;
    }
  }

  if (!io.read(hashes.data(), numFrames, "hashes")) return false;

  // v2 requires first frame is 0, old version didn't always do that
  if (frames.size() && frames[0] != 0) {
    qDebug() << "fixing non-zero first frame bug";
    frames.insert(frames.begin(), 0);
    hashes.insert(hashes.begin(), 0);
  }

  if (frames.size() != hashes.size())
    qWarning() << "frames/hashes size mismatch:" << frames.size() << hashes.size();

  return true;
}
