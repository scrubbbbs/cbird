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
#include "mediaitemdelegate.h"
#include "mediapage.h"
#include "pooledimageallocator.h"
#include "videocomparewidget.h"
#include "cropwidget.h"
#include "mediabrowser.h"
#include "theme.h"

#include "../cimgops.h" // qualityScore
#include "../database.h"
#include "../env.h"
#include "../lib/jpegquality.h"
#include "../profile.h"
#include "../qtutil.h"
#include "../templatematcher.h"
#include "../videocontext.h"

#ifdef LW_RLIMIT
#include <sys/resource.h>  // setrlimit()
#endif

#define LW_LOW_FREE_MEMORY_KB (1024 * 1024) // start freeing memory here
#define LW_MIN_FREE_MEMORY_KB (256 * 1024)  // allocations fail after this
#define LW_MAX_CACHED_ROWS (5)

#define LW_PAN_STEP (10.0)
#define LW_ZOOM_STEP (0.9)

#define LW_UPDATE_HZ (60) // minimum time between repaints
#define LW_PRELOAD_DELAY (100) // milliseconds to wait until preloading

static void maybeAppend(QStringList& sl, const QString& s) {
  if (!sl.contains(s)) sl.append(s);
}

static void maybeAppend(QStringList& sl, const QStringList& s) {
  for (const auto& str : s) maybeAppend(sl, str);
}

/// Passed in/out of background jobs
class ImageWork : public QFutureWatcher<void> {
  NO_COPY_NO_DEFAULT(ImageWork, QFutureWatcher<void>)
public:
  ImageWork(QObject* parent) : super(parent) {}

  Media media;               // input/output
  QVector<Media> args;       // for analysis
  MediaPage* page = nullptr; // page it was originally on (could be deleted after job starts)
  int index = -1;            // index in the group (could change due to rotation/deletion)
  bool oom = false;          // out of memory
};

static auto* __imgAlloc = new PooledImageAllocator(LW_LOW_FREE_MEMORY_KB);

/**
 * @brief False-color image to show differences between two images.
 * @details Black>Blue == small differences, probably unnoticable
 *          Cyan>Green == noticable upon close inspection
 *          Magenta>White = obvious without any differencing
 * @todo move to library function
 * @return
 */
static QImage differenceImage(const Media& ml, const Media& mr,
                              const QFuture<void>* future = nullptr) {
  QImage nullImage;
  QImage inLeft = ml.image().convertToFormat(QImage::Format_RGB32);
  QImage inRight = mr.image().convertToFormat(QImage::Format_RGB32);
  if (inLeft.isNull() || inRight.isNull()) return nullImage;

  // apply template matcher transform
  if (!mr.transform().isIdentity()) {
    QImage xFormed(inLeft.size(), QImage::Format_RGB32);
    QPainter p(&xFormed);
    const QTransform tx(mr.transform().inverted());
    p.setTransform(tx, true);
    p.drawImage(0, 0, inRight);
    inRight = xFormed;
  }

  // cancellation points between slow steps
  if (future && future->isCanceled()) return nullImage;

  // normalize to reduce the effects of brightness/exposure
  // TODO: setting for % histogram clipping
  Q_ASSERT(inLeft.format() == QImage::Format_RGB32);
  Q_ASSERT(inRight.format() == QImage::Format_RGB32);
  cv::Mat norm1, norm2;
  QImage left;
  QFuture<void> f1 = QtConcurrent::run([&]() {
    const MessageContext context(ml.name());
    qImageToCvImgNoCopy(inLeft, norm1);
    brightnessAndContrastAuto(norm1, norm2, 5);
    cvImgToQImageNoCopy(norm2, left, QImage::Format_RGB32);
  });

  cv::Mat norm3, norm4;
  QImage right;
  QFuture<void> f2 = QtConcurrent::run([&]() {
    const MessageContext context(mr.name());
    qImageToCvImgNoCopy(inRight, norm3);
    brightnessAndContrastAuto(norm3, norm4, 5);
    cvImgToQImageNoCopy(norm4, right, QImage::Format_RGB32);
  });

  f1.waitForFinished();
  f2.waitForFinished();

  if (future && future->isCanceled()) return nullImage;

  // scale to the larger size
  QSize rsize = right.size();
  QSize lsize = left.size();
  int rightArea = rsize.width() * rsize.height();
  int leftArea = lsize.width() * lsize.height();
  if (rightArea < leftArea)
    right = right.scaled(lsize);
  else
    left = left.scaled(rsize);

  Q_ASSERT(left.format() == QImage::Format_RGB32);
  Q_ASSERT(right.format() == QImage::Format_RGB32);
  Q_ASSERT(left.size() == right.size());

  QImage img(left.size(), left.format());

  // FIXME: each thread should take a block of scanlines
  QVector<int> lines;
  for (int y = 0; y < img.height(); ++y) lines.append(y);

  QtConcurrent::blockingMap(lines, [&](const int& y) {
    const QRgb* lp = (QRgb*)left.constScanLine(y);
    const QRgb* rp = (QRgb*)right.constScanLine(y);
    QRgb* dstP = (QRgb*)img.scanLine(y);
    const QRgb* dstEnd = dstP + img.width();
    while (dstP < dstEnd) {
      int dr = qRed(*lp) - qRed(*rp);
      int dg = qGreen(*lp) - qGreen(*rp);
      int db = qBlue(*lp) - qBlue(*rp);

      lp++;
      rp++;

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
      int r = (sum >> 10) << 1;       // 6 most significant bits
      int g = ((sum >> 5) & 31) << 3; // 5 middle bits
      int b = (sum & 31) << 3;        // 5 least signficant bits

      *dstP = qRgb(r, g, b);
      dstP++;
    }
  });
  return img;
}

/**
 * @brief Do background loading things
 * @param work      source/destination of the image/things
 * @param fastSeek if true, then seek video in a faster but less accurate way
 * @return true if successful
 */
static void loadImage(QPromise<void>& promise, ImageWork* work, bool fastSeek) {
  Media& m = work->media;
  Q_ASSERT(m.image().isNull());

  const MessageContext ctx(m.path().split("/").last());

  // uint64_t ts = nanoTime();
  // uint64_t then = ts;
  // uint64_t now;

  // now = nanoTime();
  // uint64_t t1 = now - then;
  // then = now;

  QFuture<void> future = promise.future();
  Q_ASSERT(future.isStarted());

  if (future.isCanceled()) {
    qDebug() << work->page << work->index << "cancelled";
    return;
  }

  QImage img;

  if (MediaPage::isDifferenceAnalysis(m)) {
    if (work->args.count() == 2) // could be < 2 if deleting items
      img = differenceImage(work->args.at(0), work->args.at(1), &future);
  } else if (m.type() == Media::TypeImage) {
    if (!MediaPage::isAnalysis(m)) {
      static auto dateFunc = Media::propertyFunc(
          "exif#Photo.DateTimeOriginal,Photo.DateTimeDigitized");
      static auto camFunc = Media::propertyFunc(
          "exif#Image.UniqueCameraModel,Image.Model,Image.Make");

      m.setAttribute("datetime", dateFunc(m).toDateTime().toString());
      m.setAttribute("camera", camFunc(m).toString());

      if (future.isCanceled()) return;

      ImageLoadOptions opt;
      opt.alloc = __imgAlloc;
      img = m.loadImage(QSize(), &future, opt);

      if (img.text("oom") == "true") {
        work->oom = true;
        img = QImage(); // img returned is 1x1
      }
    }

  } else if (m.type() == Media::TypeVideo) {
    VideoContext::DecodeOptions opt;
    img = VideoContext::frameGrab(m.path(), m.matchRange().dstIn, fastSeek, opt, &future);

    if (future.isCanceled()) return;

    VideoContext video;
    video.open(m.path());
    video.metadata().toMediaAttributes(m);

    static auto dateFunc = Media::propertyFunc("ffmeta#creation_time");
    m.setAttribute("date", dateFunc(m).toString());
  }

  if (!img.isNull()) {
    // rgb32 is supposedly best for painting
    QImage::Format fmt = QImage::Format_RGB32;
    if (img.hasAlphaChannel()) fmt = QImage::Format_ARGB32;
    img = img.convertToFormat(fmt);

    m.setImage(img);
    m.setWidth(img.width());
    m.setHeight(img.height());
    m.readMetadata();
  }

  // ts = nanoTime() - ts;
  // ts = ts / 1000000;
  // t1 = t1 / 1000000;
  // if (ts > 1000) {
  //   qWarning("slow %d %d %dms[%d] %dk : %s", work->page->row, work->index, int(ts), int(t1),
  //            int(m.originalSize() / 1024), qUtf8Printable(m.path()));
  // }
}

// image loader stats
// note: do not use for control flow, since multiple MGLWs possible
static int __started = 0;           // queued up
static int __finished = 0;          // done, we have an image
static int __canceled = 0;          // cancelled, no image

