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

class HammingTree;

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
  virtual bool isLoaded() const;
  virtual int count() const;
  virtual size_t memoryUsage() const;
  virtual void load(QSqlDatabase& db, const QString& cachePath,
                    const QString& dataPath);
  virtual void save(QSqlDatabase& db, const QString& cachePath);
  virtual void add(const MediaGroup& media);
  virtual void remove(const QVector<int>& ids);
  virtual QVector<Index::Match> find(const Media& m, const SearchParams& p);
  virtual Index* slice(const QSet<uint32_t>& mediaIds) const;

  // video index does not use sql, but we need media ids
  virtual int databaseId() const override { return 0; }

 private:
  QVector<Index::Match> findFrame(const Media& needle,
                                  const SearchParams& params);
  QVector<Index::Match> findVideo(const Media& needle,
                                  const SearchParams& params);
  void insertHashes(int mediaIndex, HammingTree* tree,
                     const SearchParams& params);
  void buildTree(const SearchParams& params);

  HammingTree* _tree;
  std::vector<uint32_t> _mediaId;
  QString _dataPath;
  std::map<uint32_t, HammingTree*> _cachedIndex;
  QMutex _mutex;
  bool _isLoaded;
};
