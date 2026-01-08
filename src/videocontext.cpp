/* Video decoding and metadata
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
#include "videocontext.h"
#include "media.h"  // setAttribute()
#include "qtutil.h" // message context

#include "opencv2/core.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
};

#include <unistd.h>  // getcwd

#include <QtCore/QFileInfo>
#include <QtCore/QFuture>
#include <QtCore/QLoggingCategory>
#include <QtCore/QMessageLogger>
#include <QtCore/QMutexLocker>
#include <QtCore/QThreadStorage>

#define AV_CRITICAL(x) qCritical() << x << Qt::hex << err << avErrorString(err)
#define AV_WARNING(x) qWarning() << x
#define AV_DEBUG(x) qDebug() << x

static QString avErrorString(int err) {
  char str[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_make_error_string(str, sizeof(str), err);
  return str;
}

static void avImgToQImg(uint8_t* planes[4], int linesizes[4], int width, int height, QImage& dst,
                        AVPixelFormat fmt = AV_PIX_FMT_YUV420P) {
  const QSize size(width, height);
  const uchar* const data = planes[0];
  int skip = linesizes[0];

  if (fmt != AV_PIX_FMT_BGR24) {
    QImage::Format format = QImage::Format_Grayscale8;

    if (dst.size() != size || dst.format() != format) dst = QImage(size, format);

    for (int y = 0; y < height; y++) {
      uchar* dstLine = dst.scanLine(y);
      for (int x = 0; x < width; x++) {
        *dstLine = *(data + y * skip + x);
        dstLine++;
      }
    }
  } else {
    QImage::Format format = QImage::QImage::Format_RGB888;

    if (dst.size() != size || dst.format() != format) dst = QImage(size, format);

    for (int y = 0; y < height; y++) {
      uchar* dstLine = dst.scanLine(y);

      for (int x = 0; x < width; x++) {
        const uchar* const pixel = data + y * skip + x * 3;
        dstLine[0] = pixel[2];
        dstLine[1] = pixel[1];
        dstLine[2] = pixel[0];
        dstLine += 3;
      }
    }
  }
}

/*
static void avImgToQImgNoCopy(uint8_t *planes[4], int linesizes[4], int width,
                        int height, QImage& dst, AVPixelFormat fmt = AV_PIX_FMT_YUV420P) {

  const QSize size(width, height);
  const uchar* const data = planes[0];
  int skip = linesizes[0];

  if (fmt != AV_PIX_FMT_RGB24) {
    Q_ASSERT(0);
  } else {
    QImage::Format format = QImage::QImage::Format_RGB888;
    dst = QImage(data, width, height, skip, format);
  }
}
*/

static void avImgToCvImg(uint8_t* planes[4], int linesizes[4], int width, int height, cv::Mat& dst,
                         AVPixelFormat fmt = AV_PIX_FMT_YUV420P) {
  const QSize size(width, height);
  const uchar* const data = planes[0];
  int skip = linesizes[0];

  if (fmt != AV_PIX_FMT_BGR24) {
    if (dst.rows != height || dst.cols != width || dst.type() != CV_8UC(1)) {
      qDebug() << "allocating cvImg";
      dst = cv::Mat(height, width, CV_8UC(1));
    }

    for (int y = 0; y < height; y++) {
      uchar* dstLine = dst.ptr(y);
      memcpy(dstLine, data + y * skip, size_t(width));
    }
  } else {
    if (dst.rows != height || dst.cols != width || dst.type() != CV_8UC(3))
      dst = cv::Mat(height, width, CV_8UC(3));

    for (int y = 0; y < height; y++) {
      uchar* dstLine = dst.ptr(y);
      memcpy(dstLine, data + y * skip, size_t(width * 3));
    }
  }
}

static void avFrameToQImg(const AVFrame& frame, QImage& dst) {
  int w = frame.width, h = frame.height, skip = frame.linesize[0];
  const uchar* const data = frame.data[0];

  const QSize size(frame.width, frame.height);

  QImage::Format format = QImage::Format_Grayscale8;

  if (dst.size() != size || dst.format() != format) dst = QImage(size, format);

  for (int y = 0; y < h; y++) {
    uchar* dstLine = dst.scanLine(y);
    for (int x = 0; x < w; x++) {
      *dstLine = *(data + y * skip + x);
      dstLine++;
    }
  }
}

static void avFrameToCvImg(const AVFrame& frame, cv::Mat& dst) {
  int w = frame.width, h = frame.height, skip = frame.linesize[0];
  const uchar* const data = frame.data[0];

  if (dst.rows != h || dst.cols != w || dst.type() != CV_8UC(1)) {
    qDebug() << "allocating cvImg";
    dst = cv::Mat(h, w, CV_8UC(1));
  }

  for (int y = 0; y < h; y++) {
    uchar* dstLine = dst.ptr(y);
    for (int x = 0; x < w; x++) {
      *dstLine = *(data + y * skip + x);
      dstLine++;
    }
  }
}

static void FFmpeg(void* ptr, int level, const char* fmt, va_list vl) {
  if (level > av_log_get_level()) return;

  char buf[1024] = {0};
  vsnprintf(buf, sizeof(buf) - 1, fmt, vl);
  const QByteArray bytes = QByteArray(buf).trimmed();
  const char* msg = bytes.data();

  // this warning comes from cuviddec and qsvdec, I don't think
  // it has any consequence. the source is mp4 files with packet.timebase=0/1
  // which seems to be pretty common
  if (bytes.startsWith("Invalid pkt_timebase")) return;

  QString msgContext;

  // use the current context if there is one
  auto& threadCtx = qMessageContext();
  if (threadCtx.hasLocalData()) msgContext = threadCtx.localData();

  if (msgContext.isEmpty()) msgContext = "unknown file";

  // note a valid pointer will have its first member point to AVClass;
  // there are some cases in libav where the ptr is valid but the
  // class is null; av_default_item_name() will segfault in these cases
  if (ptr && ((AVClass**) ptr)[0] != 0) {
    const QLatin1StringView avClassName(av_default_item_name(ptr));
    if (avClassName == "AVFormatContext") {
      auto ctx = (AVFormatContext*) ptr;
      if (ctx->opaque) msgContext = (const char*) ctx->opaque;
      msgContext += lc('|');
      msgContext += ctx->iformat ? ctx->iformat->name : "format";
    } else if (avClassName == "AVCodecContext") {
      auto ctx = (AVCodecContext*) ptr;
      if (ctx->opaque) msgContext = (const char*) ctx->opaque;
      msgContext += lc('|');
      msgContext += ctx->codec_descriptor ? ctx->codec_descriptor->name : "codec";
    } else if (avClassName == "AVFilterGraph") {
      auto ctx = (AVFilterGraph*) ptr;
      if (ctx->opaque) msgContext = (const char*) ctx->opaque;
      msgContext += "|graph";
    } else if (avClassName == "SwsContext") {
      // libswscale > release/7.1
      // auto ctx = (SwsContext*) ptr;
      // if (ctx->opaque) msgContext = (const char*) ctx->opaque;
      msgContext += "|sws";
    } else if (avClassName == "AVFilter") {
      // auto filter = (AVFilter*) ptr;
      // if (filter->name)
      // msgContext += filter->name; // TODO: prints nonsense
      // else
      msgContext += "|filter";
    } else {
      msgContext += lc('|');
      msgContext += avClassName;
    }
  }

  MessageContext context(msgContext);

  if (level <= AV_LOG_ERROR) VideoContext::avLoggerWriteLogLine(msgContext, msg);

  //     if (level >= AV_LOG_TRACE) ;
  // else if (level >= AV_LOG_DEBUG) ;
  static const QLoggingCategory category("FFmpeg");
  if (level >= AV_LOG_VERBOSE)
    qCDebug(category) << msg;
  else if (level >= AV_LOG_INFO)
    qCInfo(category) << msg;
  else if (level >= AV_LOG_WARNING)
    qCWarning(category) << msg;
  // else if (level >= AV_LOG_ERROR);
  else if (level >= AV_LOG_FATAL)
    qCCritical(category) << msg;
  else if (level >= AV_LOG_PANIC)
    qFatal("%s", msg);
}

void VideoContext::loadLibrary() {
  av_log_set_callback(&FFmpeg);
}

QStringList VideoContext::ffVersions() {
  QStringList list;
  const char* compiled = AV_STRINGIFY(LIBAVUTIL_VERSION);

  unsigned int packed = avutil_version();
  QString runtime = QString("%1.%2.%3")
                        .arg(AV_VERSION_MAJOR(packed))
                        .arg(AV_VERSION_MINOR(packed))
                        .arg(AV_VERSION_MICRO(packed));

  list << runtime << compiled;
  return list;
}

void VideoContext::listFormats() {

  qWarning("listing FFmpeg configuration, not necessarily available for indexing (see -about)");

  void* opaque = nullptr;
  const AVInputFormat* fmt;

  qInfo("----------------------------------------");
  qInfo("Name \"Description\" (Known Extensions)");
  qInfo("----------------------------------------");
  QVector<QPair<QString, QString>> formats;
  while (nullptr != (fmt = av_demuxer_iterate(&opaque))) {
    QString desc = QString().asprintf("%-10s \"%s\" (%s)", fmt->name, fmt->long_name,
                                      fmt->extensions);
    formats.append({fmt->name, desc});
  }
  std::sort(formats.begin(), formats.end(), [](auto& a, auto& b) { return a.first < b.first; });

  for (auto& format : std::as_const(formats))
    qInfo().noquote() << format.second;
}

void VideoContext::listCodecs() {
  qWarning("listing FFmpeg video decoders, not necessarily available for indexing");

  void* opaque = nullptr;
  const AVCodec* codec;
  qInfo("------------------------------");
  qInfo("Threads Type Name Description");
  qInfo("------------------------------");
  QVector<QPair<QString, QString>> codecs;
  while (nullptr != (codec = av_codec_iterate(&opaque))) {
    if (codec->type != AVMEDIA_TYPE_VIDEO) continue;
    if (!av_codec_is_decoder(codec)) continue;
    QString desc = QString().asprintf("%3s %3s %-20s %s",
                                      codec->capabilities
                                              & (AV_CODEC_CAP_SLICE_THREADS
                                                 | AV_CODEC_CAP_FRAME_THREADS
                                                 | AV_CODEC_CAP_OTHER_THREADS)
                                          ? "mt"
                                          : "st",
                                      codec->capabilities
                                              & (AV_CODEC_CAP_HARDWARE | AV_CODEC_CAP_HYBRID)
                                          ? "hw"
                                          : "sw",
                                      codec->name, codec->long_name);
    codecs.append({codec->name, desc});
  }

  std::sort(codecs.begin(), codecs.end(), [](auto& a, auto& b) { return a.first < b.first; });

  for (auto& codec : std::as_const(codecs))
    qInfo().noquote() << codec.second;
}