MediaGroupListWidget::MediaGroupListWidget(const MediaGroupList& list,
                                           const MediaWidgetOptions& options, QWidget* parent)
    : QListWidget(parent), _options(options), _origCount(list.count()) {

  setViewMode(QListView::IconMode);
  setResizeMode(QListView::Adjust);
  setMovement(QListView::Static);
  setSelectionRectVisible(false);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setMinimumSize(QSize{320, 240});
  setUniformItemSizes(true);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  loadFolderLocks();
  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);

  settings.beginGroup(staticMetaObject.className() + qq(".view"));
  _autoDifference = settings.value(ll("enableDifferenceImage"), false).toBool();

  _itemDelegate = new MediaItemDelegate(this);
  _itemDelegate->setZoom(_zoom);
  _itemDelegate->setPan(QPointF(_panX, _panY));
  _itemDelegate->setScaleMode(settings.value(ll("scaleMode"), 0).toInt());
  setItemDelegate(_itemDelegate);
  setSpacing(_itemDelegate->spacing());

  if (list.count() == 0) {
    qWarning() << "empty list, closing";
    close();
    return;
  }

#ifdef LW_RLIMIT
  // we are going to consume gobs of memory loading uncompressed images
  // make malloc() fail if we take it too far
  float totalKb, freeKb;
  Env::systemMemory(totalKb, freeKb);
  rlim_t limit = freeKb*1024; // // (freeKb - LW_MIN_FREE_MEMORY_KB) * 1024;
  int res = RLIMIT_DATA;
  struct rlimit rlim  = { limit, limit };
  Q_ASSERT(0 == setrlimit(res, &rlim));
#endif

  int id = 1000;
  for (auto& group : list) {
    auto* page = new MediaPage(id++, group, options);

    if (_autoDifference)
      page->addDifferenceAnalysis();

    _list.append(page);
  }

  // we expect libjpeg errors due to i/o cancellation
  // color-correction errors aren't an issue
  // FIXME: if query is running in another thread, as we would like to do in the future,
  //        we would be dropping errors we would probably like to see
  qMessageLogCategoryEnable("qt.gui.imageio.jpeg", false);
  qMessageLogCategoryEnable("qt.gui.icc", false);

  // coalesce item updates (mainly from image loading completion),
  connect(&_updateTimer, &QTimer::timeout, [this] {
    _updateTimer.stop();
    updateItems();
  });

  // coalesce media loading, scrolling produces a lot of unused requests otherwise
  connect(&_loadTimer, &QTimer::timeout,[this]{
    _loadTimer.stop();
    MediaPage* page = currentPage();
    qDebug()  << "loadtimer: page" << page->row  << "preload" << (_preloadPage ? _preloadPage->row : -1);

    if (!page->isLoaded())
      loadMedia(page);
    else if (_preloadPage)
      loadMedia(_preloadPage);
  });

  // take care of oom on the image loaders
  connect(&_oomTimer, &QTimer::timeout, [this] {
    _oomTimer.stop();
    loaderOutOfMemory();
  });

  // we are a memory hog so play nice with the system
  connect(&_oomGuard, &QTimer::timeout, [this] {
    checkMemoryUsage();

    float totalKb, freeKb;
    Env::systemMemory(totalKb, freeKb);

    if (freeKb > LW_MIN_FREE_MEMORY_KB) return;

    qDebug() << "oom-guard releasing memory...";

    cancelOtherLoaders({});
    for (MediaPage* page : _list)
      page->unloadData(false);
    waitLoaders();
    __imgAlloc->compact();

    QMessageBox dialog(QMessageBox::Warning,
                       qq("Low System Memory Detected"),
                       qq("Images have been unloaded to save memory."),
                       QMessageBox::Ok,
                       this);
    Theme::instance().execDialog(&dialog);
    reloadAction();
  });
  _oomGuard.start(1000);

  connect(this, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(openAction()));

  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, &QWidget::customContextMenuRequested, this, &self::execContextMenu);

  settings.endGroup();
  settings.beginGroup(staticMetaObject.className() + qq(".shortcuts"));

  WidgetHelper::addAction(settings, "File/Open File", Qt::Key_X, this, SLOT(openAction()));
  WidgetHelper::addAction(settings, "File/Open Enclosing Folder", Qt::Key_E, this,
                          SLOT(openFolderAction()));

  WidgetHelper::addAction(settings, "File/Rename", Qt::Key_F2, this, SLOT(renameFileAction()));
  WidgetHelper::addAction(settings, "File/Copy Name", Qt::SHIFT | Qt::Key_F2, this,
                          SLOT(copyNameAction()));
  WidgetHelper::addAction(settings, "File/Rename Parent", Qt::Key_F3, this,
                          SLOT(renameFolderAction()));

  WidgetHelper::addAction(settings, "File/Delete File", Qt::Key_D, this, SLOT(deleteAction()))
      ->setEnabled(!(_options.flags & MediaWidgetOptions::FlagDisableDelete));

  WidgetHelper::addAction(settings, "File/Replace File", Qt::Key_F, this, SLOT(replaceAction()))
      ->setEnabled(!(_options.flags & MediaWidgetOptions::FlagDisableDelete));

  QAction* a =
      WidgetHelper::addAction(settings, "File/Move File", Qt::Key_G, this, SLOT(moveFileAction()));
  a->setEnabled(_options.db != nullptr);
  a->setData(ll(";newfolder;"));

  a = WidgetHelper::addAction(settings, "File/Move Parent", Qt::Key_B, this,
                              SLOT(moveFolderAction()));
  a->setEnabled(_options.db != nullptr);
  a->setData(ll(";newfolder;"));

  WidgetHelper::addAction(settings, "File/Copy Image Buffer", Qt::CTRL | Qt::Key_C, this,
                          SLOT(copyImageAction()));
  WidgetHelper::addAction(settings, "File/Set Index Thumbnail", Qt::Key_H, this,
                          SLOT(thumbnailAction()))
      ->setEnabled(_options.db != nullptr);

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Compare/Rotate Items", Qt::Key_R, this, SLOT(rotateAction()));
  WidgetHelper::addAction(settings, "Compare/Remove Item", Qt::Key_A, this, SLOT(clearAction()));
  WidgetHelper::addAction(settings, "Compare/Quality Score", Qt::Key_Q, this,
                          SLOT(qualityScoreAction()));
  WidgetHelper::addAction(settings, "Compare/Template Match", Qt::Key_T, this,
                          SLOT(templateMatchAction()));
  WidgetHelper::addAction(settings, "Compare/Toggle Differences", Qt::Key_Z, this,
                          SLOT(toggleAutoDifferenceAction()));
  WidgetHelper::addAction(settings, "Compare/Compare Videos", Qt::Key_V, this,
                          SLOT(compareVideosAction()));
  WidgetHelper::addAction(settings, "Compare/Compare Audio", Qt::Key_C, this,
                          SLOT(compareAudioAction()));
  WidgetHelper::addAction(settings, "Compare/Reset", Qt::Key_F5, this, SLOT(reloadAction()));

  WidgetHelper::addSeparatorAction(this);

  // for building test/validation data sets
  WidgetHelper::addAction(settings, "Tag/Record Good Match", Qt::Key_Y, this,
                          SLOT(recordMatchTrueAction()));
  WidgetHelper::addAction(settings, "Tag/Record Bad Match", Qt::Key_N, this,
                          SLOT(recordMatchFalseAction()));
  WidgetHelper::addAction(settings, "Tag/Forget Weed", Qt::Key_W, this, SLOT(forgetWeedsAction()));
  WidgetHelper::addAction(settings, "Tag/Add to Negative Matches", Qt::Key_Minus, this,
                          SLOT(negMatchAction()))
      ->setEnabled(_options.db != nullptr);
  WidgetHelper::addAction(settings, "Tag/Add All to Negative Matches", Qt::SHIFT | Qt::Key_Minus,
                          this, SLOT(negMatchAllAction()))
      ->setEnabled(_options.db != nullptr);

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Display/Cycle Scale Mode", Qt::Key_S, this,
                          SLOT(scaleModeAction()));
  WidgetHelper::addAction(settings, "Display/Zoom In", Qt::Key_9, this, SLOT(zoomInAction()));
  WidgetHelper::addAction(settings, "Display/Zoom Out", Qt::Key_7, this, SLOT(zoomOutAction()));

  WidgetHelper::addAction(settings, "Display/Reset Zoom", Qt::Key_5, this, SLOT(resetZoomAction()));
  WidgetHelper::addAction(settings, "Display/Pan Left", Qt::Key_4, this, SLOT(panLeftAction()));
  WidgetHelper::addAction(settings, "Display/Pan Right", Qt::Key_6, this, SLOT(panRightAction()));
  WidgetHelper::addAction(settings, "Display/Pan Up", Qt::Key_8, this, SLOT(panUpAction()));
  WidgetHelper::addAction(settings, "Display/Pan Down", Qt::Key_2, this, SLOT(panDownAction()));
  WidgetHelper::addAction(settings, "Display/Cycle Min Filter", Qt::Key_1, this,
                          SLOT(cycleMinFilter()));
  WidgetHelper::addAction(settings, "Display/Cycle Max Filter", Qt::Key_3, this,
                          SLOT(cycleMagFilter()));
  WidgetHelper::addAction(settings, "Display/More per Page", Qt::Key_BracketRight, this,
                          SLOT(increasePageSize()));
  WidgetHelper::addAction(settings, "Display/Less per Page", Qt::Key_BracketLeft, this,
                          SLOT(decreasePageSize()));

  WidgetHelper::addSeparatorAction(this);

  QString text;
  switch (_options.selectionMode) {
    case MediaWidgetOptions::SelectSearch:
      text = "Navigate/Search Selected";
      break;
    case MediaWidgetOptions::SelectOpen:
      text = "Navigate/Open Selected";
      break;
    case MediaWidgetOptions::SelectExitCode:
      text = "Navigate/Choose Selected";
      break;
  }
  WidgetHelper::addAction(settings, text, Qt::Key_Return, this, SLOT(chooseAction()));

  WidgetHelper::addAction(settings, "Navigation/Toggle Folder Lock", Qt::Key_L, this,
                          SLOT(toggleFolderLockAction()));

  WidgetHelper::addAction(settings, "Navigate/Browse Parent", Qt::Key_Tab, this,
                          SLOT(browseParentAction()))
      ->setEnabled(_options.db != nullptr);

  WidgetHelper::addAction(settings, "Navigate/Forward", Qt::ALT | Qt::Key_Down, this,
                          SLOT(nextGroupAction()))
      ->setEnabled(_list.count() > 1);
  WidgetHelper::addAction(settings, "Navigate/Back", Qt::ALT | Qt::Key_Up, this,
                          SLOT(prevGroupAction()))
      ->setEnabled(_list.count() > 1);
  WidgetHelper::addAction(settings, "Navigate/Jump Forward", Qt::Key_PageDown, this,
                          SLOT(jumpForwardAction()))
      ->setEnabled(_list.count() > 1);
  WidgetHelper::addAction(settings, "Navigate/Jump Back", Qt::Key_PageUp, this,
                          SLOT(jumpBackAction()))
      ->setEnabled(_list.count() > 1);
  WidgetHelper::addAction(settings, "Navigate/Jump to Start", Qt::Key_Home, this,
                          SLOT(jumpToStartAction()))
      ->setEnabled(_list.count() > 1);
  WidgetHelper::addAction(settings, "Navigate/Jump to End", Qt::Key_End, this,
                          SLOT(jumpToEndAction()))
      ->setEnabled(_list.count() > 1);

  WidgetHelper::addSeparatorAction(this);

  WidgetHelper::addAction(settings, "Window/Close Window", Qt::CTRL | Qt::Key_W, this,
                          SLOT(close()));
  WidgetHelper::addAction(settings, "Window/Close Window (Alt)", Qt::Key_Escape, this,
                          SLOT(close()));

  // qt maps ctrl to meta; meta+ctrl is default for spotlight search
