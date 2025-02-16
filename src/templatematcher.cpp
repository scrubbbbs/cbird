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
#include "templatematcher.h"

#include "cvutil.h"
#include "hamm.h"
#include "index.h"
#include "media.h"
#include "opencv2/features2d.hpp"
#include "opencv2/video/tracking.hpp"  // estimateRigidTransform
#include "profile.h"

#include <QtCore/QReadWriteLock>
#ifdef TESTING
#include <QtGui/QPainter>
#include <QtWidgets/QLabel>
#endif

TemplateMatcher::TemplateMatcher() {
  _lock = new QReadWriteLock;
}

TemplateMatcher::~TemplateMatcher() {
  delete _lock;
}

void TemplateMatcher::match(const Media& tmplMedia, MediaGroup& group, const SearchParams& params) {
  if (group.count() <= 0) return;

  uint64_t then = nanoTime();

  // matching is slow, look for results in our cache first
  bool useCache = true;

  if (tmplMedia.md5().isEmpty()) {
    if (params.verbose) qWarning() << "tmpl image has no md5 sum, won't cache:" << tmplMedia.path();
    useCache = false;
  }

  for (const Media& m : group)
    if (m.md5().isEmpty()) {
      if (params.verbose) qWarning() << "cand image has no md5 sum, won't cache:" << m.path();
      useCache = false;
    }

  MediaGroup good, notCached;

  if (useCache) {
    QReadLocker locker(_lock);

    for (int i = 0; i < group.count(); i++) {
      Media& m = group[i];
      Q_ASSERT(!m.md5().isEmpty());

      // cache stores one key (md5(a)+md5(b)) for each pair of images that have
      // been template matched; check both possible keys
      const QString cacheKey(m.md5() + tmplMedia.md5());
      const QString key2(tmplMedia.md5() + m.md5());

      QString key;

      if (_cache.contains(cacheKey))
        key = cacheKey;
      else if (_cache.contains(key2))
        key = key2;

      if (!key.isNull()) {
        int dist = _cache[key];
        m.setScore(dist);
        if (dist < params.tmThresh) good.append(m);
      } else
        notCached.append(m);
    }
  } else
    notCached = group;

  group.clear();

  // if all images pairs are cached, return immediately
  if (notCached.count() == 0) {
    if (params.verbose) qDebug("all cached");
    group = good;
    std::sort(group.begin(), group.end());
    return;
  }

  // decompress target image and build high-res
  // feature keypoints and descriptors
  QImage qImg = tmplMedia.loadImage();
  if (qImg.isNull()) {
    qWarning() << "failure to load tmpl image:" << tmplMedia.path();
    return;
  }

  cv::Mat tmplImg;
  qImageToCvImg(qImg, tmplImg);

  // Media needle = tmplMedia;

  KeyPointList tmplKeypoints;
  KeyPointDescriptors tmplDescriptors;
  tmplMedia.makeKeyPoints(tmplImg, params.needleFeatures, tmplKeypoints);
  tmplMedia.makeKeyPointDescriptors(tmplImg, tmplKeypoints, tmplDescriptors);

  if (params.verbose)
    qInfo("tmpl kp=%d descriptors=%d (max %d)", int(tmplKeypoints.size()),
          int(tmplDescriptors.cols), params.needleFeatures);

  if (tmplDescriptors.cols <= 0) {
    qWarning() << "no keypoints in template:" << tmplMedia.path();
    return;
  }

  // build brute force matcher for the target image
  // TODO: FLANN should be faster, but does other overhead dominate?
  cv::BFMatcher matcher(cv::NORM_HAMMING, true);
  std::vector<cv::Mat> haystack;

  haystack.push_back(tmplDescriptors);
  matcher.add(haystack);

  struct {
    uint64_t candResize;
    uint64_t candLoad;
    uint64_t candKeypoints;
    uint64_t candFeatures;
    uint64_t radiusMatch;
    uint64_t matchSort;
    uint64_t estimateTransform;
    uint64_t matchResize;
    uint64_t matchPhash;
  } timing;

  memset(&timing, 0, sizeof(timing));
  uint64_t ns0 = nanoTime(), ns1 = 0;

#define PROFILE(x)  \
  ns1 = nanoTime(); \
  x += (ns1 - ns0); \
  ns0 = ns1;

  // check each candidate image against the template
  for (int i = 0; i < notCached.count(); i++) {
    Media& m = notCached[i];
    QString cacheKey(m.md5() + tmplMedia.md5());
    if (!useCache) cacheKey = "invalid-cache-key";

    qImg = m.loadImage();
    if (qImg.isNull()) {
      qWarning() << "failure to load cand image:" << m.path();
      continue;
    }

    cv::Mat img;
    qImageToCvImg(qImg, img);

    PROFILE(timing.candLoad);

    // if the candidate image is much larger than the
    // target image, we generate too many features that
    // don't show up in the template. If we have some
    // idea of how much cropping there is we can
    // shrink the cand first.
    float candScale = 1.0;

    if (tmplImg.rows * tmplImg.cols < img.rows * img.cols) {
      int cSize = std::max(img.cols, img.rows);
      int tSize = std::max(tmplImg.rows, tmplImg.cols);
      int maxSize = tSize * params.tmScalePct / 100.0;
      if (cSize > maxSize) {
        candScale = float(maxSize) / cSize;
        sizeScaleFactor(img, candScale);
      }
    }

    PROFILE(timing.candResize);

    KeyPointList queryKeypoints;
    m.makeKeyPoints(img, params.haystackFeatures, queryKeypoints);

    PROFILE(timing.candKeypoints);

    KeyPointDescriptors queryDescriptors;
    m.makeKeyPointDescriptors(img, queryKeypoints, queryDescriptors);

    PROFILE(timing.candFeatures);

    if (params.verbose)
      qInfo("(%d) cand scale=%.2f kp=%d descriptors=%d (max %d)", i, double(candScale),
            int(queryKeypoints.size()), int(queryDescriptors.rows), params.haystackFeatures);

    if (queryDescriptors.cols <= 0) {
      if (params.verbose) qWarning("(%d) no keypoints in cand", i);
      continue;
    }

    // match descriptors in the template and candidate
    std::vector<std::vector<cv::DMatch> > dmatch;
    matcher.radiusMatch(queryDescriptors, dmatch, params.cvThresh);

    PROFILE(timing.radiusMatch);

    int nMatches = 0;
    MatchList matches;

    for (size_t k = 0; k < dmatch.size(); k++)
      for (size_t j = 0; j < dmatch[k].size(); j++) {
        matches.push_back(dmatch[k][j]);
        nMatches++;
      }

    if (nMatches <= 0) {
      if (params.verbose) qInfo("(%d) no keypoint matches", i);
      QWriteLocker locker(_lock);
      _cache[cacheKey] = INT_MAX;
      continue;
    }

    // get the x,y coordinates of each match in the target and candidate
    const KeyPointList& trainKp = tmplKeypoints;
    const KeyPointList& queryKp = queryKeypoints;

    std::vector<cv::Point2f> tmplPoints, matchPoints;

    for (const cv::DMatch& match : matches) {
      Q_ASSERT(match.trainIdx < int(trainKp.size()));

      const cv::KeyPoint& kp = trainKp[uint(match.trainIdx)];

      tmplPoints.push_back(kp.pt);
      matchPoints.push_back(queryKp[uint(match.queryIdx)].pt);
    }

    PROFILE(timing.matchSort);

    // need at least 3 points to estimate transform
    if (tmplPoints.size() < 3) {
      if (params.verbose) qInfo("(%d) less than 3 keypoint matches", i);
      QWriteLocker locker(_lock);
      _cache[cacheKey] = INT_MAX;
      continue;
    }

    // find an affine transform from the target points to the candidate.
    // if there is such a transform, it is most likely a good match.
    cv::Mat transform = cv::estimateRigidTransform(tmplPoints, matchPoints, false);

    PROFILE(timing.estimateTransform);

    if (transform.empty()) {
      if (params.verbose) qInfo("(%d) no transform found", i);
      QWriteLocker locker(_lock);
      _cache[cacheKey] = INT_MAX;
      continue;
    }

    // validate the match
    // take section from candidate that should represent
    // the target, then compare with the template image
    // for similarity
    // TODO: this fails to match images that have been put on different
    // backgrounds; to solve that, don't use phash to score; score
    // the area around each keypoint; or don't do this at and use strength
    // of the keypoint correlation

    std::vector<cv::Point2f> tmplRect;
    tmplRect.push_back(cv::Point2f(0, 0));
    tmplRect.push_back(cv::Point2f(tmplImg.cols, 0));
    tmplRect.push_back(cv::Point2f(tmplImg.cols, tmplImg.rows));
    tmplRect.push_back(cv::Point2f(0, tmplImg.rows));

    std::vector<cv::Point2f> candRect;

    cv::transform(tmplRect, candRect, transform);

    {
      // set the roi rect in the match;
      // TODO: instead of the image corners, map the image borders
      QVector<QPoint> roi;
      for (uint i = 0; i < 4; i++)
        roi.append(QPoint(int(candRect[i].x / candScale), int(candRect[i].y / candScale)));
      m.setRoi(roi);

      // make qt-compatible transform matrix
      // redo estimate since we want transform on the original, unscaled image
      for (cv::Point2f& p : matchPoints) {
        p.x /= candScale;
        p.y /= candScale;
      }

      const cv::Mat tx = cv::estimateRigidTransform(tmplPoints, matchPoints, false);
      if (tx.empty())
        qWarning("(%d) roi: empty transform", i);
      else if (tx.rows < 2 || tx.cols < 3)
        qWarning("(%d) roi: transform rows/cols invalid", i);
      else {
        QTransform qtx(tx.at<double>(0, 0), tx.at<double>(1, 0), tx.at<double>(0, 1),
                       tx.at<double>(1, 1), tx.at<double>(0, 2), tx.at<double>(1, 2));

        m.setTransform(qtx);
      }
    }

    // score the match by transforming cand patch and taking its hash
    //
    // if the cand patch is smaller than the template, undefined pixels
    // are around it (black); Copy those pixels to a copy of the template
    // image so the two are compareable.
    //
    // if template has alpha channel, copy it to the cand as well
    //
    //
    cv::invertAffineTransform(transform, transform);
    cv::warpAffine(img, img, transform, tmplImg.size(), cv::INTER_AREA,
                   cv::BORDER_CONSTANT, cv::Scalar(0,0,0,255));

    PROFILE(timing.matchResize);

    cv::Mat tmplMasked = tmplImg.clone();

    // make "0" the mask indicator, dctHash needs gray anyways
    grayscale(img, img);

    {
      const int srcChannels = tmplMasked.channels();
      Q_ASSERT(srcChannels >= 1 && srcChannels <= 4);
      Q_ASSERT(img.channels() == 1);

      for (int y = 0; y < tmplMasked.rows; ++y) {
        uint8_t* src = tmplMasked.ptr(y);
        uint8_t* dst = img.ptr(y);

        for (int x = 0; x < tmplMasked.cols; ++x) {
          uint8_t* dp = dst + x;
          uint8_t* sp = src + x * srcChannels;
          const uint8_t dstPixel = *dp;
          const uint8_t dstMask = dstPixel != 0 ? 255 : 0;
          if (srcChannels < 4) {
            for (int j = 0; j < srcChannels; ++j)
              *sp++ &= dstMask;
          } else {
            const int srcAlpha = sp[3];
            sp[0] = ((sp[0] * srcAlpha) >> 8) & dstMask;
            sp[1] = ((sp[1] * srcAlpha) >> 8) & dstMask;
            sp[2] = ((sp[2] * srcAlpha) >> 8) & dstMask;
            sp[3] = 255;
            *dp = (dstPixel * srcAlpha) >> 8;
          }
        }
      }
    }

    uint64_t candHash = dctHash64(img);
    uint64_t tmplHash = dctHash64(tmplMasked);

    int dist = hamm64(candHash, tmplHash);

    PROFILE(timing.matchPhash);

    m.setScore(dist);

    if (dist < params.tmThresh)
      good.append(m);
    else {
      if (params.verbose) qInfo("(%d) match above threshold (%d), consider raising tmThresh", i, dist);
#ifdef TESTING
      if (getenv("TEMPLATE_MATCHER_DEBUG")) {
        QImage tImg, txImg;
        cvImgToQImage(tmplMasked, tImg);
        cvImgToQImage(img, txImg);

        qDebug() << txImg;

        QImage test(tImg.width() + txImg.width() + 30,
                    qMax(tImg.height(), txImg.height()) + 20,
                    QImage::Format_RGB32);
        test.fill(Qt::green);

        QPainter painter(&test);
        painter.setPen(Qt::magenta);
        painter.drawImage(10, 10, tImg);
        painter.translate(10+5+tImg.width(), 10);
        painter.drawImage(0,0, txImg);

        static auto *window = new QLabel();
        window->setWindowTitle(QString("template|cand score:%1").arg(dist));
        window->setFixedSize(test.size());
        window->setPixmap(QPixmap::fromImage(test));
        window->show();
      }
#endif
    }

    QWriteLocker locker(_lock);
    _cache[cacheKey] = dist;
  }

  uint64_t now = nanoTime();
  uint64_t total = now - then;

  if (params.verbose)
    qInfo(
        "%lld/%lld %dms:tot %lldms:ea | ld=%.2f rz=%.2f kp=%.2f "
        "ft=%.2f rm=%.2f ms=%.2f ert=%.2f mr=%.2f mp=%.2f ttl=%.2f",
        good.count(), notCached.count(), int(total) / 1000000, total / 1000000 / notCached.count(),
        timing.candLoad * 100.0 / total, timing.candResize * 100.0 / total,
        timing.candKeypoints * 100.0 / total, timing.candFeatures * 100.0 / total,
        timing.radiusMatch * 100.0 / total, timing.matchSort * 100.0 / total,
        timing.estimateTransform * 100.0 / total, timing.matchResize * 100.0 / total,
        timing.matchPhash * 100.0 / total,
        (timing.candLoad + timing.candResize + timing.candKeypoints + timing.candFeatures +
         timing.radiusMatch + timing.matchSort + timing.estimateTransform + timing.matchResize +
         timing.matchPhash) *
            100.0 / total);

  group = good;
  std::sort(group.begin(), group.end()); // sort by score
}
