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
#include "cvutil.h"

#include "cimg_lib.h"
#include "cimgops.h"
#include "ioutil.h"
#include "profile.h"

#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

static_assert(cv::INTER_LANCZOS4 == FWD_INTER_LANCZOS4, "check header for invalid constant");

// todo: new versions of load/save matrix that do not have to
// read the whole file into memory before we can start reading/writing
struct MatrixHeader {
  uint32_t id;
  int rows, cols, type, stride;
};

void loadMatrix(int rows, int cols, int type, int stride, const char* src, cv::Mat& m) {
  m.create(rows, cols, type);

  int rowLen = m.size().width * int(m.elemSize());
  Q_ASSERT(rowLen == stride);

  for (int i = 0; i < m.size().height; i++) {
    char* dst = m.ptr<char>(i);
    memcpy(dst, src, size_t(rowLen));
    src += rowLen;
  }
}

QByteArray matrixHeader(uint32_t mediaId, const cv::Mat& m) {
  MatrixHeader h;

  h.id = mediaId;
  h.rows = m.rows;
  h.cols = m.cols;
  h.type = m.type();
  h.stride = m.cols * int(m.elemSize());

  return QByteArray(reinterpret_cast<char*>(&h), sizeof(h));
}

QByteArray matrixData(const cv::Mat& m) {
  const int len = m.cols * int(m.elemSize());
  try {
    QByteArray b;
    for (int i = 0; i < m.rows; ++i) b.append(m.ptr<char>(i), len);
    return b;
  } catch (std::bad_alloc& e) {
    qFatal("QByteArray limits exceeded");
  }
  return QByteArray();
}

#if DEADCODE

void loadMatrixArray(const QString& path, vector<uint32_t>& mediaIds, vector<cv::Mat>& array) {
  void* dataPtr = nullptr;
  uint64_t dataLen;

  loadBinaryData(path, &dataPtr, &dataLen, false);

  char* src = reinterpret_cast<char*>(dataPtr);
  char* end = src + dataLen;
  while (src < end) {
    MatrixHeader* h = reinterpret_cast<MatrixHeader*>(src);

    src += sizeof(*h);
    int len = h->stride * h->rows;

    cv::Mat mat;
    loadMatrix(h->rows, h->cols, h->type, h->stride, src, mat);
    src += len;

    mediaIds.push_back(h->id);
    array.push_back(mat);
  }

  free(dataPtr);
}

void saveMatrixArray(const vector<uint32_t>& mediaIds, const vector<cv::Mat>& array,
                     const QString& path) {
  QFile f(path);
  bool ok = f.open(QFile::WriteOnly | QFile::Truncate);
  Q_ASSERT(ok);

  for (size_t i = 0; i < array.size(); i++) {
    const cv::Mat& m = array[i];
    QByteArray data;
    data += matrixHeader(mediaIds[i], m);
    data += matrixData(m);
    int len = f.write(data);
    if (len != data.length())
      qFatal("write failed: %d: %s", f.error(), qPrintable(f.errorString()));
  }
}
#endif  // DEADCODE

void loadMatrix(const QString& path, cv::Mat& mat) {
  QFile f(path);
  bool ok = f.open(QFile::ReadOnly);
  if (!ok) qFatal("open failed: %d: %s", f.error(), qPrintable(f.errorString()));

  MatrixHeader h;
  int len = f.read(reinterpret_cast<char*>(&h), sizeof(h));
  if (len != sizeof(h))
    qFatal("read failed (header): %d: %s", f.error(), qPrintable(f.errorString()));

  mat.create(h.rows, h.cols, h.type);

  int rowLen = mat.size().width * int(mat.elemSize());
  Q_ASSERT(rowLen == h.stride);

  for (int i = 0; i < mat.size().height; i++) {
    char* dst = mat.ptr<char>(i);
    len = f.read(dst, rowLen);
    if (len != rowLen) qFatal("read failed (row): %d: %s", f.error(), qPrintable(f.errorString()));
  }
}

void saveMatrix(const cv::Mat& mat, const QString& path) {
  writeFileAtomically(path, [&mat](QFile& f) {
    QByteArray data = matrixHeader(0, mat);
    int len = f.write(data);
    if (len != data.length()) throw f.errorString();

    int rowLen = mat.cols * int(mat.elemSize());
    for (int i = 0; i < mat.rows; ++i) {
      len = f.write(mat.ptr<char>(i), rowLen);
      if (len != rowLen) throw f.errorString();
    }
  });
}

void showImage(const cv::Mat& img) {
  const char* title = "showImage";
  cv::namedWindow(title, CV_WINDOW_AUTOSIZE);
  cv::moveWindow(title, 100, 100);
  cv::imshow(title, img);
  cv::waitKey();
  cv::destroyWindow(title);
}

void cImgToCvImg(const CImg<uint8_t>& img, cv::Mat& cvImg) {
  if (img.spectrum() >= 3) {
    cvImg.create(img.height(), img.width(), CV_8UC(3));

    for (int y = 0; y < img.height(); y++) {
      uint8_t* pix = cvImg.ptr<uint8_t>(y);
      for (int x = 0; x < img.width(); x++) {
        uint ux = uint(x);
        uint uy = uint(y);
        pix[0] = img(ux, uy, 0, 2);
        pix[1] = img(ux, uy, 0, 1);
        pix[2] = img(ux, uy, 0, 0);
        pix += 3;
      }
    }
  } else if (img.spectrum() == 1) {
    cvImg.create(img.height(), img.width(), CV_8UC(1));

    for (int y = 0; y < img.height(); y++) {
      uint8_t* pix = cvImg.ptr<uint8_t>(y);
      for (int x = 0; x < img.width(); x++) {
        uint8_t gray = img(uint(x), uint(y));

        pix[0] = gray;
        pix++;
      }
    }
  } else {
    throw std::logic_error("cvImgToCvImg: unsupported image spectrum (bit depth)");
  }
}

void cvImgToCImg(const cv::Mat& cvImg, CImg<uint8_t>& cImg) {
  // note: not tested yet
  const bool isGray = cvImg.type() == CV_8UC(1);

  const uint w = static_cast<unsigned int>(cvImg.cols);
  const unsigned int h = static_cast<unsigned int>(cvImg.rows);

  cImg = CImg<uint8_t>(w, h, 1, isGray ? 1 : 3);

  for (int y = 0; y < cvImg.rows; y++)
    for (int x = 0; x < cvImg.cols; x++) {
      const uint8_t* elem = cvImg.ptr<uint8_t>(y, x);
      uint ux = uint(x);
      uint uy = uint(y);
      cImg(ux, uy, 0, 0) = elem[0];
      if (!isGray) {
        cImg(ux, uy, 0, 1) = elem[1];
        cImg(ux, uy, 0, 2) = elem[2];
      }
    }
}

