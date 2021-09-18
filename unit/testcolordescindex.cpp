
#include "testindexbase.h"
#include "colordescindex.h"

#include <QtTest/QtTest>

class TestColorDescIndex : public TestIndexBase {
  Q_OBJECT
  SearchParams _params;
 private Q_SLOTS:
  void initTestCase() {
    baseInitTestCase(new ColorDescIndex, "40x5-sizes");
    _params.algo = SearchParams::AlgoColor;
    _params.filterSelf = false;
  }
  void cleanupTestCase() { baseCleanupTestCase(); }

  void testDefaults() { baseTestDefaults(new ColorDescIndex); }
  void testEmpty() { baseTestEmpty(new ColorDescIndex); }
  void testLoad() { baseTestLoad(_params); }
  void testAddRemove() { baseTestAddRemove(_params, 40); }
  void testMemoryUsage();
};

void TestColorDescIndex::testMemoryUsage() {
  // descriptor size plus media id size
  QCOMPARE(_index->memoryUsage(),
           (sizeof(ColorDescriptor) + 4) * size_t(_index->count()));
}

QTEST_MAIN(TestColorDescIndex)
#include "testcolordescindex.moc"
