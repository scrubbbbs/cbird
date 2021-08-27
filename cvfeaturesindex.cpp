#include "cvfeaturesindex.h"
#include "cvutil.h"
#include "ioutil.h"
#include "profile.h"
#include "qtutil.h"

// there is a breaking change to flann interface at some point
#if !(CV_VERSION_EPOCH == 2 && CV_VERSION_MAJOR >= 4 && CV_VERSION_MINOR >= 13)
#error OpenCV 2.4.13+ is required
#endif

CvFeaturesIndex::CvFeaturesIndex() {
  _id = SearchParams::AlgoCVFeatures;
  _index = nullptr;
}

CvFeaturesIndex::~CvFeaturesIndex() { delete _index; }

void CvFeaturesIndex::createTables(QSqlDatabase& db) const {
  QSqlQuery query(db);

  if (!query.exec("select * from matrix limit 1")) {
    if (!query.exec("create table matrix ("
                    " id       integer primary key not null,"
                    " media_id integer not null,"
                    " rows     integer not null,"
                    " cols     integer not null,"
                    " type     integer not null,"
                    " stride   integer not null,"
                    " data     blob not null"
                    " );"))
      SQL_FATAL(exec);

    if (!query.exec("create index matrix_media_id_index on matrix(media_id);"))
      SQL_FATAL(exec);
  }
}

void CvFeaturesIndex::addRecords(QSqlDatabase& db,
                                 const MediaGroup& media) const {
  bool isValid = false;
  for (const Media& m : media)
    if (m.keyPointDescriptors().total() > 0) {
      isValid = true;
      break;
    }

  if (!isValid) return;

  QSqlQuery query(db);

  if (!query.prepare("insert into matrix "
                     "(media_id,  rows,  cols,  type,  stride,  data) values "
                     "(:media_id, :rows, :cols, :type, :stride, :data)"))
    SQL_FATAL(prepare);

  for (const Media& m : media)
    if (m.keyPointDescriptors().total() > 0) {
      const KeyPointDescriptors& d = m.keyPointDescriptors();

      query.bindValue(":media_id", m.id());
      query.bindValue(":rows", d.size().height);
      query.bindValue(":cols", d.size().width);
      query.bindValue(":type", d.type());
      query.bindValue(":stride", int(uint(d.size().width) * d.elemSize()));
      query.bindValue(":data", qCompress(matrixData(d)));
      if (!query.exec()) SQL_FATAL(exec);
    }
}

void CvFeaturesIndex::removeRecords(QSqlDatabase& db,
                                    const QVector<int>& mediaIds) const {
  QSqlQuery query(db);
  for (auto id : mediaIds)
    if (!query.exec("delete from matrix where media_id=" + QString::number(id)))
      SQL_FATAL(exec);
}

bool CvFeaturesIndex::isLoaded() const { return _index != nullptr; }

int CvFeaturesIndex::count() const { return _descriptors.rows; }

size_t CvFeaturesIndex::memoryUsage() const {
  size_t mem = 0;
  if (_descriptors.rows > 0) {
    const cv::Mat& d = _descriptors;

    mem += uint(d.rows * d.cols) * d.elemSize();

    // don't really know how much lsh index uses
    mem *= 2;

    // fixme:also memory for lookup trees
  }

  return mem;
}

void CvFeaturesIndex::add(const MediaGroup& media) {
  if (_index) {
    cv::Mat addedDescriptors;

    for (const Media& m : media) {
      const KeyPointDescriptors& desc = m.keyPointDescriptors();

      uint32_t mid = uint32_t(m.id());
      uint32_t numDesc = uint32_t(_descriptors.rows);
      _idMap[mid] = numDesc;
      _indexMap[numDesc] = mid;

      numDesc += uint32_t(desc.rows);
      _idMap[UINT32_MAX] = numDesc;
      _indexMap[numDesc] = 0;

      for (int j = 0; j < desc.rows; j++) {
        _descriptors.push_back(desc.row(j));
        addedDescriptors.push_back(desc.row(j));
      }
    }

    buildIndex(addedDescriptors);
  }
}

void CvFeaturesIndex::remove(const QVector<int>& ids) {
  for (int id : ids) {
    auto it = _idMap.find(uint32_t(id));
    if (it != _idMap.end()) {
      uint32_t index = it->second;
      auto it2 = _indexMap.find(index);
      if (it2 != _indexMap.end()) {
        // fixme: crashes here sometimes
        Q_ASSERT(int(it2->second) == id);
        it2->second = 0;
      }
    }
  }
}

