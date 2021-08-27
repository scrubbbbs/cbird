#include "qtutil.h"
#include <QtDBus/QtDBus>

// qttools/src/qdbus/qdbus/qdbus.cpp
#include <QtXml/QtXml>
static QStringList listServiceObjects(QDBusConnection& connection, const QString &service, const QString &path) {
  QStringList objectPaths;

  // make a low-level call, to avoid introspecting the Introspectable interface
  QDBusMessage call = QDBusMessage::createMethodCall(service, path.isEmpty() ? QLatin1String("/") : path,
                                                     QLatin1String("org.freedesktop.DBus.Introspectable"),
                                                     QLatin1String("Introspect"));
  QDBusReply<QString> xml = connection.call(call);
  if (path.isEmpty()) {
    // top-level
    if (xml.isValid()) {
      //printf("/\n");
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
      //printf("%s\n", qPrintable(sub));
      objectPaths.append(sub);
      objectPaths.append(listServiceObjects(connection, service, sub));
    }
    child = child.nextSiblingElement();
  }
  return objectPaths;
}

void DesktopHelper::runProgram(QStringList& args, const QString& inPath,
                               double seek, const QString& inPath2,
                               double seek2) {
  QString path(inPath);
  QString path2(inPath2);
#ifdef Q_OS_WIN
  path.replace("/", "\\");
  path2.replace("/", "\\");
#endif
  for (QString& arg : args) {
    arg.replace("%1", path);
    arg.replace("%2", path2);
    arg.replace("%seek", QString::number(seek));
    arg.replace("%seek2", QString::number(seek2));
    arg.replace("%home", QDir::homePath());
    arg.replace("%dirname(1)", QFileInfo(path).dir().absolutePath());
    arg.replace("%dirname(2)", QFileInfo(path2).dir().absolutePath());
  }

  qDebug() << args;

  if (args.count() > 0) {
    const QString prog = args.first();
    if (prog == "DesktopServices") {
      if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        qWarning() << "failed to open via desktop services";
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
      qDebug() << "DBus services" << QStringList(bus.interface()->registeredServiceNames());
      qDebug() << "Service objects" << paths;

      // path may contain regular expression, the first matching path is taken
      // useful for apps that have randomized path names (krusader)
      auto objectPath = args[2];
      QRegularExpression pathMatch(objectPath);
      for (auto& path : paths)
        if (pathMatch.match(path).hasMatch()) {
          objectPath = path;
          break;
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
    } else {
#ifdef Q_OS_WIN
      QProcess p;
      p.setProgram(prog);
      p.setNativeArguments(args.mid(1).join(" "));
      if (!p.startDetached())
#else
      if (!QProcess::startDetached(prog, args.mid(1)))
#endif
        qWarning() << "process failed to start";
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

void DesktopHelper::revealPath(const QString& path) {
#ifdef Q_OS_WIN
  const QStringList defaultArgs{{"explorer", "/select,\"%1\""}};
#else
  const QStringList defaultArgs{{"/usr/bin/nautilus", "-s", "%1"}};
#endif
  QStringList args = getSetting("OpenFileLocation", defaultArgs).toStringList();
  runProgram(args, path);
}

void DesktopHelper::openVideo(const QString& path, double seekSeconds) {
  QString settingsKey = "OpenVideoSeek";
  QStringList defaultArgs{"mpv", "--start=%seek" "%1"};
  if (abs(seekSeconds) < 0.1) {
    settingsKey = "OpenVideo";
    defaultArgs = QStringList{"mpv", "%1"};
  }

  QStringList args = getSetting(settingsKey, defaultArgs).toStringList();
  runProgram(args, path, seekSeconds);
}

void DesktopHelper::compareAudio(const QString& path1, const QString& path2) {
  const QString settingsKey = "CompareAudio";
  const QStringList defaultArgs{
      {"%home/src/ffscripts/compareaudio.sh", "%1", "%2"}};
  QStringList args = getSetting(settingsKey, defaultArgs).toStringList();
  runProgram(args, path1, 0.0, path2);
}

void DesktopHelper::playSideBySide(const QString& path1, double seek1,
                                   const QString& path2, double seek2) {
  const QString settingsKey = "PlaySideBySide";
  const QStringList defaultArgs{
      {"%home/src/ffscripts/sbs.sh", "%1", "%seek", "%2", "%seek2"}};
  QStringList args = getSetting(settingsKey, defaultArgs).toStringList();
  runProgram(args, path1, seek1, path2, seek2);
}

QString DesktopHelper::settingsFile() {
  const char* file = getenv("SETTINGS_FILE");
  QString path;
  if (file)
    path = file;
  else
    path = qApp->applicationDirPath() + "/settings.ini";

  qDebug() << path;

  return path;
}

QString DesktopHelper::trashDir(const QString& path) {
  (void)path;
  QString trashDir =
      QProcessEnvironment::systemEnvironment().value("INDEX_TRASH_DIR");

  return trashDir;
}

bool DesktopHelper::moveToTrash(const QString& path) {

  QString dir = trashDir(path);
  if (dir.isEmpty()) {
    qWarning("INDEX_TRASH_DIR environment unset or path does not exist");
    return false;
  }

  QFileInfo info(path);
  if (!info.isFile()) {
    qWarning() << "requested path is not a file:" << path;
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

#include <unistd.h>   // isatty

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

void qColorMessageOutput(QtMsgType type, const QMessageLogContext& context,
                            const QString& msg) {
  const QByteArray localMsg = msg.toLocal8Bit();
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
      color = (VT_REVERSE VT_BRIGHT VT_RED);
      break;
  }

  if (!tty) {
    color = "";
    reset = "";
  }

  QStringList filteredClasses = {};

  if (typeCode) {
    QString shortFunction = context.function;
    if (shortFunction.contains("::<lambda"))
      shortFunction = shortFunction.split("::<lambda").front();
    if (shortFunction.contains("::")) {
      // int Foo::bar(int x) const
      // int Foo::bar<float>(int x) const
      // int Foo::bar<float>(int x)::(anonymous class)::operator()()

      QStringList parts = QString(context.function).split("::");
      QString className = "";
      if (parts.length() > 1) {
        // remove return type
        className = parts[0].split(" ").back();

        // remove argument list
        QString args = parts[1].split("(").front();

        // remove trailing things from function/arguments
        // QString args = parts[1].replace("()", "");
        shortFunction = className + "::" + args;
      }

      if (filteredClasses.contains(className)) return;
    } else {
      shortFunction = shortFunction.split("(").front();  // drop arguments
      shortFunction = shortFunction.split(" ").back();   // drop return type
    }

    // we can crash the app to help locate a log message
    static QString debugTrigger;
    static bool triggerCheck = false;
    if (!debugTrigger.isNull()) {
      if (msg.contains(debugTrigger)) {
        const char* color = tty ? VT_RED : "";
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
        fprintf(stdout, "%s[X] debug message enabled: %s\n%s", color,
                qPrintable(debugTrigger), reset);
        fflush(stdout);
      }
    }

    if (!threadContext.isNull())
      shortFunction += "{"+threadContext+"}";

    fprintf(stdout, "%s[%c][%s] %s%s\n", color, typeCode,
            qPrintable(shortFunction), localMsg.constData(), reset);
  }
}
