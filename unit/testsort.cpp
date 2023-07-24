
#include <QtTest/QtTest>

#include "testbase.h"
#include "media.h"
#include "qtutil.h"

class TestSort : public TestBase {
  Q_OBJECT

 private Q_SLOTS:
  void initTestCase();
  // void cleanupTestCase() {}

  void testNumericCompare_data() { loadDataSet("misc", "numericsort"); }
  void testNumericCompare();
};

void TestSort::initTestCase() {
  QString dataDir = getenv("TEST_DATA_DIR");
  QVERIFY(QDir(dataDir).exists());
}

void TestSort::testNumericCompare() {
  QFETCH(QString, left);
  QFETCH(QString, right);
  QFETCH(int, order);

  QCollator collator;

  int result = qNumericSubstringCompare(collator, left, right);
  QCOMPARE(result, order);
}

QTEST_MAIN(TestSort)
#include "testsort.moc"
