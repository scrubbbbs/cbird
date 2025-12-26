#include <QtTest/QtTest>

#include "media.h"
#include "qtutil.h"

class TestPath : public QObject {
  Q_OBJECT

 private Q_SLOTS:
  void initTestCase();
  // void cleanupTestCase() {}

  void testIsArchive_data() { loadArchivePaths(); }
  void testIsArchive();

  void testArchivePath_data() { loadMemberPaths(); }
  void testArchivePath();

  void parseArchive_benchmark_data() { loadBenchmarkPaths(); }

 private:
  void parseArchive_benchmark();
  void loadArchivePaths();
  void loadMemberPaths();
  void loadBenchmarkPaths();
};

void TestPath::loadArchivePaths() {
  QTest::addColumn<bool>("isArchive");
  QTest::addColumn<QString>("path");

  static constexpr struct {
    bool isArchive;
    const char* path;
  } data[] = {{true, "file.zip"},
              {true, "/path/file.zip"},
              {true, "./file.zip"},
              {true, ".file.zip"},
              {true, ".zip"},
              {false, "file.zipp"},
              {false, "file.zi"},
              {false, "file."},
              {false, "file"},
              {false, ".file"},
              {false, "./file"},
              {false, ".."},
              {false, "."},
              {false, ""},
              {true, "file.ZIP"},
              {false, "file.ZiP"},
              {true, "file.cbz"},
              {true, "file.CBZ"},
              {false, "/path/zips/file.txt"},
              {false, "/path/zips/.zip.txt"},
              {false, "/path/.zip/file.txt"},
              {true, "/path/.zip/.zip"},
              // {false, "/path/file.rar:member.zip"},
              {false, "/path/file.zip:member.zip"},
              {false, "/path/file.zip:zips/member.txt"},
              {false, "/path/file.zip:zips/member.zip"}};

  for (auto& d : data) {
    QTestData& row = QTest::newRow(d.path);
    row << d.isArchive;
    row << QString(d.path);
  }
}

void TestPath::initTestCase() {
  QString dataDir = getenv("TEST_DATA_DIR");
  QVERIFY(QDir(dataDir).exists());
}

void TestPath::testIsArchive() {
  QFETCH(bool, isArchive);
  QFETCH(QString, path);
  QVERIFY(isArchive == Media::isArchive(path));
}

void TestPath::loadMemberPaths() {
  QTest::addColumn<bool>("isMember");
  QTest::addColumn<QString>("path");
  QTest::addColumn<QString>("parent");
  QTest::addColumn<QString>("member");

  static constexpr struct {
    bool isArchiveMember;
    const char* path;
    const char* parent = nullptr;
    const char* member = nullptr;
  } data[] = {
      {false, "file.zip"},
      {false, "/path/file.zip"},
      {false, "./file.zip"},
      {false, ".file.zip"},
      {false, ".zip"},
      {false, "file.zipp"},
      {false, "file.zi"},
      {false, "file."},
      {false, "file"},
      {false, ".file"},
      {false, "./file"},
      {false, ".."},
      {false, "."},
      {false, ""},
      {false, "file.ZIP"},
      {false, "file.ZiP"},
      {false, "file.cbz"},
      {false, "file.CBZ"},
      {false, "/path/zips/file.txt"},
      {false, "/path/zips/.zip.txt"},
      {false, "/path/.zip/file.txt"},
      {false, "/path/.zip/.zip"},
      // {true, "/path/file.rar:member.zip"},
      {true, "/path/file.zip:member.zip", "/path/file.zip", "member.zip"},
      {true, "/path/file.CBZ:member.zip", "/path/file.CBZ", "member.zip"},
      {true, "/path/file.zip:zips/member.txt", "/path/file.zip", "zips/member.txt"},
      {true, "/path/file.zip:zips/member.zip", "/path/file.zip", "zips/member.zip"},
      {true, "/zip/zip.zip:zip/zip.zip", "/zip/zip.zip", "zip/zip.zip"},
      {true, "f.zip:z", "f.zip", "z"},
      {true, ".zip:z", ".zip", "z"},
      {true, "..zip:z", "..zip", "z"},
      {true, "..zip:z", "..zip", "z"},
      {true, ".zip::z:", ".zip", ":z:"},
  };

  for (auto& d : data) {
    QTestData& row = QTest::newRow(d.path);
    row << d.isArchiveMember;
    row << QString(d.path);
    row << QString(d.parent);
    row << QString(d.member);
  }
}

void TestPath::testArchivePath() {
  QFETCH(bool, isMember);
  QFETCH(QString, path);
  QFETCH(QString, parent);
  QFETCH(QString, member);

  QVERIFY(isMember == Media::isArchived(path));

  QString parsedParent, parsedMember;
  Media::archivePaths(path, &parsedParent, &parsedMember);
  QCOMPARE(parent, parsedParent);
  QCOMPARE(member, parsedMember);

  if (isMember) {
    QVERIFY(Media::virtualPath(parsedParent, parsedMember) == path);
  }

  auto result = Media::parseArchivePath(path);
  QVERIFY(isMember == static_cast<bool>(result));
  if (isMember) {
    QCOMPARE(parent, QString(result->parentPath));
    QCOMPARE(member, QString(result->childPath));
  }
}

void TestPath::loadBenchmarkPaths() {
  QTest::addColumn<QString>("path");

  static constexpr struct {
    const char* path;
  } data[] = {
      {"C:/Users/Johnny Appleseed/Photos/Vacation/Florida/2012/DSC1439.jpg"},
      {"C:/Users/Johnny Appleseed/Photos/Vacation/Florida/2012.zip:DSC1439.jpg"},
      {"/mnt/tank/backup3/2009/December/Florida/day3/DSC1337.jpg"},
      {"/mnt/tank/backup3/2009/December/Florida.zip:day3/DSC1337.jpg"},
      {"/mnt/tank/zipbackup3.zip:2009/December/Florida/day3/DSC1337.jpg"},
      {"/mnt/tank/zipbackup3.DOCX:2009/December/Florida/day3/DSC1337.jpg"},
  };

  for (auto& d : data) {
    QTestData& row = QTest::newRow(d.path);
    row << QString(d.path);
  }
}

void TestPath::parseArchive_benchmark() {
  QFETCH(QString, path);

  volatile int sink = 0;
  QBENCHMARK {
    if (Media::isArchived(path)) {
      QString pParent, pMember;
      Media::archivePaths(path, &pParent, &pMember);
      sink += pParent.length() + pMember.length();
    }
  }

  QBENCHMARK {
    auto result = Media::parseArchivePath(path);
    if (result) {
      sink += result->parentPath.length() + result->childPath.length();
    }
  }
}

QTEST_MAIN(TestPath)
#include "testpath.moc"
