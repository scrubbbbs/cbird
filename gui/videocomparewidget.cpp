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

#include <inttypes.h>

#include "../qtutil.h"
#include "../cimgops.h"

/// decode one frame in a thread
static bool loadFrameFunc(VideoContext* context, QImage* outImg) {
  VideoContext::DecodeOptions opt;
  opt.rgb = 1;
  if (!context->nextFrame(*outImg)) return false;
  return true;
}

/// decode thread output
class Frame {
 public:
  int frame = 0;
  int quality = -1;
  QImage image;
  uint64_t hash = 0;
  int uses = 0;
};

/// retain some decoded frames, but not too much
class FrameCache {
 public:
  FrameCache(const Media& m) {
    MessageContext mctx(m.path().split("/").last());
    VideoContext::DecodeOptions opt;
    opt.rgb = true;
    opt.threads = QThread::idealThreadCount();
    _ctx.open(m.path(), opt);
    _curPos = 0;
    _end = _ctx.metadata().duration * _ctx.metadata().frameRate;

    Frame f;
    f.frame = 0;
    f.uses = 1;
    Q_ASSERT(loadFrameFunc(&_ctx, &f.image));
    _curPos++;
    _cache[0] = f;
    _emptyFrame.image =
        QImage(_ctx.width(), _ctx.height(), QImage::Format_RGB888);
    _emptyFrame.image.fill(0xFF0000);
  }

  ~FrameCache() {}

  enum { Forward = 1, Backward = 2 };

  void releaseMemory(int pos) {
    (void)pos;

    auto keys = _cache.keys();
    std::sort(keys.begin(), keys.end());

    int frameSize =
        _emptyFrame.image.bytesPerLine() * _emptyFrame.image.height();
    int bytes = keys.count() * frameSize;

    qDebug("mem usage = %d MB", bytes / 1024 / 1024);

    int maxBytes = 512 * 1024 * 1024;

    if (bytes < maxBytes) return;

    int toDelete = (bytes - maxBytes) / frameSize;

    class Cand {
     public:
      int key;
      int dist;
      bool operator<(const Cand& a) const { return dist > a.dist; }
    };

    // cache is full, find some frames to nuke
    for (int uses = 0; uses < 10; uses++) {
      QVector<Cand> cand;

      // find the distance of level X frames from a higher-level frame (our
      // candidates) the ones that are furthest away get deleted first
      for (int i = 0; i < keys.count(); i++) {
        int k = keys[i];
        cand.append({k, abs(pos - k)});
      }

      std::sort(cand.begin(),cand.end());

      for (auto c : cand) {
        qDebug("delete frame pos=%d dist=%d uses=%d done=%d", c.key, c.dist,
               uses, toDelete);
        _cache.remove(c.key);
        toDelete--;
        if (toDelete < 0) return;
      }

      keys = _cache.keys();
      std::sort(keys.begin(), keys.end());
    }
  }

  Frame* frame(int pos) {
    MessageContext mctx(_ctx.path().split("/").last());

    if (_cache.contains(pos)) {
      Frame& f = _cache[pos];
      f.uses++;
      return &f;
    }

    releaseMemory(pos);

    if (pos >= 0 && pos < _end) {
      if (pos != _curPos) {
        VideoContext::DecodeOptions opt;
        opt.rgb = 1;

        // when seeking forward, the nearest frames behind pos
        // might never be needed. Same with seeking backward.
        // however, if playing backward, probably want all of them
        // to be cached.
        QVector<QImage> decoded;
        if (_ctx.seek(pos, opt, &decoded)) {
          for (int i = 0; i < decoded.count(); i++) {
            // note: will maybe overwrite stuff already in cache
            Frame f;
            f.uses = 0;
            f.image = decoded[i];
            f.frame = pos - decoded.count() + i;

            if (!_cache.contains(f.frame)) _cache[f.frame] = f;
          }

          qInfo("seeked to %d", pos);
          _curPos = pos;
        } else
          return &_emptyFrame;
      }

      Frame f;
      f.frame = pos;
      f.uses = 1;

      if (loadFrameFunc(&_ctx, &f.image)) {
        qInfo("decode frame @ %d", pos);

        _cache[f.frame] = f;
        _curPos = pos + 1;

        return &_cache[pos];
      }
    }

    return &_emptyFrame;
  }

  const VideoContext& ctx() const { return _ctx; }

 private:
  VideoContext _ctx;

  int _curPos, _end;
  Frame _emptyFrame;

  QHash<int, int> _mru;
  QHash<int, Frame> _cache;
  QFuture<Frame*> _thread;
};