class VideoContextPrivate {
  Q_DISABLE_COPY_MOVE(VideoContextPrivate)

 public:
  VideoContextPrivate(){};

  AVFormatContext* format = nullptr;
  AVStream* videoStream = nullptr;
  const AVCodec* codec = nullptr;
  AVCodecContext* context = nullptr;

  bool hwFilter = false;                    // true if hwaccel uses a scale filter
  const uint8_t* hwFramesContext = nullptr; // frames context used by filter graph
  AVFilterGraph* filterGraph = nullptr;
  AVFilterContext* filterSource = nullptr;
  AVFilterContext* filterSink = nullptr;
  AVFrame* filterFrame = nullptr;          // result of filter
  AVFrame* transferFrame = nullptr;        // hw download frame
  AVFrame* frame = nullptr;                // frame we decode into (could be hardware frame)
  AVPacket packet;
  float sar = -1.0;

  SwsContext* scaler = nullptr;
  struct {
    uint8_t* data[4] = {nullptr};
    int linesize[4] = {0};
  } scaled;
};

QImage VideoContext::frameGrab(const QString& path, int frame, bool fastSeek,
                               const VideoContext::DecodeOptions& options, QFuture<void>* future) {
  QImage img;

  // note, hardware decoder is much slower to open, not worthwhile here
  // TODO: system configuration for grab location
  VideoContext video;
  if (0 != video.open(path, options)) return img;
  if (future && future->isCanceled()) return img;

  const auto md = video.metadata();
  const int maxFrame = int(md.frameRate * float(md.duration));
  if (frame >= maxFrame) {
    qWarning() << path << ": seek frame out of range :" << frame << ", using auto";
    frame = -1;
  }
  if (frame < 0) {
    if (md.duration > 60)
      frame = int(60 * md.frameRate);
    else
      frame = int(float(md.duration) * md.frameRate * 0.10f);
  }

  bool ok = false;
  if (fastSeek)
    ok = video.seekFast(frame);
  else
    ok = video.seek(frame);

  if (future && future->isCanceled()) return img;

  if (ok) video.nextFrame(img);

  float par = video.pixelAspectRatio();
  if (par > 0.0 && par != 1.0) {
    int w = par * img.width();
    img = img.scaled(w, img.height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }
  return img;
}

VideoContext::VideoContext() {
  _p = new VideoContextPrivate;
}

VideoContext::~VideoContext() {
  if (_p->context) close();
  delete _p;
}

bool VideoContext::initFilters(const char* filters) {
  if (_p->filterGraph || _p->filterFrame || _p->filterSource || _p->filterSink) {
    qCritical("filter graph wasn't cleaned up correctly");
    return false;
  }

  // stuff we need to cleanup before returning
  struct ScopedPointers {
    AVFilterInOut* outputs = avfilter_inout_alloc(); // always free on return
    AVFilterInOut* inputs = avfilter_inout_alloc();
    AVFilterGraph* graph = avfilter_graph_alloc();   // free only if error
    AVFilterContext* source = nullptr;               // do not free as always tied to graph
    AVFilterContext* sink = nullptr;
    bool success = false; // set to true and copy retained pointers on success
    ScopedPointers() { qDebug() << this; }
    ~ScopedPointers() {
      if (!success) {
        avfilter_graph_free(&graph);
      }
      avfilter_inout_free(&inputs);
      avfilter_inout_free(&outputs);
    }
  } s;

  s.graph->opaque = (void*) logContext();

  // we cannot use avfilter_graph_create_filter() as we might need to set hw_frames_ctx
  s.source = avfilter_graph_alloc_filter(s.graph, avfilter_get_by_name("buffer"), "in");
  if (!s.source) {
    qCritical("alloc filter failed");
    return false;
  }

  // hw_frames_ctx is obtained by decoding the first frame
  if (_p->context->hw_device_ctx && !_p->context->hw_frames_ctx) {
    qCritical("cannot setup hardware avfilter without hw_frames_ctx");
    return false;
  }

  auto* params = av_buffersrc_parameters_alloc();
  params->color_range = _p->context->color_range;
  params->color_space = _p->context->colorspace;
  params->time_base = _p->videoStream->time_base; // _p->context->time_base is always 0/1
  params->width = _p->context->width;
  params->height = _p->context->height;
  params->format = _p->context->pix_fmt;
  params->sample_aspect_ratio = _p->context->sample_aspect_ratio;

  if (_p->context->hw_frames_ctx) {
#if 1
    auto* hwctx = (AVHWFramesContext*) _p->context->hw_frames_ctx->data;
    qDebug() << "<MAG>configuring hw avfilter:" << av_get_pix_fmt_name(hwctx->format);
    params->format = hwctx->format;
    params->hw_frames_ctx = _p->context->hw_frames_ctx;
#else
    // this works, but has issues since frames are not shared with decoder,
    // we cannot use hwdownload without another hwfilter ahead of it
    auto* hwctx = (AVHWFramesContext*) _p->context->hw_frames_ctx->data;

    AVBufferRef* frameCtx = av_hwframe_ctx_alloc(_p->context->hw_device_ctx);
    AVHWFramesContext* c = (AVHWFramesContext*) frameCtx->data;
    c->format = hwctx->format;
    c->sw_format = hwctx->sw_format;
    c->width = _p->context->width;
    c->height = _p->context->height;
    //c->initial_pool_size = 2 + _p->context->extra_hw_frames;
    int err = av_hwframe_ctx_init(frameCtx);
    if (err < 0) {
      AV_CRITICAL("hwframe_ctx_init");
      return false;
    }
    params->format = c->format;
    params->hw_frames_ctx = frameCtx;
#endif
  }

  int err;
  if ((err = av_buffersrc_parameters_set(s.source, params)) < 0) {
    AV_CRITICAL("buffersrc params");
    av_free(params);
    return false;
  }
  av_free(params);

  if ((err = avfilter_init_dict(s.source, NULL)) < 0) {
    AV_CRITICAL("buffersrc init");
    return false;
  }

  err = avfilter_graph_create_filter(&s.sink, avfilter_get_by_name("buffersink"), "out", NULL, NULL,
                                     s.graph);
  if (err < 0) {
    AV_CRITICAL("create buffer sink");
    return false;
  }

  /* this was in example but now says deprecated
  enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_NV12, AV_PIX_FMT_NONE};
  err = av_opt_set_int_list(_p->filterSinkCtx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE,
                            AV_OPT_SEARCH_CHILDREN);
  if (err < 0) {
    AV_CRITICAL("set buffer sink options");
    cleanup();
    return false;
  }
*/

  s.outputs->name = av_strdup("in");
  s.outputs->filter_ctx = s.source;
  s.outputs->pad_idx = 0;
  s.outputs->next = NULL;

  s.inputs->name = av_strdup("out");
  s.inputs->filter_ctx = s.sink;
  s.inputs->pad_idx = 0;
  s.inputs->next = NULL;

  err = avfilter_graph_parse_ptr(s.graph, filters, &s.inputs, &s.outputs, NULL);
  if (err < 0) {
    AV_CRITICAL("parse filter description");
    return false;
  }

  err = avfilter_graph_config(s.graph, NULL);
  if (err < 0) {
    AV_CRITICAL("config filter graph");
    return false;
  }

  s.success = true;
  _p->filterGraph = s.graph;
  _p->filterSource = s.source;
  _p->filterSink = s.sink;
  _p->filterFrame = av_frame_alloc();
  _p->hwFramesContext = _p->context->hw_frames_ctx ? _p->context->hw_frames_ctx->data : nullptr;

  return true;
}

bool VideoContext::checkAmd(
    const QString& family, int codecId, int pixelFormat, int width, int height) {
  static constexpr struct {
    const char* type;
    int hwVersion;
  } supportedGpus[] = {
      {"uvd2", 20},  {"uvd5", 50},  {"uvd6", 60},  {"uvd6.3", 63},
      {"vcn1", 100}, {"vnc2", 120}, {"vcn3", 130},
  };

  static constexpr struct {
    int id;
    const char* name;
  } supportedCodecs[] = {
      {AV_CODEC_ID_AV1, "av1"},
      {AV_CODEC_ID_H264, "h264"},
      {AV_CODEC_ID_HEVC, "hevc"},
      {AV_CODEC_ID_VP9, "vp9"},
  };

  // https://wiki.archlinux.org/title/Hardware_video_acceleration#NVIDIA_driver_only
  // https://en.wikipedia.org/wiki/Unified_Video_Decoder
  static constexpr struct {
    const char* name;
    int pixFmt;
    int hwVersion;
  } supportedPixelFormats[] = {
      // clang-format off
      {"h264", AV_PIX_FMT_YUV420P, 20},
      
      {"hevc", AV_PIX_FMT_YUV420P, 60},
      {"hevc", AV_PIX_FMT_YUV420P10, 63},
      
      {"vp9", AV_PIX_FMT_YUV420P, 100},
      {"vp9", AV_PIX_FMT_YUV420P10, 100},
      {"vp9", AV_PIX_FMT_YUV420P12, 100},

      {"av1",  AV_PIX_FMT_YUV420P, 130},
      {"av1",  AV_PIX_FMT_YUV420P10, 130},
      // clang-format on
  };

  static constexpr struct {
    const char* codec;
    int maxW;
    int maxH;
    int hwVersion;
  } supportedResolutions[] = {
      // clang-format off
      {"h264", 1920, 1080, 20},
      {"h264", 4096, 2304, 50},

      {"hevc", 4096, 2304, 60},

      {"h264", 7680, 4320, 120},
      {"hevc", 7680, 4320, 120},
      {"vp9", 7680, 4320, 120},

      {"av1", 7680, 4320, 130},
      // clang-format on
  };

  QStringList gpuTypes;
  int hwVersion = -1;
  for (auto& gpu : supportedGpus) {
    gpuTypes += gpu.type;
    if (gpu.type == family) hwVersion = gpu.hwVersion;
  }

  if (hwVersion < 0 && family == "all") hwVersion = INT_MAX;

  if (hwVersion < 0) {
    qWarning() << "<NC>\namd: cannot check format support, unknown device family:" << family;
    qInfo() << "<NC>-options are:<MAG>" << gpuTypes;
    qInfo() << "<NC>-or use <MAG>\"all\"<RESET> to blindly try all known formats";
    qInfo() << "<NC>-reference: "
               "<URL><CYN>https://en.wikipedia.org/wiki/Unified_Video_Decoder";
    qInfo() << "<NC>";
    return false;
  }

  QString codecName;
  for (auto& codec : supportedCodecs)
    if (codec.id == codecId) {
      codecName = codec.name;
      break;
    }

  if (codecName.isEmpty()) {
    auto* dec = avcodec_find_decoder((AVCodecID) codecId);
    qDebug() << "unsupported codec:" << (dec ? QString(dec->name) : QString::number(codecId));
    return false;
  }

  bool supported = false;
  for (auto& codec : supportedPixelFormats) {
    if (codec.hwVersion > hwVersion) break;
    if (codec.name == codecName && codec.pixFmt == pixelFormat) {
      supported = true;
      break;
    }
  }

  if (!supported) {
    qDebug() << "unsupported codec/pixel format:" << codecName
             << av_pix_fmt_desc_get(AVPixelFormat(pixelFormat))->name;
    return false;
  }

  const QSize videoRes(width, height);
  if (videoRes.width() < 48 || videoRes.height() < 48) {
    qDebug() << "resolution must be at least 48x48:" << videoRes;
    return false;
  }

  supported = false;
  for (auto& res : supportedResolutions) {
    if (res.hwVersion > hwVersion) break;
    if (res.codec == codecName && res.maxH >= videoRes.height() && res.maxW >= videoRes.width()) {
      supported = true;
      break;
    }
  }

  if (!supported) {
    qDebug() << "unsupported resolution:" << codecName << videoRes;
    return false;
  }

  return true;
}

bool VideoContext::checkQuicksync(
    const QString& family, int codecId, int pixelFormat, int width, int height) {
  static constexpr struct {
    const char* type;
    int qsvVersion;
  } supportedGpus[] = {{"clarkdale", 10},  {"sandybridge", 11}, {"ivybridge", 20},
                       {"haswell", 30},    {"broadwell", 40},   {"braswell", 50},
                       {"skylake", 51},    {"apollolake", 60},  {"kabylake", 61},
                       {"coffeelake", 61}, {"cometlake", 61},   {"whiskeylake", 61},
                       {"icelake", 70},    {"jasperlake", 70},  {"tigerlake", 80},
                       {"rocketlake", 80}, {"alderlake", 80},   {"raptorlake", 80},
                       {"meteorlake", 90}, {"arrowlake", 90},   {"arc-alchemist", 90},
                       {"lunarlake", 100}};

  static constexpr struct {
    int id;
    const char* name;
  } supportedCodecs[] = {
      {AV_CODEC_ID_AV1, "av1"}, {AV_CODEC_ID_H264, "h264"}, {AV_CODEC_ID_HEVC, "hevc"},
      {AV_CODEC_ID_VP9, "vp9"}, {AV_CODEC_ID_VVC, "vvc"},
      // todo; add the other somewhat useless codecs
  };

  // https://en.wikipedia.org/wiki/Intel_Quick_Sync_Video
  // https://trac.ffmpeg.org/wiki/Hardware/QuickSync
  // https://github.com/intel/media-driver#decodingencoding-features
  static constexpr struct {
    const char* name;
    int pixFmt;
    int qsvVersion;
  } supportedPixelFormats[] = {
      // clang-format off
      // clarkdale 
      {"h264", AV_PIX_FMT_YUV420P, 10},
      
			// braswell
      {"hevc", AV_PIX_FMT_YUV420P, 50},

			// apollo lake
      {"hevc", AV_PIX_FMT_YUV420P10, 60},
      
      // kaby lake
      {"vp9", AV_PIX_FMT_YUV420P, 61},
      {"vp9", AV_PIX_FMT_YUV420P10, 61},
      {"hevc", AV_PIX_FMT_YUV420P12, 61},

			// ice lake
      {"vp9", AV_PIX_FMT_YUV444P, 70},
      {"vp9", AV_PIX_FMT_YUV444P10, 70},
      {"hevc", AV_PIX_FMT_YUV422P, 70},
      {"hevc", AV_PIX_FMT_YUV422P10, 70},
		  {"hevc", AV_PIX_FMT_YUV444P, 70},
			{"hevc", AV_PIX_FMT_YUV444P10,70},

      // tiger lake
      {"vp9", AV_PIX_FMT_YUV420P12, 80},
      {"vp9", AV_PIX_FMT_YUV444P12, 80},
      {"hevc", AV_PIX_FMT_YUV420P12, 80},
      {"hevc", AV_PIX_FMT_YUV422P12, 80},
      {"hevc", AV_PIX_FMT_YUV444P12, 80},
      {"av1",  AV_PIX_FMT_YUV420P, 80},
      {"av1", AV_PIX_FMT_YUV420P10, 80},
     
      // lunar lake 
      {"vvc", AV_PIX_FMT_YUV420P, 100},
      {"vvc", AV_PIX_FMT_YUV420P10, 100},

      // clang-format on
  };

  static constexpr struct {
    const char* codec;
    int maxW;
    int maxH;
    int qsvVersion;
  } supportedResolutions[] = {
      {"h264", 4096, 4096, 10}, //
      {"hevc", 4096, 4096, 60}, // https://forums.serverbuilds.net/t/guide-hardware-transcoding-the-jdm-way-quicksync-and-nvenc/1408/3
      {"hevc", 8192, 8192, 61}, {"vp9", 8192, 8192, 61},
      {"av1", 8192, 8192, 80},  {"vvc", 16384, 16384, 100}, // FIXME: guess
  };

  QStringList gpuTypes;
  int qsvVersion = -1;
  for (auto& gpu : supportedGpus) {
    gpuTypes += gpu.type;
    if (gpu.type == family) qsvVersion = gpu.qsvVersion;
  }

  if (qsvVersion < 0 && family == "all") qsvVersion = INT_MAX;

  if (qsvVersion < 0) {
    qWarning() << "<NC>\nqsv: cannot check format support, unknown device family:" << family;
    qInfo() << "<NC>-options:" << gpuTypes;
    qInfo() << "<NC>-or use <MAG>\"all\"<RESET> to blindly try all known formats";
    qInfo() << "<NC>-reference: "
               "<URL><CYN>https://en.wikipedia.org/wiki/Intel_Quick_Sync_Video";
    qInfo() << "<NC>";
    return false;
  }

  QString codecName;
  for (auto& codec : supportedCodecs)
    if (codec.id == codecId) {
      codecName = codec.name;
      break;
    }

  if (codecName.isEmpty()) {
    auto* dec = avcodec_find_decoder((AVCodecID) codecId);
    qDebug() << "unsupported codec:" << (dec ? QString(dec->name) : QString::number(codecId));
    return false;
  }

  bool supported = false;
  for (auto& codec : supportedPixelFormats) {
    if (codec.qsvVersion > qsvVersion) break;
    if (codec.name == codecName && codec.pixFmt == pixelFormat) {
      supported = true;
      break;
    }
  }

  if (!supported) {
    qDebug() << "unsupported codec/pixel format:" << codecName
             << av_pix_fmt_desc_get(AVPixelFormat(pixelFormat))->name;
    return false;
  }

  const QSize videoRes(width, height);
  if (videoRes.width() < 48 || videoRes.height() < 48) {
    qDebug() << "resolution must be at least 48x48:" << videoRes;
    return false;
  }

  supported = false;
  for (auto& res : supportedResolutions) {
    if (res.qsvVersion > qsvVersion) break;
    if (res.codec == codecName && res.maxH >= videoRes.height() && res.maxW >= videoRes.width()) {
      supported = true;
      break;
    }
  }

  if (!supported) {
    qDebug() << "unsupported resolution:" << codecName << videoRes;
    return false;
  }

  return true;
}

bool VideoContext::checkNvdec(
    const QString& family, int codecId, int pixelFormat, int width, int height) {
  static constexpr struct {
    const char* type;
    int nvdecVersion;
  } supportedGpus[] = {
      {"maxwell-v1", 10}, {"maxwell-v2", 20}, {"maxwell-v2+", 25}, {"pascal", 30},
      {"pascal+", 35},    {"volta", 36},      {"turing", 40},      {"hopper", 40},
      {"ampere", 50},     {"ada", 50},        {"blackwell", 60},
  };

  static constexpr struct {
    int id;
    const char* name;
  } supportedCodecs[] = {
      {AV_CODEC_ID_AV1, "av1"},
      {AV_CODEC_ID_H264, "h264"},
      {AV_CODEC_ID_HEVC, "hevc"},
      {AV_CODEC_ID_VP9, "vp9"},
      // todo; add the other somewhat useless codecs (mpeg1,mpeg2,mpeg4,vc1,vp8)
  };

  // https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new
  static constexpr struct {
    const char* name;
    int pixFmt;
    int nvdecVersion;
  } supportedPixelFormats[] = {
      // clang-format off
      // maxwell-v1/v2
      {"h264", AV_PIX_FMT_YUV420P, 10},
      
	  // maxwell-v2+, GTX750 / GTX950-960, GeForce GTX 965M
      {"hevc", AV_PIX_FMT_YUV420P, 25},
      {"hevc", AV_PIX_FMT_YUV420P10, 25},
      
      // pascal, 10-series
      {"vp9", AV_PIX_FMT_YUV420P, 30},
      {"hevc", AV_PIX_FMT_YUV420P12, 30},

      // pascal+, 1050ti/1080ti/Titan
      {"vp9", AV_PIX_FMT_YUV420P10, 35},
      {"vp9", AV_PIX_FMT_YUV420P12, 35},
      
      // turing, 20-series
      {"hevc", AV_PIX_FMT_YUV444P, 40},
      {"hevc", AV_PIX_FMT_YUV444P10, 40},
      {"hevc", AV_PIX_FMT_YUV444P12, 40},
      
      // ampere/ada 30/40 series
      {"av1",  AV_PIX_FMT_YUV420P, 50},
      {"av1",  AV_PIX_FMT_YUV420P10, 50},
      
      // blackwell 50-series
      {"h264", AV_PIX_FMT_YUV420P10, 60},
      {"h264", AV_PIX_FMT_YUV422P, 60},
      {"h264", AV_PIX_FMT_YUV422P10, 60},

      {"hevc", AV_PIX_FMT_YUV422P, 60},
      {"hevc", AV_PIX_FMT_YUV422P10, 60},
      // clang-format on
  };

  static constexpr struct {
    const char* codec;
    int maxW;
    int maxH;
    int nvdecVersion;
  } supportedResolutions[] = {
      {"h264", 4096, 4096, 1}, {"hevc", 4096, 2304, 25}, {"hevc", 8192, 8192, 3},
      {"vp9", 8192, 8192, 3},  {"av1", 8192, 8192, 5},
  };

  QStringList gpuTypes;
  int nvdecVersion = -1;
  for (auto& gpu : supportedGpus) {
    gpuTypes += gpu.type;
    if (gpu.type == family) nvdecVersion = gpu.nvdecVersion;
  }

  if (nvdecVersion < 0 && family == "all") nvdecVersion = INT_MAX;

  if (nvdecVersion < 0) {
    qWarning() << "<NC>\nnvdec: cannot check format support, unknown nvidia family:" << family;
    qInfo() << "<NC>-options are:<MAG>" << gpuTypes;
    qInfo() << "<NC>-or use <MAG>\"all\"<RESET> to blindly try all known formats";
    qInfo() << "<NC>-reference: "
               "<URL><CYN>https://developer.nvidia.com/"
               "video-encode-and-decode-gpu-support-matrix-new";
    qInfo() << "<NC>";
    return false;
  }

  QString codecName;
  for (auto& codec : supportedCodecs)
    if (codec.id == codecId) {
      codecName = codec.name;
      break;
    }

  if (codecName.isEmpty()) {
    auto* dec = avcodec_find_decoder((AVCodecID) codecId);
    qDebug() << "unsupported codec:" << (dec ? QString(dec->name) : QString::number(codecId));
    return false;
  }

  bool supported = false;
  for (auto& codec : supportedPixelFormats) {
    if (codec.nvdecVersion > nvdecVersion) break;
    if (codec.name == codecName && codec.pixFmt == pixelFormat) {
      supported = true;
      break;
    }
  }

  if (!supported) {
    qDebug() << "unsupported codec/pixel format:" << codecName
             << av_pix_fmt_desc_get(AVPixelFormat(pixelFormat))->name;
    return false;
  }

  const QSize videoRes(width, height);
  if (videoRes.width() < 48 || videoRes.height() < 48) {
    qDebug() << "resolution must be at least 48x48:" << videoRes;
    return false;
  }

  supported = false;
  for (auto& res : supportedResolutions) {
    if (res.nvdecVersion > nvdecVersion) break;
    if (res.codec == codecName && res.maxH >= videoRes.height() && res.maxW >= videoRes.width()) {
      supported = true;
      break;
    }
  }

  if (!supported) {
    qDebug() << "unsupported resolution:" << codecName << videoRes;
    return false;
  }

  return true;
}

bool VideoContext::initAccel(const AVCodec** outCodec,
                             AVCodecContext** outContext,
                             bool& outUsesFilter,
                             const AVCodec* swCodec,
                             const AVCodecContext* swContext,
                             const AVStream* videoStream) const {
  QString deviceType, deviceId, deviceVendor, deviceFamily;

  QStringList disabled;
  QStringList enabled;

  // <libav-device-string>,family=<family>,vendor=<vendor>,disable=<rej-list>,enable=<accept-list>
  QHash<QString, QString> deviceOptions;
  {
    const QStringList parts = _options.accel.split(',');
    deviceId = parts[0];
    deviceType = deviceId.mid(0, deviceId.indexOf(':'));
    for (int i = 1; i < parts.count(); ++i) {
      QStringList kv = parts[i].split('=');
      if (kv[0] == "family") {
        deviceFamily = kv[1];
        continue; // don't pass our own options to libavcodec
      }
      if (kv[0] == "vendor") {
        deviceVendor = kv[1];
        continue;
      }
      if (kv[0] == "jobs") continue;
      if (kv[0] == "disable") {
        disabled = kv[1].split(';');
        continue;
      }
      if (kv[0] == "enable") {
        enabled = kv[1].split(';');
        continue;
      }
      deviceOptions.insert(kv[0], kv[1]);
    }
  }

  if (disabled.count() && enabled.count()) {
    qWarning() << "using both disabled= and enabled= is not supported";
    return false;
  }

  AVHWDeviceType deviceTypeId = AV_HWDEVICE_TYPE_NONE;
  QString codecSuffix = "";

  outUsesFilter = false;

  const char* ffConfigure = nullptr;
  if (deviceType == "nvdec") {
    // we do not setup hwdevice for nvdec as it can decode&scale directly into system memory
    // the device index is set via the "gpu" option to the codec
    deviceVendor = "nvidia";
    codecSuffix = "_cuvid";
    ffConfigure = "--enable-cuvid";
  } else if (deviceType == "qsv") {
    deviceTypeId = AV_HWDEVICE_TYPE_QSV;
    deviceVendor = "intel";
    codecSuffix = "_qsv";
    ffConfigure = "--enable-libvpl";
    outUsesFilter = true; // scale_qsv
  } else if (deviceType == "vulkan") {
    deviceTypeId = AV_HWDEVICE_TYPE_VULKAN;
    ffConfigure = "--enable-vulkan --enable-libshaderc";
    outUsesFilter = true; // scale_vulkan
  }
#ifdef Q_OS_UNIX
  else if (deviceType == "vaapi") {
    deviceTypeId = AV_HWDEVICE_TYPE_VAAPI;
    ffConfigure = "--enable-vaapi";
    outUsesFilter = true; // scale_vaapi
  }
#endif
#ifdef Q_OS_WIN
  else if (deviceType == "d3d11va") {
    deviceTypeId = AV_HWDEVICE_TYPE_D3D11VA;
    // outUsesFilter = true; // experimental scale_d3d11va
  } else if (deviceType == "d3d12va") {
    deviceTypeId = AV_HWDEVICE_TYPE_D3D12VA;
  }
#endif
#ifdef Q_OS_MAC
  else if (deviceType == "videotoolbox") {
    deviceTypeId = AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
    outUsesFilter = true; // scale_vt
  }
#endif
  else {
    QStringList hwAccels;
#ifdef Q_OS_LINUX
    hwAccels = {"nvdec,qsv,vaapi,vulkan"};
#endif
#ifdef Q_OS_WIN
    hwAccels = {"nvdec,qsv,d3d11va,d3d12va,vulkan"};
#endif
#ifdef Q_OS_MAC
    hwAccels = {"videotoolbox"};
#endif

    qWarning() << "unsupported device type" << deviceType << "choices are: " << hwAccels;
    return false;
  }

  if (deviceFamily.isEmpty()) {
    qWarning("device type and family is required e.g. -i.hwdec nvdec,family=pascal");
    return false;
  }

  bool supported = false;
  if (deviceVendor == "intel")
    supported = checkQuicksync(deviceFamily, swContext->codec_id, swContext->pix_fmt,
                               videoStream->codecpar->width, videoStream->codecpar->height);
  else if (deviceVendor == "nvidia")
    supported = checkNvdec(deviceFamily, swContext->codec_id, swContext->pix_fmt,
                           videoStream->codecpar->width, videoStream->codecpar->height);
  else if (deviceVendor == "amd")
    supported = checkAmd(deviceFamily, swContext->codec_id, swContext->pix_fmt,
                         videoStream->codecpar->width, videoStream->codecpar->height);
  else if (deviceVendor != "any") {
    qWarning() << "<NC>\ncannot check format support, device vendor is required, e.g. -i.hwdec "
                  "vaapi,vendor=intel,family=kabylake";
    qInfo() << "<NC>-options are: <MAG>amd, intel, nvidia";
    qInfo() << "<NC>-or use <MAG>\"any\"<RESET> to skip format checks";
    qInfo() << "<NC>";
  } else {
    supported = true;
    qDebug() << "format checks disabled";
  }

  // vp9 is definitely not supported
  if (supported && deviceType == "vulkan" && swContext->codec_id == AV_CODEC_ID_VP9) {
    qDebug() << "vulkan does not support vp9";
    supported = false;
  }

  if (!supported) return false;

  const AVCodec* hwCodec = nullptr;

  const QString codecName = swCodec->name + codecSuffix;

  qDebug() << "checking codec:" << codecName
           << av_pix_fmt_desc_get(AVPixelFormat(swContext->pix_fmt))->name
           << videoStream->codecpar->width << videoStream->codecpar->height;

  hwCodec = avcodec_find_decoder_by_name(qUtf8Printable(codecName));

  if (!hwCodec) {
    qWarning() << "codec" << codecName << "is not available in libavcodec";
    if (ffConfigure) qWarning() << "did you compiled ffmpeg with" << ffConfigure << "?";
    return false;
  }

  if (disabled.contains(swCodec->name)) {
    qDebug() << deviceId << swCodec->name << "is supported, but disabled by user";
    return false;
  }

  if (enabled.count() && !enabled.contains(swCodec->name)) {
    qDebug() << deviceId << swCodec->name << "is supported, but disabled by user";
    return false;
  }

  // this is as much as we can do without opening device/driver
  // we use this with fork option of indexer for buggy hw decoders
  // that might crash the application or leak system/GPU memory
  if (_options.preflight) return true;

  int err = 0;

  AVCodecContext* hwContext = avcodec_alloc_context3(hwCodec);
  if (!hwContext) {
    AV_WARNING("could not allocate codec context");
    return false;
  }
  hwContext->opaque = (void*) logContext();

  *outContext = hwContext; // the caller must free on error

  if (deviceTypeId) {
    AVDictionary* dict = nullptr;
    for (const auto& option : std::as_const(deviceOptions).asKeyValueRange())
      av_dict_set(&dict, qPrintable(option.first), qPrintable(option.second), 0);

    int pos = deviceId.indexOf(':');

    QString device;
    if (pos > 0) device = deviceId.mid(pos + 1);

    qDebug() << "creating device context" << deviceId << device;
    // FIXME: quicksync on Windows11 leaks badly unless we only do this once; however it also
    // crashes after around 45-46 iterations if we do
    //static AVBufferRef* devCtx = nullptr;
    //if (!devCtx) err = av_hwdevice_ctx_create(&devCtx, deviceTypeId, qPrintable(deviceId), dict, 0);
    //hwContext->hw_device_ctx = av_buffer_ref(devCtx);
    av_log_set_level(AV_LOG_TRACE);
    err = av_hwdevice_ctx_create(&hwContext->hw_device_ctx, deviceTypeId,
                                 device.isEmpty() ? NULL : qPrintable(device), dict, 0);
    av_dict_free(&dict);
    av_log_set_level(AV_LOG_INFO);

    if (err != 0 || !hwContext->hw_device_ctx) {
      AV_CRITICAL("create device context failed");
      qInfo("check you supplied the correct device id and options");
      qInfo("see ffmpeg -init_hw_device "
            "<URL>https://www.ffmpeg.org/ffmpeg.html#Advanced-Video-options");
      return false;
    }
    qDebug() << "<MAG>create hw device successful";

    //hwContext->get_format = get_format_qsv;
    // the documentation says I should not be setting hw_device_ctx but rather
    // setting hw_frames_ctx from get_format()... however, this has problems:
    // - what sw_pix_fmt to use? depends on accel/codec/etc..
    // - what initial_pool_size value is sufficient for accel/codec)
    // - get_format() is not called when opening the codec as docs suggest (at least for qsv),
    //   so I would not be able to initialize filter (which needs hw_frames_ctx), before decoding any frame
    // - all of this is solved by allowing libavcodec to manage hw_frames_ctx, the
    //   downside is hw filters cannot be initialized until after the first decodeFrame()
  }

  if ((err = avcodec_parameters_to_context(hwContext, videoStream->codecpar)) < 0) {
    AV_CRITICAL("failed to copy codec params");
    return -3;
  }

  AVDictionary* hwOptions = nullptr;
  if (QString(hwCodec->name).endsWith("cuvid")) {
    int pos = deviceId.indexOf(':');
    if (pos >= 0) {
      QString index = deviceId.mid(pos + 1);
      qDebug() << "using nvdec option gpu=" << index;
      av_dict_set(&hwOptions, "gpu", qPrintable(index), 0);
    }
    if (_options.maxW > 0 && _options.maxH > 0) {
      // hardware scaling should be better, assuming it can happen without leaving the gpu,
      // and does not require filter graph
      QString size = QString("%1x%2").arg(_options.maxW).arg(_options.maxH);
      qDebug() << "using nvdec option resize=" << size;
      av_dict_set(&hwOptions, "resize", qPrintable(size), 0);
    }
  }

  // add extra frames for filter
  // if (outUsesFilter && hwContext->hw_device_ctx) hwContext->extra_hw_frames = 2;

  if ((err = avcodec_open2(hwContext, hwCodec, &hwOptions)) < 0) {
    AV_CRITICAL("failed to open codec");
    return false;
  }

  *outCodec = hwCodec;

  return true;
}

int VideoContext::open(const QString& path, const DecodeOptions& opt_) {
  if (_p->context) close();

  snprintf(_logContext, sizeof(_logContext), "%s", qUtf8Printable(path));

  if (_path != path) _metadata.clear();

  _path = path;
  _options = opt_;

  _errorCount = 0;

  _firstPts = AV_NOPTS_VALUE;
  _lastFrameNumber = -1;

  _eof = false;
  _p->packet.size = 0;
  _p->packet.data = nullptr;

  _p->format = avformat_alloc_context(); // only reason for this is avLogger
  _p->format->opaque = (void*) logContext();
  Q_ASSERT(_p->format);

  // set context to log source of errors
  const QString fileName = QFileInfo(_path).fileName();

  AVDictionary* formatOptions = nullptr;
  av_dict_set(&formatOptions, "ignore_editlist", "1", 0);

  int err = 0;
  if ((err = avformat_open_input(&_p->format, qUtf8Printable(_path), nullptr, &formatOptions)) < 0) {
    AV_CRITICAL("cannot open input");
    avformat_free_context(_p->format);
    _p->format = nullptr;
    return -1;
  }

  // firstPts is needed for seeking
  // read a few packets to find it, then close/reopen the file
  _p->format->flags |= AVFMT_FLAG_GENPTS;

  for (int i = 0; i < 5;) {
    if (0 > av_read_frame(_p->format, &_p->packet)) break;

    const auto* stream = _p->format->streams[_p->packet.stream_index];
    int64_t pts = _p->packet.pts;
    av_packet_unref(&_p->packet);  // free memory

    if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) continue;
    if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) continue;

    // TODO: if this is reliable we don't need this loop...
    //    if (stream->start_time != AV_NOPTS_VALUE) {
    //      _firstPts = stream->start_time;
    //      qDebug() << "stream start_time" << _firstPts;
    //      break;
    //    }
    if (_firstPts == AV_NOPTS_VALUE)
      _firstPts = pts;
    else
      _firstPts = std::min(_firstPts, pts);
    i++;
  }

  avformat_close_input(&_p->format);  // _p->format == nullptr
  Q_ASSERT(_p->format == nullptr);

  err = 0;
  if (_firstPts == AV_NOPTS_VALUE) {
    AV_CRITICAL("no first PTS was found");
    return -1;
  }

  _p->format = avformat_alloc_context();
  Q_ASSERT(_p->format);
  _p->format->opaque = (void*) logContext();

  formatOptions = nullptr;
  av_dict_set(&formatOptions, "ignore_editlist", "1", 0);

  if ((err = avformat_open_input(&_p->format, qUtf8Printable(_path), nullptr, &formatOptions)) < 0) {
    AV_CRITICAL("cannot reopen input");
    avformat_free_context(_p->format);
    _p->format = nullptr;
    return -1;
  }

  auto freeFormat = [this]() {
    avformat_close_input(&_p->format);
    Q_ASSERT(_p->format == nullptr);
  };

  if ((err = avformat_find_stream_info(_p->format, nullptr)) < 0) {
    AV_CRITICAL("cannot find stream info");
    freeFormat();
    return -2;
  }

  // av_dump_format(_p->format, 0, NULL, 0);

  // determine stream we are opening and get some metadata
  int videoStreamIndex = -1;
  int audioStreamIndex = -1;
  for (unsigned int i = 0; i < _p->format->nb_streams; i++) {
    AVStream* stream = _p->format->streams[i];
    const AVCodecParameters* codecParams = stream->codecpar;
    stream->discard = AVDISCARD_ALL;

    if (codecParams->codec_type == AVMEDIA_TYPE_VIDEO) {
      if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) continue;
      if (videoStreamIndex >= 0) continue;

      videoStreamIndex = int(i);
      _metadata.isEmpty = false;

      AVRational fps = stream->r_frame_rate;
      _metadata.frameRate = float(av_q2d(fps));
      _metadata.frameSize = QSize(codecParams->width, codecParams->height);
      _metadata.videoBitrate = int(codecParams->bit_rate);

      // if (stream->metadata) {
      //   AVDictionaryEntry* tag = NULL;
      //   while ((tag = av_dict_get(stream->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
      //     qDebug() << tag->key << tag->value;
      //   }
      // }

      const AVPacketSideData* sideData = av_packet_side_data_get(codecParams->coded_side_data,
                                                                 codecParams->nb_coded_side_data,
                                                                 AV_PKT_DATA_DISPLAYMATRIX);
      if (sideData) {
        const int32_t* matrix = (const int32_t*) sideData->data;
        _metadata.rotation = av_display_rotation_get(matrix);
        if ((std::abs(int(_metadata.rotation)) / 90) & 1) _metadata.frameSize.transpose();
      }

      const AVCodec* vCodec = avcodec_find_decoder(codecParams->codec_id);
      if (vCodec) {
        _metadata.videoCodec = vCodec->name;
        const char* profileName = av_get_profile_name(vCodec, codecParams->profile);
        if (profileName) {
          _metadata.videoProfile = profileName;
          if (codecParams->level > 0)
            _metadata.videoProfile += qq(", Level %1").arg(codecParams->level);
        }
      }
      stream->discard = AVDISCARD_NONE;

    } else if (codecParams->codec_type == AVMEDIA_TYPE_AUDIO) {
      if (audioStreamIndex >= 0) continue;

      audioStreamIndex = int(i);
      _metadata.isEmpty = false;
      _metadata.audioBitrate = int(codecParams->bit_rate);
      _metadata.sampleRate = codecParams->sample_rate;
      _metadata.channels = codecParams->ch_layout.nb_channels;

      const AVCodec* aCodec = avcodec_find_decoder(codecParams->codec_id);
      if (aCodec) _metadata.audioCodec = aCodec->name;
    }
  }

  const int fileBitRate = _p->format->bit_rate;
  if (!_metadata.videoBitrate && fileBitRate) { // not all codecs/formats provide bitrate (MKV)
    if (_metadata.audioBitrate)
      _metadata.videoBitrate = fileBitRate - _metadata.audioBitrate;
    else if (_p->format->bit_rate) {
      qDebug() << "no codec bitrate provided, guessing from file bitrate";
      _metadata.audioBitrate = 128000;
      _metadata.videoBitrate = fileBitRate - _metadata.audioBitrate;
    }
  }

  _metadata.duration = int(_p->format->duration / AV_TIME_BASE);

  if (_p->format->metadata) {
    AVDictionaryEntry* metaTitle = av_dict_get(_p->format->metadata, "title", nullptr, 0);
    if (metaTitle) _metadata.title = metaTitle->value;

    AVDictionaryEntry* metaTime = av_dict_get(_p->format->metadata, "creation_time", nullptr, 0);
    if (metaTime) {
      _metadata.creationTime = QDateTime::fromString(metaTime->value, "yyyy-MM-ddThh:mm:s.zzzzzzZ");
    }
  }

  err = 0;
  if (videoStreamIndex < 0) {
    AV_CRITICAL("cannot find video stream");
    freeFormat();
    return -3;
  }

  _p->videoStream = _p->format->streams[videoStreamIndex];
  _p->format->flags |= AVFMT_FLAG_GENPTS;

  const AVCodec* swCodec = avcodec_find_decoder(_p->videoStream->codecpar->codec_id);
  if (!swCodec) {
    AV_CRITICAL("cannot find video codec");
    freeFormat();
    return -3;
  }

  _p->context = avcodec_alloc_context3(swCodec);
  if (!_p->context) {
    AV_CRITICAL("could not allocate video codec context");
    freeFormat();
    return -3;
  }
  _p->context->opaque = (void*) logContext();

  auto freeContext = [this]() {
    avcodec_free_context(&_p->context);
    Q_ASSERT(_p->context == nullptr);
    avformat_close_input(&_p->format);
    Q_ASSERT(_p->format == nullptr);
  };

  if (avcodec_parameters_to_context(_p->context, _p->videoStream->codecpar) < 0) {
    AV_CRITICAL("failed to copy codec params to codec context");
    freeContext();
    return -3;
  }

  if (!_options.accel.isEmpty()) {
    const AVCodec* hwCodec = nullptr;
    AVCodecContext* hwContext = nullptr;
    MessageContext msgContext(_path + '|' + deviceId());
    if (initAccel(&hwCodec, &hwContext, _p->hwFilter, swCodec, _p->context, _p->videoStream)) {
      avcodec_free_context(&_p->context);
      _p->codec = hwCodec;
      _p->context = hwContext;
      _options.threads = 1;
      _options.iframes = false;
      _options.lowres = false;
      snprintf(_logContext, sizeof(_logContext), "%s|%s", qUtf8Printable(path),
               qUtf8Printable(deviceId()));
      if (_options.preflight) return 0;
    } else {
      avcodec_free_context(&hwContext);
      _p->hwFilter = false;
      _options.accel.clear();
      if (_options.nofallback || _options.preflight) {
        freeContext();
        return -3;
      }
      qDebug() << "hardware codec failed, falling back to software";
    }
  }

  if (_options.accel.isEmpty()) {
    AVDictionary* codecOptions = nullptr;
    _p->codec = swCodec;

    if (_options.fast) {
      // it seems safe to enable this, about 20% boost.
      // the downscaler will smooth out any artifacts
      av_dict_set(&codecOptions, "skip_loop_filter", "all", 0);

      // grayscale decoding is not built-in to ffmpeg usually,
      // performance improvement is 5-10%
      // if (!opt.rgb)
      // av_dict_set(&codecOptions, "flags", "gray", 0);
    }

    if (_options.iframes) {
      // note: some codecs do not support this or are already intra-frame codecs
      // FIXME: is there a way to tell before we see the gap in the frame numbers?

      // for these codecs we want "nointra", to get more keyframes; note if this list
      // is wrong, we get 0 keyframes
      static constexpr AVCodecID codecs[] = {AV_CODEC_ID_H264, AV_CODEC_ID_AV1,
                                             AV_CODEC_ID_HEVC, AV_CODEC_ID_MPEG2VIDEO,
                                             AV_CODEC_ID_PDV,  AV_CODEC_ID_NONE};
      const char* skip = "nokey";
      for (int i = 0; codecs[i]; ++i)
        if (_p->codec->id == codecs[i]) {
          skip = "nointra";
          break;
        }
      av_dict_set(&codecOptions, "skip_frame", skip, 0);
    }

    if (_options.lowres > 0) {
      // this is quite good for some old codecs; nothing modern though
      // TODO: set lowres value so it gives >= maxw/maxh
      int lowres = _options.lowres;
      if (_p->codec->max_lowres <= 0) {
        qDebug("lowres decoding requested but %s doesn't support it",
               qPrintable(_metadata.videoCodec));
        lowres = 0;
      } else {
        if (lowres > _p->codec->max_lowres) {
          lowres = _p->codec->max_lowres;
          qWarning("lowres limited to %d", lowres);
        }
        av_dict_set(&codecOptions, "lowres", qPrintable(QString::number(lowres)), 0);
      }
      _options.lowres = lowres;
    }

    // if (_p->codec->capabilities & CODEC_CAP_TRUNCATED)
    //    _p->context->flags|= CODEC_FLAG_TRUNCATED; // we do not send complete
    //    frames

    int threads = 1;
    if (_p->codec->capabilities
        & (AV_CODEC_CAP_FRAME_THREADS | AV_CODEC_CAP_SLICE_THREADS | AV_CODEC_CAP_OTHER_THREADS)) {
      threads = _options.threads;
      _metadata.supportsThreads = true;
    }
    _options.threads = threads;

    // note: no need to set thread_type, let ffmpeg choose the best options
    if (_options.threads > 0) {
      qDebug() << "set thread count" << _options.threads;
      _p->context->thread_count = _options.threads;
    }

    if ((err = avcodec_open2(_p->context, _p->codec, &codecOptions)) < 0) {
      AV_CRITICAL("could not open codec");
      freeContext();
    }
  }

  //  qDebug("%s using %d threads (requested %d) threads=%s",
  //         _p->codec->name,
  //         _p->context->thread_count, _numThreads,
  //         _p->context->active_thread_type == FF_THREAD_FRAME ? "frame" : "slice");

  _p->frame = av_frame_alloc();
  if (!_p->frame) {
    err = 0;
    AV_CRITICAL("could not allocate video frame");
    freeContext();
    return -6;
  }

  if (_p->videoStream->nb_frames == 0)
    _p->videoStream->nb_frames = long(_p->videoStream->duration * av_q2d(_p->videoStream->time_base)
                                      / av_q2d(_p->videoStream->r_frame_rate));

  // if we are using hwaccel, we should probably decode a frame. It seems most hwaccel will
  // happily open the codec to then find out it is unsupported format or there was a problem
  // opening the hwaccel in the first place...
  if (_options.accel.isEmpty()) return 0;

  QImage frame;
  if (nextFrame(frame)) {
    int err;
    if ((err = av_seek_frame(_p->format, _p->videoStream->index, 0, AVSEEK_FLAG_BACKWARD) < 0)) {
      AV_CRITICAL("seek to frame 0");
      return -8;
    }
    avcodec_flush_buffers(_p->context);
    return 0;
  } else {
    qDebug() << "failed to decode the first frame";
    close();
    return -7;
  }
}

