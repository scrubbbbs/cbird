/* Table display for list of Media
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
#include "mediagrouptablewidget.h"

#include "../qtutil.h"

int MediaGroupTableModel::validMatchFlags(int oldFlags, int newFlag) {
  switch (newFlag) {
    case ShowBigger:
    case ShowSmaller:
      oldFlags &= ~ShowAnyMatch;
      Q_FALLTHROUGH();
    case ShowAnyMatch:
      oldFlags &= ~(ShowBigger | ShowSmaller);
      Q_FALLTHROUGH();
    case ShowNoMatch:
      oldFlags &= ~ShowAll;
      break;
    case ShowAll:
      oldFlags = 0;
      break;
  }

  return oldFlags | newFlag;
}

MediaGroupTableModel::MediaGroupTableModel(QObject* parent) : QAbstractTableModel(parent) {
  _header << "Icon"
          << "Order"
          << "Size"
          << "Res"
          << "Alt"
          << "Group"
          << "Comment"
          << "Origin"
          << "Path"
          << "Score";
  _pos = 0;
  // note: sort function must be applied on widget level to show up correctly
  setSortFunction(0, Qt::SortOrder::AscendingOrder);
  applyFilter(0, 0, "");
}

int MediaGroupTableModel::rowCount(const QModelIndex& parent) const {
  (void)parent;
  return _filtered.count();
}

int MediaGroupTableModel::columnCount(const QModelIndex& parent) const {
  (void)parent;
  return _header.count();
}

QVariant MediaGroupTableModel::headerData(int section, Qt::Orientation orientation,
                                          int role) const {
  if (role == Qt::DisplayRole) {
    if (orientation == Qt::Horizontal)
      return _header[section];
    else
      return QVariant();  // disable vertical header
  } else
    return QAbstractTableModel::headerData(section, orientation, role);
}

QVariant MediaGroupTableModel::data(const QModelIndex& index, int role) const {
  QVariant v;

  const Media& m = _data[_filtered[index.row()]];

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
      case ColOrderAdded:
        v = m.position();
        break;
      case ColMegaPixels:
        v = (m.width() * m.height()) / 1000000.0;
        break;
      case ColDimensions:
        v = QString("%1x%2").arg(m.width()).arg(m.height());
        break;
      case ColPath:
        v = m.path().startsWith("data:") ? "<data-url>" : m.path();
        break;
      case ColAlt:
        v = m.attributes()["alt"];
        break;
      case ColSubdir:
        v = m.attributes()["group"];
        break;
      case ColComment:
        v = m.attributes()["comment"];
        break;
      case ColOrigin:
        v = m.attributes()["origin"];
        break;
      case ColScore:
        v = m.score();
        break;
    }
  } else if (role == Qt::DecorationRole) {
    if (index.column() == ColIcon)
      if (_icons.contains(m.path())) v = _icons[m.path()];
  } else if (role == Qt::BackgroundRole) {
    const QString& path = m.path();
    if (_mark.contains(path) && _mark[path])
      v = QColor("purple");
    else
      v = m.matchColor();
  }

  return v;
}

void MediaGroupTableModel::setSortFunction(int column, Qt::SortOrder order) {
  _sortColumn = column;
  _sortOrder = order;

  _compareFunc = [](const Media& a, const Media& b) { return a < b; };

#define LESS(a, b, thing) (a.thing < b.thing)

  switch (column) {
    case ColOrderAdded:
      if (order == Qt::SortOrder::AscendingOrder)
        _compareFunc = [](const Media& a, const Media& b) { return LESS(a, b, position()); };
      else
        _compareFunc = [](const Media& a, const Media& b) { return LESS(b, a, position()); };
      break;
    case ColMegaPixels:
      if (order == Qt::SortOrder::AscendingOrder)
        _compareFunc = [](const Media& a, const Media& b) { return LESS(a, b, resolution()); };
      else
        _compareFunc = [](const Media& a, const Media& b) { return LESS(b, a, resolution()); };
      break;
    case ColDimensions:
      if (order == Qt::SortOrder::AscendingOrder)
        _compareFunc = [](const Media& a, const Media& b) {
          return std::max(a.width(), a.height()) < std::max(b.width(), b.height());
        };
      else
        _compareFunc = [](const Media& a, const Media& b) {
          return std::max(b.width(), b.height()) < std::max(a.width(), a.height());
        };
      break;
    case ColScore:
      if (order == Qt::SortOrder::AscendingOrder)
        _compareFunc = [](const Media& a, const Media& b) { return LESS(a, b, score()); };
      else
        _compareFunc = [](const Media& a, const Media& b) { return LESS(b, a, score()); };
      break;
    case ColPath:
      if (order == Qt::SortOrder::AscendingOrder)
        _compareFunc = [](const Media& a, const Media& b) { return LESS(a, b, path()); };
      else
        _compareFunc = [](const Media& a, const Media& b) { return LESS(b, a, path()); };
      break;
    case ColAlt:
      if (order == Qt::SortOrder::AscendingOrder)
        _compareFunc = [](const Media& a, const Media& b) {
          return LESS(a, b, attributes()["alt"]);
        };
      else
        _compareFunc = [](const Media& a, const Media& b) {
          return LESS(b, a, attributes()["alt"]);
        };
      break;
    case ColComment:
      if (order == Qt::SortOrder::AscendingOrder)
        _compareFunc = [](const Media& a, const Media& b) {
          return LESS(a, b, attributes()["comment"]);
        };
      else
        _compareFunc = [](const Media& a, const Media& b) {
          return LESS(b, a, attributes()["comment"]);
        };
      break;
    case ColOrigin:
      if (order == Qt::SortOrder::AscendingOrder)
        _compareFunc = [](const Media& a, const Media& b) {
          return LESS(a, b, attributes()["origin"]);
        };
      else
        _compareFunc = [](const Media& a, const Media& b) {
          return LESS(b, a, attributes()["origin"]);
        };
      break;
  }
}

void MediaGroupTableModel::applyFilter(int match, int size, const QString& path) {
  // default, filter all
  _filterFunc = [](const Media& a) {
    (void)a;
    return true;
  };

  // TODO: change this to composition of functions
  if (match == ShowAll) {
    if (size > 0) {
      // FIXME: path != "" case
      _filterFunc = [size](const Media& a) {
        // minsize filter
        return std::max(a.width(), a.height()) < size;
      };
    } else if (path != "") {
      _filterFunc = [path](const Media& a) { return !a.path().contains(QRegularExpression(path)); };
    } else {
      // filter none
      _filterFunc = [path](const Media& a) {
        (void)a;
        return false;
      };
    }
  } else if (match) {
    _filterFunc = [size, path, match](const Media& a) {
      // minsize
      if (size != 0 && std::max(a.width(), a.height()) < size) return true;

      // path
      if (path != "" && !(a.path().contains(QRegularExpression(path)))) return true;

      if ((match & ShowNoMatch) && a.score() < 0) return false;

      if (a.score() >= 0) {
        if (match & ShowAnyMatch)
          return false;
        else if ((match & ShowBigger) &&
                 (a.matchFlags() & (Media::MatchBiggerDimensions | Media::MatchBiggerFile)))
          return false;
        else if ((match & ShowSmaller) &&
                 !(a.matchFlags() & (Media::MatchBiggerDimensions | Media::MatchBiggerFile)))
          return false;
      }

      return true;
    };
  }

  _filtered.clear();
  for (const Media& m : _data)
    if (!_filterFunc(m)) _filtered.append(m.path());

  sort(_sortColumn, _sortOrder);

  qDebug("_data.count=%lld _filtered.count=%lld", _data.count(), _filtered.count());
}

void MediaGroupTableModel::sort(int column, Qt::SortOrder order) {
  setSortFunction(column, order);

  beginResetModel();
  std::stable_sort(_filtered.begin(), _filtered.end(), [=](const QString& a, const QString& b) {
    return _compareFunc(_data[a], _data[b]);
  });
  endResetModel();
}

bool MediaGroupTableModel::removeRows(int row, int count, const QModelIndex& parent) {
  (void)parent;

  if (row >= 0 && row + count <= _filtered.count()) {
    beginRemoveRows(QModelIndex(), row, row + count - 1);

    // need temporary for deletion since _filtered is
    // also modified by removeData()
    QStringList paths;
    for (int i = 0; i < count; i++) paths.append(_filtered[row + i]);

    for (int i = 0; i < count; i++) removeData(Media(paths[i]));

    endRemoveRows();
    return true;
  } else
    qWarning("invalid range requested (%d + %d)", row, count);

  return true;
}

void MediaGroupTableModel::addIcon(const Media& m) {
  if (_icons.contains(m.path())) return;

  QIcon icon = m.loadIcon(QSize(0, 256));

  _icons.insert(m.path(), icon);
}

void MediaGroupTableModel::addMedia(const Media& m) {
  // we already have it, update instead
  auto it = std::find(_filtered.begin(), _filtered.end(), m.path());
  if (it != _filtered.end()) {
    updateMedia(m);
    return;
  }

  addIcon(m);
  Media copy = m;
  copy.setPosition(_pos++);
  // free decompressed image if there is data backup
  if (copy.data().size() > 0 || QFileInfo(copy.path()).exists()) copy.setImage(QImage());

  // add to unfiltered
  _data[copy.path()] = copy;

  // filtered out, return
  if (_filterFunc(copy)) return;

  // _filtered is already sorted by _compareFunc, go find where
  // to insert the new item
  it = std::lower_bound(
      _filtered.begin(), _filtered.end(), copy.path(),
      [=](const QString& a, const QString& b) { return _compareFunc(_data[a], _data[b]); });

  // insert the item and notify views
  int row = it - _filtered.begin();

  beginInsertRows(QModelIndex(), row, row);
  _filtered.insert(it, copy.path());
  endInsertRows();
}

void MediaGroupTableModel::addMediaGroup(const MediaGroup& g) {
  // filter out existing items and set the level property
  for (const Media& m : g) addMedia(m);
}

void MediaGroupTableModel::removeMediaWithPath(const QString& path) {
  removeData(Media(path));
  sort(_sortColumn, _sortOrder);
}

void MediaGroupTableModel::removeData(const Media& m) {
  const QString& path = m.path();

  _data.remove(path);
  _filtered.removeAll(path);
  _icons.remove(path);
  _mark.remove(path);
}

void MediaGroupTableModel::removeAll() {
  beginResetModel();
  _data.clear();
  _filtered.clear();
  _icons.clear();
  _mark.clear();
  endResetModel();
}

void MediaGroupTableModel::updateMedia(const Media& m) {
  const QString& path = m.path();
  auto it = _data.find(path);
  if (it == _data.end()) return;

  Media copy = m;
  copy.setPosition(_pos++);
  *it = copy;

  int i = _filtered.indexOf(path);
  if (i < 0) return;

  _filtered[i] = path;

  emit dataChanged(index(i, 0), index(i, columnCount()));
}

void MediaGroupTableModel::setMark(const QString& path, bool mark) {
  if (!_data.contains(path)) return;

  _mark[path] = mark;
  updateMedia(mediaWithPath(path));
}

Media MediaGroupTableModel::mediaWithPath(const QString& path) const {
  Media m(path);

  auto it = _data.find(path);
  if (it != _data.end()) m = *it;

  return m;
}

void MediaGroupTableModel::memoryUsage(int& objects, size_t& bytes) const {
  objects = 0;
  bytes = 0;
  for (const QString& path : _data.keys()) {
    objects++;
    bytes += _data[path].memSize();
    // bytes += _icons[path].pixmap
  }
}

/**
 * @brief The ImageItemDelegate class is used to draw an icon in a table cell
 */
