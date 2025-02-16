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

#include "gui/theme.h"  // TODO: I don't like this dependency
#include "profile.h"

#ifdef Q_OS_WIN
#include <windows.h> // ShQueryRecycleBin
#endif

// qttools/src/qdbus/qdbus/qdbus.cpp
#ifndef Q_OS_WIN
#  include <QtDBus/QtDBus>
#  include <QtXml/QtXml>

static QStringList listServiceObjects(QDBusConnection& connection, const QString& service,
                                      const QString& path) {
  QStringList objectPaths;

  // make a low-level call, to avoid introspecting the Introspectable interface
  QDBusMessage call = QDBusMessage::createMethodCall(
      service, path.isEmpty() ? QLatin1String("/") : path,
      QLatin1String("org.freedesktop.DBus.Introspectable"), QLatin1String("Introspect"));
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
      QString sub = path + QLatin1Char('/') + child.attribute(QLatin1String("name"));
      // printf("%s\n", qPrintable(sub));
      objectPaths.append(sub);
      objectPaths.append(listServiceObjects(connection, service, sub));
    }
    child = child.nextSiblingElement();
  }
  return objectPaths;
}

static bool callServiceMethod(const QStringList& args) {
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
    return false;
  }

  QDBusInterface remoteApp(service, object, interface, QDBusConnection::sessionBus());
  if (remoteApp.isValid()) {
    QDBusReply<void> reply;
    reply = remoteApp.callWithArgumentList(QDBus::Block, method, methodArgs);
    if (!reply.isValid()) {
      qWarning() << "DBus Error:" << reply.error();
      return false;
    }
    return true;
  }

  qWarning() << "DBus failed to connect:" << remoteApp.lastError();
  return false;
}

#endif  // !Q_OS_WIN

bool DesktopHelper::runProgram(QStringList& args, bool wait, const QString& inPath, double seek,
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
      if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        qWarning() << "QDesktopService::openUrl failed for" << path;
        return false;
      }
#ifndef Q_OS_WIN
    } else if (prog == "DBus") {
      // example : "DBus, org.krusader, /Instances/krusader/right_manager,
      // org.krusader.PanelManager, newTab, %dirname(1)
      if (args.count() < 5) {
        qWarning() << "DBus requires at least 4 arguments (service, path, "
                      "interface, method, [args...])";
        return false;
      }

      // multiple calls possible, separated by "&&"
      qDebug() << "DBus args:" << args;
      QVector<QStringList> calls;
      QStringList dbusArgs;
      for (int i = 1; i < args.count(); ++i) {
        if (args[i] == "&&") {
          calls.append(dbusArgs);
          dbusArgs.clear();
        } else
          dbusArgs.append(args[i]);
      }
      calls.append(dbusArgs);

      for (const QStringList& dbusCall : qAsConst(calls))
        if (!callServiceMethod(dbusCall))
          return false;

#endif  // !Q_OS_WIN
    } else {
      QProcess p;
      p.setProgram(prog);

      const QString disableAppProgs = getenv("CBIRD_NO_BUNDLED_PROGS");

      if (disableAppProgs.isEmpty()) {
        bool portable = false;  // portable binaries in same dir as main program
#ifdef CBIRD_PORTABLE_BINARY
        portable = true;
#endif
        QString appDir = getenv("APPDIR"); // AppImage
        bool setEnv = false;
        QString binDir, libDir;
        if (portable || getenv("CBIRD_PORTABLE")) {
          setEnv = true;
          appDir = qApp->applicationDirPath();
          binDir = appDir + "/";
          libDir = appDir + "/";
        } else if (!appDir.isEmpty()) {
          setEnv = true;
          binDir = appDir + "/cbird/bin/";
          libDir = appDir + "/cbird/lib/";
        }

        if (setEnv)
          qDebug() << "portable PATH:" << binDir << "LD_LIBRARY_PATH:" << libDir;

        const QString appProg = binDir + prog;
        if (setEnv && QFileInfo(appProg).exists()) {
          qDebug() << "using " << appProg << "for" << prog;
          qDebug() << "to disable this, set CBIRD_NO_BUNDLED_PROGS";

          // put the bundled apps before everything else
          auto env = QProcessEnvironment::systemEnvironment();
          const QString binPath = env.value("PATH");
          const QString libPath = env.value("LD_LIBRARY_PATH");  // might be empty, should be fine
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
        if (!p.startDetached()) {
          qWarning() << prog << "failed to start, is it installed?";
          return false;
        }
      } else {
        p.start();
        if (!p.waitForStarted()) {
          qWarning() << prog << "failed to start, is it installed?";
          return false;
        }
        p.waitForFinished();
        if (p.exitCode() != 0) {
          qWarning() << prog << "exit code" << p.exitCode() << p.errorString();
          return false;
        }
      }
    }
  }
  return true;
}