void VideoContext::close() {
  if (_p->scaler) {
    sws_freeContext(_p->scaler);
    _p->scaler = nullptr;
  }
  if (_p->scaled.data[0]) av_freep(&(_p->scaled.data[0]));

  if (_p->transferFrame) av_frame_free(&_p->transferFrame);

  if (_p->filterFrame) av_frame_free(&_p->filterFrame);

  if (_p->filterGraph) avfilter_graph_free(&_p->filterGraph);

  _p->filterSource = nullptr;
  _p->filterSink = nullptr;
  _p->hwFilter = false;
  _p->hwFramesContext = nullptr;

  if (_p->frame) {
    av_frame_free(&_p->frame);
    Q_ASSERT(_p->frame == nullptr);
  }

  if (_p->context) {
    avcodec_free_context(&_p->context);
    Q_ASSERT(_p->context == nullptr);
  }

  if (_p->format) {
    avformat_close_input(&_p->format);
    Q_ASSERT(_p->format == nullptr);
  }

  // av_packet_unref(&_p->packet);

  delete _p;
  _p = new VideoContextPrivate;
}

int VideoContext::ptsToFrame(int64_t pts) const {
  auto timeBase = av_q2d(_p->videoStream->time_base);
  auto frameRate = av_q2d(_p->videoStream->r_frame_rate);
  return floor((pts - _firstPts) * timeBase * frameRate + 0.5);
}

