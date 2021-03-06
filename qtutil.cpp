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
#include "qtutil.h"
#include "profile.h"

// qttools/src/qdbus/qdbus/qdbus.cpp
#ifndef Q_OS_WIN
#include <QtDBus/QtDBus>
#include <QtXml/QtXml>

static QStringList listServiceObjects(QDBusConnection& connection,
                                      const QString& service,
                                      const QString& path) {
  QStringList objectPaths;

  // make a low-level call, to avoid introspecting the Introspectable interface
  QDBusMessage call = QDBusMessage::createMethodCall(
      service, path.isEmpty() ? QLatin1String("/") : path,
      QLatin1String("org.freedesktop.DBus.Introspectable"),
      QLatin1String("Introspect"));
  QDBusReply<QString> xml = connection.call(call);
  if (path.isEmpty()) {
    // top-level
    if (xml.isValid()) {
      // printf("/\n");
    } else {
      QDBusError err = xml.error();
      if (err.type() == QDBusError::ServiceUnknown)
        qCritical() << "Service does not exist:" << service;
      else
        qCritical() << "Error:" << err.name() << err.message();
      return objectPaths;
    }
  } else if (!xml.isValid()) {
    // this is not the first object, just fail silently
    return objectPaths;
  }
  QDomDocument doc;
  doc.setContent(xml);
  QDomElement node = doc.documentElement();
  QDomElement child = node.firstChildElement();
  while (!child.isNull()) {
    if (child.tagName() == QLatin1String("node")) {
      QString sub =
          path + QLatin1Char('/') + child.attribute(QLatin1String("name"));
      // printf("%s\n", qPrintable(sub));
      objectPaths.append(sub);
      objectPaths.append(listServiceObjects(connection, service, sub));
    }
    child = child.nextSiblingElement();
  }
  return objectPaths;
}

static void callServiceMethod(const QStringList& args) {
  auto service = args[0];
  auto object = args[1];
  auto interface = args[2];
  auto method = args[3];
  QVariantList methodArgs;
  for (int i = 4; i < args.count(); ++i) methodArgs.append(args[i]);

  auto bus = QDBusConnection::sessionBus();
  auto paths = listServiceObjects(bus, service, QString());
  qDebug() << "DBus services" << QStringList(bus.interface()->registeredServiceNames());
  qDebug() << "Service objects" << paths;

  // path may contain regular expression, the first matching path is taken
  // useful for apps that have randomized path names (krusader)

  bool validPath = false;
  QRegularExpression pathMatch(object);
  for (auto& path : paths)
    if (pathMatch.match(path).hasMatch()) {
      object = path;
      validPath = true;
      break;
    }

  if (!validPath) {
    qWarning() << "DBus service missing" << object << "is" << service << "running?";
    return;
  }

  QDBusInterface remoteApp(service, object, interface, QDBusConnection::sessionBus());
  if (remoteApp.isValid()) {
    QDBusReply<void> reply;
    reply = remoteApp.callWithArgumentList(QDBus::Block, method, methodArgs);
    if (!reply.isValid()) qWarning() << "DBus Error:" << reply.error();

  } else
    qWarning() << "DBus failed to connect:" << remoteApp.lastError();
}

#endif  // !Q_OS_WIN

void DesktopHelper::runProgram(QStringList& args, bool wait,
                               const QString& inPath, double seek,
                               const QString& inPath2, double seek2) {
  QString path(inPath);
  QString path2(inPath2);
#ifdef Q_OS_WIN
  path.replace("/", "\\");
  path2.replace("/", "\\");
#endif
  for (QString& arg : args) {
    arg.replace("%1", path);
    arg.replace("%2", path2);
    arg.replace("%seek2(int)", QString::number(int(seek2)));
    arg.replace("%seek(int)", QString::number(int(seek)));
    arg.replace("%seek2", QString::number(seek2));
    arg.replace("%seek", QString::number(seek));
    arg.replace("%home", QDir::homePath());
    arg.replace("%dirname(1)", QFileInfo(path).dir().absolutePath());
    arg.replace("%dirname(2)", QFileInfo(path2).dir().absolutePath());
  }

  qInfo() << args;

  if (args.count() > 0) {
    const QString prog = args.first();
    if (prog == "DesktopServices") {
      if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        qWarning() << "QDesktopService::openUrl failed for" << path;
#ifndef Q_OS_WIN
    } else if (prog == "DBus") {
      // example : "DBus, org.krusader, /Instances/krusader/right_manager,
      // org.krusader.PanelManager, newTab, %dirname(1)
      if (args.count() < 5) {
        qWarning() << "DBus requires at least 4 arguments (service, path, "
                      "interface, method, [args...])";
        return;
      }

      // multiple calls possible, separated by "&&"
      qDebug() << "DBus args:" << args;
      QVector<QStringList> calls;
      QStringList dbusArgs;
      for (int i = 1; i < args.count(); ++i) {
        if (args[i] == "&&") {
          calls.append(dbusArgs);
          dbusArgs.clear();
        }
        else
          dbusArgs.append(args[i]);
      }
      calls.append(dbusArgs);

      for (const QStringList& dbusCall : qAsConst(calls))
        callServiceMethod(dbusCall);

#endif  // !Q_OS_WIN
    } else {
      QProcess p;
      p.setProgram(prog);

      const QString disableAppProgs = getenv("CBIRD_NO_APPIMAGE_PROGS");
      if (disableAppProgs.isEmpty()) {
        const QString appDir = getenv("APPDIR");
        const QString appProg = appDir + "/cbird/bin/" + prog;
        if (!appDir.isEmpty() && QFileInfo(appProg).exists()) {
          qInfo() << "using" << appProg << "for:" << prog;
          qInfo() << "to disable this, set CBIRD_NO_APPIMAGE_PROGS";

          // put the bundled apps before everything else
          auto env = QProcessEnvironment::systemEnvironment();
          const QString binPath = env.value("PATH");
          const QString libPath = env.value("LD_LIBRARY_PATH"); // might be empty, should be fine
          env.insert("PATH", appDir+"/cbird/bin:" + binPath);
          env.insert("LD_LIBRARY_PATH", appDir+"/cbird/lib:" + libPath);
          p.setProcessEnvironment(env);
          p.setProgram(appProg);
        }
      }
#ifdef Q_OS_WIN
      p.setNativeArguments(args.mid(1).join(" "));
#else
      p.setArguments(args.mid(1));
#endif
      if (!wait) {
        if (!p.startDetached())
          qWarning() << prog << "failed to start, is it installed?";
      } else {
        p.start();
        if (!p.waitForStarted()) {
          qWarning() << prog << "failed to start, is it installed?";
          return;
        }
        p.waitForFinished();
        if (p.exitCode() != 0)
          qWarning() << prog << "exit code" << p.exitCode() << p.errorString();
      }
    }
  }
}

