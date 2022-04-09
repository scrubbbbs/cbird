/* Grid display for list of MediaGroup (search results)
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
#include "mediagrouplistwidget.h"
#include "mediafolderlistwidget.h"
#include "videocomparewidget.h"

#include "../lib/jpegquality.h"
#include "../database.h"
#include "../env.h"
#include "../ioutil.h"
#include "../profile.h"
#include "../qtutil.h"
#include "../templatematcher.h"
#include "../videocontext.h"
#include "../cimgops.h"

#include <memory>

#define LW_MIN_FREE_MEMORY_KB (256*1024)
#define LW_MAX_CACHED_ROWS (5)

#define LW_PAN_STEP (10.0)
#define LW_ZOOM_IN_STEP (0.9)
#define LW_ZOOM_OUT_STEP (1.1)

#define LW_ITEM_SPACING (8)
#define LW_ITEM_MIN_IMAGE_HEIGHT (16)  // do not draw image below this
#define LW_ITEM_HISTOGRAM_PADDING (16) // distance from item edge
#define LW_ITEM_HISTOGRAM_SIZE (32)    // width of histogram plot
#define LW_ITEM_TITLE_FUZZ (24)        // fixme: unknown extra space needed for title text

static bool isDifferenceAnalysis(const Media& m) { return m.path().endsWith("-diff***"); }
static bool isAnalysis(const Media& m) { return m.path().endsWith("***"); }
static Media newDifferenceAnalysis() {
  // needs unique "path" for image loader, this is probably fine
  QString id = QString::number(nanoTime(), 16);
  Media m(id+"-diff***", Media::TypeImage);
  return m;
}
static int countNonAnalysis(const MediaGroup& group) {
  return std::count_if(group.begin(), group.end(), [](const Media& m) {
    return !isAnalysis(m);
  });
}

MediaFolderListWidget::MediaFolderListWidget(const MediaGroup& list,
                                             const MediaWidgetOptions& options,
                                             QWidget* parent)
    : super(parent), _list(list), _options(options) {
  setWindowTitle(
      QString("Group-List Set : %2 [x%1]").arg(_list.count()).arg(_options.basePath));

  setViewMode(QListView::IconMode);
  setResizeMode(QListView::Adjust);
  setMovement(QListView::Static);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setWrapping(true);
  setSpacing(LW_ITEM_SPACING);

  int iconW = 0;
  int iconH = 0;
  for (auto&  m : list) {
    iconW = std::max(iconW, m.image().width());
    iconH = std::max(iconH, m.image().height());
  }
  setIconSize({iconW,iconH});

  // todo: external stylesheet
  setStyleSheet(
      "QListWidget::item { "
        "margin: 0px; "
        "padding: 8px; "
      "}"
      "QListWidget::item:selected { "
        "margin: 0px; "
        "padding: 8px; "
        "background-color: #444; "
      "}"
      "QListWidget { "
        "background-color: black; "
        "selection-color: #FFF; " // text color
        "selection-background-color: #FF0; " // text bg, overriden by item bg
        "font-size: 16px; "
        "color: white; " // font color
      "}"
      "QScrollBar {"
        "width: 32px; "
        "background-color: black; "
        "color: darkGray; "
      "}");

  int index = 0;
  for (const Media& m : list) {
    // todo: using type() for list index is not necessary since
    // we have indexFromItem()
    QListWidgetItem* item = new QListWidgetItem(m.path(), nullptr, index++);
    item->setIcon(QIcon(QPixmap::fromImage(m.image())));
    addItem(item);
  }

//  if (_options.db) {
//    QAction* a = new QAction("Move to...", this);
//    a->setMenu(MenuHelper::dirMenu(_options.db->path(),this,
//                                   SLOT(moveFolderAction())));
//    addAction(a);
//  }

  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(this->metaObject()->className() + QString(".shortcuts"));

  WidgetHelper::addAction(settings, "Close Window", Qt::CTRL | Qt::Key_W,
                          this, SLOT(close()));
  WidgetHelper::addAction(settings, "Close Window (Alt)", Qt::Key_Escape,
                          this, SLOT(close()));
  WidgetHelper::addAction(settings, "Choose Selected", Qt::Key_Return,
                          this, SLOT(chooseAction()));

  setContextMenuPolicy(Qt::ActionsContextMenu);

  connect(this, &QListWidget::doubleClicked, this, &self::chooseAction);

  WidgetHelper::restoreGeometry(this);
}

MediaFolderListWidget::~MediaFolderListWidget() {
  WidgetHelper::saveGeometry(this);
}

void MediaFolderListWidget::close() {
  super::close();
  deleteLater();
}

void MediaFolderListWidget::closeEvent(QCloseEvent* event) {
  super::closeEvent(event);
  deleteLater();
}

void MediaFolderListWidget::chooseAction() {
  MediaGroup g = selectedMedia();
  if (!g.empty()) emit mediaSelected(g);
}

MediaGroup MediaFolderListWidget::selectedMedia() const {
  const QList<QListWidgetItem*> items = selectedItems();
  MediaGroup selected;
  for (auto& item : items)
    selected.append(_list[item->type()]);
  return selected;
}

//void MediaFolderListWidget::moveFolderAction() {
//}

/// Passed in/out of background jobs
struct ImageWork {
  Media media;          // copy of target
  QVector<Media> input; // copy of target
  int row=-1, index=-1; // row/index of the job (debugging)
  QFuture<void> future; // cancellation
  bool isReady = false; // cancellation
};

/// Filter for resizing images (bicubic, nearest, etc)
struct ScaleFilter {
  int id;
  QString name;
};

/// Custom painting and layout of list view items
class MediaItemDelegate : public QAbstractItemDelegate {
 public:
  MediaItemDelegate(MediaGroupListWidget* parent)
      : QAbstractItemDelegate(parent) {
    _filters.push_back({-1, "Qt"});
    _filters.push_back({cv::INTER_NEAREST, "Nearest"});
    _filters.push_back({cv::INTER_LINEAR, "Linear"});
    _filters.push_back({cv::INTER_AREA, "Area"});
    _filters.push_back({cv::INTER_CUBIC, "Cubic"});
    _filters.push_back({cv::INTER_LANCZOS4, "Lanczos"});

    _debug = QProcessEnvironment::systemEnvironment().contains("DEBUG_LAYOUT");
  }

  virtual ~MediaItemDelegate() {}

  void setAverageItemRatio(double ratio) { _avgItemRatio = ratio; }
  void setZoom(double zoom) { _zoom = zoom; }
  void setPan(const QPointF& pan) { _pan = pan; }
  void setTextHeight(int height) { _textHeight = height; }

  void toggleScaleToFit() { _scaleToFit = !_scaleToFit; }
  void toggleActualSize() { _actualSize = !_actualSize; }

  void cycleMinFilter() { _minFilter = (_minFilter + 1) % _filters.count(); }
  void cycleMagFilter() { _magFilter = (_magFilter + 1) % _filters.count(); }

 protected:
  /**
   * @brief Get the scale factor, destination rect, and
   * image-to-viewport transform for <full> to fit inside <rect>,
   * accounting for scale-to-fit and zoom/pan state
   *
   * @param imgRect   Full size (unscaled) image
   * @param itemRect  List item paint (sub) rectangle
   * @param scale     Scale-to-fit factor from image->viewport (ignoring zoom)
   * @param dstRect   Destination rectangle in item coordinates
   * @param i2v       Image-to-viewport transformation
   */
  void calculate(const QRect& imgRect, const QRect& itemRect, double& scale,
                 QRectF& dstRect, QTransform& i2v) const {

    double sw = double(itemRect.width()) / imgRect.width();
    double sh = double(itemRect.height()) / imgRect.height();
    scale = _actualSize ? 1.0 : qMin(sw, sh);

    // scale-to-fit mode disabled and magnification needed, limit to 100% scale
    if (!_scaleToFit && scale > 1.0) scale = 1.0;

    double x = (itemRect.width() - scale * imgRect.width()) / 2;
    double y = (itemRect.height() - scale * imgRect.height()) / 2;

    double px = _pan.x()/scale * _zoom;
    double py = _pan.y()/scale * _zoom;

    dstRect = QRectF(x, y,
                     imgRect.width() * scale, imgRect.height() * scale);

    i2v.translate(itemRect.width()/2,itemRect.height()/2);
    i2v.scale(scale, scale);

    i2v.scale(1.0 / _zoom, 1.0 / _zoom);
    i2v.translate(-imgRect.width() / 2 + px,
                  -imgRect.height() / 2 + py);
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const {

    auto* parent = dynamic_cast<const MediaGroupListWidget*>(option.widget);
    Q_ASSERT(parent);
    const auto* item = parent->item(index.row());
    Q_ASSERT(item != nullptr);

    const QPalette& palette = parent->palette();
    const Media& m = parent->_list[parent->_currentRow][index.row()];

    // offset rectangle for image
    QRect rect = option.rect.adjusted(0,0,0, -_textHeight);

    // draw image
    if (rect.height() > LW_ITEM_MIN_IMAGE_HEIGHT) {
      const QImage& full = m.image();

      QTransform i2v;  // image-to-viewport transform
      QRectF dstRect;  // destination paint rectangle (viewport coordinates)
      double scale;    // scale factor for scale-to-fit

      const QRect fullRect = !full.isNull() ? full.rect() : QRect(0,0,m.width(),m.height());
      calculate(fullRect, rect, scale, dstRect, i2v);

      if (_debug) {
        painter->setPen(Qt::cyan);
        painter->drawRect(rect);
        painter->setPen(Qt::red);
        painter->drawRect(dstRect.translated(rect.topLeft()));
      }

      // total scale from source image to viewport, to select filter
      double totalScale = scale / _zoom;
      bool isRoi = false;
      double rotation = 0.0;

      if (m.roi().count() > 0) {
        if (index.model()->rowCount() != 2)
          qWarning("item count must be 2 for transform display");
        else {
          isRoi = true;

          // align with template by calculating new transform
          // from m.transform()

          // the template image is the other one
          int tmplIndex = (index.row() + 1) % index.model()->rowCount();

          const QRect& tmplRect =
              parent->_list[parent->_currentRow][tmplIndex].image().rect();

          QTransform tx;
          calculate(tmplRect, rect, scale, dstRect, tx);

          // m.transform() is from template to m.image(),
          // tx is from template to viewport so with the inversion we get
          // m->template->viewport
          i2v = QTransform(m.transform()).inverted() * tx;

          // to confirm the mapping is right, draw the outline
          if (_debug) {
            painter->setPen(Qt::yellow);
            painter->drawRect(dstRect.translated(rect.topLeft()));
          }
          // get accurate scale for the filters
          QPointF p1 = i2v.map(QPointF(0, 0));
          QPointF p2 = i2v.map(QPointF(1, 0));
          QPointF p3 = p2 - p1;
          totalScale = sqrt(p3.x() * p3.x() + p3.y() * p3.y());

          // rotation angle is nice to know
          rotation = qRotationAngle(i2v.toAffine());
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
      //if (isRoi) painter->fillRect(dstRect, Qt::gray);

      if (full.isNull()) {
        if (fullRect.height() > 0) {
          QRectF r = i2v.mapRect(fullRect);
          r = r.intersected(QRect{0,0,rect.width(),rect.height()});
          painter->fillRect(r.translated(rect.topLeft()),
                            QBrush(Qt::darkGray,Qt::FDiagPattern));
        }
      }
      else if (filterId == -1) {
        // Qt5 scaling (bicubic?)
        painter->setRenderHint(QPainter::SmoothPixmapTransform);

        // this is slower, only use if there is a rotation
        if (i2v.isRotating()) {
          // bug: at some scale factors qt seems to only
          // draw the top half of the image
          painter->save();
          painter->setClipRect(rect);
          painter->translate(rect.x(),rect.y());
          painter->setTransform(i2v, true);
          painter->drawImage(0,0,full);
          painter->restore();
        } else {
          QRectF srcRect = i2v.inverted().mapRect(
              QRectF(0,0,rect.width(),rect.height()));
          painter->drawImage(rect, full, srcRect);
        }
        //if (isRoi) painter->drawRect(dstRect);

      } else {
        Q_ASSERT(!full.isNull()); // opencv exception/segfault
        // OpenCV scaling
        cv::Mat cvImg;
        qImageToCvImgNoCopy(full, cvImg);

        // note: OpenCV uses CCW rotation, so swap 21,11
        double mat[2][3] = {{i2v.m11(), i2v.m21(), i2v.dx()},
                            {i2v.m12(), i2v.m22(), i2v.dy()}};

        cv::Mat xForm(2, 3, CV_64FC(1), mat);

        cv::Mat subImg;
        cv::warpAffine(cvImg, subImg, xForm,
                       cv::Size(rect.width(), rect.height()),
                       filterId, cv::BORDER_CONSTANT);

        QImage qImg;
        cvImgToQImageNoCopy(subImg, qImg);
        painter->drawImage(rect.topLeft(), qImg);
      }

      // draw info about the image display (scale factor, mode, filter etc)
      painter->setPen(palette.text().color());

      QString info = QString("%1% %2(%3) %4")
                         .arg(int(totalScale * 100))
                         .arg(_actualSize ? "[1:1]" : _scaleToFit ? "[Fit] " : "")
                         .arg(_filters[filterIndex].name)
                         .arg(isRoi ?
                                    QString("[ROI] %1\xC2\xB0").arg(rotation, 0, 'f', 1)
                                    : "");
      int h1 = painter->fontMetrics().lineSpacing();

      painter->setPen(QColor(128,128,128,255));
      painter->drawText(QPoint{rect.x()+h1, rect.y()+h1}, info);

      const ColorDescriptor& cd = m.colorDescriptor();
      if (cd.numColors > 0)    {
        painter->save();
        int xOffset = LW_ITEM_HISTOGRAM_PADDING;
        int yOffset = h1 + LW_ITEM_HISTOGRAM_PADDING;
        painter->translate(rect.x() + xOffset,
                           rect.y() + yOffset);

        int totalWeight = 1;  // prevent divide-by-zero
        for (int i = 0; i < cd.numColors; i++) totalWeight += cd.colors[i].w;

        int x = 0;
        int y = 0;

        for (int i = 0; i < cd.numColors; i++) {
          const DescriptorColor& dc = cd.colors[i];
          QColor rgb = dc.toQColor();
          int w = LW_ITEM_HISTOGRAM_SIZE;
          int h = int(dc.w) * (rect.height()-yOffset) / totalWeight;

          painter->fillRect(x, y, w, h, rgb);
          painter->drawLine(x, y + h, x + w + 2, y + h);
          y += h;
        }
        painter->restore();
      }
    }

    rect = option.rect;
    rect = rect.adjusted(0,std::max(0, rect.height()-_textHeight),0,0);

    if (option.state & QStyle::State_Selected) {
      painter->fillRect(rect, palette.highlight());
      painter->setPen(palette.highlightedText().color());
    } else
      painter->setPen(palette.text().color());

    QString title = item->data(Qt::UserRole+0).toString();
    title = painter->fontMetrics().elidedText(title, Qt::ElideLeft, rect.width()-LW_ITEM_TITLE_FUZZ, 0);
    QString text = item->text();
    text = text.replace("@title@", title);
    text = text.replace("@width@", QString::number(rect.width()));

    WidgetHelper::drawRichText(painter, rect, text);

    if (_debug) {
      painter->setPen(Qt::magenta);
      painter->drawRect(rect);
      painter->setPen(Qt::green);
      painter->drawRect(option.rect);
    }
  }

  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const {
    (void)index;

    auto* parent = dynamic_cast<const MediaGroupListWidget*>(option.widget);

    // all items are the same size
    // estimate of ideal number of rows/columns to
    // maximize icon size and prevent scrollbars
    const QSize& viewSize = parent->frameRect().size();

    const int spacing = parent->spacing();
    //const int scrollbarWidth =
    //    option.widget->style()->pixelMetric(QStyle::PM_ScrollBarExtent,nullptr,parent->verticalScrollBar());
    const int textHeight = _textHeight;

    int numCols = 0, numRows = 0;

    const int itemCount = parent->count();

    // try all combinations to max icon size and minimize empty space
    // - only runs once per layout since we use uniformItemSizes()
    // - average aspect ratio of images determines if we favor more rows or column
    double minWasted = DBL_MAX;
    double maxUsed = DBL_MIN;

    for (int nRows = 1; nRows <= itemCount; ++nRows)
      for (int nCols = 1; nCols <= itemCount; ++nCols)
        if ((nRows * nCols) >= itemCount) {
          // estimate w/o scrollbar since it shouldn't be visible
          const double fw = (viewSize.width() - spacing*(nCols+1)) / double(nCols); // full item w/h
          const double fh = (viewSize.height() - spacing*(nRows+1)) / double(nRows);

          const double iw = (viewSize.width()  - spacing*(nCols+1)) / double(nCols); // image w/h
          const double ih = (viewSize.height() - textHeight*nRows - spacing*(nRows+1)) / double(nRows);
          double itemAspect = iw / ih;

          if (iw < 0 || ih < 0) continue;

          int emptyCount = nRows*nCols - itemCount;

          double sw, sh;
          if (_avgItemRatio < itemAspect) {
            sh = ih;
            sw = sh * _avgItemRatio;
          } else {
            sw = iw;
            sh = sw / _avgItemRatio;
          }

          int iconArea = sw*sh * itemCount;

          int emptyArea = (iw*ih*itemCount) - iconArea + (fw*fh*emptyCount);

          if (emptyArea < minWasted &&
              iconArea >= maxUsed) {
            //qWarning() << itemCount << i << j << sw
            //           << sw << sh
            //           << _avgItemRatio << itemAspect << minWasted << maxUsed;
            minWasted = emptyArea;
            maxUsed = iconArea;
            numCols = nCols;
            numRows = nRows;
          }
        }

    // fixme: should probably be minimum that forces scrollbar
    //if (ih < 32 || iw <32) continue;

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
    QSize hint(
        (viewSize.width() - spacing*(numCols+2))  / numCols,
        (viewSize.height() - spacing*(numRows+2)) / numRows);

    if (_debug)
      qInfo() << numCols << "x" << numRows << hint;

    return hint;
  }

 private:
  QVector<ScaleFilter> _filters;
  double _avgItemRatio = 2.0/3.0;
  double _zoom = 1.0;
  QPointF _pan;
  int _equalFilter = 0, _minFilter = 0, _magFilter = 0;
  bool _scaleToFit = false;
  int _textHeight = 100;
  bool _debug = false;
  bool _actualSize = false;
};

MediaGroupListWidget::MediaGroupListWidget(const MediaGroupList& list,
                                           const MediaWidgetOptions& options,
                                           QWidget* parent)
    : QListWidget(parent), _list(list), _options(options) {

  _itemDelegate = new MediaItemDelegate(this);

  setViewMode(QListView::IconMode);
  setResizeMode(QListView::Adjust);
  setMovement(QListView::Static);
  setSelectionRectVisible(false);
  setItemDelegate(_itemDelegate);
  setSpacing(LW_ITEM_SPACING);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setMinimumSize(QSize{320,240});
  setUniformItemSizes(true);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  setStyleSheet(
      "QListWidget { "
        "background-color: black; "
        "selection-color: rgba(255,255,255,255); "
        "selection-background-color: #444; "
        "font-size: 16px; "
        "color: rgba(255,255,255,255); "
      "}"
      "QScrollBar {"
        "width: 32px; "
        "background-color: black; "
        "color: darkGray; "
      "}");

  if (list.count() > 0) {
    loadRow(0);
    int row = 0;
    if (!(_options.flags & MediaWidgetOptions::FlagSelectFirst))
      row = model()->rowCount() - 1;
    setCurrentIndex(model()->index(row, 0));
  }

  // info text height must be accurate for reliable layout
  if (count() > 0) {
    QImage qImg(640, 480, QImage::Format_RGB32);

    const auto green = qRgb(0,0,255);
    QPainter painter;
    painter.begin(&qImg);
    painter.fillRect(qImg.rect(), green);
    WidgetHelper::drawRichText(&painter, qImg.rect(), item(0)->text());
    painter.end();

    int y;
    for (y = qImg.height()-1; y >= 0; --y )
      if (qImg.pixel(10, y) != green)
        break;

//    QLabel* label = new QLabel;
//    label->setPixmap(QPixmap::fromImage(qImg));
//    label->show();

    qDebug() << "estimated text box height:" << y;
    _itemDelegate->setTextHeight(y);
  }

  connect(&_updateTimer, &QTimer::timeout, [&]() {
    _updateTimer.stop();
    if (_updateTimer.property("row").toInt() != _currentRow) return;
    this->updateItems();
  });

  connect(this, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this,
          SLOT(openAction()));

  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, &QWidget::customContextMenuRequested,
          this, &MediaGroupListWidget::execContextMenu);

  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(this->metaObject()->className() + QString(".shortcuts"));

  WidgetHelper::addAction(settings, "Rename", Qt::Key_F2, this, SLOT(renameFileAction()));
  WidgetHelper::addAction(settings, "Copy Name", Qt::SHIFT | Qt::Key_F2, this, SLOT(copyNameAction()));
  WidgetHelper::addAction(settings, "Rename Folder", Qt::Key_F3, this, SLOT(renameFolderAction()));
  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Rotate", Qt::Key_R, this, SLOT(rotateAction()));
  WidgetHelper::addAction(settings, "Toggle Scale-Up", Qt::Key_S, this, SLOT(normalizeAction()));
  WidgetHelper::addAction(settings, "Template Match", Qt::Key_T, this, SLOT(templateMatchAction()));
  WidgetHelper::addAction(settings, "Quality Score", Qt::Key_Q, this, SLOT(qualityScoreAction()));
  WidgetHelper::addAction(settings, "Toggle Compare Images", Qt::Key_X, this,
            SLOT(toggleAutoDifferenceAction()));
  WidgetHelper::addAction(settings, "Clear", Qt::Key_A, this, SLOT(clearAction()));
  WidgetHelper::addAction(settings, "Open File", Qt::Key_O, this, SLOT(openAction()));
  WidgetHelper::addAction(settings, "Open Enclosing Folder", Qt::Key_E, this, SLOT(openFolderAction()));
  WidgetHelper::addAction(settings, "Compare Videos", Qt::Key_V, this,
            SLOT(compareVideosAction()));
  WidgetHelper::addAction(settings, "Compare Audio", Qt::Key_C, this, SLOT(compareAudioAction()));
  WidgetHelper::addAction(settings, "Choose Selected", Qt::Key_Return, this, SLOT(chooseAction()));
  WidgetHelper::addAction(settings, "Reload", Qt::Key_F5, this, SLOT(reloadAction()));
  WidgetHelper::addAction(settings, "Copy Image", Qt::CTRL|Qt::Key_C, this, SLOT(copyImageAction()));

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Delete", Qt::Key_D, this, SLOT(deleteAction()))
      ->setEnabled(!(_options.flags & MediaWidgetOptions::FlagDisableDelete));

  WidgetHelper::addAction(settings, "Replace", Qt::Key_G, this, SLOT(replaceAction()))
      ->setEnabled(!(_options.flags & MediaWidgetOptions::FlagDisableDelete));

  WidgetHelper::addSeparatorAction(this);

  // for building test/validation data sets
  WidgetHelper::addAction(settings, "Record Good Match", Qt::Key_Y, this, SLOT(recordMatchTrueAction()));
  WidgetHelper::addAction(settings, "Record Bad Match", Qt::Key_N, this, SLOT(recordMatchFalseAction()));

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Add to Negative Matches", Qt::Key_Minus, this, SLOT(negMatchAction()))
      ->setEnabled(_options.db != nullptr);
  WidgetHelper::addAction(settings, "Add All to Negative Matches", Qt::SHIFT | Qt::Key_Minus,
            this, SLOT(negMatchAllAction()))
      ->setEnabled(_options.db != nullptr);

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Zoom In", Qt::Key_9, this, SLOT(zoomInAction()));
  WidgetHelper::addAction(settings, "Zoom Out", Qt::Key_7, this, SLOT(zoomOutAction()));
  WidgetHelper::addAction(settings, "Zoom 100%", Qt::Key_0, this, [&]() {
    _itemDelegate->toggleActualSize();
    repaint();
  });
  WidgetHelper::addAction(settings, "Pan Left", Qt::Key_4, this, SLOT(panLeftAction()));
  WidgetHelper::addAction(settings, "Pan Right", Qt::Key_6, this, SLOT(panRightAction()));
  WidgetHelper::addAction(settings, "Pan Up", Qt::Key_8, this, SLOT(panUpAction()));
  WidgetHelper::addAction(settings, "Pan Down", Qt::Key_2, this, SLOT(panDownAction()));
  WidgetHelper::addAction(settings, "Reset Zoom", Qt::Key_5, this, SLOT(resetZoomAction()));
  WidgetHelper::addAction(settings, "Cycle Min Filter", Qt::Key_1, this, SLOT(cycleMinFilter()));
  WidgetHelper::addAction(settings, "Cycle Max Filter", Qt::Key_3, this, SLOT(cycleMagFilter()));

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Forward", Qt::ALT | Qt::Key_Down, this, SLOT(nextGroupAction()))
      ->setEnabled(_list.count() > 1);
  WidgetHelper::addAction(settings, "Back", Qt::ALT | Qt::Key_Up, this, SLOT(prevGroupAction()))
      ->setEnabled(_list.count() > 1);
  WidgetHelper::addAction(settings, "Jump Forward", Qt::Key_PageDown, this, SLOT(jumpForwardAction()))
      ->setEnabled(_list.count() > 1);
  WidgetHelper::addAction(settings, "Jump Back", Qt::Key_PageUp, this, SLOT(jumpBackAction()))
      ->setEnabled(_list.count() > 1);
  WidgetHelper::addAction(settings, "Jump to Start", Qt::Key_Home, this, SLOT(jumpToStartAction()))
      ->setEnabled(_list.count() > 1);
  WidgetHelper::addAction(settings, "Jump to End", Qt::Key_End, this, SLOT(jumpToEndAction()))
      ->setEnabled(_list.count() > 1);

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Move to Next Screen", Qt::SHIFT | Qt::Key_F11,
            this, SLOT(moveToNextScreenAction()));
  WidgetHelper::addAction(settings, "Close Window", Qt::CTRL | Qt::Key_W, this, SLOT(close()));
  WidgetHelper::addAction(settings, "Close Window (Alt)", Qt::Key_Escape, this, SLOT(close()));

  _maximized = WidgetHelper::restoreGeometry(this);
}

