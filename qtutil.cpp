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
#include "gui/theme.h" // todo: I don't like this dependency

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

      const QString disableAppProgs = getenv("CBIRD_NO_BUNDLED_PROGS");

      if (disableAppProgs.isEmpty()) {
        bool portable = false; // portable binaries
#ifdef CBIRD_PORTABLE_BINARY
        portable=true;
#endif
        bool appImage = getenv("APPDIR") != nullptr;
        bool setEnv = false;
        QString binDir, libDir;
        if (portable) {
          setEnv = true;
          QString appDir =qApp->applicationDirPath();
          binDir = appDir + "/";
          libDir = appDir + "/";
          qDebug() << "portable path=" << appDir;
        }
        else if (appImage) {
          setEnv = true;
          QString appDir = getenv("APPDIR");
          binDir = appDir + "/cbird/bin/";
          libDir = appDir + "/cbird/lib/";
        }

        const QString appProg = binDir + prog;
        qDebug() << appProg << QFileInfo(appProg).exists();
        if (setEnv && QFileInfo(appProg).exists()) {
          qInfo() << "using" << appProg << "for" << prog;
          qInfo() << "to disable this, set CBIRD_NO_BUNDLED_PROGS";

          // put the bundled apps before everything else
          auto env = QProcessEnvironment::systemEnvironment();
          const QString binPath = env.value("PATH");
          const QString libPath = env.value("LD_LIBRARY_PATH"); // might be empty, should be fine
          env.insert("PATH", binDir + ":" + binPath);
          env.insert("LD_LIBRARY_PATH", libDir + ":" + libPath);
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
                                  const QString& settingsKey,
                                  const QString& dialogTitle,
                                  const QString& dialogText) {
  if (options.count() == 1 && args.empty()) {
    args = options.at(0).mid(1);
    putSetting(settingsKey, args);
  }

  if (args.empty() || (qApp->keyboardModifiers() & Qt::ControlModifier)) {
    QStringList optionLabels;
    for (auto& option : qAsConst(options))
      optionLabels += option.first();

    QWidget* parent = qApp->widgetAt(QCursor::pos());

    QString program = optionLabels.at(0);
    {
      QInputDialog dialog(parent);
      int result = Theme::instance().execInputDialog(
          &dialog, dialogTitle,
          QString(dialogText) + "\n\n" +
              "To change this setting, press the Control "
              "key while selecting the action.",
          program, optionLabels);
      if (result != QInputDialog::Accepted)
        return false;
      program = dialog.textValue();
    }

    for (auto& option : qAsConst(options))
      if (option.first() == program) {
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

#if defined(Q_OS_WIN)
  fileManagers = {{"Explorer", "explorer", "/select,\"%1\""}};
#elif defined(Q_OS_MACOS)
  fileManagers = {{"Finder", "open", "-R", "%1"}};
#else
  fileManagers = {
      {"Desktop Default", "DesktopServices"},
      {"Dolphin (KDE)", "dolphin", "--select", "%1"},
      {"Gwenview", "gwenview", "%dirname(1)"},
      {"Krusader (Right Panel)", "DBus", "org.krusader",
       "/Instances/krusader[0-9]*/right_manager", "", "newTab", "%dirname(1)",
       "&&", "org.krusader", "/MainWindow_[0-9]*", "", "raise"},
      {"Krusader (Left Panel)", "DBus", "org.krusader",
       "/Instances/krusader[0-9]*/left_manager", "", "newTab", "%dirname(1)",
       "&&", "org.krusader", "/MainWindow_[0-9]*", "", "raise"},
      {"Nautilus (GNOME)", "nautilus", "-s", "%1"},
      {"PCMan (LXDE)", "pcmanfm", "%dirname(1)"},
      {"thunar (Xfce)", "thunar", "%dirname(1)"},
      {"gThumb (GNOME)", "gthumb", "%dirname(1)"}
  };
#endif
  const QString settingsKey = qq("OpenFileLocation");
  QStringList args = getSetting(settingsKey, {}).toStringList();

  if (!chooseProgram(
          args, fileManagers, settingsKey, qq("Choose File Manager"),
          qq("Please choose the program for viewing a file's location.")))
    return;

  // QDesktopServices cannot reveal file location and select it, we need the dir
  QString tmp = path;
  if (args.count() > 0 && args.first() == ll("DesktopServices"))
    tmp = QFileInfo(path).absoluteDir().path();

  runProgram(args, false, tmp);
}

void DesktopHelper::openVideo(const QString& path, double seekSeconds) {
  QStringList args;
  if (abs(seekSeconds) >= 0.1) {
    const QString settingsKey = qq("OpenVideoSeek");
    const QStringList defaultArgs = QStringList{{"DesktopServices"}};
    args = getSetting(settingsKey, defaultArgs).toStringList();

    QVector<QStringList> openVideoSeek;
#ifdef Q_OS_WIN
    openVideoSeek = {
        {"Desktop Default (No Timestamp)", "DesktopServices"},
        {"VLC", "\"C:/Program Files (x86)/VideoLan/VLC/vlc.exe\"", "--start-time=%seek", "\"%1\""},
        {"FFplay", "ffplay.exe", "-ss", "%seek", "\"%1\""},
        {"MPlayer", "mplayer.exe", "-ss", "%seek", "\"%1\""},
        {"MPV", "mpv.exe", "--start=%seek", "\"%1\""}};
#else
    openVideoSeek = {
        {"Desktop Default (No Timestamp)", "DesktopServices"},
        {"Celluloid", "celluloid", "--mpv-options=--start=%seek", "%1"},
        {"FFplay", "ffplay", "-ss", "%seek", "%1"},
        {"MPlayer", "mplayer", "-ss", "%seek", "%1"},
        {"MPV", "mpv", "--start=%seek", "%1"},
        {"SMPlayer", "smplayer", "-start", "%seek(int)", "%1"},
        {"VLC", "vlc", "--start-time=%seek", "%1"}};
#endif
    if (!chooseProgram(args, openVideoSeek, settingsKey, qq("Choose Video Player"),
                       qq("Select the program for viewing video at a timestamp")))
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
  key = key.replace(QRegularExpression("[^a-z0-9-\\+\\%]+"), "_");

  if (!settings.contains(key)) settings.setValue(key, defaultShortcut);
  return settings.value(key, defaultShortcut).toString();
}

#ifndef Q_OS_WIN

void WidgetHelper::setWindowCloak(QWidget* window, bool enable) {
  (void)window; (void)enable;
}

void WidgetHelper::setWindowTheme(QWidget* window, bool dark) {
  (void)window;
  (void)dark;
}

void WidgetHelper::hackShowWindow(QWidget* window, bool maximized) {
  if (maximized)
    window->showMaximized();
  else
    window->show();
}

#else
#include <dwmapi.h>

enum PreferredAppMode {
  Default,
  AllowDark,
  ForceDark,
  ForceLight,
  Max
};

enum WINDOWCOMPOSITIONATTRIB {
  WCA_UNDEFINED = 0,
  WCA_NCRENDERING_ENABLED = 1,
  WCA_NCRENDERING_POLICY = 2,
  WCA_TRANSITIONS_FORCEDISABLED = 3,
  WCA_ALLOW_NCPAINT = 4,
  WCA_CAPTION_BUTTON_BOUNDS = 5,
  WCA_NONCLIENT_RTL_LAYOUT = 6,
  WCA_FORCE_ICONIC_REPRESENTATION = 7,
  WCA_EXTENDED_FRAME_BOUNDS = 8,
  WCA_HAS_ICONIC_BITMAP = 9,
  WCA_THEME_ATTRIBUTES = 10,
  WCA_NCRENDERING_EXILED = 11,
  WCA_NCADORNMENTINFO = 12,
  WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
  WCA_VIDEO_OVERLAY_ACTIVE = 14,
  WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
  WCA_DISALLOW_PEEK = 16,
  WCA_CLOAK = 17,
  WCA_CLOAKED = 18,
  WCA_ACCENT_POLICY = 19,
  WCA_FREEZE_REPRESENTATION = 20,
  WCA_EVER_UNCLOAKED = 21,
  WCA_VISUAL_OWNER = 22,
  WCA_HOLOGRAPHIC = 23,
  WCA_EXCLUDED_FROM_DDA = 24,
  WCA_PASSIVEUPDATEMODE = 25,
  WCA_USEDARKMODECOLORS = 26,
  WCA_LAST = 27
};

struct WINDOWCOMPOSITIONATTRIBDATA {
  WINDOWCOMPOSITIONATTRIB Attrib;
  PVOID pvData;
  SIZE_T cbData;
};

using AllowDarkModeForWindowFunc =  BOOL (WINAPI *)(HWND hWnd, BOOL allow);
using SetPreferredAppModeFunc = PreferredAppMode (WINAPI *)(PreferredAppMode appMode);
using SetWindowCompositionAttributeFunc =  BOOL (WINAPI *)(HWND hwnd, WINDOWCOMPOSITIONATTRIBDATA *);

void WidgetHelper::setWindowTheme(QWidget* window, bool dark) {
  if (!dark) return;

  qDebug() << "Enabling Win32 Dark Mode";

  auto uxThemeLib = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

  auto AllowDarkModeForWindow = (AllowDarkModeForWindowFunc)
      (void*)GetProcAddress(uxThemeLib, MAKEINTRESOURCEA(133));

  auto SetPreferredAppMode = (SetPreferredAppModeFunc)
      (void*)GetProcAddress(uxThemeLib, MAKEINTRESOURCEA(135));

  auto user32Lib = GetModuleHandleW(L"user32.dll");
  auto SetWindowCompositionAttribute = (SetWindowCompositionAttributeFunc)
      (void*)GetProcAddress(user32Lib, "SetWindowCompositionAttribute");

  SetPreferredAppMode(AllowDark);

  HWND hwnd = (HWND)window->winId();
  BOOL enable = true;
  AllowDarkModeForWindow(hwnd, enable);

  WINDOWCOMPOSITIONATTRIBDATA data = {
      WCA_USEDARKMODECOLORS,
      &enable,
      sizeof(enable)
  };
  SetWindowCompositionAttribute(hwnd, &data);
}

void WidgetHelper::hackShowWindow(QWidget* window, bool maximized) {
  // cloak and then uncloak to hide the white window flash
  // caveat: no fade-in animation
  setWindowCloak(window, true);

  if (maximized)
    window->showMaximized();
  else
    window->show();

  qApp->processEvents(); // paint the initial background

  setWindowCloak(window, false);
}

void WidgetHelper::setWindowCloak(QWidget* window, bool enable) {
  BOOL cloak = enable;
  auto hwnd = (HWND)window->winId();
  DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
}

#endif

void WidgetHelper::saveGeometry(const QWidget *w, const char *id) {
  if (!id) id = w->metaObject()->className();
  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(id);
  settings.setValue("geometry", w->saveGeometry());
  settings.setValue("maximized", w->isMaximized());
}

bool WidgetHelper::restoreGeometry(QWidget *w, const char *id) {
  if (!id) id = w->metaObject()->className();
  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(id);
  if (!w->restoreGeometry(settings.value("geometry").toByteArray()))
    w->setGeometry(100, 100, 1024, 768);

  return settings.value("maximized").toBool();
}

void WidgetHelper::saveTableState(const QTableView* w, const char* id) {
  auto* model = w->model();
  Q_ASSERT(model);

  if (!id) id = w->metaObject()->className();
  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(id);

  QStringList colWidths;
  for (int i = 0; i < model->columnCount(); ++i)
    colWidths.append(QString::number(w->columnWidth(i)));
  settings.setValue("columnWidths", colWidths);
}

bool WidgetHelper::restoreTableState(QTableView* w, const char* id) {
  Q_ASSERT(w->model());
  if (!id) id = w->metaObject()->className();
  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(id);

  const QStringList colWidths = settings.value("columnWidths").toStringList();
  settings.endGroup();

  if (colWidths.count() <= 0)
    return false;

  for (int i = 0; i < colWidths.count(); i++)
    w->setColumnWidth(i, colWidths[i].toInt());

  return true;
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

QDateTime DBHelper::lastModified(const QSqlDatabase &db) {
  // this only works with local file database drivers, like sqlite
  QString dbPath = db.databaseName();
  Q_ASSERT( !dbPath.isEmpty() );

  QFileInfo dbInfo(dbPath);
  if (!dbInfo.exists()) return QDateTime::fromSecsSinceEpoch(INT64_MAX);

  return dbInfo.lastModified();
}

QMenu *MenuHelper::dirMenu(const QString &root, QWidget *target, const char *slot, int maxDepth) {
  QMenu* menu = makeDirMenu(root, target, slot, maxDepth, 0);
  if (!menu) menu = new QMenu;

  QAction* action = new QAction("Choose Folder...", menu);
  action->connect(action, SIGNAL(triggered(bool)), target, slot);
  action->setData(";newfolder;");

  menu->insertSeparator(menu->actions().at(0));
  menu->insertAction(menu->actions().at(0), action);

  return menu;
}

QMenu *MenuHelper::makeDirMenu(const QString &root, QWidget* target, const char *slot,
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
    int half = (maxLen-3) / 2;
    tmp = str.mid(0, half) + "..." + str.mid(str.length() - half);
  } else
    tmp = str;

  return tmp;
}

double qRotationAngle(const QTransform& mat) {
  QPointF p0 = mat.map(QPointF(0, 0));
  QPointF p1 = mat.map(QPointF(1, 0));
  return 180.0 / M_PI * atan((p1.y() - p0.y()) / (p1.x() - p0.x()));
}

// bad form, but only way to get to metacallevent
// this header won't be found unless foo is added to qmake file
#if 0
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
#endif

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
//    * qFlushMessageLog() to sync up when needed
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

#include "exiv2/error.hpp" // capture exif library logs

static void exifLogHandler(int level, const char* msg) {
  const int nLevels = 4;
  static constexpr QtMsgType levelToType[nLevels] = {
    QtDebugMsg, QtInfoMsg, QtWarningMsg, QtCriticalMsg
  };
  static constexpr QMessageLogContext context("", 0, "exif()", "");
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

/// terminal color/format codes
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

/// Private logging class/singleton
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
  const bool   _showTimestamp = getenv("CBIRD_LOG_TIMESTAMP");
  mutable const char* _lastColor = nullptr; // format()
  mutable int64_t _lastTime = nanoTime();   // format()
  mutable QString _formatStr;               // format()

  MessageLog();
  ~MessageLog();

  void outputThread();
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

void qFlushMessageLog() { MessageLog::instance().flush(); }

QThreadStorage<QString>& qMessageContext() { return MessageLog::context(); }

void qColorMessageOutput(QtMsgType type, const QMessageLogContext& ctx,
                         const QString& msg) {

  const auto& perThreadContext = MessageLog::context();
  QString threadContext;
  if (perThreadContext.hasLocalData())
    threadContext = perThreadContext.localData();

  MessageLog::instance().append(LogMsg{threadContext, type, msg, ctx.version,
                                       ctx.line, ctx.file, ctx.function, ctx.category});

#ifdef DEBUG
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
      qFlushMessageLog();
      fprintf(stdout, "\n\n[X] debug trigger registered: \"%s\"\n\n", debugTrigger);
      fflush(stdout);
    }
  }

  if (debugTrigger) {
    if (msg.contains(debugTrigger)) {
      qFlushMessageLog();
      fprintf(stdout, "\n\n[X][%s:%d] debug trigger matched: <<%s>>\n\n", ctx.file,
              ctx.line, qPrintable(msg));
      fflush(stdout);
      char* ptr = nullptr;
      *ptr = 0;
    }
  }
#endif
}

MessageContext::MessageContext(const QString& context) {
  auto& threadContext = MessageLog::context();
  if (threadContext.hasLocalData() && !threadContext.localData().isEmpty())
    qWarning() << "overwriting message context"; // todo: save/restore message context
  threadContext.setLocalData(context);
}

MessageContext::~MessageContext() { MessageLog::context().setLocalData(QString()); }

MessageLog::MessageLog() {
  std::set_terminate(qFlushMessageLog);
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

  // detection is buggy, provide overrides
  if (getenv("CBIRD_FORCE_COLORS")) _termColors = 1;

  QString tc = getenv("CBIRD_CONSOLE_WIDTH");
  if (!tc.isEmpty()) _termColumns = tc.toInt();

#ifdef DEBUG
  if (_isTerm)
    printf("term width=%d colors=%d\n", _termColumns, _termColors);
#endif

  _homePath = QDir::homePath();

#ifdef Q_OS_WIN
  // disable text mode to speed up console
  _setmode( _fileno(stdout), _O_BINARY );
#endif
  _thread = QThread::create(&MessageLog::outputThread, this);

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

void MessageLog::outputThread() {
  _sync = false;

  static constexpr QChar charCR('\r'), charLF('\n'), charSpace(' ');
  static constexpr QLatin1String tokenProgress("<PL>"), tokenElide("<EL>");

  QString lastInput, lastOutput, lastProgressLine;
  int numRepeats = 0;
  QMutexLocker locker(&_mutex);
  while (!_stop && _logCond.wait(&_mutex)) {
    if (_sync && _log.count() <= 0) {
      _sync = false;
      _syncCond.wakeAll();
    }
    while (_log.count() > 0) {
      const LogMsg msg = _log.takeFirst();

      // compress repeats while we hold the lock
      int pl = msg.msg.indexOf(tokenProgress);    // do not compress progress lines
      if (pl <= 0 && lastInput == msg.msg) {
        numRepeats++;
        continue;
      }
      locker.unlock();

      // repeats end, print one more with the total count
      if (numRepeats > 0) {
        QString output = lastInput;
        output += QLatin1String(" [x");
        output += QString::number(numRepeats);
        output += QLatin1String("]\n");

        const QByteArray utf8 = output.toUtf8();
        fwrite(utf8.data(), utf8.length(), 1, stdout);
        numRepeats = 0;
      }

      const QString formatted = format(msg);
      if (formatted.isEmpty()) { // possible with filters
        locker.relock();
        continue;
      }
      lastInput.resize(0);
      lastInput += formatted;
      QString output = lastInput;

      // special progress line, everything before the <PL> must be static
      pl = output.indexOf(tokenProgress);
      if (pl > 0)
        output.remove(pl, tokenProgress.size());

      // special elide indicator, everything after is elided to terminal width
      // note: must come after <PL> since prefix of <PL> must be static
      int elide = output.indexOf(tokenElide);
      if (elide > 0 && elide > pl) {
        if (_termColumns > 0) {
          auto toElide = output.mid(elide + tokenElide.size());
          auto elided = qElide(toElide, _termColumns - elide);
          output = output.mid(0, elide) + elided;
          output += QString().fill(charSpace, _termColumns - output.length());
        } else
          output.remove(elide, tokenElide.size());
      }

      // find chars to append/prepend
      const QChar* prepend = nullptr, *append = nullptr;

      if (pl > 0 && _isTerm) {
        auto prefix = QStringView(output).mid(0, pl);
        if (lastProgressLine.startsWith(prefix)) prepend = &charCR;
        else if (!lastOutput.endsWith(charLF))   prepend = &charLF;

        lastProgressLine = output;

      } else {
        if (!lastOutput.endsWith(charLF)) prepend = &charLF;
        append = &charLF;
        lastProgressLine.clear();
      }

      lastOutput.resize(0); // next appends are allocation free
      if (lastOutput.capacity() > 1024) lastOutput.squeeze(); // don't take too much

      if (prepend) lastOutput += *prepend;
      lastOutput += output;
      if (append) lastOutput += *append;

      const QByteArray utf8 = lastOutput.toUtf8();
      fwrite(utf8.data(), utf8.length(), 1, stdout);

      // we only need to flush if CR is present;
      // however, windows must always flush
#ifndef Q_OS_WIN
      if (!append)
#endif
        fflush(stdout);

      locker.relock();
    } // _log.count() > 0
  } // !_stop
}

QString MessageLog::format(const LogMsg& msg) const {

  struct MessageFormat {
    QLatin1String label;
    const char* color;
  };

  // table index is QtDebugMsg ... QtInfoMsg
  static constexpr MessageFormat formats[QtInfoMsg + 1] = {
      {QLatin1String("D"), VT_WHT},
      {QLatin1String("W"), VT_YEL},
      {QLatin1String("C"), (VT_BRIGHT VT_RED)},
      {QLatin1String("F"), (VT_UNDERL VT_BRIGHT VT_RED)},
      {QLatin1String("I"), VT_GRN}};

  // table could become invalid, enum is unnumbered in qlogging.h
  static_assert(QtDebugMsg == 0 && QtInfoMsg == 4);
  if (msg.type < 0 || msg.type > 4) {
    fprintf(stderr, "unexpected qt logging type\n");
    return msg.msg;
  }

  const auto& fmt = formats[msg.type];

  // don't change colors unless we have to (maybe faster)
  if (_termColors && _lastColor != fmt.color) {
    fprintf(stdout, "%s%s", VT_RESET, fmt.color);
    _lastColor = fmt.color;
  }

  // shortened function name provides sufficient context
  QString shortFunction = QString::fromLatin1(msg.function);

  // drop lambda, we want the scope where it was defined
  {
    int i = shortFunction.indexOf(QLatin1String("::<lambda"));
    if (i >= 0) shortFunction.resize(i);
  }

  // drop arguments
  // int bar(...)
  // int Foo::bar(...)
  // int Foo::bar<float>(...)
  // std::function<void foo(int)>  Foo::bar(...) fixme...
  {
    int i = shortFunction.indexOf(QLatin1Char('('));
    if (i >= 0) shortFunction.resize(i);
  }

  // drop return type
  // (1) cv::Mat foo
  // (2) void class::foo
  // (3) cv::Mat class::foo
  // (4) cv::Mat class<bar>::foo
  // (5) void foo
  {
    int i = shortFunction.lastIndexOf(QLatin1Char(' '));
    if (i >= 0) shortFunction.remove(0, i + 1);

    //    static const QSet<QString> filteredClasses{"MediaGroupListWidget"};
    //    i = shortFunction.indexOf("::");
    //    if (i >= 0) {
    //      auto className = shortFunction.mid(0, i);
    //      //fprintf(stderr, "\n%s\n", qPrintable(className));
    //      if (filteredClasses.contains(className)) return QString();
    //    }
  }

  _formatStr.resize(0); // no more allocs after a few calls

  if (_formatStr.capacity() > 1024) // don't take too much
    _formatStr.squeeze();

  if (_showTimestamp) {
    auto currTime = nanoTime();
    int micros = (currTime - _lastTime) / 1000;
    _formatStr += QString::asprintf("%06d ", micros);
    _lastTime = currTime;
  }

  if (msg.msg.startsWith(QLatin1String("<NC>")))  // no context
    _formatStr += QStringView(msg.msg).mid(4);
  else {
    _formatStr += QLatin1Char('[');
    _formatStr += fmt.label;
    _formatStr += QLatin1String("][");

    _formatStr += shortFunction;
    if (!msg.threadContext.isNull()) {
      _formatStr += QLatin1Char('{');
      _formatStr += msg.threadContext;
      _formatStr += QLatin1Char('}');
    }
    _formatStr += QLatin1String("] ");
    _formatStr += msg.msg;
  }

  _formatStr.replace(_homePath, QLatin1String("~"));

  return _formatStr;
}

void MessageLog::append(const LogMsg& msg) {
  if (msg.type != QtFatalMsg) {
    QMutexLocker locker(&_mutex);
    _log.append(msg);
    _logCond.wakeAll();
  }
  else {
    // if fatal, flush logger, since abort() comes next
    qFlushMessageLog();
    fprintf(stdout, "\n%s\n\n", qUtf8Printable(format(msg)));
    fflush(stdout);
  }
}

void MessageLog::flush() {
  QMutexLocker locker(&_mutex);

  // prefer thread to write the logs since it handles things
  if (_thread && _thread->isRunning()) {
    _sync = true;
    while (_sync) {
      _syncCond.wait(&_mutex, 10);
      _logCond.wakeAll();
    }
  }
  else {
    // no thread, ensure all logs are written
    QByteArray utf8("\n");

    while (_log.count() > 0) {
      utf8 += format(_log.takeFirst()).toUtf8();
      utf8 += "\n";
    }
    fwrite(utf8.constData(), utf8.length(), 1, stdout);
  }
  if (_termColors) fwrite(VT_RESET, strlen(VT_RESET), 1, stdout);
  fflush(stdout);
  fflush(stderr);
}

#if QT_VERSION_MAJOR > 5
QPartialOrdering qVariantCompare(const QVariant& a, const QVariant& b) {
  QPartialOrdering ord = QVariant::compare(a, b);
  if (ord == QPartialOrdering::Unordered)
    qWarning() << a << "and" << b << "are not comparable";
  return ord;
}
#endif

ShadeWidget::ShadeWidget(QWidget *parent) : QLabel(parent) {
  setGeometry({0,0,parent->width(),parent->height()});
  setMargin(0);
  setFrameShape(QFrame::NoFrame);
  setStyleSheet(R"qss(
        QLabel {
          background-color:rgba(0,0,0,128);
        })qss");
  show();
}

int qNumericSubstringCompare(const QCollator& cmp, const QStringView& a, const QStringView& b) {
  // fixme: this can be sped up massively by eliminating the regexps
  static const QRegularExpression numberStartExp(qq("[0-9]"));

  // non-digit not preceded by digit or .
  static const QRegularExpression numberEndExp(qq("(?![0-9\\.])[^0-9]"));

  // current index into string
  int ia = a.isEmpty() ? -1 : 0;
  int ib = b.isEmpty() ? -1 : 0;

  auto compareNumbers = [&a, &b, &ia, &ib, &cmp](const int na, const int nb) {
    ia = a.indexOf(numberEndExp, ia);
    ib = b.indexOf(numberEndExp, ib);
    auto pa = a.mid(na, ia > 0 ? (ia - na) : -1);
    auto pb = b.mid(nb, ib > 0 ? (ib - nb) : -1);
    return cmp.compare(pa, pb);
  };

  while (true) {
    if (ia < 0 && ib >= 0) return -1;  // empty < something
    if (ia >= 0 && ib < 0) return 1;   // something > empty
    if (ia < 0 && ib < 0) return 0;    // empty == empty
    // both >= 0; keep looping

    const int na = a.indexOf(numberStartExp, ia);
    if (na == ia) {
      const int nb = b.indexOf(numberStartExp, ib);
      if (nb == ib) {  // numeric in both
        int r = compareNumbers(na, nb);
        if (r != 0) return r;
        continue;
      } else {
        // a is numeric, b is not, a < b
        return -1;
      }
    }
    const int nb = b.indexOf(numberStartExp, ib);
    if (nb == ib) {
      const int na = a.indexOf(numberStartExp, ia);
      if (na == ia) {  // same block as above
        int r = compareNumbers(na, nb);
        if (r != 0) return r;
        continue;
      } else
        return 1;  // b is num, a is not, a > b
    }

    // if there is a number somewhere,
    // and prefixes have the same length, compare prefixes
    if (na > 0 && nb > 0 && na == nb) {
      auto pa = a.mid(ia, na - ia);
      auto pb = b.mid(ib, nb - ib);
      int r = cmp.compare(pa, pb);
      if (r != 0) return r;
      ia = na;
      ib = nb;
      continue;
    } else {
      // no number or unequal prefixes
      return cmp.compare(a.mid(ia), b.mid(ib));
    }

    Q_UNREACHABLE();  // every case either continues or returns
  }
}
