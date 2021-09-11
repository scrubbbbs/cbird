/* General reusable qt bits and platform abstractions
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
#pragma once

/// portable interface for desktop/platform environment
class DesktopHelper {
 public:
  /// open filesystem location of path, ideally also scroll to and highlight the item
  static void revealPath(const QString& path);

  /// open video player with optional starting time
  static void openVideo(const QString& path, double seekSeconds = 0.0);

  /// open audio track (visual) comparison tool
  static void compareAudio(const QString& path1, const QString& path2);

  /// play two videos side-by-side with seek for alignment
  static void playSideBySide(const QString& path1, double seek1,
                             const QString& path2, double seek2);

  /// location of settings file
  static QString settingsFile();

  /// move file to trash, do not overwrite existing files
  static bool moveToTrash(const QString& path);

private:
  static void runProgram(QStringList& args, bool wait, const QString& inPath = "",
                         double seek = 0.0, const QString& inPath2 = "",
                         double seek2 = 0.0);
  static QVariant getSetting(const QString& key, const QVariant& defaultValue);
  static void putSetting(const QString &key, const QVariant &value);
  static bool chooseProgram(QStringList& args, const QVector<QStringList>& options,
                            const char* settingsKey, const char* dialogTitle, const char* dialogText);
  static bool moveFile(const QString& path, const QString& dir);
};

/// common tasks for widgets
class WidgetHelper {
 public:
  /// save position/size and maximized state of a top-level widget
  static void saveGeometry(const QWidget* w, const char* id = nullptr) {
    if (!id) id = w->metaObject()->className();
    QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
    settings.beginGroup(id);
    settings.setValue("geometry", w->saveGeometry());
    settings.setValue("maximized", w->isMaximized());
    settings.endGroup();
  }

  /// restore position/size and maximized state of a top-level widget
  /// @return true if widget was maximized
  static bool restoreGeometry(QWidget* w, const char* id = nullptr) {
    if (!id) id = w->metaObject()->className();
    QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
    settings.beginGroup(id);
    if (!w->restoreGeometry(settings.value("geometry").toByteArray()))
      w->setGeometry(100, 100, 1024, 768);

    return settings.value("maximized").toBool();
  }

  /// one-liner for adding actions
  static QAction* addAction(const QString& label, const QKeySequence& shortcut,
                            QWidget* target, const char* slot) {
    QAction* a = new QAction(label, target);
    target->connect(a, SIGNAL(triggered(bool)), target, slot);
    a->setShortcut(shortcut);
    target->addAction(a);
    return a;
  }

  /// draw rich text using global style
  static void drawRichText(QPainter* painter, const QRect& r,
                           const QString& text) {
    // todo: external stylesheet
    QTextDocument td;

    td.setDefaultStyleSheet(
        "table { color:rgba(255,255,255,192); font-size:16px; }"
        "tr.even { background-color:rgba(96,96,96,128); }" // even rows of table
        "tr.odd  { background-color:rgba(64,64,64,128); }" // odd rows of table
        ".more { color:#9F9; }"   // value is >
        ".less { color:#F99; }"   // value is <
        ".same { color:#99F; }"   // value is ==
        ".time { color:#FF9; }"   // value is a timecode or duration
        ".video { color:#9FF; }"  // value describes video properties
        ".audio { color:#F9F; }"  // value describes audio properties
        ".none { color:#000; }"   // "hide" value by matching background color
        ".archive { color:#FF9; }"// value is an archive/zip file
        ".file { color:#FFF; }"   // value is normal file
        ".default { color:#FFF; }"   // normal text
     );

    td.setHtml(text);
    td.setDocumentMargin(0);

    painter->save();
    painter->translate(r.x(), r.y());

    QRect rect1 = QRect(0, 0, r.width(), r.height());
    td.drawContents(painter, rect1);
    painter->restore();
  }
};

class DBHelper {
 public:
  static bool isCacheFileStale(const QSqlDatabase& db, const QString& cacheFile) {
    QFileInfo cacheInfo(cacheFile);

    return !cacheInfo.exists() ||
           lastModified(db) > cacheInfo.lastModified();
  }

  static QDateTime lastModified(const QSqlDatabase& db) {
    // this only works with local file database drivers, like sqlite
    QString dbPath = db.databaseName();
    Q_ASSERT( !dbPath.isEmpty() );

    QFileInfo dbInfo(dbPath);
    if (!dbInfo.exists()) return QDateTime::fromSecsSinceEpoch(INT64_MAX);

    return dbInfo.lastModified();
  }
};

/// helpers for menus/context menus
class MenuHelper {
 public:
  /// build menu for a directory tree
  static QMenu* dirMenu(const QString& root, QObject* target,
                        const char* slot) {
    QMenu* menu = makeDirMenu(root, target, slot);

    QAction* action = new QAction("*new folder*", menu);
    action->connect(action, SIGNAL(triggered(bool)), target, slot);
    action->setData(";newfolder;");
    menu->insertAction(menu->actions()[0], action);

    return menu;
  }

 private:
  static QMenu* makeDirMenu(const QString& root, QObject* target,
                            const char* slot) {
    QMenu* menu = new QMenu;
    QAction* action = menu->addAction(".");
    action->setData(root);
    action->connect(action, SIGNAL(triggered(bool)), target, slot);

    const auto& list =
        QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    int partition = 0;
    QMenu* partMenu = nullptr;
    for (const QFileInfo& entry : list) {
      // todo: setting for index dir name
      const QString& path = entry.absoluteFilePath();
      if (path.endsWith("_index")) continue;

      // todo: setting or detect max popup size
      const int maxFolders = 100;
      if (list.count() > maxFolders) {
        if (partition == 0) {
          const QString name = entry.fileName();
          partMenu = new QMenu;
          partMenu->setTitle(name + "...");
          menu->addMenu(partMenu);
        }
        partition = (partition + 1) % maxFolders;
      } else
        partMenu = menu;

      if (QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot).count() >
          0) {
        QMenu* subMenu = makeDirMenu(path, target, slot);
        subMenu->setTitle(entry.fileName());
        partMenu->addMenu(subMenu);
      } else {
        action = partMenu->addAction(entry.fileName());
        action->setData(path);
        action->connect(action, SIGNAL(triggered(bool)), target, slot);
      }
    }

    return menu;
  }
};

/// elide string in the middle
static inline QString qElide(const QString& str, int maxLen = 80) {
  QString tmp;
  if (str.length() > maxLen) {
    int half = maxLen / 2 - 3;
    tmp = str.mid(0, half) + "..." + str.mid(str.length() - half);
  } else
    tmp = str;

  return tmp;
}

/// get rotation angle represented by matrix
static inline double qRotationAngle(const QMatrix& mat) {
  QPointF p0 = mat.map(QPointF(0, 0));
  QPointF p1 = mat.map(QPointF(1, 0));

  return 180.0 / M_PI * atan((p1.y() - p0.y()) / (p1.x() - p0.x()));
}

/// customized logger with compression, color, etc
void qColorMessageOutput(QtMsgType type, const QMessageLogContext& context, const QString& msg);

/// flush log messages before using printf/scanf etc
void qFlushOutput();

extern QThreadStorage<QString> qMessageContext;

class MessageContext {
  NO_COPY_NO_DEFAULT(MessageContext,QObject);
 public:
  MessageContext(const QString& context) {
    if (qMessageContext.hasLocalData() && !qMessageContext.localData().isEmpty())
      qWarning() << "overwriting message context"; // todo: save/restore message context
    qMessageContext.setLocalData(context);
  }
  ~MessageContext() {
    qMessageContext.setLocalData(QString()); }
};
