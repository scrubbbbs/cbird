/* CImg utilities
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
#include "cimg_lib.h"
#include "media.h"
#include "profile.h"

#define MT_QUALITYSCORE (1)

// originally used float, uint8_t is fine, cast to int for absolute difference
typedef uint8_t pixel_t;
typedef CImg<pixel_t> img_t;
// #define absDifference(x) fabs((x)-(y))
// #define absDifference(x,y) abs((x)-(y))
#define absDifference(x, y) abs(int(x) - int(y));

/// absolute difference between neighbors in y-direction
static double makeDiff(const uint h0, const uint h1, const img_t& __restrict img,
                       img_t& __restrict diff) {
  const auto w = uint(img.width());
  const auto w1 = w - 1;
  const auto* src = img.data();
  auto* dst = diff.data();
  double sum = 0;  // note: must be double! or mt/st results differ..

  const auto* srcPtr = src + h0 * w;
  auto* dstPtr = dst + h0 * w;

  for (uint y = h0; y < h1; ++y) {
    dstPtr[0] = 0;
    for (uint x = 1; x < w1; ++x) {
      double d = absDifference(srcPtr[x - 1], srcPtr[x + 1]);
      sum += d;
      dstPtr[x] = d;
    }
    dstPtr[w1] = 0;
    srcPtr += w;
    dstPtr += w;
  }
  return sum;
}

static void makeBlur(const uint h0, const uint h1, const img_t& __restrict img,
                     img_t& __restrict blur) {
  // determine if a pixel x,y is blurred or not
  const uint w1 = uint(img.width() - 1);
  for (uint y = h0; y < h1; ++y)
    for (uint x = 1; x < w1; ++x) {
      // blur measured by how far the center pixel
      // is from the mean of its two neighbors.
      int mean = abs(img(x - 1, y) - img(x + 1, y)) / 2;

      int dist = abs(img(x, y) - mean);

      blur(x, y) = dist;
    }
}

/// find edges in y-direction
static void makeEdge(const uint h0, const uint h1, const img_t& __restrict diff, const float mean,
                     CImg<uint8_t>& __restrict edge) {
  const auto w = uint(diff.width());
  const auto w1 = w - 1;

  const auto* __restrict diffPtr = diff.data() + h0 * w;
  auto* __restrict edgePtr = edge.data() + h0 * w;

  auto m = pixel_t(mean);

  for (uint y = h0; y < h1; ++y) {
    edgePtr[0] = 0;
    auto center = diffPtr[0] > m ? diffPtr[0] : 0;
    auto right = diffPtr[1] > m ? diffPtr[1] : 0;
    // auto dR = diffPtr[2] > m ? diffPtr[2] : 0;
    for (uint x = 1; x < w1; ++x) {
      // edge candidates, diff(x,y) > mean
      // rolling left/right neighbors and center
      auto left = center;
      center = right;
      right = diffPtr[x + 1] > m ? diffPtr[x + 1] : 0;

      // const auto left   = dL > m ? dL : 0; //candPtr2[x-1]; //cand(x - 1, y);
      // const auto center = dC > m ? dC : 0; // candPtr2[x];   //cand(x, y);
      // const auto right  = dR > m ? dR : 0; //candPtr2[x+1]; //cand(x + 1, y);

      // "on edge", greater diff than its neighbor candidates
      edgePtr[x] = center > left && center > right ? 255 : 0;
    }
    edgePtr[w1] = 0;
    diffPtr += w;
    edgePtr += w;
  }
}

/**
 * count how many long edges there are. if the image
 * is noisy there aren't as many long edges, if its
 * blurred more edges are long
 *
 * @note only counts vertical/horizontal edges
 * @note edgeT must be transpose of makeEdge result
 */