#ifdef Q_OS_MACOS
  auto key = Qt::META | Qt::Key_Space;
#else
  auto key = Qt::CTRL | Qt::Key_Space;
#endif
  WidgetHelper::addAction(settings, "Window/Show Context Menu", key, this, [this] {
    QPoint local = frameRect().center();
    auto items = selectedItems();
    QListWidgetItem* item = items.count() > 0 ? items.at(0) : nullptr;
    if (item) local = this->visualItemRect(item).center();
    auto* evt = new QContextMenuEvent(QContextMenuEvent::Keyboard, local, QPoint());
    qApp->sendEvent(this, evt);
  });

  for (auto* act : actions()) {
    const QString label = act->text();
    const auto parts = QStringView(label).split('/');
    if (parts.count() > 1) {
      act->setProperty("group", parts[0].toString());
      act->setText(parts[1].toString());
    }
  }

  _maximized = WidgetHelper::restoreGeometry(this);

  loadRow(0);

  int modelIndex = _list.at(0)->defaultModelIndex();
  if (modelIndex >= 0)
    setCurrentIndex(model()->index(modelIndex, 0));

  // get info text box height so it won't clip
  {
    QImage qImg(640, 480, QImage::Format_RGB32);

    const auto green = qRgb(0, 0, 255);
    QPainter painter;
    painter.begin(&qImg);
    painter.fillRect(qImg.rect(), green);
    Theme::instance().drawRichText(&painter, qImg.rect(), item(0)->text());
    painter.end();

    int y;
    for (y = qImg.height() - 1; y >= 0; --y)
      if (qImg.pixel(10, y) != green) break;

    qDebug() << "found text box height:" << y;
    _itemDelegate->setTextHeight(y);
  }

  // get info text box height so it won't clip
  {
    QImage qImg(640, 480, QImage::Format_RGB32);

    const auto green = qRgb(0, 0, 255);
    QPainter painter;
    painter.begin(&qImg);
    painter.fillRect(qImg.rect(), green);
    Theme::instance().drawRichText(&painter, qImg.rect(), item(0)->text());
    painter.end();

    int y;
    for (y = qImg.height() - 1; y >= 0; --y)
      if (qImg.pixel(10, y) != green) break;

    qDebug() << "found text box height:" << y;
    _itemDelegate->setTextHeight(y);
  }
}

MediaGroupListWidget::~MediaGroupListWidget() {
  qDebug("~MediaGroupListWidget");
  qMessageLogCategoryEnable("qt.gui.imageio.jpeg", true);
  qMessageLogCategoryEnable("qt.gui.icc", true);

  saveFolderLocks();
  WidgetHelper::saveGeometry(this);

  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);

  settings.beginGroup(staticMetaObject.className() + qq(".view"));
  settings.setValue(ll("enableDifferenceImage"), _autoDifference);
  settings.setValue(ll("scaleMode"), _itemDelegate->scaleMode());

  waitLoaders();
  for (MediaPage* page : _list) delete page;
  for (MediaPage* page : _deletedPages) delete page;
  __imgAlloc->compact();
  Q_ASSERT(__imgAlloc->freeKb() == 0);
}

//---------- events -------------------//

void MediaGroupListWidget::closeEvent(QCloseEvent* event) {
  waitLoaders();
  super::closeEvent(event);
  this->deleteLater();
}

void MediaGroupListWidget::keyPressEvent(QKeyEvent* event) {
  // up/down key moves to the next group if we're on the first/last row of the group
  // note: Mac OS X will set KeypadModifier, so check for valid modifiers too
  const auto validModifiers = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier
                              | Qt::MetaModifier;

  bool modifiers = event->modifiers() & validModifiers;
  const QModelIndexList list = selectedIndexes();

  if (list.count() == 1 && !modifiers) {
    const QModelIndex& curr = list.first();
    if (event->key() == Qt::Key_Down) {
      QModelIndex next = moveCursor(QAbstractItemView::MoveDown, Qt::NoModifier);
      if (curr == next && _currentRow + 1 < _list.count()) return loadRow(_currentRow + 1);
    } else if (event->key() == Qt::Key_Up) {
      QModelIndex next = moveCursor(QAbstractItemView::MoveUp, Qt::NoModifier);
      if (curr == next && _currentRow - 1 >= 0) return loadRow(_currentRow - 1);
    }
  }

  // note: super must also take event; moveCursor doesn't move the selection
  super::keyPressEvent(event);
}

void MediaGroupListWidget::paintEvent(QPaintEvent* event) {
  super::paintEvent(event);

  static const QColor c = Theme::instance().palette().text().color();
  static const QColor barColor = QColor(c.red(), c.green(), c.blue(),
                                        Theme::INFO_OPACITY*255);
  QPainter painter(this->viewport());
  float maxWidth = this->viewport()->width();
  painter.fillRect(0,0, maxWidth - (_list.count()*maxWidth/_origCount), 10, barColor);
}

void MediaGroupListWidget::wheelEvent(QWheelEvent* event) {
  const int yDelta = event->angleDelta().y();
  const int xDelta = event->angleDelta().x();

  if (yDelta != 0) {
    if (yDelta > 0)
      loadRow(_currentRow - 1);
    else
      loadRow(_currentRow + 1);
    event->accept();
  } else if (xDelta != 0) {
    if (xDelta > 0) {
      rotateAction();
      event->accept();
    }
  }
}

//------------- public ------------------

bool MediaGroupListWidget::selectItem(const Media& item) {
  int rowIndex = -1;
  int groupIndex = -1;
  for (int i = 0; i < _list.count(); ++i)
    if (0 <= (groupIndex = _list.at(i)->group.indexOf(item))) {
      rowIndex = i;
      break;
    }
  if (rowIndex <= 0) return false;

  loadRow(rowIndex);
  setCurrentIndex(model()->index(groupIndex, 0));

  return true;
}

void MediaGroupListWidget::show() { Theme::instance().showWindow(this, _maximized); }

void MediaGroupListWidget::close() {
  super::close();
  this->deleteLater(); // why? seems unsafe
}

//-------- context menu ----------//

QMenu* MediaGroupListWidget::dirMenu(const char* slot) {
  QMenu* dirs = MenuHelper::dirMenu(_options.db->path(), this, slot, 20, 3);

  QSet<QString> groupDirs;
  const MediaGroup& group = currentGroup();

  // add shortcuts for dirs in the current row,
  // in case they are buried it is nice to have
  int selectedIndex = -1;
  const QModelIndex index = currentIndex();
  if (index.isValid()) selectedIndex = index.row();

  for (int i = 0; i < group.count(); ++i)
    if (i != selectedIndex) {
      const auto& m = group.at(i);
      if (!MediaPage::isAnalysis(m)) {
        QString path = m.dirPath();
        if (m.isArchived()) {
          m.archivePaths(&path);
          auto list = path.split(lc('/'));
          list.removeLast();
          path = list.join(lc('/'));
        }
        groupDirs.insert(path);
      }
    }

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
  QMenu* menu = new QMenu;
  if (_options.db) {
    QMenu* dirs = dirMenu(SLOT(moveFileAction()));
    QAction* act = new QAction("Move File to ...", this);
    act->setMenu(dirs);
    act->setEnabled( selectionIsMoveable() );
    menu->addAction(act);

    dirs = dirMenu(SLOT(moveFolderAction()));
    act = new QAction("Move Parent to ...", this);
    act->setMenu(dirs);
    act->setEnabled( selectionParentIsMoveable() );
    menu->addAction(act);
  }

  QHash<QString, QMenu*> groups;
  const QList<QAction*> actions = this->actions();
  for (QAction* act : actions) {
    QMenu* actionMenu = menu;
    const QString group = act->property("group").toString();

    if (!group.isEmpty()) {
      actionMenu = groups.value(group);
      if (!actionMenu) {
        actionMenu = new QMenu(group);
        groups.insert(group, actionMenu);
        menu->addMenu(actionMenu);
      }
    }
    actionMenu->addAction(act);
  }

  ShadeWidget shade(this);
  menu->exec(this->mapToGlobal(p));
  delete menu;
}

