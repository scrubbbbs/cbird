/* Index for rescaled, clipped, recompressed videos
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
#include "dctvideoindex.h"

#include "qtutil.h"
// #include "tree/hammingtree.h"
#include "tree/radix.h"
#include <unordered_map>
#include <cinttypes>

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

int DctVideoIndex::count() const {
  // this returns 0 if isLoaded==true but we didn't buildTree()
  // return _tree ? int(_tree->size()) : 0;
  return _mediaId.size();
}

size_t DctVideoIndex::memoryUsage() const {
  return _tree ? _tree->stats().memory : 0;
}

DctVideoIndex::VStat DctVideoIndex::insertHashes(mediaid_t mediaIndex,
                                                 VideoSearchTree* tree,
                                                 const SearchParams& params) {
  QString indexPath = QString("%1/%2.vdx").arg(_dataPath).arg(_mediaId[uint32_t(mediaIndex)]);
  if (!QFileInfo(indexPath).exists()) {
    qWarning() << "index file missing:" << indexPath;
    return {0, 0};
  }

  VideoIndex index;
  index.load(indexPath);

  if (index.frames.size() == 0) {
    return {0, 0};
  }

  const int lastFrame = index.frames[index.frames.size() - 1];
  const int skip = params.skipFrames;

  std::vector<VideoSearchTree::Value> values;
  values.reserve(index.hashes.size());

  for (size_t j = 0; j < index.hashes.size(); ++j) {
    // drop hashes with < 5 0's or 1's (insufficient detail)
    // TODO: figure out what value is reasonable
    // TODO: drop these when creating the index
    // TODO: params
    dcthash_t hash = index.hashes[j];
    if (hamm64(hash, 0) < 5 || hamm64(hash, 0xFFFFFFFFFFFFFFFF) < 5) continue;

    // drop begin/end frames if there are enough left over
    int frame = index.frames[j];
    if (skip && lastFrame / 2 > skip) {
      if (frame < skip || frame > lastFrame - skip) continue;
    }

    VideoTreeIndex treeIndex;
    treeIndex.idx = mediaIndex;
    treeIndex.frame = frame;

    values.push_back(VideoSearchTree::Value(treeIndex, index.hashes[j]));
  }

  // std::sort(values.begin(), values.end(), [tree](auto& a, auto& b) {
  //   return tree->indexOf(a.hash) < tree->indexOf(b.hash);
  // });

  tree->insert(values);

  return {uint64_t(lastFrame), values.size()};
}

void DctVideoIndex::buildTree(const SearchParams& params) {
  Q_ASSERT(isLoaded());

  if (_tree) return;

  QMutexLocker locker(&_mutex);

  if (!_tree) {
    VStat sum{0, 0};
    // auto* tree = new VideoSearchTree;
    auto* tree = new VideoSearchTree(params.videoRadix);
    QElapsedTimer timer; // don't spam progress prints
    timer.start();
    PROGRESS_LOGGER(pl, "<PL>%percent %step videos", _mediaId.size());
    for (size_t i = 0; i < _mediaId.size(); ++i) {
      if (timer.elapsed() > 100) {
        pl.step(i);
        timer.start();
      }
      VStat st = insertHashes(mediaid_t(i), tree, params);
      sum.videoFrames += st.videoFrames;
      sum.usedFrames += st.usedFrames;
    }
    pl.end();

    // auto stats = tree->stats();
    // qInfo("%" PRIu64 " frames, %" PRIu64
    //       " hashes, %d:1, %.1f MB, %d nodes, %d%% small, depth %d, vtrim %d",
    //       sum.videoFrames,
    //       sum.usedFrames,
    //       sum.usedFrames > 0 ? int(sum.videoFrames / sum.usedFrames) : 1,
    //       stats.memory / 1024.0 / 1024.0,
    //       stats.numNodes,
    //       stats.numNodes > 0 ? stats.smallNodes * 100 / stats.numNodes : 0,
    //       stats.maxHeight,
    //       params.skipFrames);

    auto toKb = [](size_t bytes) { return int(bytes + 1024) / 1024; };

    auto stats = tree->stats();
    qInfo("%'" PRIu64 " frames, %'" PRIu64 " hashes, %d:1, %.1f MB, vtrim %d",
          sum.videoFrames,
          sum.usedFrames,
          sum.usedFrames > 0 ? int(sum.videoFrames / sum.usedFrames) : 1,
          stats.memory / 1024.0 / 1024.0,
          params.skipFrames);

    qInfo("%'d buckets, %'d empty, sizes(KB): min:%'d max:%'d avg:%'d variance:%d%%",
          stats.numBuckets,
          stats.empty,
          toKb(stats.min),
          toKb(stats.max),
          toKb(stats.mean),
          stats.mean > 0 ? stats.sigma * 100 / stats.mean : 0);

    _tree = tree;
  }
}

void DctVideoIndex::load(QSqlDatabase& db, const QString& cachePath, const QString& dataPath) {
  (void)cachePath;
  _dataPath = dataPath;

  QSqlQuery query(db);
  query.setForwardOnly(true);

  if (!query.prepare("select count(0) from media where type=:type")) SQL_FATAL(exec);
  query.bindValue(":type", Media::TypeVideo);
  if (!query.exec()) SQL_FATAL(exec);
  if (!query.next()) SQL_FATAL(next);
  const uint64_t rowCount = query.value(0).toLongLong();

  if (!query.prepare("select id from media where type=:type order by id")) SQL_FATAL(prepare);
  query.bindValue(":type", Media::TypeVideo);
  if (!query.exec()) SQL_FATAL(exec);

  delete _tree;
  _tree = nullptr;
  _mediaId.clear();
  _isLoaded = false;

  PROGRESS_LOGGER(pl, "querying:<PL> %percent %step rows", rowCount);

  size_t i = 0;
  while (query.next()) {
    _mediaId.push_back(query.value(0).toUInt());
    if (_mediaId.size() == MAX_VIDEOS_PER_INDEX) {
      qCritical("maximum of %d videos can be searched, remaining videos will be ignored",
                MAX_VIDEOS_PER_INDEX);
      break;
    }
    pl.stepRateLimited(i++);
  }

  // lazy load the tree since findFrame may not need it
  _isLoaded = true;

  pl.end();
}

void DctVideoIndex::save(QSqlDatabase& db, const QString& cachePath) {
  (void)db;
  (void) cachePath;
}

QSet<mediaid_t> DctVideoIndex::mediaIds(QSqlDatabase& db,
                                        const QString& cachePath,
                                        const QString& dataPath) const {
  (void) cachePath;

  QSet<mediaid_t> result;
  if (isLoaded()) {
    for (auto& id : _mediaId)
      result.insert(id);
    return result;
  }

  QSqlQuery query(db);
  query.setForwardOnly(true);

  if (!query.prepare("select count(0) from media where type=:type")) SQL_FATAL(exec);
  query.bindValue(":type", Media::TypeVideo);
  if (!query.exec()) SQL_FATAL(exec);
  if (!query.next()) SQL_FATAL(next);
  const uint64_t rowCount = query.value(0).toLongLong();

  if (!query.prepare("select id from media where type=:type")) SQL_FATAL(prepare);
  query.bindValue(":type", Media::TypeVideo);

  if (!query.exec()) SQL_FATAL(exec);

  PROGRESS_LOGGER(pl, "querying:<PL> %percent %step rows", rowCount);
  size_t i = 0;
  while (query.next()) {
    uint32_t id = query.value(0).toUInt();
    QString indexPath = QString("%1/%2.vdx").arg(dataPath).arg(id);
    if (QFile::exists(indexPath)) result.insert(id);
    pl.stepRateLimited(i++);
  }
  pl.end();
  return result;
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

  const VideoSearchTree* queryIndex = _tree;

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

        auto* tree = new VideoSearchTree(params.videoRadix);

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

  std::vector<VideoSearchTree::Match> matches;

  queryIndex->search(hash, params.dctThresh, matches);

  qint64 end = QDateTime::currentMSecsSinceEpoch();

  if (params.verbose)
    qInfo(
        "thresh=%d haystack=%dK match=%d time=%dus "
        "rate=%.2f Mhash/s [%s]",
        params.dctThresh, 0, int(matches.size()), int(end - start), double(count()) / (end - start),
        qUtf8Printable(needle.path()));

  // get 1 nearest frame for each video matched
  QMap<mediaid_t, VideoSearchTree::Match> nearest;

  for (const auto& match : matches) {
    mediaid_t mediaIndex = match.value.index.idx;

    auto it = nearest.find(mediaIndex);

    if (it != nearest.end()) {
      if (match.distance < it->distance) *it = match;
    } else
      nearest.insert(mediaIndex, match);
  }

  for (const auto& match : nearest) {
    mediaid_t mediaIndex = match.value.index.idx;
    int dstFrame = match.value.index.frame;

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

  QVector<Index::Match> results;

  buildTree(params);
  //const VideoSearchTree* queryIndex = _tree;

  std::vector<VideoSearchTree::Value> srcData;

  {
    VideoIndex srcIndex;
    // if id == 0, it doesn't exist in the db and was indexed separately
    if (needle.id() == 0)
      srcIndex = needle.videoIndex();
    else
      srcIndex.load(QString("%1/%2.vdx").arg(_dataPath).arg(needle.id()));

    if (srcIndex.isEmpty()) {
      qWarning() << "needle video index is empty:" << needle.path();
      return results;
    }

    if (srcIndex.frames.size() != srcIndex.hashes.size()) {
      qWarning() << "ignoring corrupt video index:" << needle.id();
      return results;
    }

    srcData.reserve(srcIndex.frames.size());
    auto lastFrame = srcIndex.frames[srcIndex.frames.size() - 1];
    for (size_t i = 0; i < srcIndex.frames.size(); ++i) {
      auto srcFrame = srcIndex.frames[i];
      if (srcFrame < params.skipFrames || srcFrame > (lastFrame - params.skipFrames)) continue;

      VideoTreeIndex index;
      index.frame = srcFrame;

      VideoSearchTree::Value v(index, srcIndex.hashes[i]);
#if 1
      // sort needles by bucket, this should improve cache utilization
      // also required for vector search
      if (srcData.size() > 0)
        srcData.insert(std::upper_bound(srcData.begin(),
                                        srcData.end(),
                                        v,
                                        [this](auto& a, auto& b) {
                                          auto pa = _tree->indexOf(a.hash);
                                          auto pb = _tree->indexOf(b.hash);
                                          return pa < pb;
                                        }),
                       v);
      else
#endif
        srcData.push_back(VideoSearchTree::Value(index, srcIndex.hashes[i]));
    };
  }

  // pull out some constants to help optimizer
  const bool filterSelf = params.filterSelf;
  mediaid_t needleId = needle.id();

  QMap<mediaid_t, std::vector<MatchRange>> cand; // potential matches before filtering

  struct ScoredMatch
  {
    int score;
    int frame;
  };
  std::unordered_map<mediaid_t, ScoredMatch> closestMatch; // mediaId to closest match

  // tree search returns all matches below threshold, we only need one per
  // matching video
  // NOTE: if the video matched the same frame many times, that would
  // not improve the result score...like perhaps a slideshow or webcast
  // with a mostly static frames
  const auto reduceMatches = [&closestMatch,
                              &filterSelf,
                              &needleId,
                              &cand,
                              &params,
                              this](VideoSearchTree::hash_t queryHash,
                                    int queryFrame,
                                    const std::vector<VideoSearchTree::Match>& matches) {
    // reuse this to reduce allocations
    closestMatch.clear();

    for (const auto& match : std::as_const(matches)) {
      mediaid_t matchIndex = match.value.index.idx;
      int matchFrame = match.value.index.frame;
      dcthash_t matchHash = match.value.hash;

      // we have this remapping since real mediaId can be much
      // larger than VideoTreeIndex::idx which is 24-bit to save memory
      mediaid_t id = std::as_const(_mediaId)[matchIndex];

      if (Q_UNLIKELY(id == needleId) && filterSelf) {
        // if (params.verbose) qInfo("reject id %d == myself", id);
        continue;
      }

      const auto it = closestMatch.find(id);
      int matchScore = hamm64(queryHash, matchHash);
      if (it == closestMatch.end() || matchScore < it->second.score)
        closestMatch[id] = {matchScore, matchFrame};
    }

    // add the closest match to the list of potential matches
    const int matchLen = 1; // to be determined
    for (auto& closest : qAsConst(closestMatch))
      cand[closest.first].push_back(MatchRange(queryFrame, int(closest.second.frame), matchLen));
  };

//
// Vector search experiment; each search query is fixed-length vector
// known to map to the same bucket. Requires hashes to already be sorted
// by bucket when loading above.
//
// In theory it performs better due to fewer cache misses. If the vector
// is the right length, maybe it also fits in avx register. The trade-off
// is that we have to compute hashes even if we don't have one to put
// in the vector (must be filled with 0s).
//
// This implementation is slower than non-vector version,
// even when bucket spills from L3, but it is probably needed for GPU version
//
#if 0
  FIXME: this is broken when video searching for itself?

  const int maxLen = VideoSearchTree::vectorSize;
  VideoSearchTree::hash_t queryVector[maxLen]; // clump of hashes mapped to the same bucket
  std::vector<VideoSearchTree::Match> matchVector[maxLen];
  const auto& frames = srcData;

  for (size_t i = 0; i < frames.size();) {
    // fill the search vector with hashes from the same bucket,
    // if there are not enough, pad with 0
    auto hash = frames[i].hash;
    auto nextBucket = _tree->indexOf(hash);
    const auto queryBucket = nextBucket;

    size_t queryLength = 0;
    while (nextBucket == queryBucket && queryLength < maxLen) {
      queryVector[queryLength] = hash;
      matchVector[queryLength].clear();
      queryLength++;

      i++;
      if (i >= frames.size()) break;

      hash = frames[i].hash;
      nextBucket = _tree->indexOf(hash);
    }

    for (size_t k = queryLength; k < maxLen; ++k) {
      queryVector[k] = 0;
      matchVector[k].clear();
    }

    _tree->search(queryVector, params.dctThresh, matchVector);

    size_t j = 0;
    for (auto& matches : std::as_const(matchVector)) {
      const auto queryHash = queryVector[j];
      const auto queryFrame = frames[i - queryLength + j].index.frame;
      j++;
      reduceMatches(queryHash, queryFrame, matches);
    }
  }
#else
  std::vector<VideoSearchTree::Match> matches;
  for (const auto& frame : std::as_const(srcData)) {
    const uint64_t queryHash = frame.hash;
    const int queryFrame = frame.index.frame;
    matches.clear();

    // qInfo("0x%08x %d", (int) _tree->addressOf(queryHash), queryFrame);

    _tree->search(queryHash, params.dctThresh, matches);

    reduceMatches(queryHash, queryFrame, matches);
  }
#endif

  //
  // Try to form a contiguous match range
  //
  // Consider frames to be close enough within some margin,
  // since we discard a lot of frames in the indexer
  //
  // NOTE: the indexer compresses frames with a variable-length
  // window that has no upper bound. So for videos with that
  // that are mostly static (with respect to dcthash) this "margin"
  // approach will likely not be reliable.
  //
  const int frameMargin = 15; // TODO: params

  for (auto it = cand.begin(); it != cand.end(); ++it) {
    auto& ranges = it.value();

    // query hashes are pre-sorted for optimal lookup,
    // re-sort by query frame number
    std::sort(ranges.begin(), ranges.end());

    // If every match was perfect, and no frames were discarded,
    // we would expect all matches to be in ascending order.
    // However we discard a lot of frames, so score based on adjacency,
    // which allows forgiving some amount of out-of-order and more
    // distant frames.
    int numAdjacent = 0;
    int lastFrame = 0;
    for (const MatchRange& range : qAsConst(ranges)) {
      int frame = range.dstIn;
      if (abs(frame - lastFrame) < frameMargin) numAdjacent++;
      lastFrame = frame;
    }

    int num = int(ranges.size());              // number of frames that matched
    int percentNear = numAdjacent * 100 / num; // the scoring metric

    // the size of the chunk matched is also a good indicator
    if (num < params.minFramesMatched) {
      if (params.verbose)
        qInfo() << "reject id" << it.key() << "too few matches" << num << "/"
                << params.minFramesMatched;
      continue;
      // FIXME: this is extremely bad being inside the loop..need to retain
      // this when building the tree
      // for short videos, decrease minFramesMatched..but why 0.75??
      float shortClipMatches = 0.75;
      VideoIndex dstIndex;
      dstIndex.load(QString("%1/%2.vdx").arg(_dataPath).arg(it.key()));
      if (num < dstIndex.frames.size() * shortClipMatches) {
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

    Index::Match im;
    im.mediaId = it.key();
    im.score = 100 - percentNear;
    im.range.srcIn = ranges.front().srcIn;
    im.range.dstIn = ranges.front().dstIn;

    int srcLen = ranges.back().srcIn - im.range.srcIn;
    int dstLen = ranges.back().dstIn - im.range.dstIn;
    im.range.len = std::max(srcLen, dstLen);

    results.append(im);
  }

  return results;
}
