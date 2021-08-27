
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
    baseInitTestCase(new CvFeaturesIndex, "40x-people/cuthbert", false);
  }
  // void cleanupTestCase() {}
  // void init() {}              // called before/after each unit test (testXXX
  // method) void cleanup() {}

  void testMatch_data();
  void testMatch();

 private:
};

void TestTemplateMatcher::testMatch_data() {
  QTest::addColumn<QString>("file");
  QTest::addColumn<QImage>("img");

  for (QString file : _database->indexedFiles()) {
    QString op;
    QImage img(file);

    int maxDim = std::max(img.width(), img.height());
    int minDim = std::min(img.width(), img.height());

    int dx = (img.width() - minDim) / 2;
    int dy = (img.height() - minDim) / 2;
    QImage centerCrop = img.copy(dx, dy, minDim - dx, minDim - dy);

    QImage offCenterCrop = img.copy(img.width() * 0.1, img.height() * 0.1,
                                    img.width() * 0.7, img.height() * 0.7);

    QImage tmp;

    op = "nocrop";
    tmp = img;
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(file + "." + op)) << file << tmp;

    op = "crop-width-centered";
    tmp = img.copy(img.width() * 0.2, 0, img.width() * 0.6, img.height());
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(file + "." + op)) << file << tmp;

    op = "crop-width-off-center";
    tmp = img.copy(img.width() * 0.2, 0, img.width() * 0.7, img.height());
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(file + "." + op)) << file << tmp;

    op = "crop-height-centered";
    tmp = img.copy(0, img.height() * 0.1, img.width(), img.height() * 0.8);
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(file + "." + op)) << file << tmp;

    op = "crop-height-off-center";
    tmp = img.copy(0, img.height() * 0.1, img.width(), img.height() * 0.7);
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(file + "." + op)) << file << tmp;

    op = "cropcenter";
    tmp = centerCrop;
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(file + "." + op)) << file << tmp;

    op = "cropcenter+rot10";
    tmp =
        centerCrop.transformed(QMatrix().rotate(10), Qt::SmoothTransformation);
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(file + "." + op)) << file << tmp;

    op = "cropcenter+rot30";
    tmp =
        centerCrop.transformed(QMatrix().rotate(30), Qt::SmoothTransformation);
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(file + "." + op)) << file << tmp;

    op = "cropcenter+rot60";
    tmp =
        centerCrop.transformed(QMatrix().rotate(60), Qt::SmoothTransformation);
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(file + "." + op)) << file << tmp;

    op = "cropcenter+rot90";
    tmp =
        centerCrop.transformed(QMatrix().rotate(90), Qt::SmoothTransformation);
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(file + "." + op)) << file << tmp;

    op = "cropcenter+rot135";
    tmp =
        centerCrop.transformed(QMatrix().rotate(135), Qt::SmoothTransformation);
    tmp = tmp.scaledToHeight(256, Qt::SmoothTransformation);
    QTest::newRow(qPrintable(file + "." + op)) << file << tmp;
  }
}

void TestTemplateMatcher::testMatch() {
  QFETCH(QString, file);
  QFETCH(QImage, img);
  QVERIFY(!img.isNull());

  SearchParams params;
  params.needleFeatures = 100;
  params.cvThresh = 25;
  params.maxMatches = 5;
  params.dctThresh = 11;
  params.algo = _index->id();

  QString op = QFileInfo(QTest::currentDataTag()).suffix();

  Media original(file);
  Media modified(img);
  modified.setPath(op);

  MediaGroup g;

  // match modified->original, e.g. the database contains the known original
  g.append(original);
  TemplateMatcher().match(modified, g, params);

  // match original->modified
  // g.append(modified);
  // TemplateMatcher().match(original, g, params);

  if (!g.contains(original) && getenv("VIEW_RESULTS")) {
    g.prepend(original);
    g.prepend(modified);

    MediaGroupList list;
    list.append(g);

    MediaGroupListWidget* w =
        new MediaGroupListWidget(list, nullptr, 0, _database);
    w->show();
    QEventLoop* loop = new QEventLoop;
    loop->processEvents();
    while (w->isVisible()) loop->processEvents();
    delete loop;
    delete w;
    QFAIL("no match");
  }
}

QTEST_MAIN(TestTemplateMatcher)
#include "testtemplatematcher.moc"