//---------- file actions -------------//

void MediaGroupListWidget::openAction() {
  const MediaGroup& group = selectedMedia();
  if (group.count() != 1) return;

  const Media& m = group[0];
  float seek = 0;

  if (m.type() == Media::TypeVideo) {
    int dstIn = m.matchRange().dstIn;
    float fps = m.attributes().value("fps").toFloat();
    if (dstIn > 0 && fps > 0.0f) seek = m.matchRange().dstIn / fps;
    else qDebug() << "cannot seek video: no position or fps given";
  }
  Media::openMedia(m, seek);
}

void MediaGroupListWidget::openFolderAction() {
  const QList<QListWidgetItem*> items = selectedItems();
  if (items.count() != 1) return;

  const MediaGroup& group = currentGroup();
  const Media& m = group[items[0]->type()];

  Media::revealMedia(m);
}

void MediaGroupListWidget::deleteAction() { removeSelection(true); }

void MediaGroupListWidget::replaceAction() { removeSelection(true, true); }

void MediaGroupListWidget::removeSelection(bool deleteFiles, bool replace) {
  QList<QListWidgetItem*> items = selectedItems();
  Q_ASSERT((!deleteFiles && !replace) || (deleteFiles && !replace) || (deleteFiles && replace));

  const MediaPage* page = currentPage();

  const int groupCount = page->countNonAnalysis();

  // guard against deleting everything
  if (deleteFiles && items.count() == groupCount) {
    qWarning() << "assuming unintentional deletion of entire group; no action taken";
    return;
  }

  if (deleteFiles && replace && items.count() == 1 && !page->isPair()) {
    qWarning() << "delete+replace is only possible with 1 selection in 2 items";
    return;
  }

  QSet<int> removedIndices; // group indexes
  QSet<int> removedIds; // database media ids

  for (int i = 0; i < items.count(); ++i) {
    int index = items[i]->type();
    Q_ASSERT(index >=0 && index < groupCount);
    const Media& m = page->group[index];

    QString path = m.path();
    if (m.isArchived()) m.archivePaths(&path);

    if (!deleteFiles) {
      removedIndices.insert(index);
      if (m.isValid())
        removedIds.insert(m.id());
      continue;
    }

    if (replace && m.isArchived()) {
      qWarning() << "delete+replace for archives unsupported";
      return;
    }

    if (_lockedFolders.contains(m.dirPath())) {
      QMessageBox dialog(
          QMessageBox::Warning, qq("Delete Item: Folder Locked"),
          qq("\"%1\" is locked for deletion.\n\n")
              .arg(m.dirPath()),
          QMessageBox::Ok, this);
      (void)Theme::instance().execDialog(&dialog);
      continue;
    }

    {
      static bool skipDeleteConfirmation = false;
      int button = 0;
      const QString fileName = QFileInfo(path).fileName();
      if (m.isArchived()) {
        QMessageBox dialog(QMessageBox::Warning, qq("Delete Zip Confirmation"),
                           qq("The selected file is a member of \"%1\"\n\n"
                              "Modification of zip archives is unsupported. Move the "
                              "entire zip to the trash?")
                               .arg(fileName),
                           QMessageBox::No | QMessageBox::Yes, this);
        button = Theme::instance().execDialog(&dialog);
      } else if (skipDeleteConfirmation) {
        button = QMessageBox::Yes;
      } else {
        QMessageBox dialog(QMessageBox::Warning, qq("Delete File Confirmation"),
                           qq("Move this file to the trash?\n\n%1").arg(fileName),
                           QMessageBox::No | QMessageBox::Yes | QMessageBox::YesToAll, this);
        button = Theme::instance().execDialog(&dialog);
      }

      if (button == QMessageBox::YesToAll)
        skipDeleteConfirmation = true;
      else if (button != QMessageBox::Yes)
        return;
    }

    if (!DesktopHelper::moveToTrash(path)) return;

    removedIndices.insert(index);

    if (!_options.db) continue;

    if (m.isArchived()) {
      QString like = path;
      like.replace("%", "\\%").replace("_", "\\_");
      like += ":%";
      MediaGroup zipGroup = _options.db->mediaWithPathLike(like);
      _options.db->remove(zipGroup);

      for (auto& mm : qAsConst(zipGroup))
        removedIds.insert(mm.id());

      if (_options.trackWeeds)
        qWarning() << "Cannot track weeds when deleting zip files";
    } else {
      if (m.isValid()) {
        int mediaId = m.id();
        _options.db->remove(mediaId);
        removedIds.insert(mediaId);
      }

      if (!page->isPair()) continue;

      // we can do extra stuff on pairs of items
      if (_options.trackWeeds)
        page->addWeed(index);

      if (replace)
        currentPage()->replaceFile(index);

    } // not archived
  } // for each selected item

  if (removedIndices.count() <= 0) return;

  if (removedIds.count() > 0) {
    // remove anything in the full list with the same id
    for (MediaPage* p : _list)
      p->removeIds(removedIds);
  } else {
    // remove deleted indices; we cannot remove from full list since there
    // is no reliable identifier e.g. media.path() is mutable
    currentPage()->removeIndices(removedIndices);
  }

  itemCountChanged();
}

bool MediaGroupListWidget::selectionIsMoveable() {
  const auto& selection = selectedMedia();
  if (selection.count() <= 0) return false;

  for (const auto& m : selection)
    if (m.isArchived()) return false;

  return true;
}

bool MediaGroupListWidget::selectionParentIsMoveable() {
  const auto& selection = selectedMedia();
  if (selection.count() <= 0) return false;

  if (!_options.db) return true;

  const QString& dbPath = QDir(_options.db->path()).absolutePath();

  for (const auto& m : selection) {
    const QString absSrc = QDir(dbPath).absoluteFilePath(m.dirPath());
    if (!absSrc.startsWith(dbPath)) return false;
    if (absSrc == dbPath) return false;
  }

  return true;
}

bool MediaGroupListWidget::renameWarning() {
  if (!_options.db) {
    QMessageBox dialog(QMessageBox::Warning, qq("Rename Without Database?"),
                       qq("Renaming without a database will invalidate the index."),
                       QMessageBox::Yes | QMessageBox::No, this);

    dialog.setDefaultButton(QMessageBox::No);

    int button = Theme::instance().execDialog(&dialog);
    if (button != QMessageBox::Yes) return true;
  }
  return false;
}

void MediaGroupListWidget::moveFileAction() {
  Q_ASSERT(_options.db);

  QAction* action = dynamic_cast<QAction*>(sender());
  Q_ASSERT(action);

  QString dirPath = action->data().toString();

  if (dirPath == ll(";newfolder;"))
    dirPath = Theme::instance().getExistingDirectory(qq("Move File"), qq("Destination:"),
                                                     _options.db->path(), this);
  if (dirPath.isEmpty()) return;

  for (Media& m : selectedMedia()) {
    QString path = m.path();
    if (_options.db->move(m, dirPath)) updateMedia(path, m);
  }
  loadRow(_currentRow);  // path in window title may have changed
}

void MediaGroupListWidget::renameFileAction() {
  const MediaGroup& group = currentGroup();

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
        QString fileName;
        m2.archivePaths(nullptr, &fileName);
        maybeAppend(completions, fileName);
      } else
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

    QString newName = info.fileName();
    QInputDialog dialog(this);
    int result = Theme::instance().execInputDialog(&dialog, qq("Rename File"), qq("Rename File"),
                                                   newName, completions);

    if (result != QInputDialog::Accepted) return;

    newName = dialog.textValue();
    if (newName == info.fileName()) return;

    QString path = m.path();
    if (_options.db) {
      if (_options.db->rename(m, newName))
        updateMedia(path, m);
      else
        qWarning() << "rename via database failed";
    } else {
      QDir parentDir = info.dir();
      if (parentDir.rename(info.fileName(), newName)) {
        m.setPath(parentDir.absoluteFilePath(newName));
        updateMedia(path, m);
      } else
        qWarning() << "rename via filesystem failed";
    }
  }
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
  if (other->isArchived())
    other->archivePaths(nullptr, &otherName);
  else
    otherName = other->name();  // TODO: should name() work with archives?

  QString newName = QFileInfo(otherName).completeBaseName() + "." + info.suffix();
  const QString oldPath = selected->path();
  if (_options.db) {
    if (_options.db->rename(*selected, newName))
      updateMedia(oldPath, *selected);
    else
      qWarning() << "rename via database failed";
  } else {
    QDir dir = info.dir();
    if (dir.rename(oldPath, newName)) {
      selected->setPath(dir.absoluteFilePath(newName));
      updateMedia(oldPath, *selected);
    } else
      qWarning() << "rename via filesystem failed";
  }
}

