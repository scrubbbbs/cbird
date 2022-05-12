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

#include "cimg_fwd.h"
#include "opencv2/imgproc/imgproc.hpp"

/// per-thread error logger, useful for scanner
class CVErrorLogger {
 public:
  CVErrorLogger(const QString& context);
  ~CVErrorLogger();
 private:
  Q_DISABLE_COPY_MOVE(CVErrorLogger);
  CVErrorLogger() = delete;
  static int log(int status, const char* func_name,
                   const char* err_msg, const char* file_name,
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

// number of colors in the histogram
#define NUM_DESC_COLORS (32)

/**
 * @brief Storage unit of histogram-based color search
 */
struct ColorDescriptor {
  DescriptorColor colors[NUM_DESC_COLORS] = {};
  uint8_t numColors = 0; // <= NUM_DESC_COLORS

  void clear() noexcept {
    memset(&colors, 0, sizeof(colors));
    numColors = 0;
  }
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
                   QImage::Format forceFormat=QImage::Format_Invalid);
void cvImgToQImageNoCopy(const cv::Mat& src, QImage& dst,
                         QImage::Format forceFormat=QImage::Format_Invalid);

QString cvMatTypeName(int type);

// load opencv matrix from a buffer
void loadMatrix(int rows, int cols, int type, int stride, const char* src,
                cv::Mat& m);

// byte array for matrix meta data associated with a database image
QByteArray matrixHeader(uint32_t mediaId, const cv::Mat& m);

// byte array for matrix content
QByteArray matrixData(const cv::Mat& m);

#if DEADCODE
// load/store matrix with associated ids
void loadMatrixArray(const QString& path, std::vector<uint32_t>& mediaIds,
                     std::vector<cv::Mat>& array);

void saveMatrixArray(const std::vector<uint32_t>& mediaIds,
                     const std::vector<cv::Mat>& array, const QString& path);
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
void demosaic(const cv::Mat& cvImg, QVector<QRect>& rects);

/**
 *  Automatic brightness and contrast optimization with optional
 *histogram clipping
 *  @param [in]src Input image GRAY, BGR or BGRA
 *  @param [out]dst Destination image
 *  @param clipHistPercent cut wings of histogram at given percent typical=>1,
 *0=>Disabled
 *  @note In case of BGRA image, we won't touch the transparency
 *  @see
 *http://answers.opencv.org/question/75510/how-to-make-auto-adjustmentsbrightness-and-contrast-for-image-android-opencv-image-correction/
 **/
void brightnessAndContrastAuto(const cv::Mat& src, cv::Mat& dst,
                               float clipHistPercent = 0);

// perceptual hashes
#ifdef ENABLE_LIBPHASH
uint64_t phash64_cimg(const cv::Mat& cvImg);
#endif

uint64_t dctHash64(const cv::Mat& cvImg);
uint64_t averageHash64(const cv::Mat& cvImg);

// color hash
float colorDistance(const ColorDescriptor& a, const ColorDescriptor& b);
void colorDescriptor(const cv::Mat& cvImg, ColorDescriptor& desc);

// inline uint64_t histogram64(const cv::Mat& cvImg) { (void)cvImg; return 0; }

// make the longest side of the image == size
inline void sizeLongestSide(cv::Mat& img, int size,
                            int filter = cv::INTER_LANCZOS4) {
  int w, h;
  float aspect = float(img.size().width) / img.size().height;
  if (img.size().width > img.size().height) {
    w = size;
    h = int(w / aspect);
  } else {
    h = size;
    w = int(aspect * h);
  }

  // cv::resize will throw a more obscure message
  if (w == 0 || h == 0)
    throw std::invalid_argument(
        "sizeLongestSide: computed width or height is 0, probably bad input");

  cv::resize(img, img, cv::Size(w, h), 0, 0, filter);
}

// size by a factor
inline void sizeScaleFactor(cv::Mat& img, float factor) {
  int w = int(img.size().width * factor);
  int h = int(img.size().height * factor);

  cv::resize(img, img, cv::Size(w, h), 0, 0, cv::INTER_LANCZOS4);
}

// size to given amount, stretch it
inline void sizeStretch(cv::Mat& img, int w, int h) {
  cv::resize(img, img, cv::Size(w, h), 0, 0, cv::INTER_LANCZOS4);
}

QStringList cvVersion();
