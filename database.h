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

  /// @return last time anything was added, files modified
  ///         after this date are re-indexed
  QDateTime lastAdded();

  /**
   * Add processed media (typically from Scanner) to the index
   * @note all-or-nothing operation, using sql transactions
   * @note larger groups seem to be more efficient, usually
   */
  void add(const MediaGroup& media);

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

  /// Move file without re-indexing
  bool move(Media& m, const QString& newDir);

  /// Rename file without re-indexing
  bool rename(Media& m, const QString& newName);

  /// Rename all files under a dir without re-indexing
  bool renameDir(const QString& dirPath, const QString& newName);

  /// Fast test if index contains file
  bool mediaExists(const QString& path);

  // Basic database lookups
  Media mediaWithId(int id);
  Media mediaWithPath(const QString& path);
  MediaGroup mediaWithPathLike(const QString& path);
  MediaGroup mediaWithPathRegexp(const QString& exp);
  MediaGroup mediaWithMd5(const QString& md5);
  MediaGroup mediaWithType(int type);
  MediaGroup mediaWithIds(const QVector<int>& ids);
  MediaGroup mediaWithSql(const QString& sql, const QString& placeholder="",
                          const QVariant& value=QVariant());

  /// @return all files in the index
  QSet<QString> indexedFiles();

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

 private:
  /**
   * @return database connection for the current thread
   * @param id which database to use
   * @note id corresponds to Index.id()
   * @note sqlite is not thread-safe, each database/file/thread gets its own
   * instance
   * @note the connections remain open as long as the thread is alive
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
   * @param subset If not empty, restrict results to this subset
   */
  MediaGroup searchIndex(Index* index, const Media& needle,
                         const SearchParams& params,
                         const QHash<int, Media>& subset);

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

  /// @return atomic int for unique connection names in the pool
  static QAtomicInt& connectionCount();

  /// @return mutex for database connection pool (_dbConnections)
  static QMutex& dbMutex();

  /// @return the database per-thread connection pool
  static QHash<int, QHash<QThread*, QString>>& dbConnections();

  /// @return the new path after moving file
  QString moveFile(const QString& srcPath, const QString& dstDir);

  /// write timestamp for modification detection
  void writeTimestamp();

  /// Directory containing the indexed files and database file
  QString _indexDir;

  /// Lock for single-writer, multiple-reader situations
  QReadWriteLock _rwLock;

  /// Registered algorithms
  QVector<Index*> _algos;

  /// Sql column index for "media" table
  struct {
    int id, type, path, width, height, md5, phash_dct;
  } _mediaIndex;

  /// Negative matches list (md5 hash)
  QMap<QString, QStringList> _negMatch;

  /// Negative matches list status
  bool _negMatchLoaded = false;
};