static int longEdgeCount(const uint h0, const uint h1, const CImg<uint8_t>& edgeT) {

  auto w = uint(edgeT.width());
  auto w1 = w - 1;

  int count = 0;
  const auto* srcPtr = edgeT.data() + (h0+1) * w;

  for (uint y = h0+1; y < h1-1; y++) {
//    int len = 0;
//    for (uint x = 1; x < w1; x++) {
//      if (srcPtr[x] != 0)
//        len++;
//      else {
//        if (len > 1) count++;
//        len = 0;
//      }
//    }
    for (uint x = 2; x < w1 - 1; ++x) {
      const auto* prev = edgeT.data() + (y - 1) * w;
      const auto* next = edgeT.data() + (y + 1) * w;

      // { l0, *, r1 }
      // { h0, x, h1 }
      // { r0, *, l1 }
      if (srcPtr[x] != 0 &&
          ((srcPtr[x - 1] != 0 && srcPtr[x + 1] != 0) || // horizontal
           (prev[x - 1] != 0 && next[x + 1] != 0) || // diagonal
           (next[x - 1] != 0 && prev[x + 1] != 0)))  // diagonal
        count++;
    }

    srcPtr += w;
  }
  return count;
}

void qImageToCImg(const QImage& src, CImg<uint8_t>& dst) {
  dst = CImg<uint8_t>(uint(src.width()), uint(src.height()), 1, 3);
  for (int y = 0; y < src.height(); ++y)
    for (int x = 0; x < src.width(); ++x) {
      QColor pixel(src.pixel(x, y));
      uint ux = uint(x);
      uint uy = uint(y);
      int r, g, b;
      pixel.getRgb(&r, &g, &b);
      dst(ux, uy, 0, 0) = r;
      dst(ux, uy, 0, 1) = g;
      dst(ux, uy, 0, 2) = b;
    }
}

void cImgToQImage(const CImg<uint8_t>& src, QImage& dst) {
  dst = QImage(src.width(), src.height(), QImage::Format_RGB888);

  if (src.spectrum() == 3) {
    for (int y = 0; y < src.height(); y++)
      for (int x = 0; x < src.width(); x++) {
        uint ux = uint(x);
        uint uy = uint(y);
        int r = src(ux, uy, 0, 0);
        int g = src(ux, uy, 0, 1);
        int b = src(ux, uy, 0, 2);

        dst.setPixel(x, y, qRgb(r, g, b));
      }
  } else {
    for (int y = 0; y < src.height(); y++)
      for (int x = 0; x < src.width(); x++) {
        uint ux = uint(x);
        uint uy = uint(y);
        int r = src(ux, uy, 0, 0);

        dst.setPixel(x, y, qRgb(r, r, r));
      }
  }
}

#ifndef MT_QUALITYSCORE

static void filterHorizontal(const img_t& img, img_t& diff, CImg<uint8_t>& edge, float& mean,
                             int& edgeCount) {
  mean = makeDiff(0, img.height(), img, diff);
  mean /= (img.width() - 1) * (img.height() - 1);

  //  blur.fill(0);
  //  makeBlur(1, img.height()-1, img, blur);

  makeEdge(0, diff.height(), diff, mean, edge);
  auto edgeT = edge;
  edgeT.transpose();
  edgeCount = longEdgeCount(0, edgeT.height(), edgeT);
}
#endif

#if MT_QUALITYSCORE

/// find ideal row ranges to assign to each worker thread
static QVector<QVector<int>> workRanges(int begin, int end, int stride) {
  QVector<QVector<int>> ranges;

  int rowsPerJob = ((32 * 1024) / stride) + 1;  // target 32kb data per thread
  //qDebug() << "rowsPerThread" << rowsPerJob << rowsPerJob * stride / 1024 << "kb";

  for (int h0 = begin; h0 < end; h0 += rowsPerJob) {
    int h1 = std::min(h0 + rowsPerJob, end);
    ranges.append({h0, h1});
  }
  return ranges;
}

