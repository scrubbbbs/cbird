/* Display list of Media in different ways
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
#include "mediabrowser.h"

#include "mediafolderlistwidget.h"
#include "mediagrouplistwidget.h"
#include "theme.h"

#include "../database.h"
#include "../engine.h"
#include "../qtutil.h"
#include "../scanner.h"
#include "../videocontext.h"

#include <QtConcurrent/QtConcurrentMap>
#include <QtCore/QFile>
#include <QtCore/QTimer>
#include <QtWidgets/QApplication>

static QImage loadThumb(const Media& m, const MediaWidgetOptions& options) {
  const qreal dpr = qApp->devicePixelRatio();
  const int iconSize = dpr * options.iconSize;

  const float iconAspect = options.iconAspect;
  const bool doCrop = iconAspect >= 0;

  // size before scaling, unknown if media isn't indexed
  int origW = m.width(), origH = m.height();

  QImage img;

  if (m.type() == Media::TypeVideo) {
    img = VideoContext::frameGrab(m.path(), -1, true);
  } else if (doCrop && (origW <= 0 || origH <= 0)) {
    // for crop, we don't know the aspect(yet), can't use fast image loader
    // note: this could do a fast probe of the file instead
    qWarning() << "slow path, no width/height information...";
    img = m.loadImage();
  }

  if (!doCrop) {
    QSize size(0, iconSize);
    img = img.isNull() ? m.loadImage(size) : Media(img).loadImage(size);
    img.setDevicePixelRatio(dpr);
    return img;
  }

  if (origW <= 0 || origH <= 0) {
    origW = img.width();
    origH = img.height();
  }

  const float origAspect = float(origW) / origH;

  if (origAspect < iconAspect) {
    QSize size(iconSize * iconAspect, 0);
    img = img.isNull() ? m.loadImage(size) : Media(img).loadImage(size);
    int h = img.width() / iconAspect;
    int y = (img.height() - h) / 2;
    img = img.copy(0, y, img.width(), h);
  } else {
    QSize size(0, iconSize);
    img = img.isNull() ? m.loadImage(size) : Media(img).loadImage(size);
    int w = img.height() * iconAspect;
    int x = (img.width() - w) / 2;
    img = img.copy(x, 0, w, img.height());
  }

  img.setDevicePixelRatio(dpr);
  return img;
}

MediaBrowser::MediaBrowser(const MediaWidgetOptions& options) : _options(options) {}

int MediaBrowser::showList(const MediaGroupList& list, const MediaWidgetOptions& options) {
  MediaBrowser browser(options);
  browser.show(list);
  // FIXME: we cannot call exec if we did already; it returns immediately
  // this should return void; if the caller wants the exit code, they can
  // use qApp->exec() to get it.
  return qApp->exec();
}

int MediaBrowser::show(const MediaGroupList& list, int mode, const MediaWidgetOptions& options) {
  Theme::setup();

  if (list.count() <= 0) return 0;
  for (auto& g : list)
    if (g.count() <= 0) {
      qCritical() << "empty group in list";
      return 0;
    }

  if (mode == ShowNormal) return MediaBrowser::showList(list, options);
  if (mode == ShowPairs) return MediaBrowser::showSets(list, options);
  if (mode == ShowFolders) return MediaBrowser::showFolders(list, options);

  Q_UNREACHABLE();
}

int MediaBrowser::showFolders(const MediaGroupList& list, const MediaWidgetOptions& options) {
  if (list.count() <= 0) return 0;

  qInfo() << "collecting info...";

  const QString prefix = Media::greatestPathPrefix(list);

  struct GroupStats {
    int itemCount = 0;
  };
  QHash<QString, GroupStats> stats;

  QStringList keys;
  for (const MediaGroup& g : list) {
    Q_ASSERT(g.count() > 0);
    const Media& first = g.at(0);
    QString key = first.attributes().value("group");       // use -group-by before path
    if (!key.isEmpty()) key = key.split(qq("==")).back();  // don't display -group-by expression

    if (key.isEmpty()) {
      if (first.type() == Media::TypeVideo) // don't group videos TODO: option
        key = first.path();
      else
        key = first.dirPath();
      key = key.mid(prefix.length());
    }
    key = qElide(key, options.iconTextWidth);
    keys.append(key);

    GroupStats& s = stats[key];
    s.itemCount += g.count();
  }

  QHash<QString, MediaGroupList> folders;

  qInfo() << "building folders...";
  for (int i = 0; i < list.count(); ++i) {
    const QString& key = keys.at(i);
    const GroupStats& s = stats.value(key);
    QString newKey = key + QString(" [x%1]").arg(s.itemCount);
    auto& set = folders[newKey];
    const auto& group = list.at(i);
    if (group.count() > options.maxPerPage)
      set.append(Media::splitGroup(group, options.maxPerPage));
    else
      set.append(group);
  }

  MediaGroup index;
  for (auto it = folders.constKeyValueBegin(); it != folders.constKeyValueEnd(); ++it)
    index.append(Media(it->first));

  auto f = QtConcurrent::map(index, [&](Media& m) {
    const Media& first = folders[m.path()][0][0];
    QImage img = loadThumb(first, options);
    m.setImage(img);
    m.readMetadata();
  });

  PROGRESS_LOGGER(pl, "loading thumbnails:<PL> %percent %step folders", f.progressMaximum());
  while (f.isRunning()) {
    pl.step(f.progressValue());
    QThread::msleep(100);
  }
  pl.end();

  qInfo() << "sorting...";
  Media::sortGroup(index, {"path"});

  auto opt = options;
  opt.basePath = prefix.mid(0, prefix.length() - 1);
  MediaBrowser browser(opt);
  browser.showIndex(index, folders);
  return qApp->exec();
}

int MediaBrowser::showSets(const MediaGroupList& list, const MediaWidgetOptions& options) {
  if (list.count() <= 0) return 0;

  // try to form a list of MediaGroupList, where each member
  // matches between two directories, or an image "set".
  // If there is no correlation, put match in "unpaired" set.
  const char* unpairedKey = "*unpaired*";

  MediaGroup index;                  // dummy group for top-level navigation
  index.append(Media(unpairedKey));  // entry for the "unpaired" list

  QHash<QString, MediaGroupList> sets;
  for (const MediaGroup& g : list) {
    QStringList dirPaths;
    for (const Media& m : g) {
      QString path = m.dirPath();
      if (!dirPaths.contains(path)) dirPaths.append(path);
    }

    // we have a pair, add it
    if (dirPaths.count() == 2) {
      // find the common prefix, exclude from the key
      // TODO: maybe overflow potential, but all paths should be absolute...
      const QString& a = dirPaths[0];
      const QString& b = dirPaths[1];
      int i = 0;
      while (i < a.length() &&  // longest prefix
             i < b.length() && a[i] == b[i])
        i++;
      while (i - 1 >= 0 && a[i - 1] != '/')  // parent dir
        i--;

      QString key;
      key += qElide(dirPaths[0].mid(i), options.iconTextWidth) + "/";
      key += "\n";
      key += qElide(dirPaths[1].mid(i), options.iconTextWidth) + "/";

      sets[key].append(g);
    } else {
      sets[unpairedKey].append(g);
    }
  }

  // any set with only one match, throw into the "other"
  for (auto key : sets.keys())
    if (key != unpairedKey && sets[key].count() == 1) {
      sets[unpairedKey].append(sets[key][0]);
      sets.remove(key);
    } else {
      // add the dummy item to index
      Media m(key);
      if (!index.contains(m)) {
        index.append(m);
      }
    }

  if (sets[unpairedKey].isEmpty()) index.removeFirst();

  auto f = QtConcurrent::map(index, [&](Media& m) {
    m.setImage(loadThumb(sets[m.path()][0][0], options));
    m.readMetadata();
  });

  PROGRESS_LOGGER(pl, "loading thumbnails:<PL> %percent %step sets", f.progressMaximum());
  while (f.isRunning()) {
    QThread::msleep(100);
    pl.step(f.progressValue());
  }
  pl.end();

  Media::sortGroup(index, {"path"});
  MediaBrowser browser(options);

  if (index.count() == 1)
    browser.show(list);
  else
    browser.showIndex(index, sets);

  return qApp->exec();
}

void MediaBrowser::showIndex(const MediaGroup& index,
                             const QHash<QString, MediaGroupList>& folders) {
  _groups = &folders;
  _options.selectionMode = MediaWidgetOptions::SelectOpen;
  MediaFolderListWidget* w = new MediaFolderListWidget(index, _options);
  connect(w, &MediaFolderListWidget::mediaSelected, this, &MediaBrowser::mediaSelected);

  w->show();

  class Animation : public QObject {
   public:
    Animation(const MediaWidgetOptions& options, QObject* parent)
        : QObject(parent), _options(options) {
      _timer = new QTimer(parent);
      _timer->setInterval(300);
      _timer->setSingleShot(true);
      connect(_timer, &QTimer::timeout, this, &Animation::nextFrame);
    }
    void start(QListWidgetItem* item, const MediaGroupList& list) {
      _item = item;
      _list = list;
      _listIndex = 0;
      _groupIndex = 0;
      nextFrame();
    }
    void stop() { _timer->stop(); }
    void nextFrame() {
      if (qApp->activeWindow() != parent()) return;

      _timer->start();

      auto& group = _list.at(_listIndex);

      // if we opened the folder and modified the path
      // we will not see it here, check if it still exists
      const Media& m = group.at(_groupIndex);
      const QString filePath = m.containerPath();
      if (!QFile::exists(filePath)) return;

      QImage img = loadThumb(group.at(_groupIndex), _options);
      if (img.isNull()) return;
      _item->setIcon(QPixmap::fromImage(img));
      _groupIndex = (_groupIndex + 1) % group.count();
      if (_groupIndex == 0) _listIndex = (_listIndex + 1) % _list.count();
    }

   private:
    const MediaWidgetOptions _options;
    QTimer* _timer = nullptr;
    QListWidgetItem* _item = nullptr;
    MediaGroupList _list;
    int _listIndex = 0;
    int _groupIndex = 0;
  };

  auto* anim = new Animation(_options, w);

  connect(w, &MediaFolderListWidget::endHover, this, [anim]() { anim->stop(); });

  connect(w, &MediaFolderListWidget::beginHover, this, [this, w, anim](int index) {
    QListWidgetItem* item = w->item(index);
    if (!item) return;

    const MediaGroupList& gl = _groups->value(item->text());
    if (gl.count() <= 0) return;
    if (gl.at(0).count() < 2) return;

    anim->start(item, gl);
  });
}

void MediaBrowser::show(const MediaGroupList& list) {
  MediaGroupListWidget* w = new MediaGroupListWidget(list, _options);
  if (_options.selectOnOpen.isValid()) w->selectItem(_options.selectOnOpen);

  connect(w, &MediaGroupListWidget::mediaSelected, this, &MediaBrowser::mediaSelected);
  w->show();
  w->activateWindow();
  w->setAttribute(Qt::WA_DeleteOnClose);
}

void MediaBrowser::mediaSelected(const MediaGroup& group) {
  for (const Media& m : group) {
    if (_options.selectionMode == MediaWidgetOptions::SelectExitCode) {
      qApp->exit(m.position() + 1);  // subtract 1 where show() was called
      return;
    }

    const MediaFolderListWidget* mw = dynamic_cast<MediaFolderListWidget*>(sender());
    if (mw && _options.selectionMode == MediaWidgetOptions::SelectOpen && _groups &&
        _groups->count() > 0)
      show(_groups->value(m.path()));
    else if (_options.db) {
      MediaSearch search;
      search.needle = m;
      search.params = _options.params;
      search = Engine(_options.db->path(), IndexParams()).query(search);

      // TODO: refactor common filtering logic
      search.matches.prepend(search.needle);
      MediaGroupList list;
      if (!_options.db->filterMatch(_options.params, search.matches)) list.append(search.matches);
      _options.db->filterMatches(_options.params, list);
      show(list);
    }
  }
}