int64_t VideoContext::frameToPts(int frame) const {
  auto timeBase = av_q2d(_p->videoStream->time_base);
  auto frameRate = av_q2d(_p->videoStream->r_frame_rate);
  return floor(frame / frameRate / timeBase + 0.5) + _firstPts;
}

bool VideoContext::seekDumb(int frame) {
  if (frame > _MAX_DUMBSEEK_FRAMES) {
    qCritical() << "refusing to seek, too many frames:" << frame;
    return false;
  }
  AV_WARNING("!! decoding *all* frames !!");
  while (frame--)
    if (!decodeFrame()) return false;

  return true;
}

bool VideoContext::seekFast(int frame) {
  if (frame <= 0) return true;

  const int64_t target = frameToPts(frame);

  // qDebug("frame=%d pts=(%" PRIi64 ")", frame, target);

  int err = av_seek_frame(_p->format, _p->videoStream->index, target, AVSEEK_FLAG_BACKWARD);
  if (err < 0) {
    AV_WARNING("seek error") << err;
    return false;
  }

  avcodec_flush_buffers(_p->context);

  _eof = false;

  return true;
}

bool VideoContext::seek(int frame, QVector<QImage>* decoded, int* maxDecoded) {
  const double frameDuration = 1.0 / av_q2d(_p->videoStream->time_base);
  const int64_t target = frameToPts(frame);

  // qDebug("frame=%d pts=(%" PRIi64 ") pts=(%" PRIi64 ")", frame, target, target-_firstPts);

  int seekedFrame = 0;  // frame number we actually seeked to

  if (target > _firstPts) {  // do not seek before first pts; decode frames instead
    bool isKeyframe = false;
    int64_t seekTime = target;
    int tries = 0;

    // seek <= where we want to go, and try to get a keyframe
    // if we don't get a keyframe, or we overshot seek point,
    // back up and try again
    do {
      int err = av_seek_frame(_p->format, _p->videoStream->index, seekTime, AVSEEK_FLAG_BACKWARD);
      if (err < 0) {
        qCritical("av_seek_frame error %d", err);
        return false;
      }
      // Q_ASSERT(_p->videoStream->codec == _p->context);
      avcodec_flush_buffers(_p->context);
      _eof = false;

      // check for keyframe (stream corruption, or eos?)
      do {
        if (!readPacket()) break;

        Q_ASSERT(_p->packet.pts != AV_NOPTS_VALUE);

        isKeyframe = _p->packet.flags & AV_PKT_FLAG_KEY;
        // if (!isKeyframe)
        //   AV_WARNING("miss: seek to non-keyframe");

        err = avcodec_send_packet(_p->context, &_p->packet);
        if (err != 0) {
          AV_WARNING("miss: send_packet error") << Qt::hex << err;
          break;
        }

      } while (!isKeyframe && _p->packet.pts < target);

      tries++;
      if (!isKeyframe || _p->packet.pts > target) {
        AV_DEBUG("try:") << tries << "key:" << isKeyframe << "dist:" << seekTime - _p->packet.pts;

        // guess the next time try; if we back up too much we pay the price
        // of decoding a lot of frames; not enough and we never get there...

        // back up half the amount we missed, plus one frame
        seekTime = (seekTime + (seekTime - _p->packet.pts) / 2.0 - frameDuration);

        if (tries > 10) {
          AV_WARNING("failed after 10 attempts, seeking dumb");
          close();
          open(_path, _options);
          return seekDumb(frame);
        }
      }

    } while (!isKeyframe || _p->packet.pts > target);

    seekedFrame = ptsToFrame(_p->packet.pts);
  } else {
    // to read frames from the start we have to reopen
    AV_WARNING("reopening stream for seek < first pts");
    close();
    if (0 != open(_path, _options)) return false;
  }

  // accurate seek requires decoding some frames
  int framesLeft = frame - seekedFrame;
  if (framesLeft > 0) AV_DEBUG("decoding") << framesLeft << "interframes";

  if (maxDecoded) *maxDecoded = framesLeft;

  int i = 0;
  while (framesLeft--)
    if (decodeFrame()) {
      // store the intermediate frames, useful for backwards scrub
      if (decoded) {
        if (framesLeft < decoded->count()) {
          QImage& img = (*decoded)[i++];
          frameToQImg(img);
        } else if (decoded->count()) {
          AV_WARNING("insufficient frames supplied" << decoded->count());
        }
      }
    } else {
      // no longer valid since we couldn't get the target
      if (decoded) decoded->clear();

      AV_WARNING("decode failed, giving up");
      return false;
    }

  return true;
}