class ImageItemDelegate : public QItemDelegate {
 public:
  ImageItemDelegate(QAbstractItemModel* model, QObject* parent) : QItemDelegate(parent) {
    _model = model;
  }

  virtual ~ImageItemDelegate() {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const {
    QVariant data = _model->data(index, Qt::DecorationRole);
    const QIcon& icon = data.value<QIcon>();
    icon.paint(painter, option.rect);
  }

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    (void)index;
    (void)option;

    return QSize(256, 256);
  }

 private:
  QAbstractItemModel* _model;
};

MediaGroupTableWidget::MediaGroupTableWidget(QWidget* parent) : QTableView(parent) {
  _defaultRowHeight = -1;

  setSortingEnabled(true);
  sortByColumn(MediaGroupTableModel::ColOrderAdded, Qt::DescendingOrder);
  setTextElideMode(Qt::ElideLeft);
  /*
    QString sheet =
        "QTableView {"
        "   border: 1px solid rgb(255,255,255);"
        "   color: rgb(240,240,240);"
        "   background-color: rgb(96,96,96);"
        "   alternate-background-color: rgb(64,64,64);"
        "}"

        "QHeaderView:section {"
        "   background-color: rgb(128,128,128);"
        "   color: rgb(240,240,240);"
        "   border: 1px solid rgb(64,64,64);"
        "   height: 20px"
        "}"

        "QHeaderView:selected {"
        "   background-color: rgb(64,64,64);"
        "}"

        "QHeaderView::up-arrow {"
        "   color: rgb(255,255,255);"
        "}"

        "QHeaderView::down-arrow {"
        "   color: rgb(255,255,255);"
        "}";

    setStyleSheet(sheet);
  */
  setSelectionBehavior(QAbstractItemView::SelectRows);
  setAlternatingRowColors(true);

  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, &QWidget::customContextMenuRequested, this, &self::execContextMenu);

  connect(this, &QTableView::doubleClicked, this, &self::expandRow);

  addAction("Download", Qt::Key_F, SLOT(downloadAction()));
  addAction("Download Sequence", QKeySequence("Shift+F"), SLOT(downloadSequenceAction()));
  addAction("Search...", Qt::Key_S, SLOT(searchAction()));
  addAction("Alt Search...", QKeySequence("Shift+S"), SLOT(altSearchAction()));
  addAction("Open...", Qt::Key_V, SLOT(openAction()));
  addAction("Delete", Qt::Key_D, SLOT(deleteAction()));
  addAction("Copy Url", QKeySequence("Ctrl+C"), SLOT(copyUrlAction()));
  addAction("Copy Image", QKeySequence("Ctrl+Shift+C"), SLOT(copyImageAction()));
  addAction("Reveal", Qt::Key_E, SLOT(revealAction()));

  _maximized = WidgetHelper::restoreGeometry(this);
}

