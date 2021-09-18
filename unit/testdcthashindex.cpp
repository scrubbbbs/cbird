
#include "testindexbase.h"
#include "dcthashindex.h"

#include <QtTest/QtTest>

class TestDctHashIndex : public TestIndexBase {
  Q_OBJECT
  SearchParams _params;

 private Q_SLOTS:
  // load the 40 images x 5 scales data set
  void initTestCase() {
    baseInitTestCase(new DctHashIndex, "40x5-sizes");
    _params.algo = SearchParams::AlgoDCT;
    _params.filterSelf = false;
  }
  void cleanupTestCase() { baseCleanupTestCase(); }

  void testDefaults() { baseTestDefaults(new DctHashIndex); }
  void testEmpty() { baseTestEmpty(new DctHashIndex); }
  void testLoad() { baseTestLoad(_params); }
  void testAddRemove() { baseTestAddRemove(_params, 40); }
  void testMemoryUsage();
};

void TestDctHashIndex::testMemoryUsage() {
  // 8 bytes per hash, plus 4 bytes index
  QCOMPARE(_index->memoryUsage(), (size_t)(8 + 4) * _index->count());
}

QTEST_MAIN(TestDctHashIndex)
#include "testdcthashindex.moc"