bool VideoContext::readPacket() {
  do {
    av_packet_unref(&_p->packet);

    int err = av_read_frame(_p->format, &_p->packet);
    if (err < 0) {
      if (err != AVERROR_EOF) AV_CRITICAL("av_read_frame");
      _eof = true;
      return false;
    }

    if (_p->packet.flags & AV_PKT_FLAG_CORRUPT) AV_WARNING("corrupt packet");

  } while (_p->packet.stream_index != _p->videoStream->index);

  return true;
}

bool VideoContext::decodeFrame() {
  while (true) {
    if (_errorCount > _MAX_ERROR_COUNT) {
      qWarning() << "maximum error count exceeded";
      close();
      return false;
    }

    int err = avcodec_receive_frame(_p->context, _p->frame);

    if (err == 0) {
        auto conv = av_mul_q(_p->videoStream->time_base, _p->videoStream->r_frame_rate);
        auto pts = av_make_q(_p->frame->best_effort_timestamp, 1);
        int frameNumber = av_q2d(av_mul_q(pts, conv));
        if (frameNumber == _lastFrameNumber) {
          // qWarning() << "non-increasing frame number" << frameNumber << _p->context->frame_num;
          // seems to be rounding issue so just bump it
          frameNumber++;
        }

        // NOTE: this will happen, commonly with stream captures; but it would break indexer
        // as it requires increasing frame numbers (since v2 format)
        if (_options.iframes && frameNumber < _lastFrameNumber)
          qWarning() << "backwards frame number" << frameNumber << _lastFrameNumber << _p->context->frame_num;

        // if (frameNumber != _lastFrameNumber+1) // we expect this if skip_frame is working
        //   qWarning() << "non-monotonically increasing frame number" << frameNumber << _lastFrameNumber << _p->context->frame_num;

        _lastFrameNumber = frameNumber;

      return true;
    }

    if (err == AVERROR_EOF) {
      AV_DEBUG("avcodec_receive_frame eof");
      break;
    } else if (err != AVERROR(EAGAIN)) {
      QString msg = qq("avcodec_receive_frame near frame: %1 avError=%2 %3")
                        .arg(_lastFrameNumber)
                        .arg(err, 0, 16)
                        .arg(avErrorString(err));
      avLoggerWriteLogLine(logContext(), msg);
      _errorCount++;
      qCritical() << msg;
      break;
    }

    // AVERROR(EAGAIN)
    if (!_eof) {
      if (!readPacket()) {                          // if false, _eof == true
        avcodec_send_packet(_p->context, nullptr);  // flush decoder
        continue;                                   // => receive_frame()
      }

      if (_p->packet.size == 0) {
        QString msg = qq("empty packet, giving up near frame: %1 avError=%2 %3")
                          .arg(_lastFrameNumber)
                          .arg(err, 0, 16)
                          .arg(avErrorString(err));
        avLoggerWriteLogLine(logContext(), msg);
        qCritical() << msg;
        break;
      }

      Q_ASSERT(_p->packet.stream_index == _p->videoStream->index);

      err = avcodec_send_packet(_p->context, &_p->packet);
      if (err != 0) {
        QString msg = qq("avcodec_send_packet near frame: %1 avError=%2 %3")
                          .arg(_lastFrameNumber)
                          .arg(err, 0, 16)
                          .arg(avErrorString(err));
        // TODO: limit number of errors logged
        avLoggerWriteLogLine(logContext(), msg);
        _errorCount++;
        qCritical() << msg;

        // we get this when decoding av1 when there is no av1 implementation "Function not implemented"
        if (err == -0x28) {
          qWarning() << "decode aborted";
          return false;
        }
      }
    } else {
      // attempt to prevent hang when seeking near the eof
      qWarning() << "resending null packet near frame:" << _lastFrameNumber;
      avcodec_send_packet(_p->context, nullptr);
    }
  }

  return false;
}