MediaGroupTableWidget::~MediaGroupTableWidget() {
  WidgetHelper::saveGeometry(this);

  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(self::staticMetaObject.className());

  if (model()) {
    QStringList colWidths;
    for (int i = 0; i < model()->columnCount(); i++)
      colWidths.append(QString::number(columnWidth(i)));
    settings.setValue("columnWidths", colWidths);
  }

  settings.endGroup();
}

QAction* MediaGroupTableWidget::addAction(const QString& label, const QKeySequence& shortcut,
                                          const char* slot) {
  QAction* a = new QAction(label, this);
  connect(a, SIGNAL(triggered(bool)), this, slot);
  a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  a->setShortcutVisibleInContextMenu(true);
  a->setShortcut(shortcut);
  super::addAction(a);
  return a;
}

void MediaGroupTableWidget::setModel(QAbstractItemModel* model) {
  QTableView::setModel(model);

  setItemDelegateForColumn(0, new ImageItemDelegate(model, this));

  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(this->metaObject()->className());
  QStringList colWidths = settings.value("columnWidths").toStringList();
  settings.endGroup();

  if (colWidths.count() == this->model()->columnCount())
    for (int i = 0; i < this->model()->columnCount(); i++) setColumnWidth(i, colWidths[i].toInt());
}

