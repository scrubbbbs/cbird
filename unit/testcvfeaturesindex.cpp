
#include "testindexbase.h"
#include "cvfeaturesindex.h"
#include "database.h"
#include "scanner.h"

#include <QtTest/QtTest>

class TestCvFeaturesIndex : public TestIndexBase
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();        // called at startup/shutdown
    //void cleanupTestCase() {}
    //void init() {}              // called before/after each unit test (testXXX method)
    //void cleanup() {}

    void testDefaults()   { baseTestDefaults(new CvFeaturesIndex); }
    void testEmpty()      { baseTestEmpty(new CvFeaturesIndex); }

    void testMemoryUsage();
    void testLoad();
    void testAddRemove();

private:
    SearchParams _params;
};

void TestCvFeaturesIndex::initTestCase()
{
    baseInitTestCase(new CvFeaturesIndex, "40x5-sizes", true);

    _params.algo = _index->id();
    _params.verbose = true;
    _params.maxMatches = 5;
}

void TestCvFeaturesIndex::testMemoryUsage()
{
   // load index
   (void)_database->similar(_params);

    QVERIFY(_index->memoryUsage() > 0);
}

void TestCvFeaturesIndex::testLoad()
{
    MediaGroupList results = _database->similar(_params);

    QVERIFY(_index->memoryUsage() > 0);

    // ideally we would get 40 sets of 5, but that isn't going
    // to happen since the search is imprecise.
    QVERIFY(results.count() <= 40);

    // the features match some sets will not match completely,
    // not sure right now how to spec this. by definition
    // it must be > 1
    for (const MediaGroup& group : results)
        QVERIFY(group.count() > 1);

    // look up every path and we should get the 5 that matched it,
    // that includes matching itself. much slower since we
    // are processing the file as the scanner would

    // note: QSet uses randomized hashing, sort it so we
    // get consistent results
    QStringList indexed = _database->indexedFiles().values();
    qSort(indexed);

    for (const QString& path : indexed)
    {
        QVERIFY(QFileInfo(path).exists());

        // we won't neccessarily get keypoints. maybe if image is too small
        Media needle = _scanner->processImageFile(path).media;
        if (needle.keyPointHashes().size() > 0)
        {
            // we won't get a match for every needle since maybe it never
            // stored any keypoints in the db
            MediaGroup group = _database->similarTo(needle, _params);
            if (group.count() <= 1)
                printf("no matches: %s\n", qPrintable(path));
        }
        else
            printf("no keypoints: %s\n", qPrintable(path));
    }
}

void TestCvFeaturesIndex::testAddRemove()
{
    MediaGroupList before = _database->similar(_params);

    QVERIFY(before.count() <= 40);

    MediaGroup removed;
    removed.append(before[0][0]);
    removed.append(before[1][1]);
    removed.append(before[2][2]);
    QVERIFY(removed.contains(removed[0]));

    Media m = _scanner->processImageFile(removed[0].path()).media;
    MediaGroup g = _database->similarTo(m, _params);
    QVERIFY(g.contains(m));

    _database->remove(removed);

    g = _database->similarTo(m, _params);
    QVERIFY(!g.contains(m));

    // if we re-scan they should show up again
    QSet<QString> skip = _database->indexedFiles();
    _scanner->scanDirectory(_database->path(), skip);
    _scanner->finish();

    //= _scanner->processImageFile(removed[0].path());
    g = _database->similarTo(m, _params);
    QVERIFY(g.contains(m));
}

QTEST_MAIN(TestCvFeaturesIndex)
#include "testcvfeaturesindex.moc"