QVariantList VideoContext::readMetaData(const QString& _path, const QStringList& keys) {
  AVFormatContext* format = nullptr;

  QVariantList values;
  for (int i = 0; i < keys.count(); ++i) values.append(QVariant());

  format = avformat_alloc_context();
  Q_ASSERT(format);

  QByteArray cPath = _path.toUtf8();
  format->opaque = cPath.data();

  AVDictionary* formatOptions = nullptr;
  av_dict_set(&formatOptions, "ignore_editlist", "1", 0);

  int err = 0;
  if ((err = avformat_open_input(&format, qUtf8Printable(_path), nullptr, &formatOptions)) < 0) {
    AV_CRITICAL("cannot open input");
    return values;
  }

  if (!format->metadata) {
    AV_DEBUG("no metadata");
    return values;
  }

  for (int i = 0; i < keys.count(); ++i) {
    AVDictionaryEntry* entry = av_dict_get(format->metadata, qPrintable(keys[i]), nullptr, 0);
    if (entry) values[i] = QString(entry->value);
  }

  avformat_close_input(&format);

  return values;
}

int VideoContext::convertFrame(int& w, int& h, int& fmt, const AVFrame* srcFrame) {
  w = _options.maxW;
  h = _options.maxH;

  if (!w || !h) {
    w = srcFrame->width;
    h = srcFrame->height;
  }

  // formats we can convert directly to cvImg/QImage
  const bool isConvertable = (_options.gray && srcFrame->format == AV_PIX_FMT_YUV420P);

  // hw frames have to be downloaded/mapped to system memory
  const bool isHardware = srcFrame->hw_frames_ctx != NULL;

  if (isConvertable && !isHardware && w == srcFrame->width && h == srcFrame->height)
    return ConvertNotNeeded;

  // we could do this with api, but we need avfilter anyways
  if (isHardware) {
    if (_p->filterGraph) {
      qWarning() << "hwdownload filter should be used when filtering";
      return ConvertError;
    }

    if (!_p->transferFrame) _p->transferFrame = av_frame_alloc();

    int err;
    if ((err = av_hwframe_transfer_data(_p->transferFrame, _p->frame, 0)) < 0) {
      AV_CRITICAL("hw frame transfer failed");
      return ConvertError;
    }

    srcFrame = _p->transferFrame;
  }

  fmt = AV_PIX_FMT_BGR24;
  if (_options.gray) fmt = AV_PIX_FMT_YUV420P;

  if (_p->scaler == nullptr) {
    if (!av_pix_fmt_desc_get(AVPixelFormat(srcFrame->format))) {
      int err = 0;
      AV_CRITICAL("invalid pixel format in AVFrame");
      return ConvertError;
    }

    // area filter seems the best for downscaling (indexing)
    // - faster than bicubic with fewer artifacts
    // - less artifacts than bilinear
    // fast-bilinear produces artifacts if heights differ
    int fastFilter = SWS_AREA;
    int fw = srcFrame->width;
    int fh = srcFrame->height;
    if (w >= fw || h > fh) {
      if (fh == h)
        fastFilter = SWS_FAST_BILINEAR;
      else
        fastFilter = SWS_BILINEAR;
    }
    int filter = _options.fast ? fastFilter : SWS_BICUBIC;

    const char* filterName = "other";
    switch (filter) {
      case SWS_AREA:
        filterName = "area";
        break;
      case SWS_BILINEAR:
        filterName = "bilinear";
        break;
      case SWS_FAST_BILINEAR:
        filterName = "fast-bilinear";
        break;
      case SWS_BICUBIC:
        filterName = "bicubic";
        break;
    }

    // all of this to supress a warning:
    // "deprecated pixel format..."
    // which requires us to use a different one and set the color range to full
    int srcFmt = srcFrame->format;
    static constexpr struct {
      int deprecated;
      int compatible;
    } deprecatedFormats[] = {
        {AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P}, {AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUV420P},
        {AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUV422P}, {AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUV440P},
        {AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUV444P},
    };
    for (auto& d : deprecatedFormats)
      if (d.deprecated == srcFmt) {
        srcFmt = d.compatible;
        break;
      }

    qDebug() << av_get_pix_fmt_name(AVPixelFormat(srcFmt))
             << QString("@%1x%2").arg(srcFrame->width).arg(srcFrame->height) << "=>"
             << av_get_pix_fmt_name(AVPixelFormat(fmt)) << QString("@%1x%2").arg(w).arg(h)
             << filterName << (_options.fast ? "fast" : "");

    _p->scaler = sws_getContext(srcFrame->width, srcFrame->height, AVPixelFormat(srcFmt), w, h,
                                AVPixelFormat(fmt), filter, nullptr, nullptr, nullptr);
    if (!_p->scaler) {
      qCritical() << "failed to allocate sw scaler";
      return ConvertError;
    }

    // libswscale > release/7.1
    // _p->scaler->opaque = (void*) logContext();

    if (srcFmt != srcFrame->format) {
      if (_p->context->color_range != AVCOL_RANGE_JPEG)
        qWarning() << "full-range colorspace is not enabled in codec";

      const int* srcTable = sws_getCoefficients(_p->context->colorspace);
      const int* dstTable = sws_getCoefficients(AVCOL_SPC_RGB);
      if (0 > sws_setColorspaceDetails(_p->scaler, srcTable, 1, dstTable, 1, 0, 1 << 16, 1 << 16))
        qWarning() << "full-range colorspace could not be enabled in scaler";
    }

    int size = av_image_alloc(_p->scaled.data, _p->scaled.linesize, w, h, AVPixelFormat(fmt), 16);
    if (size < 0) qFatal("av_image_alloc failed: %d", size);
  }

  sws_scale(_p->scaler, srcFrame->data, srcFrame->linesize, 0, srcFrame->height, _p->scaled.data,
            _p->scaled.linesize);

  return ConvertOK;
}