QVariant DesktopHelper::getSetting(const QString& key,
                                   const QVariant& defaultValue) {
  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup("DesktopHelper");
  if (!settings.contains(key)) settings.setValue(key, defaultValue);

  return settings.value(key);
}

void DesktopHelper::putSetting(const QString& key, const QVariant& value) {
  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup("DesktopHelper");
  settings.setValue(key, value);
}

bool DesktopHelper::chooseProgram(QStringList& args,
                                  const QVector<QStringList>& options,
                                  const char* settingsKey,
                                  const char* dialogTitle,
                                  const char* dialogText) {
  if (args.empty() || (qApp->keyboardModifiers() & Qt::ControlModifier)) {
    QStringList items;
    for (auto& option : qAsConst(options)) items += option.first();

    QWidget* parent = qApp->widgetAt(QCursor::pos());

    bool ok = false;
    QString item =
        QInputDialog::getItem(parent, dialogTitle,
                              QString(dialogText) + "\n\n" +
                                  "To change this setting, press the Control "
                                  "key while selecting the action.",
                              items, 0, false, &ok);
    if (!ok) return false;

    for (auto& option : qAsConst(options))
      if (option.first() == item) {
        args = option;
        args.removeFirst();
        break;
      }

    putSetting(settingsKey, args);
  }

  return true;
}

void DesktopHelper::revealPath(const QString& path) {
  QVector<QStringList> fileManagers;

#ifdef Q_OS_WIN
  const QStringList defaultArgs{{"explorer", "/select,\"%1\""}};
#else
  const QStringList defaultArgs;
  fileManagers += QStringList{{"Default", "DesktopServices"}};
  fileManagers += QStringList{{"Dolphin (KDE)", "/usr/bin/dolphin", "--select", "%1"}};
  fileManagers += QStringList{
      {"Krusader (Right Panel)", "DBus", "org.krusader", "/Instances/krusader[0-9]*/right_manager",
       "", "newTab", "%dirname(1)", "&&", "org.krusader", "/MainWindow_[0-9]*", "", "raise"}};
  fileManagers += QStringList{
      {"Krusader (Left Panel)", "DBus", "org.krusader", "/Instances/krusader[0-9]*/left_manager",
       "", "newTab", "%dirname(1)", "&&", "org.krusader", "/MainWindow_[0-9]*", "", "raise"}};
  fileManagers += QStringList{{"Nautilus (GNOME)", "/usr/bin/nautilus", "-s", "%1"}};
#endif
  const char* settingsKey = "OpenFileLocation";
  QStringList args = getSetting(settingsKey, defaultArgs).toStringList();

  if (!chooseProgram(
          args, fileManagers, settingsKey, "Choose File Manager",
          "Please choose the program for viewing a file's location."))
    return;

  // QDesktopServices cannot reveal file location and select it, we need the dir
  // path
  QString tmp = path;
  if (args.count() > 0 && args.first() == "DesktopServices")
    tmp = QFileInfo(path).absoluteDir().path();

  runProgram(args, false, tmp);
}

