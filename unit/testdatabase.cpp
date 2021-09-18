#include <QtTest/QtTest>

#include "database.h"
#include "dcthashindex.h"
#include "testindexbase.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

class TestDatabase : public TestIndexBase {
  Q_OBJECT
 public:
  TestDatabase() {}
  virtual ~TestDatabase() {}

 private slots:
  void initTestCase() {
    baseInitTestCase(new DctHashIndex, "40x5-sizes/150x150");
  }
  void cleanupTestCase() { baseCleanupTestCase(); }

  void testRename();
  void testMove();
};

void TestDatabase::testRename() {
  QString bogusPath = "";
  QString origPath = *(_database->indexedFiles().begin());
  QString otherPath = *(++_database->indexedFiles().begin());

  // fail, rename not in the db
  Media missing = _database->mediaWithPath(bogusPath);
  QVERIFY(!_database->rename(missing, otherPath));

  // fail, new name exists
  Media exists = _database->mediaWithPath(origPath);
  QVERIFY(!_database->rename(exists, otherPath));

  // fail, rename to itself
  Media same = _database->mediaWithPath(origPath);
  QVERIFY(!_database->rename(same, origPath));

  // rename
  QString newPath = origPath + ".moved";
  Media moved = _database->mediaWithPath(origPath);
  QVERIFY(_database->rename(moved, newPath));

  QVERIFY(_database->indexedFiles().contains(newPath));

  // rename it back
  QVERIFY(_database->rename(moved, origPath));
  QVERIFY(_database->indexedFiles().contains(origPath));
}

void TestDatabase::testMove() {
  QString bogusPath = "";
  QString srcPath = *(_database->indexedFiles().begin());
  QString otherPath = *(++_database->indexedFiles().begin());

  QString srcDir = QFileInfo(srcPath).absoluteDir().absolutePath();
  QString dstDir = srcDir + "/newdir";
  QString dstName = QFileInfo(srcPath).fileName();

  // fail, src not in the db
  {
    Media m = _database->mediaWithPath(bogusPath);
    QVERIFY(!_database->move(m, otherPath));
  }

  // fail, dst isn't a dir
  {
    Media m = _database->mediaWithPath(srcPath);
    QVERIFY(!_database->move(m, otherPath));
  }

  // fail, dst dir doesn't exist
  {
    Media m = _database->mediaWithPath(srcPath);
    QVERIFY(!_database->move(m, dstDir));
  }

  // fail, dst dir invalid
  {
    Media m = _database->mediaWithPath(srcPath);
    QVERIFY(!_database->move(m, "/tmp"));
  }

  // fail, dst file exists
  {
    Media m = _database->mediaWithPath(srcPath);
    QVERIFY(!_database->move(m, otherPath));
  }

  // fail, move to itself
  {
    Media m = _database->mediaWithPath(srcPath);
    QVERIFY(!_database->move(m, srcPath));
  }

  // make dir
  qWarning("mkdir %s", qPrintable(dstDir));
  QVERIFY(0 == mkdir(qPrintable(dstDir), 0700));

  // move file
  QString dstPath = dstDir + "/" + dstName;

  Media before = _database->mediaWithPath(srcPath);
  QVERIFY(before.path() == srcPath);
  QVERIFY(_database->move(before, dstDir));
  QVERIFY(before.path() == dstPath);

  QVERIFY(!QFileInfo(srcPath).exists());
  QVERIFY(QFileInfo(dstPath).exists());

  QVERIFY(_database->indexedFiles().contains(dstPath));
  QVERIFY(!_database->indexedFiles().contains(srcPath));

  QVERIFY(_database->mediaWithId(before.id()).path() == dstPath);
  QVERIFY(_database->mediaWithPath(dstPath).id() == before.id());

  // move it back
  Media after = _database->mediaWithPath(before.path());
  QVERIFY(after.id() == before.id());

  QVERIFY(after.path() == dstPath);
  QVERIFY(_database->move(after, srcDir));
  QVERIFY(after.path() == srcPath);

  QVERIFY(QFileInfo(srcPath).exists());
  QVERIFY(!QFileInfo(dstPath).exists());

  QVERIFY(_database->indexedFiles().contains(srcPath));
  QVERIFY(!_database->indexedFiles().contains(dstPath));

  QVERIFY(_database->mediaWithId(after.id()).path() == srcPath);
  QVERIFY(_database->mediaWithPath(srcPath).id() == after.id());

  // remove dir
  qWarning("rmdir %s", qPrintable(dstDir));
  QVERIFY(0 == rmdir(qPrintable(dstDir)));
}

QTEST_MAIN(TestDatabase)
#include "testdatabase.moc"
