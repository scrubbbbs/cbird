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

      auto bus = QDBusConnection::sessionBus();
      auto paths = listServiceObjects(bus, args[1], QString());
      qDebug() << "DBus services"
               << QStringList(bus.interface()->registeredServiceNames());
      qDebug() << "Service objects" << paths;

      // path may contain regular expression, the first matching path is taken
      // useful for apps that have randomized path names (krusader)
      auto objectPath = args[2];
      bool validPath = false;
      QRegularExpression pathMatch(objectPath);
      for (auto& path : paths)
        if (pathMatch.match(path).hasMatch()) {
          objectPath = path;
          validPath = true;
          break;
        }

      if (!validPath) {
        qWarning() << "DBus service missing" << objectPath << "is" << args[1]
                   << "running?";
        return;
      }

      QDBusInterface remoteApp(args[1], objectPath, args[3],
                               QDBusConnection::sessionBus());
      if (remoteApp.isValid()) {
        QVariantList methodArgs;
        for (int i = 5; i < args.count(); ++i) methodArgs.append(args[i]);

        QDBusReply<void> reply;
        reply =
            remoteApp.callWithArgumentList(QDBus::Block, args[4], methodArgs);
        if (!reply.isValid()) qWarning() << "DBus Error:" << reply.error();
      } else
        qWarning() << "DBus failed to connect:" << remoteApp.lastError();
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
      //#ifdef Q_OS_WIN
      //      p.setNativeArguments(args.mid(1).join(" "));
      //#else
      p.setArguments(args.mid(1));
      //#endif
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
  fileManagers +=
      QStringList{{"Dolphin (KDE)", "/usr/bin/dolphin", "--select", "%1"}};
  fileManagers += QStringList{{"Krusader (Right Panel)", "DBus", "org.krusader",
                               "/Instances/krusader[0-9]*/right_manager", "",
                               "newTab", "%dirname(1)"}};
  fileManagers += QStringList{{"Krusader (Left Panel)", "DBus", "org.krusader",
                               "/Instances/krusader[0-9]*/left_manager", "",
                               "newTab", "%dirname(1)"}};
  fileManagers +=
      QStringList{{"Nautilus (GNOME)", "/usr/bin/nautilus", "-s", "%1"}};
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
        QStringList{{"VLC", "C:/Program Files (x86)/VideoLan/VLC/vlc.exe",
                     "--start-time=%seek", "%1"}};
    openVideoSeek +=
        QStringList{{"FFplay", "ffplay.exe", "-ss", "%seek", "%1"}};
#else
    openVideoSeek += QStringList{
        {"Celluloid", "celluloid", "--mpv-options=--start=%seek", "%1"}};
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
    qInfo("moved\n\t%s\nto\n\t%s\n", qPrintable(path), qPrintable(trashPath));
  else
    qWarning("move\n\t%s\nto\n\t%s\nfailed due to filesystem error",
             qPrintable(path), qPrintable(trashPath));

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

//
// Custom logger magic!
//
// - "just works" by using qDebug() etc everywhere
// - colors for qDebug etc
// - disable colors if no tty (isatty()=0)
// - per-thread context can be added (like what file is being worked on)
// - special sequences recognized
//   <NC> - do not write message context, good for progress bars
//   <PL> - progress line, erase the previous line (like \r)
// - threaded output
//    * logs choke main thread, really badly on windows
//    * qFlushOutput() to sync up when needed
// - compression
//    * repeated log lines show # of repeats
//
// todo: drop logs if too much piles up
// todo: avoid color resets? they might slow the terminal
// todo: do bigger console writes, combine lines with timer
// todo: use ::write() instead of fwrite
//


#include <unistd.h>  // isatty
#include "exiv2/error.hpp" // capture exif library logs

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

#if !defined(QT_MESSAGELOGCONTEXT)
#error qColorMessageOutput requires QT_MESSAGELOGCONTEXT
#endif
QThreadStorage<QString> qMessageContext;

static void exifLogHandler(int level, const char* msg) {
  const int nLevels = 4;
  constexpr QtMsgType levelToType[nLevels] = {
    QtDebugMsg, QtInfoMsg, QtWarningMsg, QtCriticalMsg
  };
  constexpr QMessageLogContext context("", 0, "exif()", "");
  if (level < nLevels && level > 0)
    qColorMessageOutput(levelToType[level], context, QString(msg).trimmed());
}


#ifdef Q_OS_WIN
#include <fcntl.h>
#endif

class LoggerThread {
 public:
  QThread* thread;
  QMutex mutex;
  QWaitCondition cond;
  volatile bool stop = false;
  QVector<QString> log;

