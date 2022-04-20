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

  void testNegativeMatch();
  void testWeeds();

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
  QVERIFY(!_database->moveDir(first.dirPath(), first.dirPath()));

  // /tmp1 is not index subdir
  QVERIFY(!_database->moveDir(first.dirPath(), "/tmp1"));

  // foo/bar is not a valid name
  QVERIFY(!_database->moveDir(first.dirPath(), "foo/bar"));

  // this should work
  QVERIFY(_database->moveDir(first.dirPath(), newName));

  QCOMPARE(_database->mediaWithPathLike(newName+"/%").count(),
           dirContents.count());

  // move it back, (restore the data dir)
  QDir dir(first.dirPath());
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
  QVERIFY(!_database->moveDir(first.dirPath(), "bogusDir/dir.moved"));

  QVERIFY(_database->moveDir(first.dirPath(), newName));

  QCOMPARE(_database->mediaWithPathLike(newName+"/%").count(),
           dirContents.count());

  // move it back, (restore the data dir)
  QDir dir(first.dirPath());
  dir.cdUp();
  QString newPath = dir.absoluteFilePath(newName);
  QVERIFY(_database->moveDir(newPath, first.dirPath()));

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

void TestDatabase::testNegativeMatch() {
  {
    Media needle, deleted;
    _database->addNegativeMatch(needle, deleted);
    QVERIFY(!_database->isNegativeMatch(needle, deleted));
  }

  MediaGroupList dups =_database->dupsByMd5(SearchParams());
  QVERIFY(dups.count() >= 3);
  MediaGroup& g = dups[0];
  QVERIFY(g.count() >= 2);

  // fail, same md5
  _database->addNegativeMatch(g[0], g[1]);
  QVERIFY(!_database->isNegativeMatch(g[0],g[1]));

  const Media& a = dups[0][0];
  const Media& b = dups[1][0];
  const Media& c = dups[2][0];

  QVERIFY(!_database->isNegativeMatch(a,b));
  QVERIFY(!_database->isNegativeMatch(a,c));

  _database->addNegativeMatch(a, b);
  _database->addNegativeMatch(b, a);
  _database->addNegativeMatch(a, c);

  QVERIFY(_database->isNegativeMatch(a,b));
  QVERIFY(_database->isNegativeMatch(b,a));
  QVERIFY(_database->isNegativeMatch(c,a));

  _database->unloadNegativeMatches();
  QVERIFY(_database->isNegativeMatch(a,b));
  QVERIFY(_database->isNegativeMatch(b,a));
  QVERIFY(_database->isNegativeMatch(c,a));

  MediaGroup g1({a,b,c});
  g1 = _database->filterNegativeMatches(g1);
  Media::printGroup(g1);
  QVERIFY(g1.count() == 0); // a,b a,c are neg;

  MediaGroup g2({b,c,a});
  g2 = _database->filterNegativeMatches(g2);
  QVERIFY(g2.count() == 2); // a removed but not c
  QVERIFY(g2[0] == b);
  QVERIFY(g2[1] == c);

  MediaGroup g3({a});
  g3 = _database->filterNegativeMatches(g3);
  QVERIFY(g3.count() == 1);
  QVERIFY(g3[0] == a);
}

void TestDatabase::testWeeds() {

  {
    // fail, no md5 sums
    Media weed, orig;
    QVERIFY(!_database->addWeed(weed, orig));
    QVERIFY(!_database->isWeed(weed));
  }

  MediaGroupList dups =_database->dupsByMd5(SearchParams());
  QVERIFY(dups.count() >= 3);
  MediaGroup& g = dups[0];
  QVERIFY(g.count() >= 2);

  // fail, same md5 sums
  QVERIFY(!_database->addWeed(g[0], g[1]));
  QVERIFY(!_database->isWeed(g[0]));
  QVERIFY(!_database->isWeed(g[1]));

  const Media& orig = dups[0][0];
  const Media& weed1 = dups[1][0];
  const Media& weed2 = dups[2][0];

  QVERIFY(!_database->isWeed(weed1)); // not added yet
  QVERIFY(_database->addWeed(weed1, orig));
  QVERIFY(_database->isWeed(weed1));
  QVERIFY(!_database->isWeed(orig)); // reverse is not true...

  QVERIFY(_database->addWeed(weed1, orig)); // re-adding, ignore it
  QVERIFY(!_database->addWeed(weed1, weed2)); // already added, different orig

  // second weed
  QVERIFY(!_database->isWeed(weed2));
  QVERIFY(!_database->addWeed(weed2, weed1)); // illegal, orig is a weed
  QVERIFY(_database->addWeed(weed2, orig)); // legal, multiple orig allowed
  QVERIFY(_database->isWeed(weed2));

  // unload/re-load from file
  _database->unloadWeeds();
  QVERIFY(_database->isWeed(weed1));
  QVERIFY(_database->isWeed(weed2));

  // removal of original invalidates (orphans) the weed records
  _database->remove(_database->mediaWithMd5(orig.md5()));
  QVERIFY(0 == _database->mediaWithMd5(orig.md5()).count());

  QVERIFY(!_database->isWeed(weed1)); // still present, but orphaned
  QVERIFY(!_database->isWeed(weed2));

  MediaGroup grp{orig};
  _database->add(grp);
  QVERIFY(_database->isWeed(weed1)); // add back, weed again
  QVERIFY(_database->isWeed(weed2));

  QVERIFY(_database->removeWeed(weed1));
  QVERIFY(!_database->isWeed(weed1));
  QVERIFY(_database->isWeed(weed2));

  _database->unloadWeeds();
  QVERIFY(!_database->isWeed(weed1));
  QVERIFY(_database->isWeed(weed2));
}

QTEST_MAIN(TestDatabase)
#include "testdatabase.moc"