void qImageToCvImg(const QImage& src, cv::Mat& dst) {
  // qDebug("qImageToCvImage: depth=%d size=%dx%d hasAlpha=%d", src.depth(),
  //       src.width(), src.height(),
  //       src.hasAlphaChannel());

  // you could do this with less code, however since these
  // get used a lot, we want optimized versions of each
  // note: this implementation is possibly wrong on older
  // versions of Qt or big-endian architectures
  const int srcW = src.width();
  const int srcH = src.height();

  switch (src.depth()) {
    case 32:
      if (!src.hasAlphaChannel()) {
        dst = cv::Mat(srcH, srcW, CV_8UC(3));
        for (int y = 0; y < srcH; ++y) {
          const uint8_t* sp = reinterpret_cast<const uint8_t*>(src.constScanLine(y));
          uint8_t* dp = reinterpret_cast<uint8_t*>(dst.ptr(y));
          for (int x = 0; x < srcW; ++x) {
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp += 3;
            sp += 4;
          }
        }
      } else {
        dst = cv::Mat(srcH, srcW, CV_8UC(4));
        for (int y = 0; y < srcH; ++y) {
          const uint32_t* sp = reinterpret_cast<const uint32_t*>(src.constScanLine(y));
          uint32_t* dp = reinterpret_cast<uint32_t*>(dst.ptr(y));
          memcpy(dp, sp, size_t(srcW * 4));
        }
      }
      break;

    case 24:
      dst = cv::Mat(srcH, srcW, CV_8UC(3));
      for (int y = 0; y < srcH; ++y) {
        const uint8_t* sp = reinterpret_cast<const uint8_t*>(src.constScanLine(y));
        uint8_t* dp = reinterpret_cast<uint8_t*>(dst.ptr(y));
        for (int x = 0; x < srcW; ++x) {
          dp[0] = sp[2];
          dp[1] = sp[1];
          dp[2] = sp[0];
          dp += 3;
          sp += 3;
        }
      }
      break;

    case 8:
      switch (src.format()) {
        case QImage::Format_Grayscale8:
          dst = cv::Mat(srcH, srcW, CV_8UC(1));
          for (int y = 0; y < srcH; ++y) {
            const uint8_t* sp = reinterpret_cast<const uint8_t*>(src.constScanLine(y));
            uint8_t* dp = reinterpret_cast<uint8_t*>(dst.ptr(y));
            memcpy(dp, sp, size_t(srcW));
          }
          break;

        case QImage::Format_Indexed8:
          // opencv doesn't have index color, convert to 24-bit rgb
          dst = cv::Mat(srcH, srcW, CV_8UC(3));
          for (int y = 0; y < srcH; ++y) {
            uint8_t* dp = reinterpret_cast<uint8_t*>(dst.ptr(y));
            for (int x = 0; x < srcW; ++x) {
              QRgb pixel = src.pixel(x, y);
              dp[0] = qBlue(pixel) & 0xFF;
              dp[1] = qGreen(pixel) & 0xFF;
              dp[2] = qRed(pixel) & 0xFF;
              dp += 3;
            }
          }
          break;

        default:
          qFatal("unsupported 8-bit QImage pixel format: %d", src.format());
      }
      break;

    case 1:
      dst = cv::Mat(srcH, srcW, CV_8UC(1));
      for (int y = 0; y < srcH; ++y) {
        uint8_t* dp = reinterpret_cast<uint8_t*>(dst.ptr(y));
        for (int x = 0; x < srcW; ++x) {
          QRgb pixel = src.pixel(x, y);
          dp[x] = qRed(pixel) & 0xFF;
        }
      }
      break;

    default:
      qWarning("unsupported depth: %d, converting to RGB888", src.depth());
      QImage tmp = src.convertToFormat(QImage::Format_RGB888);
      qImageToCvImg(tmp, dst);
  }
}

void qImageToCvImgNoCopy(const QImage& src, cv::Mat& dst) {
  int type = 0;
  switch (src.depth()) {
    case 32:
      type = CV_8UC(4);
      break;
    case 24:
      type = CV_8UC(3);
      break;
    case 8:
      type = CV_8UC(1);
      break;
    default:
      qFatal("unsupported bit depth: %d", src.depth());
  }

  dst = cv::Mat(src.height(), src.width(), type, const_cast<uchar*>(src.constScanLine(0)),
                size_t(src.bytesPerLine()));
}

void cvImgToQImage(const cv::Mat& src, QImage& dst, QImage::Format forceFormat) {
  bool force = forceFormat != QImage::Format_Invalid;
  switch (src.type()) {
    case CV_8UC(3):
      dst = QImage(src.cols, src.rows, force ? forceFormat : QImage::Format_RGB32);
      for (int y = 0; y < src.rows; y++) {
        const uint8_t* sp = reinterpret_cast<const uint8_t*>(src.ptr(y));
        uint8_t* dp = reinterpret_cast<uint8_t*>(dst.scanLine(y));
        for (int x = 0; x < src.cols; x++) {
          dp[0] = sp[0];
          dp[1] = sp[1];
          dp[2] = sp[2];
          dp[3] = 0xff;
          dp += 4;
          sp += 3;
        }
      }
      break;

    case CV_8UC(4):
      dst = QImage(src.cols, src.rows, force ? forceFormat : QImage::Format_ARGB32);
      for (int y = 0; y < src.rows; y++) {
        const uint8_t* sp = reinterpret_cast<const uint8_t*>(src.ptr(y));
        uint8_t* dp = reinterpret_cast<uint8_t*>(dst.scanLine(y));
        memcpy(dp, sp, size_t(4 * src.cols));
      }
      break;

    case CV_8UC(1):
      dst = QImage(src.cols, src.rows, force ? forceFormat : QImage::Format_Grayscale8);
      for (int y = 0; y < src.rows; y++) {
        const uint8_t* sp = reinterpret_cast<const uint8_t*>(src.ptr(y));
        uint8_t* dp = reinterpret_cast<uint8_t*>(dst.scanLine(y));
        memcpy(dp, sp, size_t(src.cols));
      }
      break;

    case CV_16UC(3):
      dst = QImage(src.cols, src.rows, force ? forceFormat : QImage::Format_RGB32);
      for (int y = 0; y < src.rows; y++) {
        const uint16_t* sp = reinterpret_cast<const uint16_t*>(src.ptr(y));
        uint8_t* dp = reinterpret_cast<uint8_t*>(dst.scanLine(y));
        for (int x = 0; x < src.cols; x++) {
          dp[0] = sp[0] >> 8;
          dp[1] = sp[1] >> 8;
          dp[2] = sp[2] >> 8;
          dp[3] = 0xff;
          dp += 4;
          sp += 3;
        }
      }
      break;

    default:
      qFatal("unsupported type: %s", qPrintable(cvMatTypeName(src.type())));
  }
}

