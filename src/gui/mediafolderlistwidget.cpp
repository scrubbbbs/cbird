/* Display for groups of related Media
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
#include "mediafolderlistwidget.h"
#include "theme.h"
#include "../qtutil.h"

#include <QtCore/QSettings>
#include <QtCore/QTimer>

#include <QtGui/QMouseEvent>

#define LW_ITEM_SPACING (8)             // can't be too small or it breaks layout logic

MediaFolderListWidget::MediaFolderListWidget(const MediaGroup& list,
                                             const MediaWidgetOptions& options, QWidget* parent)
    : super(parent), _list(list), _options(options) {
  setWindowTitle(QString("Group-List Set : %2 [x%1]").arg(_list.count()).arg(_options.basePath));

  setViewMode(QListView::IconMode);
  setResizeMode(QListView::Adjust);
  setMovement(QListView::Static);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setWrapping(true);
  setSpacing(LW_ITEM_SPACING);

  int iconW = 0;
  int iconH = 0;
  int textH = 0;
  for (auto& m : list) {
    qreal dpr = m.image().devicePixelRatioF();
    QSize size = m.image().size();
    iconW = std::max(iconW, int(size.width() / dpr));
    iconH = std::max(iconH, int(size.height() / dpr));
    textH = std::max(textH, int(m.path().split(lc('\n')).count()));
  }
  setIconSize({iconW, iconH});

  const int lineH = 16, spacing = 16;
  setGridSize({iconW + spacing, iconH + spacing + textH * lineH});

  int index = 0;
  for (const Media& m : list) {
    // TODO: using type() for list index is not necessary since
    // we have indexFromItem()
    QListWidgetItem* item = new QListWidgetItem(m.path(), nullptr, index++);
    item->setIcon(QIcon(QPixmap::fromImage(m.image())));
    addItem(item);
  }
  setCurrentIndex(model()->index(0, 0));

  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(staticMetaObject.className() + qq(".shortcuts"));

  WidgetHelper::addAction(settings, "Close Window", Qt::CTRL | Qt::Key_W, this, SLOT(close()));
  WidgetHelper::addAction(settings, "Close Window (Alt)", Qt::Key_Escape, this, SLOT(close()));
  WidgetHelper::addAction(settings, "Choose Selected", Qt::Key_Return, this, SLOT(chooseAction()));

  setContextMenuPolicy(Qt::ActionsContextMenu);

  connect(this, &QListWidget::doubleClicked, this, &self::chooseAction);

  WidgetHelper::restoreGeometry(this);

  setMouseTracking(true);
  _hoverTimer = new QTimer(this);
  _hoverTimer->setSingleShot(true);
  _hoverTimer->setInterval(300);
  connect(_hoverTimer, &QTimer::timeout, this, [this]() {
    QListWidgetItem* item = itemAt(_hoverPos);
    if (!item) return;
    QModelIndex index = indexFromItem(item);
    _hovering = true;
    emit beginHover(index.row());
  });
}

MediaFolderListWidget::~MediaFolderListWidget() { WidgetHelper::saveGeometry(this); }

void MediaFolderListWidget::show() { Theme::instance().showWindow(this); }

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

void MediaFolderListWidget::mouseMoveEvent(QMouseEvent* event) {
  const QPoint& newPos = event->pos();
  _hoverPos = newPos;
  _hoverTimer->stop();
  if (_hovering) {
    _hovering = false;
    emit endHover();
  } else
    _hoverTimer->start();
}

MediaGroup MediaFolderListWidget::selectedMedia() const {
  const QList<QListWidgetItem*> items = selectedItems();
  MediaGroup selected;
  for (auto& item : items) selected.append(_list[item->type()]);
  return selected;
}
