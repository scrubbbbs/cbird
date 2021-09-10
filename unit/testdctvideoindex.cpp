
#include <QtTest/QtTest>

#include "dctvideoindex.h"
#include "testindexbase.h"

#include "database.h"
#include "scanner.h"

class TestDctVideoIndex : public TestIndexBase {
  Q_OBJECT

 private Q_SLOTS:
  void initTestCase() {
    baseInitTestCase(new DctVideoIndex, "xiph-video", false);
  }
  // void cleanupTestCase() {}
  // void init() {}            // called before/after each unit test (testXXX
  // method) void cleanup() {}

  void testDefaults();
  void testLoad();
  void testAddRemove();

 private:
  QString _dataDir;
  Database* _database;
  Scanner* _scanner;
  DctVideoIndex* _index;
  SearchParams _params;
};

void TestDctVideoIndex::testDefaults() {
  DctVideoIndex index;

  QVERIFY(!index.isLoaded());
  QCOMPARE(index.memoryUsage(), (size_t)0);
  QCOMPARE(index.count(), 0);
}

void TestDctVideoIndex::testLoad() {
  MediaGroupList results = _database->similar(_params);

  QVERIFY(_index->memoryUsage() > 0);

  // ideally we would get 40 sets of 5, but that isn't going
  // to happen since the search is imprecise.
  QVERIFY(results.count() <= 40);

  // the features match some sets will not match completely,
  // not sure right now how to spec this. by definition
  // it must be > 1
  for (const MediaGroup& group : results) QVERIFY(group.count() > 1);

  // look up every path and we should get the 5 that matched it,
  // that includes matching itself. much slower since we
  // are processing the file as the scanner would

  // note: QSet uses randomized hashing, sort it so we
  // get consistent results
  QStringList indexed = _database->indexedFiles().values();
  std::sort(indexed.begin(), indexed.end());

  for (const QString& path : indexed) {
    QVERIFY(QFileInfo(path).exists());

    if (!_scanner->imageTypes().contains(QFileInfo(path).suffix().toLower())) {
      qWarning("skip non-image: %s\n", qPrintable(path));
      continue;
    }

    // we won't neccessarily get keypoints. maybe if image is too small
    Media needle = _scanner->processImageFile(path).media;
    if (needle.keyPointHashes().size() > 0) {
      // we won't get a match for every needle since maybe it never
      // stored any keypoints in the db
      MediaGroup group = _database->similarTo(needle, _params);
      if (group.count() <= 1) printf("no matches: %s\n", qPrintable(path));
    } else
      printf("no keypoints: %s\n", qPrintable(path));
  }
}

void TestDctVideoIndex::testAddRemove() {
  // search for images that match videos
  MediaGroupList before = _database->similar(_params);

  QCOMPARE(before.count(), 40);

  // the first matched video will be removed, it should be
  // at index 1 of the first group
  QVERIFY(before[0].count() >= 2);

  Media toRemove = before[0][1];

  QVERIFY(toRemove.type() == Media::TypeVideo);

  // count how many images matched the video we are removing
  int numImages = 0;
  for (const MediaGroup& g : before) {
    QVERIFY(g.count() >= 2);
    if (g[1].path() == toRemove.path()) numImages++;
  }

  MediaGroup removed;
  removed.append(toRemove);
  QVERIFY(removed.contains(removed[0]));

  // Media m = _scanner->processVideoFile(removed[0].path());
  // MediaGroup g = _database->similarTo(m, _params);
  // QVERIFY(g.contains(m));

  _database->remove(removed);

  MediaGroupList after = _database->similar(_params);
  QCOMPARE(after.count(), 40 - numImages);

  // if we re-scan they should show up again
  QSet<QString> skip = _database->indexedFiles();
  _scanner->scanDirectory(_database->path(), skip);
  _scanner->finish();

  after = _database->similar(_params);
  QCOMPARE(after.count(), 40);

  //= _scanner->processImageFile(removed[0].path());
  // g = _database->similarTo(m, _params);
  // QVERIFY(g.contains(m));
}

QTEST_MAIN(TestDctVideoIndex)
#include "testdctvideoindex.moc"