MediaGroupListWidget::~MediaGroupListWidget() {
  WidgetHelper::saveGeometry(this);
  qDebug("~MediaGroupListWidget");
}

void MediaGroupListWidget::close() {
  waitLoaders();
  super::close();
  this->deleteLater();
}

void MediaGroupListWidget::closeEvent(QCloseEvent* event) {
  waitLoaders();
  super::closeEvent(event);
  this->deleteLater();
}

QMenu* MediaGroupListWidget::dirMenu(const char* slot) {
  QMenu* dirs = MenuHelper::dirMenu(_options.db->path(), this, slot);

  QSet<QString> groupDirs;
  const auto& group = _list[_currentRow];

  // add shortcuts for dirs in the current row,
  // in case they are buried it is nice to have
  int selectedIndex = -1;
  const QModelIndex index = currentIndex();
  if (index.isValid()) selectedIndex = index.row();

  for (int i = 0; i < group.count(); ++i)
    if (i != selectedIndex)
      if (!isAnalysis(group[i])) groupDirs.insert(group[i].dirPath());

  const auto& keys = groupDirs.values();
  QList<QAction*> actions;
  for (auto& dirPath : keys) {
    QDir dir(dirPath);
    auto count = dir.entryList(QDir::Files | QDir::NoDotAndDotDot).count();
    auto name = dir.dirName() + QString(" [x%1]").arg(count);
    auto* a = new QAction(name, this);
    a->setData(dirPath);
    connect(a, SIGNAL(triggered()), this, slot);
    actions.append(a);
  }

  if (actions.count() > 0) {
    QAction* first = dirs->actions().first();
    first = dirs->insertSeparator(first);
    dirs->insertActions(first, actions);
  }

  return dirs;
}

