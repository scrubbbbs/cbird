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
    baseInitTestCase(new DctHashIndex, "rename");
  }
  void cleanupTestCase() { baseCleanupTestCase(); }

  void testRename();
  void testMove();
  void testRenameZipped();
  void testMoveZipped();
  void testRenameDir();
  void testRenameZip();
  void testMoveDir();
  void testMoveZip();

 private:
  void existingPaths(bool archived, QString& path1, QString& path2);
};

void TestDatabase::existingPaths(bool archived, QString& path1, QString& path2) {
  const auto indexed = _database->indexedFiles();
  for (auto& i : indexed) {
    if (archived ^ Media(i).isArchived()) continue;
    else if (path1.isEmpty()) path1 = i;
    else if (path2.isEmpty()) {
      path2 = i;
      break;
    }
  }
}

void TestDatabase::testRename() {
  QString bogusPath = "bogus1";
  QString origPath, otherPath;
  existingPaths(false, origPath, otherPath);

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
  QString bogusPath = "bogus2";
  QString srcPath, otherPath;
  existingPaths(false, srcPath, otherPath);

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

void TestDatabase::testRenameZipped() {
  QString srcPath, otherPath;
  existingPaths(true, srcPath, otherPath);
  QString dstPath = srcPath + ".zrenamed";
  Media m(srcPath);
  QVERIFY(!_database->rename(m, dstPath)); // unsupported (v0.5.0)
}

void TestDatabase::testMoveZipped() {
  QString srcPath, otherPath;
  existingPaths(true, srcPath, otherPath);
  QString dstPath = srcPath + ".zrenamed";
  Media m(srcPath);
  QVERIFY(!_database->rename(m, dstPath)); // unsupported (v0.5.0)
}

void TestDatabase::testRenameDir() {
  const QString dirName = "dir";
  const MediaGroup dirContents = _database->mediaWithPathLike(dirName+"/%");
  QVERIFY(dirContents.count() > 0);
  const Media first = dirContents.first();
  const QString newName = "dir.renamed";

  // invalid src, not a dir
  QVERIFY(!_database->moveDir(first.path(), "foo"));

  // invalid src, has no parent
  QVERIFY(!_database->moveDir("/", "/tmp"));

  // rename to itself
  QVERIFY(!_database->moveDir(first.parentPath(), first.parentPath()));

  // /tmp1 is not index subdir
  QVERIFY(!_database->moveDir(first.parentPath(), "/tmp1"));

  // foo/bar is not a valid name
  QVERIFY(!_database->moveDir(first.parentPath(), "foo/bar"));

  // this should work
  QVERIFY(_database->moveDir(first.parentPath(), newName));

  QCOMPARE(_database->mediaWithPathLike(newName+"/%").count(),
           dirContents.count());

  // move it back, (restore the data dir)
  QDir dir(first.parentPath());
  dir.cdUp();
  QString newPath = dir.absoluteFilePath(newName);
  QVERIFY(_database->moveDir(newPath, dirName));

  QCOMPARE(_database->mediaWithPathLike(dirName+"/%").count(),
           dirContents.count());
}

void TestDatabase::testRenameZip() {
  QString srcPath, otherPath;
  existingPaths(true, srcPath, otherPath);

  QString zipPath, childPath;
  Media::archivePaths(srcPath, zipPath, childPath);
  const auto zipContents = _database->mediaWithPathLike(zipPath+"%");
  QVERIFY(zipContents.count() > 0);

  const QString zipName = QFileInfo(zipPath).fileName();
  const QString dstZipName = zipName + ".moved.zip";

  // dst is not a zip
  QVERIFY(!_database->moveDir(zipPath, "foo"));

  QVERIFY(_database->moveDir(zipPath, dstZipName));

  QCOMPARE(_database->mediaWithPathLike(dstZipName+"%").count(),
          zipContents.count());

  // move it back
  QVERIFY(_database->moveDir(zipPath+".moved.zip", zipName));

  QCOMPARE(_database->mediaWithPathLike(zipPath+"%").count(),
           zipContents.count());
}

void TestDatabase::testMoveDir() {
  const QString dirName = "dir";
  const MediaGroup dirContents = _database->mediaWithPathLike(dirName+"/%");
  QVERIFY(dirContents.count() > 0);
  const Media first = dirContents.first();
  const QString newName = "otherdir/dir.moved";

  // fail, dst dir does not exist
  QVERIFY(!_database->moveDir(first.parentPath(), "bogusDir/dir.moved"));

  QVERIFY(_database->moveDir(first.parentPath(), newName));

  QCOMPARE(_database->mediaWithPathLike(newName+"/%").count(),
           dirContents.count());

  // move it back, (restore the data dir)
  QDir dir(first.parentPath());
  dir.cdUp();
  QString newPath = dir.absoluteFilePath(newName);
  QVERIFY(_database->moveDir(newPath, first.parentPath()));

  QCOMPARE(_database->mediaWithPathLike(dirName+"/%").count(),
           dirContents.count());
}

void TestDatabase::testMoveZip() {
  QString srcPath, otherPath;
  existingPaths(true, srcPath, otherPath);

  QString zipPath, childPath;
  Media::archivePaths(srcPath, zipPath, childPath);
  const auto zipContents = _database->mediaWithPathLike(zipPath+"%");
  QVERIFY(zipContents.count() > 0);

  const QFileInfo info(zipPath);
  const QString zipName = info.fileName();
  const QString dstZipName = "otherdir/" + zipName + ".moved.zip";
  const QString dstZipPath = info.dir().absoluteFilePath(dstZipName);

  // fail, dst is not a zip
  QVERIFY(!_database->moveDir(zipPath, "otherdir/foo"));

  // fail, dst does not exist
  QVERIFY(!_database->moveDir(zipPath, "bogusdir/foo.zip"));

  QVERIFY(_database->moveDir(zipPath, dstZipName));

  QCOMPARE(_database->mediaWithPathLike(dstZipPath+"%").count(),
           zipContents.count());

  // move it back
  QVERIFY(_database->moveDir(dstZipPath, zipPath));

  QCOMPARE(_database->mediaWithPathLike(zipPath+"%").count(),
           zipContents.count());
}

QTEST_MAIN(TestDatabase)
#include "testdatabase.moc"
