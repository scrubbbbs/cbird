
#include <QtTest/QtTest>

#include "ioutil.h"
#include "media.h"
#include "qtutil.h"

class TestVideoIndex : public QObject {
  Q_OBJECT

 private Q_SLOTS:
  void initTestCase();
  void cleanupTestCase() {}

  void testV1Load();
  void testV1Save();

  void testLoad();
  void testSave();

 private:
  QString _dataDir;
};

void TestVideoIndex::initTestCase() {
  _dataDir = getenv("TEST_DATA_DIR");
  _dataDir += "/videoindex";
  QVERIFY(QDir(_dataDir).exists());
}

void TestVideoIndex::testV1Load() {
  {
    // NOTE: version1.vdx comes from xiph-video/mp4/highway_cif.mp4
    VideoIndex v;
    QString path = QString("%1/version1.vdx").arg(_dataDir);
    SimpleIO io;
    QVERIFY(io.open(path, true));
    QVERIFY(v.load_v1(io));
    QCOMPARE(v.frames.size(), size_t(201));
    QCOMPARE(v.hashes.size(), size_t(201));
  }

  {
    VideoIndex v;
    QString path = QString("%1/version1-truncated.vdx").arg(_dataDir);
    QVERIFY(!v.isValid(path));
    SimpleIO io;
    QVERIFY(io.open(path, true));
    QVERIFY(!v.load_v1(io));
  }

  {
    VideoIndex v;
    QString path = QString("%1/empty.vdx").arg(_dataDir);
    QVERIFY(!v.isValid(path));
    SimpleIO io;
    QVERIFY(io.open(path, true));
    QVERIFY(!v.load_v1(io));
  }

  {
    VideoIndex v;
    QString path = QString("%1/version1-empty.vdx").arg(_dataDir);
    QVERIFY(v.isValid(path));
    SimpleIO io;
    QVERIFY(io.open(path, true));
    QVERIFY(v.load_v1(io));
  }
}

void TestVideoIndex::testV1Save() {
  QString path = QString("%1/temp_v1.vdx").arg(_dataDir);
  QFile::remove(path);

  {
    VideoIndex a;
    a.frames = {10, 30};
    a.hashes = {10101010, 30303030};
    SimpleIO io;
    QVERIFY(io.open(path, false));
    QVERIFY(a.save_v1(io));
    QVERIFY(QFileInfo(path).exists());

    VideoIndex b;
    QVERIFY(io.open(path, true)); // reopen supported
    QVERIFY(b.load_v1(io));
    QCOMPARE(b.frames, a.frames);
    QCOMPARE(b.hashes, a.hashes);
  }

  QVERIFY(QFile::remove(path));
}

void TestVideoIndex::testLoad() {
  // test backwards-compatible loading
  VideoIndex v1;
  v1.load(QString("%1/version1.vdx").arg(_dataDir));
  QCOMPARE(v1.frames.size(), size_t(201));
  QCOMPARE(v1.frames[0], 0);
  QCOMPARE(v1.frames[v1.frames.size() - 1], 1999);
  QCOMPARE(v1.frames.size(), v1.hashes.size());

  // creating the sample file
  // QVERIFY(v1.save_v2(QString("%1/version2.vdx").arg(_dataDir)));
  // VideoIndex v2;
  // QVERIFY(v2.load_v2(QString("%1/version2.vdx").arg(_dataDir)));
  // QCOMPARE(v1.frames, v2.frames);
  // QCOMPARE(v1.hashes, v2.hashes);
  // return;

  // test loading the sample file
  {
    VideoIndex v2;
    QString path = QString("%1/version2.vdx").arg(_dataDir);
    QVERIFY(v2.isValid(path));
    v2.load(path);
    QCOMPARE(v1.frames, v2.frames);
    QCOMPARE(v1.hashes, v2.hashes);
  }

  {
    VideoIndex v2;
    QString path = QString("%1/empty.vdx").arg(_dataDir);
    QVERIFY(!v2.isValid(path));
    v2.load(path);
    QCOMPARE(v2.frames.size(), size_t(0));
    QCOMPARE(v2.hashes.size(), size_t(0));
  }

  {
    VideoIndex v2;
    QString path = QString("%1/version2-empty.vdx").arg(_dataDir);
    QVERIFY(v2.isValid(path));
    v2.load(path);
    QCOMPARE(v2.frames.size(), size_t(0));
    QCOMPARE(v2.hashes.size(), size_t(0));
  }

  {
    VideoIndex v2;
    QString path = QString("%1/version2-truncated.vdx").arg(_dataDir);
    QVERIFY(!v2.isValid(path));
    v2.load(path);
    QCOMPARE(v2.frames.size(), size_t(0));
    QCOMPARE(v2.hashes.size(), size_t(0));
  }
}