void MediaGroupListWidget::execContextMenu(const QPoint& p) {
  // add the move-to folder action
  QMenu* menu = new QMenu;
  if (_options.db) {
    QMenu* dirs = dirMenu(SLOT(moveFileAction()));
    QAction* act = new QAction("Move File to ...", this);
    act->setMenu(dirs);
    menu->addAction(act);

    dirs = dirMenu(SLOT(moveFolderAction()));
    act = new QAction("Move Folder to ...", this);
    act->setMenu(dirs);
    menu->addAction(act);
  }

  for (QAction* act : this->actions()) menu->addAction(act);

  menu->exec(this->mapToGlobal(p));
  delete menu;
}

/**
 * @brief False-color image to show differences between two images.
 * @details Black>Blue == small differences, probably unnoticable
 *          Cyan>Green == noticable upon close inspection
 *          Magenta>White = obvious without any differencing
 * @param left
 * @param right
 * @todo move to library function
 * @todo template match first, to align images
 * @return
 */
static QImage differenceImage(const QImage& inLeft, const QImage& inRight,
                              const QFuture<void>* future=nullptr) {
  QImage nullImage;
  if (inLeft.isNull() || inRight.isNull()) return nullImage;

  // normalize to reduce the effects of brightness/exposure
  // todo: setting for % histogram clipping  
  cv::Mat norm1, norm2;
  qImageToCvImgNoCopy(inLeft, norm1);
  brightnessAndContrastAuto(norm1, norm2, 5);
  QImage left;
  cvImgToQImageNoCopy(norm2, left);

  // cancellation points between slow steps
  if (future && future->isCanceled()) return nullImage;

  cv::Mat norm3, norm4;
  qImageToCvImgNoCopy(inRight, norm3);
  brightnessAndContrastAuto(norm3, norm4, 5);
  QImage right;
  cvImgToQImageNoCopy(norm4, right);

  if (future && future->isCanceled()) return nullImage;

  QSize rsize = right.size();
  QSize lsize = left.size();
  int rightArea = rsize.width()*rsize.height();
  int leftArea = lsize.width()*lsize.height();
  if (rightArea < leftArea)
    right = right.scaled(lsize);
  else
    left = left.scaled(rsize);

  Q_ASSERT(left.size() == right.size());

  QImage img(left.size(), left.format());
  for (int y = 0; y < img.height(); y++)
    for (int x = 0; x < img.width(); x++) {
      QRgb lp = left.pixel(x, y);
      QRgb rp = right.pixel(x, y);

      int dr = qRed(lp) - qRed(rp);
      int dg = qGreen(lp) - qGreen(rp);
      int db = qBlue(lp) - qBlue(rp);

      // multiply to make > 0 and enhance differences
      dr = dr * dr;
      dg = dg * dg;
      db = db * db;

      // we care about overall difference and not per-channel differences
      int sum = dr + dg + db;

      // there are 255*255*3 possible values now
      // this is between 2^16 and 2^17
      // red = huge difference
      // green = medium
      // blue = small
      int r = (sum >> 10) << 2;
      int g = ((sum >> 5) & 31) << 3;
      int b = (sum & 31) << 3;

      QRgb diff = qRgb(r, g, b);
      img.setPixel(x, y, diff);
    }

  return img;
}

