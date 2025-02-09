/* Index for similar-color search
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
 * @class ColorDescIndex
 * @brief Index for ColorDescriptor
 *
 * Detects images with similar colors
 */
class ColorDescIndex : public Index {
  Q_DISABLE_COPY_MOVE(ColorDescIndex)

 public:
  ColorDescIndex();
  ~ColorDescIndex() override;

  void createTables(QSqlDatabase& db) const override;
  void addRecords(QSqlDatabase& db, const MediaGroup& media) const override;
  void removeRecords(QSqlDatabase& db, const QVector<int>& mediaIds) const override;

  bool isLoaded() const override;
  size_t memoryUsage() const override;
  int count() const override;

  void load(QSqlDatabase& db, const QString& cachePath, const QString& dataPath) override;
  void save(QSqlDatabase& db, const QString& cachePath) override;

  QSet<mediaid_t> mediaIds(QSqlDatabase& db,
                           const QString& cachePath,
                           const QString& dataPath) const override;

  void add(const MediaGroup& media) override;
  void remove(const QVector<int>& id) override;

  QVector<Index::Match> find(const Media& m, const SearchParams& p) override;
  bool findIndexData(Media& m) const override;

  Index* slice(const QSet<uint32_t>& mediaIds) const override;

 private:
  void unload();

  int _count; // FIXME: use size_t
  uint32_t* _mediaId;
  ColorDescriptor* _descriptors;
};