void CvFeaturesIndex::load(QSqlDatabase& db, const QString& cachePath,
                           const QString& dataPath) {
  (void)dataPath;

  qint64 then = QDateTime::currentMSecsSinceEpoch();

  bool stale = DBHelper::isCacheFileStale(db, indexFile(cachePath));

  if (!_index || stale) {
    _descriptors = cv::Mat();
    delete _index;
    _index = nullptr;

    if (!stale) {
      qInfo("from cache");
      loadIndex(cachePath);
    } else {
      QSqlQuery query(db);
      query.setForwardOnly(true);
      query.exec(
          "select media_id,rows,cols,type,stride,data from matrix order by "
          "media_id");

      size_t nextProgress = 50000;
      uint32_t numDesc = 0;

      size_t i = 0;
      uint32_t lastId = 0;

      while (query.next()) {
        uint32_t id;
        int rows, cols, type, stride;
        QByteArray data;
        KeyPointDescriptors desc;

        id = query.value(0).toUInt();
        rows = query.value(1).toInt();
        cols = query.value(2).toInt();
        type = query.value(3).toInt();
        stride = query.value(4).toInt();
        data = query.value(5).toByteArray();
        data = qUncompress(data);

        loadMatrix(rows, cols, type, stride, data, desc);

        Q_ASSERT(lastId < id);  // must be true for _idMap to work
        Q_ASSERT(desc.type() == type);
        Q_ASSERT(desc.size().width == cols);
        Q_ASSERT(desc.size().height == rows);

        // smoosh all features into one big cv::Mat
        for (int j = 0; j < desc.rows; j++) _descriptors.push_back(desc.row(j));

        // maps to get back to the media or descriptors associated with media
        _idMap[id] = numDesc;
        _indexMap[numDesc] = id;

        i++;
        numDesc += uint32_t(desc.rows);
        lastId = id;

        if (numDesc > nextProgress) {
          printf("CvFeaturesIndex::load: query %d\r", int(numDesc));
          fflush(stdout);
          nextProgress = numDesc + 50000;
        }
      }
      printf("\n");
      assert(_descriptors.rows == int(numDesc));

      // build the actual flann index
      buildIndex(cv::Mat());

      saveIndex(cachePath);
    }

    // trailing values to get length of last value
    _idMap[UINT32_MAX] = uint32_t(_descriptors.rows);
    _indexMap[uint32_t(_descriptors.rows)] = 0;
  }

  qInfo("%d descriptors %dms %dMB", _descriptors.rows,
        int(QDateTime::currentMSecsSinceEpoch() - then),
        int(memoryUsage() / 1000000));
}

Index* CvFeaturesIndex::slice(const QSet<uint32_t>& mediaIds) const {
  CvFeaturesIndex* chunk = new CvFeaturesIndex;

  // this mirrors what load() is doing
  uint32_t numDesc = 0;
  auto values = mediaIds.values();
  std::sort(values.begin(), values.end());
  for (uint32_t id : values) {
    cv::Mat desc = descriptorsForMediaId(id);
    if (desc.rows > 0) {
      for (int j = 0; j < desc.rows; j++)
        chunk->_descriptors.push_back(desc.row(j));
      chunk->_idMap[id] = numDesc;
      chunk->_indexMap[numDesc] = id;
      numDesc += uint(desc.rows);
    }
  }
  chunk->_idMap[UINT32_MAX] = numDesc;
  chunk->_indexMap[numDesc] = 0;

  Q_ASSERT(chunk->_descriptors.rows == int(numDesc));

  chunk->buildIndex(cv::Mat());

  return chunk;
}

void CvFeaturesIndex::save(QSqlDatabase& db, const QString& cachePath) {
  if (!_index) return;

  if (DBHelper::isCacheFileStale(db, indexFile(cachePath)))
    saveIndex(cachePath);
}