VideoCompareWidget::VideoCompareWidget(const Media& left, const Media& right,
                                       const MatchRange& range, QWidget* parent,
                                       Qt::WindowFlags f)
    : QWidget(parent, f), _left(left), _right(right), _range(range) {

  if (_range.srcIn < 0) {
    _range.srcIn = 0;
    _range.dstIn = 0;
  }

  _leftFrames = new FrameCache(left);
  _rightFrames = new FrameCache(right);

  QString prefix = Media::greatestPathPrefix({_left, _right});

  _leftLabel = left.path().mid(prefix.length());
  _rightLabel = right.path().mid(prefix.length());

  setWindowTitle("Compare Videos: " + prefix);

  setStyleSheet(
      "VideoCompareWidget { "
      "  background-color: #000; "
      "  font-size: 16px; "
      "  color: white; "
      "}");

  _maximized = WidgetHelper::restoreGeometry(this);

  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(this->metaObject()->className());
  _interleaved = settings.value("interleaved", false).toBool();
  settings.endGroup();

  settings.beginGroup(this->metaObject()->className() + QString(".shortcuts"));

  WidgetHelper::addAction(settings, "Play/Pause", Qt::Key_Space, this,
                          [&]() { _scrub = _scrub ? 0 : 1; repaint();});
  WidgetHelper::addAction(settings, "Play Backward", Qt::SHIFT|Qt::Key_Space, this,
                          [&]() { _scrub = -1; repaint();});

  WidgetHelper::addAction(settings, "Goto Start", Qt::Key_Home, this,
                          [&]() { loadFrameIfNeeded(0); repaint();});
  WidgetHelper::addAction(settings, "Goto End", Qt::Key_End, this,
                          [&]() { loadFrameIfNeeded(_range.len-1); repaint();});

  WidgetHelper::addAction(settings, "Forward", Qt::Key_Right, this,
                          [&]() { loadFrameIfNeeded(_selectedFrame + 1); repaint();});
  WidgetHelper::addAction(settings, "Backward", Qt::Key_Left, this,
                          [&]() { loadFrameIfNeeded(_selectedFrame - 1); repaint();});
  WidgetHelper::addAction(settings, "Skip Forward", Qt::Key_Down, this,
                          [&]() { loadFrameIfNeeded(_selectedFrame + 30); repaint();});
  WidgetHelper::addAction(settings, "Skip Backward", Qt::Key_Up, this,
                          [&]() { loadFrameIfNeeded(_selectedFrame - 30); repaint();});
  WidgetHelper::addAction(settings, "Jump Forward", Qt::Key_PageDown, this,
                          [&]() { loadFrameIfNeeded(_selectedFrame + 300); repaint();});
  WidgetHelper::addAction(settings, "Jump Backward", Qt::Key_PageUp, this,
                          [&]() { loadFrameIfNeeded(_selectedFrame - 300); repaint(); });

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Offset +1", Qt::SHIFT|Qt::Key_Right, this,
                          [&]() { shiftFrames(1); repaint(); });
  WidgetHelper::addAction(settings, "Offset -1", Qt::SHIFT|Qt::Key_Left, this,
                          [&]() { shiftFrames(-1); repaint(); });
  WidgetHelper::addAction(settings, "Offset +30", Qt::SHIFT|Qt::Key_Down, this,
                          [&]() { shiftFrames(30); repaint(); });
  WidgetHelper::addAction(settings, "Offset -30", Qt::SHIFT|Qt::Key_Up, this,
                          [&]() { shiftFrames(-30); repaint(); });
  WidgetHelper::addAction(settings, "Offset +300", Qt::SHIFT|Qt::Key_PageDown, this,
                          [&]() { shiftFrames(+300); repaint(); });
  WidgetHelper::addAction(settings, "Offset -300", Qt::SHIFT|Qt::Key_PageUp, this,
                          [&]() { shiftFrames(-300); repaint(); });

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Toggle Scaling", Qt::Key_S, this,
                          [&]() { _sameSize=!_sameSize; repaint(); });
  WidgetHelper::addAction(settings, "Toggle Interleave", Qt::Key_I, this,
                          [&]() { _interleaved=!_interleaved; repaint(); });
  WidgetHelper::addAction(settings, "Swap Sides", Qt::Key_R, this,
                          [&]() { _swap=!_swap; repaint(); });
  WidgetHelper::addAction(settings, "Toggle Crop A", Qt::Key_BracketLeft, this,
                          [&]() { _cropLeft=!_cropLeft; repaint(); });
  WidgetHelper::addAction(settings, "Toggle Crop B", Qt::Key_BracketRight, this,
                          [&]() { _cropRight=!_cropRight; repaint(); });

  WidgetHelper::addAction(settings, "Zoom In", Qt::Key_9, this,
                          [&]() { _zoom = qMin(_zoom+0.1, 0.9); repaint();});
  WidgetHelper::addAction(settings, "Zoom Out", Qt::Key_7, this,
                          [&]() { _zoom = qMin(_zoom-0.1, 0.9); repaint();});
  WidgetHelper::addAction(settings, "Zoom Reset", Qt::Key_5, this,
                          [&]() { _zoom = 0; repaint();});

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Align Temporally", Qt::Key_A, this,
                          [&]() { alignTemporally(); });
  WidgetHelper::addAction(settings, "Align Spatially", Qt::Key_Z, this,
                          [&]() { alignSpatially(); });
  WidgetHelper::addAction(settings, "Quality Score", Qt::Key_Q, this,
                          [&]() { findQualityScores(); });
  WidgetHelper::addAction(settings, "Cycle Quality Visual", Qt::Key_V, this, [&]() {
    if (_leftQualityVisual.count() > 0) {
      _visualIndex++;
      _visualIndex %= _leftQualityVisual.count() + 1;
      repaint();
    }
  });

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Play Side-by-Side", Qt::Key_P, this,
                          [&]() { playSideBySide(); });

  WidgetHelper::addAction(settings, "Close", Qt::CTRL|Qt::Key_W, this, SLOT(close()));
  WidgetHelper::addAction(settings, "Close (Alt)", Qt::Key_Escape, this, SLOT(close()));

  setContextMenuPolicy(Qt::ActionsContextMenu);
}

