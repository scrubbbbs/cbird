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
#include "params.h"

/**
 * @class SearchParams
 * @brief Parameters passed to search functions
 *
 * Common structure for all types of searches
 *
 * @note only Engine::query incorporates all parameters, other functions
 * use the applicable parameters.
 */
class SearchParams : public Params {
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
    AlgoVideo = 4,        /// DCT hashes of video frames (scale,small-crops)
    NumAlgos = 5
  };

  /**
   * mirror modes/orientations
   * @note no index besides color histogram recognizes mirrored images,
   * a mirrored query/needle image will be created
   */
  enum {
    MirrorNone = 0,        /// Do not flip needle
    MirrorHorizontal = 1,  /// Flip needle horizontally
    MirrorVertical = 2,    /// Flip needle vertically
    MirrorBoth = 4,        /// Flip needle both h & v
  };

  /**
   * combination of flags to indicate supported/desired types
   */
  enum {
    FlagImage = 1 << (Media::TypeImage - 1),
    FlagVideo = 1 << (Media::TypeVideo - 1),
    FlagAudio = 1 << (Media::TypeAudio - 1)
  };

  int algo = AlgoDCT,        // AlgoXXX
      dctThresh = 5,         // threshold for DCT hash hamming distance
      cvThresh = 25,         // threshold for ORB descriptors distance
      minMatches = 1,        // minimum number of matches required
      maxMatches = 5,        // maximum number of matches after sort by score
      needleFeatures = 100,  // template match: number of needle/template features
      haystackFeatures = 1000,  // template match: number of haystack/candidate features
      mirrorMask = MirrorNone,  // MirrorXXX flags for mirror search
      maxThresh = 0,          // if > 0, increment dct/cv/Thresh < maxThresh until match is found
      tmThresh = 7,           // template match: threshold for match validation
      tmScalePct = 200;       // template match: max scale factor between template/cand

  bool templateMatch = false,  // remove results that don't pass the template matcher
      negativeMatch = false,   // remove results in the negative matches (blacklist)
      autoCrop = false,        // de-letterbox prior to search
      verbose = false;         // show more information about what the query is doing

  QString path;         // subdirectory to search or accept/reject results from
  bool inPath = false;  // true==accept results from, false=reject results from

  MediaGroup set;      // subset to search within, must include result types and query types
  bool inSet = false;  // true==use subset

  /// @deprecated overlaps with inSet, has limited use, should be removed
  Q_DECL_DEPRECATED uint32_t target = 0;  // specify a media id to search in/for (Media::id())

  int queryTypes = FlagImage;  // types to include in query set

  int skipFrames = 300;       // video search: ignore first and last N frames of video
  int minFramesMatched = 30;  // video search: require >N frames match between videos
  int minFramesNear = 60;     // video search: require >N% of frames that matched are nearby
  int videoRadix = 10;        // video search: radix of RadixSearch

  bool filterSelf = true;       // remove media that matched itself
  bool filterGroups = true;     // remove duplicate groups from results (a matches (b,c,d)
                                //   and b matches (a,c,d) omit second one)
  bool filterParent = false;    // remove matches with the same parent directory as needle
  bool expandGroups = false;    //   expand group a,b,c,d by making (a,b), (a,c) and (a,d)
  int mergeGroups = 0;          // merge n-connected groups (value = # of connections) (a
                                //   matches b and b matches c, then a matches (b,c)
  int progressInterval = 1000;  // number of items searched between progress updates

  /// @return true if the media is compatible with search parameters
  bool mediaSupported(const Media& needle) const;

  /// @return true if the needle is is indexed to allow a search with these parameters
  bool mediaReady(const Media& needle) const;

  /// @return valid query/needle tytpes of algo
  //int needleTypes() const;

  /// @return valid result types of algo
  int resultTypes() const;

  SearchParams();

  // int cvMatchMatches; // cv features: default:10 max number of near features
  // to consider with knn search on descriptors

  // float haystackScaleFactor; // template matching: default:2.0 maximum size
  // of candidate image relative to target image,
  //                               rescale candidate prior to feature detection
};

/// Common base for searchable index
class Index {
  Q_DISABLE_COPY_MOVE(Index)

 public:
  virtual ~Index() {}

  /// item type of index lookups
  struct Match {
    uint32_t mediaId; // unique id of indexed media
    int score;        // score of match, lower is better
    MatchRange range; // matching area/segment, e.g. frame numbers for partial video match
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
  virtual void removeRecords(QSqlDatabase& db, const QVector<int>& mediaIds) const {
    (void)db;
    (void)mediaIds;
  }

  /**
   * Load index from disk or sql server
   * @note For large databases, storage to flat file(s) is a good idea. The SQL
   * database is used to reconstruct the flat files and this is often very slow.
   */
  virtual void load(QSqlDatabase& db, const QString& cachePath, const QString& dataPath) = 0;

  /**
   * Save in-memory index to cache file for faster loading
   * @note since this is slow, it is usually going to be called after making a batch of
   * additions/removals. Or maybe only at application exit. In the removal case, the downside is
   * that there will usually be voids/nulls in the index that will remain until rebuilding the cache
   * file.
   */
  virtual void save(QSqlDatabase& db, const QString& cachePath) = 0;

  /**
   * Return all media ids represented in the index
   */
  virtual QSet<mediaid_t> mediaIds(QSqlDatabase& db,
                                   const QString& cachePath,
                                   const QString& dataPath) const;

  /**
   * Add processed media to the in-memory representation without touching the database
   * @note it is an error to call this if isLoaded() is false
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

  /**
   * Get media types returned
   * @return Media::Type flags
   */
  virtual int resultTypes() const { return Media::typeFlag(Media::TypeImage); }

 protected:
  int _id;
  Index() { _id = -1; }
};

/// sort matches by score
inline bool operator<(const Index::Match& m1, const Index::Match& m2) {
  return m1.score < m2.score;
}
