/* Trying to cleanup CLI loop WIP
   Copyright (C) 2023 scrubbbbs
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
#include "commands.h"

#include <opencv2/core/core.hpp>

#include "database.h"
#include "engine.h"
#include "qtutil.h"
#include "videocontext.h"

/// <expression> parser and evaluator
class Expression {
 private:
  std::function<bool(const QVariant&, const QVariant&)> _operator;  // binary or unary (rhs ignored)
  QVariant _rhs;                                                    // right hand side of operator
  bool _rhsIsNeedle = false;                                        // rhs contains "%needle"
  QString _opToken;                                                 // token-value of the operator

  Expression() = delete;

  void parseBinaryExpression(const QString& valueExp, const QMetaType& lhsType) {
    int valueOffset=0;
    if (valueExp.startsWith("==")) {
      valueOffset = 2;
      _operator = [](const QVariant& lhs, const QVariant& rhs) { return lhs == rhs; };
    } else if (valueExp.startsWith("!=")) {
      valueOffset = 2;
      _operator = [](const QVariant& lhs, const QVariant& rhs) { return lhs != rhs; };
    } else if (valueExp.startsWith("<=")) {
      valueOffset = 2;
      _operator = [](const QVariant& lhs, const QVariant& rhs) { return lhs <= rhs; };
    } else if (valueExp.startsWith(">=")) {
      valueOffset = 2;
      _operator = [](const QVariant& lhs, const QVariant& rhs) { return lhs >= rhs; };
    } else if (valueExp.startsWith("=")) {
      valueOffset = 1;
      _operator = [](const QVariant& lhs, const QVariant& rhs) { return lhs == rhs; };
    } else if (valueExp.startsWith("<")) {
      valueOffset = 1;
      _operator = [](const QVariant& lhs, const QVariant& rhs) { return lhs < rhs; };
    } else if (valueExp.startsWith(">")) {
      valueOffset = 1;
      _operator = [](const QVariant& lhs, const QVariant& rhs) { return lhs > rhs; };
    } else if (valueExp.startsWith("~")) {
      valueOffset = 1;
      _operator = [](const QVariant& lhs, const QVariant& rhs) { return lhs.toString().contains(rhs.toString()); };
    } else if (valueExp.startsWith("!")) {
      valueOffset = 1;
      _operator = [](const QVariant& lhs, const QVariant& rhs) { return !lhs.toString().contains(rhs.toString()); };
    } else {
      _operator = [](const QVariant& lhs, const QVariant& rhs) { return lhs == rhs; };
    }

    _opToken = valueExp.mid(0, valueOffset);

    QString constant = valueExp.mid(valueOffset).trimmed();
    _rhsIsNeedle = constant == "%needle";

    if (!_rhsIsNeedle) {
      _rhs = QVariant(constant);
      if (lhsType.isValid() && !_rhs.convert(lhsType))
        qFatal("in expression \"%s\", constant \"%s\" is not convertable to \"%s\"",
               qUtf8Printable(valueExp), qUtf8Printable(constant), lhsType.name());
    }
  }

  enum {
    BooleanEnd = -1, // placeholder
    BooleanAnd = 0,
    BooleanOr = 1
  };

  void parseBooleanExpression(const QString expr, const QMetaType& lhsType,
                             const QRegularExpression& regex, QRegularExpressionMatch& match) {
    std::vector<std::pair<Expression, int>> booleanExp;
    QString subExpr = expr;
    do {
      Expression op(match.captured(1).trimmed(), lhsType);
      QString booleanOp = match.captured(2);
      int boolean = match.captured(2) == "&&" ? BooleanAnd : BooleanOr;
      booleanExp.push_back({op, boolean});
      subExpr = subExpr.mid(match.capturedLength());
      match = regex.match(subExpr);
    } while (match.hasMatch());

    Expression op(subExpr.trimmed(), lhsType);
    booleanExp.push_back({op, BooleanEnd});

    _rhsIsNeedle = false;
    for (auto& p : booleanExp) _rhsIsNeedle |= p.first.rhsIsNeedle();

    _operator = [booleanExp](const QVariant& lhs, const QVariant& rhs) {
      bool lhsResult = false;  // lhs of boolean operator
      int boolean;             // && or ||
      for (size_t i = 0; i < booleanExp.size(); ++i) {
        if (i > 0) {
          if (boolean == BooleanAnd && !lhsResult)
            return false;
          else if (boolean == BooleanOr && lhsResult)
            return true;
        }

        auto& p = booleanExp.at(i);
        const Expression& op = p.first;
        lhsResult = op.rhsIsNeedle() ? op.eval(lhs, rhs) : op.eval(lhs);
        boolean = p.second;
      }
      return lhsResult;  // rhs of the last boolean
    };
  }

 public:
  Expression(const QString& expr, const QMetaType& lhsType) {
    if (expr.isEmpty())
      qFatal("empty expression, use %%empty or %%null to test for empty/null value");

    if (!lhsType.isValid())
      qWarning("left-hand-side datatype for \"%s\" is unknown, a type conversion may be required, "
          "e.g. exif#Photo.DateTimeOriginal#todate",
          qUtf8Printable(expr));

    const QRegularExpression regex(qq("^(.+?)(&&|\\|\\|)")); // a&&[...] a||[...]
    QRegularExpressionMatch match = regex.match(expr);
    if (match.hasMatch()) {
      _opToken = expr;
      parseBooleanExpression(expr, lhsType, regex, match);
      return;
    }

    // unary expression
    _opToken = expr;
    if (expr == "%null")
      _operator = [](const QVariant& lhs, const QVariant& rhs) { (void)rhs; return lhs.isNull(); };
    else if (expr == "!%null")
      _operator = [](const QVariant& lhs, const QVariant& rhs) { (void)rhs; return !lhs.isNull(); };
    else if (expr == "%empty")
      _operator = [](const QVariant& lhs, const QVariant& rhs) { (void)rhs; return lhs.toString().isEmpty(); };
    else if (expr == "!%empty")
      _operator = [](const QVariant& lhs, const QVariant& rhs) { (void)rhs; return !lhs.toString().isEmpty(); };
    else if (expr.startsWith(":")) {
      const QRegularExpression re(expr.mid(1));
      if (!re.isValid())
        qFatal("invalid regular expression: %s at offset %lld", qPrintable(re.errorString()),
               re.patternErrorOffset());
      _operator = [re](const QVariant& lhs, const QVariant& rhs) {
        (void)rhs;
        return re.match(lhs.toString()).hasMatch();
      };
    } else
      parseBinaryExpression(expr, lhsType);
  }

  /// if true, then exec() must supply rhs value from the needle
  bool rhsIsNeedle() const { return _rhsIsNeedle; }

  /// get the operator token if applicable (for logging)
  const QString& opToken() const { return _opToken; }

  const QVariant& rhs() const { return _rhs; }
  bool eval(const QVariant& lhs) const { return eval(lhs, rhs()); }
  bool eval(const QVariant& lhs, const QVariant& rhs) const { return _operator(lhs, rhs); }
};

QString Commands::nextArg() {
  if (_args.count() > 0) return _args.takeFirst();
  qCritical() << _switch << "requires additional argument(s)";
  ::exit(1);
}

int Commands::intArg() {
  bool ok;
  int val = nextArg().toInt(&ok);
  if (ok) return val;
  qCritical() << _switch << "requires an integer value";
  ::exit(1);
}

QStringList Commands::optionList() {
  QStringList list;
  while (_args.count() > 0 && !_args.first().startsWith(lc('-')))
    list += _args.takeFirst();
  if (list.isEmpty()) {
    qCritical() << _switch << "expects one or more arguments";
    ::exit(1);
  }
  return list;
}

void Commands::filter(const std::vector<Filter>& filters) const {

  // helpful info stuffed into "filter" attribute
  const auto filterInfo = [](const char* withName,
                       const QVariant& lhs, const Expression& exp,
                      const QVariant& needleValue) {
    QVariant rhs = exp.rhs();
    if (exp.rhsIsNeedle())
      rhs = needleValue;

    return QString("%1 %2(%3) %4 %5(%6)")
        .arg(withName)
        .arg(lhs.typeName())
        .arg(lhs.toString())
        .arg(exp.opToken())
        .arg(rhs.typeName())
        .arg(rhs.toString());
  };

  // the "filter" attribute sets if we keep the item
  for (auto& m : _selection)
    m.unsetAttribute("filter");
  for (auto& g : _queryResult)
    for (auto& m : g)
      m.unsetAttribute("filter");

  for (auto& filter : filters) {
    const QString& key = std::get<0>(filter);
    const QString& valueExp = std::get<1>(filter);
    bool without = std::get<2>(filter);

    const auto getValue = Media::propertyFunc(key);
    const Expression op(valueExp, getValue(Media()).metaType());
    const char* withName = without ? "without" : "with";

    // some properties require readMetadata()
    bool usesMetadata = false;
    if (Media::isExternalProperty(key)) usesMetadata = true;

    if (_selection.count() > 0) {
      if (op.rhsIsNeedle())
        qFatal("compare with %%needle is only supported in group lists (-similar*,-dups*,-group-by)");

      QAtomicInteger<int> count;
      auto future = QtConcurrent::map(_selection, [&](Media& m) {
        if (m.attributes().contains("filter")) return; // filtered by previous iteration
        if (usesMetadata) m.readMetadata();
        QVariant lhs = getValue(m);
        if (without ^ op.eval(lhs)) {
          auto info = filterInfo(withName, lhs, op, QVariant());
          m.setAttribute("filter", info);
          count++;
        }
      });

      const auto progress = [&]() {
        int percent = future.progressValue() * 100 / future.progressMaximum();
        qInfo().nospace().noquote() << "{" << withName << " " << key << " " << valueExp
                                    << "} <PL>" << percent << "% matched " << count.loadRelaxed();
      };
      while (future.isRunning()) {
        QThread::msleep(100);
        progress();
      }
      progress();  // always show 100%
    }

    if (_queryResult.count() > 0) {
      QAtomicInteger<int> count;
      auto future = QtConcurrent::map(_queryResult, [&](MediaGroup& g) {
        if (Q_UNLIKELY(g.count() < 1)) return;
        g[0].setAttribute("filter", "*needle*");  // never filter needle

        if (usesMetadata) g[0].readMetadata();
        const QVariant needleValue = getValue(g[0]);  // compare to the needle's value

        for (int i = 1; i < g.count(); ++i) {
          if (usesMetadata) g[i].readMetadata();
          QVariant lhs = getValue(g[i]);
          bool result = op.rhsIsNeedle() ? op.eval(lhs, needleValue) : op.eval(lhs);
          if (without ^ result) {
            auto info = filterInfo(withName, lhs, op, needleValue);
            g[i].setAttribute("filter", info);
            count++;
          }
        }
      });

      const auto progress = [&]() {
        int percent = future.progressValue() * 100 / future.progressMaximum();
        qInfo().nospace().noquote() << "{" << withName << " " << key << " " << valueExp
                                    << "} <PL>" << percent << "% matched " << count.loadRelaxed();
      };
      while (future.isRunning()) {
        QThread::msleep(100);
        progress();
      }
      progress();  // always show 100%
    }
  }
  if (_selection.count() > 0) {
    MediaGroup tmp;
    for (const Media& m : qAsConst(_selection))
      if (m.attributes().contains("filter"))
        tmp.append(m);

    _selection = tmp;
  }

  if (_queryResult.count() > 0) {
    MediaGroupList tmp;
    for (auto& g : _queryResult) {
      MediaGroup filtered;
      for (auto& m : g)
        if (m.attributes().contains("filter"))
          filtered.append(m);

      if (filtered.count() > 1)
        tmp.append(filtered);
    }
    _queryResult = tmp;
  }
}

void Commands::rename(Database* db, const QString& srcPat, const QString& dstPat,
                      const QString& options) {
  const QRegularExpression re(srcPat);
  if (!re.isValid())
    qFatal("rename: <find> pattern <%s> is illegal regular expression: %s at offset %lld",
           qUtf8Printable(srcPat), qPrintable(re.errorString()), re.patternErrorOffset());

  int pad = int(log10(double(_selection.count()))) + 1;
  int num = 1;

  bool findReplace = false;
  if (!dstPat.contains("#")) {
    findReplace = true;
    qInfo() << "rename: no captures in <replace> pattern, using substring find/replace";
  }

  for (int i = 1; i < re.captureCount(); ++i)
    if (!dstPat.contains("#" + QString::number(i)))
      qCritical("rename: capture #%d is discarded", i);

  for (int i = re.captureCount() + 1; i < re.captureCount() + 10; ++i)
    if (dstPat.contains("#" + QString::number(i)))
      qCritical("rename: capture reference (#%d) with no capture", i);

  QStringList newNames;
  MediaGroup toRename;
  for (const Media& m : _selection) {
    if (m.isArchived()) {
      qWarning() << "rename: cannot rename archived file:" << m.path();
      continue;
    }

    const bool matchPath = options.indexOf("p") >= 0;
    const QFileInfo info(m.path());
    QString oldName = info.completeBaseName();
    if (matchPath) {
      QStringList relParts = m.path().mid(db->path().length() + 1).split("/");
      relParts.removeLast();
      oldName = relParts.join("/") + "/" + oldName;
    }

    if (info.suffix().isEmpty()) {
      qWarning() << "rename: no file extension:" << m.path();
      continue;
    }

    QString newName;
    if (findReplace) {
      newName = oldName;
      newName.replace(QRegularExpression(srcPat), dstPat);
      if (newName.contains("%n"))
        newName.replace("%n", QString("%1").arg(num, pad, 10, QChar('0')));
      else if (newName == oldName) {
        if (options.indexOf("v") >= 0)
          qWarning("rename: <find> text (%s) doesn't match: <%s>", qUtf8Printable(srcPat),
                   qUtf8Printable(oldName));
        continue;
      }
    } else {
      newName = dstPat;

      QRegularExpressionMatch match = re.match(oldName);
      if (!match.hasMatch()) {
        if (options.indexOf("v") >= 0)
          qWarning("rename: <find> regexp <%s> does not match: <%s>", qUtf8Printable(srcPat),
                   qUtf8Printable(oldName));
        continue;
      }
      QStringList captured = match.capturedTexts();
      for (int i = 0; i < captured.size(); ++i) {
        QString placeholder = "#" + QString::number(i);
        newName = newName.replace(placeholder, captured[i]);
      }

      if (newName.contains("%n"))
        newName.replace("%n", QString("%1").arg(num, pad, 10, QChar('0')));
    }

    {
      struct Replacement {
        int start, end;
        QString text;
      };
      QVector<Replacement> replacements;

      int funcOpen = newName.indexOf("{");
      int funcClose = newName.indexOf("}", funcOpen + 1);
      while (funcOpen >= 0 && (funcClose - funcOpen) > 1) {
        const QStringList funcs = newName.mid(funcOpen + 1, funcClose - funcOpen - 1).split(":");
        QVariant result;
        if (funcs.count() == 2)
          result = (Media::unaryFunc(funcs[1]))(funcs[0]);
        else if (funcs.count() == 1 && !funcs[0].isEmpty())
          result = (Media::propertyFunc(funcs[0]))(m);
        else
          qFatal("rename: invalid syntax between {}, expected {arg:<func>} or {<prop>[#<func>]}");

        replacements.append({funcOpen, funcClose + 1, result.toString()});
        funcOpen = newName.indexOf("{", funcClose + 1);
        funcClose = newName.indexOf("}", funcOpen + 1);
      }
      while (!replacements.empty()) {
        auto r = replacements.takeLast();
        newName = newName.replace(r.start, r.end - r.start, r.text);
      }
    }

    newName += "." + info.suffix();
    if (newName.contains("/"))  // fixme: add proper set
      qFatal("rename: new filename contains illegal characters %s -> <%s>",
             qUtf8Printable(m.path()), qUtf8Printable(newName));

    const QString newPath = QFileInfo(info.dir().absolutePath()).absoluteFilePath() + "/" + newName;

    if (newNames.contains(newPath))
      qWarning("rename: collision: %s,%s => %s",
               qUtf8Printable(toRename[newNames.indexOf(newPath)].path()), qUtf8Printable(oldName),
               qUtf8Printable(newName));
    else if (info.dir().exists(newName))
      qWarning("rename: new name will overwrite: %s -> %s", qUtf8Printable(m.path()),
               qUtf8Printable(newName));
    else {
      newNames.append(newPath);
      toRename.append(m);
      num++;
    }
  }

  Q_ASSERT(newNames.count() == toRename.count());

  for (int i = 0; i < toRename.count(); ++i) {
    auto m = toRename[i];
    qDebug() << m.path() << "->" << newNames[i];
    if (options.indexOf("x") < 0) continue;
    if (!db->rename(m, newNames[i].split("/").back()))
      qFatal("rename failed, maybe index is stale...");
  }

  if (options.indexOf("x") >= 0)
    qInfo() << "renamed" << toRename.count() << ", skipped"
            << _selection.count() - toRename.count();

  _selection.clear();
}

void Commands::selectFiles() {
  static const Scanner* sc = new Scanner; // pointer to avoid static destruction
  while (_args.count() > 0) {
    const QString arg = _args.front();
    if (arg.startsWith("-"))  // next switch
      break;
    _args.pop_front();

    const QFileInfo info(arg);
    if (!Media::isArchived(arg) && !info.exists()) {
      qWarning() << "select-files: file not found:" << arg;
      continue;
    }
    if (info.isDir()) {
      qDebug() << "selected-files: listing dir:" << arg;
      const auto paths = QDir(arg).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                                             QDir::DirsFirst | QDir::Reversed);
      for (auto& path : paths)
        if (path != INDEX_DIRNAME) _args.push_front(arg + "/" + path);
      continue;
    }

    QString ext = info.suffix().toLower();

    if (sc->archiveTypes().contains(ext)) {
      const auto list = Media::listArchive(arg);
      QStringList zippedFiles;
      for (auto& path : list) zippedFiles.append(path);

      std::sort(zippedFiles.begin(), zippedFiles.end(), std::greater<QString>());
      for (auto& path : qAsConst(zippedFiles)) _args.push_front(path);
      continue;
    }

    int type = 0;
    if (sc->imageTypes().contains(ext))
      type = Media::TypeImage;
    else if (sc->videoTypes().contains(ext))
      type = Media::TypeVideo;
    else
      qWarning() << "select-files: unknown file type:" << arg;

    if (type) _selection.append(Media(info.absoluteFilePath(), type));
  }
}

void Commands::verify(Database* db, const QString& jpegFixPath) {
  // note: hashes will not match when files get overwritten (rename tool
  // bypassed)
  if (_indexParams.indexThreads > 0)
    QThreadPool::globalInstance()->setMaxThreadCount(_indexParams.indexThreads);

  QAtomicInt okCount;
  QAtomicInteger<qint64> totalBytesRead;
  qint64 startTime = QDateTime::currentMSecsSinceEpoch();

  auto hashFunc = [db, &okCount, &totalBytesRead](const Media& m) {
    qint64 bytesRead = 0;
    QString hash = Scanner::hash(m.path(), m.type(), &bytesRead);
    bool match = hash == m.md5();
    // qDebug() << m.path();
    if (!match)
      qCritical() << "file hash changed:" << m.path().mid(db->path().length() + 1)
                  << "current:" << hash << "stored:" << m.md5();
    else
      okCount.ref();

    totalBytesRead += bytesRead;
  };

  // to avoid thrashing, large files are hashed sequentially
  // todo: setting for anti-thrash file size
  const qint64 largeFileSize = 16 * 1024 * 1024;

  for (const auto& m : _selection)
    if (!m.isArchived() && QFileInfo(m.path()).size() >= largeFileSize) hashFunc(m);

  QFuture<void> f = QtConcurrent::map(
      _selection.constBegin(), _selection.constEnd(), [&hashFunc](const Media& m) {
        if (m.isArchived() || QFileInfo(m.path()).size() < largeFileSize) hashFunc(m);
      });
  f.waitForFinished();

  // since every file was touched, good time to
  // do repairs and other maintenance
  {
    QMutexLocker locker(Scanner::staticMutex());
    const auto errors = Scanner::errors();
    for (auto it = errors->begin(); it != errors->end(); ++it)
      if (it.value().contains(Scanner::ErrorJpegTruncated)) {
        QString path = it.key();
        if (!Media(path).isArchived()) {
          path = path.replace("\"", "\\\"");
          QString cmd = QString("%1 \"%2\"").arg(jpegFixPath).arg(path);
          if (0 != system(qUtf8Printable(cmd))) qWarning() << "jpeg repair script failed";
        }
      }
  }

  int numOk = okCount.loadRelaxed();
  qint64 endTime = QDateTime::currentMSecsSinceEpoch();
  qint64 mb = totalBytesRead.loadRelaxed() / 1024 / 1024;
  float hashRate = 1000.0f * mb / (endTime - startTime);

  qInfo() << "verified" << numOk << "/" << _selection.count() << "," << mb << "MB," << hashRate
          << "MB/s";
  int status = 0;
  if (numOk == _selection.count()) status = 1;
  exit(status);
}

void Commands::testVideoDecoder(const QString& path) {
  VideoContext::DecodeOptions opt;
  opt.gpu = _indexParams.useHardwareDec;
  opt.threads = _indexParams.decoderThreads;
  opt.maxH = 128;  // 128 is used for video hashing
  opt.maxW = 128;

  bool display = false, loop = false, scale = false, crop = false, zoom = false, noSws = false;
  while (_args.count() > 0) {
    const QString arg = nextArg();
    if (arg == "-show") {
      display = true;
      scale = true;
    } else if (arg == "-loop")
      loop = true;
    else if (arg == "-gray")
      opt.gray = 1;
    else if (arg == "-maxw")
      opt.maxW = intArg();
    else if (arg == "-maxh")
      opt.maxH = intArg();
    else if (arg == "-device")
      opt.deviceIndex = intArg();
    else if (arg == "-fast")
      opt.fast = true;
    else if (arg == "-scale")
      scale = true;
    else if (arg == "-unscaled") {
      opt.maxH = 0;
      opt.maxW = 0;
    } else if (arg == "-crop") {
      crop = true;
      scale = true;
    } else if (arg == "-zoom") {
      zoom = true;
    } else if (arg == "-no-sws") {
      noSws = true;
    } else
      qFatal("unknown arg to -test-video-decoder");
  }

  int numFrames = 0;
  qint64 then;

  auto timing = [&]() {
    numFrames++;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - then > 1000) {
      qInfo() << numFrames * 1000.0f / (now - then) << "frames/second";
      then = now;
      numFrames = 0;
    }
  };

  static bool quit = false;
  class CloseFilter : public QObject {
   public:
    bool eventFilter(QObject* obj, QEvent* event) override {
      (void)obj;
      if (event->type() == QEvent::Close ||
          (event->type() == QEvent::KeyPress &&
           static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)) {
        qInfo() << "quit event";
        quit = true;
      }
      return false;
    }
  };

  QLabel* label = nullptr;
  int zoomSize = opt.maxH * 10;
  if (display) {
    if (!qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR"))
      qWarning() << "display scaling is enabled, may introduce artifacts";

    QWidget* window = new QWidget;
    QLayout* layout = new QHBoxLayout(window);
    layout->addItem(new QSpacerItem(1, 1, QSizePolicy::Expanding));
    label = new QLabel(window);
    label->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    label->setAttribute(Qt::WA_OpaquePaintEvent);
    label->setScaledContents(false);
    layout->addWidget(label);
    layout->addItem(new QSpacerItem(1, 1, QSizePolicy::Expanding));
    layout->setSpacing(0);
    QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
    QRect windowRect;
    if (zoom) {
      windowRect = screenRect;
      zoomSize = int(screenRect.height() * 0.95 / opt.maxH) * opt.maxH;
      qInfo() << "zoom in (nearest neighbor) :" << zoomSize;
    } else {
      VideoContext video;
      Q_ASSERT(0 == video.open(path, opt));
      QImage img;
      video.nextFrame(img);
      windowRect.setWidth(img.width());
      windowRect.setHeight(img.height());
      windowRect.moveCenter(screenRect.center());
    }
    window->setGeometry(windowRect);
    window->setContentsMargins(0, 0, 0, 0);
    window->installEventFilter(new CloseFilter);
    window->show();
  }

  do {
    VideoContext video;
    Q_ASSERT(0 == video.open(path, opt));
    then = QDateTime::currentMSecsSinceEpoch();
    numFrames = 0;
    if (scale) {
      QImage img;
      QImage out;
      while ((noSws ? video.decodeFrame() : video.nextFrame(img))) {
        if (quit) ::exit(0);
        if (crop) {
          cv::Mat m1;
          qImageToCvImg(img, m1);
          autocrop(m1);
          cvImgToQImage(m1, out);
        } else
          out = img;
        if (display) {
          if (zoom)
            out = out.scaled(zoomSize, zoomSize, Qt::KeepAspectRatio, Qt::FastTransformation);
          label->setGeometry(0, 0, out.width(), out.height());
          label->setPixmap(QPixmap::fromImage(out));
          qApp->processEvents();  // repaint
        }
        timing();
      }
    } else
      while (video.decodeFrame()) timing();
    video.seekFast(0);
  } while (loop);
}

void Commands::testImageSearch(Engine& engine) {
  const int sizeHt = 128;
  struct {
    QString id;
    SearchParams params;
    QSize size;
    ImageLoadOptions loadOptions;
  } test[4];

  // params.inSet = true;
  // params.set = selection;

  test[0].id = "islow";   // standard image reader + qt scaler
  test[1].id = "ifast";   // ifast jpeg decode + qt scaler
  test[2].id = "iscale";  // idct scaler + qt scaler
  test[3].id = "i150";    // idct scaler only

  for (int i : {0, 1, 2, 3}) test[i].params = _searchParams;
  for (int i : {0, 1, 2}) test[i].size = QSize(0, sizeHt);
  for (int i : {1, 2, 3}) test[i].loadOptions.fastJpegIdct = true;

  test[2].loadOptions.readScaled = true;
  test[2].loadOptions.minSize = sizeHt;
  test[2].loadOptions.maxSize = sizeHt * 1.5;
  test[3].loadOptions = test[2].loadOptions;

  for (int algo : {0, 1, 2})
    for (auto t : qAsConst(test)) {
      t.params.algo = algo;
      const int startMs = QDateTime::currentMSecsSinceEpoch();
      engine.scanner->setIndexParams(_indexParams);
      QList<QFuture<MediaSearch>> jobs;
      for (auto& m : _selection) {
        Q_ASSERT(m.isValid());

        jobs += QtConcurrent::run([m, &t, &engine]() {
          QString path = "@" + m.name() + ":" + QString(t.id);

          auto io = m.ioDevice();
          Q_ASSERT(io->open(QIODevice::ReadOnly));
          QImage img = m.loadImage(io->readAll(), t.size, m.name(), nullptr, t.loadOptions);
          delete io;

          Q_ASSERT(!img.isNull());
          if (t.size != QSize())
            Q_ASSERT(img.height() == t.size.height());
          else if (t.loadOptions.readScaled)
            Q_ASSERT(qMax(img.width(), img.height()) >= t.loadOptions.minSize);

          IndexResult r = engine.scanner->processImage(path, m.name(), img);
          Q_ASSERT(r.ok);

          MediaSearch s;
          s.params = t.params;
          s.needle = r.media;
          s = engine.query(s);
          s.needle.setId(m.id());  // to identify the correct match
          return s;
        });
      }

      int hit = 0, miss = 0;
      int totScore = 0, minScore = INT_MAX, maxScore = INT_MIN;
      int totDist = 0, minDist = INT_MAX, maxDist = INT_MIN;
      while (jobs.count()) {
        auto job = jobs.front();
        jobs.pop_front();
        job.waitForFinished();
        MediaSearch s = job.result();
        int distance = -1, score = -1;
        for (int i = 0; i < s.matches.count(); ++i)
          if (s.matches[i].id() == s.needle.id()) {
            distance = i;
            score = s.matches[i].score();
          }
        // qDebug() << m.path() << sz << distance << score;

        if (distance < 0) {
          miss++;
          // s.needle.setImage(img);
          // s.matches.prepend(s.needle);
          // MediaBrowser::show({s.matches}, MediaBrowser::ShowNormal, widgetOptions);
        } else {
          hit++;
          totScore += score;
          minScore = qMin(minScore, score);
          maxScore = qMax(maxScore, score);
          totDist += distance;
          minDist = qMin(minDist, distance);
          maxDist = qMax(maxDist, distance);
        }
        const int endMs = QDateTime::currentMSecsSinceEpoch();
        int elapsed = (endMs - startMs) / 1000.0 + 1;
        qWarning("<NC>| %s/%d/%d<PL> | %6d | %6d | %6d (%.4f%%) | %.4f/%d/%d | %.4f/%d/%d       ",
                 qPrintable(t.id), t.size.height(), t.params.algo, elapsed, hit, miss,
                 miss * 100.0 / (hit + miss), totScore * 1.0 / hit, minScore, maxScore,
                 totDist * 1.0 / hit, minDist, maxDist);
      }

    }  // for each test
}

void Commands::testVideoIndex(Engine& engine, const QString& path) {
  VideoContext vc;
  VideoContext::DecodeOptions opt;
  // settings used by indexer, maybe higher hit rate
  opt.maxW = 128;
  opt.maxH = 128;
  opt.gray = true;
  opt.gpu = _indexParams.useHardwareDec;
  opt.threads = _indexParams.decoderThreads;

  if (0 != vc.open(path, opt)) {
    qWarning("failed to open");
    return;
  }

// #define SHOWFRAMES
// #define DECODETEST
#ifdef SHOWFRAMES
  QLabel* label = nullptr;
  QImage qImg;
#endif
#ifdef DECODETEST
  int numFrames = 0;

  while (vc.nextFrame(qImg)) {
    numFrames++;
    printf("%d\r", numFrames);
    fflush(stdout);
#  ifdef SHOWFRAMES
    if (!label) label = new QLabel;
    label->setPixmap(QPixmap::fromImage(qImg));
    label->show();
    qApp->processEvents();
#  endif
  }
  printf("videotest: video contains %d frames\n", numFrames);

  continue;
#endif

#define USE_THREADS 1

#if USE_THREADS
  QList<QFuture<MediaSearch>> work;
#endif
  QString absPath = QFileInfo(path).absoluteFilePath();
  QString results;
  int sumDistance = 0, minDistance = INT_MAX, maxDistance = INT_MIN;
  cv::Mat img;
  int srcFrame = -1;
  QVector<int> rangeError;
  while (vc.nextFrame(img)) {
    srcFrame++;
    {
#ifdef SHOWFRAMES
      cvImgToQImage(img, qImg);
      if (!label) label = new QLabel;
      label->setPixmap(QPixmap::fromImage(qImg));
      label->show();
      qApp->processEvents();
#endif
    }

#if USE_THREADS
    cv::Mat imgCopy = img.clone();
    work.append(QtConcurrent::run([this, &engine, srcFrame, imgCopy]() {
      cv::Mat img = imgCopy;
      autocrop(img, 20);  // same as indexer
      uint64_t hash = dctHash64(img);

      Media m("", Media::TypeImage, img.cols, img.rows, "md5", hash);
      MediaSearch search;
      m.setMatchRange({srcFrame, -1, 0});
      search.needle = m;
      search.params = _searchParams;

      return engine.query(search);
    }));
#else

    autocrop(img, 20);
    uint64_t hash = phash64(img);

    Media m("", Media::TypeImage, img.cols, img.rows, "md5", hash);
    MediaSearch search;
    search.needle = m;
    search.params = params;

    search = engine().query(search);
#endif

#if USE_THREADS
    while (work.count() > 0) {
      auto& future = work[0];
      if (!future.isFinished()) break;

      MediaSearch search = future.result();
      work.removeFirst();
#endif
      char status = 'n';
      int matchIndex = -1;
      if (search.matches.count() > 0) {
        matchIndex = Media::indexInGroupByPath(search.matches, absPath);
        if (matchIndex == 0)
          status = 'Y';
        else if (matchIndex > 0)
          status = 'p';
        else
          status = '0';
      }
      results += status;

      if (matchIndex >= 0) {
        auto range = search.matches[matchIndex].matchRange();
        int distance = abs(range.srcIn - range.dstIn);
        sumDistance += distance;
        rangeError.append(distance);
        minDistance = qMin(minDistance, distance);
        maxDistance = qMax(maxDistance, distance);
        // printf("%d %d\n", range.srcIn, range.dstIn);
      }

      qFlushMessageLog();
      printf("%c", status);
      // fflush(stdout);
#if USE_THREADS
    }
#endif

  }  // while nextFrame

  // adjust for vpad
  results = results.mid(_searchParams.skipFrames, results.length() - 2 * _searchParams.skipFrames);

  int found, bad, poor, none;
  found = bad = poor = none = 0;

  for (int i = 0; i < results.length(); i++) switch (results[i].toLatin1()) {
      case 'Y':
        found++;
        break;
      case 'p':
        poor++;
        break;
      case '0':
        bad++;
        break;
      default:
        none++;
    }

  int frames = results.length();

  std::sort(rangeError.begin(), rangeError.end());

  qFlushMessageLog();

  printf("\nframes=%d found=%.3f%% poor=%.3f%% bad=%.3f%% none=%.3f%%\n", frames,
         found * 100.0 / frames, poor * 100.0 / frames, bad * 100.0 / frames,
         none * 100.0 / frames);
  printf("range error (frames): mean=%.3f, min=%d, max=%d, median=%d\n\n",
         double(sumDistance) / (found + poor), minDistance, maxDistance,
         rangeError[rangeError.length() / 2]);
}

void Commands::testUpdate(Engine& engine) {
  QDialog* d = new QDialog();
  QPushButton* b = new QPushButton(d);
  QPushButton* c = new QPushButton(d);
  QPushButton* f = new QPushButton(d);

  b->setText("Start Update");
  c->setText("Stop Update");
  f->setText("Finish Update");

  QObject::connect(engine.scanner, &Scanner::scanCompleted, [=] {
    qDebug() << "\n\nscan completed";
    b->setText("Start Update");
    c->setText("Stop Update");
    f->setText("Finish Update");
  });

  QObject::connect(b, &QPushButton::pressed, [this, &engine, &b] {
    b->setText("Updating...");
    engine.scanner->setIndexParams(_indexParams);
    engine.update();
  });

  QObject::connect(c, &QPushButton::pressed, [&engine, &c] {
    qDebug() << "\n\nstop update";
    c->setText("Stopping...");
    engine.stopUpdate();
  });

  QObject::connect(f, &QPushButton::pressed, [&engine, &f] {
    qDebug() << "\n\nfinish update";
    f->setText("Finishing...");
    engine.scanner->finish();
  });

  QHBoxLayout* l = new QHBoxLayout(d);
  l->addWidget(b);
  l->addWidget(c);
  l->addWidget(f);
  d->exec();
  delete d;
}

void Commands::testCsv(Engine& engine, const QString& path) {
  QFile csv(path);
  csv.open(QFile::ReadOnly);
  QByteArray line;
  int numImages, numFound;
  numImages = numFound = 0;
  while ("" != (line = csv.readLine())) {
    const QStringList tmp = QString(line).split(";");

    if (tmp.size() < 2) continue;

    numImages++;
    QString src = tmp[0];
    QString dst = tmp[1];
    src.replace("\"", "");
    dst.replace("\"", "");

    qInfo() << "testing " << src << "=>" << dst;

    Media m(src);
    MediaGroup results = engine.db->similarTo(m, _searchParams);

    Media found;
    int i;
    for (i = 0; i < results.size(); i++)
      if (results[i].path() == dst) {
        found = results[i];
        numFound++;
        break;
      }

    m.recordMatch(found, i + 1, results.size());
  }
  qInfo() << "accuracy:" << (numFound * 100.0 / numImages) << "%";
}