void cvImgToQImageNoCopy(const cv::Mat& src, QImage& dst, QImage::Format forceFormat) {
  QImage::Format format = forceFormat;
  if (format == QImage::Format_Invalid) switch (src.type()) {
      case CV_8UC(3):
        format = QImage::Format_RGB888;
        break;

      case CV_8UC(4):
        format = QImage::Format_ARGB32;
        break;

      case CV_8UC(1):
        format = QImage::Format_Grayscale8;
        break;

      default:
        qFatal("unsupported type: %s", qPrintable(cvMatTypeName(src.type())));
    }

  dst = QImage(src.ptr(0), src.cols, src.rows, int(src.step[0]), format);
}

uint64_t dctHash64(const cv::Mat& cvImg) {
  // convert RGB(A) to YUV, extract and work with Y channel
  cv::Mat gray;
  grayscale(cvImg, gray);
  // cv::imwrite("1.gray.png", gray);

  // blur with 7x7 mean filter (all one's) convolution kernel
  // v3, blur small images less
  int kernelSize = 7;
  int area = cvImg.size().area();
  if (area <= 32 * 32)
    kernelSize = 0;
  else if (area <= 64 * 64)
    kernelSize = 3;
  else if (area <= 128 * 128)
    kernelSize = 5;
  else
    kernelSize = 7;

  if (kernelSize) cv::blur(gray, gray, cv::Size(kernelSize, kernelSize));

  // cv::imwrite("2.blur.png", gray);

  // resize to 32x32
  // v2: use INTER_AREA instead of INTER_NEAREST
  cv::resize(gray, gray, cv::Size(32, 32), 0, 0, cv::INTER_AREA);
  // cv::imwrite("3.size.png", gray);

  // 32x32 DCT
  cv::Mat freq;
  gray.convertTo(freq, CV_32F);
  cv::dct(freq, freq);
  // cv::imwrite("4.freq.png", freq);

  // take 8x8 lowest frequencies of DCT, into a 64 element array
  // v4: take 9x9 and discard some lower freqs
  freq = freq.rowRange(cv::Range(0, 9)).colRange(cv::Range(0, 9)).clone();
  // cv::imwrite("4.structure.png", freq);

  freq = freq.reshape(1, 1);
  // cv::imwrite("5.reshape.png", freq);

  // v4: The frequency order is changed using zig-zag traversal,
  // so near frequences appear together, lowest frequencies
  // at the start.
  constexpr char zigZag[] = {0,  9,  1,  2,  10, 18, 27, 19, 11, 3,  4,  12, 20, 28, 36, 45, 37,
                             29, 21, 13, 5,  6,  14, 22, 30, 38, 46, 54, 63, 55, 47, 39, 31, 23,
                             15, 7,  8,  16, 24, 32, 40, 48, 56, 64, 72, 73, 65, 57, 49, 41, 33,
                             25, 17, 26, 34, 42, 50, 58, 66, 74, 75, 67, 59, 51, 43, 35, 44, 52,
                             60, 68, 76, 77, 69, 61, 53, 62, 70, 78, 79, 71, 80};
  Q_STATIC_ASSERT(sizeof(zigZag) == 81);

  //    constexpr char zigZag[64] = {
  //        0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,
  //        56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63
  //    };

  // convert to 64 element vector
  {
    cv::Mat tmp = freq.clone();
    float* dst = reinterpret_cast<float*>(tmp.ptr(0));
    float* src = reinterpret_cast<float*>(freq.ptr(0));
    for (int i = 0; i < 81; i++) dst[i] = src[int(zigZag[i])];

    // remove a few of the lowest frequencies, the theory
    // is that they do not represent much structure or detail,
    // and would be poor for differentiating
    freq = tmp.colRange(6, 70).clone();
  }
  Q_ASSERT(freq.cols == 64);

  // find the threshold for encoding hash
  float thresh;
  {
    // v3: median value including DC; problem is hash distance
    // ends up always being an even number
    // cv::Mat sort;
    // cv::sort(freq, sort, cv::SORT_ASCENDING);
    // float* rowPtr = (float*)sort.ptr(0);
    // thresh = (rowPtr[31]+rowPtr[32]) / 2;

    // v4: use average, solves the even number issue
    float sum = float(cv::sum(freq)[0]);
    thresh = sum / 64;
  }

  // in a 64-bit ulong, for each bit position,
  // set to 1 if the corresponding DCT coef is above the threshold
  uint64_t hash = 0;

  float* row = reinterpret_cast<float*>(freq.ptr(0));
  for (int i = 1; i < 64; i++)
    if (row[i] > thresh) hash |= 1ULL << i;

  return hash;
}

#ifdef ENABLE_LIBPHASH

uint64_t phash64_cimg(const cv::Mat& cvImg) {
  CImg<uint8_t> img;
  cvImgToCImg(cvImg, img);

  uint64_t hash = 0;
  if (img.width() == 32 && img.height() == 32) {
    if (0 < ph_dct_imagehash_cimg32(img, hash)) qCritical("phash64 (32x32) failed");
  } else if (0 < ph_dct_imagehash_cimg(img, hash))
    qCritical("phash64 failed");

  return hash;
}

#endif

uint64_t averageHash64(const cv::Mat& cvImg) {
  cv::Mat gray;
  cv::resize(cvImg, gray, cv::Size(8, 8), 0, 0, cv::INTER_CUBIC);
  grayscale(gray, gray);

  uint8_t mean = uint8_t(cv::mean(gray)[0]);

  uint64_t hash = 0;
  for (int i = 0; i < 64; i++)
    if (gray.at<uint8_t>(i) > mean) hash |= 1ULL << i;

  return hash;
}

void grayLevel(const cv::Mat& src, float clipHistPercent, int& minGray, int& maxGray) {
  const int histSize = 256;

  // to calculate grayscale histogram, color => gray
  cv::Mat gray;
  if (src.type() == CV_8UC1)
    gray = src;
  else if (src.type() == CV_8UC3)
    cvtColor(src, gray, CV_BGR2GRAY);
  else if (src.type() == CV_8UC4)
    cvtColor(src, gray, CV_BGRA2GRAY);
  if (clipHistPercent == 0.0f) {
    // keep full available range
    double min, max;
    cv::minMaxLoc(gray, &min, &max);
    minGray = int(min);
    maxGray = int(max);
  } else {
    cv::Mat hist;  // the grayscale histogram

    float range[] = {0, 256};
    const float* histRange = {range};
    bool uniform = true;
    bool accumulate = false;
    cv::calcHist(&gray, 1, nullptr, cv::Mat(), hist, 1, &histSize, &histRange, uniform, accumulate);

    // calculate cumulative distribution from the histogram
    uint hSize = uint(histSize);
    std::vector<float> accumulator(hSize);
    accumulator[0] = hist.at<float>(0);
    for (uint i = 1; i < uint(histSize); ++i)
      accumulator[i] = accumulator[i - 1] + hist.at<float>(int(i));

    // locate points that cuts at required value
    float max = accumulator.back();
    clipHistPercent *= (max / 100.0f);  // make percent as absolute
    clipHistPercent /= 2.0f;            // left and right wings

    // locate left cut, overflow check added for serenity, never crashed here (yet)
    minGray = 0;
    while (minGray < int(hSize) && accumulator[uint(minGray)] < clipHistPercent)
      minGray++;

    // locate right cut, overflow check is needed, some inputs will segfault
    maxGray = histSize - 1;
    while (maxGray >= 0 && accumulator[uint(maxGray)] >= (max - clipHistPercent))
      maxGray--;
  }
}

