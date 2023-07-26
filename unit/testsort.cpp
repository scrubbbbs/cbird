
#include <QtTest/QtTest>

#include "media.h"
#include "qtutil.h"
#include "testbase.h"

class TestSort : public TestBase {
  Q_OBJECT

 private Q_SLOTS:
  void initTestCase();
  // void cleanupTestCase() {}

  void testNumericCompare_data() { loadDataSet("misc", "numericsort"); }
  void testNumericCompare();

  void testSortGroup();
  void testSortGroupList();

 private:
  MediaGroup readDir(const QString& dir);
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

void TestSort::testSortGroup() {
  MediaGroup g = readDir(qq("exif-samples"));
  QCollator col;
  // string sort
  Media::sortGroup(g, {"path"});
  for (int i = 1; i < g.count(); ++i) {
    int order = col.compare(g.at(i - 1).path(), g.at(i).path());
    QVERIFY(order <= 0);
  }

  // integer sort
  Media::sortGroup(g, {"fileSize"});
  for (int i = 1; i < g.count(); ++i) {
    auto& left = g.at(i - 1);
    auto& right = g.at(i);
    QCOMPARE_LE(left.originalSize(), right.originalSize());
  }

  // date sort
  const char* prop = "exif#Photo.DateTimeOriginal#todate";
  auto propFn = Media::propertyFunc(prop);
  Media::sortGroup(g, {prop});
  for (int i = 1; i < g.count(); ++i) {
    auto& left = g.at(i - 1);
    auto& right = g.at(i);
    QCOMPARE_LE(propFn(left), propFn(right));
  }

  // multisort, sort fileSize descending
  int secondary = 0;  // make sure we tested secondary sort
  Media::sortGroup(g, {"suffix", "^fileSize"});
  for (int i = 1; i < g.count(); ++i) {
    auto& left = g.at(i - 1);
    auto& right = g.at(i);
    int order = col.compare(left.suffix(), right.suffix());
    QVERIFY(order <= 0);

    if (order == 0) {
      secondary++;
      QCOMPARE_GE(left.originalSize(), right.originalSize());
    }
  }
  QVERIFY(secondary > 0);
}

void TestSort::testSortGroupList() {
  const MediaGroup g_ = readDir(qq("exif-samples"));

  MediaGroupList gl = Media::groupBy(g_, "dirPath");
  QVERIFY(gl.count() > 1);

  QCollator col;
  Media::sortGroupList(gl, {"path"});
  for (int i = 1; i < gl.count(); ++i) {
    auto& left = gl.at(i - 1).at(0);
    auto& right = gl.at(i).at(0);
    int order = col.compare(left.path(), right.path());
    QVERIFY(order <= 0);
  }

  int secondary = 0;
  Media::sortGroupList(gl, {"suffix", "^fileSize"});
  for (int i = 1; i < gl.count(); ++i) {
    auto& left = gl.at(i - 1).at(0);
    auto& right = gl.at(i).at(0);
    int order = col.compare(left.suffix(), right.suffix());
    QVERIFY(order <= 0);
    if (order == 0) {
      secondary++;
      QCOMPARE_GE(left.originalSize(), right.originalSize());
    }
  }
  QVERIFY(secondary > 0);

  // test empty-sorts-first
  gl.append(MediaGroup());
  Media::sortGroupList(gl, {"completeBaseName"});
  QVERIFY(gl.at(0).isEmpty());
}

MediaGroup TestSort::readDir(const QString& dir) {
  QString dataDir = qEnvironmentVariable("TEST_DATA_DIR") + "/" + dir;

  MediaGroup g;
  QDirIterator it(dataDir, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    Media m(it.next());
    m.readMetadata();
    g += m;
  }
  return g;
}

QTEST_MAIN(TestSort)
#include "testsort.moc"
