#pragma once

class Scanner;
class Database;
class Index;
class Media;

class TestIndexBase : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void mediaProcessed(const Media& m);

protected:
    void baseInitTestCase(Index* index, const QString& dataSet, bool enableFeatures);
    void baseTestDefaults(Index* index);
    void baseTestEmpty(Index* index);

    QString _dataDir;
    Database* _database;
    Scanner*  _scanner;
    Index* _index;
};