void stretchContrast(const cv::Mat& src, cv::Mat& dst, int minGray, int maxGray) {
  const int histSize = 256;

  if (minGray >= maxGray) {  // range could be 0, maybe invalid too
    qWarning() << "no adjustment is possible";
    dst = src;
    return;
  }

  // current range
  float inputRange = maxGray - minGray;

  float alpha = (histSize - 1) / inputRange;  // alpha expands current range to histsize range
  float beta = -minGray * alpha;  // beta shifts current range so that minGray will go to 0

  // Apply brightness and contrast normalization
  // convertTo operates with saurate_cast
  src.convertTo(dst, -1, double(alpha), double(beta));

  // restore alpha channel from source
  // fixme: crashes
  //    if (dst.type() == CV_8UC4)
  //    {
  //        int from_to[] = { 3, 3};
  //        cv::mixChannels(&src, 4, &dst,1, from_to, 1);
  //    }
  return;
}

void brightnessAndContrastAuto(const cv::Mat& src, cv::Mat& dst, float clipHistPercent) {
  Q_ASSERT(clipHistPercent >= 0);
  Q_ASSERT((src.type() == CV_8UC1) || (src.type() == CV_8UC3) || (src.type() == CV_8UC4));

  int minGray = 0, maxGray = 0;

  grayLevel(src, clipHistPercent, minGray, maxGray);
  stretchContrast(src, dst, minGray, maxGray);
}

// Earth Movers Distance (EMD) test
#define USE_EMD 0

#if USE_EMD
static void descriptorToSignature(const ColorDescriptor& cd, cv::Mat& m) {
  m = cv::Mat(cd.numColors, 4, CV_32F);
  for (int i = 0; i < cd.numColors; i++) {
    float* p = (float*)m.ptr(i);
    auto& color = cd.colors[i];
    p[0] = color.w;
    color.get(p[1], p[2], p[3]);
  }
}
#endif

float ColorDescriptor::distance(const ColorDescriptor& a_, const ColorDescriptor& b_) {
  if (a_.numColors == 0 || b_.numColors == 0 || (abs(a_.numColors - b_.numColors) > 2))
    return FLT_MAX;

    // Earth Mover's Distance doesn't work well
    // results seem better by ignoring weight and
    // looking at average distance
#if USE_EMD

  cv::Mat ha, hb;
  descriptorToSignature(a_, ha);
  descriptorToSignature(b_, hb);

  return cv::EMD(ha, hb, CV_DIST_L2);
#else

  // swap a/b if b has more colors
  const ColorDescriptor* a;
  const ColorDescriptor* b;

  if (a_.numColors < b_.numColors) {
    a = &b_;
    b = &a_;
  } else {
    a = &a_;
    b = &b_;
  }

  const int numA = a->numColors;
  const int numB = b->numColors;

  float minDist[NUM_DESC_COLORS];
  //    int minWeight[NUM_DESC_COLORS];

  for (int i = 0; i < numA; i++) {
    minDist[i] = FLT_MAX;

    const DescriptorColor& c1 = a->colors[i];
    //        const int w1 = c1.w;
    float l1, u1, v1;
    c1.get(l1, u1, v1);

    for (int j = 0; j < numB; j++) {
      const DescriptorColor& c2 = b->colors[j];
      //            const int w2 = c2.w;

      float l2, u2, v2;
      c2.get(l2, u2, v2);

      float dl = l1 - l2;
      float du = u1 - u2;
      float dv = v1 - v2;

      float dist = sqrtf(dl * dl + du * du + dv * dv);

      if (dist < minDist[i]) {
        minDist[i] = dist;
        //                minWeight[i] = abs(w1-w2);
      }
    }
  }

  float score = 1;
  for (int i = 0; i < numA; i++) score += minDist[i];  //*minWeight[i];

  return score;
#endif
}

// greys are not colors technically...
// static bool greyFilter(float l, float u, float v)
//{
//    (void)l;
//    return   !(u > 1 && u < -1)
//          && !(v > 1 && v < -1);
//}

// washed out colors aren't useful; also we must
// reject pure black for masking operation
static bool brightFilter(float l, float u, float v) {
  (void)u;
  (void)v;
  return l > 4;  //!(l > 250 || l < 5);
}

// fixme: these are OpenCV 8-bit scaled values...
// static bool skinToneFilter(float l, float u, float v)
//{
//    return !(u >= 100 && u < 158 && v >= 135 && v < 195)
//           && !(l < 10 || l > 245);
//}

static bool histFilter(float l, float u, float v) {
  return brightFilter(l, u, v);
  //    return greyFilter(l,u,v);
  //    return skinToneFilter(l,u,v);
}

QColor DescriptorColor::toQColor() const {
  cv::Mat luv(1, 1, CV_32FC(3));
  float* p = reinterpret_cast<float*>(luv.ptr(0));
  get(p[0], p[1], p[2]);
  cv::cvtColor(luv, luv, CV_Luv2BGR);
  luv *= 255.0;
  return QColor(int(p[2]), int(p[1]), int(p[0]));
}

