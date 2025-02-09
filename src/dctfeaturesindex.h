/* Index for cropped images
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

typedef HammingTree_t<uint32_t> HammingTree; // 32-bit index, 64-bit hash

/**
 * @class DctFeaturesIndex
 * @brief Index of a feature-based matcher using DCT hashes
 *
 * DCT hashes do not support rotation, unlike ORB. However
 * they are much smaller and good at detecting cropping
 */
class DctFeaturesIndex : public Index {
  Q_DISABLE_COPY_MOVE(DctFeaturesIndex)

 public:
  DctFeaturesIndex();
  ~DctFeaturesIndex() override;

  void createTables(QSqlDatabase& db) const override;
  void addRecords(QSqlDatabase& db, const MediaGroup& media) const override;
  void removeRecords(QSqlDatabase& db, const QVector<int>& mediaIds) const override;

  int count() const override;
  bool isLoaded() const override;
  size_t memoryUsage() const override;

  void load(QSqlDatabase& db, const QString& cachePath, const QString& dataPath) override;
  void save(QSqlDatabase& db, const QString& cachePath) override;

  QSet<mediaid_t> mediaIds(QSqlDatabase& db,
                           const QString& cachePath,
                           const QString& dataPath) const override;

  void add(const MediaGroup& media) override;
  void remove(const QVector<int>& id) override;

  QVector<Index::Match> find(const Media& m, const SearchParams& p) override;

  Index* slice(const QSet<uint32_t>& mediaIds) const override;

 private:
  void init();
  void unload();
  HammingTree* _tree;
};
