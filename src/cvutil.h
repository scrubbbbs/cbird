/* Operations on cv::Mat images
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

class QMutex;

#include "cimg_fwd.h"
//#include "opencv2/imgproc/imgproc.hpp"
namespace cv {
class Mat;
#define FWD_INTER_LANCZOS4 (4)
}

/// per-thread error logger, useful for scanner
class CVErrorLogger {
  Q_DISABLE_COPY_MOVE(CVErrorLogger);
  CVErrorLogger() = delete;

 public:
  CVErrorLogger(const QString& context);
  ~CVErrorLogger();

 private:
  static int log(int status, const char* func_name, const char* err_msg, const char* file_name,
                 int line, void* userData);
  static QMutex& mutex();
  static QHash<QThread*, CVErrorLogger*>& map();

  QString _context;
};

/**
 * @brief Store LUV color and its weight
 * @note Functions declared inline for speed
 *       In general this can change without too much breakage
 *       in compareColors() and colorDescriptor()
 * @note LUV is a metric space, useful for search
 */
struct DescriptorColor {
  // need 16 bits since L,u,v can exceed [0-255]
  uint16_t l;  // L,u,v compressed to 0-65535
  uint16_t u;
  uint16_t v;
  uint16_t w;  // weight/frequency

  typedef uint64_t key_t;

  static int max() { return UINT16_MAX; }

  static int clamp16(int n) {
    n &= -(n >= 0);
    return n | ((UINT16_MAX - n) >> 31);
  }

  // unique key
  key_t key() const { return key_t(l) << 32 | key_t(u) << 16 | key_t(v); }

  void setKey(const key_t key) {
    l = (key >> 32) & 0xFFFF;
    u = (key >> 16) & 0xFFFF;
    v = key & 0xFFFF;
  }

  // decompress
  void get(float& l_, float& u_, float& v_) const {
    l_ = l * 100.0f / UINT16_MAX;
    u_ = u * 354.0f / UINT16_MAX - 134.0f;
    v_ = v * 262.0f / UINT16_MAX - 140.0f;
  }

  // compress (lossy)
  void set(float l_, float u_, float v_) {
    l = clamp16(int(UINT16_MAX / 100.0f * l_)) & 0xFFFF;
    u = clamp16(int(UINT16_MAX / 354.0f * (u_ + 134.0f))) & 0xFFFF;
    v = clamp16(int(UINT16_MAX / 262.0f * (v_ + 140.0f))) & 0xFFFF;
  }

  QColor toQColor() const;
};

/**
 * @brief Storage unit of histogram-based color search
 */
struct ColorDescriptor {
  enum { NUM_DESC_COLORS = 32 }; // number of colors in the histogram
  DescriptorColor colors[NUM_DESC_COLORS] = {};
  uint8_t numColors = 0;  // <= NUM_DESC_COLORS

  void clear() noexcept {
    memset(&colors, 0, sizeof(colors));
    numColors = 0;
  }
  static void create(const cv::Mat& cvImg, ColorDescriptor& desc);
  static float distance(const ColorDescriptor& a_, const ColorDescriptor& b_);
};

// conversions to/from CImg
void cImgToCvImg(const CImg<uint8_t>& img, cv::Mat& cvImg);
void cvImgToCImg(const cv::Mat& cvImg, CImg<uint8_t>& cImg);

// show image viewer and wait for key press
void showImage(const cv::Mat& img);

// convert QImage to cv::Mat
// unless QImage is manipulated in some way,
// will produce same results as cv::imread provided that you pass:
//  for 1-bit images, CV_LOAD_GRAYSCALE
//  for 16-bit images, CV_LOAD_COLOR
//  all others CV_LOAD_IMAGE_UNCHANGED
void qImageToCvImg(const QImage& src, cv::Mat& dst);

// convert w/o copying pixels
// BGR/RGB swap is not performed as in normal conversion
// QImage must not be destroyed before Mat
void qImageToCvImgNoCopy(const QImage& src, cv::Mat& dst);

void cvImgToQImage(const cv::Mat& src, QImage& dst,
                   QImage::Format forceFormat = QImage::Format_Invalid);
void cvImgToQImageNoCopy(const cv::Mat& src, QImage& dst,
                         QImage::Format forceFormat = QImage::Format_Invalid);

QString cvMatTypeName(int type);

// load opencv matrix from a buffer
void loadMatrix(int rows, int cols, int type, int stride, const char* src, cv::Mat& m);