void MediaGroupListWidget::moveDatabaseDir(const Media& child, const QString& newName) {
  QDir dir = QFileInfo(child.path()).dir();

  QString newPath = newName;
  QString absSrcPath = QFileInfo(dir.absolutePath()).absoluteFilePath();
  if (child.isArchived()) {
    child.archivePaths(&absSrcPath);
    dir = QFileInfo(absSrcPath).dir();  // dir otherwise may refer to a zip dir
    if (!newPath.endsWith(".zip")) newPath += ".zip";
  } else if (!dir.cdUp()) {
    // use parent for direct rename/updating
    qWarning() << "cdUp() failed";
    return;
  }

  qDebug() << absSrcPath << "=>" << newPath;
  QString absDstPath;
  if (_options.db) {
    absDstPath = QFileInfo(QDir(_options.db->path()).absoluteFilePath(newPath)).absoluteFilePath();
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

  for (MediaPage* p : _list)
    p->setParentPath(absSrcPath, absDstPath);

  // updateItems() won't work since we may have changed window title
  loadRow(_currentRow);
}

void MediaGroupListWidget::moveFolderAction() {
  Q_ASSERT(_options.db);

  QAction* action = dynamic_cast<QAction*>(sender());
  Q_ASSERT(action);

  QString dirPath = action->data().toString();

  if (dirPath == ";newfolder;")
    dirPath = Theme::instance().getExistingDirectory(qq("Move Parent"), qq("Destination:"),
                                                     _options.db->path(), this);

  if (dirPath.isEmpty()) return;

  QSet<QString> moved;

  for (Media& m : selectedMedia()) {
    QString srcPath;
    if (m.isArchived())
      m.archivePaths(&srcPath);
    else
      srcPath = m.dirPath();

    if (moved.contains(srcPath))  // already moved
      continue;

    const QString dstPath = dirPath + "/" + QFileInfo(srcPath).fileName();
    moveDatabaseDir(m, dstPath);
    moved += srcPath;
  }
}

void MediaGroupListWidget::renameFolderAction() {
  const auto sel = selectedMedia();
  if (sel.count() != 1) return;

  if (renameWarning()) return;

  const Media& m = sel[0];

  QString newName;
  QStringList completions;
  QDir parentDir;

  if (m.isArchived()) {  // first completion is selection
    QString zip;
    m.archivePaths(&zip);
    QFileInfo info(zip);
    newName = info.fileName();
    parentDir = info.dir();
  } else {
    QFileInfo info(m.path());
    newName = info.dir().dirName();
    parentDir = info.dir();
    parentDir.cdUp();
  }
  completions += newName;

  //  for (const auto& ii : qAsConst(currentPage())) {
  //    const auto it = ii.attributes().find("group");
  //    if (it != ii.attributes().end())
  //      maybeAppend(completions, it.value());
  //  }

  for (const Media& ii : currentGroup()) {
    if (ii.isArchived()) {
      QString zipPath;
      ii.archivePaths(&zipPath);
      const QFileInfo info(zipPath);
      QString zipName = QFileInfo(zipPath).fileName();
      if (!m.isArchived()) zipName = zipName.mid(0, zipName.lastIndexOf("."));
      maybeAppend(completions, zipName);
    } else {
      QString dirName = QFileInfo(ii.path()).dir().dirName();
      if (m.isArchived()) dirName += ".zip";
      maybeAppend(completions, dirName);
    }
  }

  QInputDialog dialog(this);
  int result = Theme::instance().execInputDialog(&dialog, qq("Rename Folder/Zip"), qq("Rename Folder/Zip"),
                                                 newName, completions);

  if (result != QInputDialog::Accepted) return;

  // new path is not index-relative...pass absolute
  QString newPath = parentDir.absoluteFilePath(dialog.textValue());
  moveDatabaseDir(m, newPath);
}

void MediaGroupListWidget::copyImageAction() {
  auto sel = selectedMedia();
  if (sel.count() <= 0) return;
  const Media& m = sel[0];
  qApp->clipboard()->setImage(m.image());
}

void MediaGroupListWidget::thumbnailAction() {
  auto sel = selectedMedia();
  if (sel.count() != 1) return;
  CropWidget::setIndexThumbnail(*_options.db, sel[0], this, false);
}

//---------- compare actions -------------//

void MediaGroupListWidget::rotateAction() {
  currentPage()->rotate();
  updateItems();
}

void MediaGroupListWidget::qualityScoreAction() {
  MediaGroup& group = _list[_currentRow]->group;
  QList<QFuture<void>> work;

  for (Media& m : group)
    if (!m.image().isNull())
      work += QtConcurrent::run([&]() {
        // no-reference quality score
        double score = qualityScore(m);
        m.setAttribute("quality-score", QString::number(score));

        // jpeg codec quality factor
        if (m.type() != Media::TypeImage || MediaPage::isAnalysis(m))  // raw images can't be checked
          return;

        auto* io = m.ioDevice();
        if (!io) return;

        // EstimateJpegQuality does a lot of small io's, can be very
        // slow on network filesystems; so read the whole file to a buffer device
        if (!io->open(QIODevice::ReadOnly)) {
          delete io;
          return;
        }

        QByteArray buffer = io->readAll();
        delete io;
        io = new QBuffer(&buffer);

        // if it isn't jpeg we don't get jq.ok
        const JpegQuality jq = EstimateJpegQuality(io);
        if (jq.ok && jq.isReliable) m.setAttribute("jpeg-quality", QString::number(jq.quality));
      });

  qApp->setOverrideCursor(QCursor(Qt::WaitCursor));
  for (auto& w : work) w.waitForFinished();
  qApp->restoreOverrideCursor();

  updateItems();
}

void MediaGroupListWidget::templateMatchAction() {
  MediaGroup& group = _list[_currentRow]->group;

  if (group.count() < 2) return;

  // we selected one, guess the other one in the pair
  // probably the first image, unless selection is the first one
  QList<QListWidgetItem*> items = selectedItems();
  if (items.count() == 1) {
    MediaGroup filtered;
    int selectedIndex = items[0]->type();
    int otherIndex = selectedIndex == 0 ? (selectedIndex + 1) % group.count() : 0;
    filtered.append(group[otherIndex]);
    filtered.append(group[selectedIndex]);
    group = filtered;
  }

  // no selection, the pair is {0,1}
  if (group.count() > 2) group.remove(2, group.count() - 2);

  // clear roi, template matcher sets it
  group[0].setRoi(QVector<QPoint>());
  group[1].setRoi(QVector<QPoint>());

  if (group[0].image().isNull() || group[1].image().isNull()) return;

  // look for first image in the second image
  int tmplIndex = 0;
  int targetIndex = 1;

  // set threshold high to consider all matches with a transform,
  // regardless if it is a good match or not, since we can visually evaluate
  SearchParams params = _options.params;
  params.tmThresh = 64;

  MediaGroup haystack;
  haystack.append(group[targetIndex]);

  TemplateMatcher().match(group[tmplIndex], haystack, params);
  if (haystack.count() > 0) group[targetIndex] = haystack[0];

  // reload since we may have deleted items
  itemCountChanged();
}

void MediaGroupListWidget::toggleAutoDifferenceAction() {
  for (MediaPage* p : _list)
    if (_autoDifference)
      p->removeAnalysis();
    else
      p->addDifferenceAnalysis();

  _autoDifference = !_autoDifference;
  loadRow(_currentRow);
}

void MediaGroupListWidget::compareVideosAction() {
  QList<QListWidgetItem*> items = selectedItems();
  if (items.count() < 1) return;
  if (_list.count() < 1) return;

  MediaGroup group = currentGroup();
  if (group.count() < 2) group.append(group[0]);

  Media left = group[0];
  Media right = group[items[0]->type()];

  if (left.type() != Media::TypeVideo || right.type() != Media::TypeVideo) return;

  MatchRange range(0, 0, -1);

  // if left is needle, right is match
  if (left.matchRange().srcIn < 0)
    range = right.matchRange();

  VideoCompareWidget* comp = new VideoCompareWidget(left, right, range, _options);
  comp->setAttribute(Qt::WA_DeleteOnClose);
  comp->show();
}

void MediaGroupListWidget::compareAudioAction() {
  QList<QListWidgetItem*> items = selectedItems();
  if (items.count() != 1) return;

  const MediaGroup& group = currentGroup();
  if (group.count() < 2) return;

  Media left = group[0];
  Media right = group[items[0]->type()];

  DesktopHelper::compareAudio(left.path(), right.path());
}

void MediaGroupListWidget::reloadAction() {
  currentPage()->reset();
  resetZoom();
  itemCountChanged();
}

//----------tagging ----------------//

void MediaGroupListWidget::recordMatch(bool matched) {
  const MediaGroup& group = currentGroup();
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

  if (_currentRow < _list.count() - 1)
    loadRow(_currentRow + 1);
  else
    close();
}

bool MediaGroupListWidget::addNegMatch(bool all) {
  const MediaPage* p = currentPage();

  if (all || p->isPair()) {
    p->setNegativeMatch();
  } else {
    QListWidgetItem* item = currentItem();
    if  (!item) return false;

    int otherIndex = item->type();
    if (otherIndex <= 0) return false;

    p->setNegativeMatch(0, otherIndex);
  }
  return true;
}

void MediaGroupListWidget::forgetWeedsAction() {
  if (!_options.db) return;

  const MediaGroup group = selectedMedia();
  QSet<QString> removed;
  for (const Media& m : group)
    if (_options.db->removeWeed(m)) removed.insert(m.md5());

  for (MediaPage* p : _list)
    for (Media& m : p->group)
      if (removed.contains(m.md5())) m.setIsWeed(false);

  updateItems();
}

//--------- display actions ----------------//

void MediaGroupListWidget::scaleModeAction() {
  _itemDelegate->cycleScaleMode();
  repaint();
}

void MediaGroupListWidget::zoomInAction() {
  _zoom *= LW_ZOOM_STEP;
  _zoom = qMax(_zoom, 0.001);
  _itemDelegate->setZoom(_zoom);
  update();
}

void MediaGroupListWidget::zoomOutAction() {
  _zoom *= 1+(1-LW_ZOOM_STEP);
  _zoom = qMin(1.0, _zoom);
  _itemDelegate->setZoom(_zoom);
  update();
}

void MediaGroupListWidget::panLeftAction() {
  _panX -= LW_PAN_STEP;
  _itemDelegate->setPan({_panX, _panY});
  update();
}

void MediaGroupListWidget::panRightAction() {
  _panX += LW_PAN_STEP;
  _itemDelegate->setPan({_panX, _panY});
  update();
}

void MediaGroupListWidget::panUpAction() {
  _panY -= LW_PAN_STEP;
  _itemDelegate->setPan({_panX, _panY});
  update();
}

void MediaGroupListWidget::panDownAction() {
  _panY += LW_PAN_STEP;
  _itemDelegate->setPan({_panX, _panY});
  update();
}

void MediaGroupListWidget::resetZoom() {
  _itemDelegate->setZoom(_zoom=1.0);
  _itemDelegate->setPan({_panX=0.0, _panY=0.0});
}

void MediaGroupListWidget::resetZoomAction() {
  resetZoom();
  update();
}

void MediaGroupListWidget::cycleMinFilter() {
  _itemDelegate->cycleMinFilter();
  update();
}

void MediaGroupListWidget::cycleMagFilter() {
  _itemDelegate->cycleMagFilter();
  update();
}

void MediaGroupListWidget::resizePage(bool more) {
  // start freeing up memory now
  cancelOtherLoaders({});

  const MediaPage* oldPage = currentPage();

  // remember what we were on, restore after resizing
  const MediaGroup sel = selectedMedia();
  const Media lastViewed = sel.count()>0 ? sel[0] : oldPage->group.at(oldPage->count()/2);
  const int oldSize = _list.at(0)->count();

  // preset of small sizes, multiples of largest size thereafter
  const int numSizes = 5;
  const int sizes[numSizes] = {1, 2, 4, 6, 12};
  const int scale = sizes[numSizes - 1];

  int newSize;

  if (oldSize >= scale * 2)
    newSize = ((oldSize / scale) + (more ? 1 : -1)) * scale;
  else {
    if (more) {
      newSize = scale * 2;
      for (auto p : sizes)
        if (oldSize < p) {
          newSize = p;
          break;
        }
    } else {
      newSize = sizes[0];
      for (auto p : sizes)
        if (oldSize > p) newSize = p;
    }
  }

  int id = _list.last()->id + 1; // ensure id's are not repeated
  QList<MediaPage*> newList;
  MediaGroup newGroup;
  for (const MediaPage* p : qAsConst(_list))
    for (const Media& m : p->group)
      if (!MediaPage::isAnalysis(m)) {
        newGroup += m;
        if (newGroup.count() == newSize) {
          newList += new MediaPage(id++, newGroup, _options);
          newGroup.clear();
        }
      }
  if (newGroup.count() > 0)
    newList += new MediaPage(id++, newGroup, _options);

  deletePages();
  _list = newList;

  // find the page that contains the selected item
  const MediaPage* currentPage = nullptr;
  int currentIndex = -1;
  for (const MediaPage* p : qAsConst(_list)) {
    int index = p->group.indexOf(lastViewed);
    if (index < 0) continue;

    currentPage = p;
    currentIndex =  index;
    break;
  }

  _currentRow = _list.indexOf(currentPage);
  Q_ASSERT(_currentRow >= 0);

  // free up memory we lost track of because _lruRows was invalided
  for (MediaPage* p : _list) {
    if (p != currentPage)
      p->unloadData(false);
  }

  __imgAlloc->compact();

  _loadedPages.clear();
  _autoDifference = false;

  // no preloading since next resize would just invalidate it immediately
  loadRow(_currentRow, false);

  setCurrentIndex(model()->index(currentIndex, 0));
}

//---------- navigation action -------------//

void MediaGroupListWidget::chooseAction() {
  MediaGroup g = selectedMedia();
  if (!g.empty()) emit mediaSelected(g);
}

void MediaGroupListWidget::toggleFolderLockAction() {
  const MediaGroup g = selectedMedia();
  for (auto& m : g) {
    const QString dirPath = m.dirPath();
    const auto it  = _lockedFolders.find(dirPath);
    if (it != _lockedFolders.end())
      _lockedFolders.erase(it);
    else
      _lockedFolders.insert(dirPath);
  }
  updateItems();
}

void MediaGroupListWidget::loadFolderLocks() {
  if (!_options.db) return;

  QFile f(_options.db->indexPath() + lc('/') + _FOLDER_LOCKS_FILE);
  if (!f.open(QFile::ReadOnly)) {
    qDebug() << f.errorString();
    return;
  }

  const QDir base(_options.db->path());
  const auto lines = f.readAll().split('\n');
  for (auto& l : lines) {
    if (l.startsWith("Version:")) continue;
    if (l.isEmpty()) continue;
    QString path = base.cleanPath(base.absoluteFilePath(l));
    if (QFileInfo(path).exists())
      _lockedFolders.insert(path);
  }
}

void MediaGroupListWidget::saveFolderLocks() const {
  if (!_options.db) return;

  QFile f(_options.db->indexPath() + lc('/') + _FOLDER_LOCKS_FILE);
  if (!f.open(QFile::WriteOnly|QFile::Truncate)) {
    qDebug() << f.errorString();
    return;
  }

  const QDir base(_options.db->path());
  f.write("Version: 1\n");
  for (auto& path : _lockedFolders)
    f.write(qPrintable(base.relativeFilePath(path) + lc('\n')));
}

void MediaGroupListWidget::browseParentAction() {
#ifdef QT_TESTLIB_LIB
  qWarning()
      << "browseParentAction() disabled for unit tests";  // MediaBrowser dependency breaks tests
#else
  const MediaGroup g = selectedMedia();
  if (g.count() < 1) return;
  if (!_options.db) {
    qWarning() << "database is required";
    return;
  }

  const Media& m = g.first();
  QString path;
  if (m.isArchived())
    m.archivePaths(&path);
  else
    path = m.dirPath();

  MediaGroup siblings = _options.db->mediaWithPathLike(path + lc('%'));
  Media::sortGroup(siblings, {qq("path")});

  MediaWidgetOptions options = _options;
  options.selectOnOpen = m;

  MediaBrowser::show(Media::splitGroup(siblings, options.maxPerPage), MediaBrowser::ShowNormal,
                     options);
#endif
}

//---------- items & selections ---------------//

const MediaGroup& MediaGroupListWidget::currentGroup() const {
  return currentPage()->group;
}

MediaGroup MediaGroupListWidget::selectedMedia() {
  if (_list.count() < 1) return MediaGroup();

  const QList<QListWidgetItem*> items = selectedItems();
  const MediaGroup& group = currentGroup();

  MediaGroup selected;
  for (int i = 0; i < items.count(); i++) {
    int index = items[i]->type();
    Media m = group[index];
    selected.append(m);
  }

  return selected;
}

bool MediaGroupListWidget::selectedPair(Media** selected, Media** other) {
  MediaPage* p = currentPage();
  const auto& selection = selectedItems();
  if (selection.count() != 1 || !p->isPair()) return false;

  int selIndex = selection[0]->type();
  int otherIndex = (selIndex + 1) % 2;

  // assumes we keep analysis images at the end
  Q_ASSERT(!MediaPage::isAnalysis(p->group[otherIndex]));

  *selected = &p->group[selIndex];
  *other = &p->group[!selIndex];
  return true;
}


void MediaGroupListWidget::restoreSelectedItem(const QModelIndex& last) {
  int count = currentPage()->countNonAnalysis();
  int selIndex = std::min(last.row(), count - 1);
  if (selIndex >= 0) setCurrentIndex(model()->index(selIndex, 0));
}

//----------- updating items ---------------//

// return if two values are, less, more or the same (for color-coding text)
template <typename T>
static const char* relativeLabel(const T& a, const T& b) {
  return a < b ? "less" : (b < a ? "more" : "same");
};

void MediaGroupListWidget::updateItems() {
  const MediaGroup& group = currentGroup();
  if (group.count() <= 0) return;

  QString prefix = Media::greatestPathPrefix(group);
  prefix = prefix.mid(0, prefix.lastIndexOf('/') + 1);

  QHash<QString, int> fsFileCount;  // cache file count for large folders

  // store the attributes of the first item and compare to the others
  struct {
    int64_t size = 0;        // byte size
    double compression = 0;  // compressed size / uncompressed sized
    int pixels = 0;          // number of pixels / pixels per frame
    int quality = 0;         // no-reference quality score
    int score = 0;           // match score
    int fileCount = 0;       // number of files in the same dir as this one
    QDateTime date;          // media creation date (best guess)
    int jpegQuality = 0;     // jpeg quality factor used when saving
    int qualityScore = 0;    // no-reference quality score
    int duration = 0;        // video: duration in seconds
    float fps = 0;           // video: frames-per-second
  } first;

  for (int i = 0; i < group.count(); i++) {
    const Media& m = group[i];
    // QString fmt;
    const bool isVideo = m.type() == Media::TypeVideo;
    // if (isVideo)
    //   fmt = " :: " + m.attributes().value("vformat");

    int64_t size = m.originalSize();
    const int pixels = m.resolution();
    const double compression = double(m.compressionRatio());
    const int score = m.score();
    const int jpegQuality = m.attributes().value("jpeg-quality").toInt();
    const int qualityScore = m.attributes().value("quality-score").toInt();
    const int duration = m.attributes().value("duration").toInt();
    const float fps = m.attributes().value("fps").toFloat();
    const bool locked = _lockedFolders.contains(m.dirPath());

    QString path = m.path();
    const QFileInfo fileInfo(path);

    // truncate display name to common prefix
    if (fileInfo.isFile() || m.isArchived()) {
      path = path.mid(prefix.length());
      if (size == 0) size = fileInfo.size();
    }

    int fileCount = 0;
    if (m.isArchived()) {
      // can be slow for large archives, we can cache since
      // archives are immutable here
      QString archivePath;
      m.archivePaths(&archivePath);
      auto it = _archiveFileCount.find(archivePath);
      if (it != _archiveFileCount.end())
        fileCount = *it;
      else {
        fileCount = m.archiveCount();
        _archiveFileCount.insert(archivePath, fileCount);
      }
    } else if (fileInfo.isFile()) {
      const auto key = fileInfo.absolutePath();
      const auto it = fsFileCount.find(key);
      if (it != fsFileCount.end())
        fileCount = it.value();
      else {
        fileCount = fileInfo.dir().entryList(QDir::Files).count();
        fsFileCount.insert(key, fileCount);
      }
    }

    QDateTime date = QDateTime::fromString(m.attributes().value("datetime"));
    QString camera = m.attributes().value("camera");

    // store if current value is less than/greater than the first item in the
    // group the labels assigned are referenced in the stylesheet to change the
    // color of the value
    struct {
      QString compression, pixels, size, score, fileCount, date, duration, frameRate, jpegQuality,
          qualityScore;
    } compare;


    static const auto percent = [](double a, double b) {
      return int((a - b) * 100.0 / b);
    };

    static const auto formatPercent = [](double a, double b) {
      if (b == 0) return QString("--");
      return QString("%1").arg(percent(a,b));
    };

    if (i == 0) {
      first.compression = compression;
      first.pixels = pixels;
      first.size = size;
      first.score = score;
      first.fileCount = fileCount;
      first.date = date;
      first.jpegQuality = jpegQuality;
      first.qualityScore = qualityScore;
      first.duration = duration;
      first.fps = fps;

      compare.compression = compare.pixels = compare.score = compare.size = compare.fileCount =
          "none";
      compare.date = "same";
      compare.duration = "same";   // isVideo ? "same" : "none"; // do not hide
      compare.frameRate = "same";  // isVideo ? "same" : "none";
      compare.jpegQuality = jpegQuality == 0 ? "none" : "same";  // hide unless computed
      compare.qualityScore = qualityScore == 0 ? "none" : "same";
    } else {
      compare.compression = percent(compression, first.compression) == 0
                                ? "same"
                                : relativeLabel(compression, first.compression);
      compare.pixels = relativeLabel(pixels, first.pixels);
      compare.size = percent(size, first.size) == 0 ? "same" : relativeLabel(size, first.size);
      compare.score = relativeLabel(score, first.score);
      compare.fileCount = relativeLabel(fileCount, first.fileCount);
      compare.jpegQuality =
          jpegQuality == 0 ? "none" : relativeLabel(jpegQuality, first.jpegQuality);
      compare.qualityScore =
          qualityScore == 0 ? "none" : relativeLabel(qualityScore, first.qualityScore);

      compare.duration = isVideo ? relativeLabel(duration, first.duration) : "same";
      compare.frameRate = isVideo ? relativeLabel(fps, first.fps) : "same";

      if (first.date.isValid() && date.isValid())
        compare.date = relativeLabel(first.date, date);
      else
        compare.date = "same";
    }

    // we want to elide the filename, but we use richtext which has no elide,
    // and we also need to know how many characters fit in that space to do it correctly
    //
    // we have to construct the full text string for the first line (non-elided) into "title",
    // pass it to paint() via item->data()
    //
    // paint() strips out the second part "(%2)" so it is styled differently, as well as
    // the weed and lock indicator, in the <span> following the filename
    //
    // assume drawRichText() uses similar font metrics as the widget paint()
    //
    // note: the (%2) is styled differently by lopping it off in paint() and adding
    // it back in <span> following filename
    //
    // note: extra space or else clipping (fontMetrics inaccurate?)
    //
    QString title = path + QString(" [x%1] (%2) ").arg(fileCount).arg(fileCount - first.fileCount);

#define WEED_CSTR "\317\211" // omega (curvy w)
    if (m.isWeed()) title += " " WEED_CSTR;

#define LOCK_CSTR "\316\273" // lambda
    if (locked) title += " " LOCK_CSTR;

    //
    // TODO: convert this to some kind of loadable/configurable template with variable replacement
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
            "<tr class=\"base\">"
            "<td class=\"%26\" colspan=\"3\" count=\"%15\">"
            "%1"                                 // file name + count
            "<span class=\"%16\">(%17)</span>"   // count difference
            "<span class=\"weed\">%27</span>"    // weed
            "<span class=\"locked\">%28</span>"  // folder lock
            "</td>"
            "</tr>"
            "<tr class=\"altbase\">"
            "<td>%2x%3</td>"
            "<td><span class=\"%7\">%11%</span></td>"
            "<td><span class=\"%18\">%19</span></td>"
            "</tr>"
            "<tr class=\"base\">"
            "<td>%4k</td>"
            "<td><span class=\"%8\">%12%</span></td>"
            "<td><span class=\"%20\">%21</span></td>"
            "</tr>"
            "<tr class=\"altbase\">"
            "<td>%5:1</td>"
            "<td><span class=\"%9\">%13%</span></td>"
            "<td><span class=\"%22\">%23</span></td>"
            "</tr>"
            "<tr class=\"base\">"
            "<td>s%6</td>"
            "<td><span class=\"%10\">%14%</span></td>"
            "<td><span class=\"%24\">%25</span></td>"
            "</tr>"
            "</tbody></table>")
            .arg("@title@") // paint elides text to the item width
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
            .arg(compare.date)
            .arg(date.toString("yyyy/MM/dd HH:mm:ss"))
            .arg(isVideo ? compare.duration : "same")
            .arg(isVideo ? m.attributes().value("time") : camera)
            .arg(isVideo ? compare.frameRate : compare.jpegQuality)
            .arg(isVideo ? QString::number(fps) : QString::number(jpegQuality))
            .arg(compare.qualityScore)
            .arg(qualityScore)
            .arg(m.isArchived() ? "archive" : "file")
            .arg(m.isWeed() ? "&nbsp;" WEED_CSTR : "")
            .arg(locked ? "&nbsp;" LOCK_CSTR : "");

    // QListWidgetItem::type() will be used to refer back to the associated Media object
    // setting: using type() for list index is not needed anymore, use indexFromItem()
    QListWidgetItem* item;
    if (i < this->count()) {
      item = this->item(i);
    } else {
      item = new QListWidgetItem(nullptr, i);
      insertItem(i, item);
    }
    if (MediaPage::isAnalysis(m)) {
      item->setFlags(Qt::NoItemFlags);  // disable selection of analysis items
    } else {
      item->setText(text);
      item->setData(Qt::UserRole + 0, title);
      item->setToolTip(path);
    }
  }
  // assuming something changed, force repaint
  update();
}

