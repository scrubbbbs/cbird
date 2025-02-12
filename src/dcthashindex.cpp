/* Index for rescaled or recompressed images
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
#include "dcthashindex.h"

#include "qtutil.h"
#include "tree/dcttree.h"

DctHashIndex::DctHashIndex() {
  _id = SearchParams::AlgoDCT;
  init();
}

void DctHashIndex::init() {
  _hashes = nullptr;
  _mediaId = nullptr;
  _numHashes = 0;
  _isLoaded = false;
  _tree = nullptr;
}

DctHashIndex::~DctHashIndex() { unload(); }

void DctHashIndex::unload() {
  // we should maybe use QVector, however since memory
  // usage could be huge, we could use the savings,
  // plus realloc() is super cheap in case QVector
  // doesn't use it (which would be dumb)
  free(_hashes);
  free(_mediaId);
  delete _tree;
  init();
}

size_t DctHashIndex::memoryUsage() const {
  // TODO: tree->memoryUsage
  return (sizeof(*_hashes) + sizeof(*_mediaId)) * size_t(_numHashes);
}

void DctHashIndex::buildTree() {
  delete _tree;
  _tree = nullptr;
  if (_numHashes > 0) {
    _tree = new DctTree;
    _tree->create(_hashes, _mediaId, _numHashes);
  }
}

void DctHashIndex::load(QSqlDatabase& db, const QString& cachePath, const QString& dataPath) {
  // hashes are always loaded from database, no caching
  (void)cachePath;
  (void)dataPath;

  if (!isLoaded()) {
    unload();

    _isLoaded = true;

    QSqlQuery query(db);
    query.setForwardOnly(true);

    // progress bar
    if (!query.exec("select count(0) from media where type=1")) SQL_FATAL(exec);
    if (!query.next()) SQL_FATAL(next);
    size_t rowCount = query.value(0).toULongLong();
    PROGRESS_LOGGER(pl, "<PL>%percent %bignum hashes", rowCount);

    if (!query.exec("select id,phash_dct from media where type=1")) SQL_FATAL(exec);

    int chunkSize = 1024;
    int capacity = 0;
    size_t currentRow = 0;

    while (query.next()) {
      if (currentRow % chunkSize == 0) {
        capacity += chunkSize;
        _hashes = strict_realloc(_hashes, capacity);
        _mediaId = strict_realloc(_mediaId, capacity);
      }

      _mediaId[currentRow] = query.value(0).toUInt();
      _hashes[currentRow] = uint64_t(query.value(1).toLongLong());

      pl.stepRateLimited(currentRow++);
    }

    _numHashes = currentRow;

    buildTree();

    pl.end();
  }
}

void DctHashIndex::save(QSqlDatabase& db, const QString& cachePath) {
  (void)db;
  (void)cachePath;
  // TODO: _tree->save()
}

QSet<mediaid_t> DctHashIndex::mediaIds(QSqlDatabase& db,
                                       const QString& cachePath,
                                       const QString& dataPath) const {
  (void) cachePath;
  (void) dataPath;
  QSet<mediaid_t> set;

  // note: hash is stored as 0 if we did not compute one
  if (_isLoaded) {
    for (int i = 0; i < _numHashes; ++i)
      if (_hashes[i] != 0) set.insert(_mediaId[i]);
    return set;
  }

  QSqlQuery query(db);
  query.setForwardOnly(true);

  if (!query.exec("select count(0) from media where type=1")) SQL_FATAL(exec);
  if (!query.next()) SQL_FATAL(next);
  size_t rowCount = query.value(0).toULongLong();
  PROGRESS_LOGGER(pl, "<PL>%percent %bignum hashes", rowCount);

  if (!query.exec("select id,phash_dct from media where type=1")) SQL_FATAL(exec);

  size_t currentRow = 0;
  while (query.next()) {
    mediaid_t id = query.value(0).toUInt();
    uint64_t hash = uint64_t(query.value(1).toLongLong());
    if (hash != 0) set.insert(id);
    pl.stepRateLimited(currentRow++);
  }
  pl.end();

  return set;
}

void DctHashIndex::add(const MediaGroup& media) {
  int end = _numHashes;
  _numHashes += media.count();

  _hashes = strict_realloc(_hashes, _numHashes);
  _mediaId = strict_realloc(_mediaId, _numHashes);

  for (int i = 0; i < media.count(); i++) {
    const Media& m = media[i];
    _hashes[i + end] = hashForMedia(m);
    _mediaId[i + end] = uint32_t(m.id());
  }

  // TODO: _tree->insert(media);
  buildTree();
}

void DctHashIndex::remove(const QVector<int>& removed) {
  if (!isLoaded()) return;

  // rather than realloc the index, nullify the removed items
  // TODO: track the amount of wasted space and compact at some point
  QSet<int> ids;
  for (int id : removed) ids.insert(id);

  for (int i = 0; i < _numHashes; i++)
    if (ids.contains(int(_mediaId[i]))) {
      _mediaId[i] = 0;
      _hashes[i] = 0;
    }

  // TODO: _tree->remove()
  buildTree();
}

QVector<Index::Match> DctHashIndex::find(const Media& m, const SearchParams& p) {
  QVector<Index::Match> results;

  uint64_t target = hashForMedia(m);
  if (!target) {
    qWarning() << "no hash for needle:" << m.path();
    return results;
  }

  if (!_tree) {
    qWarning() << "empty/null tree";
    return results;
  }
// TODO: maybe use brute if threshold is high
#if 1
  results = _tree->search(target, p.dctThresh);
#else
  // brute-force search is often fast enough
  for (int i = 0; i < _numHashes; i++) {
    int score = hamm64(target, _hashes[i]);
    if (score < p.dctThresh) {
      uint32_t id = _mediaId[i];
      if (id != 0) results.append(Index::Match(id, score));
    }
  }
#endif
  return results;
}

Index* DctHashIndex::slice(const QSet<uint32_t>& mediaIds) const {
  Q_ASSERT(isLoaded());

  DctHashIndex* chunk = new DctHashIndex;
  chunk->_numHashes = mediaIds.count();
  chunk->_hashes = strict_malloc(chunk->_hashes, chunk->_numHashes);
  chunk->_mediaId = strict_malloc(chunk->_mediaId, chunk->_numHashes);
  chunk->_isLoaded = true;

  int j = 0;
  for (int i = 0; i < _numHashes; ++i)
    if (mediaIds.contains(_mediaId[i])) {
      Q_ASSERT(j < chunk->_numHashes);
      chunk->_hashes[j] = _hashes[i];
      chunk->_mediaId[j] = _mediaId[i];
      j++;
    }

  // don't use unitialized values
  chunk->_numHashes = j;

  // tree may not like empty array
  if (chunk->_numHashes <= 0) return chunk;

  chunk->_tree = new DctTree;
  chunk->_tree->create(chunk->_hashes, chunk->_mediaId, chunk->_numHashes);

  return chunk;
}