void ColorDescriptor::create(const cv::Mat& cvImg, ColorDescriptor& desc) {
  // todo: there seems to be some randomness in the descriptor with identical input
  if (cvImg.type() != CV_8UC3 && cvImg.type() != CV_8UC4) {
    qWarning("input is not rgb or rgba");
    return;
  }

  // show what's happening
  bool debug = getenv("DEBUG_COLORDESCRIPTOR") != nullptr;

  // remove alpha channel
  cv::Mat rgb = cvImg;
  if (rgb.type() == CV_8UC4) cv::cvtColor(rgb, rgb, CV_BGRA2BGR);

  // need 3 channels or we crash
  Q_ASSERT(rgb.type() == CV_8UC(3));

  // resize to process faster
  // - keep aspect ratio to avoid distorting weights
  // - use nearest filter to preserve color values
  if (rgb.rows > 256 || rgb.cols > 256) sizeLongestSide(rgb, 256, cv::INTER_NEAREST);

    //
    // generate a mask for dropping the edge colors
    // note: they only drop if filter also drops dark colors
    //
    // in theory the center colors of the image are more important,
    // if we can remove the edge colors or reduce their weight
    // perhaps the histogram is better at finding similar images
    //
#define MASK_EDGES 1
#if MASK_EDGES
  // use an ellipse to remove most of corners and some of the sides
  cv::Mat mask(rgb.rows, rgb.cols, CV_8UC(1));
  mask = mask.setTo(0);
  cv::RotatedRect maskRect({mask.cols * 0.5f, mask.rows * 0.5f},
                           {mask.cols * 0.9f, mask.rows * 0.9f}, 0.0f);
  cv::ellipse(mask, maskRect, 255, CV_FILLED);

  // this only works if pure black is removed
  Q_ASSERT(!histFilter(0, 96, 136));

  if (debug) {
    cv::imshow("mask", mask);
    cv::moveWindow("mask", mask.cols, 0);
  }

  // apply mask
  for (int row = 0; row < rgb.rows; row++) {
    uint8_t* pix = reinterpret_cast<uint8_t*>(rgb.ptr(row));
    uint8_t* m = reinterpret_cast<uint8_t*>(mask.ptr(row));
    for (int col = 0; col < rgb.cols * 3; col += 3) {
      int alpha = *m++;
      pix[col] = (int(pix[col]) * alpha >> 8) & 0xFF;
      pix[col + 1] = (int(pix[col + 1]) * alpha >> 8) & 0xFF;
      pix[col + 2] = (int(pix[col + 2]) * alpha >> 8) & 0xFF;
    }
  }
#endif

  // normalize brightness
  // disabled: the distance function (between colors) would
  // seem to find the closest colors anyways
  // BrightnessAndContrastAuto(img, img, 1);

  // use Luv color space since the perceptual
  // distance between colors is more uniform
  // use floating point LUV since the 8-bit form
  // is transformed and will mess up kmeans
  const int convToRgb = CV_Luv2BGR;
  const int convFromRgb = CV_BGR2Luv;
  cv::Mat luv;
  rgb.convertTo(luv, CV_32FC(3));
  luv *= 1.0 / 255.0;
  cv::cvtColor(luv, luv, convFromRgb);

  Q_ASSERT(luv.type() == CV_32FC(3));

  uint8_t filter[luv.rows][luv.cols];  // 1==keep, 0==discard
  std::vector<cv::Point3f> samples;
  for (int row = 0; row < luv.rows; row++) {
    float* pix = reinterpret_cast<float*>(luv.ptr(row));

    for (int col = 0; col < luv.cols; col++) {
      float l = pix[0];
      float u = pix[1];
      float v = pix[2];
      pix += 3;

      if (histFilter(l, u, v)) {
        filter[row][col] = 1;
        samples.push_back(cv::Point3f(l, u, v));
      } else
        filter[row][col] = 0;
    }
  }

  if (samples.size() < NUM_DESC_COLORS) {
    qWarning("not enough colors");
    return;
  }

  cv::Mat labels;
  cv::Mat centers;
  // uint64_t ts = nanoTime();
  (void)cv::kmeans(samples, NUM_DESC_COLORS, labels,
                   cvTermCriteria(CV_TERMCRIT_ITER | CV_TERMCRIT_EPS, 100, 10), 1,
                   cv::KMEANS_PP_CENTERS, centers);
  // ts = nanoTime()-ts;

  QHash<DescriptorColor::key_t, float> freq;

  float maxDistFromCenter;
  {
    float dx = luv.cols / 2.0f;
    float dy = luv.rows / 2.0f;
    maxDistFromCenter = sqrtf(dx * dx + dy * dy);
  }

  // count pixels in each bucket
#if 0
#  error this is probably broken (drops colors that shifted)
    for (int i = 0; i < labels.rows; i++)
    {
        int label = labels.at<int>(i);

        int l = centers.at<float>(label, 0);
        int u = centers.at<float>(label, 1);
        int v = centers.at<float>(label, 2);

        if (histFilter(l,u,v))
        {
            int color = l << 16 | u << 8 | v;
            freq[color]++;
        }
    }

#else
  // instead of straight count we'll count some
  // more than others based on properties
  int sampleIndex = 0;
  for (int row = 0; row < luv.rows; row++) {
    //        uint8_t *pix = (uint8_t*)luv.ptr(row);

    for (int col = 0; col < luv.cols; col++) {
      //            float l = pix[0];
      //            float u = pix[1];
      //            float v = pix[2];
      //            pix += 3;

      if (filter[row][col]) {
        int label = labels.at<int>(sampleIndex++);

        float l = centers.at<float>(label, 0);
        float u = centers.at<float>(label, 1);
        float v = centers.at<float>(label, 2);

        DescriptorColor d;
        d.set(l, u, v);

        // pack into single value for hashing
        // and later extraction
        auto key = d.key();

        // damp off-center colors
        int dx = col - luv.cols / 2;
        int dy = row - luv.rows / 2;
        float dist = sqrtf(dx * dx + dy * dy);

        // boost off-grey colors
        //                int fromGrey;
        //                float du = fabs(u-96);
        //                float dv = fabs(v-136);
        //                fromGrey = sqrtf(du*du+dv*dv);

        //                //qDebug() << row << col/3 << dist << fromGrey;
        //                (void)fromGrey;

        freq[key] += (maxDistFromCenter - dist) / maxDistFromCenter;
      }
    }
  }
#endif

  // build quantized image also indicating filtered colors
  if (debug) {
    int sampleIndex = 0;
    for (int row = 0; row < luv.rows; row++) {
      float* pix = reinterpret_cast<float*>(luv.ptr(row));

      for (int col = 0; col < luv.cols; col++) {
        float l, u, v;

        if (filter[row][col]) {
          int label = labels.at<int>(sampleIndex++);

          l = centers.at<float>(label, 0);
          u = centers.at<float>(label, 1);
          v = centers.at<float>(label, 2);
        } else {
          l = 50;
          u = 0;
          v = 0;
        }

        pix[0] = l;
        pix[1] = u;
        pix[2] = v;
        pix += 3;
      }
    }
  }

  float maxFreq = 0;
//  float totFreq = 0;
  for (float count : freq.values()) {
    maxFreq = std::max(maxFreq, count);
//    totFreq += count;
  }

  // sort on frequency: in case there are more colors
  // than descriptor will store, drop the lower ones
  auto keys = freq.keys();
  std::sort(keys.begin(), keys.end(), [&freq](DescriptorColor::key_t a, DescriptorColor::key_t b) {
    return freq[a] > freq[b];
  });

  // setup histogram plot
  int x = 0, xDiv = 0;
  cv::Mat graph;
  if (debug) {
    int numColors = keys.count();
    if (numColors == 0) numColors = 1;

    xDiv = 1024 / numColors;
    if (xDiv > 255) xDiv = 255;
    if (xDiv <= 40) xDiv = 40;

    int cols = xDiv * numColors;
    graph = cv::Mat(512 + 100, cols, CV_32FC(3), cv::Scalar(0, 0, 0));

    x = xDiv / 2;
  }

  // format the color descriptor
  desc.clear();
  uint8_t descIndex = 0;

  for (auto key : keys) {
    DescriptorColor d;
    d.setKey(key);

    if (descIndex < NUM_DESC_COLORS) {
      // tuning: which is better?
      // desc.weights[descIndex] = freq[color]*255.0/samples.size(); //
      // proportion of samples desc.weights[descIndex] = freq[color]; //
      // unmodified; might overflow
      d.w = int(freq[key] * DescriptorColor::max() / maxFreq) & 0xFFFF;  // normalized

      desc.colors[descIndex] = d;
      desc.numColors = descIndex;

      if (debug) {
        float fl, fu, fv;
        d.get(fl, fu, fv);

        double l, u, v;
        l = double(fl);
        u = double(fu);
        v = double(fv);

        int val = int(desc.colors[descIndex].w) * 512 / DescriptorColor::max();
        cv::line(graph, cv::Point(x, graph.rows - val), cv::Point(x, graph.rows),
                 cv::Scalar(l, u, v), xDiv, 8, 0);

        cv::putText(graph, qPrintable(QString::number(l)), cv::Point(x - xDiv / 2, 20), 0, 0.5,
                    cv::Scalar(255, 0, 255));
        cv::putText(graph, qPrintable(QString::number(u)), cv::Point(x - xDiv / 2, 34), 0, 0.5,
                    cv::Scalar(255, 0, 255));
        cv::putText(graph, qPrintable(QString::number(v)), cv::Point(x - xDiv / 2, 48), 0, 0.5,
                    cv::Scalar(255, 0, 255));
        cv::putText(graph, qPrintable(QString::number(val)), cv::Point(x - xDiv / 2, 60), 0, 0.5,
                    cv::Scalar(255, 0, 255));

        x += xDiv;
      }

      descIndex++;
    }
  }

  if (debug) {
    cv::cvtColor(luv, rgb, convToRgb);
    cv::cvtColor(graph, graph, convToRgb);

    cv::namedWindow("quantized", CV_WINDOW_AUTOSIZE);
    cv::moveWindow("quantized", 0, 0);
    cv::imshow("quantized", rgb);
    cv::waitKey(0);

    cv::namedWindow("colors", CV_WINDOW_AUTOSIZE);
    cv::moveWindow("colors", 0, rgb.rows);
    cv::imshow("colors", graph);
    cv::waitKey(0);
  }
}