void MediaGroupListWidget::itemCountChanged() {
  // delete consecutive rows if it looks like we are finished with them
  while (currentPage()->countNonAnalysis() < 2) {
    qInfo() << "auto remove row" << _currentRow << "with one item left";

    deletePage(_currentRow);

    if (_currentRow >= _list.count()) _currentRow--;

    if (_currentRow < 0) {
      qInfo() << "closing view, nothing left to display";
      close();
      return;
    }
  }

  // caller may have dropped it, to force it to recompute
  if (_autoDifference)
    currentPage()->addDifferenceAnalysis();

  loadRow(_currentRow);
}

void MediaGroupListWidget::updateMedia(const QString& path, const Media& m) {
  for (MediaPage* page : _list)
    page->setMediaWithPath(path, m);

  updateItems();
}

//---------- image loading ---------------//

void MediaGroupListWidget::checkMemoryUsage() const {
  // debug: there should not be loaded images on uncached pages
  float usedKb = 0;
  float leakingKb = 0;

  QSet<MediaPage*> loaded;
  for (MediaPage* page : _loadedPages)
    loaded.insert(page);

  for (ImageWork* iw : _loaders)
    loaded.insert(iw->page);

  for (MediaPage* page : _list)
    if (!loaded.contains(page)) {
      for (Media& m : page->group)
        leakingKb += m.image().bytesPerLine() * m.image().height() / 1024;
    }
    else
      for (Media& m : page->group)
        usedKb += m.image().bytesPerLine() * m.image().height() / 1024.0;

  for (MediaPage* page : _deletedPages)
    for (Media& m : page->group)
      leakingKb += m.image().bytesPerLine() * m.image().height() / 1024;

  float totalKb, freeKb;
  Env::systemMemory(totalKb, freeKb);

  qDebug() << "total:" << int(totalKb / 1024) << "used:" << int(usedKb / 1024)
           << "free:" << int(freeKb / 1024) << "loaders:" << _loaders.count()
           << "leaking:" << int(leakingKb/1024);
}