void TestVideoIndex::testSave() {
  const QString path = QString("%1/temp_v2.vdx").arg(_dataDir);

  // test converting v1 file
  {
    VideoIndex v1;
    v1.load(QString("%1/version1.vdx").arg(_dataDir));
    v1.save(path);
    VideoIndex v2;
    v2.load(path);
    QCOMPARE(v1.frames, v2.frames);
    QCOMPARE(v1.hashes, v2.hashes);
  }

  // test empty
  {
    QFile::remove(path);
    VideoIndex().save(path);
    QVERIFY(QFileInfo(path).exists());
    VideoIndex v2;
    v2.load(path);
    QCOMPARE(v2.frames.size(), size_t(0));
    QFile::remove(path);
  }

  // synthetic data (small offsets)
  {
    QFile::remove(path);
    VideoIndex a;
    a.frames = {0, 1, 2, 3};
    a.hashes = {4, 3, 2, 1};
    a.save(path);

    VideoIndex b;
    b.load(path);
    QCOMPARE(a.frames, b.frames);
    QCOMPARE(a.hashes, b.hashes);
  }

  // big offset in the middle
  {
    QFile::remove(path);
    VideoIndex a;
    a.frames = {0, 1, 2000, 2001};
    a.hashes = {4, 3, 2, 1};
    a.save(path);

    VideoIndex b;
    b.load(path);
    QCOMPARE(a.frames, b.frames);
    QCOMPARE(a.hashes, b.hashes);
  }

  // big offset at the end
  {
    QFile::remove(path);
    VideoIndex a;
    a.frames = {0, 1, 2, 2000};
    a.hashes = {4, 3, 2, 1};
    a.save(path);

    VideoIndex b;
    b.load(path);
    QCOMPARE(a.frames, b.frames);
    QCOMPARE(a.hashes, b.hashes);
  }

  // big offset at the start
  {
    QFile::remove(path);
    VideoIndex a;
    a.frames = {0, 1000, 1001, 1002};
    a.hashes = {4, 3, 2, 1};
    a.save(path);
    VideoIndex b;
    b.load(path);
    QCOMPARE(a.frames, b.frames);
    QCOMPARE(a.hashes, b.hashes);
  }

  // all big offsets
  {
    QFile::remove(path);
    VideoIndex a;
    a.frames = {0, 1000, 2000, 3000};
    a.hashes = {4, 3, 2, 1};
    a.save(path);

    VideoIndex b;
    b.load(path);
    QCOMPARE(a.frames, b.frames);
    QCOMPARE(a.hashes, b.hashes);
  }

  // mix
  {
    QFile::remove(path);
    VideoIndex a;
    a.frames = {0, 1000, 1001, 2000, 2001, 3000, 3001, 4000};
    a.hashes = {4, 3, 2, 1, 1, 2, 3, 4};
    a.save(path);

    VideoIndex b;
    b.load(path);
    QCOMPARE(a.frames, b.frames);
    QCOMPARE(a.hashes, b.hashes);
  }

  QFile::remove(path);
}

QTEST_MAIN(TestVideoIndex)
#include "testvideoindex.moc"