static float videoFps(const QString& path) {
  float fps = 29.97f;

  VideoContext video;
  VideoContext::DecodeOptions opt;
  if (0 == video.open(path, opt)) {
    QImage frame;
    video.nextFrame(frame);
    fps = video.fps();
    video.close();
  }

  return fps;
}

/**
 * @brief Load video metadata
 * @param m source media
 * @return Video metadata
 */
static VideoContext::Metadata loadVideo(const Media& m) {
  VideoContext video;
  VideoContext::DecodeOptions opt;
  video.open(m.path(), opt);
  video.close();
  return video.metadata();
}

/**
 * @brief Do background loading things
 * @param work      source/destination of the image/things
 * @param fastSeek if true, then seek video in a faster but less accurate way
 * @return true if successful
 */
static void loadImage(ImageWork* work, bool fastSeek) {
  Media& m = work->media;
  Q_ASSERT(m.image().isNull());

  uint64_t ts = nanoTime();
  uint64_t then = ts;
  uint64_t now;

  // prevent race on work->future
  while (!work->isReady) {
    QThread::msleep(1);
  }

  now = nanoTime();
  uint64_t t1 = now-then;
  then = now;

  if (work->future.isCanceled()) {
    qWarning() << work->row << work->index << "cancelled";
    return;
  }

  QImage img;

  if (isDifferenceAnalysis(m)) {
    const auto& left = work->input[0];
    const auto& right = work->input[1];

    QImage li = left.image();
    QImage ri = right.image();

    if (!li.isNull() && !ri.isNull())
      img = differenceImage(li, ri, &work->future);
  }
  else if (m.type() == Media::TypeImage) {
    img = m.loadImage(QSize(), &work->future);
  } else if (m.type() == Media::TypeVideo) {
    img = VideoContext::frameGrab(m.path(), m.matchRange().dstIn, fastSeek);
    if (work->future.isCanceled()) return;
    auto meta = loadVideo(m);
    m.setAttribute("duration", QString::number(meta.duration));
    m.setAttribute("fps", QString::number(double(meta.frameRate)));
    m.setAttribute("time", meta.timeDuration().toString("mm:ss"));
    m.setAttribute("vformat", meta.toString());
  }

  if (!img.isNull()) {
    m.setImage(img);
    m.setWidth(img.width());
    m.setHeight(img.height());
    m.readMetadata();
  }

  ts = nanoTime() - ts;
  ts = ts / 1000000;
  t1 = t1 / 1000000;
  if (ts > 1000) {
    qWarning("%d %2d %dms[%d] %dk : %s", work->row, work->index,
           int(ts), int(t1),
           int(m.originalSize() / 1024),
           qPrintable(m.path()));
  }
}

// image loader stats
// node: do not use for control flow, since multiple MGLWs possible
static int __started = 0;           // queued up
static int __finished = 0;          // done, we have an image
static int __canceled = 0;          // cancelled, no image
static int __canceledComplete = 0;  // cancelled, image

float MediaGroupListWidget::requiredMemory(int row) const {
  qint64 requiredMemory = 0;
  for (const auto& m : _list[row]) {
    const auto& img = m.image();
    if (img.isNull())
      requiredMemory += 4 * m.width() * m.height();
  }
  return requiredMemory / 1024.0;
}

void MediaGroupListWidget::loadMedia(int row) {
  if (row < 0 || row >= _list.count()) return;

  // row moves to back of lru list
  _lruRows.removeAll(row);
  _lruRows.append(row);

  MediaGroup& group = _list[row];

  // it is possible we were passed a crapton of items,
  // which would exhaust system memory, attempt to purge items

  auto shouldPurge = [](const Media& m) {
    return isAnalysis(m) || m.isReloadable();
  };

//  auto then = nanoTime();

  do {
    float totalKb, freeKb;
    Env::systemMemory(totalKb, freeKb);

    float requiredKb = 0;
    for (auto& r : _lruRows)
      requiredKb += requiredMemory(r);

    if (_lruRows.count() <= LW_MAX_CACHED_ROWS && // we have purged enough
        requiredKb < freeKb - LW_MIN_FREE_MEMORY_KB) break; // we have enough

    int lru = _lruRows.takeFirst();

    qDebug() << "purge row" << lru << "lru:" << _lruRows << "reqKb" << requiredKb << "freeKb" << freeKb;

    // cannot purge the current displayed row, move it to the back
    if (lru == _currentRow) {
      _lruRows.removeAll(_currentRow);
      _lruRows.append(_currentRow);
      if (_lruRows.count() == 1) break;
      continue;
    }

    // this can take a bit, if we do not wait for threads to cancel
    // memory could be exhausted
    waitLoaders(lru);

    for (auto& m : _list[lru])
      if (shouldPurge(m)) {
        m.setImage(QImage());
        m.setData(QByteArray());
      } else
        // memory use increasing...
        qWarning() << "unpurgable item, heap expanding" << m.path();

    if (lru == row) { // we just purged ourself, nothing else we can do
      qWarning() << "row" << row+1 << "cannot be loaded due to low memory"
                 << _lruRows;
      return;
    }

  } while(true);

//  auto now = nanoTime();
//  qCritical() << "purge:" << (now-then) / 100000.0 << "ms";

  int groupIndex=-1;
  for (Media& m : group) {
    groupIndex++;
    if (m.image().isNull()) {
      ImageWork* iw = new ImageWork;
      iw->row = row+1; // match gui display
      iw->index = groupIndex;
      iw->media = m;

      if (group.count() == 3 && isDifferenceAnalysis(m)) {
        const Media& left = group[0];
        const Media& right = group[1];
        if (left.image().isNull() || right.image().isNull()) continue;
        iw->input = {left, right};
      }

      QFutureWatcher<void>* w = new QFutureWatcher<void>(this);
      _loaders.append(w);

//      connect(w, &QFutureWatcher<bool>::canceled, [=]{
//        qWarning() << row+1 << groupIndex << "canceled";
//      });

      connect(w, &QFutureWatcher<void>::finished, [this,w,iw] {
        if (w->isCanceled()) {
          if (!iw->media.image().isNull())
            __canceledComplete = true;
          else
            __canceled++;
        } else {
          __finished++;
        }

        // if result is valid and in lru list we can keep it
        if (!iw->media.image().isNull()) {
          int row = w->property("row").toInt();
          const QString& path = w->property("path").toString();
          if (row >= 0 && row < _list.count() && _lruRows.contains(row)) {
            for (Media& m : _list[row])
              if (m.path() == path && m.image().isNull()) {
                m = iw->media;
                if (row == _currentRow) {
                  _updateTimer.stop();                  // coalesce updates
                  _updateTimer.setProperty("row", row); // don't update rows we can't see
                  _updateTimer.start(1000/60);          // 60hz is plenty
                }
                break;
              }

            // run difference image once dependents are loaded and
            // the row was not canceled
            auto& group = _list[row];
            if (!w->isCanceled() &&
                group.count() == 3 &&
                isDifferenceAnalysis(group[2]) &&
                !group[0].image().isNull() &&
                !group[1].image().isNull() &&
                group[2].image().isNull()) {
              loadMedia(row);
            }
          }
        }
        delete iw;
        w->deleteLater();
        _loaders.removeOne(w);
      });

      w->setProperty("row", row);
      w->setProperty("path", m.path());
      iw->future = QtConcurrent::run(loadImage, iw, fastSeek());
      w->setFuture(iw->future);
      iw->isReady = true;
      __started++;
    }
  }
}

