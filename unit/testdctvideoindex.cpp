
#include "testindexbase.h"
#include "dctvideoindex.h"

#include <QtTest/QtTest>

class TestDctVideoIndex : public TestIndexBase {
  Q_OBJECT
  SearchParams _params;
 private Q_SLOTS:
  void initTestCase() {
    baseInitTestCase(new DctVideoIndex, "xiph-video");
    _params.algo = SearchParams::AlgoVideo;
    _params.filterSelf = false;
    _params.dctThresh = 3;
    _params.minFramesMatched = 10;
    _params.minFramesNear = 50;
    _params.verbose = true;
    _params.skipFrames = 0;
    _params.queryTypes = {Media::TypeVideo};
  }
  void cleanupTestCase() { baseCleanupTestCase(); }

  void testDefaults();
  //void testLoad();
  void testAddRemove() { baseTestAddRemove(_params, 5); }
};

void TestDctVideoIndex::testDefaults() {
  DctVideoIndex index;

  QVERIFY(!index.isLoaded());
  QCOMPARE(index.memoryUsage(), (size_t)0);
  QCOMPARE(index.count(), 0);
}
/*
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
*/
QTEST_MAIN(TestDctVideoIndex)
#include "testdctvideoindex.moc"
