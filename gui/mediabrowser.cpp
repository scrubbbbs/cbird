#include "mediabrowser.h"
#include "videocontext.h"
#include "mediafolderlistwidget.h"
#include "mediagrouplistwidget.h"
#include "engine.h"
#include "database.h"
#include "qtutil.h"

extern Engine& engine();

#define MB_TEXT_MAXLEN (40) // max char width of item text

// hack to snoop events, signals/slot invocations
#ifdef DEBUG_EVENT_FILTER

#include "QtCore/5.4.0/QtCore/private/qobject_p.h"

class DebugFilter : public QObject {
 public:
  DebugFilter() : QObject(){};
  virtual ~DebugFilter(){};

  bool eventFilter(QObject* object, QEvent* event) {
    static int counter = 0;

    printf("event %d: object=%p event=%p type=%d spontaneous=%d\n", counter++,
           object, event, event->type(), event->spontaneous());

    if (event->type() == QEvent::MetaCall) {
      QMetaCallEvent* mc = (QMetaCallEvent*)event;
      QMetaMethod slot = object->metaObject()->method(mc->id());
      const char* senderClass = "unknown";
      const char* recvClass = "unknown";

      if (mc->sender() && mc->sender()->metaObject())
        senderClass = mc->sender()->metaObject()->className();
      if (object->metaObject()) recvClass = object->metaObject()->className();
      printf("meta call event: sender=%s receiver=%s method=%s\n", senderClass,
             recvClass, qPrintable(slot.methodSignature()));

      // return true;
    }

    return QObject::eventFilter(object, event);
  }
};
#endif

static QMap<QString, MediaGroupList> _matchSets;

MediaBrowser::MediaBrowser(const SearchParams& params, bool sets,
                           int selectionMode) {
  _params = params;
  _sets = sets;
  _selectionMode = selectionMode;
}

int MediaBrowser::showList(const MediaGroupList& list,
                           const SearchParams& params, int selectionMode) {
  MediaBrowser browser(params, false, selectionMode);

#ifdef DEBUG_EVENT_FILTER
  qApp->installEventFilter(new DebugFilter());
#endif

  browser.show(list);

  return qApp->exec();
}

int MediaBrowser::show(const MediaGroupList& list, const SearchParams& params,
                       int mode, int selectionMode) {
  if (list.count() <= 0) return 0;
  for (auto& g : list)
    if (g.count() <= 0) {
      qCritical() << "empty group in list";
      return 0;
    }
  if (mode == ShowNormal)
    return MediaBrowser::showList(list, params, selectionMode);
  if (mode == ShowPairs) return MediaBrowser::showSets(list, params);
  if (mode == ShowFolders) return MediaBrowser::showFolders(list, params);

  Q_UNREACHABLE();
}

int MediaBrowser::showFolders(const MediaGroupList& list,
                              const SearchParams& params) {
  if (list.count() <= 0) return 0;

  QString prefix = Media::greatestPathPrefix(list);

  class GroupStats {
   public:
    int itemCount = 0;
    //int byteCount = 0;
  };

  QHash<QString, GroupStats> stats;

  qInfo() << "collecting info...";
  QStringList keys;
  for (const MediaGroup& g : list) {
    Q_ASSERT(g.count() > 0);
    const Media& first = g.at(0);
    QString key, tmp;
    if (first.isArchived())
      first.archivePaths(key, tmp);
    else if (first.type() == Media::TypeVideo)
      key = first.path();
    else
      key = first.parentPath();  // todo: media::parent, media::relativeParent,
                                 // media::relativePath
    key = key.mid(prefix.length());
    keys.append(key);

    GroupStats& s = stats[key];
    s.itemCount += g.count();
//    for (const Media& m : g) {
//      Media tmp(m);
//      tmp.readMetadata();
//      s.byteCount += tmp.originalSize();
//    }
  }

  qInfo() << "building folders...";
  for (int i = 0; i < list.count(); i++) {
    QString key = keys[i];
    GroupStats& s = stats[key];
    QString newKey = key + QString(" [x%1]").arg(s.itemCount);
    _matchSets[newKey].append(list[i]);
  }

  qInfo() << "loading thumbnails...";
  QVector<QFuture<Media>> work;
  MediaGroup index;
  for (auto key : _matchSets.keys()) {
    work.append(QtConcurrent::run([key] {
      Media m(key);
      const Media& ref = _matchSets[key][0][0];
      QImage img;
      if (ref.type() == Media::TypeVideo)
        img = VideoContext::frameGrab(ref.path(), -1, true);
      else
        img = ref.loadImage(QSize(480, 0));
      m.setImage(img);
      m.readMetadata();
      return m;
    }));
  }
  for (auto& w : work) {
    w.waitForFinished();
    index.append(w.result());
  }

  qInfo() << "sorting...";
  Media::sortGroup(index, "path");
  MediaBrowser browser(params, true);
  browser.showIndex(index, prefix.mid(0, prefix.length() - 1));
  return qApp->exec();
}

