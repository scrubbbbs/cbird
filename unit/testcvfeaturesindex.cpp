
#include "testindexbase.h"
#include "cvfeaturesindex.h"

#include <QtTest/QtTest>

class TestCvFeaturesIndex : public TestIndexBase {
  Q_OBJECT
  SearchParams _params;

 private Q_SLOTS:
  void initTestCase() {
    baseInitTestCase(new CvFeaturesIndex, "40x5-sizes");
    _params.algo = SearchParams::AlgoCVFeatures;
    _params.filterSelf = false;
  }
  void cleanupTestCase() { baseCleanupTestCase(); }

  void testDefaults() { baseTestDefaults(new CvFeaturesIndex); }
  void testEmpty() { baseTestEmpty(new CvFeaturesIndex); }
  void testLoad() { baseTestLoad(_params); }
  void testAddRemove() { baseTestAddRemove(_params, 40); };
  void testMemoryUsage() { QVERIFY(_index->memoryUsage() > 0); }
};

QTEST_MAIN(TestCvFeaturesIndex)
#include "testcvfeaturesindex.moc"