/*
uint64_t histogram64(const cv::Mat& cvImg)
{
    // note: this doesn't work worth s**t
    cv::Mat img = cvImg;

    cv::resize(img, img, cv::Size(128,128), 0, 0, cv::INTER_AREA);
    BrightnessAndContrastAuto(img, img, 1);
    cv::cvtColor(img, img, CV_BGR2HSV);

    vector<cv::Mat> planes;
    cv::split(img, planes);
    assert(planes.size()!=2);

    if (planes.size()==1)
    {
        planes.push_back(planes[0]);
        planes.push_back(planes[0]);
    }


    int size = 32;           // number of histogram buckets
    float range[] = {0,256}; // range of color channels
    const float *ranges = { range };

    bool uniform=true;
    bool accumulate=false;

    const int planeOffset = 0;
    const int numChannels = 3;
    cv::Mat hh[numChannels];

    for (int i = 0; i < numChannels; i++)
        cv::calcHist( &planes[i+planeOffset], 1, 0, cv::Mat(), hh[i], 1, &size,
&ranges, uniform, accumulate);

    assert(hh[0].type() == CV_32F);

    cv::Mat graph(32, 32, CV_8UC(1), cv::Scalar(0,0,0));

    int height=graph.rows/numChannels;
    int width=graph.cols/size;

//    int minVal = 255 * 0.95;
//    int maxVal = 255;
//    float mult = maxVal / log(maxVal-minVal);
//

    for (int i = 0; i < numChannels; i++)
        cv::normalize(hh[i], hh[i], 0, 1, cv::NORM_MINMAX, -1, cv::Mat() );


    for (int channel=0; channel < numChannels; channel++)
    {
        int y = channel*height;
//
//        cv::Mat meanMat, stdDevMat;
//
//        cv::meanStdDev(hh[channel], meanMat, stdDevMat);
//
//        float thresh = meanMat.at<float>(0) + stdDevMat.at<float>(0);

//        float mean = cv::mean(hh[channel]).val[0];

        for (int i = 0; i < size; i++)
        {
            int x = i*width + width/2;
            float val = hh[channel].at<float>(i);

//
//            if (val < mean)
//                continue;

            int intensity = 0;
            //if (val > 0.90)
            //    intensity = 255;
//
//            if (val > 0.95)
//                intensity = val*255;

            intensity = val*255;

            // local maxima of histogram
//            float next = 0;
//            float prev = 0;
//
//            if (i > 0)
//                prev = hh[channel].at<float>(i-1);
//
//            if (i < size-1)
//                next = hh[channel].at<float>(i+1);
//
//            if (val > mean && val > prev && val > next)
//                intensity = 255;

//            int intensity = mult * log(255*val - minVal);
//            intensity = std::max(0, intensity);

            cv::line(graph,
                cv::Point(x, y+height - height*val),
                cv::Point(x, y+height),
                cv::Scalar(intensity), width, 8, 0);
        }
    }

//    cv::namedWindow("calcHist Demo", CV_WINDOW_AUTOSIZE);
//    cv::imshow("calcHist Demo", graph);
//    cv::waitKey(0);

    return phash64(graph);
}
*/

bool compare(const cv::Mat& a, const cv::Mat& b) {
  if (a.depth() != b.depth() || a.channels() != b.channels()) {
    qDebug() << "fail: depth or channels";
    return false;
  }
  if (a.rows != b.rows || a.cols != b.cols) {
    qDebug() << "fail: dimensions";
    return false;
  }

  // cannot pass empty array through countNonZero()
  if (a.empty() || b.empty()) {
    if (a.empty() && b.empty()) return true;

    qDebug()  << "fail: a or b is empty";
    return false;
  }

  if (a.channels() > 1) {
    std::vector<cv::Mat> planesA, planesB;

    cv::split(a, planesA);
    cv::split(b, planesB);

    Q_ASSERT(planesA[0].channels() == 1);
    Q_ASSERT(planesA.size() == planesB.size());

    for (size_t i = 0; i < planesA.size(); i++)
      if (0 != cv::countNonZero(planesA[i] != planesB[i])) {
        qDebug() << "fail: plane" << i;
        return false;
      }
    return true;
  } else if (0 != cv::countNonZero(a != b)) {
    qDebug() << "fail: grayscale plane";
    return false;
  }
  return true;
}