  LoggerThread() {
    std::set_terminate(qFlushOutput);
    Exiv2::LogMsg::setHandler(exifLogHandler);

#ifdef Q_OS_WIN
    // disable text mode to speed up console
    _setmode( _fileno(stdout), _O_BINARY );
#endif
    thread = QThread::create([this]() {
      QString lastInput, lastOutput;
      int repeats = 0;
      QMutexLocker locker(&mutex);
      while (!stop && cond.wait(&mutex))
        while (log.count() > 0) {
          const QString line = log.takeFirst();
          // do not compress progress lines
          int pl = line.indexOf("<PL>");
          if (pl <= 0 && lastInput == line) {
            repeats++;
            continue;
          }
          locker.unlock();

          if (repeats > 0) {
            QString output = lastInput + " [x" + QString::number(repeats) + "]\n";
            QByteArray utf8 = output.toUtf8();
            fwrite(utf8.data(), utf8.length(), 1, stdout);
            repeats = 0;
          }

          lastInput = line;

          QString output;

          if (pl > 0) {
            output = line;
            output.replace("<PL>", "");
            if (lastInput.startsWith(line.midRef(0, pl)))
              output = "\r" + output;
          }
          else {
            if (!lastOutput.endsWith("\n"))
              output = "\n" + line + "\n";
            else
              output = line + "\n";
          }

          lastOutput = output;

          QByteArray utf8 = output.toUtf8();
          fwrite(utf8.data(), utf8.length(), 1, stdout);
          if (pl > 0) fflush(stdout);
          locker.relock();
        }
    });
    thread->start();
  }
  ~LoggerThread() {
    //fprintf(stdout, "~LoggerThread\n");
    stop = true;
    cond.wakeAll();
    thread->wait();
    qFlushOutput();
    fprintf(stdout, "\n"); // last output might have no trailing "\n"
    fflush(stdout);
  };
  void append(const QString& msg) {
    {
      QMutexLocker locker(&mutex);
      log.append(msg);
    }
    cond.wakeAll();
  }
  void flush() {
    QMutexLocker locker(&mutex);
    QByteArray utf8;
    while (log.count() > 0)
      utf8 += (log.takeFirst() + "\n").toUtf8();
    fwrite(utf8.data(), utf8.length(), 1, stdout);
    fflush(stdout);
  }
};

/// global logger, use static destruction to flush the log!
static LoggerThread logger;

void qFlushOutput() {
  logger.flush();
}

void qColorMessageOutput(QtMsgType type, const QMessageLogContext& context,
                         const QString& msg) {
  // const QByteArray localMsg = msg.toLocal8Bit();
  const bool tty = isatty(fileno(stdout));

  char typeCode = '\0';
  const char* color = VT_WHT;
  const char* reset = VT_RESET;

  QString threadContext;
  if (qMessageContext.hasLocalData())
    threadContext = qMessageContext.localData();

  switch (type) {
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

  if (msg.contains("<PL>")) // progress line
    color = VT_CYN;

  if (!tty) {
    color = "";
    reset = "";
  }

  QStringList filteredClasses = {};

  if (typeCode) {
    QString shortFunction = context.function;

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
      //fprintf(stdout, "%s\n", qPrintable(shortFunction));

      shortFunction = parts.back().trimmed();
      //fprintf(stdout, "%s\n", qPrintable(shortFunction));
      parts.pop_back();
      parts = parts.join("").split(" ");
      if (parts.length() > 1) { // case (1) ignored
        QString className = parts.back();
        shortFunction = className + "::" + shortFunction;
        if (filteredClasses.contains(className)) return;
      }
    } else {
      shortFunction = shortFunction.split(" ").back();   // drop return type
    }

    // we can crash the app to help locate a log message
    static QString debugTrigger;
    static bool triggerCheck = false;
    if (!debugTrigger.isNull()) {
      if (msg.contains(debugTrigger)) {
        const char* color = tty ? VT_RED : "";
        qFlushOutput();
        fprintf(stdout, "%s[X][%s:%d] debug message matched: %s\n%s", color,
                QT_MESSAGELOG_FUNC, QT_MESSAGELOG_LINE, qPrintable(msg), reset);
        fflush(stdout);
        char* ptr = nullptr;
        *ptr = 0;
      }
    } else if (!triggerCheck) {
      // QProcessEnvironment::systemEnvironment() crashed once, maybe
      // not reentrant
      triggerCheck = true;
      static QMutex mutex;
      QMutexLocker locker(&mutex);

      auto env = QProcessEnvironment::systemEnvironment();
      if (env.contains("DEBUG_MSG")) {
        debugTrigger = env.value("DEBUG_MSG");
        const char* color = tty ? VT_RED : "";
        qFlushOutput();
        fprintf(stdout, "%s[X] debug message enabled: %s\n%s", color,
                qPrintable(debugTrigger), reset);
        fflush(stdout);
      }
    }

    if (!threadContext.isNull()) shortFunction += "{" + threadContext + "}";

    QString logLine;
    if (msg.startsWith("<NC>")) // no context
      logLine = QString("%1%2%3")
                      .arg(color)
                      .arg(msg.midRef(4))
                      .arg(reset);
    else
      logLine = QString("%1[%2][%3] %4%5")
                          .arg(color)
                          .arg(typeCode)
                          .arg(shortFunction)
                          .arg(msg)
                          .arg(reset);

    logger.append(logLine);

    if (type == QtFatalMsg) { // we are going to abort() next or debug break
      qFlushOutput();
      fprintf(stdout, "\n\n");
    }

    // logLine += "\n";
    //auto buf = logLine.toUtf8();
    //fwrite(buf, buf.length(), 1, stdout);

    // fprintf(stdout, "%s[%c][%s] %s%s\n", color, typeCode,
    //        qPrintable(shortFunction), localMsg.constData(), reset);
  }
}
