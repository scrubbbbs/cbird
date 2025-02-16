/* Database management and search
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

#include "index.h"
#include "media.h"

class QSqlQuery;
class QRecursiveMutex;
class QReadWriteLock;

/// Manage and query media in a directory
class Database {
 public:
  /**
   * @param path top-level directory to manage, if
   *        empty use CWD
   */
  Database(const QString& path = "");
  ~Database();

  /// @return top-level directory being managed
  QString path() const { return _indexDir; }

  /// @return path for index data
  QString indexPath() const { return path() + "/" INDEX_DIRNAME; }

  /// @return path for a database file
  QString dbPath(int id = 0) const {
    return QString("%1/media%2.db").arg(indexPath()).arg(id);
  }

  /// @return directory that can be deleted without affecting the index
  QString cachePath() const { return indexPath() + "/cache"; }

  /// @return directory for video index files
  QString videoPath() const { return indexPath() + "/video"; }

  /// @return path to index icon/thumbnail
  QString thumbPath() const { return path() + "/thumb.png"; };

  /// @return last time anything was added, files modified
  ///         after this date are re-indexed
  QDateTime lastAdded();

  /// write timestamp for modification detection
  void writeTimestamp();

  /**
   * Add processed media (typically from Scanner) to the index
   * @note all-or-nothing operation, using sql transactions
   * @note larger groups seem to be more efficient, usually
   */
  void add(MediaGroup& media);

  /**
   * Remove media from the index, physical media is not deleted
   * @note it is most efficient to remove multiple at a time, as a transaction
   */
  void remove(const QVector<int>& ids);
  void remove(const MediaGroup& media);
  void remove(int id);

  /**
   * Overwrite the stored md5 hash
   * @note to avoid a re-index due to hashing scheme changes
   */
  bool setMd5(Media& m, const QString& md5);

  /// Move file to existing dir, preserving index
  bool move(Media& m, const QString& dstDir);

  /// Rename file, preserving index
  bool rename(Media& m, const QString& newName);

  /// move/rename dir or zip, preserving index
  bool moveDir(const QString& dirPath, const QString& newName);

  /// Fast test if index contains file
  bool mediaExists(const QString& path);

  /// Fast test if index contains path (sql like)
  bool mediaExistsLike(const QString& pathLike);

  // Basic database lookups
  Media mediaWithId(int id);
  Media mediaWithPath(const QString& path);
  MediaGroup mediaWithPathLike(const QString& path);
  MediaGroup mediaWithPathRegexp(const QString& exp);
  MediaGroup mediaWithMd5(const QString& md5);
  MediaGroup mediaWithType(int type);
  [[deprecated]] MediaGroup mediaWithIds(const QVector<int>& ids); // doesn't seem to be used anywhere
  MediaGroup mediaWithSql(const QString& sql, const QString& placeholder="",
                          const QVariant& value=QVariant());

  /// @return all files in the index
  QSet<QString> indexedFiles();

  /// @return all files in the index with its id and indexed algorithms
  struct Item {
    mediaid_t id = 0;
    int type = 0;
    int algos = 0;
  };
  QHash<QString, Item> indexedItems();

  /**
   * get indexed media
   * @param algos algos flags / indexes to check
   * @param missing if true, return if *not* indexed for all algos
   * @return media indexed for all algos
   */
  QHash<QString, mediaid_t> indexedForAlgos(int algos, bool missing);

  /// @return duplicate Media via md5 hash
  MediaGroupList dupsByMd5(const SearchParams& params);

  /// @return similar Media via Index
  MediaGroupList similar(const SearchParams& params);

  /**
   * @return similar Media to a single Media/needle
   * @note if needle does not exist in the database, it must be
   *       be pre-processed first (Scanner::process*)
   */
  MediaGroup similarTo(const Media& needle, const SearchParams& params);

  /**
   * Add pair of Media to match blacklist
   * @note negative matches are removed from result if enabled in SearchParams
   */
  void addNegativeMatch(const Media& m1, const Media& m2);
  bool isNegativeMatch(const Media& m1, const Media& m2);
  void loadNegativeMatches();
  void unloadNegativeMatches();
  MediaGroup filterNegativeMatches(const MediaGroup& in);

  /**
   * @brief Track reappearing deleted files "weeds"
   * @param weed deleted file
   * @param original related non-weed
   * @note A file is a weed when the associated non-weed still exists,
   *       then it can be deleted automatically
   */
  bool addWeed(const Media& weed, const Media& original);
  bool isWeed(const Media& media);
  /// remove deletion records for files(needles) which no longer exist
  //void updateWeeds();
  bool removeWeed(const Media& weed);
  void loadWeeds();
  void unloadWeeds();
  MediaGroupList weeds();

  /**
   * Add content index methods
   * @param index interface for storage and search for a class of media
   * @note in theory, search methods can be plugins, and the plugin
   *       loader would call this method
   */
  void addIndex(Index* index);

  /// @return proper index for given parameters, but do not load/initialize it
  Index* chooseIndex(const SearchParams& params) const;

  /**
   * @return loaded Index from disk or refresh from the in-memory representation
   * @note each index is not loaded until first used
   */
  Index* loadIndex(const SearchParams& params);

  /// Called after addIndex to do setup tasks like sql schema
  void setup();

  /**
   * @return count of indexed objects with a given type
   * @param type Media::Type
   */
  int countType(int type);

  /// @return count of indexed objects regardless of type
  int count();

  // @return rough estimate of current memory usage (heap)
  size_t memoryUsage() const;

  /**
   * Filter a match with search params
   * @note the group must have the needle prepended
   * @return true if entire group should be discarded
   */
  bool filterMatch(const SearchParams& params, MediaGroup& match);

  /// Filter a set of matches
  void filterMatches(const SearchParams& params, MediaGroupList& matches);

  /// Defragment sql databases, optimize indexes
  void vacuum();

  /// unit testing only: close all database connections (all threads),
  /// when called there must not be any live instances of Database
  static void disconnectAll();

 private:
  /**
   * Thread-safe database connection
   * @return per-thread instance
   * @param id which database to use
   * @note id corresponds to Index.id()
   * @note QSqlDatabase is not thread-safe, each database/file/thread gets its own
   * instance
   * @note the connections remain open as long as the thread is alive
   * @note database writes also use lock file for extra safety
   */
  QSqlDatabase connect(int id = 0);

  /// Close all database connections associated with the calling thread
  static void disconnect();

  /// @return Media matching needle
  /**
   * @return Media matching needle
   * @param index  Index to search
   * @param needle Needle, processed for searching
   * @param params Search parameters
   * @param idMap If not empty, used to lookup media info
   */
  MediaGroup searchIndex(Index* index, const Media& needle,
                         const SearchParams& params,
                         const QHash<int, Media>& idMap);

  /// Create database (sql) tables for index id 0, the others use Index interface
  void createTables();

  /// Initialize media group with results from "select * from media ..."
  void fillMediaGroup(QSqlQuery& query, MediaGroup& media, int maxLen = 0);

  /**
   * Ask indices to write in-memory representation to disk so they may
   * bypass slow initialization
   * @details sql db is convienient but slow for building in-memory
   * representation for searching. Therefore the Index may implement a flat file
   * cache backed by the sql db. This function may be called to flush any
   * changes to this cache so it does not need to be rebuilt from the sql db
   * later.
   */
  void saveIndices();

  /// read/write a key-value map
  void readMap(const QString& name, std::function<void(const QString&, const QString&)> insert) const;
  bool appendMap(const QString& name, const QString& key, const QString& value) const;
  bool writeMap(const QString& name, const QVector<std::pair<QString, QString> >& keyValues) const;

  /// @return atomic int for unique connection names in the pool
  static QAtomicInt& connectionCount();

  /// @return mutex for database connection pool (_dbConnections)
  static QRecursiveMutex& dbMutex();

  /// @return the database per-thread connection pool
  static QHash<int, QHash<QThread*, QString>>& dbConnections();

  /// @return the new path after moving file
  QString moveFile(const QString& srcPath, const QString& dstDir);

  /// @return the new path after renaming file
  QString renameFile(const QString& path, const QString& newName);

  /// modify paths in database and update the group
  bool updatePaths(const MediaGroup& group, const QStringList& newPaths);

  /// Directory containing the indexed files and database file
  QString _indexDir;

  /// Lock for single-writer, multiple-reader situations
  QReadWriteLock* _rwLock;

  /// Registered algorithms
  QVector<Index*> _algos;

  /// Sql column index for "media" table
  struct {
    int id, type, path, width, height, md5, phash_dct;
  } _mediaIndex;

  /// Negative matches list (md5 hash)
  QHash<QString, QSet<QString>> _negMatch;

  /// Negative matches list status
  bool _negMatchLoaded = false;

  QHash<QString, QString> _weeds; /// deleted hash => retained hash
  bool _weedsLoaded = false;

  bool _firstTime = false; /// true if running for the first time
};