void MediaGroupTableWidget::execContextMenu(const QPoint& p) {
  QMenu* menu = new QMenu(this);

  // TODO: fast menu for folder(s) in any matches

  if (!_indexPath.isEmpty()) {
    // FIXME: don't iterate the filesystem twice here
    QAction* folders = new QAction(this);
    folders->setMenu(MenuHelper::dirMenu(_indexPath, this, SLOT(downloadToFolderAction())));
    folders->setText("Save to Folder");
    menu->addAction(folders);

    folders = new QAction(this);
    folders->setMenu(MenuHelper::dirMenu(_indexPath, this, SLOT(moveToFolderAction())));
    folders->setText("Move to Folder");
    menu->addAction(folders);
  }

  for (QAction* a : this->actions()) menu->addAction(a);

  menu->exec(this->mapToGlobal(p));
  delete menu;
}

QStringList MediaGroupTableWidget::selectedPaths() const {
  QStringList paths;

  for (const QModelIndex& index : selectionModel()->selectedRows(MediaGroupTableModel::ColPath))
    paths.append(index.model()->data(index).toString());

  return paths;
}

Media MediaGroupTableWidget::firstSelectedMedia() {
  Media m;
  const QStringList& paths = selectedPaths();

  if (paths.count() > 0) {
    MediaGroupTableModel* tm = dynamic_cast<MediaGroupTableModel*>(model());
    if (tm) m = tm->mediaWithPath(paths.front());
  }

  return m;
}