int MediaBrowser::showSets(const MediaGroupList& list,
                           const SearchParams& params) {
  if (list.count() <= 0) return 0;

  // try to form a list of MediaGroupList, where each member
  // matches only between two directories, or an image "set".
  // If there is no correlation, put match in "unpaired" set.

  MediaGroupList unpaired;
  const char* unpairedKey = "*unpaired*";

  MediaGroup index;                  // dummy group for top-level navigation
  index.append(Media(unpairedKey));  // entry for the "unpaired" list

  for (MediaGroup g : list) {
    // note: the groups from search results default sort by score, with
    // the first image being the needle...probably don't want to sort
    // Media::sortGroup(g, "path");

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
      while (i < a.length() &&     // longest prefix
             i < b.length() &&
             a[i] == b[i])
        i++;
      while (i-1 >= 0 && a[i-1] != '/') // parent dir
        i--;

      QString key;
      key += qElide(dirPaths[0].mid(i), MB_TEXT_MAXLEN) + "/";
      key += "\n";
      key += qElide(dirPaths[1].mid(i), MB_TEXT_MAXLEN) + "/";

      _matchSets[key].append(g);
    } else {
      _matchSets[unpairedKey].append(g);
    }
  }

  // any set with only one match, throw into the "other"
  QMap<QString, MediaGroupList> filtered;
  for (auto key : _matchSets.keys())
    if (key != unpairedKey && _matchSets[key].count() == 1) {
      _matchSets[unpairedKey].append(_matchSets[key][0]);
      _matchSets.remove(key);
    } else {
      // add the dummy item to index
      Media m(key);
      if (!index.contains(m)) {
        m.setImage(_matchSets[key][0][0].loadImage());
        m.readMetadata();
        index.append(m);
      }
    }

  if (_matchSets[unpairedKey].isEmpty())
    index.removeFirst();
  else {
    Media& other = index.first();
    other.setImage( _matchSets[unpairedKey][0][0].loadImage() );
  }
  Media::sortGroup(index, "path");
  MediaBrowser browser(params, true);

  if (index.count() == 1)
    browser.show(list);
  else
    browser.showIndex(index, "");

#ifdef DEBUG_EVENT_FILTER
  qApp->installEventFilter(new DebugFilter());
#endif

  return qApp->exec();
}

void MediaBrowser::showIndex(const MediaGroup& index, const QString& basePath) {
  MediaFolderListWidget* w =
      new MediaFolderListWidget(index, basePath, engine().db);
  connect(w, &MediaFolderListWidget::mediaSelected, this,
          &MediaBrowser::mediaSelected);
  w->show();
}

void MediaBrowser::show(const MediaGroupList& list) {
  // todo: browser options
  // fast seek is not great for many codecs
  int options = 0;  // MediaGroupListWidget::FlagFastSeek;
  MediaGroupListWidget* w =
      new MediaGroupListWidget(list, nullptr, options, engine().db);
  connect(w, &MediaGroupListWidget::mediaSelected, this,
          &MediaBrowser::mediaSelected);
  w->show();
  w->activateWindow();
  w->setAttribute(Qt::WA_DeleteOnClose);
}

void MediaBrowser::mediaSelected(const MediaGroup& group) {
  for (const Media& m : group) {
    if (_selectionMode == SelectExitCode) {
      qApp->exit(m.position() + 1);  // subtract 1 where show() was called
      return;
    }

    const MediaFolderListWidget* mw =
        dynamic_cast<MediaFolderListWidget*>(sender());
    if (mw && _sets && _matchSets.count() > 0)
      show(_matchSets[m.path()]);
    else {
      MediaSearch search;
      search.needle = m;
      search.params = _params;
      search = engine().query(search);

      search.matches.prepend(search.needle);
      MediaGroupList list;
      if (!engine().db->filterMatch(_params, search.matches))
        list.append(search.matches);
      engine().db->filterMatches(_params, list);
      show(list);
    }
  }
}
