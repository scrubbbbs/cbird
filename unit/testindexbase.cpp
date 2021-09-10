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

  _database->add({m});
}

void TestIndexBase::baseInitTestCase(Index* index, const QString& dataSet,
                                     bool enableFeatures) {
  _index = index;

  _dataDir = getenv("TEST_DATA_DIR");
  if (_dataDir.isEmpty()) qFatal("TEST_DATA_DIR environment is required");
  QVERIFY(QDir(_dataDir).exists());

  _database = new Database(_dataDir + "/" + dataSet);

  _database->addIndex(_index);
  _database->setup();

  _database->remove(_database->mediaWithType(Media::TypeImage));

  _scanner = new Scanner();

  // if we don't care about features that will speed up the test
  IndexParams params;
  if (!enableFeatures) params.algos &= ~(
    1<<SearchParams::AlgoDCTFeatures |
    1<<SearchParams::AlgoCVFeatures);

  _scanner->setIndexParams(params);

  connect(_scanner, &Scanner::mediaProcessed, this,
          &TestIndexBase::mediaProcessed);

  printf("scanning dir: %s\n", qPrintable(_database->path()));

  QSet<QString> skip = _database->indexedFiles();

  _scanner->scanDirectory(_database->path(), skip);
  _scanner->finish();
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
  db.add({m});
  m = db.mediaWithPath(path);
  QVERIFY(m.id() != 0);

  db.remove(m.id());
  m = db.mediaWithPath(path);
  QVERIFY(m.id() == 0);
}
