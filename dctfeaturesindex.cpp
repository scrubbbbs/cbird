/* Index for cropped images
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
#include "dctfeaturesindex.h"

#include "ioutil.h"
#include "profile.h"
#include "qtutil.h"
#include "tree/hammingtree.h"

static QString cacheFile(const QString& cachePath) { return cachePath + qq("/dctfeatures.cache"); }

DctFeaturesIndex::DctFeaturesIndex() { init(); }

DctFeaturesIndex::~DctFeaturesIndex() { unload(); }

void DctFeaturesIndex::createTables(QSqlDatabase& db) const {
  QSqlQuery query(db);
  if (!query.exec("select * from kphash limit 1")) {
    if (!query.exec("create table kphash ("
                    " media_id  integer not null,"
                    " hashes    blob not null"
                    " );"))
      SQL_FATAL(exec);

    if (!query.exec("create index kphash_media_id_index on kphash(media_id);")) SQL_FATAL(exec);
  }
}

void DctFeaturesIndex::addRecords(QSqlDatabase& db, const MediaGroup& media) const {
  bool isValid = false;
  for (const Media& m : media)
    if (m.keyPointHashes().size() > 0) {
      isValid = true;
      break;
    }

  if (!isValid) return;

  QSqlQuery query(db);

  QString sql = "insert into kphash (media_id, hashes) values (:media_id, :hashes)";

  if (!query.prepare(sql)) SQL_FATAL(prepare);

  for (const Media& m : media) {
    const KeyPointHashList& hashes = m.keyPointHashes();
    if (hashes.size() > 0) {
      query.bindValue(":media_id", m.id());
      query.bindValue(":hashes", QByteArray(reinterpret_cast<const char*>(hashes.data()),
                                            int(hashes.size() * sizeof(uint64_t))));

      if (!query.exec()) SQL_FATAL(exec);
    }
  }
}

void DctFeaturesIndex::removeRecords(QSqlDatabase& db, const QVector<int>& mediaIds) const {
  QSqlQuery query(db);
  for (auto id : mediaIds)
    if (!query.exec("delete from kphash where media_id=" + QString::number(id))) SQL_FATAL(exec);
}

void DctFeaturesIndex::init() {
  _id = SearchParams::AlgoDCTFeatures;
  _tree = nullptr;
}

void DctFeaturesIndex::unload() {
  delete _tree;
  init();
}

int DctFeaturesIndex::count() const { return _tree ? int(_tree->size()) : 0; }

size_t DctFeaturesIndex::memoryUsage() const { return _tree ? _tree->stats().memory : 0; }

bool DctFeaturesIndex::isLoaded() const { return _tree != nullptr; }

void DctFeaturesIndex::load(QSqlDatabase& db, const QString& cachePath, const QString& dataPath) {
  (void)dataPath;

  const QString path = cacheFile(cachePath);
  bool stale = DBHelper::isCacheFileStale(db, path);

  if (_tree == nullptr || stale) {
    qint64 then = QDateTime::currentMSecsSinceEpoch();

    unload();
    _tree = new HammingTree;

    if (!stale) {
      qInfo("from cache");
      _tree->read(qUtf8Printable(path));
    } else {
      QSqlQuery query(db);
      query.setForwardOnly(true);

      // progress bar
      if (!query.exec("select count(0) from kphash")) SQL_FATAL(exec);
      if (!query.next()) SQL_FATAL(next);

      const uint64_t rowCount = query.value(0).toLongLong();
      uint64_t currentRow = 0;
      const QLocale locale;
      uint64_t numHashes = 0;  // total hashes seen

      std::vector<HammingTree::Value> chunk;  // build tree in chunks to reduce temp memory
      const int minChunkSize = 100000;

      if (!query.exec("select media_id,hashes from kphash")) SQL_FATAL(exec);

      while (query.next()) {
        currentRow++;

        const uint32_t mediaId = query.value(0).toUInt();
        const QByteArray hashes = query.value(1).toByteArray();

        if (size_t(hashes.size()) % sizeof(uint64_t) != 0) {
          qCritical() << "sql: ignoring invalid data @ media_id=" << mediaId;
          continue;
        }

        const uint64_t* ptr = reinterpret_cast<const uint64_t*>(hashes.constData());
        const int len = int(size_t(hashes.size()) / sizeof(uint64_t));

        for (int j = 0; j < len; j++) chunk.push_back(HammingTree::Value(mediaId, ptr[j]));

        numHashes += len;

        if (chunk.size() >= minChunkSize) {  // todo: this size seems to have some small effect
          _tree->insert(chunk);
          chunk.clear();  // no reallocation

          qInfo("sql query:<PL> %d%% %s hashes", int(currentRow * 100 / rowCount),
                qPrintable(locale.toString(numHashes)));
        }
      }
      _tree->insert(chunk);
      chunk.clear();
      save(db, cachePath);
    }

    HammingTree::Stats stats = _tree->stats();

    qInfo("%dKhash, height=%d nodes=%d %dMB %dms", stats.numValues / 1000, stats.maxHeight,
          stats.numNodes, int(stats.memory / 1000000),
          int(QDateTime::currentMSecsSinceEpoch() - then));
  }
}

void DctFeaturesIndex::save(QSqlDatabase& db, const QString& cachePath) {
  if (!isLoaded()) return;

  const QString path = cacheFile(cachePath);

  if (!DBHelper::isCacheFileStale(db, path)) return;

  qInfo() << "save tree";
  writeFileAtomically(path, [this](QFile& f) { _tree->write(f); });
}

void DctFeaturesIndex::add(const MediaGroup& media) {
  if (media.count() <= 0 || !isLoaded()) return;

  std::vector<HammingTree::Value> values;
  for (const Media& m : media)
    for (uint64_t hash : m.keyPointHashes()) values.push_back(HammingTree::Value(m.id(), hash));

  _tree->insert(values);
}

void DctFeaturesIndex::remove(const QVector<int>& ids) {
  if (ids.count() <= 0 || !isLoaded()) return;

  std::unordered_set<HammingTree::index_t> indices;
  for (int id : ids) indices.insert(uint32_t(id));

  _tree->remove(indices);
}

Index* DctFeaturesIndex::slice(const QSet<uint32_t>& mediaIds) const {
  DctFeaturesIndex* chunk = new DctFeaturesIndex;

  qint64 then = QDateTime::currentMSecsSinceEpoch();

  // note: probably faster to slice from cache if it's available
  std::unordered_set<HammingTree::index_t> ids;
  for (uint32_t id : mediaIds.values()) ids.insert(id);

  chunk->_tree = _tree->slice(ids);

  HammingTree::Stats stats = chunk->_tree->stats();

  qInfo("%dKhash, height=%d nodes=%d %dMB %dms", stats.numValues / 1000, stats.maxHeight,
        stats.numNodes, int(stats.memory / 1000000),
        int(QDateTime::currentMSecsSinceEpoch() - then));

  return chunk;
}

QVector<Index::Match> DctFeaturesIndex::find(const Media& needle, const SearchParams& params) {
  KeyPointHashList hashes = needle.keyPointHashes();

  //
  // for each needle hash
  // - find the closest hash
  // - image with most matches wins
  //
  uint64_t now, then = nanoTime();

  if (hashes.size() <= 0) {
    // if we don't have hashes for the needle,
    // we can get them from tree
    if (needle.id() > 0) _tree->findIndex(needle.id(), hashes);

    if (hashes.size() <= 0) {
      qWarning() << "no hashes for needle id" << needle.id() << needle.path();
      return QVector<Index::Match>();
    }
  }

  const int numNeedleHashes = hashes.size();
  uint64_t nHash[numNeedleHashes];

  for (int j = 0; j < numNeedleHashes; j++) nHash[j] = hashes[j];

  // todo: investigate if it may be possible to prune the search
  // - if a hash has no matches, nearby hashes probably also have no matches
  std::vector<HammingTree::Match> cand[numNeedleHashes];
  for (int j = 0; j < numNeedleHashes; j++) _tree->search(nHash[j], params.dctThresh, cand[j]);

  QMap<uint32_t, uint32_t> matches;  // map
  QMap<uint32_t, int> scores;
  uint32_t maxMatches = 0;

  for (int j = 0; j < numNeedleHashes; j++) {
    // take the first 10, which gives us the 10 best matches
    int len = std::min(10, (int)cand[j].size());
    for (int k = 0; k < len; k++) {
      const HammingTree::Match& match = cand[j][k];
      int index = match.value.index;

      // zero index means deleted, negative must be bogus
      if (index <= 0) continue;

      uint64_t hash = match.value.hash;
      Q_ASSERT(hamm64(hash, nHash[j]) < params.dctThresh);

      int mediaId = index;

      if (matches.contains(mediaId)) {
        matches[mediaId]++;
        scores[mediaId] += match.distance;
      } else {
        matches[mediaId] = 1;
        scores[mediaId] = match.distance;
      }

      if (needle.id() != mediaId) maxMatches = std::max(matches[mediaId], maxMatches);
    }
  }

  now = nanoTime();
  if (params.verbose)
    qInfo("%d features, %lld results, %.1f ms rate=%.1f Mhash/sec", numNeedleHashes,
          matches.count(), (now - then) / 1000000.0,
          (_tree->size() * numNeedleHashes) / ((now - then) / 1000.0));

  QVector<Index::Match> results;

  for (uint32_t mediaId : matches.keys())
    if (matches[mediaId] > 0) {
      Index::Match match;
      match.mediaId = mediaId;
      match.score = 0;

      float avgScore = (float)scores[mediaId] / matches[mediaId];

      // qDebug("score=%.2f matches=%d maxMatches=%d", avgScore, matches[mediaId], maxMatches);
      if (mediaId == uint32_t(needle.id()))
        match.score = -1;
      else if (maxMatches == 1) {
        // only one match, use the avg score
        match.score = 10 * avgScore;
      } else {
        // more matches gets lower score
        // quality of each match is controlled by params.dctThresh
        match.score = maxMatches - matches[mediaId];
      }

      results.append(match);
    }

  return results;
}
