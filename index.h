/* Search index common interface
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
#pragma once

#include "media.h"

/// dir name for index database
#define INDEX_DIRNAME "_index"

/// report sql error with context & detail
#define SQL_FATAL(x) \
  qFatal("QSqlQuery." #x ": %s", qPrintable(query.lastError().text()));

/**
 * @class SearchParams
 * @brief Parameters passed to search functions
 *
 * Common structure for all types of searches
 *
 * @note only Engine::query incorporates all parameters, other functions
 * use the applicable parameters.
 */
class SearchParams {
 public:
  /**
   * list of available media search algorithms/indexing methods
   * @details Each method is good for some types of modifications (noted).
   */
  enum {
    AlgoDCT = 0,          /// DCT-based image hash (scale, small-crop)
    AlgoDCTFeatures = 1,  /// DCT hash around features (scale,big-crop)
    AlgoCVFeatures = 2,   /// OpenCV features (scale,big-crop,rotation)
    AlgoColor = 3,        /// Color Histogram match (any transform)
    AlgoVideo = 4         /// DCT hashes of video frames (scale,small-crops)
  };

  /**
   * mirror modes/orientations
   * @note currently no index recognizes mirrored images,
   * a mirrored query image will be created
   */
  enum {
    MirrorNone = 0,
    MirrorHorizontal = 1,
    MirrorVertical = 2,
    MirrorBoth = 4,
  };

  int algo,        // AlgoXXX
      dctThresh,   // threshold for DCT hash hamming distance
      cvThresh,    // threshold for ORB descriptors distance
      minMatches,  // minimum number of matches required
      maxMatches,  // maximum number of matches to return (after sort by lowest
                   // score)
      needleFeatures,    // number of features to generate for needle image
                         // (template matcher)
      haystackFeatures,  // number of features to generate for haystack image
                         // (template matcher)
      mirrorMask;        // MirrorXXX flags

  bool templateMatch,  // remove results that don't pass the template matcher
      negativeMatch,   // remove results in the negative matches (blacklist)
      autoCrop,        // de-letterbox prior to search
      verbose;         // show more information about what the query is doing

  QString path;  // subdirectory to search or accept/reject results from
  bool inPath;   // true==accept results from, false=reject results from

  MediaGroup set;  // subset to search within (using Index::slice())
  bool inSet;      // true==use subset

  uint32_t target;  // specify a media id to search in/for (Media::id())

  QVector<int> resultTypes;  // list of Media::Type to include in result set
  QVector<int> queryTypes;   // list of Media::Type to include in query set

  int skipFramesIn;     // video search: ignore N frames at start of video (intros)
  int skipFramesOut;    // video search: ignore N frames at end of video (outros)
  int minFramesMatched; // video search: >N frames match between videos
  int minFramesNear;    // video search: >N% of frames that matched are near each other

  bool filterSelf;    // remove media that matched itself
  bool filterGroups;  // remove duplicate groups from results (a matches (b,c,d)
                      // and b matches (a,c,d) omit second one)
  int mergeGroups;    // merge n-connected groups (value = # of connections) (a
                      // matches b and b matches c, then a matches (b,c)
  bool filterParent;  // remove matches with the same parent directory as needle
  bool expandGroups;  // expand group a,b,c,d by making (a,b), (a,c) and (a,d)
  int progressInterval;  // number of items searched between progress updates

  SearchParams() {
    dctThresh = 5;
    cvThresh = 25;
    algo = AlgoDCT;
    minMatches = 1;
    maxMatches = 5;
    needleFeatures = 100;
    haystackFeatures = 1000;
    templateMatch = false;
    verbose = false;
    negativeMatch = false;
    autoCrop = false;
    mirrorMask = MirrorNone;
    resultTypes << Media::TypeImage << Media::TypeVideo;
    queryTypes << Media::TypeImage;
    target = 0;
    skipFramesIn = 300;
    skipFramesOut = 300;
    minFramesMatched = 30;
    minFramesNear = 60;
    filterSelf = true;
    filterGroups = true;
    expandGroups = false;
    mergeGroups = 0;
    inPath = true;
    filterParent = false;
    inSet = false;
    progressInterval = 1000;
  }

