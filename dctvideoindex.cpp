/* Index for rescaled, clipped, recompressed videos
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
#include "dctvideoindex.h"

#include "qtutil.h"
#include "tree/hammingtree.h"

DctVideoIndex::DctVideoIndex() {
  _id = SearchParams::AlgoVideo;
  _tree = nullptr;
  _isLoaded = false;
}

DctVideoIndex::~DctVideoIndex() {
  delete _tree;
  for (auto it : _cachedIndex) delete it.second;
}

bool DctVideoIndex::isLoaded() const { return _isLoaded; }

int DctVideoIndex::count() const { return _tree ? int(_tree->size()) : 0; }

size_t DctVideoIndex::memoryUsage() const { return _tree ? _tree->stats().memory : 0; }

void DctVideoIndex::insertHashes(int mediaIndex, HammingTree* tree, const SearchParams& params) {
  QString indexPath = QString("%1/%2.vdx").arg(_dataPath).arg(_mediaId[uint32_t(mediaIndex)]);
  if (!QFileInfo(indexPath).exists()) {
    qWarning() << "index file missing:" << indexPath;
    return;
  }

  VideoIndex index;
  index.load(indexPath);

  std::vector<HammingTree::Value> values;
  for (size_t j = 0; j < index.hashes.size(); j++) {
    // drop hashes with < 5 0's or 1's (insufficient detail)
    // TODO: figure out what value is reasonable
    // TODO: drop these when creating the index
    // TODO: params
    uint64_t hash = index.hashes[j];
    if (hamm64(hash, 0) < 5 || hamm64(hash, 0xFFFFFFFFFFFFFFFF) < 5) continue;

    // drop begin/end frames if there are enough left over
    int lastFrame = index.frames[index.frames.size() - 1];
    if (lastFrame > (params.skipFrames * 2)) {
      if (index.frames[j] < params.skipFrames || index.frames[j] > lastFrame - params.skipFrames)
        continue;
    }

    // the index is a composite of the media index (not id) and
    // the frame number corresponding to the stored hash. to get back
    // to the mediaId use _mediaId array
    uint32_t treeIndex = (uint32_t(mediaIndex) << 16) | index.frames[j];

    values.push_back(HammingTree::Value(treeIndex, index.hashes[j]));
  }

  tree->insert(values);
}

void DctVideoIndex::buildTree(const SearchParams& params) {
  Q_ASSERT(isLoaded());

  if (_tree) return;

  QMutexLocker locker(&_mutex);

  if (!_tree) {
    auto* tree = new HammingTree;
    PROGRESS_LOGGER(pl, "<PL>%percent %bignum videos", _mediaId.size());
    for (size_t i = 0; i < _mediaId.size(); i++) {
      pl.step(i);
      insertHashes(int(i), tree, params);
    }
    pl.end();

    HammingTree::Stats stats = tree->stats();
    qInfo("%d hashes, %.1f MB, %d nodes, depth %d, vtrim %d",
          stats.numValues, stats.memory / 1024.0 / 1024.0, stats.numNodes, stats.maxHeight,
          params.skipFrames);

    _tree = tree;
  }
}

void DctVideoIndex::load(QSqlDatabase& db, const QString& cachePath, const QString& dataPath) {
  (void)cachePath;
  _dataPath = dataPath;

  QSqlQuery query(db);
  query.setForwardOnly(true);

  if (!query.prepare("select id from media where type=:type order by id")) SQL_FATAL(prepare);

  query.bindValue(":type", Media::TypeVideo);

  if (!query.exec()) SQL_FATAL(exec);

  delete _tree;
  _tree = nullptr;
  _mediaId.clear();
  _isLoaded = false;

  PROGRESS_LOGGER(pl, "<PL>sql query: %bignum videos", 0);

  int i = 0;
  while (query.next()) {
    _mediaId.push_back(query.value(0).toUInt());
    if (++i % 1000 == 0) pl.step(i);
  }

  if (_mediaId.size() > 0xFFFF) qFatal("maximum of %d videos can be searched", 0xFFFF);

  // lazy load the tree since findFrame may not need it
  _isLoaded = true;

  pl.end();
}

void DctVideoIndex::save(QSqlDatabase& db, const QString& cachePath) {
  (void)db;
  (void)cachePath;
}

void DctVideoIndex::add(const MediaGroup& media) {
  for (auto& m : media) _mediaId.push_back(m.id());
  delete _tree;
  _tree = nullptr;
}

void DctVideoIndex::remove(const QVector<int>& ids) {
  QSet<int> set;
  for (auto& id : ids) set.insert(id);

  decltype(_mediaId) copy;
  for (auto& id : qAsConst(_mediaId))
    if (!set.contains(id)) {
      copy.push_back(id);
    } else {
      auto it = _cachedIndex.find(id);
      if (it != _cachedIndex.end()) {
        _cachedIndex.erase(it);
        delete it->second;
      }
    }
  _mediaId = copy;
  delete _tree;
  _tree = nullptr;
}

QVector<Index::Match> DctVideoIndex::find(const Media& needle, const SearchParams& params) {
  if (needle.type() == Media::TypeImage)
    return findFrame(needle, params);
  else if (needle.type() == Media::TypeVideo)
    return findVideo(needle, params);

  return QVector<Index::Match>();
}

QVector<Index::Match> DctVideoIndex::findFrame(const Media& needle, const SearchParams& params) {
  Q_ASSERT(needle.type() == Media::TypeImage);
  qint64 start = QDateTime::currentMSecsSinceEpoch();

  const HammingTree* queryIndex = _tree;

  // optimization to search only a particular video, (future, small subset)
  if (params.target != 0) {
    if (params.verbose) qInfo("search single video");

    QMutexLocker locker(&_mutex);

    if (_cachedIndex[params.target])
      queryIndex = _cachedIndex[params.target];
    else {
      if (params.verbose) qInfo("build single video index");

      auto it = std::lower_bound(_mediaId.begin(), _mediaId.end(), params.target);
      if (it != _mediaId.end()) {
        int mediaIndex = int(it - _mediaId.begin());

        HammingTree* tree = new HammingTree;
        _cachedIndex[params.target] = tree;
        insertHashes(mediaIndex, tree, params);
        queryIndex = tree;
      } else {
        qWarning("unable to find the requested target id");
        return QVector<Index::Match>();
      }
    }

    Q_ASSERT(queryIndex);
  }

  if (!queryIndex) {
    buildTree(params);
    queryIndex = _tree;
  }

  QVector<Index::Match> results;

  uint64_t hash = needle.dctHash();
  if (hash == 0) {
    qWarning()  << "needle has no dct hash" << needle.id() << needle.path();
    return results;
  }

  std::vector<HammingTree::Match> matches;

  queryIndex->search(hash, params.dctThresh, matches);

  qint64 end = QDateTime::currentMSecsSinceEpoch();

  if (params.verbose)
    qInfo(
        "thresh=%d haystack=%dK match=%d time=%dus "
        "rate=%.2f Mhash/s [%s]",
        params.dctThresh, 0, int(matches.size()), int(end - start), double(count()) / (end - start),
        qUtf8Printable(needle.path()));

  // get 1 nearest frame for each video matched
  QMap<int, HammingTree::Match> nearest;

  for (const auto& match : matches) {
    // int dstFrame   = match.index & 0xFFFF;
    int mediaIndex = match.value.index >> 16;

    auto it = nearest.find(mediaIndex);

    if (it != nearest.end()) {
      if (match.distance < it->distance) *it = match;
    } else
      nearest.insert(mediaIndex, match);
  }

  for (const auto& match : nearest) {
    uint32_t dstFrame = match.value.index & 0xFFFF;
    uint32_t mediaIndex = match.value.index >> 16;

    Index::Match result;
    result.mediaId = _mediaId[mediaIndex];
    result.score = match.distance;

    // get the source in reference from needle if it was supplied
    int srcIn = needle.matchRange().dstIn;
    if (srcIn < 0) srcIn = 0;

    result.range = MatchRange(srcIn, int(dstFrame), 1);

    // FIXME: best match is media with most good matches,
    // not the single best match?

    results.append(result);
  }

  return results;
}

Index* DctVideoIndex::slice(const QSet<uint32_t>& mediaIds) const {
  DctVideoIndex* copy = new DctVideoIndex;
  // replicate what load() does, but use the subset
  // tree rebuilds on first query
  copy->_dataPath = _dataPath;
  copy->_isLoaded = true;
  for (auto& id : mediaIds) copy->_mediaId.push_back(id);
  return copy;
}

QVector<Index::Match> DctVideoIndex::findVideo(const Media& needle, const SearchParams& params) {
  Q_ASSERT(needle.type() == Media::TypeVideo);

  VideoIndex srcIndex;
  QVector<Index::Match> results;

  // if id == 0, it doesn't exist in the db and was indexed separately
  if (needle.id() == 0)
    srcIndex = needle.videoIndex();
  else
    srcIndex.load(QString("%1/%2.vdx").arg(_dataPath).arg(needle.id()));

  if (srcIndex.isEmpty()) {
    qWarning() << "needle video index is empty:" << needle.path();
    return results;
  }

  buildTree(params);
  const HammingTree* queryIndex = _tree;

  QMap<uint32_t, std::vector<MatchRange>> cand;

  const int lastFrame = srcIndex.frames[srcIndex.frames.size() - 1];
  for (size_t i = 0; i < srcIndex.hashes.size(); i++) {
    const int srcFrame = srcIndex.frames[i];
    const uint64_t srcHash = srcIndex.hashes[i];

    if (srcFrame < params.skipFrames || srcFrame > (lastFrame - params.skipFrames)) continue;

    std::vector<HammingTree::Match> matches;
    queryIndex->search(srcHash, params.dctThresh, matches);

    // we really only need the one closest frame for each matching video,
    // except in a corner-case where video repeats the same frame over and over (but this is rare)
    // FIXME: this implies there is a faster/better way to do this? (array of vptree?)
    struct ScoredMatch { int score; uint32_t frame; };
    std::unordered_map<int, ScoredMatch> closestMatch;

    for (const HammingTree::Match& match : matches) {
      const uint32_t dstFrame = match.value.index & 0xFFFF;
      const uint32_t dstIndex = match.value.index >> 16;
      const uint64_t dstHash = match.value.hash;

      const uint32_t id = _mediaId[dstIndex];
      if (!params.filterSelf || id != uint32_t(needle.id())) {
        const auto it = closestMatch.find(id);
        const int score = hamm64(srcHash, dstHash);
        if (it == closestMatch.end() || score < it->second.score)
          closestMatch[id] = {score, dstFrame};
      }
    }

    for (auto& closest : qAsConst(closestMatch))
      cand[closest.first].push_back(MatchRange(srcFrame, int(closest.second.frame), 1));
  }

  int nearMargin = 15; // TODO: params

  for (auto it = cand.begin(); it != cand.end(); ++it) {
    auto ranges = it.value();

    //std::sort(ranges.begin(), ranges.end()); already sorted by srcFrame

    int num = int(ranges.size());  // number of frames that matched

    // ranges are sorted by src frame, we would expect all matches
    // to also be in ascending order, so score them based on how
    // ascending they are
    int numAscending = 0;
    int lastFrame = 0;
    for (const MatchRange& range : qAsConst(ranges)) {
      // some number of frames before and after are still "nearby" because
      // the indexer removed similar consecutive frames
      int frame = range.dstIn;
      if (abs(frame-lastFrame) < nearMargin) numAscending++;
      lastFrame = frame;
    }

    int percentNear = numAscending * 100 / num;

    float shortClipMatches=0.75;

    if (num < params.minFramesMatched) {
      VideoIndex dstIndex;
      dstIndex.load(QString("%1/%2.vdx").arg(_dataPath).arg(it.key()));
      if (num < dstIndex.frames.size()*shortClipMatches) {
        if (params.verbose)
          qInfo() << "reject id" << it.key() << "too few matches" << num << "/" << dstIndex.frames.size();
        continue;
      }
    }
    if (percentNear < params.minFramesNear) {
      if (params.verbose)
        qInfo() << "reject id" << it.key() << "bad match locality" << percentNear;
      continue;
    }

    {
      Index::Match im;
      im.mediaId = it.key();
      im.score = 100 - percentNear;
      im.range.srcIn = ranges.front().srcIn;
      im.range.dstIn = it.value().front().dstIn;

      int srcLen = ranges.back().srcIn - ranges.front().srcIn;
      int dstLen = it.value().back().dstIn - it.value().front().dstIn;
      im.range.len = std::max(srcLen, dstLen);

      results.append(im);
    }
  }

  return results;
}