/// run the qualityscore filter pipeline using multiple threads
static void filterHorizontalMT(const img_t& img, img_t& diff, CImg<uint8_t>& edge, float& mean,
                               img_t& blur,int& edgeCount) {
  // cache-aware work ranges for normal order and transpose
  auto ranges = workRanges(0, img.height(), sizeof(pixel_t) * img.width());
  auto rangesT = workRanges(0, img.width(), sizeof(pixel_t) * img.height());

  QList<QFuture<int>> workI;     // no return value
  QList<QFuture<double>> workF;  // float return value
  QList<QFuture<void>> workV;    // no return value

  for (const auto& range : ranges) {
    int h0 = range[0];
    int h1 = range[1];
    workF.append(
        QtConcurrent::run([h0, h1, &img, &diff]() { return makeDiff(h0, h1, img, diff); }));
  }

  double sum = 0;
  for (auto& w : workF) {
    w.waitForFinished();
    sum += w.result();
  }
  workF.clear();
  mean = sum / (img.width()* img.height());

  for (auto& range : ranges) {
    int h0 = range[0];
    int h1 = range[1];
    workV.append(QtConcurrent::run([h0,h1,&img,&blur]() { return makeBlur(h0, h1, img, blur);}));
    workV.append(QtConcurrent::run(
        [h0, h1, &diff, mean, &edge]() { return makeEdge(h0, h1, diff, mean, edge); }));
  }

  for (auto& w : workV) w.waitForFinished();
  workV.clear();

  auto hEdgeT = edge;
  hEdgeT.transpose();

  for (auto& range : rangesT) {
    int h0 = range[0];
    int h1 = range[1];
    workI.append(QtConcurrent::run([h0, h1, &hEdgeT]() { return longEdgeCount(h0, h1, hEdgeT); }));
  }

  for (auto& w : workI) {
    w.waitForFinished();
    edgeCount += w.result();
  }
  workI.clear();
}
#endif  // MT_QUALITYSCORE

static void addVisual(const img_t& img, const char* label, bool normalize,
                      QVector<QImage>* visuals) {
  if (visuals == nullptr) return;
  auto gray = img;
  if (normalize) gray.normalize(0, (1 << 8) - 1);
  CImg<uint8_t> tmp = gray;
  QImage qImg;
  cImgToQImage(tmp, qImg);
  qImg.setText("description", label);
  visuals->append(qImg);
}
/**
 * no-reference quality metric for images
 *
 * inspired by "No-Reference Image Quality Assessment using Blur and Noise" (2009 WASET)
 * @ref doc/papers/No-Reference-Image-Quality-Assessment-using-Blur-and-Noise.pdf
 *
 * The edge detection method is the same as paper,
 * however instead of blur-noise ratio "away from the edges" as a quality
 * metric, edge ratio and long edge ratio are used. A long edge at the moment
 * means an edge with length > 1.
 *
 * - more edges generally means better quality (less blur) - but could also mean more noise
 * - more long edges generally means higher resolution, and less noise
 *
 * @note it seems to generally be OK but it definitely fails some cases and
 *       the scores are usually closer than they should be
 *
 * TODO: convert to use cvimage and evaluate how good
 * it is against various degradations (rescale, noise, blur,
 * dct noise, blocking, sharpening)
 */