QVariant DesktopHelper::getSetting(const QString& key, const QVariant& defaultValue) {
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

bool DesktopHelper::chooseProgram(QStringList& args, const QVector<QStringList>& options,
                                  const QString& settingsKey, const QString& dialogTitle,
                                  const QString& dialogText) {
  if (options.count() == 1 && args.empty()) {
    args = options.at(0).mid(1);
    putSetting(settingsKey, args);
  }

  if (args.empty() || (qApp->keyboardModifiers() == CBIRD_DIALOG_MODS)) {
    QStringList optionLabels;
    for (auto& option : qAsConst(options)) optionLabels += option.first();

    QString program = optionLabels.at(0);
#ifndef QT_TESTLIB_LIB // remove Theme dependency in unit tests
    {
      QWidget* parent = qApp->widgetAt(QCursor::pos());
      QInputDialog dialog(parent);
      int result =
          Theme::instance().execInputDialog(&dialog, dialogTitle,
                                            QString(dialogText) + "\n\n" +
                                                "To change this setting, hold "
                                                CBIRD_DIALOG_KEYS " "
                                                "while selecting the action.",
                                            program, optionLabels);
      if (result != QInputDialog::Accepted) return false;
      program = dialog.textValue();
    }
#endif

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
      {"Krusader (Right Panel)", "DBus", "org.krusader", "/Instances/krusader[0-9]*/right_manager",
       "", "newTab", "%dirname(1)", "&&", "org.krusader", "/MainWindow_[0-9]*", "", "raise"},
      {"Krusader (Left Panel)", "DBus", "org.krusader", "/Instances/krusader[0-9]*/left_manager",
       "", "newTab", "%dirname(1)", "&&", "org.krusader", "/MainWindow_[0-9]*", "", "raise"},
      {"Nautilus (GNOME)", "nautilus", "-s", "%1"},
      {"PCMan (LXDE)", "pcmanfm", "%dirname(1)"},
      {"thunar (Xfce)", "thunar", "%dirname(1)"},
      {"gThumb (GNOME)", "gthumb", "%dirname(1)"}};
#endif
  const QString settingsKey = qq("OpenFileLocation");
  QStringList args = getSetting(settingsKey, {}).toStringList();

  if (!chooseProgram(args, fileManagers, settingsKey, qq("Choose File Manager"),
                     qq("Please choose the program for viewing a file's location.")))
    return;

  // QDesktopServices cannot reveal file location and select it, we need the dir
  QString tmp = path;
  if (args.count() > 0 && args.first() == ll("DesktopServices"))
    tmp = QFileInfo(path).absoluteDir().path();

  if (!runProgram(args, false, tmp))
    putSetting(settingsKey, {});
}

void DesktopHelper::openVideo(const QString& path, double seekSeconds) {
  QStringList args;
  QString settingsKey;

  settingsKey = qq("OpenVideoSeek");
  args = getSetting(settingsKey, {}).toStringList();

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

  if (!runProgram(args, false, path, seekSeconds)) putSetting(settingsKey, {});
}

void DesktopHelper::openVideo(const QString& path)  {
  QString settingsKey = qq("OpenVideo");
  const QStringList defaultArgs = QStringList{{"DesktopServices"}};
  QStringList args = getSetting(settingsKey, defaultArgs).toStringList();

  qDebug() << settingsKey << args;
  (void) runProgram(args, false, path);
}

void DesktopHelper::compareAudio(const QString& path1, const QString& path2) {
  const QString settingsKey = "CompareAudio";
  const QStringList defaultArgs{{"ff-compare-audio", "%1", "%2"}};
  QStringList args = getSetting(settingsKey, defaultArgs).toStringList();
  runProgram(args, false, path1, 0.0, path2);
}

void DesktopHelper::playSideBySide(const QString& path1, double seek1, const QString& path2,
                                   double seek2) {
  const QString settingsKey = "PlaySideBySide";
  const QStringList defaultArgs{{"ffplay-sbs", "%1", "%seek", "%2", "%seek2"}};
  QStringList args = getSetting(settingsKey, defaultArgs).toStringList();
  runProgram(args, false, path1, seek1, path2, seek2);
}

QString DesktopHelper::settingsFile() {
  QString path = QProcessEnvironment::systemEnvironment().value("CBIRD_SETTINGS_FILE");
  if (path.isEmpty())
    path =
        QSettings(QSettings::IniFormat, QSettings::UserScope, qApp->applicationName()).fileName();
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
    trashPath =
        QString("%1/%2.%3.%4").arg(dir).arg(info.completeBaseName()).arg(num).arg(info.suffix());
    num++;
  }

  bool ok = QDir().rename(path, trashPath);

  if (ok)
    qInfo("moved\n\t%s\nto\n\t%s\n", qUtf8Printable(path), qUtf8Printable(trashPath));
  else
    qWarning("move\n\t%s\nto\n\t%s\nfailed due to filesystem error", qUtf8Printable(path),
             qUtf8Printable(trashPath));

  return ok;
}

bool DesktopHelper::moveToTrash(const QString& path) {
  QFileInfo info(path);
  if (!info.isFile()) {
    qWarning() << "requested path is not a file:" << path;
    return false;
  }

  QString dir = QProcessEnvironment::systemEnvironment().value("CBIRD_TRASH_DIR");
  if (!dir.isEmpty()) return moveFile(path, dir);

  const char* settingsKey = "TrashFile";

#if defined(Q_OS_WIN)
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
        if (parts.length() > 2) mountPoint = "//" + parts[0] + "/" + parts[1] + "/";
      } else {
        const QStorageInfo sInfo(QFileInfo(absPath).dir());
        if (!sInfo.isValid()) {
          qWarning() << "has no mount point (drive letter) or is invalid" << absPath;
          return false;
        }
        mountPoint = sInfo.rootPath();
      }

      if (mountPoint.isEmpty() || !QDir(mountPoint).exists()) {
        qWarning() << "invalid or unsupported mount point" << mountPoint;
        return false;
      }

      const QString volumeTrashDir = getSetting("VolumeTrashDir", "_trash").toString();

      qInfo() << "trying fallback" << mountPoint + volumeTrashDir;

      QDir trashDir(mountPoint);
      if (!trashDir.exists(volumeTrashDir) && !trashDir.mkdir(volumeTrashDir)) {
        qWarning() << "fallback failed, cannot create volume trash dir on" << trashDir;
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
      if (status != 0) qWarning() << "SHFileOperation() error" << Qt::hex << status;
    }
  }
