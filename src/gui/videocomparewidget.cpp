/* Side-by-side Video Display
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
#include "videocomparewidget.h"

#include "cropwidget.h"
#include "theme.h"

#include "../cimgops.h"
#include "../env.h"
#include "../nleutil.h"
#include "../qtutil.h"

#include "opencv2/core.hpp"

#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QFuture>
#include <QtCore/QSettings>
#include <QtCore/QThread>
#include <QtCore/QTimer>

#include <QtGui/QDesktopServices>
#include <QtGui/QPainter>
#include <QtGui/QWheelEvent>

#include <QtWidgets/QApplication>

/// frame cache entry
class Frame {
 public:
  int frame = 0;
  int quality = -1;
  QImage image;
  uint64_t hash = 0;
};

/// retain some decoded frames, but not too much
class FrameCache {
 public:
  FrameCache(const Media& m, float cacheSizeKb) {
    MessageContext mctx(m.path());
    VideoContext::DecodeOptions opt;
    opt.threads = QThread::idealThreadCount();

    _curPos = 0;
    _rateFactor = 1.0f;
    _keyInterval = 0;
    _lastKey = 0;
    _maxCacheSizeKb = cacheSizeKb;

    // read first frame to get real format for dummy frames
    // if we can't for some reason use dummy frame
    QImage firstFrame;
    if (_ctx.open(m.path(), opt) < 0) {
      _end = 1;
    } else {
      _end = _ctx.metadata().duration * _ctx.metadata().frameRate;
      if (!_ctx.nextFrame(firstFrame)) _end = 1;
    }

    if (firstFrame.isNull()) {
      firstFrame = QImage(16, 16, QImage::Format_RGB888);
      firstFrame.fill(_ERROR_COLOR);
    }

    _curPos++;
    cacheFrame(0, firstFrame);

    _errorFrame.image = firstFrame;
    _errorFrame.image.fill(_ERROR_COLOR);
    _oobFrame = _errorFrame;
    _oobFrame.image.fill(_OOB_COLOR);
  }

  int availableCache() const {
    const auto& img = _errorFrame.image;
    const float frameSize = img.sizeInBytes();
    const float bytes = _cache.count() * frameSize;

    Q_ASSERT(int(frameSize) > 0);
    return std::max(0.0f, _maxCacheSizeKb * 1024 - bytes - frameSize) / frameSize;
  }

  void allocFrames(int pos, int numFrames, QVector<QImage>& frames) {
    Q_ASSERT(numFrames > 0);
    int avail = availableCache();
    int toDelete = numFrames - avail;
    // qDebug("delete frames pos=%d want=%d have=%d", pos, numFrames, avail);

    if (toDelete <= 0) {
      frames.fill(QImage(), numFrames);
      return;
    }

    if (avail > 0) frames.fill(QImage(), avail);

    // recycle the furthest frames from pos
    auto keys = _cache.keys();
    std::sort(keys.begin(), keys.end(),
              [pos](int a, int b) { return abs(a - pos) > abs(b - pos); });

    // note: calls to malloc/free stop due to implict sharing of QImage,
    // so heap usage won't fluctuate (and image pixels won't be copied)
    for (auto k : qAsConst(keys)) {
      frames.append(_cache[k].image);
      _cache.remove(k);
      toDelete--;
      if (toDelete == 0) break;
    }
  }

  void cacheFrame(int pos, const QImage& img) {
    if (_cache.contains(pos)) return;
    // bool isKey = img.text("isKey").toInt();
    // qInfo("%d isKey=%d keyInt=%d", pos, isKey, _keyInterval);

    Frame f;
    f.image = img;
    f.frame = pos;
    _cache.insert(pos, f);
  }

  Frame* frame(int normalizedPos, bool scrub = false) {
    MessageContext mctx(_ctx.logContext());

    // pos is scaled to match videos with different rates, the
    // slower video returns cached frames (duplicates) as needed
    const int pos = normalizedPos * _rateFactor;

    if (_cache.contains(pos)) return &_cache[pos];

    if (pos >= 0 && pos < _end) {
      //
      // For backwards jumps, store the inter-frames, unless it is a big jump.
      // The amount we need is at most the maximum keyframe interval (aka gop size),
      // which we discover by seeking a few times.
      //
      // TODO: for intra-only codecs, seek before pos and decode a few frames
      //
      (void)scrub;
      int interFrames = 0;
      int jump = pos - _curPos;
      if (jump < 0 && (-jump < _keyInterval)) interFrames = _keyInterval;

      QVector<QImage> frames;
      allocFrames(pos, 1 + interFrames, frames);  // +1 for target frame

      QImage img = frames.takeLast();

      if (pos != _curPos) {
        if (!_ctx.seek(pos, &frames, &interFrames)) return &_errorFrame;

        // cache the inter-frames and park the unused ones
        int usedFrames = std::min(int(frames.count()), interFrames);
        int i;
        for (i = 0; i < usedFrames; ++i) cacheFrame(pos - usedFrames + i, frames[i]);
        for (; i < frames.count(); ++i) cacheFrame(INT_MAX - i, frames[i]);

        _curPos = pos;
        _keyInterval = std::max(_keyInterval, interFrames);
      }

      if (_ctx.nextFrame(img)) {
        cacheFrame(pos, img);
        _curPos = pos + 1;
        return &_cache[pos];
      }
    }

    return &_oobFrame;
  }

  void setRateFactor(const FrameCache& other) {
    if (other._ctx.fps() > _ctx.fps()) _rateFactor = _ctx.fps() / other._ctx.fps();
  }

  float rateFactor() const { return _rateFactor; }
  const VideoContext& ctx() const { return _ctx; }

 private:
  VideoContext _ctx;
  int _curPos, _end;             // position in decoder
  QHash<int, Frame> _cache;      // nearby frames
  Frame _errorFrame, _oobFrame;  // dummy frames
  float _rateFactor;             // multiply requested frame by this
  int _keyInterval, _lastKey;    // key interval detection
  float _maxCacheSizeKb;         // memory management

  const int _OOB_COLOR = 0x5050FF;
  const int _ERROR_COLOR = 0xFF5050;
};

VideoCompareWidget::VideoCompareWidget(const Media& left, const Media& right,
                                       const MatchRange& range, const MediaWidgetOptions& options,
                                       QWidget* parent)
    : super(parent), _options(options) {
  float totalKb, cacheKb;
  Env::systemMemory(totalKb, cacheKb);

  // use almost all available memory, helpful for 4k video
  // cacheKb = (cacheKb - 2 * 1024 * 1024) / 2;   // 1GB for other things
  // cacheKb = std::max(cacheKb, 512 * 1024.0f);  // 21 4k frames
  cacheKb = 1024 * 1024;

  _video[0].media = left;
  _video[0].side = "A";
  _video[0].in = range.srcIn >= 0 ? range.srcIn : 0;

  _video[1].media = right;
  _video[1].side = "B";
  _video[1].in = range.dstIn >= 0 ? range.dstIn : 0;

  // qWarning() << "range in:" << range.srcIn << range.dstIn << range.len;

  const QString prefix =
      Media::greatestPathPrefix({_video[0].media.path(), _video[1].media.path()});

  for (int i = 0; i < 2; ++i) {
    auto& v = _video[i];
    v.cache.reset(new FrameCache(v.media, cacheKb));
    v.label = v.media.path().mid(prefix.length());
    v.crop = false;
    v.meta = &v.cache->ctx().metadata();
    v.offset = 0;
    v.visualFrame = -1;
  }

  // sync different frame rates by scaling one of them
  for (int i = 0; i < 2; ++i) {
    _video[i].cache->setRateFactor(*_video[(i + 1) % 2].cache);
    _video[i].in /= _video[i].cache->rateFactor();
  }

  int matchLen = 0;
  if (range.len > 0) matchLen = range.len / _video[0].cache->rateFactor();  // len is in dst units

  Q_ASSERT(matchLen >= 0);

  // get max legal out frame between the two videos (shortest video duration)
  int maxOut = INT_MAX;
  for (int i = 0; i < 2; ++i) {
    _video[i].out =
        (_video[i].meta->duration * _video[i].meta->frameRate - 15) / _video[i].cache->rateFactor();
    maxOut = std::min(maxOut, _video[i].out);
  }

  // use the match len if we have it, otherwise shorted duration
  for (int i = 0; i < 2; ++i)
    _video[i].out = matchLen > 0 ? std::min(_video[i].in + matchLen, _video[i].out) : maxOut;

  // endPos is limit of cursor, which is relative to video.in (negative cursor is before inpoint)
  _endPos = std::min(_video[0].out - _video[0].in, _video[1].out - _video[1].in);

  // fps for positioning based on seconds, is the highest fps
  _fps = std::max(_video[0].cache->ctx().fps(), _video[1].cache->ctx().fps());

  setWindowTitle("Compare Videos: " + prefix);

  _maximized = WidgetHelper::restoreGeometry(this);

  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  const char* className = self::staticMetaObject.className();
  settings.beginGroup(className);
  _stacked = settings.value("stacked", false).toBool();
  settings.endGroup();

  settings.beginGroup(className + QString(".shortcuts"));

  WidgetHelper::addAction(settings, "Play/Pause", Qt::Key_Space, this, [&]() {
    _scrub = _scrub ? 0 : 1;
    update();
  });
  WidgetHelper::addAction(settings, "Play Backward", Qt::SHIFT | Qt::Key_Space, this, [&]() {
    _scrub = -1;
    update();
  });
  WidgetHelper::addAction(settings, "Goto Start", Qt::Key_Home, this, [&]() { seekFrame(0); });
  WidgetHelper::addAction(settings, "Goto End", Qt::Key_End, this,
                          [&]() { seekFrame(_endPos - 1); });
  WidgetHelper::addAction(settings, "Forward 1f", Qt::Key_Right, this,
                          [&]() { seekFrame(_cursor + 1); });
  WidgetHelper::addAction(settings, "Backward 1f", Qt::Key_Left, this,
                          [&]() { seekFrame(_cursor - 1); });
  WidgetHelper::addAction(settings, "Forward 1s", Qt::Key_Down, this, [&]() { skipSeconds(1); });
  WidgetHelper::addAction(settings, "Backward 1s", Qt::Key_Up, this, [&]() { skipSeconds(-1); });
  WidgetHelper::addAction(settings, "Forward 10s", Qt::CTRL | Qt::Key_Down, this,
                          [&]() { skipSeconds(10); });
  WidgetHelper::addAction(settings, "Backward 10s", Qt::CTRL | Qt::Key_Up, this,
                          [&]() { skipSeconds(-10); });
  WidgetHelper::addAction(settings, "Forward 1m", Qt::Key_PageDown, this,
                          [&]() { skipSeconds(60); });
  WidgetHelper::addAction(settings, "Backward 1m", Qt::Key_PageUp, this,
                          [&]() { skipSeconds(-60); });

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Offset +1f", Qt::SHIFT | Qt::Key_Right, this,
                          [&]() { offsetFrames(1); });
  WidgetHelper::addAction(settings, "Offset -1f", Qt::SHIFT | Qt::Key_Left, this,
                          [&]() { offsetFrames(-1); });
  WidgetHelper::addAction(settings, "Offset +1s", Qt::SHIFT | Qt::Key_Down, this,
                          [&]() { offsetSeconds(1); });
  WidgetHelper::addAction(settings, "Offset -1s", Qt::SHIFT | Qt::Key_Up, this,
                          [&]() { offsetSeconds(-1); });
  WidgetHelper::addAction(settings, "Offset +1m", Qt::SHIFT | Qt::Key_PageDown, this,
                          [&]() { offsetSeconds(+60); });
  WidgetHelper::addAction(settings, "Offset -1m", Qt::SHIFT | Qt::Key_PageUp, this,
                          [&]() { offsetSeconds(-60); });

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Toggle Scaling", Qt::Key_S, this, [&]() {
    _sameSize = !_sameSize;
    update();
  });
  WidgetHelper::addAction(settings, "Toggle Stacking", Qt::Key_I, this, [&]() {
    _stacked = !_stacked;
    update();
  });
  WidgetHelper::addAction(settings, "Swap Sides", Qt::Key_R, this, [&]() {
    _swap = !_swap;
    update();
  });
  WidgetHelper::addAction(settings, "Toggle Crop A", Qt::Key_BracketLeft, this, [&]() {
    _video[0].crop = !_video[0].crop;
    update();
  });
  WidgetHelper::addAction(settings, "Toggle Crop B", Qt::Key_BracketRight, this, [&]() {
    _video[1].crop = !_video[1].crop;
    update();
  });
  WidgetHelper::addAction(settings, "Zoom In", Qt::Key_9, this, [&]() {
    _zoom = qMin(_zoom + 0.1, 0.9);
    update();
  });
  WidgetHelper::addAction(settings, "Zoom Out", Qt::Key_7, this, [&]() {
    _zoom = qMin(_zoom - 0.1, 0.9);
    update();
  });
  WidgetHelper::addAction(settings, "Zoom Reset", Qt::Key_5, this, [&]() {
    _zoom = 0;
    update();
  });

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Align Temporally", Qt::Key_A, this,
                          [&]() { alignTemporally(); });
  WidgetHelper::addAction(settings, "Align Spatially", Qt::Key_Z, this,
                          [&]() { alignSpatially(); });
  WidgetHelper::addAction(settings, "Quality Score", Qt::Key_Q, this,
                          [&]() { findQualityScores(); });
  WidgetHelper::addAction(settings, "Cycle Quality Visual", Qt::Key_V, this, [&]() {
    const auto& v = _video[0].visual;
    if (v.count() > 0) {
      _visualIndex++;
      _visualIndex %= v.count() + 1;  // index 0==no visual
      update();
    }
  });

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Play Side-by-Side", Qt::Key_P, this,
                          [&]() { playSideBySide(); });

  WidgetHelper::addAction(settings, "Compare in Kdenlive", Qt::Key_K, this,
                          [&]() { compareInKdenlive(); });

  if (_options.db) {
    WidgetHelper::addAction(settings, "Thumbnail A", Qt::Key_H, this, [&]() { writeThumbnail(0); });
    WidgetHelper::addAction(settings, "Thumbnail B", Qt::Key_J, this, [&]() { writeThumbnail(1); });
  }

  WidgetHelper::addAction(settings, "Close", Qt::CTRL | Qt::Key_W, this, SLOT(close()));
  WidgetHelper::addAction(settings, "Close (Alt)", Qt::Key_Escape, this, SLOT(close()));

  setContextMenuPolicy(Qt::ActionsContextMenu);
}

VideoCompareWidget::~VideoCompareWidget() {
  WidgetHelper::saveGeometry(this);

  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(self::staticMetaObject.className());
  settings.setValue("stacked", _stacked);
}

void VideoCompareWidget::show() { Theme::instance().showWindow(this, _maximized); }

void VideoCompareWidget::drawFrame(QPainter& painter, const FrameCache& cache, const QImage& img,
                                   int iw,
                                   int ih,                     // frame image and scaled size
                                   int matchIn, int matchLen,  // in,len of match
                                   int currPos,  // current position, relative to matchIn
                                   const QString& text, int x, int y, int w,
                                   int h) const {  // x,y,w,h location
  (void)w;                                         // w is == iw in this case, so unused

  int infoMargin = 16;
  int infoHeight = 130;

  // make space for info text
  if (ih > h - infoHeight) {
    float aspect = float(iw) / ih;
    int ow = iw;
    ih = h - infoHeight;
    iw = int(ih * aspect);
    x += (ow - iw) / 2;
  }

  QPoint ip(x, y + ((h - ih - infoHeight) / 2));
  painter.drawImage(QRect(ip.x(), ip.y(), iw, ih), img);

  // range
  float numFrames =
      cache.ctx().metadata().duration * cache.ctx().metadata().frameRate / cache.rateFactor();

  const int cx = ip.x();
  const int cy = ip.y() + ih;

  painter.fillRect(
      QRect(cx + (matchIn / numFrames * iw), cy, matchLen / numFrames * iw, infoMargin),
      Qt::darkGray);

  // cursor
  {
    float pos = (matchIn + currPos) * iw / numFrames;
    const int half = infoMargin / 2;
    if (pos < 0) {
      painter.drawLine(cx + half, cy, cx, cy + half);
      painter.drawLine(cx, cy + half, cx + half, cy + infoMargin - 1);
    } else if (pos > iw) {
      painter.drawLine(cx + iw - half, cy, cx + iw, cy + half);
      painter.drawLine(cx + iw, cy + half, cx + iw - half, cy + infoMargin - 1);
    } else
      painter.drawLine(cx + pos, cy, cx + pos, cy + infoMargin - 1);
  }

  const int tx = cx;
  const int ty = h - infoHeight;
  Theme::instance().drawRichText(&painter, QRect(tx + infoMargin, ty + infoMargin, iw, infoHeight),
                                 text);
}

void VideoCompareWidget::paintEvent(QPaintEvent* event) {
  (void)event;

  QPainter painter(this);

  QElapsedTimer timer;
  timer.start();

  const auto& v = _video;
  bool showVisual = false;
  if (v[0].visualFrame == _cursor + v[0].in + v[0].offset && _visualIndex > 0 &&
      v[0].visual.count() > 0 && (_visualIndex - 1) < v[0].visual.count())
    showVisual = true;

  // decode frames
  QFuture<Frame*> work[2];
  for (int i = 0; i < 2; ++i)
    work[i] = (QtConcurrent::run(&FrameCache::frame, v[i].cache.get(),
                                 v[i].in + _cursor + v[i].offset, _scrub));

  // accurate seek is often slow due to interframe decoding,  show beach ball
  bool waitCursor = false;
  for (auto& w : work) {
    while (!waitCursor && !w.isFinished()) {
      if (timer.elapsed() > 100) {
        qApp->setOverrideCursor(QCursor(Qt::WaitCursor));
        waitCursor = true;
      }
      QThread::msleep(1);
    }
    if (waitCursor) w.waitForFinished();
  }
  if (waitCursor) qApp->restoreOverrideCursor();

  struct {
    const Frame* frame;
    QImage img;
    QString text;
  } pane[2];

  for (int i = 0; i < 2; ++i) {
    auto& v = _video[i];
    auto& p = pane[i];

    p.frame = work[i].result();
    p.img = showVisual ? v.visual.at(_visualIndex - 1) : p.frame->image;

    if (v.crop) {
      cv::Mat cvImg;
      qImageToCvImg(p.img, cvImg);
      autocrop(cvImg, 10);
      cvImgToQImage(cvImg, p.img);
    }

    if (_zoom > 0) {
      auto crop = [=](const QImage& img) {
        int mw = 0, mh = 0;
        int h = img.height(), w = img.width();
        if (h > w)
          mh = int(h * _zoom / 2);
        else
          mw = int(w * _zoom / 2);

        if (mh && (h - mh * 2) < w)
          mw = (w - (h - mh * 2)) / 2;
        else if (mw && (w - mw * 2) < h)
          mh = (h - (w - mw * 2)) / 2;

        return img.copy({mw, mh, w - mw * 2, h - mh * 2});
      };
      p.img = crop(p.img);
    }

    p.text = QString::asprintf(
        "<div class=\"default\">%s: %s<br/>%s<br/>%dx%d %s (sar=%.2f) "
        "<br/>In:[%d+%d+%d]=%d src={%d} "
        "Out:[%d]<br/>",
        qPrintable(v.side), qPrintable(v.label), qPrintable(v.meta->toString(true)),
        p.frame->image.width(), p.frame->image.height(), qPrintable(p.img.text("format")),
        v.cache->ctx().pixelAspectRatio(), v.in, _cursor, v.offset, v.in + _cursor + v.offset,
        p.img.text("frame").toInt(), v.out);

    if (p.frame->quality >= 0) p.text += "<br/>Q:" + QString::number(p.frame->quality);

    const QString desc = p.img.text("description");  // from quality score
    if (!desc.isNull()) p.text += "(" + desc + ")";

    p.text += "</div>";
  }

  QRect geom = this->geometry();

  // full width of widget or half (side-by-side)
  const int iw = _stacked ? geom.width() : geom.width() / 2;

  // left/right height only depends on width,
  // right can be scaled to match left
  const auto& p0 = pane[0];
  const auto& p1 = pane[1];
  const int lh = (p0.img.height() * iw) / p0.img.width();
  const int rh = _sameSize ? lh : (p1.img.height() * iw) / p1.img.width();

  const struct {
    int i, x, y, w, h;
  } setup[2] = {
      {0, int(_alignX * iw), int(_alignY * lh), iw, lh},
      {1, 0, 0, iw, rh},
  };

  for (int k = 0; k < 2; ++k) {
    const auto& s = !_swap ? setup[k] : setup[!k];
    const auto& v = _video[s.i];
    const auto& p = pane[s.i];
    drawFrame(painter, *v.cache, p.img, s.w, s.h, v.in, v.out - v.in, _cursor + v.offset, p.text,
              s.x + k * iw, s.y, iw, geom.height());

    if (_stacked) break;
  }

  if (_scrub) {
    moveCursor(_cursor + _scrub);
    QTimer::singleShot(0, [=]() { update(); });

    if (_cursor < 0 || _cursor > _endPos) _scrub = 0;
  }
}

void VideoCompareWidget::alignSpatially() {
  int64_t minSad = INT64_MAX;
  int minX = 0, minY = 0;

  QImage img[2];
  for (int i = 0; i < 2; ++i) {
    const auto& v = _video[i];
    img[i] = v.cache->frame(v.in + _cursor + v.offset)->image.scaled(256, 256);
  }

  for (int xOffset = -8; xOffset <= 8; ++xOffset)
    for (int yOffset = -8; yOffset <= 8; ++yOffset) {
      int64_t sad = 0;
      // TODO: refactor, also used by temporal alignment
      for (int y = 8; y < 256 - 8; ++y) {
        const uchar* lp = img[0].constScanLine(y + yOffset) + 3 * 8 + 3 * xOffset;
        const uchar* rp = img[1].constScanLine(y) + 3 * 8;
        for (int x = 0; x < 256 - 8 * 2; ++x) {
          sad += abs(int(lp[0]) - int(rp[0]));
          sad += abs(int(lp[1]) - int(rp[1]));
          sad += abs(int(lp[2]) - int(rp[2]));
          lp += 3;
          rp += 3;
        }
      }
      if (sad < minSad) {
        minSad = sad;
        minX = xOffset;
        minY = yOffset;
      }
    }

  _alignX = minX / 256.0f;
  _alignY = minY / 256.0f;
  qDebug() << _alignX << _alignY;

  update();
}

int64_t VideoCompareWidget::sad128(int i) const {
  QImage img[2];
  for (int j = 0; j < 2; ++j) {
    const auto& v = _video[j];
    img[j] = v.cache->frame(v.in + _cursor + v.offset + i)->image.scaled(128, 128);
    Q_ASSERT(img[j].format() == QImage::Format::Format_RGB888);
  }

  int64_t sad = 0;
  for (int y = 0; y < 128; ++y) {
    const uchar* lp = img[0].constScanLine(y);
    const uchar* rp = img[1].constScanLine(y);
    for (int x = 0; x < 128; ++x) {
      sad += abs(int(lp[0]) - int(rp[0]));
      sad += abs(int(lp[1]) - int(rp[1]));
      sad += abs(int(lp[2]) - int(rp[2]));
      lp += 3;
      rp += 3;
    }
  }
  return sad;
}

void VideoCompareWidget::alignTemporally() {
  const int windowSize = 5;
  auto& offset = _video[0].offset;  // offset the left side
  int minOffset = offset;

  int64_t minSad = 0;
  for (int i = 0; i < windowSize; i++) minSad += sad128(i);
  minSad /= windowSize;

  offset -= 30;

  for (int forwardFrames = 0; forwardFrames < 60; forwardFrames++) {
    int64_t sad = 0;
    for (int i = 0; i < windowSize; i++) sad += sad128(i);
    sad /= windowSize;

    if (sad < minSad) {
      minOffset = offset;
      minSad = sad;
      repaint();
    }
    offset++;
  }

  offset = minOffset;
  qDebug() << offset;

  update();
}

void VideoCompareWidget::wheelEvent(QWheelEvent* event) {
  // FIXME: not getting reliable shift modifier (or with keyboardModifiers()
  const bool shift = qApp->queryKeyboardModifiers() & Qt::ShiftModifier;
  const int num = event->angleDelta().y() > 0 ? -1 : 1;
  if (shift)
    offsetCursor(num);
  else
    moveCursor(_cursor + num);
  update();
}

void VideoCompareWidget::findQualityScores() {
  for (int i = 0; i < 2; i++) {
    auto& v = _video[i];
    v.visualFrame = v.in + v.offset + _cursor;

    Frame& f = *v.cache->frame(v.visualFrame);
    const QImage& img = f.image;

    v.visual.clear();
    f.quality = qualityScore(Media(img), &v.visual);
    cv::Mat cvImg;
    qImageToCvImg(img, cvImg);
    brightnessAndContrastAuto(cvImg, cvImg);
    QImage qImg;
    cvImgToQImage(cvImg, qImg);
    qImg.setText("description", "Auto Contrast");
    v.visual.append(qImg);
  }
  update();
}

void VideoCompareWidget::playSideBySide() {
  float seek[2];
  for (int i = 0; i < 2; ++i) {
    const auto& v = _video[i];
    seek[i] = (v.in + v.offset + _cursor) * v.cache->rateFactor() / v.cache->ctx().fps();
  }

  Media::playSideBySide(_video[0].media, seek[0], _video[1].media, seek[1]);
}

void VideoCompareWidget::compareInKdenlive() {
  const float templateFps = 29.97;  // TODO: read from template
  const QString templateFile = ":/res/template.kdenlive";
  KdenEdit edit(templateFile);

  for (int i = 0; i < 2; ++i) {
    const auto& v = _video[i];
    int inFrame = (v.in + v.offset + _cursor) * v.cache->rateFactor();  // native frames
    inFrame = inFrame * templateFps / v.cache->ctx().fps();             // template frames
    int p = edit.addProducer(_video[i].media.path());
    QString track = QString("Video ") + QString::number(i + 1);
    edit.addTrack(track);
    edit.addBlank(track, 150);
    edit.addClip(track, p, inFrame, inFrame + 300);
  }

  QString outFile = DesktopHelper::tempName("cbird.XXXXXX.kdenlive", this);
  edit.saveXml(outFile);
  QDesktopServices::openUrl(QUrl::fromLocalFile(outFile));
}

void VideoCompareWidget::writeThumbnail(int index) {
  Q_ASSERT(_options.db);
  const auto& v = _video[index];
  int frameNum = v.in + _cursor + v.offset;
  const Frame* frame = v.cache->frame(frameNum);
  Media m = v.media;
  m.setImage(frame->image);
  m.setMatchRange({-1, frameNum, 1});
  CropWidget::setIndexThumbnail(*_options.db, m, this);
}
