#include "testbase.h"

#include <QtTest/QtTest>

const char* TestBase::qCString(const QString& str) {
  static QMap<QString, const char*> map;

  if (map.contains(str))
    return map[str];
  else {
    const char* cstr = strdup(qPrintable(str));
    map[str] = cstr;
    return cstr;
  }
}

void TestBase::readDataSet(const QString& name, const QString& testName,
                           const QStringList& extraColumns) {
  _dataRoot = getenv("TEST_DATA_DIR");
  _dataRoot += "/" + name + "/";

  _dataNames.clear();
  _dataTypes.clear();
  _dataRows.clear();

  // first row, data field names
  // second row, data field types
  // the rest, test data

  QFile data(_dataRoot + testName + ".csv");
  if (!data.open(QFile::ReadOnly)) qFatal("failure to open %s", qCString(data.fileName()));

  QStringList lines = QString(data.readAll()).split("\n");
  if (lines.count() < 3)
    qFatal("data set CSV requires at least 3 rows: %s", qCString(data.fileName()));

  if (extraColumns.count() != 0 && extraColumns.count() != 3)
    qFatal("extra columns data requires 3 rows: %s", qCString(data.fileName()));

  QStringList names, types;
  int lineNumber = 0;
  int dataRows = 0;
  for (QString& line : lines) {
    lineNumber++;
    if (line.isEmpty() || line.startsWith("#")) continue;

    QStringList cols = line.trimmed().split(",");
    for (auto& col : cols) col.replace("&comma;", ",");

    if (names.count() == 0) {
      names = cols;
      _dataNames = names;
      if (extraColumns.count() > 0)
        for (QString str : extraColumns[0].split(",")) names.append(str);

      _dataNames = names;
    } else if (types.count() == 0) {
      types = cols;
      if (extraColumns.count() > 0)
        for (QString str : extraColumns[1].split(",")) types.append(str);

      if (types.count() != names.count())
        qFatal("type/names column count mismatch in %s", qCString(data.fileName()));

      _dataTypes = types;
    } else {
      if (extraColumns.count() > 0)
        for (QString str : extraColumns[2].split(",")) cols.append(str);

      if (cols.count() != types.count())
        qFatal("column count mismatch on line %d: %s", lineNumber, qCString(data.fileName()));

      QStringList row;

      for (int i = 0; i < types.count(); i++) {
        const QString& type = types[i];
        // const QString& name = names[i];

        QString value = cols[i];

        // replace variables
        for (int j = 0; j < names.count(); j++) {
          QString key = "$" + names[j];
          QString val = cols[j];
          value.replace(key, val);
        }

        // path type prepends the data set path
        if (type == "path") value = _dataRoot + value;

        row.append(value);
      }

      _dataRows.append(row);
      dataRows++;
    }
  }

  if (dataRows <= 0) qFatal("no test data was parsed in %s", qCString(data.fileName()));
}

void TestBase::loadDataSet(const QString& name, const QString &testName, const QStringList& extraColumns) {
  readDataSet(name, testName, extraColumns);

  for (int i = 0; i < _dataTypes.count(); i++) {
    const QString& type = _dataTypes[i];
    const char* name = qCString(_dataNames[i]);

    // printf("%d:%d %s:%s\n", lineNumber, i, qCString(type), qCString(name));

    if (type == "str")
      QTest::addColumn<QString>(name);
    else if (type == "path")
      QTest::addColumn<QString>(name);
    else if (type == "int")
      QTest::addColumn<int>(name);
    else if (type == "bool")
      QTest::addColumn<bool>(name);
    else if (type == "float")
      QTest::addColumn<float>(name);
    else if (type == "double")
      QTest::addColumn<double>(name);
    else
      qFatal("unsupported type %s in %s\n", qCString(type), qCString(_dataRoot));
  }

  for (QStringList cols : _dataRows) {
    // data tag is the first column
    QTestData& row = QTest::newRow(qCString(cols[0]));

    for (int i = 0; i < _dataTypes.count(); i++) {
      const QString& type = _dataTypes[i];
      // const QString& name = names[i];

      QString value = cols[i];

      // printf("%d:%d %s\n", lineNumber, i, qCString(value));

      if (type == "str")
        row << value;
      else if (type == "path")
        row << value;
      else if (type == "int")
        row << value.toInt();
      else if (type == "bool")
        row << (bool)value.toInt();
      else if (type == "float")
        row << value.toFloat();
      else if (type == "double")
        row << value.toDouble();
      else
        qFatal("unsupported type %s in %s\n", qCString(type), qCString(_dataRoot));
    }
  }
}

QString TestBase::testData(int row, const QString& col) {
  if (row >= _dataRows.count()) qFatal("row index of range");

  for (int i = 0; i < _dataNames.count(); i++)
    if (_dataNames[i] == col) return _dataRows[row][i];

  qFatal("invalid column name");
}