void MediaGroupListWidget::cancelOtherLoaders(int row) {
  if (row < 0) return;
  for (auto* w : _loaders) {
    int loaderRow = w->property("row").toInt();
    if (loaderRow != row) w->cancel();
  }
}

void MediaGroupListWidget::waitLoaders(int row, bool cancel) {
  qint64 then = QDateTime::currentMSecsSinceEpoch();
  for (auto* w : _loaders) {
    int loaderRow = w->property("row").toInt();
    if (loaderRow == row || row < 0) {
      if (cancel) w->cancel();
      w->waitForFinished();
    }
  }

  qint64 blocked = QDateTime::currentMSecsSinceEpoch() - then;
  if (blocked > 100) qWarning() << "blocked for" << blocked << "ms";
}

// return if two values are, less, more or the same (for color-coding text)
template<typename T>
static const char* relativeLabel(const T& a, const T& b) {
  return a < b ? "less" : (b < a ? "more" : "same");
};

void MediaGroupListWidget::updateItems() {
  qDebug() << _currentRow
           << __started << (__finished + __canceled)
           << __canceled << __canceledComplete;

  if (_list[_currentRow].count() <= 0) return;

// I don't like this anymore
//
//  const Media& icon = _list[_currentRow].first();
//  if (!icon.image().isNull() &&
//      property("iconPath").toString() != icon.path()) {
//    setWindowIcon(QIcon(QPixmap::fromImage(
//        icon.image().scaledToHeight(LW_WM_ICON_SIZE))));
//    setProperty("iconPath", icon.path());
//  }

  MediaGroup& group = _list[_currentRow];

  QString prefix = Media::greatestPathPrefix(group);
  prefix = prefix.mid(0, prefix.lastIndexOf('/') + 1);

  // store the attributes of the first item and compare to the others
  struct {
    int64_t size;        // byte size
    double compression;  // compressed size / uncompressed sized
    int pixels;          // number of pixels / pixels per frame
    int quality;         // no-reference quality score
    int score;           // match score
    int fileCount;       // number of files in the same dir as this one
    int jpegQuality;     // jpeg quality factor used when saving
    int qualityScore;    // no-reference quality score
    int duration;        // video: duration in seconds
    float fps;           // video: frames-per-second
  } first;
  memset(&first, 0, sizeof(first));

  for (int i = 0; i < group.count(); i++) {
    const Media& m = group[i];
    //QString fmt;
    bool isVideo = m.type() == Media::TypeVideo;
    //if (isVideo)
    //  fmt = " :: " + m.attributes().value("vformat");

    int64_t size = m.originalSize();
    int pixels = m.resolution();
    double compression = double(m.compressionRatio());
    int score = m.score();
    int fileCount = 0;
    int jpegQuality = m.attributes().value("jpeg-quality").toInt();
    int qualityScore = m.attributes().value("quality-score").toInt();
    int duration = m.attributes().value("duration").toInt();
    float fps = m.attributes().value("fps").toFloat();

    QString path = m.path();
    const QFileInfo fileInfo(path);

    // truncate display name to common prefix
    if (fileInfo.isFile() || m.isArchived()) {
      path = path.mid(prefix.length());
      if (size == 0) size = fileInfo.size();
    }

    if (m.isArchived()) {
      // can be slow for large archives, we can cache since
      // archives are immutable here
      QString archivePath, filePath;
      m.archivePaths(archivePath, filePath);
      auto it = _archiveFileCount.find(archivePath);
      if (it != _archiveFileCount.end())
        fileCount = *it;
      else {
        fileCount = m.archiveCount();
        _archiveFileCount.insert(archivePath, fileCount);
      }
    } else if (fileInfo.isFile())
      fileCount = fileInfo.dir().entryList(QDir::Files).count();

    // store if current value is less than/greater than the first item in the
    // group the labels assigned are referenced in the stylesheet to change the
    // color of the value
    struct {
      QString compression, pixels, size, score, fileCount, duration, frameRate,
          jpegQuality, qualityScore;
    } compare;

    if (i == 0) {
      first.compression = compression;
      first.pixels = pixels;
      first.size = size;
      first.score = score;
      first.fileCount = fileCount;
      first.jpegQuality = jpegQuality;
      first.qualityScore = qualityScore;
      first.duration = duration;
      first.fps = fps;

      compare.compression = compare.pixels = compare.score = compare.size =
          compare.fileCount = "none";
      compare.duration = compare.frameRate = "same";  // don't hide this one
      compare.jpegQuality = compare.qualityScore = "same";
    } else {
      compare.compression = relativeLabel(first.compression, compression);
      compare.pixels = relativeLabel(pixels, first.pixels);
      compare.size = relativeLabel(size, first.size);
      compare.score = relativeLabel(score, first.score);
      compare.fileCount = relativeLabel(fileCount, first.fileCount);
      compare.jpegQuality = relativeLabel(jpegQuality, first.jpegQuality);
      compare.qualityScore = relativeLabel(qualityScore, first.qualityScore);

      if (isVideo) {
        compare.duration = relativeLabel(duration, first.duration);
        compare.frameRate = relativeLabel(fps, first.fps);
      }
    }

    const auto formatPercent = [](double a, double b) {
      if (b == 0) return QString("--");
      double percent = (a - b) * 100.0 / b;
      return QString("%1").arg(int(percent));
    };

    // elide the first row text, tricky... since there is no html attribute for it,
    // pass via item->data() to the item paint()...then must assume
    // drawRichText() uses similar font metrics
    QString title = path + QString(" [x%1] ").arg(fileCount);

    //
    // todo: convert this to some kind of loadable/configurable template with variable replacement
    // - media property keys & unary functions
    // - properties from here
    //
    // @prop@ - property
    // @prop-rel@ - "less", "more", "same"
    // @prop-rel-rev@ - "more", "less", "same"
    // @prop-diff-pct@ - "-10%" "100%"
    // @prop-diff-num@
    // @prop-diff-date@
    //
    // note: table width=100% does not work..., pass pixel width in paint()
    const QString text =
        QString(
            "<table width=@width@><tbody>"
            "<tr class=\"even\"><td class=\"%26\" colspan=\"3\" count=\"%15\">%1<span "
              "class=\"%16\">(%17)</span></td></tr>"
            "<tr class=\"odd\">"
              "<td>%2x%3</td>"
              "<td><span class=\"%7\">%11%</span></td>"
              "<td><span class=\"%18\">%19</span></td>"
            "</tr>"
            "<tr class=\"even\">"
              "<td>%4k</td>"
              "<td><span class=\"%8\">%12%</span></td>"
              "<td><span class=\"%20\">%21</span></td>"
            "</tr>"
            "<tr class=\"odd\">"
              "<td>%5:1</td>"
              "<td><span class=\"%9\">%13%</span></td>"
              "<td><span class=\"%22\">%23</span></td>"
            "</tr>"
            "<tr class=\"even\">"
              "<td>s%6</td>"
              "<td><span class=\"%10\">%14%</span></td>"
              "<td><span class=\"%24\">%25</span></td>"
            "</tr>"
            "</tbody></table>")
            .arg("@title@")
            .arg(m.width())
            .arg(m.height())
            .arg(size / 1024)
            .arg(compression, 0, 'f', 1)
            .arg(m.score())
            .arg(compare.pixels)
            .arg(compare.size)
            .arg(compare.compression)
            .arg(compare.score)
            .arg(formatPercent(pixels, first.pixels))
            .arg(formatPercent(size, first.size))
            .arg(formatPercent(compression, first.compression))
            .arg(formatPercent(score, first.score))
            .arg(fileCount)
            .arg(compare.fileCount)
            .arg(fileCount - first.fileCount)
            .arg(compare.duration)
            .arg(m.attributes().value("time"))
            .arg(compare.frameRate)
            .arg(double(fps))
            .arg(compare.jpegQuality)
            .arg(jpegQuality)
            .arg(compare.qualityScore)
            .arg(qualityScore)
            .arg(m.isArchived() ? "archive" : "file");

    // note: the "type" attribute of QListWidgetItem will be used to refer
    // back to the associated Media object
    // todo: using type() for list index is not needed, use indexFromItem()
    QListWidgetItem* item;
    if (i < this->count()) {
      item = this->item(i);
    } else {
      item = new QListWidgetItem(nullptr, i);
      insertItem(i, item);
    }
    if (isAnalysis(m)) {
      item->setFlags(Qt::NoItemFlags); // disable selection
    }
    else {
      item->setText(text);
      item->setData(Qt::UserRole+0, title);
      item->setToolTip(path);
    }
  }
  // assuming something changed, force repaint
  update();
}