QString cvMatTypeName(int type) {
  int depth = type % 8;

  const char* names[8] = {"CV_8UC",  "CV_8SC",  "CV_16UC", "CV_16SC",
                          "CV_32SC", "CV_32FC", "CV_64FC", "CV_INVALID"};

  int channels = type / 8 + 1;

  return QString("%1(%2)").arg(names[depth]).arg(channels);
}

void grayscale(const cv::Mat& input, cv::Mat& output) {
  int type = input.type();
  switch (type) {
    case CV_8UC(3):
    case CV_16UC(3):
      cv::cvtColor(input, output, CV_BGR2GRAY);
      break;
    case CV_8UC(4):
      cv::cvtColor(input, output, CV_BGRA2GRAY);
      break;
    case CV_8UC(1):
      output = input;
      break;
    default:
      qFatal("unsupported cvImage type for grayscale conversion: %s",
             qPrintable(cvMatTypeName(type)));
  }
}

void autocrop(cv::Mat& cvImg, int range) {
  cv::Mat img;
  grayscale(cvImg, img);
  Q_ASSERT(img.channels() == 1);

  if (img.rows == 0 || img.cols == 0) return;

  // color of the border
  uint8_t color = img.ptr<uint8_t>(0)[0];

  // pixels required to consider a row or column
  // to be part of the letterbox
  // it isn't 100% in case there is other content
  // in letterbox region like subtitles
  int minWidthCovered = int(img.cols * 0.66f);
  int minHeightCovered = int(img.rows * 0.66f);

  // maximum off-center of letterbox
  // a crop only occurs if it is balanced on both sides
  int maxHMarginDifference = int(img.cols * 0.05f);
  int maxVMarginDifference = int(img.rows * 0.05f);

  // from the center, go out to the edge, looking for where the letterbox starts
  // repeat for each edge of the letterbox
  int top;
  for (top = img.rows / 2; top >= 0; top--) {
    const uint8_t* pixels = img.ptr<uint8_t>(top);

    // find the left and right edge at the current scanline
    int left, right;

    for (left = 0; left < img.cols; left++)
      if (abs(pixels[left] - color) > range) break;

    for (right = img.cols - 1; right >= 0; right--)
      if (abs(pixels[right] - color) > range) break;

    right++;

    // if there is a continuous line from both the left and right side,
    // and the total length is enough, we found the edge of the letterbox
    // we could also check that the left/right are roughly equal, however
    // if a watermark/subtitle overlaps the letterbox this would not work.
    if (left > 0 && right < img.cols && left + img.cols - right > minWidthCovered) break;
  }
  top++;

  int bottom;
  for (bottom = img.rows / 2 + 1; bottom < img.rows; bottom++) {
    const uint8_t* pixels = img.ptr<uint8_t>(bottom);

    int left, right;

    for (left = 0; left < img.cols; left++)
      if (abs(pixels[left] - color) > range) break;

    for (right = img.cols - 1; right >= 0; right--)
      if (abs(pixels[right] - color) > range) break;

    right++;

    if (left + img.cols - right > minWidthCovered) break;
  }

  int left;
  for (left = img.cols / 2; left >= 0; left--) {
    int top, bottom;
    for (top = 0; top < img.rows; top++)
      if (abs(img.at<uint8_t>(top, left) - color) > range) break;

    for (bottom = img.rows - 1; bottom >= 0; bottom--)
      if (abs(img.at<uint8_t>(bottom, left) - color) > range) break;

    bottom++;

    if (top > 0 && bottom < img.rows && top + img.rows - bottom > minHeightCovered) break;
  }
  left++;

  int right;
  for (right = img.cols / 2 + 1; right < img.cols; right++) {
    int top, bottom;
    for (top = 0; top < img.rows; top++)
      if (abs(img.at<uint8_t>(top, right) - color) > range) break;

    for (bottom = img.rows - 1; bottom >= 0; bottom--)
      if (abs(img.at<uint8_t>(bottom, right) - color) > range) break;

    bottom++;

    if (top > 0 && bottom < img.rows && top + img.rows - bottom > minHeightCovered) break;
  }

  // assuming a centered letterbox, if the crop is slightly off center,
  // make it centered using the lesser margin
  int bmargin = img.rows - bottom;
  if (abs(top - bmargin) > maxVMarginDifference) {
    if (top > bmargin)
      top = bmargin;
    else
      bottom = img.rows - top;
  }

  int rmargin = img.cols - right;
  if (abs(left - rmargin) > maxHMarginDifference) {
    if (left > rmargin)
      left = rmargin;
    else
      right = img.cols - left;
  }

  if ((left != 0 && right != img.cols) || (top != 0 && bottom != img.rows))
    if (left < right && top < bottom &&              // valid ranges
        (right - left) / float(img.cols) > 0.65f &&  // sanity check we didn't crop away too much
        (bottom - top) / float(img.rows) > 0.65f)
      cvImg = cvImg.colRange(left, right).rowRange(top, bottom);
}