  /**
   * @return true if the needle is is indexed to allow a search
   * with the given parameters to occur
   */
  bool mediaReady(const Media& needle) const {
    bool ok = false;
    switch (algo) {
      case AlgoCVFeatures:
        ok = needle.id() != 0 || needle.keyPointDescriptors().rows > 0;
        break;
      case AlgoDCTFeatures:
        ok = needle.id() != 0 || needle.keyPointHashes().size() > 0;
        break;
      case AlgoColor:
        ok = needle.id() != 0 || needle.colorDescriptor().numColors > 0;
        break;
      case AlgoVideo:
        ok = needle.id() != 0 ||
             (needle.type() == Media::TypeVideo && needle.videoIndex().hashes.size()>0) ||
             (needle.type() == Media::TypeImage && needle.dctHash() != 0);
        break;
      default:
        ok = needle.dctHash() != 0;
    }
    return ok;
  }

  // int cvMatchMatches; // cv features: default:10 max number of near features
  // to consider with knn search on descriptors

  // float haystackScaleFactor; // template matching: default:2.0 maximum size
  // of candidate image relative to target image,
  //                               rescale candidate prior to feature detection

  // int minMatchLength;      // video-to-video: minimum number of frames found
  // between two videos to consider it a match

  // int minPercentContigous: // video-to-video: of frames matched, minimum
  // percentage of frames that are contiguous to consider it a match

  // int contiguousThresh:    // video-to-video: frame numbers < this are
  // considered neighboring frames
};

/// Generic interface for a searchable index
class Index {
  Q_DISABLE_COPY_MOVE(Index)

 public:
  virtual ~Index() {}

  struct Match {
    uint32_t mediaId;
    int score;
    MatchRange range;
    Match() {
      mediaId = 0;
      score = 0;
    }
    Match(uint32_t mediaId_, int score_) : mediaId(mediaId_), score(score_) {}
  };

  /// @return unique id (AlgoXXX enum)
  int id() const { return _id; }

  /// @return true if the index has been loaded (ready for searching)
  virtual bool isLoaded() const = 0;

  /// @return amount of heap memory used
  virtual size_t memoryUsage() const = 0;

  /**
   * @return number of items represented
   * @note could be less than number of items in database
   *       - index may only apply to a certain media type (image, video, audio)
   *       - index may not apply to some items (e.g. grayscale images don't have color histogram)
   *       - item might be corrupt or failed to be processed
   */
  virtual int count() const = 0;

  /**
   * @return the id of the database file / connection
   * @note Each index subclass needs a unique id (by convention it is AlgoXXX)
   * @note If != 0 then createTables/add/remove must be implemented
   * @note Indexes could in theory use the same databaseId() to share data
   */
  virtual int databaseId() const { return id(); }

  /// Create database schema, if it doesn't exist
  virtual void createTables(QSqlDatabase& db) const { (void)db; }

  /// Add items to the database
  virtual void addRecords(QSqlDatabase& db, const MediaGroup& media) const {
    (void)db;
    (void)media;
  }

  /// Remove items from the database
  virtual void removeRecords(QSqlDatabase& db,
                             const QVector<int>& mediaIds) const {
    (void)db;
    (void)mediaIds;
  }

  /**
   * Load index from disk or sql server
   * @note For large databases, storage to flat file(s) is a good idea. The SQL
   * database is used to reconstruct the flat files and this is often very slow.
   */
  virtual void load(QSqlDatabase& db, const QString& cachePath,
                    const QString& dataPath) = 0;

  /**
   * Save in-memory index to cache file for faster loading
   * @details Tells the index it should save its in-memory representation
   * as it will be destroyed soon
   */
  virtual void save(QSqlDatabase& db, const QString& cachePath) = 0;

  /**
   * Add processed media to the in-memory representation without touching the database
   * @note this allows large indexes to be updated without reloading
   */
  virtual void add(const MediaGroup& media) = 0;

  /**
   * Remove media ids from the in-memory index without touching the database
   * @note the simplest way is to set the id's to 0
   */
  virtual void remove(const QVector<int>& id) = 0;

  /**
   * Find something in the index
   * @param m The query media, pre-processed for searching
   * @param p Parameters such as thresholds, ranges, limits etc
   */
  virtual QVector<Index::Match> find(const Media& m, const SearchParams& p) = 0;

  /**
   * Get data such as descriptors that are only stored in the index
   * @param m if m.id() exists in the index then it is populated.
   * @return true if anything was found
   */
  virtual bool findIndexData(Media& m) const {
    Q_UNUSED(m);
    return false;
  }

  /**
   * Make a subset of the index as new index
   * @note useful for searching subsets in large indexes
   * @return Copy of index with only the given media ids
   */
  virtual Index* slice(const QSet<uint32_t>& mediaIds) const {
    (void)mediaIds;
    return nullptr;
  }

 protected:
  int _id;
  Index() { _id = -1; }
};

/// sort matches by score
inline bool operator<(const Index::Match& m1, const Index::Match& m2) {
  return m1.score < m2.score;
}