void MediaGroupListWidget::loadRow(int row) {
  static uint64_t start;
  if (row == 0) start = nanoTime();

  if (_list.count() <= 0) return;

  row = qBound(0, row, _list.count() - 1);

  QModelIndex selected;
  {
    auto sel = selectedIndexes();
    if (sel.count() > 0) selected = sel.first();
  }

  // cancel loaders for other rows
  cancelOtherLoaders(row);

  // todo: setting to prevent too many items per page
  const MediaGroup& group = _list[row];

  int rowStride = row - _currentRow;
  if (rowStride == 0) {
    // if we deleted a row, _currentRow doesn't change
    // but we want to preload the next row
    rowStride = _currentRow == _list.count()-1 ? -1 : 1;
  }
  _currentRow = row;
  clear();

  // start loading media, if needed
  loadMedia(row);

  // use avg aspect to guess ideal number of rows/columns
  double avgAspect = 0;
  for (const Media& m : group) avgAspect += double(m.width()) / m.height();

  avgAspect /= group.count();
  _itemDelegate->setAverageItemRatio(avgAspect);
  _itemDelegate->setZoom(_zoom);
  _itemDelegate->setPan(QPointF(_panX, _panY));

  QString prefix = Media::greatestPathPrefix(group);
  prefix = prefix.mid(0, prefix.lastIndexOf('/') + 1);

  const QString homePath = QDir::homePath();
  if (prefix.startsWith(homePath)) {
    prefix = prefix.mid(homePath.length());
    prefix = "~" + prefix;
  }

  QString info;
  if (_list[row].count() > 0) {
    QString key = _list[row][0].attributes().value("group");
    if (!key.isEmpty()) info = QString("[%1]").arg(key);
  }

  setWindowTitle(QString("Group %1 of %2 : %3 [x%4] %5")
                     .arg(row + 1)
                     .arg(_list.count())
                     .arg(prefix)
                     .arg(_list[row].count())
                     .arg(info));

  updateItems();

  if (selected.isValid()) restoreSelectedItem(selected);

  // todo: save the last row jump and offset that amount
  bool preloadNextRow = true;
  int nextRow = row + rowStride;
  nextRow = std::min(_list.count(), std::max(nextRow, 0));

  // preload the next row we expect to see
  if (preloadNextRow) {
    QTimer::singleShot(100, [=]() {
      // if it is still valid after timer
      if (_currentRow + rowStride == nextRow)
        loadMedia(nextRow);
    });
  }

  if (QProcessEnvironment::systemEnvironment().contains(
          "BENCHMARK_LISTWIDGET")) {
    // load the next row immediately
    // fixme: will this execute? (exit() prevents event loop return)
    QTimer::singleShot(1, [=]() { loadRow(row + 1); });
    if (row == _list.count() - 1) {
      uint64_t pixels = 0;
      uint64_t data = 0;
      for (const MediaGroup& g : _list)
        for (const Media& m : g) {
          pixels += uint(m.width() * m.height());
          data += uint(m.originalSize());
        }

      double seconds = (nanoTime() - start) / 1000000000.0;
      qDebug() << QString("%1 s, %2 MB/s, %3 MPx/s")
                      .arg(seconds)
                      .arg(int(data / seconds / (1024 * 1024)))
                      .arg(int(pixels / seconds / (1000 * 1000)));
      exit(0);
    }
  }
}

void MediaGroupListWidget::updateCurrentRow(const MediaGroup& group) {
  // if there is one non-analysis image left, remove the group
  // if there are no groups left, close the viewer
  int count = countNonAnalysis(group);

  if (count <= 1) {
    qInfo() << "auto remove row" << _currentRow << "with one item left";
    waitLoaders(_currentRow);
    _list.removeAt(_currentRow);
    _lruRows.removeAll(_currentRow);
    for (auto& row : _lruRows)
      if (row > _currentRow)
        row--;

    if (_list.count() < 1) {
      qInfo() << "closing view, nothing left to display";
      close();
    }
  }

  if (_autoDifference)
    addDifferenceAnalysis();

  loadRow(_currentRow);
}

void MediaGroupListWidget::loadNextRow(bool closeAtEnd) {
  if (_currentRow < _list.count() - 1)
    loadRow(_currentRow + 1);
  else if (closeAtEnd)
    close();
}

void MediaGroupListWidget::removeSelection(bool deleteFiles, bool replace) {
  QList<QListWidgetItem*> items = selectedItems();
  Q_ASSERT((!deleteFiles && !replace) ||
           (deleteFiles && !replace) ||
           (deleteFiles && replace));

  MediaGroup& group = _list[_currentRow];

  // guard against deleting everything
  if (deleteFiles && items.count() == group.count()) {
    qWarning() << "preventing accidental deletion of entire group";
    return;
  }
  if (deleteFiles && replace && items.count()==1 && countNonAnalysis(group) != 2) {
    qWarning() << "delete+replace is only possible with 1 selection in 2 items";
    return;
  }

  QVector<int> removed;

  for (int i = 0; i < items.count(); ++i) {
    int index = items[i]->type();
    const Media& m = group[index];
    QString path = m.path();
    if (m.isArchived()) {
      QString tmp;
      m.archivePaths(path, tmp);
    }

    if (deleteFiles) {
      if (replace && m.isArchived()) {
        qWarning() << "delete+replace for archives unsupported";
        return;
      }

      if (deleteFiles) {
        static bool skipDeleteConfirmation = false;
        int button = 0;
        if (m.isArchived()) {
          QString zipPath = _options.db ? path.mid(_options.db->path().length()+1) : path;
          button = QMessageBox::warning(this, "Delete Zip Confirmation",
                              QString("The selected file is a member of \"%1\"\n\n"
                                      "Modification of zip archives is unsupported. Move the entire zip to the trash?"
                                     ).arg(zipPath),
                               "&No", "&Yes");
        }
        else if (skipDeleteConfirmation) {
          button = 2;
        }
        else {
          QString filePath = _options.db ? path.mid(_options.db->path().length()+1) : path;
          button = QMessageBox::warning(this, "Delete File Confirmation",
                               QString("Move this file to the trash?\n\n%1").arg(filePath),
                               "&No", "&Yes", "Yes to &All (This Session)");
        }

        if (button == 0) return;
        if (button == 2) skipDeleteConfirmation = true;
      }

      if (!DesktopHelper::moveToTrash(path)) return;

      if (_options.db) {
        if (m.isArchived()) {
            QString like = path;
            like.replace("%", "\\%").replace("_", "\\_");
            like += ":%";
            MediaGroup zipGroup = _options.db->mediaWithPathLike(like);
            _options.db->remove(zipGroup);
        } else {
          _options.db->remove(group[index].id());
          if (replace && countNonAnalysis(group)==2) {
            int otherIndex = (index + 1) % 2;
            Media& other = group[otherIndex];
            Q_ASSERT(!isAnalysis(other));
            const QFileInfo info(path);
            const QFileInfo otherInfo(other.path());

            // the new name must keep the suffix, could be different
            QString newName = info.completeBaseName() + "." +
                              otherInfo.suffix();

            // rename (if needed) and then move
            if (otherInfo.fileName() == newName ||
                _options.db->rename(other, newName))
              _options.db->move(other, info.dir().absolutePath());
          }
        }
      }
    }

    removed.append(index);
  }

  if (removed.count() <= 0) return;

  // remove deleted indices; we cannot remove using
  // path because of renaming
  MediaGroup newGroup;
  const int oldCount = group.count();
  for (int i = 0; i < oldCount; ++i)
    if (!removed.contains(i))
      newGroup.append(group[i]);
  group = newGroup;
  updateCurrentRow(group);
}

void MediaGroupListWidget::removeAnalysis() {
  for (auto& g : _list)
    if (isAnalysis(g.last()))
      g.removeLast();
}

void MediaGroupListWidget::addDifferenceAnalysis() {
  for (auto& g : _list)
    if (g.count() == 2 && !isAnalysis(g.last()))
      g.append(newDifferenceAnalysis());
}

static void maybeAppend(QStringList& sl, const QString& s) {
  if (!sl.contains(s)) sl.append(s);
}

static void maybeAppend(QStringList& sl, const QStringList& s) {
  for (const auto& str : s) maybeAppend(sl, str);
}

void MediaGroupListWidget::renameFileAction() {
  const MediaGroup& group = _list[_currentRow];

  if (renameWarning()) return;

  for (auto& m : selectedMedia()) {
    if (m.isArchived()) {
      qWarning() << "rename archive member unsupported";
      continue;
    }

    const QFileInfo info(m.path());
    if (!info.isFile()) {
      qWarning() << "path is not a file:" << info.path();
      continue;
    }

    QStringList completions;
    completions += info.fileName();

    // names of matches
    for (auto& m2 : group) {
        if (m2.isArchived()) {
          QString z,c;
          m2.archivePaths(z,c);
          maybeAppend(completions, c);
        }
        else
          maybeAppend(completions, m2.name());
    }

    // also files in same directory
    maybeAppend(completions, info.absoluteDir().entryList(QDir::Files, QDir::Name));

    // replace suffix to match the source
    const auto suffix = info.suffix();
    for (auto& c : completions) {
      QStringList parts = c.split(".");
      parts.removeLast();
      parts.append(suffix);
      c = parts.join(".");
    }

    int index = completions.indexOf(info.fileName());

    bool ok = false;
    QString newName = QInputDialog::getItem(this, "Rename File", "New Name",
                                            completions, index, true, &ok);

    if (ok && newName != info.fileName()) {
      QString path = m.path();
      if (_options.db) {
        if (_options.db->rename(m, newName)) updateMedia(path, m);
        else qWarning() << "rename via database failed";
      }
      else {
        QDir parentDir = info.dir();
        if (parentDir.rename(info.fileName(), newName)) {
          m.setPath(parentDir.absoluteFilePath(newName));
          updateMedia(path, m);
        }
        else
          qWarning() << "rename via filesystem failed";
      }
    }
  }
}

