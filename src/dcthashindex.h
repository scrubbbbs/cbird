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
#pragma once

#include "index.h"

/**
 * @class DctHashIndex
 * @brief Index for 64-bit dct hash that uses hamming distance
 */
class DctHashIndex : public Index {
  Q_DISABLE_COPY_MOVE(DctHashIndex)

 public:
  DctHashIndex();
  ~DctHashIndex() override;

 public:
  bool isLoaded() const override { return _isLoaded; }
  int count() const override { return _numHashes; }
  size_t memoryUsage() const override;

  void add(const MediaGroup& media) override;
  void remove(const QVector<int>& ids) override;

  void load(QSqlDatabase& db, const QString& cacheFile, const QString& dataPath) override;
  void save(QSqlDatabase& db, const QString& cachePath) override;

  QSet<mediaid_t> mediaIds(QSqlDatabase& db,
                           const QString& cachePath,
                           const QString& dataPath) const override;

  QVector<Index::Match> find(const Media& m, const SearchParams& p) override;

  Index* slice(const QSet<uint32_t>& mediaIds) const override;

  uint64_t hashForMedia(const Media& m) { return m.dctHash(); }
  QString hashQuery() const { return "select id,phash_dct from media where type=1"; }

 private:
  void unload();
  class DctTree* _tree;
  uint64_t* _hashes;
  uint32_t* _mediaId;
  int _numHashes;
  bool _isLoaded;
  void init();
  void buildTree();
};
