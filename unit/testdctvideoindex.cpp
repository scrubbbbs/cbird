
#include "testindexbase.h"
#include "dctvideoindex.h"
#include "database.h"
#include "scanner.h"

#include <QtTest/QtTest>

class TestDctVideoIndex : public TestIndexBase {
  Q_OBJECT
  SearchParams _params;
  const int numVideos=5;
 private Q_SLOTS:
  void initTestCase() {
    baseInitTestCase(new DctVideoIndex, "xiph-video");
    // note: parameters tuned to match short test videos
    _params.algo = SearchParams::AlgoVideo;
    _params.filterSelf = false;
    _params.dctThresh = 1;
    _params.minFramesMatched = 1;
    _params.minFramesNear = 1;
    _params.verbose = true;
    _params.skipFrames = 0;
    _params.queryTypes = Media::TypeVideo;
  }
  void cleanupTestCase() { baseCleanupTestCase(); }

  void testDefaults() { baseTestDefaults(new DctVideoIndex); }
  void testEmpty() { baseTestEmpty(new DctVideoIndex); }
  void testAddRemove() { baseTestAddRemove(_params, numVideos); }
  void testMemoryUsage();
  void testLoad();
};

void TestDctVideoIndex::testMemoryUsage() {
  // 8 bytes per hash, plus 4 bytes index
  QVERIFY(_index->memoryUsage() > 0);
}

void TestDctVideoIndex::testLoad() {
  MediaGroupList results = _database->similar(_params);

  QVERIFY(results.count() >= numVideos);

  // look up every path, ignoring _params.queryTypes
  // this is fine since image->video search is supported
  for (const QString& path : _database->indexedFiles()) {
    Media needle;
    const QString suffix = QFileInfo(path).suffix();
    if (_scanner->videoTypes().contains(suffix))
      needle = _scanner->processVideoFile(path).media;
    else if (_scanner->imageTypes().contains(suffix))
      needle = _scanner->processImageFile(path).media;
    else
      QEXPECT_FAIL("unsupported format", qUtf8Printable(path), Continue);

    QVERIFY(_params.mediaReady(needle));

    const MediaGroup group = _database->similarTo(needle, _params);

    // image->video search misses a few frames in this data set
    if (needle.type() == Media::TypeImage && group.count() == 1) {
      // check it matched the right video by file prefix
      auto parts = needle.completeBaseName().split(lc('_'));
      parts.pop_back(); // remove frame number
      const QString queryPrefix = parts.join(lc('_'));

      const QString resultPrefix = group[0].completeBaseName();
      QCOMPARE(queryPrefix,resultPrefix);
    }

    if (needle.type()==Media::TypeVideo) {
      QVERIFY(group.count()==1);
      QVERIFY(group[0].path() == needle.path());
    }
  }
}

QTEST_MAIN(TestDctVideoIndex)
#include "testdctvideoindex.moc"
