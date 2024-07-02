/* Component integration
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

class Database;
class IndexParams;
class Scanner;
class TemplateMatcher;

/// Container for database query and results
struct MediaSearch {
  Media needle;
  SearchParams params;
  MediaGroup matches;
};

/// Integration of indexing, search queries, and filters
class Engine : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(Engine)

 public:
  /**
   * Manage a database at the given path
   * @param path directory containing (or that will contain) a database
   * @param params options for controlling indexing
   */
  Engine(const QString& path, const IndexParams& params);

  /**
   * Close/free the database
   * @note waits for update() to complete
   */
  ~Engine();

  /**
   * Search for newly added or removed files
   * @param wait block until scan is complete
   *
   * If wait=false, destruct of Engine will discard pending items
   *
   * Emits fileAdded when a new file was added or modified
   */
  void update(bool wait = false);

  /**
   * Stop updating
   * @param wait block until pending work has stopped
   */
  void stopUpdate(bool wait = false);

  /**
   * Query the database
   * @param search Query parameters and target/needle
   * @return Copy of query parameters with results added
   */
  MediaSearch query(const MediaSearch& search) const;

 public Q_SLOTS:
  /**
   * Add new file to the database (probably from Scanner)
   * @note Assumes file was processed appropriately (Scanner::process*)
   * @note Files are added in batches, call commit() to flush
   */
  void add(const Media& m);

  /**
   * Write pending changes to database
   * @note changes are batched to hide write latency of database
   */
  void commit();

 public:
  Database* db;
  Scanner* scanner;
  TemplateMatcher* matcher;

 private:
  /**
   * Get mirrored image for searching
   * @note indices do not recognized mirrored images (typically) and
   *       the query image must be mirrored to find them
   */
  Media mirrored(const Media& m, bool mirrorH, bool mirrorV) const;

  MediaGroup _batch;
};