void MediaGroupListWidget::deletePage(int index) {
  Q_ASSERT(index >= 0 && index < _list.count());
  MediaPage* p = _list[index];
  p->unloadData();
  _deletedPages.insert(p);  // there might be threads referencing it
  _list.remove(index);
}

void MediaGroupListWidget::deletePages() {
  for (MediaPage* p : _list) {
    p->unloadData();
    _deletedPages.insert(p); // don't delete yet; threads might reference it
  }
  _list.clear();
}

void MediaGroupListWidget::waitLoaders() {
  for (auto* w : _loaders)
    w->cancel();

  PROGRESS_LOGGER(pl, "waiting for image loaders...<PL> %bignum", _loaders.count());
  while (_loaders.count() > 0) {
    QThread::msleep(100);
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    pl.step(_loaders.count());
  }
  pl.end();

  Q_ASSERT(__started == (__canceled + __finished));
}

void MediaGroupListWidget::cancelOtherLoaders(QSet<const MediaPage*> keep) {
  for (auto* w : _loaders)
    if (!keep.contains(w->page)) w->cancel();
}

void MediaGroupListWidget::loaderOutOfMemory() {
  MediaPage* current = currentPage();
  if (_loaders.count() > 0) {
    QVector<int> rows;
    for (auto* w  : _loaders) rows += w->page->row;
    qDebug() << "cancel loaders" << rows;
    cancelOtherLoaders({current});
    _oomTimer.start(100);
    return;
  }

  QVector<int> rows;
  for (auto* p : _loadedPages) rows += p->row;

  if (_loadedPages.count() > 0) {
    MediaPage* lru = _loadedPages.takeFirst();
    if (lru != current) {
      qDebug() << "unload page" << lru->row << rows;
      lru->unloadData();
      __imgAlloc->setCompactFlag(); // compact if we fail again
      _loadTimer.start(100);
      return;
    }
    _loadedPages.append(lru);  // keep tracking
  }

  qDebug() << "desperation" << rows;
  if (__imgAlloc->compact()) {
    _preloadPage = nullptr;
    _loadTimer.start(100);
    return;
  }

  qWarning() << "giving up" << rows;
}

