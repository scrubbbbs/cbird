
#include "database.h"
#include "dctfeaturesindex.h"
#include "scanner.h"
#include "testindexbase.h"

#include <QtTest/QtTest>

class TestDctFeaturesIndex : public TestIndexBase {
  Q_OBJECT

 private Q_SLOTS:
  void initTestCase();
  // void cleanupTestCase() {}
  // void init() {}              // called before/after each unit test (testXXX
  // method) void cleanup() {}

  void testDefaults() { baseTestDefaults(new DctFeaturesIndex); }
  void testEmpty() { baseTestEmpty(new DctFeaturesIndex); }

  void testMemoryUsage();
  void testLoad();
  void testAddRemove();

 private:
  SearchParams _params;
};

void TestDctFeaturesIndex::initTestCase() {
  // load test data set consisting of 40 images in 5 different
  // sizes, in 5 different directories (one for each size)
  baseInitTestCase(new DctFeaturesIndex, "40x5-sizes", true);

  _params.algo = _index->id();
  _params.verbose = true;
  _params.dctThresh = 5;
  _params.filterGroups = true;
  _params.filterParent = true;
  _params.maxMatches = 5;
}

void TestDctFeaturesIndex::testMemoryUsage() {
  // load index
  (void)_database->similar(_params);

  QVERIFY(_index->memoryUsage() > 0);
}

void TestDctFeaturesIndex::testLoad() {
  MediaGroupList results = _database->similar(_params);

  QVERIFY(_index->memoryUsage() > 0);

  // ideally we would get 40 sets of 5, but that isn't going
  // to happen since the search is imprecise.
  QVERIFY(results.count() <= 50);
  QVERIFY(results.count() >= 40);

  // check matches per group
  // it must be > 1, as the first item is always the needle
  for (const MediaGroup& group : results) {
    QVERIFY(group.count() > 1);
    QVERIFY(group.count() <= _params.maxMatches);
  }

  // look up every path and we should get the 5 that matched it,
  // that includes matching itself. much slower since we
  // are processing the file as the scanner would

  // note: QSet uses randomized hashing, sort it so we
  // get consistent results
  QStringList indexed = _database->indexedFiles().values();
  std::sort(indexed.begin(), indexed.end());

  for (const QString& path : indexed) {
    QVERIFY(QFileInfo(path).exists());

    // we won't neccessarily get keypoints. maybe if image is too small
    // or there aren't any significant details
    Media needle = (_scanner->processImageFile(path)).media;
    if (needle.keyPointHashes().size() > 0) {
      MediaGroup group = _database->similarTo(needle, _params);

      // must at least have the needle
      QVERIFY(group.count() >= 1);

      // first item must be needle
      QCOMPARE(group[0].path(), needle.path());

      // unlikely we won't get a match, show it anyways
      if (group.count() < 2) qWarning("no matches: %s", qPrintable(path));
    } else
      qWarning("no keypoints: %s", qPrintable(path));
  }
}

void TestDctFeaturesIndex::testAddRemove() {
  MediaGroupList before = _database->similar(_params);

  QCOMPARE(before.count(), 40);

  MediaGroup removed;
  removed.append(before[0][0]);
  removed.append(before[1][1]);
  removed.append(before[2][2]);
  QVERIFY(removed.contains(removed[0]));

  Media m = (_scanner->processImageFile(removed[0].path())).media;
  MediaGroup g = _database->similarTo(m, _params);
  QVERIFY(g.contains(m));

  _database->remove(removed);

  g = _database->similarTo(m, _params);
  QVERIFY(!g.contains(m));

  // if we re-scan they should show up again
  QSet<QString> skip = _database->indexedFiles();
  _scanner->scanDirectory(_database->path(), skip);
  _scanner->flush();

  g = _database->similarTo(m, _params);
  QVERIFY(g.contains(m));
}

QTEST_MAIN(TestDctFeaturesIndex)
#include "testdctfeaturesindex.moc"