void DesktopHelper::openVideo(const QString& path, double seekSeconds) {
  QStringList args;
  if (abs(seekSeconds) >= 0.1) {
    const char* settingsKey = "OpenVideoSeek";
    const QStringList defaultArgs;
    args = getSetting(settingsKey, defaultArgs).toStringList();

    QVector<QStringList> openVideoSeek;
    openVideoSeek += QStringList{{"Default", "DesktopServices"}};
#ifdef Q_OS_WIN
    openVideoSeek +=
        QStringList{{"VLC", "\"C:/Program Files (x86)/VideoLan/VLC/vlc.exe\"",
                     "--start-time=%seek", "\"%1\""}};
    openVideoSeek +=
        QStringList{{"FFplay", "ffplay.exe", "-ss", "%seek", "\"%1\""}};
    openVideoSeek += QStringList{{"MPlayer", "mplayer.exe", "-ss", "%seek", "\"%1\""}};
    openVideoSeek += QStringList{{"MPV", "mpv.exe", "--start=%seek", "\"%1\""}};
#else
    openVideoSeek += QStringList{
        {"Celluloid", "celluloid", "--mpv-options=--start=%seek", "%1"}};
    openVideoSeek += QStringList{{"FFplay", "ffplay", "-ss", "%seek", "%1"}};
    openVideoSeek += QStringList{{"MPlayer", "mplayer", "-ss", "%seek", "%1"}};
    openVideoSeek += QStringList{{"MPV", "mpv", "--start=%seek", "%1"}};
    openVideoSeek +=
        QStringList{{"SMPlayer", "smplayer", "-start", "%seek(int)", "%1"}};
    openVideoSeek += QStringList{{"VLC", "vlc", "--start-time=%seek", "%1"}};
#endif
    if (!chooseProgram(args, openVideoSeek, settingsKey, "Choose Video Player",
                       "Select the program for viewing video at a timestamp"))
      return;
  } else {
    const char* settingsKey = "OpenVideo";
    const QStringList defaultArgs = QStringList{{"DesktopServices"}};
    args = getSetting(settingsKey, defaultArgs).toStringList();
  }

  runProgram(args, false, path, seekSeconds);
}

void DesktopHelper::compareAudio(const QString& path1, const QString& path2) {
  const QString settingsKey = "CompareAudio";
  const QStringList defaultArgs{{"ff-compare-audio", "%1", "%2"}};
  QStringList args = getSetting(settingsKey, defaultArgs).toStringList();
  runProgram(args, false, path1, 0.0, path2);
}

void DesktopHelper::playSideBySide(const QString& path1, double seek1,
                                   const QString& path2, double seek2) {
  const QString settingsKey = "PlaySideBySide";
  const QStringList defaultArgs{{"ffplay-sbs", "%1", "%seek", "%2", "%seek2"}};
  QStringList args = getSetting(settingsKey, defaultArgs).toStringList();
  runProgram(args, false, path1, seek1, path2, seek2);
}

QString DesktopHelper::settingsFile() {
  QString path =
      QProcessEnvironment::systemEnvironment().value("CBIRD_SETTINGS_FILE");
  if (path.isEmpty())
    path = QSettings(QSettings::IniFormat, QSettings::UserScope,
                     qApp->applicationName())
               .fileName();
  return path;
}

bool DesktopHelper::moveFile(const QString& path, const QString& dir) {
  const QFileInfo info(path);

  if (!QDir(dir).exists()) {
    qWarning() << "destination does not exist:" << dir;
    return false;
  }

  QString trashPath = dir + "/" + info.fileName();
  int num = 1;
  while (QFileInfo(trashPath).exists()) {
    trashPath = QString("%1/%2.%3.%4")
                    .arg(dir)
                    .arg(info.completeBaseName())
                    .arg(num)
                    .arg(info.suffix());
    num++;
  }

  bool ok = QDir().rename(path, trashPath);

  if (ok)
    qInfo("moved\n\t%s\nto\n\t%s\n", qUtf8Printable(path), qUtf8Printable(trashPath));
  else
    qWarning("move\n\t%s\nto\n\t%s\nfailed due to filesystem error",
             qUtf8Printable(path), qUtf8Printable(trashPath));

  return ok;
}