QString& VideoContext::avLogFile() {
  static QString logFile;
  return logFile;
}

QMutex* VideoContext::avLogMutex() {
  static QMutex mutex;
  return &mutex;
}

void VideoContext::avLoggerSetLogFile(const QString& path) {
  QMutexLocker locker(avLogMutex());
  avLogFile() = path;
}

QString VideoContext::avLoggerGetLogFile() {
  QMutexLocker locker(avLogMutex());
  QString file = avLogFile();
  return file;
}

void VideoContext::avLoggerWriteLogLine(const QString& context, const QString& message) {
  static bool failedLogFile = false;
  static bool firstTime = true;
  static QSet<QString> repeats;

  QString logFile = VideoContext::avLoggerGetLogFile();

  if (!logFile.isEmpty() && !failedLogFile) {
    QFile file(logFile);
    if (!file.open(QFile::WriteOnly | QFile::Append)) {
      failedLogFile = true;
      qWarning() << "failed to open ffmpeg log file" << file.errorString();
    }
    if (firstTime) {
      firstTime = false;
      qInfo() << "<MAG>logging video errors to:<PATH>" << logFile;
      QString line = "opening log file: " + QDateTime::currentDateTime().toString() + '\n';
      file.write(qUtf8Printable(line));
    }
    QString line = context + ':' + ' ' + message + '\n';
    QMutexLocker locker(avLogMutex());
    if (repeats.contains(line)) return;
    repeats.insert(line);
    file.write(qUtf8Printable(line));
  }
}

