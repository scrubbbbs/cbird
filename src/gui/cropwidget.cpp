/* Get a rectangle selection from an image
   Copyright (C) 2022 scrubbbbs
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
#include "cropwidget.h"

#include "../cvutil.h"
#include "../database.h"

#include "opencv2/core.hpp"
#include "exiv2/exiv2.hpp"

bool CropWidget::setIndexThumbnail(const Database &db, const Media &media, QWidget *parent,
                                   bool async, const std::function<void(bool)> &after) {
  CropWidget w(media.loadImage(), true, parent);
  w.show();
  while (w.isVisible()) qApp->processEvents();

  const QImage thumb = w.image();
  if (thumb.isNull()) return false;

  const QRect cropRect = w.cropRect();

  const QString thumbPath = QFileInfo(db.thumbPath()).absoluteFilePath();
  const QString indexPath = QFileInfo(db.path()).absoluteFilePath();

  auto saveFunc = [media, thumb, thumbPath, indexPath, async, after, cropRect]() {
    const char *exifKey = "Exif.Photo.UserComment";

    // preserve existing comment in case we recrop a thumbnail
    QString comment;
    if (QFileInfo(thumbPath).exists()) {
      auto fn = Media::propertyFunc(QString("exif#") + exifKey);
      QVariant v = fn(media);
      if (!v.isNull()) {
        qInfo() << thumbPath << "preserving exif comment";
        comment = v.toString();
      }
    }

    QImage img = thumb;
    if (img.width() > 1024 || img.height() > 1024) {
      cv::Mat cvImg;
      qImageToCvImgNoCopy(thumb, cvImg);
      sizeLongestSide(cvImg, 1024);
      cvImgToQImageNoCopy(cvImg, img);
    }

    bool ok = img.save(thumbPath, nullptr, 0);
    if (!ok) qWarning() << thumbPath << "png compression failed";

    try {
      Media::print(media);

      // store info about the original in the thumbnail
      // this is not really needed for local files, fdct/orb will find them
      // for external files (e.g. URLs) we need a reference back to it
      std::unique_ptr<Exiv2::Image> image = Exiv2::ImageFactory::open(qPrintable(thumbPath));
      if (!image.get()) throw std::logic_error("exiv2 open failed");

      QString relPath = media.path();
      if (relPath.startsWith(indexPath)) relPath = relPath.mid(indexPath.length() + 1);

      image->readMetadata();
      if (comment.isEmpty()) {
        comment = "cbird thumbnail";
        comment += "\nversion:1";
        comment += "\npath:" + relPath;
        comment += QString("\ncrop:%1:%2:%3:%4")
                       .arg(cropRect.x())
                       .arg(cropRect.y())
                       .arg(cropRect.width())
                       .arg(cropRect.height());

        QString hash = media.md5();
        if (!hash.isEmpty()) {
          comment += "\nid:" + QString::number(media.id());
          comment += "\nmd5:" + hash;
        }
        uint64_t dct = media.dctHash();
        if (dct) comment += "\ndct:" + QString::number(dct, 16);

        if (media.matchRange().dstIn >= 0)
          comment += "\nframe:" + QString::number(media.matchRange().dstIn);
      }

      // image->setComment(qPrintable(comment)); // redundant
      Exiv2::CommentValue value(qPrintable(comment));
      image->exifData()["Exif.Photo.UserComment"].setValue(&value);
      image->writeMetadata();
      qDebug() << "wrote metadata" << thumbPath;
    } catch (std::exception &e) {
      qWarning() << e.what();
    }

#ifdef Q_OS_UNIX
    // remove xdg thumbnail cache entries..shouldn't be required
    // but seems to be needed in my case (krusader)
    const auto url = QUrl::fromLocalFile(QFileInfo(thumbPath).canonicalFilePath());
    const auto bytes = url.toString(QUrl::FullyEncoded).toLocal8Bit();
    auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex();
    QString cacheDir = qEnvironmentVariable("XDG_CACHE_HOME", "$HOME/.cache/thumbnails");
    QString flushCacheCmd = QString("rm -v \"%1/\"*/%2.png").arg(cacheDir).arg(hash);
    if (0 == system(qPrintable(flushCacheCmd))) qInfo() << "thumbnail cache flushed";

    if (async) after(ok);
