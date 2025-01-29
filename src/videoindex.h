/* Video file hash storage 
   Copyright (C) 2021-2025 scrubbbbs
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

/**
 * @class VideoIndex
 * @brief Container for index of a single video file
 *
 * Index is compressed by omitting nearby frames,
 * therefore there is also list of frame numbers
 * 
 * Stored in a .vdx file, loaded/unloaded when building search tree
 *
 * @note VideoTreeIndex::frame sets the upper limit on frames per video
 *       VideoTreeIndex::idx sets the upper limit on videos per index
 **/
class VideoIndex
{
  friend class TestVideoIndex;

 public:
  std::vector<int> frames; // compatible with MatchRange
  std::vector<dcthash_t> hashes;

  size_t memSize() const { return sizeof(*this) + VECTOR_SIZE(frames) + VECTOR_SIZE(hashes); }
  bool isEmpty() const { return frames.size() == 0 || hashes.size() == 0; }
  void save(const QString& file) const;
  void load(const QString& file);
  static bool isValid(const QString& file);

 private:
  static int getVersion(SimpleIO& io);

  static bool checkHeader_v2(const QList<QByteArray>& header);
  static bool verify_v2(SimpleIO& io);
  bool save_v2(SimpleIO& io) const;
  bool load_v2(SimpleIO& io);

  static bool verify_v1(SimpleIO& io);
  bool save_v1(SimpleIO& io) const;
  bool load_v1(SimpleIO& io);
};
