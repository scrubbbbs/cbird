/* Delegate for MediaGroup-based Views
   Copyright (C) 2024 scrubbbbs
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
#include "mediaitemdelegate.h"
#include "mediapage.h"
#include "theme.h"
#include "../qtutil.h"

#include "opencv2/imgproc/imgproc.hpp"

MediaItemDelegate::MediaItemDelegate(QAbstractItemView* parent)
    : QAbstractItemDelegate(parent) {
  _filters.push_back({-1, "Qt"});
  _filters.push_back({cv::INTER_LINEAR, "Linear"});
  _filters.push_back({cv::INTER_AREA, "Area"});
  _filters.push_back({cv::INTER_CUBIC, "Cubic"});
  _filters.push_back({cv::INTER_LANCZOS4, "Lanczos"});
  _filters.push_back({cv::INTER_NEAREST, "Nearest"});

  _debug = QProcessEnvironment::systemEnvironment().contains("DEBUG_LAYOUT");
}

void MediaItemDelegate::setPage(const MediaPage* page) {
  _page = page;
  _avgItemRatio = _page->avgAspect();
}

void MediaItemDelegate::calculate(const QRect& imgRect,
                                  const QRect& itemRect_,
                                  const double dpr,
                                  double& scale,
                                  QRect& dstRect,
                                  QTransform& i2v) const {
  // hidpi: scale viewport rect by dpr, scale painter by 1/dpr
  // makes 100% scale == true pixels
  const QRect itemRect = QRect(itemRect_.topLeft() * dpr, itemRect_.size() * dpr);

  double sw = double(itemRect.width()) / imgRect.width();
  double sh = double(itemRect.height()) / imgRect.height();

  // if we want actual pixels fix the scale factor,
  // zoom still works but starts from here
  scale = _scaleMode == SCALE_NONE ? 1.0 : qMin(sw, sh);

  // do not scale up small images to better see size differences
  // fixme: add mode to see relative sizes when all images
  // are bigger than the viewport
  if (_scaleMode == SCALE_DOWN && scale > 1.0) scale = 1.0;

  // zoom ranges from 0.1 - 1.0 with 1.0 being no scaling
  // the division makes bigger jumps towards the top end
  scale /= _zoom;

  double hw = imgRect.width() / 2.0;
  double hh = imgRect.height() / 2.0;

  // dst rect is centered and doesn't go outside the box
  double dx = itemRect.x() + itemRect.width() / 2.0 - (scale * hw);
  double dy = itemRect.y() + itemRect.height() / 2.0 - (scale * hh);
  double dw = imgRect.width() * scale;
  double dh = imgRect.height() * scale;
  dstRect = QRect(dx, dy, dw, dh);
  dstRect = dstRect.intersected(itemRect);

  // pan gets less sensitive at higher scales
  // fixme: doesn't stay on center
  double px = _pan.x() / scale;
  double py = _pan.y() / scale;

  i2v.translate(dstRect.width() / 2.0, dstRect.height() / 2.0);
  i2v.scale(scale, scale);
  i2v.translate(-hw + px, -hh + py);
}

void MediaItemDelegate::paint(QPainter* painter,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
  painter->save();
  painter->setFont(Theme::instance().font());

  auto* parent = dynamic_cast<const QAbstractItemView*>(option.widget);
  Q_ASSERT(parent);

  const QPalette& palette = parent->palette();

  Q_ASSERT(_page);
  const Media& m = _page->group[index.row()];

  const QRect tightRect = painter->fontMetrics().tightBoundingRect(qq("Ay"));
  const int topInfoHeight = tightRect.height() + SPACING * 2;

  QRect rect = option.rect.adjusted(0, topInfoHeight, 0, -_textHeight);

  // draw image
  if (rect.height() > MIN_IMAGE_HEIGHT) {
    const QImage& full = m.image();

    QTransform i2v; // image-to-viewport transform
    QRect dstRect;  // destination paint rectangle (viewport coordinates)
    double scale;   // scale factor for scale-to-fit
    const qreal dpr = parent->devicePixelRatioF();

    const QRect fullRect = !full.isNull() ? full.rect() : QRect(0, 0, m.width(), m.height());
    calculate(fullRect, rect, dpr, scale, dstRect, i2v);

    if (_debug) {
      painter->save();
      painter->setPen(Qt::green);
      painter->drawRect(option.rect);
      painter->setPen(Qt::cyan);
      painter->drawRect(rect);

      painter->scale(1.0 / dpr, 1.0 / dpr);
      painter->setPen(Qt::red);
      painter->drawRect(dstRect);

      painter->translate(dstRect.topLeft());
      painter->setTransform(i2v, true);
      painter->setPen(Qt::yellow);
      painter->drawRect(full.rect());
      painter->restore();
    }

    // total scale from source image to viewport, to select filter
    double totalScale = scale;
    bool isRoi = false;
    double rotation = 0.0;

    if (m.roi().count() > 0) {
      if (!_page->isPair())
        qWarning("need pair for transform display");
      else {
        isRoi = true;

        // align with template by calculating new transform
        // from m.transform()

        // the template image is the other one
        int tmplIndex = (index.row() + 1) % index.model()->rowCount();

        const QRect& tmplRect = _page->group[tmplIndex].image().rect();

        QTransform tx;
        calculate(tmplRect, rect, dpr, scale, dstRect, tx);

        // m.transform() is from template to m.image(),
        // tx is from template to viewport so with the inversion we get
        // m->template->viewport
        i2v = QTransform(m.transform()).inverted() * tx;

        // to confirm the mapping is right, draw the outline
        if (_debug) {
          painter->setPen(Qt::yellow);
          painter->drawRect(dstRect);
        }
        // get accurate scale for the filters
        QPointF p1 = i2v.map(QPointF(0, 0));
        QPointF p2 = i2v.map(QPointF(1, 0));
        QPointF p3 = p2 - p1;
        totalScale = sqrt(p3.x() * p3.x() + p3.y() * p3.y());

        // rotation angle is nice to know
        rotation = qRotationAngle(i2v);
      }
    }

    int filterIndex;
    if (totalScale == 1.0)
      filterIndex = _equalFilter;
    else if (totalScale < 1.0)
      filterIndex = _minFilter;
    else
      filterIndex = _magFilter;

    int filterId = _filters[filterIndex].id;

    // fill with grey to show what parts are missing
    // if (isRoi) painter->fillRect(dstRect, Qt::gray);

    painter->save();
    painter->scale(1.0 / dpr, 1.0 / dpr);

    if (full.isNull()) {
      // draw outline of image to show it is loading
      // we may not know what the dimensions are so we can't always do it
      if (fullRect.height() > 0) painter->fillRect(dstRect, QBrush(Qt::darkGray, Qt::FDiagPattern));
    } else if (filterId == -1) {
      painter->setRenderHint(QPainter::SmoothPixmapTransform);

      // this is slower, only use if there is a rotation
      if (i2v.isRotating()) {
        painter->setClipRect(dstRect);
        painter->translate(dstRect.topLeft());
        painter->setTransform(i2v, true);
        painter->drawImage(full.rect(), full);
      } else {
        QRectF srcRect = i2v.inverted().mapRect(QRectF(0, 0, dstRect.width(), dstRect.height()));
        painter->drawImage(dstRect, full, srcRect);
      }
    } else {
      Q_ASSERT(!full.isNull()); // opencv exception/segfault
      // OpenCV scaling
      cv::Mat cvImg;
      qImageToCvImgNoCopy(full, cvImg);

      // note: OpenCV uses CCW rotation, so swap 21,11
      double mat[2][3] = {{i2v.m11(), i2v.m21(), i2v.dx()}, {i2v.m12(), i2v.m22(), i2v.dy()}};

      cv::Mat xForm(2, 3, CV_64FC(1), mat);

      cv::Mat subImg;
      cv::warpAffine(cvImg,
                     subImg,
                     xForm,
                     cv::Size(dstRect.width(), dstRect.height()),
                     filterId,
                     cv::BORDER_CONSTANT);

      QImage qImg;
      cvImgToQImageNoCopy(subImg, qImg);
      painter->drawImage(dstRect.topLeft(), qImg);
    }
    painter->restore();

    // draw info about the image display (scale factor, mode, filter etc)
    QString info = QString("%1%%5 | %2 | %3 %4")
                       .arg(int(totalScale * 100))
                       .arg(_scaleMode == SCALE_NONE ? "1:1"
                            : _scaleMode == SCALE_UP ? "+"
                                                     : "-")
                       .arg(_filters[filterIndex].name)
                       .arg(isRoi ? QString("| %1\xC2\xB0").arg(rotation, 0, 'f', 1) : "")
                       .arg(_zoom < 1.0 ? QString("[x%1]").arg(int(1.0 / _zoom)) : "");

    rect = option.rect;
    rect.setHeight(topInfoHeight);
    painter->setOpacity(Theme::INFO_OPACITY);
    painter->setPen(palette.text().color());
    painter->drawText(rect, info, Qt::AlignCenter | Qt::AlignVCenter);
    painter->setOpacity(1.0);

    const ColorDescriptor& cd = m.colorDescriptor();
    if (cd.numColors > 0) {
      painter->save();
      rect = option.rect;
      int xOffset = HISTOGRAM_PADDING;
      int yOffset = topInfoHeight;
      painter->translate(rect.x() + xOffset, rect.y() + yOffset);

      int totalWeight = 1; // prevent divide-by-zero
      for (int i = 0; i < cd.numColors; i++)
        totalWeight += cd.colors[i].w;

      int x = 0;
      int y = 0;

      for (int i = 0; i < cd.numColors; i++) {
        const DescriptorColor& dc = cd.colors[i];
        QColor rgb = dc.toQColor();
        int w = HISTOGRAM_SIZE;
        int h = int(dc.w) * (rect.height() - topInfoHeight - _textHeight) / totalWeight;

        painter->fillRect(x, y, w, h, rgb);
        painter->drawLine(x + w, y + h, x + w + 2, y + h);
        y += h;
      }
      painter->restore();
    } // histogram
  }   // image

  rect = option.rect;
  rect = rect.adjusted(0, std::max(0, rect.height() - _textHeight), 0, 0);

  QString title = index.data(Qt::UserRole + 0).toString();
  int cut = title.indexOf(qq(" [x")); // cut title to only elide the filename part
  QString fileName = title.mid(0, cut);
  QString info = title.mid(cut);
  int infoWidth = painter->fontMetrics().tightBoundingRect(info).width();
  title = painter->fontMetrics().elidedText(fileName,
                                            ELIDE_FILENAME,
                                            rect.width() - infoWidth,
                                            0);
  title = title + info;
  title = title.mid(0, title.lastIndexOf(lc('('))); // remove separately styled suffix

  QString text = index.data(Qt::DisplayRole).toString();
  text = text.replace("@title@", title);
  text = text.replace("@width@", QString::number(rect.width()));

  Theme::instance().drawRichText(painter, rect, text);

  if (option.state & QStyle::State_Selected) {
    QBrush selBrush = palette.highlight();
    QColor c = selBrush.color();
    c.setAlpha(Theme::SELECTION_OPACITY * 255);
    selBrush.setColor(c);
    painter->fillRect(rect, c);
  }

  if (_debug) {
    painter->setPen(Qt::magenta);
    painter->drawRect(rect);
  }

  painter->restore();
}

QSize MediaItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const {
  (void) index;

  auto* parent = dynamic_cast<const QAbstractItemView*>(option.widget);

  // all items are the same size
  // estimate of ideal number of rows/columns to
  // maximize icon size and prevent scrollbars
  const QSize& viewSize = parent->frameRect().size();

  // const int scrollbarWidth =
  //     option.widget->style()->pixelMetric(QStyle::PM_ScrollBarExtent,nullptr,parent->verticalScrollBar());
  const int textHeight = _textHeight;

  int numCols = 0, numRows = 0;

  Q_ASSERT(_page != nullptr);
  const int itemCount = _page->count();

  // try all combinations to max icon size and minimize empty space
  // - only runs once per layout since we use uniformItemSizes()
  // - average aspect ratio of images determines if we favor more rows or column
  double minWasted = DBL_MAX;
  double maxUsed = DBL_MIN;

  for (int nRows = 1; nRows <= itemCount; ++nRows)
    for (int nCols = 1; nCols <= itemCount; ++nCols)
      if ((nRows * nCols) >= itemCount) {
        // estimate w/o scrollbar since it shouldn't be visible
        const double fw = (viewSize.width() - SPACING * (nCols + 1))
                          / double(nCols); // full item w/h
        const double fh = (viewSize.height() - SPACING * (nRows + 1)) / double(nRows);

        const double iw = (viewSize.width() - SPACING * (nCols + 1)) / double(nCols); // image w/h
        const double ih = (viewSize.height() - textHeight * nRows - SPACING * (nRows + 1))
                          / double(nRows);
        double itemAspect = iw / ih;

        if (iw < 0 || ih < 0) continue;

        int emptyCount = nRows * nCols - itemCount;

        double sw, sh;
        if (_avgItemRatio < itemAspect) {
          sh = ih;
          sw = sh * _avgItemRatio;
        } else {
          sw = iw;
          sh = sw / _avgItemRatio;
        }

        int iconArea = sw * sh * itemCount;

        int emptyArea = (iw * ih * itemCount) - iconArea + (fw * fh * emptyCount);

        if (emptyArea < minWasted && iconArea >= maxUsed) {
          minWasted = emptyArea;
          maxUsed = iconArea;
          numCols = nCols;
          numRows = nRows;
        }
      }

  // fixme: should probably be minimum that forces scrollbar
  // if (ih < 32 || iw <32) continue;

  // sanity check
  if (numRows < 1) numRows = 1;
  if (numCols < 1) numCols = 1;

  if (numRows == 1) numCols = itemCount;
  if (numCols == 1) numRows = itemCount;

  // todo: we want to force 1-row in some situations, make it a toggle/option
  // possible options:
  // Layout:
  //   - Automatic
  //   - Force 1 Row
  //   - Prefer More Rows than Columns
  //   - Prefer More Columns than Rows
  if (numRows > 1 && itemCount < 4) {
    numRows = 1;
    numCols = itemCount;
  }

  // fixme: cannot seem to tell what the true spacing, add extra to prevent scrollbar
  // - there is additional unknown space on the right besides the scrollbar
  // - we shouldn't have to subtract scrollbarWidth, unless forcing a minimum
  QSize hint((viewSize.width() - SPACING * (numCols + 2)) / numCols,
             (viewSize.height() - SPACING * (numRows + 2)) / numRows);

  if (_debug) qInfo() << numCols << "x" << numRows << hint;

  return hint;
}
