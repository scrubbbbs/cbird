#include "engine.h"

#include "colordescindex.h"
#include "cvfeaturesindex.h"
#include "database.h"
#include "dctfeaturesindex.h"
#include "dcthashindex.h"
#include "dctvideoindex.h"
#include "scanner.h"
#include "templatematcher.h"

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
  // videos take a long time to process so do not batch, and commit immediately
  if (m.type() == Media::TypeVideo) {
    // If there are multiple processes updating (user error as this is unsupported),
    // they might cause database corruption, so lock them out.
    // This will not prevent sql constraint violation if they attempt to
    // add the same file. If processes work on different subtrees this won't happen.
    QLockFile dbLock(db->indexPath() + "/write.lock");
    if (dbLock.tryLock(0))
      db->add({m});
    else
      qCritical() << "database update aborted, another process is writing";
    return;
  }

  if (_batch.contains(m)) {
    qWarning() << "attempt to add media twice in same batch, discarding..."
               << m.path();
    return;
  }

  Media copy = m;
  copy.setData(QByteArray());
  copy.setImage(QImage());
  _batch.append(copy);

  if (_batch.count() >= scanner->indexParams().writeBatchSize) {
    printf("w");
    fflush(stdout);
    commit();
  }
}

void Engine::commit() {
  if (_batch.count() > 0) {
    QLockFile dbLock(db->indexPath() + "/write.lock");
    if (dbLock.tryLock(0))
      db->add(_batch);
    else
      qCritical() << "database update aborted, another process is writing";
    _batch.clear();
  }
}

void Engine::update(bool wait) {
  // find new/updated and removed files
  // fixme: find modified files
  QSet<QString> skip = db->indexedFiles();

  scanner->scanDirectory(db->path(), skip);

  QVector<int> toRemove;

  if (skip.count() > 0) {
    qInfo("removing %d files from index", skip.count());
    for (const QString& path : skip)
      toRemove.append(db->mediaWithPath(path).id());
  }

  // check for missing external index data, (currently only video index)
  // todo: should be implemented by specific index
  // todo: re-index missing item now
  if (scanner->indexParams().algos & IndexParams::AlgoVideo)
    for (const Media& m : db->mediaWithType(Media::TypeVideo)) {
      QString vIndexPath =
          QString("%1/%2.vdx").arg(db->videoPath()).arg(m.id());
      if (!QFileInfo(vIndexPath).exists()) {
        qWarning("video index missing: %s", qPrintable(m.path()));
        toRemove.append(m.id());
      } else {
        VideoIndex idx;
        idx.load(vIndexPath);
        if (idx.isEmpty()) {
          qWarning("video index is empty, forcing re-index: %s", qPrintable(m.path()));
          toRemove.append(m.id());
        }
      }
    }

  if (!scanner->indexParams().dryRun && !toRemove.isEmpty())
    db->remove(toRemove);

  if (wait) scanner->finish();
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

MediaSearch Engine::query(const MediaSearch& search_) {
  MediaSearch search = search_;
  Media& needle = search.needle;
  MediaGroup& matches = search.matches;
  const SearchParams& params = search.params;

  if (!params.mediaReady(needle))
    if (!needle.image().isNull()) {
      qWarning() << "processImage:" << needle.path();
      IndexResult result =
          scanner->processImage(needle.path(), "", needle.image());
      if (!result.ok) {
        qWarning() << "failed to process:" << result.path;
        return search;
      }

      // copy some attributes of the needle over so they aren't lost
      Media& m = result.media;
      m.setCompressionRatio(needle.compressionRatio());
      m.setTransform(needle.transform());
      m.setRoi(needle.roi());
      m.setContentType(needle.contentType());
      m.setMatchRange(needle.matchRange());
      m.setMatchFlags(needle.matchFlags());
      m.setImage(needle.image());

      needle = result.media;
    }

  if (!params.mediaReady(needle)) {
    // todo: state why
    qWarning() << "unqueryable" << needle.path();
    return search;
  }

  matches = db->similarTo(needle, params);

  // some options require loading the image
  // fixme: caller can test beforehand to avoid reloading image
  // e.g. params.imageNeeded()
  bool releaseImage = false;
  if (needle.image().isNull() && (params.mirrorMask || params.templateMatch)) {
    qWarning() << "(re)loading image" << needle.path();
    needle.setImage(needle.loadImage());
    releaseImage = true;
  }

  if (params.mirrorMask & SearchParams::MirrorHorizontal)
    matches.append(db->similarTo(mirrored(needle, true, false), params));

  if (params.mirrorMask & SearchParams::MirrorVertical)
    matches.append(db->similarTo(mirrored(needle, false, true), params));

  if (params.mirrorMask & SearchParams::MirrorBoth)
    matches.append(db->similarTo(mirrored(needle, true, true), params));

  if (params.templateMatch) matcher->match(needle, matches, params);

  std::sort(matches.begin(), matches.end());

  if (releaseImage) {
    needle.setImage(QImage());
    qWarning() << "discarding needle image";
  }

  return search;
}
