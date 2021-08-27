
#include <QtTest/QtTest>

// test template
// Copy this file and the .pro to a subdir and rename MyClass to the class you are testing.

class TestMyClass : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();        // called at startup/shutdown
    //void cleanupTestCase() {}
    //void init() {}              // called before/after each unit test (testXXX method)
    //void cleanup() {}

    void testNothing();

private:
    QString _dataDir;
};

void TestMyClass::initTestCase()
{
    _dataDir = getenv("TEST_DATA_DIR");
    QVERIFY(QDir(_dataDir).exists());
}

void TestMyClass::testNothing()
{
    QVERIFY(false);
}

QTEST_MAIN(TestMyClass)
#include "testmyclass.moc"