void MediaGroupListWidget::loadOne(MediaPage* page, int index) {

  const Media& m = page->group.at(index);

  Q_ASSERT(!MediaPage::isLoaded(m));

  ImageWork* w = new ImageWork(this);
  w->page = page;
  w->media = m;
  w->index = index;

  if (MediaPage::isAnalysis(m) && page->count() > 2) {
    const Media& left = page->group[0];
    const Media& right = page->group[1];
    if (!MediaPage::isLoaded(left) || !MediaPage::isLoaded(right)) {
      delete w;
      return;
    }
    w->args = {left, right};
  }

  connect(w, &QFutureWatcher<void>::started, [w] {
    __started++;
    qDebug() <<  "loading page" << w->page->row << "index<PL>" << w->index;
  });

  connect(w, &QFutureWatcher<void>::finished, [this, w] {
    Media& loaded = w->media;
    bool canceled = w->isCanceled();
    bool preload = w->page != currentPage();
    bool oom = w->oom;
    bool purged =!_loadedPages.contains(w->page);

    if (canceled)
      __canceled++;
    else
      __finished++;

    //for (auto* ww : _loaders)
    //   qDebug() << ww << ww->page->row << ww->index << ww->page->id;

    qDebug() << "finished page<PL>" << w->page->row << w->index
             << (preload ? "preload" : "current")
             << (canceled ? "canceled" : "")
             << (oom ? "oom" : "")
             << (purged ? "purged" : "");

    Q_ASSERT(_loaders.removeOne(w));
    w->deleteLater();

    if (canceled) return;

    if (oom) {
      // oom handler fires when all loaders have come in
      _oomTimer.start(100);
      return;
    }

    if (purged) {
      // drop image and copy metadata only
      MediaPage::unload(loaded);
    }

    bool updated = false;
    for (Media& m : w->page->group)
      if (m.path() == loaded.path()) {
        m = loaded;
        updated = true;
      }

    // release memory now (don't wait for deleteLater())
    MediaPage::unload(loaded);

    if (updated && !preload) {
      _updateTimer.start(1000 / LW_UPDATE_HZ);

      if (w->page->isLoaded() && _preloadPage)
        _loadTimer.start(LW_PRELOAD_DELAY);
    }

    if (updated && w->page == _preloadPage) {
      // clear preloadPage or we'll keep trying
      if (_preloadPage->isLoaded())
        _preloadPage = nullptr;
    }

    // run difference image once dependents are loaded
    const MediaGroup& group = w->page->group;
    if (!purged && group.count() > 2
        && MediaPage::isDifferenceAnalysis(group[2])
        && MediaPage::isLoaded(group[0])
        && MediaPage::isLoaded(group[1])
        && !MediaPage::isLoaded(group[2])) {
      loadOne(w->page, 2);
    }
  });

  bool fastSeek = _options.flags & MediaWidgetOptions::FlagFastSeek;

  w->setFuture(QtConcurrent::run(loadImage, w, fastSeek));
  _loaders.append(w);
}

void MediaGroupListWidget::loadMedia(MediaPage* page) {
  static int recursion = 0;

  Q_ASSERT(recursion == 0);
  recursion++;

  while (_loadedPages.count() > LW_MAX_CACHED_ROWS) {
    MediaPage* evicted = _loadedPages.takeFirst();
    qDebug() << "unload page" << evicted->row;
    evicted->unloadData(false);
  }

  _loadedPages.removeAll(page);
  _loadedPages.append(page);

  if (page->isLoaded()) {
    qDebug() << "page" << page->row << "is already loaded";
    recursion--;
    return;
  }

  qDebug() << "page" << page->row << (page == currentPage() ? "preload" : "");

  for (int i = 0; i < page->group.count(); ++i) {
    if (MediaPage::isLoaded(page->group.at(i))) continue;

    if (_loaders.end() != std::find_if(_loaders.begin(), _loaders.end(), [page, i](ImageWork* ww) {
          return ww->page == page && ww->index == i && !ww->isCanceled();
        })) {
      qDebug() << "skip queued page" << page->row << "<PL>index" << i;
      continue;
    }

    loadOne(page, i);
  }
  recursion--;
}

void MediaGroupListWidget::loadRow(int row, bool preloadNextRow) {
  static uint64_t start = nanoTime();

  if (_list.count() <= 0) return;

  row = qBound(0, row, _list.count() - 1);
  const MediaPage* page = _list.at(row);

  cancelOtherLoaders({page});

  QModelIndex selected; // save the selection
  {
    QModelIndexList sel = selectedIndexes();
    if (sel.count() > 0) selected = sel.first();
  }

  qDebug() << "page" << _currentRow << "=>" << row;
  int rowSkip = row - _currentRow;
  _currentRow = row;
  clear();

  _itemDelegate->setPage(page);

  QString folderPath = page->folderPath();
  setWindowFilePath(folderPath);

  const QString homePath = QDir::homePath();
  if (folderPath.startsWith(homePath)) {
    folderPath = folderPath.mid(homePath.length());
    folderPath = "~" + folderPath;
  }

  setWindowTitle(QString("Group %1 of %2 : %3 [x%4] %5")
                     .arg(row + 1)
                     .arg(_list.count())
                     .arg(folderPath)
                     .arg(page->count())
                     .arg(page->info()));

  // create lw items and repaint
  updateItems();

  if (selected.isValid()) restoreSelectedItem(selected);

  // store row number, should not be used for control flow (use Page*)
  _list[row]->row = row;

  // preload the next row we expect to see after the displayed page finishes loading
  _preloadPage = nullptr;

  int nextRow = row + rowSkip;
  if (nextRow == row) nextRow++; // we removed a row, next one is ok

  if (preloadNextRow && nextRow >= 0 && nextRow < _list.count()) {
    _preloadPage = _list[nextRow];
    _preloadPage->row = nextRow;
  }

  // if we get a ton of requests (scrolling), delay the start
  _loadTimer.start(1000/LW_UPDATE_HZ);

  static int benchmark = QProcessEnvironment::systemEnvironment().value("BENCHMARK_LISTWIDGET").toInt();
  if (benchmark) {
    static QTimer* timer = nullptr;
    if (timer) return;

    timer = new QTimer;
    timer->setInterval(1);
    connect(timer, &QTimer::timeout, [this]() {
      const MediaPage* currPage = currentPage();
      if (!currPage->isLoaded()) return;
      repaint();

      if (benchmark == 1 || currPage->row == _list.count() - 1) {
        double seconds = (nanoTime() - start) / 1000000000.0;
        int count = 0;
        if (benchmark == 1)
          count = currPage->count();
        else
          for (auto* p : qAsConst(_list))
            count += p->count();

        qCritical() << "BENCHMARK_LISTWIDGET"
                    << QString("%1 seconds, %2 images/second").arg(seconds).arg(count / seconds);
        timer->stop();
        close();
        return;
      }

      loadRow(currPage->row + 1);
      currPage = currentPage();
    });
    timer->start();
  }
}
