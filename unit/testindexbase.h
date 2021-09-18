#pragma once

class Scanner;
class Database;
class Index;
class Media;
class SearchParams;
typedef QVector<Media> MediaGroup;

class TestIndexBase : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void mediaProcessed(const Media& m);

protected:
    void baseInitTestCase(Index* index, const QString& dataSet);
    void baseCleanupTestCase();
    void baseTestDefaults(Index* index);
    void baseTestEmpty(Index* index);
    void baseTestLoad(const SearchParams& params);
    void baseTestAddRemove(const SearchParams& params, int expectedMatches);

    QString _dataDir;
    Database* _database;
    Scanner*  _scanner;
    Index* _index;
    MediaGroup* _mediaProcessed;
};

