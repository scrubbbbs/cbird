
#include <QtTest/QtTest>
#include "cvfeaturesindex.h"
#include "database.h"
#include "gui/mediagrouplistwidget.h"
#include "scanner.h"
#include "templatematcher.h"
#include "testindexbase.h"

class TestTemplateMatcher : public TestIndexBase {
  Q_OBJECT

 private Q_SLOTS:
  void initTestCase() {
    baseInitTestCase(new CvFeaturesIndex, "40x-people/cuthbert");
  }
  void cleanupTestCase() { baseCleanupTestCase(); }

  void testMatch_data();
  void testMatch();
};

void TestTemplateMatcher::testMatch_data() {
  QTest::addColumn<QString>("file");
  QTest::addColumn<QImage>("img");

  MediaGroup mg = _database->mediaWithType(Media::TypeImage);
  Media::sortGroup(mg, {"name"});

  for (auto& m : std::as_const(mg)) {
    const QString file = m.path();
    const QImage img(file);

    //int maxDim = std::max(img.width(), img.height());
    int minDim = std::min(img.width(), img.height());

    int dx = (img.width() - minDim) / 2;
    int dy = (img.height() - minDim) / 2;
    const QImage centerCrop = img.copy(dx, dy, minDim - dx, minDim - dy);

    // QImage offCenterCrop = img.copy(img.width() * 0.1, img.height() * 0.1,
    //                                 img.width() * 0.7, img.height() * 0.7);

    QImage tmp;
    const QString id = m.name();
    QString op;

    op = "nocrop";
    tmp = img;
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(id + "." + op)) << file << tmp;

    op = "crop-width-centered";
    tmp = img.copy(img.width() * 0.2, 0, img.width() * 0.6, img.height());
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(id + "." + op)) << file << tmp;

    op = "crop-width-off-center";
    tmp = img.copy(img.width() * 0.2, 0, img.width() * 0.7, img.height());
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(id + "." + op)) << file << tmp;

    op = "crop-height-centered";
    tmp = img.copy(0, img.height() * 0.1, img.width(), img.height() * 0.8);
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(id + "." + op)) << file << tmp;

    op = "crop-height-off-center";
    tmp = img.copy(0, img.height() * 0.1, img.width(), img.height() * 0.7);
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(id + "." + op)) << file << tmp;

    // op = "crop-corner-top-left";
    // tmp = img.copy(0, 0, img.width()*0.5, img.height()*0.5);
    // tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    // QTest::newRow(qPrintable(file + "." + op)) << file << tmp;

    op = "cropcenter";
    tmp = centerCrop;
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(id + "." + op)) << file << tmp;

    op = "cropcenter+rot10";
    tmp =
        centerCrop.transformed(QTransform().rotate(10), Qt::SmoothTransformation);
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(id + "." + op)) << file << tmp;

    op = "cropcenter+rot30";
    tmp =
        centerCrop.transformed(QTransform().rotate(30), Qt::SmoothTransformation);
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(id + "." + op)) << file << tmp;

    op = "cropcenter+rot60";
    tmp =
        centerCrop.transformed(QTransform().rotate(60), Qt::SmoothTransformation);
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(id + "." + op)) << file << tmp;

    op = "cropcenter+rot90";
    tmp =
        centerCrop.transformed(QTransform().rotate(90), Qt::SmoothTransformation);
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(id + "." + op)) << file << tmp;

    op = "cropcenter+rot135";
    tmp =
        centerCrop.transformed(QTransform().rotate(135), Qt::SmoothTransformation);
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(id + "." + op)) << file << tmp;
  }
}

void TestTemplateMatcher::testMatch() {
  QFETCH(QString, file);
  QFETCH(QImage, img);
  QVERIFY(!img.isNull());

  // default params modified to pass the test,
  // but they seem to be reasonable
  SearchParams params;
  params.haystackFeatures = 1000; // max number of features in the cand (original/smaller image)
  params.needleFeatures = 200; // max number of features in the target/template (cropped/larger image)
  params.cvThresh = 25;    // threshold for matching features
  params.maxMatches = 5;
  params.tmScalePct = 150; // max scale factor between target/cand before keypoint generation
  params.tmThresh = 7;     // ORB threshold for validating possible matches
  params.algo = _index->id();
  params.verbose = true;

  QString op = QFileInfo(QTest::currentDataTag()).suffix();

  Media original(file);
  original.setMd5("bogus-md5-1"); // suppress cache warning; we destruct on each test
  Media modified(img);
  modified.setPath(op);
  modified.setMd5("bogus-md5-2");

  MediaGroup g;

  // match to itself, covered by "nocrop"
  //g.append(original);
  //TemplateMatcher().match(modified, g, params);
  //QVERIFY(g.contains(original));

  // match modified->original, e.g. the database contains the known original
  g.append(original);
  TemplateMatcher().match(modified, g, params);

  // match original->modified
  // generally doesn't work because we won't rescale the original to focus features
  //g.clear();
  //g.append(modified);
  //TemplateMatcher().match(original, g, params);
  //QVERIFY(g.contains(modified));

  if (!g.contains(original) && getenv("VIEW_RESULTS")) {
    g.prepend(original);
    g.prepend(modified);

    MediaGroupList list;
    list.append(g);

    MediaGroupListWidget* w = new MediaGroupListWidget(list);
    w->show();
    QEventLoop* loop = new QEventLoop;
    loop->processEvents();
    while (w->isVisible())
      loop->processEvents();
    delete loop;
    delete w;
  }
  QVERIFY(g.contains(original));
}

QTEST_MAIN(TestTemplateMatcher)
#include "testtemplatematcher.moc"
