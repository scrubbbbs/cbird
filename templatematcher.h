/* Feature-based template matching
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

#include "media.h"
class SearchParams;

class TemplateMatcher {
  Q_DISABLE_COPY_MOVE(TemplateMatcher)

 public:
  TemplateMatcher();
  virtual ~TemplateMatcher();

  /**
   * High-resolution template matching
   * @param tmplMedia template image
   * @param group candidate images, will be modified with match information (e.g. transform())
   * @param params
   *
   * For validating results from fuzzy matchers, very slow but reliable.
   *
   * Uses a large number of features to estimate a rigid transform,
   * then validates the transform using dct hash of the estimated region
   *
   * Supports affine transformations (scale,translate,rotate). Does not support
   * occlusion/masking, or non-affine transformations (mirror, warp,
   * perspective, etc)
   *
   * On exit, candidate images are removed that do not match. Matches have their
   * roi() and transform() set
   *
   * @note results are cached, so long as instance is not destructed
   */
  void match(const Media& tmplMedia, MediaGroup& group, const SearchParams& params);

  // todo: cache load/save/update

 private:
  QHash<QString, int> _cache;
  QReadWriteLock _lock;
};
