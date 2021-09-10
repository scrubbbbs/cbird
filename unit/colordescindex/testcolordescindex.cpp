
#include "colordescindex.h"
#include "database.h"
#include "gui/mediagrouplistwidget.h"
#include "scanner.h"
#include "testindexbase.h"

#include <QtTest/QtTest>

class TestColorDescIndex : public TestIndexBase {
  Q_OBJECT

 private Q_SLOTS:
  void initTestCase() {
    baseInitTestCase(new ColorDescIndex, "40x5-sizes", false);
  }
  // void cleanupTestCase() {}
  // void init() {}              // called before/after each unit test (testXXX
  // method) void cleanup() {}

  void testDefaults() { baseTestDefaults(new ColorDescIndex); }
  void testEmpty() { baseTestEmpty(new ColorDescIndex); }
  void testAddRemove();
  void testMemoryUsage();
  void testLoad();
};

void TestColorDescIndex::testMemoryUsage() {
  // descriptor size plus media id size
  QCOMPARE(_index->memoryUsage(),
           (sizeof(ColorDescriptor) + 4) * size_t(_index->count()));
}

void TestColorDescIndex::testLoad() {
  SearchParams params;
  params.algo = SearchParams::AlgoColor;
  params.filterGroups = true;
  params.maxMatches = 5;

  MediaGroupList results = _database->similar(params);

  if (getenv("VIEW_RESULTS")) {
    MediaGroupListWidget* w =
        new MediaGroupListWidget(results, nullptr, 0, _database);
    w->show();
    QEventLoop* loop = new QEventLoop;
    loop->processEvents();
    while (w->isVisible()) loop->processEvents();
    delete loop;
    delete w;
  }

  // 5 sizes of 40 images means I should get 40 results (after filter)
  QCOMPARE(results.count(), 40);

  // each should have 5 images (params.maxMatches)
  for (const MediaGroup& group : results) QCOMPARE(group.count(), 5);

  // 1-to-N search must also work
  for (const QString& path : _database->indexedFiles()) {
    Media needle = _scanner->processImageFile(path).media;
    MediaGroup group = _database->similarTo(needle, params);
    QCOMPARE(group.count(), 5);
  }
}

void TestColorDescIndex::testAddRemove() {
  SearchParams params;
  params.algo = SearchParams::AlgoColor;
  params.filterGroups = true;
  MediaGroupList before = _database->similar(params);

  QCOMPARE(before.count(), 40);

  MediaGroup removed;
  removed.append(before[0][0]);
  removed.append(before[1][1]);
  removed.append(before[2][2]);

  _database->remove(removed);

  // we did not remove these, they should still be there
  MediaGroup g1 = _database->similarTo(removed[0], params);
  QCOMPARE(g1.count(), 4);
  g1.append(removed[0]);

  MediaGroup g2 = _database->similarTo(removed[1], params);
  QCOMPARE(g2.count(), 4);
  g2.append(removed[1]);

  MediaGroup g3 = _database->similarTo(removed[2], params);
  QCOMPARE(g3.count(), 4);
  g3.append(removed[2]);

  // if we re-scan they should show up again
  QSet<QString> skip = _database->indexedFiles();
  _scanner->scanDirectory(_database->path(), skip);
  _scanner->finish();

  MediaGroup h1 = _database->similarTo(removed[0], params);
  QCOMPARE(h1.count(), 5);
  QVERIFY(Media::groupCompareByContents(g1, h1));

  MediaGroup h2 = _database->similarTo(removed[1], params);
  QCOMPARE(h2.count(), 5);
  QVERIFY(Media::groupCompareByContents(g2, h2));

  MediaGroup h3 = _database->similarTo(removed[2], params);
  QCOMPARE(h3.count(), 5);
  QVERIFY(Media::groupCompareByContents(g3, h3));
}

QTEST_MAIN(TestColorDescIndex)
#include "testcolordescindex.moc"