bool VideoContext::frameToQImg(QImage& img) {
  int w, h, fmt;

  const AVFrame* srcFrame = _p->filterFrame ? _p->filterFrame : _p->frame;
  int status = convertFrame(w, h, fmt, srcFrame);

  if (status == ConvertOK)
    avImgToQImg(_p->scaled.data, _p->scaled.linesize, w, h, img, AVPixelFormat(fmt));
  else if (status == ConvertNotNeeded)
    avFrameToQImg(*srcFrame, img);
  else
    return false;

#ifdef AV_FRAME_FLAG_KEY
  bool isKey = (_p->frame->flags & AV_FRAME_FLAG_KEY) || _p->frame->pict_type == AV_PICTURE_TYPE_I;
#else
  bool isKey = _p->frame->key_frame || _p->frame->pict_type == AV_PICTURE_TYPE_I;
#endif

  img.setText("isKey", QString::number(isKey));

  auto conv = av_mul_q(_p->videoStream->time_base, _p->videoStream->r_frame_rate);
  auto pts = av_make_q(_p->frame->best_effort_timestamp, 1);
  int frameNumber = av_q2d(av_mul_q(pts, conv));

  img.setText("frame", QString::number(frameNumber));

  const char* formatName = av_get_pix_fmt_name(AVPixelFormat(_p->frame->format));
  if (formatName) {
    img.setText("format", formatName);
    _metadata.pixelFormat = formatName;
  }

  return true;
}

QString VideoContext::rotationFilter() {
  bool turned = (std::abs((int) _metadata.rotation) / 90) & 1;
  QString sizeMod = turned ? ":out_w=ih:out_h=iw" : "";
  QString filter = qq("rotate=%1*PI/180%2").arg(-_metadata.rotation).arg(sizeMod);
  return filter;
}

bool VideoContext::decodeFrameFiltered() {
  bool ok = decodeFrame();

#if 0
  // if this changes we should *probably* rebuild the filter graph,
  // in practice it does not seem to matter, or even use more memory?
  if (ok && _p->hwFilter && _p->filterGraph && _p->frame->hw_frames_ctx) {
    if (_p->hwFramesContext != _p->frame->hw_frames_ctx->data) {
      qDebug() << "hw frames context changed!!" << _p->hwFramesContext
                 << _p->frame->hw_frames_ctx->data << _p->context->hw_frames_ctx->data;
      av_frame_free(&_p->filterFrame);
      avfilter_graph_free(&_p->filterGraph);
      _p->filterSource = nullptr;
      _p->filterSink = nullptr;
    }
  }
#endif

  // this has to be done here since we don't have hw_frames_ctx until after decodeFrame()
  if (ok && _p->hwFilter && !_p->filterGraph) {
    // should be non-null if we decoded a frame, but sometimes not (vaapi)
    if (!_p->context->hw_frames_ctx) return false;

    AVHWFramesContext* fc = (AVHWFramesContext*) _p->context->hw_frames_ctx->data;
    qDebug() << "hw_frames_ctx:" << fc->width << fc->height << av_get_pix_fmt_name(fc->format)
             << av_get_pix_fmt_name(fc->sw_format) << fc->initial_pool_size;

    QString filters = qq("hwdownload,format=%1").arg(fc->sw_format);
    if (_options.maxH && _options.maxW) {
      // scaling high-res frames is very intensive on CPU and somewhat defeats the purpose of hwdec!
      if (fc->format == AV_PIX_FMT_QSV) {
        // qsv scaling is horrible at low sizes (at least on coffeelake), so first scale to something larger,
        // this will blur the result slightly and require more CPU usage, but dcthash is not sensitive to blur
        float factor = _p->context->height / _options.maxH;
        if (factor > 8)
          filters = qq("scale_qsv=w=-1:h=ih/8:mode=hq,") + filters;
        else
          filters = qq("scale_qsv=w=%1:h=%2:mode=hq,").arg(_options.maxW).arg(_options.maxH)
                    + filters;
      }
      // else if (fc->format == AV_PIX_FMT_D3D11) {
      // prototype d3d11 scaler from dash; does not work with quicksync devices
      // filters = qq("scale_d3d11=width=%1:height=%2,").arg(_options.maxW).arg(_options.maxH)
      // + filters;
      // }
      else if (fc->format == AV_PIX_FMT_VAAPI) {
        // same qsv problem on Linux; if amd is ok in this regard should be an option
        float factor = _p->context->height / _options.maxH;
        if (factor > 8)
          filters = qq("scale_vaapi=w=-1:h=ih/8:mode=hq,") + filters;
        else
          filters = qq("scale_vaapi=w=%1:h=%2:mode=hq,").arg(_options.maxW).arg(_options.maxH)
                    + filters;
      } else if (fc->format == AV_PIX_FMT_VULKAN) {
        filters = qq("scale_vulkan=w=%1:h=%2,").arg(_options.maxW).arg(_options.maxH) + filters;
      } else if (fc->format == AV_PIX_FMT_VIDEOTOOLBOX) {
        filters = qq("scale_vt=w=%1:h=%2,").arg(_options.maxW).arg(_options.maxH) + filters;
      } else {
        qWarning() << "no hardware scaler for" << av_get_pix_fmt_name(fc->format)
                   << "expect extremely poor performance";
      }

      if (_metadata.rotation) filters += "," + rotationFilter();
    }
    qDebug() << "using hw avfilter:" << filters;
    av_log_set_level(AV_LOG_TRACE);
    ok = initFilters(qPrintable(filters));
    av_log_set_level(AV_LOG_INFO);
    if (!ok) {
      qCritical("filter setup failure");
      return false;
    }
  }

  if (ok && !_p->filterGraph && _metadata.rotation) {
    QString filter = rotationFilter();
    ok = initFilters(qPrintable(filter));
    if (!ok) return false;
  }

  // test sw filter graph
  if (ok && !_p->filterGraph && getenv("CBIRD_SW_FILTER")) {
    av_log_set_level(AV_LOG_TRACE);
    ok = initFilters(getenv("CBIRD_SW_FILTER"));
    av_log_set_level(AV_LOG_INFO);
    if (!ok) return false;
  }

  if (!_p->filterGraph) return ok;

  // qDebug() << "filtering" << _p->filterGraph << _p->filterSource << _p->filterSink;
  while (ok) { // FIXME: ok==false we stil have flush the graph...
    _p->frame->pts = _p->frame->best_effort_timestamp;
    _p->frame->time_base = _p->videoStream->time_base;

    // if (_p->frame->hw_frames_ctx) {
    // auto* ctx = (const AVHWFramesContext*) _p->frame->hw_frames_ctx->data;
    // qDebug() << "<CYN>" << ctx << _p->frame->hw_frames_ctx->buffer << ctx->width << ctx->height
    // << av_get_pix_fmt_name(ctx->sw_format) << ctx->initial_pool_size;
    // }

    int err = av_buffersrc_add_frame_flags(_p->filterSource, _p->frame, AV_BUFFERSRC_FLAG_PUSH);
    if (err < 0) {
      AV_CRITICAL("feeding filtergraph");
      return false;
    }

    err = av_buffersink_get_frame(_p->filterSink, _p->filterFrame);
    if (err == AVERROR(EAGAIN)) {
      AV_CRITICAL("buffersink_get_frame");
      ok = decodeFrame();
    } else if (err == AVERROR_EOF) {
      AV_CRITICAL("buffersink_get_frame");
      ok = false;
    } else {
      return true;
    }
  }
  return ok;
}

bool VideoContext::nextFrame(QImage& outQImg) {
  bool gotFrame = decodeFrameFiltered();
  if (!gotFrame) return false;

  if (!frameToQImg(outQImg)) return false;

  if (_p->filterGraph) // we already called decodeEnd() when consuming decoded frame
    av_frame_unref(_p->filterFrame);

  return true;
}

bool VideoContext::nextFrame(cv::Mat& outImg) {
  bool gotFrame = decodeFrameFiltered();
  if (!gotFrame) return false;

  int w, h, fmt;

  const AVFrame* srcFrame = _p->filterFrame ? _p->filterFrame : _p->frame;
  int status = convertFrame(w, h, fmt, srcFrame);
  if (status == ConvertOK)
    avImgToCvImg(_p->scaled.data, _p->scaled.linesize, w, h, outImg, AVPixelFormat(fmt));
  else if (status == ConvertNotNeeded)
    avFrameToCvImg(*srcFrame, outImg);
  else
    return false;

  if (_p->filterGraph) av_frame_unref(_p->filterFrame);

  return true;
}

float VideoContext::pixelAspectRatio() const {
  if (_p->sar > 0.0) return _p->sar;

  _p->sar = float(av_q2d(av_guess_sample_aspect_ratio(_p->format, _p->videoStream, _p->frame)));
  if (_p->sar == 0.0f) {
    AV_DEBUG("no SAR given, assuming 1.0");
    _p->sar = 1.0f;
  }
  return _p->sar;
}

QString VideoContext::Metadata::toString(bool styled) const {
  QString fmt;
  if (styled)
    fmt =
        "<span class=\"time\">%1</span> "
        "<span class=\"video\">%2fps %3%9 @ %4k</span> "
        "<span class=\"audio\">%5khz %6ch %7 @ %8k</span>";
  else
    fmt = "%1 %2fps %3%9 @ %4k / %5khz %6ch %7 @ %8k";

  return QString(fmt)
      .arg(timeDuration().toString("mm:ss"))
      .arg(double(frameRate))
      .arg(videoCodec)
      .arg(videoBitrate / 1000)
      .arg(sampleRate / 1000)
      .arg(channels)
      .arg(audioCodec)
      .arg(audioBitrate / 1000)
      .arg(videoProfile.isEmpty() ? qq("") : qq(" (") + videoProfile + ")");
}

void VideoContext::Metadata::toMediaAttributes(Media& media) const {
  media.setAttribute("duration", QString::number(duration));
  media.setAttribute("fps", QString::number(double(frameRate)));
  media.setAttribute("time", timeDuration().toString("h:mm:ss"));
  media.setAttribute("vformat", toString());
  media.setAttribute("datetime", creationTime.toString());
}

VideoContext::DecodeOptions::DecodeOptions() {}