#endif
    return ok;
  };

  if (async)
    (void)QtConcurrent::run(saveFunc);
  else
    return saveFunc();

  return true;
}

CropWidget::CropWidget(const QImage &img_, bool fullscreen, QWidget *parent)
    : QLabel(parent, Qt::Popup) {
  // draw the image in device pixels...for high dpi displays and small images this
  // could be a problem, but it guarantees the output hasn't been scaled (besides the crop)
  // note it will still be scaled if too large to fit the screen, and
  // setIndexThumbnail() will scale to some maximum size
  const qreal dpr = this->devicePixelRatio();

  QRect geom;                    // size/position of widget
  cv::Mat rescaled;              // note: img is nocopy ref to cvImg when scaling
  QImage img = img_;             // temporary until draw

  img.setDevicePixelRatio(dpr);  // trying to force unscaled drawing

  if (fullscreen) {
    QRect r = this->window()->screen()->availableGeometry();
    geom = QRect(r.topLeft(), r.size() * dpr);

    const int iw = img.width(), ih = img.height();
    const int gw = geom.width(), gh = geom.height();
    if (iw > gw || ih > gh) {
      int h = gh;
      int w = h * iw / ih;
      if (w > gw) {
        w = gw;
        h = w * ih / iw;
      }
      // TODO: we can pull crop rect from original so no quality loss!
      //       don't need to display 1:1 pixels if that's the case...
      qWarning() << "scaling input to fit window, expect quality loss" << QSize(w, h);
      qImageToCvImgNoCopy(img_, rescaled);
      sizeStretch(rescaled, w, h);  // lanczos4
      cvImgToQImageNoCopy(rescaled, img);
      img.setDevicePixelRatio(dpr);
      _imageScale = float(img_.width())/w;
    }
  } else if (parent) {
    auto r = parent->geometry();
    geom = QRect(r.topLeft() * dpr, r.size() * dpr);
  } else
    geom = img.rect();

  qDebug() << "DPR :"
           << "display:" << dpr << "input:" << img.devicePixelRatio();
  qDebug() << "GEOM:"
           << "display:" << geom << "input:" << img.rect();

  {
    cv::Mat mat;
    qImageToCvImgNoCopy(img, mat);
    brightnessAndContrastAuto(mat, mat);
  }

  // draw image into black background, allows cropping past the edge
  _background = QPixmap(geom.size());
  _background.setDevicePixelRatio(dpr);

  {
    const int iw = img.width(), ih = img.height();
    const int gw = geom.width(), gh = geom.height();
    const int x = (gw - iw) / 2, y = (gh - ih) / 2;
    const QRect fgRect(x, y, iw, ih), bgRect(0, 0, gw, gh);
    const qreal idpr = 1.0 / dpr;

    _cropOffset.setX(x);
    _cropOffset.setY(y);

    QPainter painter(&_background);
    painter.scale(idpr, idpr);  // makes our images unscaled!

    painter.fillRect(bgRect, _BG_COLOR);
    painter.setOpacity(_BG_OPACITY);
    painter.drawImage(fgRect, img);
    this->setPixmap(_background);  // widget background is faded image

    painter.setOpacity(1.0);
    painter.fillRect(bgRect, _BG_COLOR);
    painter.drawImage(fgRect, img);  // _background is now foreground
  }

  //img.save("test1.png", nullptr, 0);
  //pixmap().toImage().save("test2.png", nullptr, 0);
  //_background.toImage().save("test3.png", nullptr, 0);
  //_image = img;

  qDebug() << img.size() << _background.size() << this->size();

  setCursor(QCursor(Qt::CrossCursor));
  setMargin(0);
  setFrameShape(QFrame::NoFrame);
  setFixedSize(_background.size() / this->devicePixelRatio());

  _selectLabel = new QLabel(this);
  _selectLabel->setAttribute(Qt::WA_NoSystemBackground); // don't draw background
  _selectLabel->setMargin(0);
  _selectLabel->setFrameShape(QFrame::NoFrame);

  // need at least 1px border and transparent frame to avoid artifacts
  _selectLabel->setStyleSheet(R"qss(
      QLabel {
        border: 1px solid rgba(0,0,0,0);
      })qss");

  _selectLabel->hide();
  _dragging = false;

  move(geom.topLeft());
}