// byte array for matrix meta data associated with a database image
QByteArray matrixHeader(uint32_t mediaId, const cv::Mat& m);

// byte array for matrix content
QByteArray matrixData(const cv::Mat& m);

#if DEADCODE
// load/store matrix with associated ids
void loadMatrixArray(const QString& path, std::vector<uint32_t>& mediaIds,
                     std::vector<cv::Mat>& array);

void saveMatrixArray(const std::vector<uint32_t>& mediaIds, const std::vector<cv::Mat>& array,
                     const QString& path);
#endif

// general load/store cv::Mat
void loadMatrix(const QString& path, cv::Mat& mat);

void saveMatrix(const cv::Mat& mat, const QString& path);

// bit-exact compare
bool compare(const cv::Mat& a, const cv::Mat& b);

// convert image to grayscale
void grayscale(const cv::Mat& input, cv::Mat& output);

/**
 * Remove letter boxing (balanced solid color borders)
 * @param cvImg source image
 * @param range number of nearby gray values to consider solid
 * @note removes equalized/balanced border from all edges, if
 * border thickness is variable, the lesser amount is removed
 */
void autocrop(cv::Mat& cvImg, int range = 50);

// detect NxM image grid (e.g. thumbnail grid) and return each sub-rect
struct DemosaicParams {
  int clipHistogramPercent = 10; // removes noise from dark/light solid lines
  float borderThresh = 19.0;     // todo: remove
  int preBlurKernel = 3;         // 3x3 gaussian, denoise the source
  int cannyThresh1 = 50;
  int cannyThresh2 = 0;
  int postBlurKernel = 3; // 3x3 gaussian, blur the edge map
  int hMargin = 0;        // todo:remove
  int vMargin = 0;        // todo:remove
  float houghRho = 0.5;   // search step, radius; < 1 seems to help a lot
  float houghTheta = 45;  // search step, degrees; 45 slightly better than 90
  float houghThreshFactor = 0.8;     // # intersections is width/height * factor

  int minGridSpacing = 96;           // line filtering: minimum grid size
  int gridTolerance = 4;             // line filtering: maximum deviation from grid spacing
  int cropMargin = 2;                // reduce output rectangles to remove lines/borders
  float maxAspectRatio = 2.5;        // if aspect greater, assume subdivision is needed
  float minAspectRatio = 0.5;        // same
};

/**
 * @brief Grid segmentation using using edge detection+hough transform
 * @param cvImg
 * @param rects rectangles of any found
 * @param params settings
 * @param outImages optional diagnostic images
 */
void demosaicHough(const cv::Mat& cvImg,
                   QVector<QRect>& rects,
                   const DemosaicParams& params = {},
                   QList<QImage>* outImages = nullptr);

/**
 * @brief Grid segmentation using basic line detection
 * @param cvImg
 * @param rects
 */
void demosaic(const cv::Mat& cvImg, QVector<QRect>& rects);

/**
 *  Automatic brightness and contrast optimization with optional histogram clipping
 *  @param [in]src Input image GRAY, BGR or BGRA
 *  @param [out]dst Destination image
 *  @param clipHistPercent cut wings of histogram at given percent typical=>1, 0=>Disabled
 *  @note In case of BGRA image, we won't touch the transparency
 *  @see doc/brightness-contrast-auto.pdf
 **/
void grayLevel(const cv::Mat &src, float clipHistPercent, int &minGray, int &maxGray);
void stretchContrast(const cv::Mat &src, cv::Mat &dst, int minGray, int maxGray);
void brightnessAndContrastAuto(const cv::Mat& src, cv::Mat& dst, float clipHistPercent = 0);

// libpash optional for testing
#ifdef ENABLE_LIBPHASH
uint64_t phash64_cimg(const cv::Mat& cvImg);
#endif

/**
 * @brief phash-like 64-bit dct hash
 * @param cvImg input image, will be converted to grayscale
 * @param inPlace if true then accept that input could be modified
 * @return
 */
uint64_t dctHash64(const cv::Mat& cvImg, bool inPlace=false);

/// average intensity with phash-like quantization
uint64_t averageHash64(const cv::Mat& cvImg);

/// make the longest side of the image == size
void sizeLongestSide(cv::Mat& img, int size, int filter = FWD_INTER_LANCZOS4); // cv::INTER_LANCZOS4);

// size by a factor
void sizeScaleFactor(cv::Mat& img, float factor);

// size to given amount, stretch it
void sizeStretch(cv::Mat& img, int w, int h);

QStringList cvVersion();