void MediaGroupTableWidget::deleteAction() {
  QModelIndexList s = selectionModel()->selectedRows();

  // the selectinon could be non-contiguous,
  // but we can still remove ranges
  while (s.count() > 0) {
    int first = s[0].row();
    int i;
    for (i = 1; i < s.count(); i++)
      if (s[i].row() != first + i)  // non-contiguous
        break;

    model()->removeRows(first, i);
    s = selectionModel()->selectedRows();
  }
}

void MediaGroupTableWidget::downloadAction() {
  QStringList altText;
  for (const QModelIndex& index : selectionModel()->selectedRows(MediaGroupTableModel::ColAlt))
    altText.append(index.model()->data(index).toString());

  const QStringList paths = selectedPaths();

  for (const QString& path : paths) {
    emit downloadUrl(QUrl(path), "", -1, altText.first());
    altText.removeFirst();
  }
}

void MediaGroupTableWidget::downloadSequenceAction() {
  QStringList altText;
  for (const QModelIndex& index : selectionModel()->selectedRows(MediaGroupTableModel::ColAlt))
    altText.append(index.model()->data(index).toString());

  const QStringList paths = selectedPaths();

  // we only pass the sequence number if there
  // are multiple items selected
  int i = 1;

  for (const QString& path : paths) {
    emit downloadUrl(QUrl(path), "", i++, altText.first());
    altText.removeFirst();
  }
}

void MediaGroupTableWidget::downloadToFolderAction() {
  QAction* action = (QAction*)sender();
  if (!action) return;

  QString dirPath = action->data().toString();

  if (!_indexPath.isEmpty() && dirPath == ";newfolder;")
    dirPath = QFileDialog::getExistingDirectory(this, "Choose Folder", _indexPath);

  QStringList altText;
  for (const QModelIndex& index : selectionModel()->selectedRows(MediaGroupTableModel::ColAlt))
    altText.append(index.model()->data(index).toString());

  const QStringList paths = selectedPaths();

  // we only pass the sequence number if there
  // are multiple downloads at the same time
  int i = paths.count() > 1 ? 1 : -1;

  for (const QString& path : selectedPaths()) {
    emit downloadUrl(QUrl(path), dirPath, i, altText.first());
    if (i > 0) i++;
    altText.removeFirst();
  }
}

void MediaGroupTableWidget::moveToFolderAction() {
  QAction* action = (QAction*)sender();
  if (!action) return;

  QString dirPath = action->data().toString();

  if (!_indexPath.isEmpty() && dirPath == ";newfolder;")
    dirPath = QFileDialog::getExistingDirectory(this, "Choose Folder", _indexPath);

  for (const QString& path : selectedPaths())
    emit moveUrl(QUrl(path), dirPath);
}

void MediaGroupTableWidget::openAction() {
  for (const QString& path : selectedPaths()) emit openUrl(QUrl(path));
}

void MediaGroupTableWidget::searchAction() {
  Media m = firstSelectedMedia();
  if (m.path() != "") emit searchMedia(m);
}

void MediaGroupTableWidget::altSearchAction() {
  Media m = firstSelectedMedia();
  if (m.path() != "") emit altSearchMedia(m);
}

void MediaGroupTableWidget::copyUrlAction() {
  const QStringList& paths = selectedPaths();
  if (paths.count() > 0) QGuiApplication::clipboard()->setText(paths.first());
}

void MediaGroupTableWidget::copyImageAction() {
  Media m = firstSelectedMedia();
  if (m.path() != "") QGuiApplication::clipboard()->setImage(m.loadImage());
}

void MediaGroupTableWidget::revealAction() {
  Media m = firstSelectedMedia();
  if (m.path() != "") emit revealMedia(m);
}

void MediaGroupTableWidget::expandRow(const QModelIndex& index) {
  int row = index.row();

  // determine the default height (starting point)
  if (_defaultRowHeight < 0) _defaultRowHeight = rowHeight(row);

  int height = rowHeight(row);
  int hint = sizeHintForRow(row);

  if (height == hint)  // expanded
    setRowHeight(row, _defaultRowHeight);
  else  // not expanded
    setRowHeight(row, sizeHintForRow(row));
}
