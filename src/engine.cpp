/* Component integration
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
#include "engine.h"

#include "colordescindex.h"
#include "cvfeaturesindex.h"
#include "database.h"
#include "dctfeaturesindex.h"
#include "dcthashindex.h"
#include "dctvideoindex.h"
#include "qtutil.h"
#include "scanner.h"
#include "templatematcher.h"
#include "videocontext.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

Engine::Engine(const QString& path, const IndexParams& params) {
  db = new Database(path);
  db->addIndex(new DctHashIndex);
  db->addIndex(new DctFeaturesIndex);
  db->addIndex(new CvFeaturesIndex);
  db->addIndex(new DctVideoIndex);
  db->addIndex(new ColorDescIndex);
  db->setup();

  scanner = new Scanner;
  scanner->setIndexParams(params);
  connect(scanner, &Scanner::mediaProcessed, this, &Engine::add);
  connect(scanner, &Scanner::scanCompleted, this, &Engine::commit);

  matcher = new TemplateMatcher;
}

Engine::~Engine() {
  scanner->flush();
  delete matcher;
  delete scanner;
  delete db;
}

void Engine::add(const Media& m) {
  // additions are committed in batches to hide database write latency,
  // this requires clients to call commit() after all items added

  if (_batch.contains(m)) {
    qWarning() << "attempt to add media twice in same batch, discarding..." << m.path();
    return;
  }

  Media copy = m;
  copy.setData(QByteArray());
  copy.setImage(QImage());
  _batch.append(copy);

  // videos take a long time to process so do not batch, and commit immediately
  if (m.type() == Media::TypeVideo || _batch.count() >= scanner->indexParams().writeBatchSize) {
    // printf("w");
    // fflush(stdout);
    commit();
  }
}

void Engine::commit() {
  if (_batch.count() > 0) {
    db->add(_batch);
    // for (const auto& m : qAsConst(_batch))
    //   qDebug() << "added id: " << m.id() << m.path();
    _batch.clear();
  }
}

void Engine::update(bool wait, const QString& dirPath) {
  QElapsedTimer timer;
  timer.start();

  qMessageLogSetRootPath(QDir(dirPath).absolutePath());
  VideoContext::avLoggerSetLogFile(db->indexPath() + "/video-error.log");

  // metadataChangeTime might not work on this filesystem
  bool useMetadataTime = scanner->indexParams().modTime;
  QDateTime timeBefore;
  if (!useMetadataTime) do {
      QString oldName = db->indexPath() + "/modtime-check-before.txt";
      QFile testFile(oldName);
      if (!testFile.open(QFile::WriteOnly)) {
        qWarning() << "cannot verify that modtime works" << testFile.errorString();
        break;
      }
      testFile.close();
      timeBefore = QFileInfo(oldName).metadataChangeTime();
    } while (false);

  // check for missing data for already indexed items
  if (scanner->indexParams().algos & (1 << SearchParams::AlgoVideo)) {
    QVector<int> missingVideos;
    const MediaGroup videos = db->mediaWithType(Media::TypeVideo);
    PROGRESS_LOGGER(pl, "verifying video index:<PL> %percent %step videos", videos.count());
    int i = 0;
    QElapsedTimer timer;
    timer.start();
    for (const Media& m : videos) {
      QString vIndexPath = QString("%1/%2.vdx").arg(db->videoPath()).arg(m.id());
      if (!QFileInfo(vIndexPath).exists()) {
        qWarning() << "missing index for" << m.path();
        missingVideos.append(m.id());
      } else {
        VideoIndex idx;
        if (!idx.isValid(vIndexPath)) {
          qWarning() << "invalid index for" << m.path();
          missingVideos.append(m.id());
        }
      }
      pl.stepRateLimited(i++);
    }
    pl.end(i);
    db->remove(missingVideos);
  }

  // if path is still present after scanning, remove from the index
  QSet<QString> indexedFiles = db->indexedFiles();

  if (false) {
    // if the stored database paths are not canonical there
    // is a bug somewhere, though not fatal it will prevent
    // updating correctly
    // FIXME: this can take a long time, figure out where
    //        the invalid paths actually come from
    QStringList paths = indexedFiles.values(); // sort to reduce random access
    paths.sort();
    QSet<QString> checked;
    int progress = 0;
    for (int i = 0; i < paths.count(); ++i) {
      auto& path = paths[i];
      if (Media::isArchived(path)) Media::archivePaths(path, &path);

      if (checked.contains(path)) continue;  // skip zip members
      checked.insert(path);

      // canonicalFilePath() hits the filesystem, if file does not
      // exist it returns empty string
      const QFileInfo info(path);
      const int prefixLen = db->path().length() + 1;
      const auto relPath = path.mid(prefixLen);
      const auto canPath = info.canonicalFilePath();
      const auto canRelPath = canPath.mid(prefixLen);

      // the relative paths should match; or the canonical path is outside of the index
      if ((canPath != "" && canPath.startsWith(db->path()) && relPath != canRelPath) ||
          relPath.contains("//"))
        qCritical("invalid path in database:\n\tcanonical=%s\n\tdatabase =%s",
                  qUtf8Printable(canRelPath), qUtf8Printable(relPath));
      if (progress++ == 10) {
        progress = 0;
        qInfo("<NC>%s: validating index <PL> %d/%lld", qUtf8Printable(db->path()), i,
              paths.count());
      }
    }
  }

  // if we want to can a subdir, we have to remove other files
  QString path = db->path();
  if (!dirPath.isEmpty()) {
    // note: path must be clean, absolute, and not ending in '/' to
    // work with scanDirectory. It may contain links or be a link itself
    QFileInfo info(dirPath);
    if (!info.isDir()) {
      qCritical("given path is not a directory");
      return;
    }
    path = QDir(dirPath).absolutePath();
    if (!path.startsWith(db->path())) {
      qCritical("given path: \"%s\" is not a subdirectory of \"%s\"", qUtf8Printable(path),
                qUtf8Printable(db->path()));
      return;
    }

    qDebug() << "clean subdir path" << path;
    Q_ASSERT(!path.endsWith(lc('/')));

    QSet<QString> included;
    for (auto& p : std::as_const(indexedFiles))
      if (p.startsWith(path)) included.insert(p);

    indexedFiles = included;
  }

  // if -i.algos changed try to manage the situation, prior to v0.8 there
  // was no management and -remove or rm -rf _index was needed
  if (scanner->indexParams().sync) {
    qDebug() << "checking for algos changes... (disable with -i.sync false)";
    const auto indexedItems = db->indexedItems();

    const IndexParams& requestParams = scanner->indexParams();
    const int requestAlgos = requestParams.algos;
    // const int algoTypes = requestParams.supportedTypes() & requestParams.types;

    QSet<QString> keep;   // files we do not have to re-index
    QVector<int> reindex; // files we have to remove from database & re-index
    int indexedAlgos = 0;
    for (auto& p : std::as_const(indexedFiles))
      if (indexedItems.contains(p)) {
        auto& item = indexedItems[p];
        indexedAlgos |= item.algos;
        // qDebug() << "item" << item.algos << requestAlgos << p;
        bool isIndexed = (item.algos & requestAlgos)
                         == (requestAlgos & IndexParams::supportedAlgos(item.type));
        if (isIndexed) // || !(algoTypes & (1 << (item.type - 1))))
          keep.insert(p);
        else {
          if (requestParams.verbose) {
            IndexParams tmp;
            tmp.setValue("algos", item.algos);
            qDebug() << "re-indexing id:" << item.id << "algos:" << tmp.toString("algos") << p;
          }

          reindex.append(item.id);
        }
      }

    qDebug() << "requested algos" << requestParams.toString("algos");

    IndexParams newParams = scanner->indexParams();
    newParams.setValue("algos", indexedAlgos);
    qDebug() << "indexed algos" << newParams.toString("algos");

    indexedAlgos |= requestParams.algos;
    newParams.setValue("algos", indexedAlgos);

    if (newParams.algos != requestParams.algos) {
      scanner->setIndexParams(newParams);
      qInfo() << "I cannot remove algos, using -i.algos" << newParams.toString("algos");
    }

    if (reindex.count()) {
      qWarning() << "re-indexing" << reindex.count()
                 << "file(s) due to -i.algos change (use \"-i.verbose 1 -v\" for details)";
      if (!scanner->indexParams().dryRun) db->remove(reindex);
    }

    indexedFiles = keep;
  }

  // finish modtime check, hopefully enough time elapsed
  if (!useMetadataTime && timeBefore.isValid()) do {
      QThread::msleep(qMax(500 - timer.elapsed(), 0));
      QString oldName = db->indexPath() + "/modtime-check-before.txt";
      QFile testFile(oldName);
      QString newName = db->indexPath() + "/modtime-check-after.txt";
      if (!testFile.rename(newName)) {
        qWarning() << "cannot verify that modtime works:" << testFile.errorString();
        testFile.remove();
        break;
      }
      QDateTime timeAfter = QFileInfo(newName).metadataChangeTime();
      testFile.remove();

      if (timeBefore >= timeAfter) {
        qWarning() << "metadataChangeTime does not work on this filesystem, zip scans will be slow";
        qWarning() << "you can force using it anyways with '-i.modtime true', however changes";
        qWarning() << "to zip files won't always be noticed";
        break;
      }

      auto params = scanner->indexParams();
      params.modTime = true;
      scanner->setIndexParams(params);

    } while (false);

  scanner->scanDirectory(path, indexedFiles, db->lastAdded());

  // we need a list of ids for fast removals, also we may want
  // to remove files that have issues (missing indexes etc)
  // FIXME: indexedItems() provides this now so it doesn't have to happen again
  QVector<int> toRemove;

  if (indexedFiles.count() > 0) {
    qDebug("removing %lld files from index", indexedFiles.count());
    QList<QString> sorted = indexedFiles.values();
    std::sort(sorted.begin(), sorted.end());

    PROGRESS_LOGGER(pl, "preparing for removal:<PL> %percent %step <EL>%1", indexedFiles.count());
    size_t i = 0;
    // TODO: this takes a long time for big removals...could be threaded
    // NOTE: use db->indexedFiles() instead of querying each file
    for (const auto& path : qAsConst(sorted)) {
      QString relPath = QDir(db->path()).relativeFilePath(path);
      pl.stepRateLimited(i++, {relPath});

      const Media m = db->mediaWithPath(path);
      if (!m.isValid()) {
        qWarning() << "invalid removal, non-indexed path:" << relPath;
        continue;
      }

      if (scanner->indexParams().verbose) qDebug() << "removing id:" << m.id() << relPath;

      toRemove.append(m.id());
    }
    pl.end();
  }

  if (!toRemove.isEmpty()) {
    if (scanner->indexParams().dryRun)
      qInfo() << "dry run, skipping removals";
    else
      db->remove(toRemove);
  }

  if (wait) {
    scanner->finish();
    // at this point, we should be done writing stuff
    // in case we did not add anything, go ahead and write timestamp once more,
    // to get rid of the warning message
    db->writeTimestamp();

    qInfo() << "update complete";
  }
}

void Engine::stopUpdate(bool wait) {
  scanner->flush(wait);
  commit();
}

Media Engine::mirrored(const Media& m, bool mirrorH, bool mirrorV) const {
  QImage tmp = m.image().mirrored(mirrorH, mirrorV);
  Q_ASSERT(!tmp.isNull());

  IndexResult result = scanner->processImage(m.path(), "", tmp);
  Q_ASSERT(result.ok);

  return result.media;
}

MediaSearch Engine::query(const MediaSearch& search_) const {
  MediaSearch search = search_;
  Media& needle = search.needle;
  MediaGroup& matches = search.matches;
  const SearchParams& params = search.params;

  // some options require loading the image, then we would be responsible for releasing it
  // TODO: caller can test beforehand to avoid this e.g. params.imageNeeded(m)
  bool releaseImage = false;

  if (!params.mediaReady(needle) && needle.type() == Media::TypeImage) {
    // FIXME: we only need to process for params.algo
    IndexResult result;
    if (needle.image().isNull())
      result = scanner->processImageFile(needle.path(), needle.data());
    else if (!needle.image().isNull())
      result = scanner->processImage(needle.path(), "", needle.image());

    if (!result.ok) {
      qWarning() << "failed to process:" << result.path;
      return search;
    }

    // copy stuff from needle except index data or query data
    Media& m = result.media;

    m.setTransform(needle.transform());
    m.setRoi(needle.roi());
    m.setContentType(needle.contentType());
    m.setMatchRange(needle.matchRange());
    m.setMatchFlags(needle.matchFlags());
    m.setImage(needle.image());
    m.readMetadata();
    m.copyAttributes(needle);
    m.setPosition(needle.position());
    m.setIsWeed(needle.isWeed());
    m.setIsFile(needle.isFile());

    needle = m;
  }

  if (!params.mediaSupported(needle)) {
    qWarning() << needle.path() << "media type unsupported or disabled with -p.types"
               << params.queryTypes;
    goto CLEANUP;
  }

  if (!params.mediaReady(needle)) {
    qWarning() << needle.path() << "is not indexed for algo" << params.algo;
    goto CLEANUP;
  }

  if (db->isWeed(needle)) needle.setIsWeed();

  matches = db->similarTo(needle, params);

  if (needle.image().isNull() && (params.mirrorMask || params.templateMatch)) {
    // qWarning() << "loading image for reflection or template match" << needle.path();
    needle.setImage(needle.loadImage());
    releaseImage = true;
  }

  if (params.mirrorMask & SearchParams::MirrorHorizontal)
    matches.append(db->similarTo(mirrored(needle, true, false), params));

  if (params.mirrorMask & SearchParams::MirrorVertical)
    matches.append(db->similarTo(mirrored(needle, false, true), params));

  if (params.mirrorMask & SearchParams::MirrorBoth)
    matches.append(db->similarTo(mirrored(needle, true, true), params));

  if (params.templateMatch && params.algo != SearchParams::AlgoVideo)
    matcher->match(needle, matches, params);

  std::sort(matches.begin(), matches.end());

  // needle takes the pos of first video match
  if (matches.count() > 0)
    search.needle.setMatchRange({-1, matches[0].matchRange().srcIn, 1});

CLEANUP:
  if (releaseImage)
    needle.setImage(QImage());

  return search;
}