VideoCompareWidget::~VideoCompareWidget() {
  WidgetHelper::saveGeometry(this);

  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(this->metaObject()->className());
  settings.setValue("interleaved", _interleaved);

  delete _leftFrames;
  delete _rightFrames;
}

void VideoCompareWidget::loadFrameIfNeeded(int frame) {
  _selectedFrame = frame;
}

static void drawFrame(QPainter& painter, const FrameCache& cache,
                      const QImage& img, int iw,
                      int ih,                     // frame image and scaled size
                      int matchIn, int matchLen,  // in+len of match range
                      int currPos,  // current position, relative to matchIn
                      const QString& text, int x, int y, int w,
                      int h)  // x,y,w,h location
{
  (void)w;  // w is == iw in this case, so unused

  int infoMargin = 10;
  int infoHeight = 130;

  // ensure space for bottom text
  if (ih > h - infoHeight) {
    float aspect = float(iw) / ih;
    int ow = iw;
    ih = h - infoHeight;
    iw = int(ih * aspect);
    x += (ow - iw) / 2;
  }

  QPoint ip(x, y + ((h - ih - infoHeight) / 2));
  painter.drawImage(QRect(ip.x(), ip.y(), iw, ih), img);

  // range indicator
  float numFrames =
      cache.ctx().metadata().duration * cache.ctx().metadata().frameRate;

  painter.fillRect(
      QRect(ip.x() + (matchIn / numFrames * iw), ip.y() + ih,
            std::min((matchLen * iw) / numFrames, (float)iw), infoMargin),
      Qt::darkGray);

  float pos = (matchIn + currPos) * iw / numFrames;

  painter.drawLine(ip.x() + pos, ip.y() + ih, ip.x() + pos,
                   ip.y() + ih + infoMargin);

  WidgetHelper::drawRichText(
      &painter, QRect(ip.x(), ip.y() + ih + infoMargin, iw, infoHeight), text);
}

