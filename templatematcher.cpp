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
#include "profile.h"
#include "qtutil.h"

#include "opencv2/highgui/highgui.hpp"
#include "opencv2/video/tracking.hpp"  // estimateRigidTransform

TemplateMatcher::TemplateMatcher() {}

TemplateMatcher::~TemplateMatcher() {}

void TemplateMatcher::match(Media& tmplMedia, MediaGroup& group,
                            const SearchParams& params) {
  if (group.count() <= 0) return;

  uint64_t then = nanoTime();

  // matching is slow, look for results in our cache first
  bool useCache = true;

  if (tmplMedia.md5().isEmpty()) {
    if (params.verbose)
      qWarning() << "tmpl image has no md5 sum, won't cache:" << tmplMedia.path();
    useCache = false;
  }

  for (const Media& m : group)
    if (m.md5().isEmpty()) {
      qWarning() << "cand image has no md5 sum, won't cache:" << m.path();
      useCache = false;
    }

  MediaGroup good, notCached;

  if (useCache) {
    QReadLocker locker(&_lock);

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
  tmplMedia.makeKeyPoints(tmplImg, params.needleFeatures);
  tmplMedia.makeKeyPointDescriptors(tmplImg);

  if (params.verbose)
    qInfo("query kp=%d descriptors=%d (max %d)",
          int(tmplMedia.keyPoints().size()),
          int(tmplMedia.keyPointDescriptors().cols), params.needleFeatures);

  if (tmplMedia.keyPointDescriptors().cols <= 0) {
    qWarning() << "no keypoints in template:" << tmplMedia.path();
    return;
  }

  // build brute force matcher for the target image
  // fixme: would another matcher be faster?
  cv::BFMatcher matcher(cv::NORM_HAMMING, true);
  std::vector<cv::Mat> haystack;

  haystack.push_back(tmplMedia.keyPointDescriptors());
  matcher.add(haystack);

  // similarity hash for matching good candidates
  uint64_t tmplHash = dctHash64(tmplImg);

  struct {
    uint64_t targetResize;
    uint64_t targetLoad;
    uint64_t targetKeyPoints;
    uint64_t targetFeatures;
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

  // check each candidate image
  for (int i = 0; i < notCached.count(); i++) {
    Media& m = notCached[i];
    QString cacheKey(m.md5() + tmplMedia.md5());
    if (!useCache) cacheKey = "invalid-cache-key";

    // decompress and build larger set of keypoints (params.haystackFeatures)
    qImg = m.loadImage();
    if (qImg.isNull()) {
      qWarning() << "failure to load cand image:" << m.path();
      continue;
    }

    cv::Mat img;
    qImageToCvImg(qImg, img);

    PROFILE(timing.targetLoad);

    // if the candidate image is much larger than the
    // target image, resize the candidate to better focus features
    // Assumes the crop did not take away the majority of the image.
    // fixme:settings: the scale factor should be a search parameter
    float candScale = 1.0;

    if (tmplImg.rows * tmplImg.cols < img.rows * img.cols) {
      int cSize = std::max(img.cols, img.rows);
      int tSize = std::max(tmplImg.rows, tmplImg.cols);
      int maxSize = tSize * 2;
      if (cSize > maxSize) {
        candScale = float(maxSize) / cSize;
        sizeScaleFactor(img, candScale);
      }
    }

    PROFILE(timing.targetResize);

    m.makeKeyPoints(img, params.haystackFeatures);

    PROFILE(timing.targetKeyPoints);

    m.makeKeyPointDescriptors(img);

    PROFILE(timing.targetFeatures);

    if (params.verbose)
      qInfo("(%d) candidate scale=%.2f kp=%d descriptors=%d (max %d)", i,
            double(candScale), int(m.keyPoints().size()),
            int(m.keyPointDescriptors().rows), params.haystackFeatures);

    if (m.keyPointDescriptors().cols <= 0) {
      if (params.verbose) qInfo("(%d): no keypoints in candidate", i);
      continue;
    }

    // match descriptors in the template and candidate
    std::vector<std::vector<cv::DMatch> > dmatch;
    matcher.radiusMatch(m.keyPointDescriptors(), dmatch, params.cvThresh);

    PROFILE(timing.radiusMatch);

    int score = 0;
    int nMatches = 0;

    MatchList matches;

    for (size_t k = 0; k < dmatch.size(); k++)
      for (size_t j = 0; j < dmatch[k].size(); j++) {
        int distance = int(dmatch[k][j].distance);

        matches.push_back(dmatch[k][j]);

        nMatches++;
        score += distance;
      }

    if (nMatches <= 0) {
      if (params.verbose) qInfo("(%d): no keypoint matches", i);
      QWriteLocker locker(&_lock);
      _cache[cacheKey] = INT_MAX;
      continue;
    }

    // get the x,y coordinates of each match in the target and candidate
    const KeyPointList& trainKp = tmplMedia.keyPoints();
    const KeyPointList& queryKp = m.keyPoints();

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
      if (params.verbose) qInfo("(%d): less than 3 keypoint matches", i);
      QWriteLocker locker(&_lock);
      _cache[cacheKey] = INT_MAX;
      continue;
    }

    // find an affine transform from the target points to the candidate.
    // if there is such a transform, it is most likely a good match.
    cv::Mat transform =
        cv::estimateRigidTransform(tmplPoints, matchPoints, false);

    PROFILE(timing.estimateTransform);

    if (transform.empty()) {
      if (params.verbose) qInfo("(%d): no transform found", i);
      QWriteLocker locker(&_lock);
      _cache[cacheKey] = INT_MAX;
      continue;
    }

    // validate the match
    // take section from candidate that should represent
    // the target, then compare with the template image
    // for similarity
    // todo: this fails to match images that have been put on different
    // backgrounds; to solve that, don't use phash to score; one idea,
    // use the closest x matched keypoints, transform them, and measure
    // the distance from the actual keypoint
    std::vector<cv::Point2f> tmplRect;
    tmplRect.push_back(cv::Point2f(0, 0));
    tmplRect.push_back(cv::Point2f(tmplImg.cols, 0));
    tmplRect.push_back(cv::Point2f(tmplImg.cols, tmplImg.rows));
    tmplRect.push_back(cv::Point2f(0, tmplImg.rows));

    std::vector<cv::Point2f> candRect;

    cv::transform(tmplRect, candRect, transform);

    {
      // set the roi rect in the match;
      // todo: instead of the image corners, map the image borders
      QVector<QPoint> roi;
      for (uint i = 0; i < 4; i++)
        roi.append(QPoint(int(candRect[i].x / candScale),
                          int(candRect[i].y / candScale)));
      m.setRoi(roi);

      // make qt-compatible transform matrix
      // redo estimate since we want transform on the original, unscaled image
      for (cv::Point2f& p : matchPoints) {
        p.x /= candScale;
        p.y /= candScale;
      }

      const cv::Mat tx = cv::estimateRigidTransform(tmplPoints, matchPoints, false);
      if (tx.empty())
        qWarning("(%d): roi: empty transform", i);
      else if (tx.rows < 2 || tx.cols < 3)
        qWarning("(%d): roi: transform rows/cols invalid", i);
      else {
        QTransform qtx(tx.at<double>(0, 0), tx.at<double>(1, 0), tx.at<double>(0, 1),
                       tx.at<double>(1, 1), tx.at<double>(0, 2), tx.at<double>(1, 2));

        m.setTransform(qtx);
      }
    }

    // score the match by transforming cand patch and taking its phash
    // we could do the reverse (transform the template) but this is better
    // assuming candidate is bigger than the template
    cv::invertAffineTransform(transform, transform);
    cv::warpAffine(img, img, transform, tmplImg.size());
    grayscale(img, img);

    // if template has alpha channel, copy it to the transformed image (mask)
    // otherwise, the phashes won't match at all
    if (tmplImg.channels() == 4) {
      Q_ASSERT(img.channels() == 1);

      for (int y = 0; y < tmplImg.rows; y++) {
        uint8_t* src = tmplImg.ptr(y);
        uint8_t* dst = img.ptr(y);

        for (int x = 0; x < tmplImg.cols; x++) {
          uint8_t srcAlpha = *(src + x * 4 + 3);

          if (srcAlpha == 0) *(dst + x) = 0;
        }
      }
    }

    PROFILE(timing.matchResize);

    uint64_t imgHash = dctHash64(img);

    int dist = hamm64(imgHash, tmplHash);

    PROFILE(timing.matchPhash);

    m.setScore(dist);

    if (dist < params.dctThresh)
      good.append(m);
    else {
      if (params.verbose) qInfo("(%d): dct hash on transform doesn't match: score %d", i, dist);

      // show the images that we hashed side by side
      if (getenv("TEMPLATE_MATCHER_DEBUG")) {
        QImage test(1200, 1200, QImage::Format_RGB32);
        test.fill(Qt::green);
        QPainter painter(&test);

        QImage tImg, txImg;
        cvImgToQImage(tmplImg, tImg);
        cvImgToQImage(img, txImg);

        painter.drawImage(0, 0, tImg);
        painter.drawImage(tImg.width(), 0, txImg);

        cv::Mat testImg;
        qImageToCvImg(test, testImg);
        cv::namedWindow("crop");
        cv::imshow("crop", testImg);
        cv::waitKey(0);
      }
    }

    QWriteLocker locker(&_lock);
    _cache[cacheKey] = dist;
  }

  uint64_t now = nanoTime();
  uint64_t total = now - then;

  if (params.verbose)
    qInfo(
      "%lld/%lld %dms:tot %lldms:ea | tl=%.2f tr=%.2f tk=%.2f "
      "tf=%.2f rm=%.2f ms=%.2f ert=%.2f mr=%.2f mp=%.2f ttl=%.2f",
      good.count(), notCached.count(), int(total) / 1000000,
      total / 1000000 / notCached.count(),
      timing.targetLoad * 100.0 / total, timing.targetResize * 100.0 / total,
      timing.targetKeyPoints * 100.0 / total,
      timing.targetFeatures * 100.0 / total, timing.radiusMatch * 100.0 / total,
      timing.matchSort * 100.0 / total,
      timing.estimateTransform * 100.0 / total,
      timing.matchResize * 100.0 / total, timing.matchPhash * 100.0 / total,
      (timing.targetLoad + timing.targetResize + timing.targetKeyPoints +
       timing.targetFeatures + timing.radiusMatch + timing.matchSort +
       timing.estimateTransform + timing.matchResize + timing.matchPhash) *
          100.0 / total);

  group = good;
  std::sort(group.begin(), group.end());
}