void MediaGroupListWidget::renameFolderAction() {
  const auto sel = selectedMedia();
  if (sel.count() != 1) return;

  if (renameWarning()) return;

  const Media& m = sel[0];

  QStringList completions;
  QDir parentDir;

  if (m.isArchived()) { // first completion is selection
    QString z, c;
    m.archivePaths(z,c);
    QFileInfo info(z);
    completions += info.fileName();
    parentDir = info.dir();
  }
  else {
    QFileInfo info(m.path());
    completions += info.dir().dirName();
    parentDir = info.dir();
    parentDir.cdUp();
  }
//  for (const auto& ii : qAsConst(_list[_currentRow])) {
//    const auto it = ii.attributes().find("group");
//    if (it != ii.attributes().end())
//      maybeAppend(completions, it.value());
//  }

  for (const auto& ii : qAsConst(_list[_currentRow])) {
    if (ii.isArchived()) {
      QString zipPath, childPath;
      ii.archivePaths(zipPath, childPath);
      const QFileInfo info(zipPath);
      QString zipName = QFileInfo(zipPath).fileName();
      if (!m.isArchived()) zipName = zipName.mid(0,zipName.lastIndexOf("."));
      maybeAppend(completions, zipName);
    } else {
      QString dirName = QFileInfo(ii.path()).dir().dirName();
      if (m.isArchived()) dirName += ".zip";
      maybeAppend(completions, dirName);
    }
  }

  bool ok = false;
  QString newName = QInputDialog::getItem(this, "Rename Folder/Zip", "New Name",
                                          completions, 0, true, &ok);
  if (!ok) return;

  // new path is not index-relative...pass absolute
  QString newPath = parentDir.absoluteFilePath(newName);
  moveDatabaseDir(m, newPath);
}

bool MediaGroupListWidget::selectedPair(Media** selected, Media** other) {
  // fixme: doesn't work when analysis image enabled
  auto& group = _list[_currentRow];
  const auto& selection = selectedItems();
  if (selection.count() != 1 || countNonAnalysis(group) != 2) return false;

  int selIndex = selection[0]->type();
  int otherIndex = !selIndex;

  // assumes we keep analysis images at the end
  Q_ASSERT( !isAnalysis(group[otherIndex]) );

  *selected = &group[selIndex];
  *other = &group[!selIndex];
  return true;
}