void VideoCompareWidget::paintEvent(QPaintEvent* event) {
  (void)event;

  QPainter painter(this);

  QElapsedTimer timer;
  timer.start();

  QFuture<Frame*> work[2] = {
      QtConcurrent::run(_leftFrames, &FrameCache::frame,
                        _range.srcIn + _selectedFrame + _frameOffset),
      QtConcurrent::run(_rightFrames, &FrameCache::frame,
                        _range.dstIn + _selectedFrame)};

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

  const Frame& leftFrame = *work[0].result();
  const Frame& rightFrame = *work[1].result();

  int dist = 0;

  QImage leftImage, rightImage;
  if (_visualFrame == _selectedFrame + _frameOffset && _visualIndex > 0 &&
      _leftQualityVisual.count() > 0 &&
      (_visualIndex - 1) < _leftQualityVisual.count()) {
    leftImage = _leftQualityVisual[_visualIndex - 1];
    rightImage = _rightQualityVisual[_visualIndex - 1];
  } else {
    leftImage = leftFrame.image;
    rightImage = rightFrame.image;
  }

  if (_cropLeft) {
    cv::Mat cvImg;
    qImageToCvImg(leftImage, cvImg);
    autocrop(cvImg, 10);
    cvImgToQImage(cvImg, leftImage);
  }
  if (_cropRight) {
    cv::Mat cvImg;
    qImageToCvImg(rightImage, cvImg);
    autocrop(cvImg, 10);
    cvImgToQImage(cvImg, rightImage);
  }

  const VideoContext::Metadata& lmd = _leftFrames->ctx().metadata();
  const VideoContext::Metadata& rmd = _rightFrames->ctx().metadata();

  QString leftText = QString::asprintf(
      "<div class=\"default\">A: %s<br/>%s<br/>%dx%d (%.2f) %.2f<br/>In:[%d%+d] "
      "Out:[%d]<br/>Hash:%" PRIx64 "",
      qPrintable(_leftLabel), qPrintable(lmd.toString(true)), leftImage.width(),
      leftImage.height(), _leftFrames->ctx().aspect(), _leftFrames->ctx().fps(),
      _range.srcIn + _selectedFrame, _frameOffset,
      _range.len > 0 ? _range.len : int(lmd.duration * lmd.frameRate),
      leftFrame.hash);

  QString rightText = QString::asprintf(
      "<div class=\"default\">B: %s<br/>%s<br/>%dx%d (%.2f) %.2f <br/>In:[%d] Out:[%d]<br/>Hash:%" PRIx64
      " (%d)",
      qPrintable(_rightLabel), qPrintable(rmd.toString(true)),
      rightImage.width(), rightImage.height(), _rightFrames->ctx().aspect(),
      _rightFrames->ctx().fps(), _range.dstIn + _selectedFrame,
      _range.len > 0 ? _range.len : int(rmd.duration * rmd.frameRate),
      rightFrame.hash, dist);

  if (leftFrame.quality != -1) {
    leftText += "<br/>Q:" + QString::number(leftFrame.quality);
    rightText += "<br/>Q:" + QString::number(rightFrame.quality);
  }

  if (!leftImage.text("description").isNull()) {
    leftText.append("(" + leftImage.text("description") + ")");
    rightText.append("(" + rightImage.text("description") + ")");
  }

  leftText += "</div>";
  rightText += "</div>";

  QRect geom = this->geometry();

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

    leftImage = crop(leftImage);
    rightImage = crop(rightImage);
  }

  if (_interleaved) {
    int lw = geom.width();
    int lh = (leftImage.height() * lw) / leftImage.width();

    int rw = lw;
    int rh = _sameSize ? lh : (rightImage.height() * rw) / rightImage.width();

    if (!_swap)
      drawFrame(painter, *_leftFrames, leftImage, lw, lh, _range.srcIn,
                _range.len, _selectedFrame + _frameOffset, leftText,
                _alignX * lw, _alignY * lh, geom.width(), geom.height());
    else
      drawFrame(painter, *_rightFrames, rightImage, rw, rh, _range.dstIn,
                _range.len, _selectedFrame, rightText, 0, 0, geom.width(),
                geom.height());
  } else {
    int lw = geom.width() / 2;
    int lh = (leftImage.height() * lw) / leftImage.width();

    int rw = lw;
    int rh = _sameSize ? lh : (rightImage.height() * rw) / rightImage.width();

    if (!_swap) {
      drawFrame(painter, *_leftFrames, leftImage, lw, lh, _range.srcIn,
                _range.len, _selectedFrame + _frameOffset, leftText,
                _alignX * lw, _alignY * lh, geom.width() / 2, geom.height());
      drawFrame(painter, *_rightFrames, rightImage, rw, rh, _range.dstIn,
                _range.len, _selectedFrame, rightText, geom.width() / 2, 0,
                geom.width() / 2, geom.height());
    } else {
      drawFrame(painter, *_rightFrames, rightImage, rw, rh, _range.dstIn,
                _range.len, _selectedFrame, rightText, 0, 0, geom.width() / 2,
                geom.height());
      drawFrame(painter, *_leftFrames, leftImage, lw, lh, _range.srcIn,
                _range.len, _selectedFrame + _frameOffset, leftText,
                geom.width() / 2 + _alignX * lw, _alignY * lh, geom.width() / 2,
                geom.height());
    }
  }

  if (_scrub) {
    if (_scrub + _selectedFrame < 0) _scrub = 0;

    loadFrameIfNeeded(_selectedFrame + _scrub);
    QTimer::singleShot(0, [=]() { repaint(); });
  }
}