bool DesktopHelper::moveToTrash(const QString& path) {
  QFileInfo info(path);
  if (!info.isFile()) {
    qWarning() << "requested path is not a file:" << path;
    return false;
  }

  QString dir =
      QProcessEnvironment::systemEnvironment().value("CBIRD_TRASH_DIR");
  if (!dir.isEmpty()) return moveFile(path, dir);

  const char* settingsKey = "TrashFile";

#ifdef Q_OS_WIN
  const QStringList defaultArgs;
  QStringList args = getSetting(settingsKey, defaultArgs).toStringList();

  if (!args.empty()) {
    runProgram(args, true, path);
  } else {
    // use the recycle bin with move fallback
    // - we might not have a window for dialogs of SHFileOperationW
    // - SHFileOperationW will silently delete on filesystems that cannot
    // recycle
    // - therefore, if path cannot be recycled do our own move
    auto absPath = QFileInfo(path).absoluteFilePath();

    SHQUERYRBINFO info;
    info.cbSize = sizeof(info);
    HRESULT res = SHQueryRecycleBinW(LPCWSTR(absPath.utf16()), &info);
    if (res != S_OK) {
      qInfo() << absPath << "does not support recycling" << Qt::hex << res;

      QString mountPoint;
      const auto parts = absPath.split("/", Qt::SkipEmptyParts);
      if (absPath.startsWith("//")) {
        if (parts.length() > 2)
          mountPoint = "//" + parts[0] + "/" + parts[1] + "/";
      } else {
        const QStorageInfo sInfo(QFileInfo(absPath).dir());
        if (!sInfo.isValid()) {
          qWarning() << "has no mount point (drive letter) or is invalid"
                     << absPath;
          return false;
        }
        mountPoint = sInfo.rootPath();
      }

      if (mountPoint.isEmpty() || !QDir(mountPoint).exists()) {
        qWarning() << "invalid or unsupported mount point" << mountPoint;
        return false;
      }

      const QString volumeTrashDir =
          getSetting("VolumeTrashDir", "_trash").toString();

      qInfo() << "trying fallback" << mountPoint + volumeTrashDir;

      QDir trashDir(mountPoint);
      if (!trashDir.exists(volumeTrashDir) && !trashDir.mkdir(volumeTrashDir)) {
        qWarning() << "fallback failed, cannot create volume trash dir on"
                   << trashDir;
      }

      return moveFile(path, trashDir.absoluteFilePath(volumeTrashDir));
    } else {
      absPath.replace(QLatin1Char('/'), QLatin1Char('\\'));  // backslashified
      QByteArray delBuf((absPath.length() + 2) * 2,
                        0);  // utf16, double-null terminated
      memcpy(delBuf.data(), absPath.utf16(), absPath.length() * 2);

      SHFILEOPSTRUCTW op;
      memset(&op, 0, sizeof(SHFILEOPSTRUCTW));
      op.wFunc = FO_DELETE;
      op.pFrom = (LPCWSTR)delBuf.constData();
      op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI;
      int status = SHFileOperationW(&op);
      if (status != 0)
        qWarning() << "SHFileOperation() error" << Qt::hex << status;
    }
  }
#else
  const QStringList defaultArgs{{"trash-put", "%1"}};
  QStringList args = getSetting(settingsKey, defaultArgs).toStringList();
  // we must wait, because the caller could be renaming or moving
  runProgram(args, true, path);
#endif
  bool ok = !QFileInfo(path).exists();
  if (!ok) qWarning() << "failed to move to trash" << path;
  return ok;
}

QString DesktopHelper::tempName(const QString& nameTemplate, QObject *parent, int maxLifetime) {
  QString fileName;
  {
    QTemporaryFile temp;
    temp.setAutoRemove(false);
    temp.setFileTemplate(QDir::tempPath() + "/" + nameTemplate);
    if (!temp.open()) return "";
    fileName = temp.fileName();
  } // temp is now closed

  if (parent)
    QObject::connect(parent, &QObject::destroyed, [=]() {
      QFile f(fileName);
      if (f.exists() && !f.remove())
        qWarning() << "failed to delete temporary (at exit)" << fileName;
    });

  if (maxLifetime > 0)
    QTimer::singleShot(maxLifetime*1000, [=]() {
      QFile f(fileName);
      if (f.exists() && !f.remove())
        qWarning() << "failed to delete temporary (on timer)" << fileName;
    });

  QObject* object = new QObject(qApp);
  QObject::connect(object, &QObject::destroyed, [=]() {
    QFile f(fileName);
    if (f.exists() && !f.remove())
      qWarning() << "failed to delete temporary (at exit)" << fileName;
  });

  return fileName;
}

QKeySequence WidgetHelper::getShortcut(QSettings& settings, const QString& label,
                                       const QKeySequence& defaultShortcut) {
  QString key = label.toLower();
  key = key.replace(QRegExp("[^a-z0-9-\\+\\%]+"), "_");

  if (!settings.contains(key)) settings.setValue(key, defaultShortcut);
  return settings.value(key, defaultShortcut).toString();
}

void WidgetHelper::saveGeometry(const QWidget *w, const char *id) {
  if (!id) id = w->metaObject()->className();
  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(id);
  settings.setValue("geometry", w->saveGeometry());
  settings.setValue("maximized", w->isMaximized());
  settings.endGroup();
}

bool WidgetHelper::restoreGeometry(QWidget *w, const char *id) {
  if (!id) id = w->metaObject()->className();
  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(id);
  if (!w->restoreGeometry(settings.value("geometry").toByteArray()))
    w->setGeometry(100, 100, 1024, 768);

  return settings.value("maximized").toBool();
}

QAction* WidgetHelper::addAction(QSettings& settings,
                                 const QString& label,
                                 const QKeySequence& shortcut,
                                 QWidget* target,
                                 const char* slot) {
  QAction* a = new QAction(label, target);
  target->connect(a, SIGNAL(triggered(bool)), target, slot);
  a->setShortcut(getShortcut(settings, label, shortcut));
  a->setShortcutVisibleInContextMenu(true);
  target->addAction(a);
  return a;
}

