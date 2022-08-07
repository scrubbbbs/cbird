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

#include "../database.h"
#include "../engine.h"
#include "../qtutil.h"
#include "../videocontext.h"
#include "mediafolderlistwidget.h"
#include "mediagrouplistwidget.h"

// todo: use widget options to decouple gui from engine()
extern Engine& engine();

static QImage loadThumb(const Media& m, const MediaWidgetOptions& options) {
  QImage img;
  if (m.type() == Media::TypeVideo) {
    img = VideoContext::frameGrab(m.path(), -1, true);
    img = Media(img).loadImage(QSize(0, options.iconSize));
  }
  else
    img = m.loadImage(QSize(0, options.iconSize));
  return img;
}

MediaBrowser::MediaBrowser(const MediaWidgetOptions& options) : _options(options) {}

int MediaBrowser::showList(const MediaGroupList& list, const MediaWidgetOptions& options) {
  MediaBrowser browser(options);
  browser.show(list);
  return qApp->exec();
}

int MediaBrowser::show(const MediaGroupList& list, int mode, const MediaWidgetOptions& options) {
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

  QString prefix = Media::greatestPathPrefix(list);

  class GroupStats {
   public:
    int itemCount = 0;
    // int byteCount = 0;
  };

  QHash<QString, GroupStats> stats;

  qInfo() << "collecting info...";
  QStringList keys;
  for (const MediaGroup& g : list) {
    Q_ASSERT(g.count() > 0);
    const Media& first = g.at(0);
    QString key, tmp;

    key = first.attributes().value("group"); // from -group-by
    if (key.isEmpty()) {
      // path-based grouping
      if (first.isArchived())
        first.archivePaths(key, tmp);
      else if (first.type() == Media::TypeVideo)
        key = first.path();
      else
        key = first.dirPath();  // todo: media::parent, media::relativeParent,
                                   // media::relativePath
      key = key.mid(prefix.length());
    }
    key = qElide(key, options.iconTextWidth);
    keys.append(key);

    GroupStats& s = stats[key];
    s.itemCount += g.count();
    //    for (const Media& m : g) {
    //      Media tmp(m);
    //      tmp.readMetadata();
    //      s.byteCount += tmp.originalSize();
    //    }
  }

  QHash<QString, MediaGroupList> folders;

  qInfo() << "building folders...";
  for (int i = 0; i < list.count(); i++) {
    QString key = keys[i];
    GroupStats& s = stats[key];
    QString newKey = key + QString(" [x%1]").arg(s.itemCount);
    auto& set = folders[newKey];
    const auto& group = list[i];
    const auto split = Media::splitGroup(group, options.maxPerPage);
    set.append(split);
  }

  MediaGroup index;
  for (auto& key : qAsConst(folders).keys()) index.append(Media(key));

  auto f = QtConcurrent::map(index, [&](Media& m) {
    const Media& first = folders[m.path()][0][0];
    QImage img = loadThumb(first, options);
    m.setImage(img);
    m.readMetadata();
  });

  while (f.isRunning()) {
    qInfo("loading thumbnails... <PL>%d/%d", f.progressValue(), f.progressMaximum());
    QThread::msleep(100);
  }

  qInfo() << "sorting...";
  Media::sortGroup(index, "path");
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
  MediaGroupList unpaired;
  const char* unpairedKey = "*unpaired*";

  MediaGroup index;                  // dummy group for top-level navigation
  index.append(Media(unpairedKey));  // entry for the "unpaired" list

  QHash<QString, MediaGroupList> sets;
  for (MediaGroup g : list) {
    QStringList dirPaths;
    for (const Media& m : g) {
      QString path = m.path().left(m.path().lastIndexOf("/"));
      if (!dirPaths.contains(path)) dirPaths.append(path);
    }

    // we have a pair, add it
    if (dirPaths.count() == 2) {
      // find the common prefix, exclude from the key
      // todo: maybe overflow potential, but all paths should be absolute...
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

  if (sets[unpairedKey].isEmpty())
    index.removeFirst();

  auto f = QtConcurrent::map(index, [&](Media& m) {
    m.setImage(loadThumb(sets[m.path()][0][0], options));
    m.readMetadata();
  });
  while (f.isRunning()) {
    qInfo("loading thumbnails... <PL>%d/%d", f.progressValue(), f.progressMaximum());
    QThread::msleep(100);
  }

  Media::sortGroup(index, "path");
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
}

void MediaBrowser::show(const MediaGroupList& list) {
  MediaGroupListWidget* w = new MediaGroupListWidget(list, _options);
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
    else {
      MediaSearch search;
      search.needle = m;
      search.params = _options.params;
      search = engine().query(search);

      search.matches.prepend(search.needle);
      MediaGroupList list;
      if (!engine().db->filterMatch(_options.params, search.matches)) list.append(search.matches);
      engine().db->filterMatches(_options.params, list);
      show(list);
    }
  }
}