int qualityScore(const Media& m, QVector<QImage>* visuals) {
  class Profiler {
   public:
    uint64_t start = nanoTime();
    uint64_t then = start;
    void step(const char* msg) {
      uint64_t now = nanoTime();
      uint64_t micros = (now - then) / 1000;
      then = now;
      //qDebug() << msg << micros;
    }
  } profiler;

  CImg<uint8_t> src;

  if (!m.image().isNull())
    qImageToCImg(m.image(), src);
  else
    src.load(qUtf8Printable(m.path()));

  profiler.step("load");

  // some cropping usually a good idea
  // note: 0 width crop cimg bug creates blank pixels on the right edge, leads to false edges
  int hCrop = int(src.width() * 0.10);
  int vCrop = int(src.height() * 0.10);
  src.crop(hCrop, vCrop, 0, 0, src.width() - hCrop, src.height() - vCrop, 0, 0);

  // image size must be at least 3x3 or we have buffer overflow, to get anything
  // meaningful must be much larger
  if (src.width() < 64 || src.height() < 64) {
    qWarning() << "cropped image must be at least 64x64 px";
    return -1;
  }

  // convert to grayscale float (0-255)
  img_t img = src.get_norm(1);
  auto imgT = img;
  imgT.transpose();

  profiler.step("normalize");

  addVisual(img, "Normalized & Cropped", true, visuals);

  const uint w = uint(img.width());
  const uint h = uint(img.height());
  const uint w1 = uint(w - 1);
  const uint h1 = uint(h - 1);

  const int blurThreshold = 26; // paper says 0.1 for normalized(0-1) intensities

  int numEdges = 0;
  int numBlur = 0;  // count of blurred edges
  float sumBlur = 0;// sum of blur of the blurred edges


  float longEdgeRatio = 0;
  int goodEdgeCount = 0;
  int numCorners = 0;
  int numSpeckle = 0;
  {
    // run in horizontal direction

#ifdef MT_QUALITYSCORE
    img_t hDiff(w, h);
    img_t hBlur(w, h);
    CImg<uint8_t> hEdge(w, h);
    int hEdgeCount = 0;
    float hMean = 0;

    auto h0 =
        QtConcurrent::run([&]() { filterHorizontalMT(img, hDiff, hEdge, hMean, hBlur, hEdgeCount); });

    //    now = nanoTime();
    //    micros = (now - then) / 1000;
    //    then = now;
    //    qDebug() << "t2" << micros;

    img_t vDiff(h, w);
    img_t vBlur(h, w);
    CImg<uint8_t> vEdge(h, w);
    float vMean = 0;
    int vEdgeCount = 0;

    auto v0 =
        QtConcurrent::run([&]() { filterHorizontalMT(imgT, vDiff, vEdge, vMean, vBlur, vEdgeCount); });

    v0.waitForFinished();
    auto v1 = QtConcurrent::run([&]() { vEdge.transpose(); });
    auto v2 = QtConcurrent::run([&]() { vDiff.transpose(); });
    auto v3 = QtConcurrent::run([&]() { vBlur.transpose(); });
    v1.waitForFinished();
    v2.waitForFinished();
    v3.waitForFinished();

    h0.waitForFinished();

    goodEdgeCount = hEdgeCount + vEdgeCount;

#else  // no concurrency
    img_t hDiff(w, h);  // difference of each pixel's two neighbors
    // img_t hBlur(w, h);  // difference of pixel to mean of two neighbors
    CImg<uint8_t> hEdge(w, h);  // edges from hDiff
    float hMean;                // average of hDiff
    int hEdgeCount;             // number of edges > 1 pixel in length

    filterHorizontal(img, hDiff, hEdge, hMean, hEdgeCount);

    // run in vertical direction by transposing image and outputs
    img_t vDiff(h, w);
    // img_t vBlur(h, w);
    CImg<uint8_t> vEdge(h, w);
    float vMean;
    int vEdgeCount;

    filterHorizontal(imgT, vDiff, vEdge, vMean, vEdgeCount);

    // vBlur.transpose();
    vEdge.transpose();
    vDiff.transpose();
#endif

    profiler.step("filter");

    addVisual(hEdge | vEdge, "Edge", false, visuals);
    addVisual(hDiff, "H Diff", true, visuals);
    addVisual(vDiff, "V Diff", true, visuals);
    //    addVisual(hBlur, "H Blur", true, visuals);
    //    addVisual(vBlur, "V Blur", true, visuals);

    // debug: only using hBlur at the moment
    if (0) {
      img_t kernel(3, 3);  // paper said use 3x3
      kernel.fill(1.0f / (3 * 3));
      img_t cBlur = img;
      cBlur.convolve(kernel);  // slow
      //qDebug("cblur=%.2f", double(cBlur.mean()));
    }

    goodEdgeCount=0;

    const auto edge = vEdge | hEdge;
    for (uint y = 1; y < h1-1; y++)
      for (uint x = 1; x < w1-1; x++) {
        if (edge(x, y)) {
          numEdges++;
          auto hb = hBlur(x,y);
          auto vb = vBlur(x,y);
          int blur = std::max(hb,vb);

          if (blur < blurThreshold) {
            sumBlur += blur/255.0;
            numBlur++;
          }

          // a b c
          // d * e
          // f g h
          int a = 1 & edge(x-1,y-1);
          int b = 1 & edge(x, y-1);
          int c = 1 & edge(x+1,y-1);
          int d = 1 & edge(x-1, y);
          int e = 1 & edge(x+1, y);
          int f = 1 & edge(x-1,y+1);
          int g = 1 & edge(x,y+1);
          int h = 1 & edge(x+1,y+1);

          int sum = a + b + c + d + e + f + g + h;
          if (sum == 0)  // spot/speckle
            numSpeckle++;

          if (sum-3 == 0)
            if ((b + c + g) == 3 || // top-left corner
                (a + b + g) == 3 || // top-right corner
                (b + g + h) == 3 || // bottom-right
                (b + g + f) == 3 )  // bottom-left
            numCorners++;
        }
      }

    profiler.step("reduce");

    // float blurRatio = numBlur / (float)numEdges;
    // printf("thresh=%.6f ratio=%.6f (%d/%d)\n",
    //  blurThresh, blurRatio, numBlur, numEdges);

    longEdgeRatio = float(goodEdgeCount) / numEdges;

    //longEdgeRatio = float(vEdgeCount + hEdgeCount) / ((w-2)*(h-2));

    //qDebug("lr=%.2f", double(longEdgeRatio));

  }  /// edge+blur

  float edgeDensity = float(numEdges) / (w*h);
  //qDebug("ed=%.2f", double(edgeDensity));


  // qDebug("nEdge=%d nBlur=%d", numEdges, numBlur);
  // qDebug("blur: m=%.2f r=%.2f", double(blurMean), double(blurRatio));

  float sumNoise = 0;
  int noiseCount = 0;
  if (1) {  // noise experiment
    // average filter, blur image somewhat
//    const int kSize = 3;  // paper said use 3x3
//    img_t kernel(kSize, kSize);
//    kernel.fill(255 / (kSize * kSize));
//    img.convolve(kernel);

    {
      img_t hDiff(w, h);
      hDiff.fill(0);
      int hMean = makeDiff(0, img.height(), img, hDiff);
      hMean /= img.width() * img.height();

      img_t vDiff(h, w);
      vDiff.fill(0);
      img.transpose();
      int vMean = makeDiff(0, img.height(), img, vDiff);
      vMean /= img.width() * img.height();

      vDiff.transpose();

      qDebug("mean2=(%d,%d) ", hMean, vMean);
      img_t cand(w, h);
      cand.fill(0);
      float sum = 0;
      int num = 0;
      for (uint y = 1; y < h1; y++)
        for (uint x = 1; x < w1; x++) {
          int dh = hDiff(x, y);
          int dv = vDiff(x, y);
          if (dh <= hMean && dv <= vMean) {
            int val = std::max(dh, dv);
            sum += val;
            num++;
            cand(x, y) = val;
          }
        }

      // float candMean = sum / ((w1-1)*(h1-1));
      float candMean = sum / num;

      qDebug("nmean=%.2f ", double(candMean));
      // if(0){
      //     img_t tmp=cand;
      //     tmp.normalize(0,1<<16 -1);
      //     tmp.save("cand.png");
      // }
      for (uint y = 1; y < h1; y++)
        for (uint x = 1; x < w1; x++) {
          float n = cand(x, y);
          if (n > candMean) {
            sumNoise += n/255.0;
            // sumNoise += fabs(n-candMean)/n;
            noiseCount++;
            cand(x, y) = 1;
          } else
            cand(x, y) = 0;
        }

      // if(0){
      //     img_t tmp=cand;
      //     tmp.normalize(0,1<<16 -1);
      //     tmp.save("noise.png");
      // }
    }
  }
  float blurMean = sumBlur / numBlur;
  float blurRatio = numBlur / (float)numEdges;

  float noiseMean = sumNoise / noiseCount;
  float noiseRatio = float(noiseCount) / (w*h);

  //qDebug() << m.name() << "corners:" << numCorners << "speckles:" << numSpeckle;
  int score =  1000 * ( 1.0f - (1*blurMean +0.95*blurRatio + 0.3*noiseMean + 0.75*noiseRatio));

  qDebug() << score << blurMean << blurRatio << noiseMean << noiseRatio;

  return score;
}