void demosaic(const cv::Mat& cvImg, QVector<QRect>& rects) {
  cv::Mat img;
  grayscale(cvImg, img);
  Q_ASSERT(img.channels() == 1);

  if (img.rows == 0 || img.cols == 0) return;

  // assume mosaic uses a flat background or small gradient
  // images are arranged in a grid with some background between them
  // fixme: this doesn't work on grids with no background between thumbnails

  const int brightThreshold = 30;      // max gray level distance between pixels on a line
  const float lengthThreshold = 0.9f;  // fraction of contiguous pixels needed to make a line

  // look for horizontal lines spanning the image

  QVector<int> hLineIn;   // top side of rectangles
  QVector<int> hLineOut;  // one-past bottom side (so out-in == height)
  int lastLine = 0;
  int lastNonLine = 0;
  int y;
  for (y = 0; y < img.rows; y++) {
    // middle-out search so it isn't sensitive to borders
    const uint8_t* pixels = img.ptr<uint8_t>(y);
    int right = img.cols / 2;
    pixels += right;
    for (; right < img.cols - 1; right++, pixels++)
      if (abs(int(pixels[0]) - int(pixels[1])) > brightThreshold) break;

    int left = img.cols / 2 - 1;
    pixels = img.ptr<uint8_t>(y);
    pixels += left;
    for (; left >= 0; left--, pixels--)
      if (abs(int(pixels[0]) - int(pixels[1])) > brightThreshold) break;

    if (right - left >= img.cols * lengthThreshold) {
      lastLine = y;
      if (hLineIn.count() && y - lastNonLine == 1) {
        qInfo() << "hline out @ y=" << y;
        // cv::line(cvImg, cv::Point(0, y), cv::Point(img.cols-1, y), {0,255,0},
        // 1);
        hLineOut.append(y);
      }
    } else {
      lastNonLine = y;
      if (y - lastLine == 1) {
        qInfo() << "hline in @ y=" << y;
        // cv::line(cvImg, cv::Point(0, y), cv::Point(img.cols-1, y), {0,0,255},
        // 1);
        hLineIn.append(y);
      }
    }
  }

  // look for vertical lines
  QVector<int> vLineIn;   // left side of rectangles
  QVector<int> vLineOut;  // one-past right side (so out-In == width)
  lastLine = 0;
  lastNonLine = 0;
  for (int x = 0; x < img.cols; x++) {
    // middle-out search so it isn't sensitive to borders
    int bot = img.rows / 2;
    for (; bot < img.rows - 1; bot++)
      if (abs(int(img.at<uint8_t>(bot, x)) - int(img.at<uint8_t>(bot + 1, x))) > brightThreshold)
        break;

    int top = img.rows / 2 - 1;
    for (; top >= 0; top--)
      if (abs(int(img.at<uint8_t>(top, x)) - int(img.at<uint8_t>(top + 1, x))) > brightThreshold)
        break;

    if (bot - top >= img.rows * lengthThreshold) {
      lastLine = x;
      if (vLineIn.count() && x - lastNonLine == 1) {
        qInfo() << "vline end @ x=" << x;
        // cv::line(cvImg, cv::Point(x, 0), cv::Point(x, img.rows-1), {255,0,0},
        // 1);
        vLineOut.append(x);
      }
    } else {
      lastNonLine = x;
      if (x - lastLine == 1) {
        qInfo() << "vline start @ x=" << x;
        // cv::line(cvImg, cv::Point(x, 0), cv::Point(x, img.rows-1), {0,255,0},
        // 1);
        vLineIn.append(x);
      }
    }
  }

  if (hLineIn.count() == 0 || hLineIn.count() != hLineOut.count() || vLineIn.count() == 0 ||
      vLineIn.count() != vLineOut.count()) {
    qWarning() << "failed to detect a grid";
    return;
  }

  for (int h = 0; h < hLineIn.count(); h++)
    for (int v = 0; v < vLineIn.count(); v++) {
      int y0 = hLineIn[h];
      int y1 = hLineOut[h];
      int x0 = vLineIn[v];
      int x1 = vLineOut[v];
      int width = x1 - x0;
      int height = y1 - y0;
      float aspect = float(width) / height;
      if (aspect > 0.9f && aspect < 2.0f) {
        // cv::rectangle(cvImg, cv::Point(x0, y0), cv::Point(x1,y1), {255,0,0},
        // CV_FILLED);
        rects.append(QRect(x0, y0, width, height));
      }
    }
}

/*
void autocrop(cv::Mat& cvImg, int range)
{
    cv::Mat img;
    grayscale(cvImg, img);
    Q_ASSERT(img.channels()==1);

    if (img.rows==0 || img.cols==0)
        return;


    int right = 0;
    int left = img.cols;
    int top = 0;
    int bottom = 0;

    uint8_t color = img.ptr<uint8_t>(0)[0];

    for (int y = 0; y < img.rows; y++)
    {
        uint8_t* pixels = img.ptr<uint8_t>(y);

        // left margin is the minimum left x coordinate
        int x;
        for (x = 0; x < img.cols; x++)
            if (abs(pixels[x] - color) >= range)
            {
                left = min(x, left);
                break;
            }

        // top margin is one past the last sequential full row
        // bottom margin is one past the last not full row
        if (x == img.cols)
        {
            if (top == y)
                top++;
        }
        else
            bottom=y+1;

        // right margin
        for (int x = img.cols-1; x > 0; x--)
            if (abs(pixels[x] - color) >= range)
            {
                right = max(x+1, right);
                break;
            }
    }

    printf ("crop: left=%d right=%d top=%d bottom=%d\n", left, right, top,
bottom);

    // only crop if each side has something to crop
    // todo: de-letterbox flag also check that amount off of each side
    // is approximately the same
    if ( (left != 0 && right != img.cols) ||
         (top != 0 && bottom != img.rows))
        if (left < right && top < bottom) // range check
            cvImg = cvImg.colRange(left, right).rowRange(top, bottom);
}
 */

QStringList cvVersion() {
  QStringList list;
  QString runtime = "???";
  QString build = cv::getBuildInformation().c_str();
  QRegularExpression re("OpenCV (\\d+\\.\\d+\\.\\d+\\.\\d+)", QRegularExpression::MultilineOption);
  auto match = re.match(build);
  if (match.isValid()) runtime = match.captured(1);
  list << runtime;
  list << CV_VERSION;
  return list;
}

CVErrorLogger::CVErrorLogger(const QString& context) : _context(context) {
  cvRedirectError(log);

  QThread* thread = QThread::currentThread();
  QMutexLocker locker(&mutex());

  if (map().contains(thread)) qCritical("Nesting CVErrorLogger will lose context");

  map().insert(thread, this);
}

CVErrorLogger::~CVErrorLogger() {
  // we don't remove the logger since we don't know if other
  // threads are using it.
  // cvRedirectError(cvStdErrReport);
  QMutexLocker locker(&mutex());
  map().remove(QThread::currentThread());
}

QMutex& CVErrorLogger::mutex() {
  static QMutex _mutex;
  return _mutex;
}

QHash<QThread*, CVErrorLogger*>& CVErrorLogger::map() {
  static QHash<QThread*, CVErrorLogger*> _map;
  return _map;
}

int CVErrorLogger::log(int status, const char* func_name, const char* err_msg,
                       const char* file_name, int line, void* userData) {
  (void)userData;
  CVErrorLogger* self = nullptr;
  {
    QMutexLocker locker(&mutex());
    auto it = map().find(QThread::currentThread());
    if (it != map().end()) self = *it;
  }
  QString context = "<no context>";
  if (self) context = self->_context;

  const QString file = QString(file_name).split("/").last();
  const QString msg = QString("%1: %2 %3 at %4:%5 in %6()")
                          .arg(context)
                          .arg(status)
                          .arg(err_msg)
                          .arg(file)
                          .arg(line)
                          .arg(func_name);
  qCritical() << msg;
  return 0;
}

void sizeLongestSide(cv::Mat& img, int size, int filter) {
  int w, h;
  float aspect = float(img.size().width) / img.size().height;
  if (img.size().width > img.size().height) {
    w = size;
    h = int(w / aspect);
  } else {
    h = size;
    w = int(aspect * h);
  }

  // cv::resize will otherwise throw a more obscure message
  if (w == 0 || h == 0)
    throw std::invalid_argument(
        "sizeLongestSide: computed width or height is 0, probably bad input");

  cv::resize(img, img, cv::Size(w, h), 0, 0, filter);
}

void sizeScaleFactor(cv::Mat& img, float factor) {
  int w = int(img.size().width * factor);
  int h = int(img.size().height * factor);

  cv::resize(img, img, cv::Size(w, h), 0, 0, cv::INTER_LANCZOS4);
}

void sizeStretch(cv::Mat& img, int w, int h) {
  cv::resize(img, img, cv::Size(w, h), 0, 0, cv::INTER_LANCZOS4);
}
