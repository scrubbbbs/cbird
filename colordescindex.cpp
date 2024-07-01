/* Index for similar-color search
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
#include "colordescindex.h"
#include "qtutil.h"

#include <cfloat>

ColorDescIndex::ColorDescIndex() : Index() {
  _id = SearchParams::AlgoColor;
  _count = 0;
  _mediaId = nullptr;
  _descriptors = nullptr;
}

ColorDescIndex::~ColorDescIndex() { unload(); }

void ColorDescIndex::createTables(QSqlDatabase& db) const {
  QSqlQuery query(db);

  if (!query.exec("select * from color limit 1")) {
    if (!query.exec("create table color ("
                    " media_id  integer not null,"
                    " color_desc  blob not null"
                    " );"))
      SQL_FATAL(prepare);

    // this index is big, but necessary for fast deletions
    if (!query.exec("create unique index color_media_id_index on color(media_id);"))
      SQL_FATAL(exec);
  }
}

void ColorDescIndex::addRecords(QSqlDatabase& db, const MediaGroup& media) const {
  bool isValid = false;
  for (const Media& m : media)
    if (m.colorDescriptor().numColors > 0) {
      isValid = true;
      break;
    }

  if (!isValid) return;

  QSqlQuery query(db);

  if (!query.prepare("insert into color "
                     "(media_id,  color_desc) values "
                     "(:media_id, :color_desc)"))
    SQL_FATAL(prepare);

  for (const Media& m : media) {
    const ColorDescriptor& desc = m.colorDescriptor();
    if (desc.numColors > 0) {
      auto bytes =
          QByteArray(reinterpret_cast<const char*>(&m.colorDescriptor()), sizeof(ColorDescriptor));
      query.bindValue(":media_id", m.id());
      query.bindValue(":color_desc", bytes);

      if (!query.exec()) {
        qDebug() << "id=" << m.id() << m.path();
        SQL_FATAL(exec);
      }
    }
  }
}

void ColorDescIndex::removeRecords(QSqlDatabase& db, const QVector<int>& mediaIds) const {
  QSqlQuery query(db);
  for (auto id : mediaIds)
    if (!query.exec("delete from color where media_id=" + QString::number(id))) SQL_FATAL(exec);
}

void ColorDescIndex::unload() {
  free(_mediaId);
  free(_descriptors);

  _count = 0;
  _mediaId = nullptr;
  _descriptors = nullptr;
}

bool ColorDescIndex::isLoaded() const { return _count > 0; }

int ColorDescIndex::count() const { return _count; }

size_t ColorDescIndex::memoryUsage() const {
  size_t num = size_t(count());
  return sizeof(ColorDescriptor) * num + sizeof(int) * num;
}

void ColorDescIndex::load(QSqlDatabase& db, const QString& cachePath, const QString& dataPath) {
  // hashes are always loaded from database, no caching
  (void)cachePath;
  (void)dataPath;

  if (isLoaded()) return;

  unload();

  QSqlQuery query(db);

  // item count for memory allocation
  if (!query.exec("select count(0) from color")) SQL_FATAL(exec);
  if (!query.next()) SQL_FATAL(next);

  _count = query.value(0).toInt();
  if (_count == 0) return;

  PROGRESS_LOGGER(pl, "<PL>%percent %bignum descriptors", _count);

  // allocate using malloc so we can use realloc() later
  _descriptors = strict_malloc(_descriptors, _count);
  _mediaId = strict_malloc(_mediaId, _count);

  query.exec("select media_id,color_desc from color");
  if (!query.first()) SQL_FATAL(exec);

  int i = 0;
  do {
    // buffer overflow guard
    if (i >= _count) {
      qCritical() << "database modified during loading:" << (i - _count + 1)
                  << "new records ignored";
      break;
    }

    _mediaId[i] = query.value(0).toUInt();

    // convert the blob
    QByteArray bytes = query.value(1).toByteArray();
    if (bytes.length() == sizeof(ColorDescriptor))
      memcpy(_descriptors + i, bytes.constData(), sizeof(ColorDescriptor));
    else {
      // this should not happen anymore since addRecords() prevents it
      _descriptors[i].clear();
      qWarning("no color desc for id %d, correct by re-indexing", _mediaId[i]);
    }
    i++;

    if (i % 20000 == 0)
      pl.step(i);

  } while (query.next());
  pl.end();
}

void ColorDescIndex::save(QSqlDatabase& db, const QString& cachePath) {
  // no caching
  (void)db;
  (void)cachePath;
}

void ColorDescIndex::add(const MediaGroup& media) {
  int end = _count;
  _count += media.count();

  _mediaId = strict_realloc(_mediaId, _count);
  _descriptors = strict_realloc(_descriptors, _count);

  for (int i = 0; i < media.count(); i++) {
    const Media& m = media[i];
    _mediaId[i + end] = uint32_t(m.id());
    _descriptors[i + end] = m.colorDescriptor();
  }
}

void ColorDescIndex::remove(const QVector<int>& toRemove) {
  if (!isLoaded()) return;

  // rather than realloc the index we can nullify the removed items
  // todo: track the amount of wasted space and compact at some point
  QSet<int> ids;
  for (int id : toRemove) ids.insert(id);

  for (int i = 0; i < _count; i++)
    if (ids.contains(int(_mediaId[i]))) {
      _mediaId[i] = 0;
      _descriptors[i].clear();
    }
}

bool ColorDescIndex::findIndexData(Media& m) const {
  uint32_t id = uint32_t(m.id());
  for (int i = 0; i < _count; i++)
    if (_mediaId[i] == id) {
      m.setColorDescriptor(_descriptors[i]);
      return true;
    }
  return false;
}

Index* ColorDescIndex::slice(const QSet<uint32_t>& mediaIds) const {
  ColorDescIndex* chunk = new ColorDescIndex;
  chunk->_count = mediaIds.count();
  chunk->_descriptors = strict_malloc(chunk->_descriptors, chunk->_count);
  chunk->_mediaId = strict_malloc(chunk->_mediaId, chunk->_count);

  int j = 0;
  for (int i = 0; i < _count; ++i)
    if (mediaIds.contains(_mediaId[i])) {
      Q_ASSERT(j < chunk->_count);
      chunk->_descriptors[j] = _descriptors[i];
      chunk->_mediaId[j] = _mediaId[i];
      j++;
    }
  chunk->_count = j;

  return chunk;
}

QVector<Index::Match> ColorDescIndex::find(const Media& m, const SearchParams& p) {
  (void)p;

  // todo: search tree for histograms

  QVector<Index::Match> results;

  ColorDescriptor target = m.colorDescriptor();
  if (target.numColors <= 0) {
    Media tmp = m;
    if (findIndexData(tmp))
      target = tmp.colorDescriptor();
    else
      qWarning() << "needle has no color descriptor" << m.id() << m.path();
  }

  for (int i = 0; i < _count; i++) {
    float distance = ColorDescriptor::distance(target, _descriptors[i]);

    if (distance < FLT_MAX) {
      uint32_t id = _mediaId[i];
      if (id != 0) results.append(Index::Match(id, int(distance)));
    }
  }

  return results;
}