bool MediaGroupListWidget::renameWarning() {
  if (!_options.db) {
    auto button = QMessageBox::warning(
        this, "Rename Without Database?",
        "Renaming without a database will invalidate the index.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (button != QMessageBox::Yes) return true;
  }
  return false;
}

void MediaGroupListWidget::copyNameAction() {
  Media *selected, *other;
  if (!selectedPair(&selected, &other)) return;

  if (renameWarning()) return;

  if (selected->isArchived()) {
    qWarning() << "renaming archived files unsupported";
    return;
  }

  const QFileInfo info(selected->path());
  QString otherName;
  if (other->isArchived()) {
    QString zipPath;
    other->archivePaths(zipPath,otherName);
  }
  else
    otherName = other->name(); // fixme: should name() work with archives?

  QString newName = QFileInfo(otherName).completeBaseName() +
                          "." + info.suffix();
  const QString oldPath = selected->path();
  if (_options.db) {
    if (_options.db->rename(*selected, newName))
      updateMedia(oldPath, *selected);
    else
      qWarning() << "rename via database failed";
  }
  else {
    QDir dir = info.dir();
    if (dir.rename(oldPath, newName)) {
      selected->setPath( dir.absoluteFilePath(newName) );
      updateMedia(oldPath, *selected);
    }
    else
      qWarning() << "rename via filesystem failed";
  }
}

void MediaGroupListWidget::moveFileAction() {
  Q_ASSERT(_options.db); // w/o db we don't have dir menu actions

  QAction* action = dynamic_cast<QAction*>(sender());
  if (!action) return;

  QString dirPath = action->data().toString();

  if (dirPath == ";newfolder;")
    dirPath =
        QFileDialog::getExistingDirectory(this, "Choose Folder", _options.db->path());

  if (dirPath.isEmpty()) return;

  for (Media& m : selectedMedia()) {
    QString path = m.path();
    if (_options.db->move(m, dirPath))
      updateMedia(path, m);
  }
}

void MediaGroupListWidget::moveDatabaseDir(const Media& child, const QString& newName) {
  QDir dir = QFileInfo(child.path()).dir();

  QString newPath = newName;
  QString absSrcPath = dir.absolutePath();
  if (child.isArchived()) {
    QString childPath;
    child.archivePaths(absSrcPath, childPath);
    dir = QFileInfo(absSrcPath).dir(); // dir otherwise may refer to a zip dir
    if (!newPath.endsWith(".zip"))
      newPath += ".zip";
  }
  else if (!dir.cdUp()) {
    // use parent for direct rename/updating
    qWarning() << "cdUp() failed";
    return;
  }

  qDebug() << absSrcPath << "=>" << newPath;
  QString absDstPath;
  if (_options.db) {
    absDstPath = QDir(_options.db->path()).absoluteFilePath(newPath);
    if (!_options.db->moveDir(absSrcPath, newPath)) {
      qWarning() << "rename folder via database failed";
      return;
    }
  } else {
    // if newPath is relative assume it is a dir name,
    // if newPath is a relative path we have a problem
    QFileInfo newInfo(newPath);
    if (newPath != newInfo.fileName()) {
      qWarning() << "I don't know what dst path is relative to, use abs path?";
      return;
    }
    absDstPath = dir.absoluteFilePath(newPath);
    if (!dir.rename(absSrcPath, absDstPath)) {
      qWarning() << "rename folder via filesystem failed" << absSrcPath << absDstPath;
      return;
    }
  }

  for (auto& g : _list)
    for (auto& m : g)
      if (m.path().startsWith(absSrcPath))
        m.setPath(absDstPath + m.path().mid(absSrcPath.length()));

  // updateItems() won't work since we may have changed window title
  loadRow(_currentRow);
}

void MediaGroupListWidget::moveFolderAction() {
  Q_ASSERT(_options.db); // w/o db we don't have dir menu actions

  QAction* action = dynamic_cast<QAction*>(sender());
  if (!action) return;

  QString dirPath = action->data().toString();

  if (dirPath == ";newfolder;")
    dirPath =
        QFileDialog::getExistingDirectory(this, "Choose Folder", _options.db->path());

  if (dirPath.isEmpty()) return;

  QSet<QString> moved;

  for (Media& m : selectedMedia()) {
    QString srcPath;
    if (m.isArchived()) {
      QString child;
      m.archivePaths(srcPath, child);
    }
    else
      srcPath = m.dirPath();

    if (moved.contains(srcPath)) // already moved
      continue;

    const QString dstPath = dirPath + "/" + QFileInfo(srcPath).fileName();
    moveDatabaseDir(m, dstPath);
    moved += srcPath;
  }
}

MediaGroup MediaGroupListWidget::selectedMedia() {
  QList<QListWidgetItem*> items = selectedItems();

  const MediaGroup& group = _list[_currentRow];

  MediaGroup selected;
  for (int i = 0; i < items.count(); i++) {
    int index = items[i]->type();
    Media m = group[index];
    selected.append(m);
  }

  return selected;
}

void MediaGroupListWidget::compareVideosAction() {
  QList<QListWidgetItem*> items = selectedItems();
  if (items.count() != 1) return;

  const MediaGroup& group = _list[_currentRow];
  if (group.count() < 2) return;

  Media left = group[0];
  Media right = group[items[0]->type()];

  if (left.type() != Media::TypeVideo || right.type() != Media::TypeVideo)
    return;

  MatchRange range(0, 0, -1);

  // if right is needle, left is match; set range
  if (left.matchRange().srcIn < 0)
    range = MatchRange(right.matchRange().srcIn, right.matchRange().dstIn,
                       right.matchRange().len);

  VideoCompareWidget* comp = new VideoCompareWidget(left, right, range);
  comp->setAttribute(Qt::WA_DeleteOnClose);
  comp->show();
}

void MediaGroupListWidget::compareAudioAction() {
  QList<QListWidgetItem*> items = selectedItems();
  if (items.count() != 1) return;

  const MediaGroup& group = _list[_currentRow];
  if (group.count() < 2) return;

  Media left = group[0];
  Media right = group[items[0]->type()];

  DesktopHelper::compareAudio(left.path(), right.path());
}

void MediaGroupListWidget::openFolderAction() {
  QList<QListWidgetItem*> items = selectedItems();
  if (items.count() != 1) return;

  MediaGroup& group = _list[_currentRow];
  Media& m = group[items[0]->type()];

  Media::revealMedia(m);
}

void MediaGroupListWidget::deleteAction() { removeSelection(true); }

void MediaGroupListWidget::replaceAction() { removeSelection(true, true); }

void MediaGroupListWidget::qualityScoreAction() {
  MediaGroup& group = _list[_currentRow];
  QList<QFuture<void>> work;

  for (Media& m : group)
    if (!m.image().isNull())
      work += QtConcurrent::run([&]() {
        // no-reference quality score
        double score = qualityScore(m);
        m.setAttribute("quality-score", QString::number(score));

        // jpeg codec quality factor
        if (m.type() != Media::TypeImage || // ok; file header is checked too
            isAnalysis(m))                  // raw image, no i/o device possible
          return;

        auto* io = m.ioDevice();
        if (!io) return;

        const JpegQuality jq = EstimateJpegQuality(io);
        if (jq.ok && jq.isReliable)
            m.setAttribute("jpeg-quality", QString::number(jq.quality));
      });

  qApp->setOverrideCursor(QCursor(Qt::WaitCursor));
  for (auto& w : work) w.waitForFinished();
  qApp->restoreOverrideCursor();

  updateItems();
}

void MediaGroupListWidget::recordMatch(bool matched) {
  const MediaGroup& group = _list[_currentRow];
  Media search = group[0];
  Media match;
  QString line;

  if (matched) {
    int index = 1;

    if (group.count() > 2) {
      QListWidgetItem* item = currentItem();
      if (item) index = item->type();
    }

    if (index <= 0) index = 1;

    match = group[index];

    line = QString("\"%1\",\"%2\",%3,%4,%5,%6\n")
               .arg(search.path())
               .arg(match.path())
               .arg(index)
               .arg(match.score())
               .arg(match.position())
               .arg(group.count() - 1);
  } else
    line = QString("%1,,0,,,%2\n").arg(search.path()).arg(group.count() - 1);

  QFile f("matches.csv");
  f.open(QFile::WriteOnly | QFile::Append);
  f.write(line.toLatin1());

  loadNextRow(true);
}

bool MediaGroupListWidget::addNegMatch(bool all) {
  if (!_options.db) return false;

  const MediaGroup& group = _list[_currentRow];

  if (all || group.count() == 2) {
    for (int i = 1; i < group.size(); i++)
      _options.db->addNegativeMatch(group[0], group[i]);

    return true;
  } else {
    QListWidgetItem* item = currentItem();

    if (item) {
      const Media& m1 = group[0];
      const Media& m2 = group[item->type()];

      _options.db->addNegativeMatch(m1, m2);
      return true;
    }
  }
  return false;
}

void MediaGroupListWidget::normalizeAction() {
  _itemDelegate->toggleScaleToFit();
  repaint();
}

void MediaGroupListWidget::templateMatchAction() {
  MediaGroup& group = _list[_currentRow];

  if (group.count() < 2) return;

  // drop extra images and put selected image second
  QList<QListWidgetItem*> items = selectedItems();
  if (items.count() == 1) {
    MediaGroup filtered;
    int selectedIndex = items[0]->type();
    int otherIndex = (selectedIndex + 1) % 2;
    filtered.append(group[otherIndex]);
    filtered.append(group[selectedIndex]);
    group = filtered;
  }

  // remove extra images
  if (group.count() > 2) group.remove(2, group.count() - 2);

  // clear roi, template matcher sets it
  group[0].setRoi(QVector<QPoint>());
  group[1].setRoi(QVector<QPoint>());

  if (group[0].image().isNull() || group[1].image().isNull()) return;

  // look for first image in the second image
  int tmplIndex = 0;
  int targetIndex = 1;

  // set threshold high to consider all matches with a transform,
  // regardless if it is a good match or not
  SearchParams params;
  params.dctThresh = 64;

  MediaGroup haystack;
  haystack.append(group[targetIndex]);

  TemplateMatcher().match(group[tmplIndex], haystack, params);
  if (haystack.count() > 0) group[targetIndex] = haystack[0];

  // reload since we may have deleted items
  loadRow(_currentRow);
}

void MediaGroupListWidget::reloadAction() {
  // reload current row and forget any uncommitted changes
  for (Media& m : _list[_currentRow]) m.setRoi(QVector<QPoint>());

  updateCurrentRow(_list[_currentRow]);
}

void MediaGroupListWidget::copyImageAction() {
  auto sel = selectedMedia();
  if (sel.count() <= 0) return;
  const Media&m = sel[0];
  qApp->clipboard()->setImage(m.image());
}

void MediaGroupListWidget::moveToNextScreenAction() {
  QDesktopWidget* desktop = QApplication::desktop();
  int nextScreen = desktop->screenNumber(pos());
  nextScreen = (nextScreen + 1) % desktop->numScreens();

  QRect newGeom = desktop->availableGeometry(nextScreen);

  int newX = newGeom.topLeft().x();
  int newY = newGeom.topLeft().y();

  QRect geom = frameGeometry();

  // move() seems to set the frame top-left, however
  // if the window height doesn't fit the screen weird shit happens
  // fixme: this might be wrong, appears to be window manager interaction...
  //        the difference in width/height should include window border,
  //        but window position won't work in that case
  int offX = geom.x() - geometry().x();
  int offY = geom.y() - geometry().y();

  // if the window is maximized we move it to top left
  if (!isMaximized()) {
    // center, or resize if we cannot
    if (newGeom.width() > geom.width())
      newX = newX + (newGeom.width() - geom.width()) / 2;
    else
      geom.setWidth(newGeom.width());

    if (newGeom.height() > geom.height())
      newY = newY + (newGeom.height() - geom.height()) / 2;
    else
      geom.setHeight(newGeom.height());

    resize(geom.width() + offX, geom.height() + offY);
  }

  QPoint newPos(newX, newY);
  move(newPos);
}

void MediaGroupListWidget::zoomInAction() {
  _zoom *= LW_ZOOM_IN_STEP;
  _zoom = qMax(_zoom, 0.01);
  _itemDelegate->setZoom(_zoom);
  repaint();
}

void MediaGroupListWidget::zoomOutAction() {
  _zoom *= LW_ZOOM_OUT_STEP;
  _zoom = qMin(1.0, _zoom);
  _itemDelegate->setZoom(_zoom);
  repaint();
}

void MediaGroupListWidget::panLeftAction() {
  _panX -= LW_PAN_STEP;
  _itemDelegate->setPan({_panX, _panY});
  repaint();
}

void MediaGroupListWidget::panRightAction() {
  _panX += LW_PAN_STEP;
  _itemDelegate->setPan({_panX, _panY});
  repaint();
}

void MediaGroupListWidget::panUpAction() {
  _panY -= LW_PAN_STEP;
  _itemDelegate->setPan({_panX, _panY});
  repaint();
}

void MediaGroupListWidget::panDownAction() {
  _panY += LW_PAN_STEP;
  _itemDelegate->setPan({_panX, _panY});
  repaint();
}

void MediaGroupListWidget::resetZoomAction() {
  _zoom = 1.0;
  _panX = 0.0;
  _panY = 0.0;
  _itemDelegate->setZoom(_zoom);
  _itemDelegate->setPan({_panX, _panY});
  repaint();
}

void MediaGroupListWidget::cycleMinFilter() {
  _itemDelegate->cycleMinFilter();
  repaint();
}

void MediaGroupListWidget::cycleMagFilter() {
  _itemDelegate->cycleMagFilter();
  repaint();
}

void MediaGroupListWidget::toggleAutoDifferenceAction() {
  if (_autoDifference) removeAnalysis();
  else addDifferenceAnalysis();

  _autoDifference = !_autoDifference;
  loadRow(_currentRow);
}

void MediaGroupListWidget::rotateGroup(int row) {
  MediaGroup& group = _list[row];
  int offset = 1;
  if (isAnalysis(group.last())) // do not rotate the analysis image
    offset = 2;
  group.move(0, group.count() - offset);
  updateItems();
}

void MediaGroupListWidget::restoreSelectedItem(const QModelIndex& last) {
  const MediaGroup& group = _list.at(_currentRow);
  int count = countNonAnalysis(group);
  int selIndex = std::min(last.row(), count - 1);
  if (selIndex >= 0)
    setCurrentIndex(model()->index(selIndex, 0));
}

void MediaGroupListWidget::keyPressEvent(QKeyEvent* event) {
  // up/down key move to the next group if we're on the first/last row of the group
  QModelIndexList list = selectedIndexes();
  if (list.count() == 1 && event->modifiers() == 0)
    switch (event->key()) {
      case Qt::Key_Down: {
        QModelIndex curr = list[0];
        QModelIndex next = moveCursor(QAbstractItemView::MoveDown, Qt::NoModifier);

        if (curr == next && _currentRow + 1 < _list.count()) {
          loadRow(_currentRow + 1);
          return;
        }
      } break;
      case Qt::Key_Up: {
        QModelIndex curr = list[0];
        QModelIndex next = moveCursor(QAbstractItemView::MoveUp, Qt::NoModifier);

        if (curr == next && _currentRow - 1 >= 0) {
          loadRow(_currentRow - 1);
          return;
        }
      }
    }
  super::keyPressEvent(event);
}

void MediaGroupListWidget::wheelEvent(QWheelEvent* event) {
  const int delta = event->delta();
  const int orientation = event->orientation();
  if (orientation == Qt::Vertical) {
    if (delta > 0)
      loadRow(_currentRow - 1);
    else
      loadRow(_currentRow + 1);
    event->accept();
  } else if (orientation == Qt::Horizontal) {
    if (delta > 0) {
      rotateAction();
      event->accept();
    }
  }
}

void MediaGroupListWidget::openAction() {
  QList<QListWidgetItem*> items = selectedItems();
  if (items.count() != 1) return;

  MediaGroup& group = _list[_currentRow];

  for (auto* item : items) {
    const Media& m = group[item->type()];
    float seek = 0;

    if (m.type() == Media::TypeVideo) {
      // fixme: we already parsed the fps
      // fixme: make sure dstIn is valid
      float fps = videoFps(m.path());
      if (fps != 0.0f) seek = m.matchRange().dstIn / fps;
    }
    Media::openMedia(m, seek);
  }
}

void MediaGroupListWidget::chooseAction() {
  MediaGroup g = selectedMedia();
  if (!g.empty()) emit mediaSelected(g);
}

void MediaGroupListWidget::updateMedia(const QString& path, const Media& m) {
  for (MediaGroup& group : _list)
    for (Media& media : group)
      if (media.path() == path) media = m;
  updateItems();
}
