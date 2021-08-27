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
   * @param group candidate images
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
  void match(Media& tmplMedia, MediaGroup& group, const SearchParams& params);

  // todo: cache load/save/update

 private:
  QHash<QString, int> _cache;
  QReadWriteLock _lock;
};