#else
#if defined(Q_OS_MAC)
  const QStringList defaultArgs{{"trash", "-v", "-F", "%1"}}; // trash-put doesn't use Finder interface
#else
  const QStringList defaultArgs{{"trash-put", "%1"}};
#endif
  QStringList args = getSetting(settingsKey, defaultArgs).toStringList();
  // we must wait, because the caller could be renaming or moving
  runProgram(args, true, path);
#endif
  bool ok = !QFileInfo(path).exists();
  if (!ok) qWarning() << "failed to move to trash" << path;
  return ok;
}

QString DesktopHelper::tempName(const QString& nameTemplate, QObject* parent, int maxLifetime) {
  QString fileName;
  {
    QTemporaryFile temp;
    temp.setAutoRemove(false);
    temp.setFileTemplate(QDir::tempPath() + "/" + nameTemplate);
    if (!temp.open()) return "";
    fileName = temp.fileName();
  }  // temp is now closed

  if (parent)
    QObject::connect(parent, &QObject::destroyed, [=]() {
      QFile f(fileName);
      if (f.exists() && !f.remove())
        qWarning() << "failed to delete temporary (at exit)" << fileName;
    });

  if (maxLifetime > 0)
    QTimer::singleShot(maxLifetime * 1000, [=]() {
      QFile f(fileName);
      if (f.exists() && !f.remove())
        qWarning() << "failed to delete temporary (on timer)" << fileName;
    });

  QObject* object = new QObject(qApp);
  QObject::connect(object, &QObject::destroyed, [=]() {
    QFile f(fileName);
    if (f.exists() && !f.remove()) qWarning() << "failed to delete temporary (at exit)" << fileName;
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
  (void)window;
  (void)enable;
}

void WidgetHelper::setWindowTheme(QWidget* window, bool dark) {
  (void)window;
  (void)dark;
}

void WidgetHelper::hackShowWindow(QWidget* window, bool maximized) {
  if (maximized) {
#ifdef Q_OS_UNIX
    // showMaximize is inherently broken on some window managers/display servers
    // https://doc.qt.io/qt-6/application-windows.html#x11-peculiarities
    bool isX11 = qEnvironmentVariable("XDG_SESSION_TYPE") == "x11";
    bool brokenMaximize = isX11 && qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains("GNOME");

    if (brokenMaximize || qEnvironmentVariableIsSet("CBIRD_MAXIMIZE_HACK")) {
      window->show();
      while (!window->isActiveWindow()) // isVisible() doesn't work here
        qApp->processEvents();

      window->showMaximized();
    } else
#endif
      window->showMaximized();
  } else
    window->show();
}

#else
#  include <dwmapi.h>

enum PreferredAppMode { Default, AllowDark, ForceDark, ForceLight, Max };

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

using AllowDarkModeForWindowFunc = BOOL(WINAPI*)(HWND hWnd, BOOL allow);
using SetPreferredAppModeFunc = PreferredAppMode(WINAPI*)(PreferredAppMode appMode);
using SetWindowCompositionAttributeFunc = BOOL(WINAPI*)(HWND hwnd, WINDOWCOMPOSITIONATTRIBDATA*);

void WidgetHelper::setWindowTheme(QWidget* window, bool dark) {
  if (!dark) return;

  qDebug() << "Enabling Win32 Dark Mode";

  auto uxThemeLib = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

  auto AllowDarkModeForWindow =
      (AllowDarkModeForWindowFunc)(void*)GetProcAddress(uxThemeLib, MAKEINTRESOURCEA(133));

  auto SetPreferredAppMode =
      (SetPreferredAppModeFunc)(void*)GetProcAddress(uxThemeLib, MAKEINTRESOURCEA(135));

  auto user32Lib = GetModuleHandleW(L"user32.dll");
  auto SetWindowCompositionAttribute = (SetWindowCompositionAttributeFunc)(void*)GetProcAddress(
      user32Lib, "SetWindowCompositionAttribute");

  SetPreferredAppMode(AllowDark);

  HWND hwnd = (HWND)window->winId();
  BOOL enable = true;
  AllowDarkModeForWindow(hwnd, enable);

  WINDOWCOMPOSITIONATTRIBDATA data = {WCA_USEDARKMODECOLORS, &enable, sizeof(enable)};
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

  qApp->processEvents();  // paint the initial background

  setWindowCloak(window, false);
}

void WidgetHelper::setWindowCloak(QWidget* window, bool enable) {
  BOOL cloak = enable;
  auto hwnd = (HWND)window->winId();
  DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
}

#endif

void WidgetHelper::saveGeometry(const QWidget* w, const char* id) {
  QByteArray tmp;
  if (!id) {
    if (!w->objectName().isEmpty()) {
      tmp = w->objectName().toUtf8();
      id = tmp.data();
    } else if (w->metaObject())
      id = w->metaObject()->className();
    else
      id = w->staticMetaObject.className();
  }

  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(id);

  // bugged for maximized windows (Gnome3)
  // also we want to set the maximized state ourselves; this cannot be allowed
  // to show the window as that goes through Theme:: for things to look right
  //settings.setValue("geometry", w->saveGeometry());

  bool maximized  = w->isMaximized() || w->isFullScreen();
  settings.setValue("maximized", maximized);

  // note: normalGeometry does not work on all platforms
  // known to be broken: xfwm4 - maximized window->normalGeometry==geometry
  auto geom = w->normalGeometry();
  settings.setValue("normalGeometry", geom);
  qDebug() << id << "normalGeometry:" << geom << "maximized:" << maximized;
}

bool WidgetHelper::restoreGeometry(QWidget* w, const char* id) {
  QByteArray tmp;
  if (!id) {
    if (!w->objectName().isEmpty()) {
      tmp = w->objectName().toUtf8();
      id = tmp.data();
    } else if (w->metaObject())
      id = w->metaObject()->className();
    else
      id = w->staticMetaObject.className();
  }

  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(id);

  // bugged for maximized windows (Gnome3)
  //  if (!w->restoreGeometry(settings.value("geometry").toByteArray())) {
  //    QRect avail = qApp->primaryScreen()->availableGeometry();
  //    int width = qMin(1280, avail.width());
  //    int height = qMin(720, avail.height());
  //    w->setGeometry((avail.width()-width)/2, (avail.height()-height)/2, width, height);
  //  }

  const QRect invalidRect(-100, -100, -1, -1);

  QRect normalGeom = settings.value("normalGeometry", invalidRect).toRect();
  QSize size = normalGeom.size();
  QPoint pos = normalGeom.topLeft();

  bool maximized = settings.value("maximized", false).toBool();

  QScreen* screen = qApp->screenAt(pos);
  if (!screen) {
    screen = qApp->primaryScreen();
    pos = invalidRect.topLeft();
  }

  QRect avail = screen->availableGeometry();

  // lost position, center the window
  if (size == invalidRect.size()) size = QSize(1280, 720);

  // !! this is actually wrong because size does not
  // include the window frame -- but we cannot know what it is...
  // if we subtract some for the title bar maybe its ok most of the time
  int width = qMin(size.width(), avail.width());
  int height = qMin(size.height(), avail.height() - 32);

  if (pos == invalidRect.topLeft())
    pos = QPoint((avail.width() - width) / 2, (avail.height() - height - 32) / 2);

  // hopefully restores normalGeometry()
  w->setGeometry(pos.x(), pos.y(), width, height);

  qDebug() << "geometry:" << pos << width << height << "maximized:" << maximized;

  return maximized;
}

void WidgetHelper::saveTableState(const QTableView* w, const char* id) {
  auto* model = w->model();
  Q_ASSERT(model);

  if (!id) id = w->metaObject()->className();
  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(id);
  settings.setValue("horizontalHeader", w->horizontalHeader()->saveState());
}

bool WidgetHelper::restoreTableState(QTableView* w, const char* id) {
  Q_ASSERT(w->model());
  if (!id) id = w->metaObject()->className();
  QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
  settings.beginGroup(id);
  return w->horizontalHeader()->restoreState(settings.value("horizontalHeader").toByteArray());
}

QAction* WidgetHelper::addAction(QSettings& settings, const QString& label,
                                 const QKeySequence& shortcut, QWidget* target, const char* slot) {
  QAction* a = new QAction(label, target);
  target->connect(a, SIGNAL(triggered(bool)), target, slot);
  a->setShortcut(getShortcut(settings, label, shortcut));
  a->setShortcutVisibleInContextMenu(true);
  target->addAction(a);
  return a;
}

QAction* WidgetHelper::addAction(QSettings& settings, const QString& label,
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

size_t DBHelper::rowCount(QSqlQuery& query, const QString& tableName) {
  if (!query.exec("select count(0) from " + tableName)) SQL_FATAL(exec);
  if (!query.next()) SQL_FATAL(next);
  return query.value(0).toULongLong();
}

QDateTime DBHelper::lastModified(const QSqlDatabase& db) {
  // this only works with local file database drivers, like sqlite
  QString dbPath = db.databaseName();
  Q_ASSERT(!dbPath.isEmpty());

  QFileInfo dbInfo(dbPath);
  if (!dbInfo.exists()) return QDateTime::fromSecsSinceEpoch(INT64_MAX);

  return dbInfo.lastModified();
}

QMenu* MenuHelper::dirMenu(
    const QString& root, QWidget* target, const char* slot, int maxFolders, int maxDepth) {
  QMenu* menu = makeDirMenu(root, target, slot, maxFolders, maxDepth, 0);
  if (!menu) menu = new QMenu;

  QAction* action = new QAction("Choose Folder...", menu);
  action->connect(action, SIGNAL(triggered(bool)), target, slot);
  action->setData(";newfolder;");

  menu->insertSeparator(menu->actions().at(0));
  menu->insertAction(menu->actions().at(0), action);

  return menu;
}

QMenu* MenuHelper::makeDirMenu(const QString& root, QWidget* target, const char* slot,
                               int maxFolders, int maxDepth, int depth) {
  if (depth >= maxDepth) return nullptr;

  const auto& list = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  if (list.count() <= 0) return nullptr;

  QMenu* menu = new QMenu;
  QAction* action = menu->addAction(".");
  action->setData(root);
  action->connect(action, SIGNAL(triggered(bool)), target, slot);

  int partition = 0;
  QMenu* partMenu = nullptr;

  for (const auto& fileName : list) {
    // TODO: setting for index dir name
    // const QString& path = entry.absoluteFilePath();
    const QString& path = root + "/" + fileName;
    if (fileName == INDEX_DIRNAME) continue;

    // TODO: setting or detect max popup size
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

    QMenu* subMenu = makeDirMenu(path, target, slot, maxFolders, maxDepth, depth + 1);
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

QString qElide(const QString& str, int maxLen) {
  QString tmp;
  if (str.length() > maxLen) {
    int half = (maxLen - 3) / 2;
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
#  include "QtCore/private/qobject_p.h"

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
// TODO: drop logs if too much piles up
// TODO: do bigger console writes, combine lines with timer
// TODO: use ::write() instead of fwrite
//

#if !defined(QT_MESSAGELOGCONTEXT)  // disabled in release targets by default... but we need it
#  error qColorMessageOutput requires QT_MESSAGELOGCONTEXT
#endif

#include "exiv2/error.hpp"  // capture exif library logs

static void exiv2(int level, const char* msg) {
  const int nLevels = 4;
  static constexpr QtMsgType levelToType[nLevels] = {QtDebugMsg, QtInfoMsg, QtWarningMsg,
                                                     QtCriticalMsg};
  // static constexpr QMessageLogContext context("", 0, "exif()", "exif");
  static QLoggingCategory category("exiv2");

  if (level >= nLevels || level < 0) return;

  //   qColorMessageOutput(levelToType[level], context, QString(msg).trimmed());
  QString str = QString(msg).trimmed();
  switch (levelToType[level]) {
    case QtDebugMsg:
      qCDebug(category) << str;
      break;
    case QtInfoMsg:
      qCInfo(category) << str;
      break;
    case QtWarningMsg:
      qCWarning(category) << str;
      break;
    case QtCriticalMsg:
    case QtFatalMsg:
      qCCritical(category) << str;
      break;
  }
}

// headers for terminal detection
#ifdef Q_OS_WIN
#  include <fcntl.h>
#  include <fileapi.h>
#  include <io.h>
#  include <winbase.h>
#else
#  include <unistd.h>  // isatty
extern "C" {
#  include <termcap.h>
#  include <sys/signal.h>
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
  const char* file;
  const char* function;
  const char* category;
};

/// Private logging class/singleton
class MessageLog {
 private:
  QList<LogMsg> _log;

  QThread* _thread = nullptr;
  QMutex _mutex;
  QWaitCondition _logCond, _syncCond;
  volatile bool _stop = false;  // set to true to stop log thread
  volatile bool _sync = false;  // set to true to sync log thread

  bool _isTerm = false;      // true if we think stdout is a tty
  bool _termColors = false;  // true if tty supports colors
  int _termColumns = -1;     // number of columns in the tty

  QString _homePath;
  const bool _showTimestamp = getenv("CBIRD_LOG_TIMESTAMP");
  mutable const char* _lastColor = nullptr;  // format()
  mutable int64_t _lastTime = nanoTime();    // format()
  mutable QString _formatStr;                // format()

  QStringList _categoryFilters;

  MessageLog();
  ~MessageLog();

  void outputThread();
  QString format(const LogMsg& msg, int& outUnprintable) const;

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

  void setCategoryFilter(const QString& category, bool enable) {
    QMutexLocker locker(&_mutex);
    if (!enable) {
      if (!_categoryFilters.contains(category))
        _categoryFilters.append(category);
    } else {
      _categoryFilters.removeOne(category);
    }
  }

#ifndef Q_OS_WIN
  static int getTTYColumns() {
    // https://stackoverflow.com/questions/1022957/getting-terminal-width-in-c
    const char* termEnv = getenv("TERM");
    if (termEnv) {
      char termBuf[2048];
      int err = tgetent(termBuf, termEnv);
      if (err <= 0)
        printf("unknown terminal TERM=%s TERMCAP=%s err=%d, cannot guess config\n",
               termEnv,
               getenv("TERMCAP"),
               err);
      else {
        char li[2] = {'l', 'i'};
        char co[2] = {'c', 'o'};
        int termLines = tgetnum(li);
        int termCols = tgetnum(co);
        if (termCols > 0 && termLines > 0) return termCols;

        printf("unknown terminal TERM=%s no lines/cols provided, use CBIRD_CONSOLE_WIDTH\n",
               termEnv);
      }
    }
    return -1;
  }

  static void sigWinchHandler(int sig) {
    (void) sig;
    instance()._termColumns = getTTYColumns();
  }
#endif
};

void qFlushMessageLog() {
  MessageLog::instance().flush();
}

Q_NORETURN void cbirdAbort() {
  qFlushMessageLog();
  _Exit(255);
}

void qMessageLogCategoryEnable(const QString& category, bool enable) {
  MessageLog::instance().setCategoryFilter(category, enable);
}

const QThreadStorage<QString> &qMessageContext() { return MessageLog::context(); }

void qColorMessageOutput(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
  const auto& perThreadContext = MessageLog::context();
  QString threadContext;
  if (perThreadContext.hasLocalData()) threadContext = perThreadContext.localData();

  MessageLog::instance().append(LogMsg{threadContext, type, msg, ctx.version, ctx.line, ctx.file,
                                       ctx.function, ctx.category});

  if (type == QtMsgType::QtFatalMsg) {
    // qFatal() is being used for user errors...however that causes core dumps
    // and possibly triggers telemetry/crash-handling -- so just exit,
    // preferably w/o calling static destructors
    cbirdAbort();
  }

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
      fprintf(stdout, "\n\n[X][%s:%d] debug trigger matched: <<%s>>\n\n", ctx.file, ctx.line,
              qPrintable(msg));
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
    _savedContext = threadContext.localData();
  threadContext.setLocalData(context);
}

MessageContext::~MessageContext() { MessageLog::context().setLocalData(_savedContext); }

void MessageContext::reset(const QString& context) { MessageLog::context().setLocalData(context); }

MessageLog::MessageLog() {
  std::set_terminate(qFlushMessageLog);
  Exiv2::LogMsg::setHandler(exiv2);

#ifdef Q_OS_WIN
  auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (GetConsoleMode(handle, &mode)) {  // windows terminal, but not msys/mingw/cygwin
    // printf("win32 console detected mode=0x%x\n", (int)mode);
    _isTerm = true;
    _termColors = mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(handle, &csbi))
      _termColumns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    // int rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  } else if (getenv("TERM") && GetFileType(handle) == FILE_TYPE_PIPE) {  // maybe msys/mingw...
    struct {
      DWORD len;
      WCHAR name[MAX_PATH];
    } info;
    memset(&info, 0, sizeof(info));
    if (GetFileInformationByHandleEx(handle, FileNameInfo, &info, sizeof(info))) {
      // req windows > 7 it seems, though it will run on 7,  no result
      QString name = QString::fromWCharArray((wchar_t*)info.name, info.len);
      printf("stdio pipe=%d %s\n", (int)info.len, qUtf8Printable(name));
      _isTerm = _termColors = (name.contains("msys-") && name.contains("-pty"));
    }
  }
#else
  _isTerm = isatty(fileno(stdout));
  if (_isTerm) {
    _termColors = true;  // assume we have color on unix-like systems
    _termColumns = getTTYColumns();
    if (_termColumns <= 0)
      _termColumns = 80;
    else if (!getenv("CBIRD_CONSOLE_WIDTH")) {
      // handle tty resize events
      if (signal(SIGWINCH, sigWinchHandler) == SIG_ERR)
        printf("signal(SIGWINCH) failed: %d %s\n", errno, strerror(errno));
    }
  }
#endif

  // detection is buggy, provide overrides
  if (getenv("CBIRD_FORCE_COLORS")) _termColors = 1;

  // useful for unit tests
  if (getenv("CBIRD_NO_COLORS")) _termColors = 0;

  QString tc = getenv("CBIRD_CONSOLE_WIDTH");
  if (!tc.isEmpty()) _termColumns = tc.toInt();

#ifdef DEBUG
  if (_isTerm) printf("[DEBUG] term width=%d colors=%d\n", _termColumns, _termColors);
#endif

  _homePath = QDir::homePath();

#ifdef Q_OS_WIN
  // disable text mode to speed up console
  _setmode(_fileno(stdout), _O_BINARY);
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

  QString lastMsg, lastInput, lastOutput, lastProgressLine;
  int numRepeats = 0;
  QMutexLocker locker(&_mutex);
  while (!_stop && _logCond.wait(&_mutex)) {
    if (_sync && _log.count() <= 0) {
      _sync = false;
      _syncCond.wakeAll();
    }
    while (_log.count() > 0) {
      const LogMsg msg = _log.takeFirst();

#ifndef DEBUG // we don't want to miss any logs in debug builds
      if (_categoryFilters.contains(msg.category)) continue;
#endif

      // compress repeats while we hold the lock
      int pl = msg.msg.indexOf(tokenProgress);  // do not compress progress lines
      if (pl <= 0 && lastMsg == msg.msg && !msg.msg.isEmpty()) {
        numRepeats++;
        continue;
      }
      locker.unlock();
      lastMsg = msg.msg;

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

      int unprintableChars = 0;   // we need this for accurate eliding
      const QString formatted = format(msg, unprintableChars);
      if (formatted.isEmpty()) {  // possible with filters
        locker.relock();
        continue;
      }
      lastInput.resize(0);
      lastInput += formatted;
      QString output = lastInput;

      // special progress line, everything before the <PL> must be static
      pl = output.indexOf(tokenProgress);
      if (pl > 0) output.remove(pl, tokenProgress.size());

      // special elide indicator, everything after is elided to terminal width
      // note: must come after <PL> since prefix of <PL> must be static
      int elide = output.indexOf(tokenElide);
      if (elide > 0 && elide > pl) {
        if (_termColumns > 0) {
          auto toElide = output.mid(elide + tokenElide.size());
          auto elided = qElide(toElide, _termColumns + unprintableChars - elide);
          output = output.mid(0, elide) + elided;
          output += QString().fill(charSpace, _termColumns + unprintableChars - output.length());
        } else
          output.remove(elide, tokenElide.size());
      }

      // find chars to append/prepend
      const QChar *prepend = nullptr, *append = nullptr;

      if (pl > 0 && _isTerm) {
        auto prefix = QStringView(output).mid(0, pl);
        if (lastProgressLine.startsWith(prefix))
          prepend = &charCR;
        else if (!lastOutput.endsWith(charLF))
          prepend = &charLF;

        lastProgressLine = output;

      } else {
        if (!lastOutput.endsWith(charLF)) prepend = &charLF;
        append = &charLF;
        lastProgressLine.clear();
      }

      lastOutput.resize(0);                                    // next appends are allocation free
      if (lastOutput.capacity() > 1024) lastOutput.squeeze();  // don't take too much

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
    }  // _log.count() > 0
  }    // !_stop
}

QString MessageLog::format(const LogMsg& msg, int& outUnprintable) const {
  struct MessageFormat {
    QLatin1String label;
    const char* color;
  };

  // table index is QtDebugMsg ... QtInfoMsg
  static constexpr MessageFormat formats[QtInfoMsg + 1] = {{QLatin1String("D"), ""},
                                                           {QLatin1String("W"), "<YEL>"},
                                                           {QLatin1String("C"), "<BRIGHT><RED>"},
                                                           {QLatin1String("F"),
                                                            "<UNDERL><BRIGHT><RED>"},
                                                           {QLatin1String("I"), ""}};

  // TODO: some colors could come from LS_COLORS for better integration
  // note: everything in this table should be unprintable (color,underline,bold,etc)
  static constexpr struct {
    const char* name;
    const char* replacement;
  } tags[] = {{"<RED>", VT_RED},         {"<GRN>", VT_GRN},       {"<YEL>", VT_YEL},
              {"<BLU>", VT_BLU},         {"<MAG>", VT_MAG},       {"<CYN>", VT_CYN},
              {"<WHT>", VT_WHT},         {"<RESET>", VT_RESET},   {"<BRIGHT>", VT_BRIGHT},
              {"<DIM>", VT_DIM},         {"<UNDERL>", VT_UNDERL}, {"<BLINK>", VT_BLINK},
              {"<REVERSE>", VT_REVERSE}, {"<HIDDEN>", VT_HIDDEN}, {"<NUM>", VT_CYN},
              {"<TIME>", VT_DIM VT_WHT}, {"<PATH>", VT_GRN}};

  // table could become invalid, enum is unnumbered in qlogging.h
  static_assert(QtDebugMsg == 0 && QtInfoMsg == 4);
  if (msg.type < 0 || msg.type > 4) {
    fprintf(stderr, "unexpected qt logging type\n");
    return msg.msg;
  }

  const auto& fmt = formats[msg.type];

  // don't change colors unless we have to (maybe faster)
  if (_termColors && _lastColor != fmt.color) {
    // fprintf(stdout, "%s%s", VT_RESET, fmt.color);
    // _lastColor = fmt.color;
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

  _formatStr.resize(0);             // no more allocs after a few calls

  if (_formatStr.capacity() > 1024) // don't take too much
    _formatStr.squeeze();

  if (_showTimestamp) {
    auto currTime = nanoTime();
    int micros = (currTime - _lastTime) / 1000;
    _lastTime = currTime;
    _formatStr += "<RESET><DIM><MAG>";
    _formatStr += QString::asprintf("%06d ", micros);
  }

  if (msg.msg.startsWith(QLatin1String("<NC>"))) { // no context
    _formatStr += "<RESET>";
    _formatStr += fmt.color;
    _formatStr += QStringView(msg.msg).mid(4);
  } else {
    _formatStr += "<RESET><DIM><WHT>";
    //_formatStr += fmt.label;
    //_formatStr += QLatin1Char(' ');
    _formatStr += QLatin1Char('@');

    _formatStr += shortFunction;
    if (!msg.threadContext.isNull()) {
      _formatStr += QLatin1Char('{');
      _formatStr += msg.threadContext;
      _formatStr += QLatin1Char('}');
    }
    _formatStr += QLatin1Char('$');
    _formatStr += QLatin1Char(' ');

    _formatStr += "<RESET>";
    _formatStr += fmt.color;

    _formatStr += msg.msg;
  }

  _formatStr.replace(_homePath, QLatin1String("~"));

  outUnprintable = 0;
  for (auto& tag : tags) {
    qsizetype pos = 0;
    while (0 <= (pos = _formatStr.indexOf(tag.name, pos))) {
      if (_termColors) {
        _formatStr.replace(pos, strlen(tag.name), tag.replacement);
        outUnprintable += strlen(tag.replacement);
      } else
        _formatStr.replace(pos, strlen(tag.name), qq(""));
    }
  }
  return _formatStr;
}

void MessageLog::append(const LogMsg& msg) {
#if CBIRD_LOG_IMMEDIATE
  auto str = format(msg) + "\n";
  const QByteArray utf8 = str.toUtf8();
  fwrite(utf8.data(), utf8.length(), 1, stdout);
  fflush(stdout);
  return;
#endif

  if (msg.type != QtFatalMsg) {
    QMutexLocker locker(&_mutex);
    _log.append(msg);
    _logCond.wakeAll();
  } else {
    // if fatal, flush logger, since abort() comes next
    qFlushMessageLog();
    int unprintable = 0;
    fprintf(stdout, "\n%s%s\n\n", qUtf8Printable(format(msg, unprintable)),
            _termColors ? VT_RESET : "");
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
  } else {
    // no thread, ensure all logs are written
    QByteArray utf8("\n");

    while (_log.count() > 0) {
      int unprintable = 0;
      utf8 += format(_log.takeFirst(), unprintable).toUtf8();
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
  if (ord == QPartialOrdering::Unordered) {
    if (a.isNull())  // sort null before non-null
      ord = QPartialOrdering::Less;
    else if (b.isNull())
      ord = QPartialOrdering::Greater;
    else
      qWarning() << a << "and" << b << "are not comparable";
  }
  return ord;
}
#endif

ShadeWidget::ShadeWidget(QWidget* parent) : QLabel(parent) {
#ifndef QT_TESTLIB_LIB // theme dep in unit tests
  setProperty("style", Theme::instance().property("style")); // stylesheet sets transparent bg color
#endif
  setGeometry({0, 0, parent->width(), parent->height()});
  setMargin(0);
  setFrameShape(QFrame::NoFrame);
  // prevent stacking of effect; note it will still stack with
  // the window manager's effect (os x, kde)
  if (!parent->property("shaded").toBool()) {
    parent->setProperty("shaded", true);
    show();
  }
}

ShadeWidget::~ShadeWidget() {
  if (!isHidden())
    parent()->setProperty("shaded", false);
}

int qNumericSubstringCompare(const QCollator& cmp, const QStringView& a, const QStringView& b) {
  static const auto indexOfNumber = [](const QStringView& str, int start) {
    for (int i = start; i < str.length(); ++i)
      if (str.at(i).isDigit()) return i;
    return -1;
  };

  static const auto indexOfNonNumber = [](const QStringView& str, int start) {
    for (int i = start; i < str.length(); ++i)
      if (!str.at(i).isDigit()) return i;
    return -1;
  };

  // current index into string
  int ia = a.isEmpty() ? -1 : 0;
  int ib = b.isEmpty() ? -1 : 0;

  auto compareNumbers = [&a, &b, &ia, &ib](const int na, const int nb) {
    bool fraction = false;
    if (na > 1 && a[na - 1] == lc('.') && a[na - 2].isDigit() &&
        nb > 1 && b[nb - 1] == lc('.') && a[nb - 2].isDigit())
      fraction = true;

    ia = indexOfNonNumber(a, ia);
    ib = indexOfNonNumber(b, ib);
    auto pa = a.mid(na, ia > 0 ? (ia - na) : -1);
    auto pb = b.mid(nb, ib > 0 ? (ib - nb) : -1);

    quint64 intA = pa.toULongLong();
    quint64 intB = pb.toULongLong();

    if (Q_UNLIKELY(fraction && pa.length() != pb.length())) {
      // pad the smaller number to the same length, to compare as int
      int min, max;
      quint64* shorter;
      const int la = pa.length(), lb = pb.length();
      if (la < lb)
        min = la, max = lb, shorter = &intA;
      else
        min = lb, max = la, shorter = &intB;
      while (min < max) {
        *shorter *= 10;
        min++;
      }
    }

    int order = intA < intB ? -1 : (intA > intB ? 1 : 0);
    return order;
  };

  while (true) {
    if (ia < 0 && ib >= 0) return -1;  // empty < something
    if (ia >= 0 && ib < 0) return 1;   // something > empty
    if (ia < 0 && ib < 0) return 0;    // empty == empty
    // both >= 0; keep looping

    const int na = indexOfNumber(a, ia);
    if (na == ia) {
      const int nb = indexOfNumber(b, ib);
      if (nb == ib) {  // numeric in both
        int r = compareNumbers(na, nb);
        if (r != 0) return r;
        continue;
      } else
        return -1; // a is num, b is not, a < b
    }

    const int nb = indexOfNumber(b, ib);
    if (nb == ib) {
      const int na = indexOfNumber(a, ia);
      if (na == ia) {
        int r = compareNumbers(na, nb);
        if (r != 0) return r;
        continue;
      } else
        return 1; // b is num, a is not, a > b
    }

    // if non-numeric prefixes have the same length, compare prefixes
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

    Q_UNREACHABLE(); // every case either continues or returns
  }
}

bool ProgressLogger::_alwaysShow = false;

ProgressLogger::ProgressLogger(const QString& format, uint64_t maxStep, const char* contextFunc)
    : _format(format)
    , _max(maxStep)
    , _context("", 0, contextFunc, "") {
  _hideTimer.start();
  _rateLimitTimer.start();
}

void ProgressLogger::formatString(QString& str, uint64_t step, const QVariantList& args) const {
  str.replace("%percent",
              "<GRN>" + (_max > 0 ? QString::number(step * 100 / _max) : "100") + "%<RESET>");
  str.replace("%step", "<CYN>" + _locale.toString(step) + "<RESET>");

  for (int i = 0; i < args.size(); ++i) {
    const QVariant& val = args.at(i);
    const QMetaType type = val.metaType();

    QString repl;

    switch (type.id()) {
      case QMetaType::Int:
      case QMetaType::UInt:
      case QMetaType::LongLong:
      case QMetaType::ULongLong:
      case QMetaType::Long:
      case QMetaType::ULong:
        repl = "<NUM>" + _locale.toString(val.toULongLong()) + "<RESET>";
        break;
      case QMetaType::Float:
      case QMetaType::Double:
        repl = "<NUM>" + _locale.toString(val.toDouble()) + "<RESET>";
        break;
      default:
        repl = val.toString();
    }

    str.replace("%" + QString::number(i + 1), repl);
  }
}

void ProgressLogger::step(uint64_t step, const QVariantList& args) const {
  if (!_alwaysShow && _hideTimer.elapsed() < 500) return;
  // one print per percent output
  // if (_max > 0 && ((step - 1) * 100) / _max == (step * 100) / _max) return;
  QString out = _format;
  formatString(out, step, args);
  qColorMessageOutput(QtInfoMsg, _context, out);
}

void ProgressLogger::stepRateLimited(uint64_t step, const QVariantList& args) {
  if (!_alwaysShow && _hideTimer.elapsed() < 500) return;
  // one print per percent output
  // if (_max > 0 && ((step - 1) * 100) / _max == (step * 100) / _max) return;
  if (_rateLimitTimer.elapsed() < 100) return;
  QString out = _format;
  formatString(out, step, args);
  qColorMessageOutput(QtInfoMsg, _context, out);
  _rateLimitTimer.start();
}

void ProgressLogger::end(uint64_t step, const QVariantList& args) const {
  if (!_alwaysShow && !_showLast && _hideTimer.elapsed() < 500) return;
  QString out = _format;
  out.replace(" %percent", "");
  out += " <TIME>" + _locale.toString(_hideTimer.elapsed()) + "ms";
  formatString(out, step > 0 ? step : _max, args);
  qColorMessageOutput(QtInfoMsg, _context, out);
}
