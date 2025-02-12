#include "testindexbase.h"

#include <QtTest/QtTest>

#include "database.h"
#include "index.h"
#include "media.h"
#include "scanner.h"

void TestIndexBase::mediaProcessed(const Media& m) {
  //printf("TestIndexBase::fileAdded: %s\n", qPrintable(m.path()));
  QVERIFY(m.path() != "");
  QVERIFY(m.path().startsWith(_database->path()));
  QVERIFY(_database->mediaWithPath(m.path()).path() == "");

  _mediaProcessed->append(m);
}

void TestIndexBase::baseInitTestCase(Index* index, const QString& dataSet) {
  _index = index;

  _dataDir = getenv("TEST_DATA_DIR");
  if (_dataDir.isEmpty()) qFatal("TEST_DATA_DIR environment is required");
  QVERIFY(QDir(_dataDir).exists());

  const QString indexPath = _dataDir + "/" + dataSet;

  QDir dbDir(indexPath + "/_index");
  if (dbDir.exists())
    QVERIFY(dbDir.removeRecursively());

  _database = new Database(indexPath);
  _database->addIndex(_index);
  _database->setup();

  _scanner = new Scanner();

  IndexParams params;
  params.algos = 1 << index->id();
  if (index->id() == SearchParams::AlgoVideo)
    params.algos |= 1 << SearchParams::AlgoDCT; // req for image->video search

  _scanner->setIndexParams(params);

  _mediaProcessed = new MediaGroup;

  connect(_scanner, &Scanner::mediaProcessed, this,
          &TestIndexBase::mediaProcessed);

  qDebug() << "scanning dir:" << _database->path();

  QSet<QString> skip = _database->indexedFiles();

  _scanner->scanDirectory(_database->path(), skip);
  _scanner->finish();

  _database->add(*_mediaProcessed);

  QVERIFY(!_index->isLoaded());
}

void TestIndexBase::baseCleanupTestCase() {
  QDir dbDir(_database->path()+"/_index");
  delete _scanner;
  delete _database;
  delete _index;
  delete _mediaProcessed;
  Database::disconnectAll();
  QThread::msleep(100);
  qApp->processEvents();
  QVERIFY(dbDir.removeRecursively());
  QVERIFY(!dbDir.exists());
}

void TestIndexBase::baseTestDefaults(Index* index) {
  QVERIFY(!index->isLoaded());
  QCOMPARE(index->memoryUsage(), (size_t)0);
  QCOMPARE(index->count(), 0);
  delete index;
}

void TestIndexBase::baseTestEmpty(Index* index) {
  QTemporaryDir indexDir;

  Database db(indexDir.path());
  db.addIndex(index);
  db.setup();

  // test load/find
  SearchParams params;
  params.algo = index->id();
  MediaGroupList list = db.similar(params);
  QCOMPARE(list.count(), 0);

  // test add/remove works
  QImage img(32, 32, QImage::Format_ARGB32);

  const QString path = indexDir.path() + "/empty.png";
  Media m = (Scanner().processImage(path, "md5", img)).media;
  MediaGroup g{m};
  db.add(g);
  m = g[0];
  QVERIFY(m.id() != 0);

  db.remove(m.id());
  m = db.mediaWithPath(path);
  QVERIFY(m.id() == 0);

  QCOMPARE(db.indexedForAlgos(1 << params.algo, false).count(), 0);
}

void TestIndexBase::baseTestLoad(const SearchParams& params) {
  QVERIFY(!_index->isLoaded());
  auto indexedByAlgoBefore = _database->indexedForAlgos(1 << params.algo, false);

  MediaGroupList results = _database->similar(params);

  QVERIFY(_index->isLoaded());
  auto indexedByAlgoAfter = _database->indexedForAlgos(1 << params.algo, false);

  // 5 sizes of 40 images means I should get at least 40 results here,
  // there could be more if any image didn't match all 4 of it's copies
  QVERIFY(results.count() >= 40);

  // each image should at least match itself
  for (const MediaGroup& group : results) QVERIFY(group.count() > 1);

  // look up every path and we should get the 5 that matched it,
  // that includes matching itself. much slower since we
  // are processing the file as the scanner would
  for (const QString& path : _database->indexedFiles()) {
    Media needle = _scanner->processImageFile(path).media;
    bool ok = params.mediaReady(needle);

    if (!ok) QEXPECT_FAIL("", qUtf8Printable(path), Continue);

    QVERIFY(indexedByAlgoBefore.contains(path));

    if (!ok) QEXPECT_FAIL("", qUtf8Printable(path), Continue);

    QVERIFY(indexedByAlgoAfter.contains(path));

    if (!ok) QEXPECT_FAIL("", qUtf8Printable(path), Continue);

    MediaGroup group = _database->similarTo(needle, params);
    QVERIFY(group.count() > 1);
  }
}

void TestIndexBase::baseTestAddRemove(const SearchParams& params, int expectedMatches) {
  MediaGroupList before = _database->similar(params);

  // 5 sizes of 40 images means I should get at least 40 results here,
  // there could be more if any image didn't match all 4 of it's copies
  QVERIFY(before.count() >= expectedMatches);

  // remove one from each group and see that they are
  // now missing when searching again
  QVERIFY(before[0].count() >= 1);
  QVERIFY(before[1].count() >= 1);
  QVERIFY(before[2].count() >= 1);

  // queried set we are going to remove
  const MediaGroup queried = {
    before[0][0],
    before[1][0],
    before[2][0]
  };

  // test they are present in results as expected
  const MediaGroup b1 = _database->similarTo(queried[0], params);
  QVERIFY(b1.contains(queried[0]));
  const MediaGroup b2 = _database->similarTo(queried[1], params);
  QVERIFY(b2.contains(queried[1]));
  const MediaGroup b3 = _database->similarTo(queried[2], params);
  QVERIFY(b3.contains(queried[2]));

  qDebug() << "test: removing...";
  _database->remove(queried);

  QVERIFY(!_database->mediaExists(queried[0].path()));
  QVERIFY(!_database->mediaExists(queried[1].path()));
  QVERIFY(!_database->mediaExists(queried[2].path()));

  // processed set we are going to add back
  MediaGroup processed;
  for (auto& q : queried)
    if (params.algo == SearchParams::AlgoVideo)
      processed += (_scanner->processVideoFile(q.path())).media;
    else
      processed += (_scanner->processImageFile(q.path())).media;

  // test if they were removed from results
  // must query with processed item since we removed it
  const MediaGroup g1 = _database->similarTo(processed[0], params);
  QVERIFY(!g1.contains(queried[0]));

  const MediaGroup g2 = _database->similarTo(processed[1], params);
  QVERIFY(!g2.contains(queried[1]));

  const MediaGroup g3 = _database->similarTo(processed[2], params);
  QVERIFY(!g3.contains(queried[2]));

  // if we re-add they should show up again
  qDebug() << "test: adding...";
  _database->add(processed);

  QVERIFY(_database->mediaExists(queried[0].path()));
  QVERIFY(_database->mediaExists(queried[1].path()));
  QVERIFY(_database->mediaExists(queried[2].path()));

  const MediaGroup h1 = _database->similarTo(processed[0], params);
  QVERIFY(Media::groupCompareByContents(b1, h1));

  const MediaGroup h2 = _database->similarTo(processed[1], params);
  QVERIFY(Media::groupCompareByContents(b2, h2));

  const MediaGroup h3 = _database->similarTo(processed[2], params);
  QVERIFY(Media::groupCompareByContents(b3, h3));
}
