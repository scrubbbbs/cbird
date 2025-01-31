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

// #define VINDEX_32BIT

#ifdef VINDEX_32BIT
// old 32-bit index, limited to 64k frames/video
#define VINDEX_FRAME_BITS 16
#define VINDEX_IDX_BITS 16
#else
// new 48-bit index, ~17% more RAM, 16.7 million frames/videos
#define VINDEX_FRAME_BITS 24
#define VINDEX_IDX_BITS 24
#endif

// must pack struct for 48-bit case,
// put the idx first since it is needed more often
#pragma pack(1)
typedef struct
{
  mediaid_t idx : VINDEX_IDX_BITS; // index into _mediaId[]
  int frame : VINDEX_FRAME_BITS;   // frame number, compatible with MatchRange
} VideoTreeIndex;
#pragma pack()

// check the packing actually worked
#define VINDEX_BITS (VINDEX_FRAME_BITS + VINDEX_IDX_BITS)
static_assert(sizeof(VideoTreeIndex) == VINDEX_BITS / 8);

#define MAX_FRAMES_PER_VIDEO (1 << VINDEX_FRAME_BITS)
#define MAX_VIDEOS_PER_INDEX (1 << VINDEX_IDX_BITS)

#ifdef USE_HAMMING
template<typename T>
class HammingTree_t;
typedef HammingTree_t<VideoTreeIndex> VideoSearchTree;
#else
template<typename T>
class RadixMap_t;
typedef RadixMap_t<VideoTreeIndex> VideoSearchTree;
#endif

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

  struct VStat
  {
    uint64_t videoFrames; // # frames  in the video file
    uint64_t usedFrames;  // # frames in the indexed file
  };
  VStat insertHashes(mediaid_t mediaIndex, VideoSearchTree* tree, const SearchParams& params);

  void buildTree(const SearchParams& params);

  VideoSearchTree* _tree;
  std::vector<mediaid_t> _mediaId;
  QString _dataPath;
  std::map<mediaid_t, VideoSearchTree*> _cachedIndex;
  QMutex _mutex;
  bool _isLoaded;
};
