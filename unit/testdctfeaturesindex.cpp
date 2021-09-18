
#include "testindexbase.h"
#include "dctfeaturesindex.h"

#include <QtTest/QtTest>

class TestDctFeaturesIndex : public TestIndexBase {
  Q_OBJECT
  SearchParams _params;
 private Q_SLOTS:
  void initTestCase() { baseInitTestCase(new DctFeaturesIndex,
                        "40x5-sizes");
    _params.algo = SearchParams::AlgoDCTFeatures;
    _params.filterSelf = false;
  }
  void cleanupTestCase() { baseCleanupTestCase(); }

  void testDefaults() { baseTestDefaults(new DctFeaturesIndex); }
  void testEmpty() { baseTestEmpty(new DctFeaturesIndex); }
  void testLoad() { baseTestLoad(_params); }
  void testAddRemove() { baseTestAddRemove(_params, 40); }
  void testMemoryUsage();
};

void TestDctFeaturesIndex::testMemoryUsage() {
  QVERIFY(_index->memoryUsage() > 0);
}

QTEST_MAIN(TestDctFeaturesIndex)
#include "testdctfeaturesindex.moc"