void CvFeaturesIndex::buildIndex(const cv::Mat& addedDescriptors) {
  qint64 ms = QDateTime::currentMSecsSinceEpoch();

  //
  // The bucket size roughly determines the query
  // performance since that part is linear time whereas the hashing
  // part is constant time.
  //
  // The key size (K) determines the bucket sizes since the descriptors
  // are sorted into 2^K buckets, the bucket size on average is around N / 2^K
  //
  // If the bucket size is too small, many hashes will miss and it takes
  // a long time to build the index. If it is too big, the index build is fast,
  // but query performance suffers.
  //
  // This first attempt is to associate the bytes or memory/cache needed
  // for each bucket and find a key size based on that.
  //
  int tables = 1;
  const int descSize = 32;  // OpenCV ORB default descriptor size (bytes)

  // verify descriptor size
  if (_descriptors.cols > 0)
    Q_ASSERT(descSize ==
             int(_descriptors.elemSize() * uint(_descriptors.cols)));

  int bytesPerBucket = 4096;
  int descPerBucket = bytesPerBucket / descSize;
  int keySize = int(log2(_descriptors.rows / descPerBucket));
  int numBuckets = 2 << keySize;
  descPerBucket = _descriptors.rows / numBuckets;
  int kbPerBucket = descPerBucket * descSize / 1024;

  int indexKb = tables * numBuckets * descPerBucket * descSize / 1024;

  qInfo(
      "descSize=%d keySize=%d buckets=%d descriptors/bucket=%d kb/bucket=%d "
      "indexKb=%d",
      descSize, keySize, numBuckets, descPerBucket, kbPerBucket, indexKb);

  cv::flann::LshIndexParams indexParams(tables, keySize, 1);

  // update with added descriptors, faster than full rebuild
  if (_index && addedDescriptors.rows > 0) {
    _index->build(_descriptors, addedDescriptors, indexParams);
  } else {
    delete _index;
    //_index = new cv::flann::Index(_descriptors, indexParams);
    _index = new cv::flann::Index;

    // build the index incrementally, it is a quite bit faster
    // somehow, and does not seem to affect accuracy
    for (int i = 0; i < _descriptors.rows; i += 10000) {
      int upper = std::min(i + 10000, _descriptors.rows);
      _index->build(_descriptors.rowRange(0, upper),
                    _descriptors.rowRange(i, upper), indexParams);
    }
  }

  ms = QDateTime::currentMSecsSinceEpoch() - ms;

  qInfo("%d descriptors, %d added, %dms %.2fus/desc", _descriptors.rows,
        addedDescriptors.rows, int(ms), ms * 1000.0 / _descriptors.rows);
}

void CvFeaturesIndex::loadIndex(const QString& path) {
  uint64_t then = nanoTime();
  loadMap(_indexMap, path + "/cvfeatures_indexmap.map");
  loadMap(_idMap, path + "/cvfeatures_idmap.map");
  loadMatrix(path + "/cvfeatures.mat", _descriptors);

  uint64_t now = nanoTime();
  uint64_t nsLoad = now - then;
  then = now;

  buildIndex(cv::Mat());

  now = nanoTime();
  uint64_t nsBuild = now - then;

  qInfo("load=%.1fms build=%.2fms", nsLoad / 1000000.0, nsBuild / 1000000.0);
}

void CvFeaturesIndex::saveIndex(const QString& path) {
  saveMatrix(_descriptors, path + "/cvfeatures.mat");
  saveMap(_idMap, path + "/cvfeatures_idmap.map");
  saveMap(_indexMap, path + "/cvfeatures_indexmap.map");
}

cv::Mat CvFeaturesIndex::descriptorsForMediaId(uint32_t mediaId) const {
  cv::Mat descriptors;

  auto it = _idMap.find(mediaId);
  if (it != _idMap.end()) {
    int firstRow = int(it->second);
    it++;
    int lastRow;
    Q_ASSERT(it != _idMap.end());  // not possible since trailer is added
    // if (it != _idMap.end())
    lastRow = int(it->second);
    // else
    //    lastRow = _descriptors.rows;

    Q_ASSERT(it->first > mediaId);
    Q_ASSERT(firstRow < lastRow);
    Q_ASSERT(lastRow - firstRow < 1000);  // s.b. < max descriptors per item
    Q_ASSERT(lastRow <= _descriptors.rows);
    Q_ASSERT(firstRow < _descriptors.rows);

    descriptors = _descriptors.rowRange(firstRow, lastRow);
  }

  return descriptors;
}

