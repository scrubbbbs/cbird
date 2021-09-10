
#include "database.h"
#include "dcthashindex.h"
#include "scanner.h"
#include "testindexbase.h"

#include <QtTest/QtTest>

class TestDctHashIndex : public TestIndexBase {
  Q_OBJECT

 private Q_SLOTS:
  // load the 40 images x 5 scales data set
  void initTestCase() {
    baseInitTestCase(new DctHashIndex, "40x5-sizes", false);
  }
  // void cleanupTestCase() {}
  // void init() {}
  // void cleanup() {}

  void testDefaults() { baseTestDefaults(new DctHashIndex); }
  void testEmpty() { baseTestEmpty(new DctHashIndex); }

  void testMemoryUsage();
  void testLoad();
  void testAddRemove();
};

void TestDctHashIndex::testMemoryUsage() {
  // 8 bytes per hash, plus 4 bytes index
  QCOMPARE(_index->memoryUsage(), (size_t)(8 + 4) * _index->count());
}

//#include "gui/mediagrouplistwidget.h"

void TestDctHashIndex::testLoad() {
  SearchParams params;
  params.filterGroups = true;
  params.filterParent = true;

  MediaGroupList results = _database->similar(params);

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
    MediaGroup group = _database->similarTo(needle, params);
    QVERIFY(group.count() > 1);
  }
}

void TestDctHashIndex::testAddRemove() {
  SearchParams params;
  params.filterGroups = true;
  params.filterParent = true;

  MediaGroupList before = _database->similar(params);

  // 5 sizes of 40 images means I should get at least 40 results here,
  // there could be more if any image didn't match all 4 of it's copies
  QVERIFY(before.count() >= 40);

  // remove one from each group and see that they are
  // now missing when searching again
  QVERIFY(before[0].count() >= 1);
  QVERIFY(before[1].count() >= 2);
  QVERIFY(before[2].count() >= 3);

  MediaGroup removed;
  removed.append(before[0][0]);
  removed.append(before[1][1]);
  removed.append(before[2][2]);

  _database->remove(removed);

  // test if they were removed from results
  MediaGroup g1 = _database->similarTo(removed[0], params);
  QVERIFY(!g1.contains(removed[0]));
  g1.append(removed[0]);

  MediaGroup g2 = _database->similarTo(removed[1], params);
  QVERIFY(!g2.contains(removed[1]));
  g2.append(removed[1]);

  MediaGroup g3 = _database->similarTo(removed[2], params);
  QVERIFY(!g3.contains(removed[2]));
  g3.append(removed[2]);

  // if we re-scan they should show up again
  QSet<QString> skip = _database->indexedFiles();
  _scanner->scanDirectory(_database->path(), skip);
  _scanner->finish();

  MediaGroup h1 = _database->similarTo(removed[0], params);
  QVERIFY(Media::groupCompareByContents(g1, h1));

  MediaGroup h2 = _database->similarTo(removed[1], params);
  QVERIFY(Media::groupCompareByContents(g2, h2));

  MediaGroup h3 = _database->similarTo(removed[2], params);
  QVERIFY(Media::groupCompareByContents(g3, h3));
}

QTEST_MAIN(TestDctHashIndex)
#include "testdcthashindex.moc"