QAction* WidgetHelper::addAction(QSettings& settings,
                                 const QString& label,
                                 const QKeySequence& shortcut, QWidget* target,
                                 std::function<void()> fn) {
  QAction* a = new QAction(label, target);
  target->connect(a, &QAction::triggered, fn);
  a->setShortcut(getShortcut(settings, label, shortcut));
  a->setShortcutVisibleInContextMenu(true);
  target->addAction(a);
  return a;
}

QAction* WidgetHelper::addSeparatorAction(QWidget* parent) {
  QAction* sep = new QAction(parent);
  sep->setSeparator(true);
  parent->addAction(sep);
  return sep;
}

void WidgetHelper::drawRichText(QPainter *painter, const QRect &r, const QString &text) {
  // todo: external stylesheet
  QTextDocument td;

  td.setDefaultStyleSheet(R"qss(
        table { color:rgba(255,255,255,192); font-size:16px; }
        tr.even { background-color:rgba(96,96,96,128); } /* even rows of table */
        tr.odd  { background-color:rgba(64,64,64,128); } /* odd rows of table */
        .more { color:#9F9; }   /* value is > */
        .less { color:#F99; }   /* value is < */
        .same { color:#99F; }   /* value is == */
        .time { color:#FF9; }   /* value is a timecode or duration */
        .video { color:#9FF; }  /* value describes video properties */
        .audio { color:#F9F; }  /* value describes audio properties */
        .none { color:rgba(0,0,0,0); } /* hide value by matching background color */
        .archive { color:#FF9; } /* value is archive/zip file */
        .file { color:#FFF; }    /* value is file */
        .default { color:#FFF; } /* normal text */
        .weed { color:#0FF };    /* value is weed */
        )qss");

  td.setHtml(text);
  td.setDocumentMargin(0);

  painter->save();
  painter->translate(r.x(), r.y());

  QRect rect1 = QRect(0, 0, r.width(), r.height());
  td.drawContents(painter, rect1);
  painter->restore();
}

QDateTime DBHelper::lastModified(const QSqlDatabase &db) {
  // this only works with local file database drivers, like sqlite
  QString dbPath = db.databaseName();
  Q_ASSERT( !dbPath.isEmpty() );

  QFileInfo dbInfo(dbPath);
  if (!dbInfo.exists()) return QDateTime::fromSecsSinceEpoch(INT64_MAX);

  return dbInfo.lastModified();
}


QMenu *MenuHelper::dirMenu(const QString &root, QObject *target, const char *slot, int maxDepth) {
  QMenu* menu = makeDirMenu(root, target, slot, maxDepth, 0);
  if (!menu) menu = new QMenu;

  QAction* action = new QAction("*new folder*", menu);
  action->connect(action, SIGNAL(triggered(bool)), target, slot);
  action->setData(";newfolder;");
  menu->insertAction(menu->actions()[0], action);

  return menu;
}

QMenu *MenuHelper::makeDirMenu(const QString &root, QObject *target, const char *slot,
                               int maxDepth, int depth) {

  if (depth >= maxDepth) return nullptr;

  const auto& list =
      QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  if (list.count() <= 0) return nullptr;

  QMenu* menu = new QMenu;
  QAction* action = menu->addAction(".");
  action->setData(root);
  action->connect(action, SIGNAL(triggered(bool)), target, slot);

  int partition = 0;
  QMenu* partMenu = nullptr;

  for (const auto& fileName : list) {

    // todo: setting for index dir name
    //const QString& path = entry.absoluteFilePath();
    const QString& path = root + "/" + fileName;
    if (fileName == INDEX_DIRNAME) continue;

    // todo: setting or detect max popup size
    const int maxFolders = 20;
    if (list.count() > maxFolders) {
      if (partition == 0) {
        const QString name = fileName;
        partMenu = new QMenu;
        partMenu->setTitle(name + "...");
        menu->addMenu(partMenu);
      }
      partition = (partition + 1) % maxFolders;
    } else
      partMenu = menu;

    QMenu* subMenu = makeDirMenu(path, target, slot, maxDepth, depth+1);
    if (subMenu) {
      subMenu->setTitle(fileName);
      partMenu->addMenu(subMenu);
    } else {
      action = partMenu->addAction(fileName);
      action->setData(path);
      action->connect(action, SIGNAL(triggered(bool)), target, slot);
    }
  }

  return menu;
}

QString qElide(const QString &str, int maxLen) {
  QString tmp;
  if (str.length() > maxLen) {
    int half = maxLen / 2 - 3;
    tmp = str.mid(0, half) + "..." + str.mid(str.length() - half);
  } else
    tmp = str;

  return tmp;
}

double qRotationAngle(const QMatrix &mat) {
  QPointF p0 = mat.map(QPointF(0, 0));
  QPointF p1 = mat.map(QPointF(1, 0));
  return 180.0 / M_PI * atan((p1.y() - p0.y()) / (p1.x() - p0.x()));
}

// bad form, but only way to get to metacallevent
// this header won't be found unless foo is added to qmake file
#include "QtCore/private/qobject_p.h"

DebugEventFilter::DebugEventFilter() : QObject() {}
DebugEventFilter::~DebugEventFilter() {}

bool DebugEventFilter::eventFilter(QObject* object, QEvent* event) {
  static int counter = 0;

  qWarning() << counter++ << event << event->spontaneous();

  // snoop signals/slot invocations
  if (event->type() == QEvent::MetaCall) {
    QMetaCallEvent* mc = (QMetaCallEvent*)event;
    QMetaMethod slot = object->metaObject()->method(mc->id());
    const char* senderClass = "unknown";
    const char* recvClass = "unknown";

    if (mc->sender() && mc->sender()->metaObject())
      senderClass = mc->sender()->metaObject()->className();
    if (object->metaObject())
      recvClass = object->metaObject()->className();

    qCritical("%d meta call event: sender=%s receiver=%s method=%s\n", counter++,
              senderClass, recvClass, qUtf8Printable(slot.methodSignature()));
  }

  return QObject::eventFilter(object, event);
}

//
// Custom logger magic!
//
// - "just works" by using qDebug() etc everywhere
// - colors for qDebug etc
// - disable colors if no tty (isatty()=0)
// - per-thread context can be added (like what file is being worked on)
// - special sequences recognized
//   <NC> - do not write message context, good for progress bars
//   <PL> - progress line, erase the previous line if text before <PL> matches
//   <EL> - elided line, text that follows is elided+padded to terminal;
//          may not come before <PL>
// - threaded output
//    * logs choke main thread, really badly on windows
//    * qFlushOutput() to sync up when needed
// - compression
//    * repeated log lines show # of repeats
//
// todo: drop logs if too much piles up
// todo: do bigger console writes, combine lines with timer
// todo: use ::write() instead of fwrite
//


#if !defined(QT_MESSAGELOGCONTEXT) // disabled in release targets by default... but we need it
#error qColorMessageOutput requires QT_MESSAGELOGCONTEXT
#endif

#define VT_RED "\x1B[31m"
#define VT_GRN "\x1B[32m"
#define VT_YEL "\x1B[33m"
#define VT_BLU "\x1B[34m"
#define VT_MAG "\x1B[35m"
#define VT_CYN "\x1B[36m"
#define VT_WHT "\x1B[37m"
#define VT_RESET "\x1B[0m"
#define VT_BRIGHT "\x1B[1m"
#define VT_DIM "\x1B[2m"
#define VT_UNDERL "\x1B[4m"
#define VT_BLINK "\x1B[5m"
#define VT_REVERSE "\x1B[6m"
#define VT_HIDDEN "\x1B[7m"

#include "exiv2/error.hpp" // capture exif library logs

static void exifLogHandler(int level, const char* msg) {
  const int nLevels = 4;
  constexpr QtMsgType levelToType[nLevels] = {
    QtDebugMsg, QtInfoMsg, QtWarningMsg, QtCriticalMsg
  };
  constexpr QMessageLogContext context("", 0, "exif()", "");
  if (level < nLevels && level > 0)
    qColorMessageOutput(levelToType[level], context, QString(msg).trimmed());
}


// headers for terminal detection
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <winbase.h>
#include <fileapi.h>
#else
#include <unistd.h>  // isatty
extern "C" {
#include <termcap.h>
}
#endif

/// Log record passed to logging thread
struct LogMsg {
  QString threadContext;
  QtMsgType type;
  QString msg;
  int version;
  int line;
  const char *file;
  const char *function;
  const char *category;
};

class MessageLog {
 private:
  QList<LogMsg> _log;

  QThread* _thread = nullptr;
  QMutex _mutex;
  QWaitCondition _logCond, _syncCond;
  volatile bool _stop = false; // set to true to stop log thread
  volatile bool _sync = false; // set to true to sync log thread

  bool _isTerm = false;     // true if we think stdout is a tty
  bool _termColors = false; // true if tty supports colors
  int _termColumns = -1;    // number of columns in the tty
  QString _homePath;

  MessageLog();
  ~MessageLog();

  QString format(const LogMsg& msg) const;

 public:
  static MessageLog& instance() {
    static MessageLog logger;
    return logger;
  }

  static QThreadStorage<QString>& context() {
    static QThreadStorage<QString> context;
    return context;
  }
  void append(const LogMsg& msg);
  void flush();
};

void qFlushOutput() { MessageLog::instance().flush(); }

QThreadStorage<QString>& qMessageContext() { return MessageLog::context(); }

void qColorMessageOutput(QtMsgType type, const QMessageLogContext& ctx,
                         const QString& msg) {

  const auto& perThreadContext = MessageLog::context();
  QString threadContext;
  if (perThreadContext.hasLocalData())
    threadContext = perThreadContext.localData();

  MessageLog::instance().append(LogMsg{threadContext, type, msg, ctx.version,
                               ctx.line, ctx.file, ctx.function, ctx.category});

  // we can crash the app to help locate a log message!
  static const char* debugTrigger = nullptr;
  static bool triggerCheck = false;
  if (!triggerCheck) {
    triggerCheck = true;
    static QMutex mutex;
    QMutexLocker locker(&mutex);

    const char* env = getenv("DEBUG_TRIGGER");
    if (env) {
      debugTrigger = env;
      qFlushOutput();
      fprintf(stdout, "\n\n[X] debug trigger registered: \"%s\"\n\n", debugTrigger);
      fflush(stdout);
    }
  }

  if (debugTrigger) {
    if (msg.contains(debugTrigger)) {
      qFlushOutput();
      fprintf(stdout, "\n\n[X][%s:%d] debug trigger matched: <<%s>>\n\n", ctx.file,
              ctx.line, qPrintable(msg));
      fflush(stdout);
      char* ptr = nullptr;
      *ptr = 0;
    }
  }
}

MessageContext::MessageContext(const QString& context) {
  auto& threadContext = MessageLog::context();
  if (threadContext.hasLocalData() && !threadContext.localData().isEmpty())
    qWarning() << "overwriting message context"; // todo: save/restore message context
  threadContext.setLocalData(context);
}

MessageContext::~MessageContext() { MessageLog::context().setLocalData(QString()); }

MessageLog::MessageLog() {
  std::set_terminate(qFlushOutput);
  Exiv2::LogMsg::setHandler(exifLogHandler);

#ifdef Q_OS_WIN32
  auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (GetConsoleMode(handle, &mode)) { // windows terminal, but not msys/mingw/cygwin
    printf("win32 console detected mode=0x%x\n", (int)mode);
    _isTerm = true;
    _termColors = mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (getenv("CBIRD_COLOR_CONSOLE")) _termColors = true;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(handle, &csbi))
      _termColumns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    //int rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  }
  else if (getenv("TERM") && GetFileType(handle) == FILE_TYPE_PIPE) { // maybe msys/mingw...
    struct {
      DWORD len;
      WCHAR name[MAX_PATH];
    } info;
    memset(&info, 0, sizeof(info));
    if (GetFileInformationByHandleEx(handle, FileNameInfo, &info, sizeof(info))) {
      // req windows > 7 it seems, though it will run on 7,  no result
      QString name=QString::fromWCharArray((wchar_t*)info.name, info.len);
      printf("stdio pipe=%d %s\n", (int)info.len, qUtf8Printable(name));
      _isTerm = _termColors = (name.contains("msys-") && name.contains("-pty"));
    }
  }
#else
  _isTerm = isatty(fileno(stdout));
  if (_isTerm) {
    _termColors = true; // assume we have color on unix-like systems
    // https://stackoverflow.com/questions/1022957/getting-terminal-width-in-c
    const char* termEnv = getenv("TERM");
    if (termEnv) {
      char termBuf[2048];
      int err = tgetent(termBuf, termEnv);
      if (err <= 0)
        printf("unknown terminal TERM=%s TERMCAP=%s err=%d, cannot guess config\n",
               termEnv, getenv("TERMCAP"), err);
      else {
        int termLines = tgetnum("li");
        _termColumns = tgetnum("co");
        if (termLines < 0 || _termColumns < 0)
          printf("unknown terminal TERM=%s %dx%d, cannot guess config\n",
                 termEnv, _termColumns, termLines);
      }
    }
  }
#endif

  // overrides
  if (getenv("CBIRD_FORCE_COLORS")) _termColors = 1;

  QString tc = getenv("CBIRD_CONSOLE_WIDTH");
  if (!tc.isEmpty()) _termColumns = tc.toInt();

  if (_isTerm)
    printf("term width=%d colors=%d\n", _termColumns, _termColors);

  _homePath = QDir::homePath();

#ifdef Q_OS_WIN
  // disable text mode to speed up console
  _setmode( _fileno(stdout), _O_BINARY );
#endif

  _thread = QThread::create([this]() {
    _sync = false;
    QString lastInput, lastOutput;
    int repeats = 0;
    QMutexLocker locker(&_mutex);
    while (!_stop && _logCond.wait(&_mutex)) {
      if (_sync && _log.count() <= 0) {
        _sync = false;
        _syncCond.wakeAll();
      }
      while (_log.count() > 0) {
        const LogMsg msg = _log.takeFirst();
        int pl = msg.msg.indexOf("<PL>");  // do not compress progress lines
        if (pl <= 0 && lastInput == msg.msg) {
          repeats++;
          continue;
        }
        locker.unlock();

        const QString line = format(msg);

        if (repeats > 0) {
          QString output = lastInput + " [x" + QString::number(repeats) + "]\n";
          QByteArray utf8 = output.toUtf8();
          fwrite(utf8.data(), utf8.length(), 1, stdout);
          repeats = 0;
        }

        lastInput = line;

        QString output = line;
        output.replace(_homePath, "~");

        pl = output.indexOf("<PL>");  // must come after replacements!
        output.replace("<PL>", "");

        // elide and pad following text to terminal width
        int elide = output.indexOf("<EL>");  // must come after <PL>!
        if (elide > 0) {
          if (_termColumns > 0) {
            QString toElide = output.mid(elide + 4);
            QString elided = qElide(toElide, _termColumns - elide);
            output = output.midRef(0, elide) + elided;
            output += QString().fill(' ', _termColumns - output.length());
          } else
            output.replace("<EL>", "");
        }

        if (pl > 0) {
          if (lastInput.startsWith(line.midRef(0, pl))) output = "\r" + output;
        } else {
          if (!lastOutput.endsWith("\n")) output = "\n" + output;
          output += "\n";
        }

        lastOutput = output;

        QByteArray utf8 = output.toUtf8();

        fwrite(utf8.data(), utf8.length(), 1, stdout);
        // we only need to flush if \r is present;
        // however, windows must always flush
#ifndef Q_OS_WIN
        if (pl > 0)
#endif
          fflush(stdout);

        locker.relock();
      }  // log.count() > 0
    }    // !stop
  });
  // wait for thread to start or we have a race with destructor!
  _sync = true;
  _thread->start();
  while (_sync) QThread::msleep(10);
}

MessageLog::~MessageLog() {
  _stop = true;
  _sync = false;
  _logCond.wakeOne();
  _thread->wait();
  flush();
}

QString MessageLog::format(const LogMsg& msg) const {
  static const char* lastColor = nullptr;
  static uint64_t lastTime = nanoTime();
  static const bool showTimestamp = getenv("CBIRD_LOG_TIMESTAMP");

  char typeCode = 'X';
  const char* color = VT_WHT;
  const char* reset = VT_RESET;

  switch (msg.type) {
    case QtDebugMsg:
      typeCode = 'D';
      color = VT_WHT;
      break;
    case QtInfoMsg:
      typeCode = 'I';
      color = VT_GRN;
      break;
    case QtWarningMsg:
      typeCode = 'W';
      color = VT_YEL;
      break;
    case QtCriticalMsg:
      typeCode = 'C';
      color = (VT_BRIGHT VT_RED);
      break;
    case QtFatalMsg:
      typeCode = 'F';
      color = (VT_UNDERL VT_BRIGHT VT_RED);
      break;
  }

  if (msg.msg.contains("<PL>"))  // progress line
    color = VT_CYN;

  QStringList filteredClasses = {};

  QString shortFunction = msg.function;

  if (shortFunction.contains("::<lambda"))
    shortFunction = shortFunction.split("::<lambda").front();

  // int bar(...)
  // int Foo::bar(...)
  // int Foo::bar<float>(...)
  shortFunction = shortFunction.split("(").first();

  QStringList parts = shortFunction.split("::");

  if (parts.length() > 1) {
    // (1) cv::Mat foo
    // (2) void class::foo
    // (3) cv::Mat class::foo
    // (4) cv::Mat class<bar>::foo
    shortFunction = parts.back().trimmed();
    parts.pop_back();
    parts = parts.join("").split(" ");
    if (parts.length() > 1) {  // case (1) ignored
      QString className = parts.back();
      shortFunction = className + "::" + shortFunction;
      if (filteredClasses.contains(className)) return "";
    }
  } else {
    shortFunction = shortFunction.split(" ").back();  // drop return type
  }

  if (!msg.threadContext.isNull()) shortFunction += "{" + msg.threadContext + "}";

  if (_termColors && lastColor != color) {
    fprintf(stdout, "%s%s", reset, color);
    lastColor = color;
  }

  QString logLine;
  if (msg.msg.startsWith("<NC>"))  // no context
    logLine += msg.msg.mid(4);
  else {
    if (showTimestamp) {
      auto currTime = nanoTime();
      logLine += QString::asprintf("%06d ", int( (currTime-lastTime)/1000 ));
      lastTime = currTime;
    }
    logLine += QString("[%1][%2] %3").arg(typeCode).arg(shortFunction).arg(msg.msg);
  }
  return logLine;
}

void MessageLog::append(const LogMsg& msg) {
  // if fatal, output immediately. Since we are going to abort() next,
  // we cannot rely on qFlushOutput to get called on exit
  if (msg.type == QtFatalMsg) {
    qFlushOutput();
    fprintf(stdout, "\n%s\n\n", qUtf8Printable(format(msg)));
    fflush(stdout);
  } else {
    QMutexLocker locker(&_mutex);
    _log.append(msg);
  }
  _logCond.wakeAll();
}

void MessageLog::flush() {
  // prefer thread to write the logs since it handles things
  if (_thread->isRunning()) {
    QMutexLocker locker(&_mutex);
    _sync = true;
    while (_sync) {
      _syncCond.wait(&_mutex, 10);
      _logCond.wakeAll();
    }
  }
  else {
    // no thread, ensure all logs are written
    QMutexLocker locker(&_mutex);
    QByteArray utf8;
    if (_log.count() <= 0)
      utf8 = "\n";
    else
      while (_log.count() > 0)
        utf8 += (format(_log.takeFirst()) + "\n").toUtf8();

    fwrite(utf8.data(), utf8.length(), 1, stdout);
  }
  if (_termColors) fwrite(VT_RESET, strlen(VT_RESET), 1, stdout);
  fflush(stdout);
  fflush(stderr);
}
