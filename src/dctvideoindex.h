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
#pragma once
#include "index.h"

template<typename T>
class HammingTree_t;

typedef HammingTree_t<uint64_t> HammingTree64; // 64-bit index, 64-bit hash

/**
 * @class DctVideoIndex
 * @brief Detect similar videos with full-frame dct hashes
 */
class DctVideoIndex : public Index {
  Q_DISABLE_COPY_MOVE(DctVideoIndex)

 public:
  DctVideoIndex();
  virtual ~DctVideoIndex();

 public:
  bool isLoaded() const override;
  int count() const override;
  size_t memoryUsage() const override;

  void load(QSqlDatabase& db, const QString& cachePath, const QString& dataPath) override;
  void save(QSqlDatabase& db, const QString& cachePath) override;

  void add(const MediaGroup& media) override;
  void remove(const QVector<int>& ids) override;

  QVector<Index::Match> find(const Media& m, const SearchParams& p) override;
  Index* slice(const QSet<uint32_t>& mediaIds) const override;

  // video index does not use sql, but we need media ids
  int databaseId() const override { return 0; }

 private:
  QVector<Index::Match> findFrame(const Media& needle, const SearchParams& params);
  QVector<Index::Match> findVideo(const Media& needle, const SearchParams& params);
  void insertHashes(int mediaIndex, HammingTree64* tree, const SearchParams& params);
  void buildTree(const SearchParams& params);

  HammingTree64* _tree;
  std::vector<uint32_t> _mediaId;
  QString _dataPath;
  std::map<uint32_t, HammingTree64*> _cachedIndex;
  QMutex _mutex;
  bool _isLoaded;

  // the 32-bit index is split between mediaId and frame number,
  // which limits the number of items we can search
  const uint32_t MAX_FRAMES = 0xFFFF;
  const uint32_t MAX_VIDEOS = 0xFFFF;
};