QVector<Index::Match> CvFeaturesIndex::find(const Media& needle,
                                            const SearchParams& params) {
  uint64_t then = nanoTime();
  const uint64_t start = then;

  cv::Mat descriptors = needle.keyPointDescriptors();

  if (descriptors.rows <= 0)
    descriptors = descriptorsForMediaId(uint32_t(needle.id()));

  if (descriptors.rows <= 0) {
    qWarning("needle has no descriptors");
    return QVector<Index::Match>();
  }

  if (_descriptors.rows <= 0) {
    qWarning("empty index");
    return QVector<Index::Match>();
  }

  // if we copied the features from db, we will have
  // a lot more than we need, reduce them while trying
  // to distribute evenly
  /*
  if (descriptors.rows > params.needleFeatures)
  {
      int skip = descriptors.rows / params.needleFeatures;
      cv::Mat trunc(0, descriptors.cols, descriptors.type());

      for (int i = 0; i < descriptors.rows; i+=skip)
          trunc.push_back(descriptors.row(i));

      descriptors = trunc;

      //qWarning("descriptors reduced to %d", descriptors.rows);
  }
   */

  uint64_t now = nanoTime();
  uint64_t nsLoad = now - then;
  then = now;

  // todo: scoring system needs validation
  // note various methods attempted below
  struct Match_ {
    int count = 0;
    //        int totalScore = 0;
    //        int minScore = INT_MAX;
    //        int maxScore = INT_MIN;
    std::vector<int> scores;
  };

  QMap<uint32_t, Match_> matches;
  int maxMatches = 0;

  // for every descriptor in the needle, find the 10 nearest in the index
  // todo: how many do we actually have to find (should it be a parameter?)
  cv::Mat flannIndices;
  cv::Mat flannDists;
  _index->knnSearch(descriptors, flannIndices, flannDists, 10);

  for (int i = 0; i < flannIndices.rows; i++)
    for (int j = 0; j < flannIndices.cols; j++) {
      int index = flannIndices.at<int>(i, j);

      if (index < 0) {
        // qWarning("zero index\n");
        continue;
      }

      int distance = flannDists.at<int>(i, j);

      // ignore bad matches
      if (distance >= params.cvThresh) continue;

      uint32_t mediaId = 0;
      auto it = _indexMap.upper_bound(uint32_t(index));
      it--;
      mediaId = it->second;

      // we found a deleted/removed item (mediaId == 0)
      if (!mediaId) {
        qWarning("no mediaId for index %d", index);
        continue;
      }

      auto& match = matches[mediaId];

      match.count++;
      match.scores.push_back(distance);

      //        match.minScore = std::min(distance, match.minScore);
      //        match.maxScore = std::max(distance, match.maxScore);
      //        match.totalScore += distance;

      maxMatches = std::max(match.count, maxMatches);
    }

  now = nanoTime();
  uint64_t nsFwd = now - then;
  then = now;

  /*
      // do a reverse match to get better results
      cv::BFMatcher reverse(cv::NORM_HAMMING, false);
      std::vector<cv::Mat> haystack;
      haystack.push_back(descriptors);
      reverse.add(haystack);

      for (uint32_t mediaId : matches.keys())
      {
          auto& match = matches[mediaId];

          cv::Mat desc = descriptorsForMediaId(mediaId);

          std::vector<std::vector<cv::DMatch> > dmatch;
          reverse.radiusMatch(desc, dmatch, params.cvThresh);

          for (size_t i = 0; i < dmatch.size(); i++)
          for (size_t j = 0; j < dmatch[i].size(); j++)
          {
              float distance = dmatch[i][j].distance;

              match.count++;
              match.score = std::min((int)distance, match.score);
          }

          maxMatches = std::max(match.count, maxMatches);
      }
  */
  now = nanoTime();
  uint64_t nsRev = now - then;

  QVector<Index::Match> results;

  // score the matches (lower score is better)
  for (uint32_t mediaId : matches.keys()) {
    auto& match = matches[mediaId];

    if (match.count > 0) {

      // average score
      // int score = match.totalScore / match.count;

      // median of scores, maybe best
      std::sort(match.scores.begin(), match.scores.end());

      int score = 0;
      uint middle = uint(match.scores.size() / 2);
      if (match.scores.size() < 2)
        score = match.scores[0];
      else if (match.scores.size() % 2 == 0) {
        score = (match.scores[middle - 1] + match.scores[middle]) / 2;
      } else {
        score = match.scores[middle];
      }

      // spread out scores (x1000) and boost if there are more matches
      score = score * 1000 / int(match.scores.size());

      results.append(Index::Match(mediaId, score));
    }
  }

  now = nanoTime();
  if (params.verbose)
    qInfo("found=%d load=%.1fms fwd=%.2fms rev=%.2fms total=%.1fms",
          results.count(), nsLoad / 1000000.0, nsFwd / 1000000.0,
          nsRev / 1000000.0, (now - start) / 1000000.0);

  return results;
}