void CropWidget::setConstraint(bool enable, int num, int den) {
  _constrain = enable;
  _aspect_num = num;
  _aspect_den = den;
  if (_constrain) _selection.setHeight(_aspect_den * _selection.width() / _aspect_num);

  repaintSelection();
}

void CropWidget::repaintSelection() {
  QRect r = _selection.intersected(this->rect());  // don't overflow
  _selectLabel->setGeometry(r);

  // subtract the border width per stylesheet
  r.adjust(1, 1, -1, -1);

  // convert to device coordinates since _background is device pixels
  qreal dpr = this->devicePixelRatio();
  r = QRect(r.topLeft() * dpr, r.size() * dpr);

  QPixmap pix = _background.copy(r);
  {
    QPainter p(&pix);
    p.scale(1/dpr, 1/dpr);
    p.setPen(Qt::gray);
    p.drawRect(QRect{0,0,r.width()-1,r.height()-1});
  }
  _selectLabel->setPixmap(pix);
  _selectLabel->show();

  _cropRect.setTopLeft(r.topLeft()-_cropOffset);
  _cropRect.setRect(_cropRect.x()*_imageScale,
                    _cropRect.y()*_imageScale,
                    r.width()*_imageScale,
                    r.height()*_imageScale);
}

void CropWidget::keyPressEvent(QKeyEvent *ev) {
  switch (ev->key()) {
    case Qt::Key_Control:
      setCursor(QCursor(Qt::SizeAllCursor));
      return;
    case Qt::Key_C:
      setConstraint(!_constrain);
      return;
    case Qt::Key_1:
      setConstraint(true, 4, 3);
      return;
    case Qt::Key_2:
      setConstraint(true, 16, 9);
      return;
    case Qt::Key_3:
      setConstraint(true, 5, 4);
      return;
    case Qt::Key_R: {
      _selection = _selection.transposed();
      setConstraint(true, _aspect_den, _aspect_num);
      return;
    }
  }
  _image = QImage();
  hide();
}

void CropWidget::keyReleaseEvent(QKeyEvent *ev) {
  if (ev->key() == Qt::Key_Control) {
    setCursor(QCursor(Qt::CrossCursor));
    return;
  }
}

void CropWidget::mousePressEvent(QMouseEvent *ev) {
  _lastMousePos = ev->pos();
  if (ev->button() == Qt::RightButton) {
    setCursor(QCursor(Qt::SizeAllCursor));

    return;
  }
  if (ev->buttons() != Qt::LeftButton) return;
  _dragging = true;
  _selection.setSize(QSize(1, 1));
  _selection.setTopLeft(_lastMousePos);
  _selection.setBottomRight(_lastMousePos + QPoint(1, 1));
}

void CropWidget::mouseMoveEvent(QMouseEvent *ev) {
  if (!_dragging) return;

  const QPoint center = _selection.center();
  const QPoint mouseDelta = (ev->pos() - _lastMousePos);
  _lastMousePos = ev->pos();

  if (ev->buttons() == Qt::LeftButton && ev->modifiers() == Qt::NoModifier) {
    QRect tmp = _selection;
    tmp.setBottomRight(tmp.bottomRight() + mouseDelta * 2);  // x2 keeps cursor close to center
    if (_constrain) tmp.setHeight(_aspect_den * tmp.width() / _aspect_num);
    _selection = tmp;
  }

  if ((ev->buttons() == (Qt::LeftButton | Qt::RightButton)) ||
      (ev->buttons() == Qt::LeftButton && ev->modifiers() == Qt::ControlModifier))
    _selection.moveCenter(center + mouseDelta);

  repaintSelection();
}

void CropWidget::mouseReleaseEvent(QMouseEvent *ev) {
  if (!_dragging) return;
  if (ev->button() == Qt::RightButton) {
    setCursor(QCursor(Qt::CrossCursor));
    return;
  }
  if (ev->button() != Qt::LeftButton) return;

  _selectLabel->hide();
  _dragging = false;

  QRect r = _selection.intersected(this->rect());
  qreal dpr = this->devicePixelRatio();
  r = QRect(r.topLeft() * dpr, r.size() * dpr);

  _image = _background.copy(r).toImage();
  hide();
}