void VideoCompareWidget::alignSpatially() {
  int64_t minSad = INT64_MAX;
  int minX = 0, minY = 0;

  const QImage& left =
      _leftFrames->frame(_range.srcIn + _selectedFrame + _frameOffset)->image;
  const QImage& right =
      _rightFrames->frame(_range.dstIn + _selectedFrame)->image;

  const QImage ri = left.scaled(256, 256);
  const QImage li = right.scaled(256, 256);

  for (int xOffset = -8; xOffset <= 8; ++xOffset)
    for (int yOffset = -8; yOffset <= 8; ++yOffset) {
      int64_t sad = 0;
      // todo: sum of absolute differences function
      for (int y = 8; y < 256 - 8; ++y) {
        const uchar* lp = li.constScanLine(y + yOffset) + 3 * 8 + 3 * xOffset;
        const uchar* rp = ri.constScanLine(y) + 3 * 8;
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

  repaint();
}

void VideoCompareWidget::alignTemporally() {
  int windowSize = 5;
  int64_t minSad = INT64_MAX;

  _frameOffset -= 30;
  int minOffset = _frameOffset;

  for (int forwardFrames = 0; forwardFrames < 60; forwardFrames++) {
    int64_t sad = 0;

    for (int i = 0; i < windowSize; i++) {
      const Frame& rightFrame =
          *_rightFrames->frame(_range.dstIn + _selectedFrame + i);
      const Frame& leftFrame =
          *_leftFrames->frame(_range.srcIn + _selectedFrame + _frameOffset + i);

      const QImage ri = rightFrame.image.scaled(128, 128);
      const QImage li = leftFrame.image.scaled(128, 128);

      Q_ASSERT(ri.format() == QImage::Format::Format_RGB888);

      // todo: sum of absolute differences function
      for (int y = 0; y < 128; ++y) {
        const uchar* lp = li.constScanLine(y);
        const uchar* rp = ri.constScanLine(y);
        for (int x = 0; x < 128; ++x) {
          sad += abs(int(lp[0]) - int(rp[0]));
          sad += abs(int(lp[1]) - int(rp[1]));
          sad += abs(int(lp[2]) - int(rp[2]));
          lp += 3;
          rp += 3;
        }
      }
    }

    sad /= windowSize;

    if (sad < minSad) {
      repaint();
      minOffset = _frameOffset;
      minSad = sad;
    }

    if (_range.srcIn + _selectedFrame + _frameOffset <= 0) break;

    _frameOffset++;
  }

  while (_frameOffset > minOffset)
    _frameOffset--;

  repaint();
}

void VideoCompareWidget::shiftFrames(int offset) { _frameOffset += offset; }

void VideoCompareWidget::wheelEvent(QWheelEvent* event) {
  // fixme: not getting reliable shift modifier (or with keyboardModifiers()
  const bool shift = qApp->queryKeyboardModifiers() & Qt::ShiftModifier;
  const int num = event->delta() > 0 ? -1 : 1;
  if (shift)
    shiftFrames(num);
  else
    loadFrameIfNeeded(_selectedFrame + num);
  repaint();
}

void VideoCompareWidget::findQualityScores() {
  Frame& left = *_leftFrames->frame(_range.srcIn + _selectedFrame + _frameOffset);
  Frame& right = *_rightFrames->frame(_range.dstIn + _selectedFrame);

  _visualFrame = _selectedFrame + _frameOffset;

  struct {
    int& quality;
    const QImage& qImage;
    QVector<QImage>& visual;
  } img[2] = {{left.quality, left.image, _leftQualityVisual},
              {right.quality, right.image, _rightQualityVisual}};

  for (int i = 0; i < 2; i++) {
    img[i].visual.clear();
    img[i].quality = qualityScore(Media(img[i].qImage), &img[i].visual);
    cv::Mat cvImg;
    qImageToCvImg(img[i].qImage, cvImg);
    brightnessAndContrastAuto(cvImg, cvImg);
    QImage qImg;
    cvImgToQImage(cvImg, qImg);
    qImg.setText("description", "Auto Contrast");
    img[i].visual.append(qImg);
  }

  repaint();
}

void VideoCompareWidget::playSideBySide() {
  float leftSeek =
      (_range.srcIn + _selectedFrame + _frameOffset) / _leftFrames->ctx().fps();
  float rightSeek = (_range.dstIn + _selectedFrame) / _rightFrames->ctx().fps();

  Media::playSideBySide(_left, leftSeek, _right, rightSeek);
}
