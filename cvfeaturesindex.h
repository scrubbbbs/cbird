/* Index for rotated images
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

#include "opencv2/core.hpp"

namespace cv::flann {
class Index;
}
/**
 * @class CvFeaturesIndex
 * @brief Index for OpenCV feature descriptors
 *
 * Detects scaled, rotated, cropped images using ORB features
 */
class CvFeaturesIndex : public Index {
  Q_DISABLE_COPY_MOVE(CvFeaturesIndex)

 public:
  CvFeaturesIndex();
  ~CvFeaturesIndex() override;

  void createTables(QSqlDatabase& db) const override;
  void addRecords(QSqlDatabase& db, const MediaGroup& media) const override;
  void removeRecords(QSqlDatabase& db, const QVector<int>& mediaIds) const override;

  bool isLoaded() const override;
  int count() const override;
  size_t memoryUsage() const override;

  void load(QSqlDatabase& db, const QString& cachePath, const QString& dataPath) override;
  void save(QSqlDatabase& db, const QString& cachePath) override;

  void add(const MediaGroup& media) override;
  void remove(const QVector<int>& id) override;

  QVector<Index::Match> find(const Media& m, const SearchParams& p) override;

  Index* slice(const QSet<uint32_t>& mediaIds) const override;

 private:
  void buildIndex(const cv::Mat& addedDescriptors);
  void loadIndex(const QString& path);
  void saveIndex(const QString& path);

  cv::Mat descriptorsForMediaId(uint32_t mediaId) const;

  cv::Mat _descriptors;      // all descriptors merged into one fat cv::Mat
  cv::flann::Index* _index;  // index of the cv::Mat

  // map of first descriptor index to media Id, in ascending order,
  // INTMAX,0 as last item
  std::map<uint32_t, uint32_t> _indexMap;

  // map of media id to first _descriptors[] index, in ascending order,
  // INTMAX,_descriptors.rows as last item
  std::map<uint32_t, uint32_t> _idMap;
};
