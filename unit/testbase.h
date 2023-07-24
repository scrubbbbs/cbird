#pragma once

class TestBase : public QObject {
  Q_OBJECT

 protected:
  QString _dataRoot;
  QString _dataSetName;
  QStringList _dataNames;
  QStringList _dataTypes;
  QVector<QStringList> _dataRows;

  // was having issues with qPrintable so using this string pool
  static const char* qCString(const QString& str);

  void readDataSet(const QString& name, const QString& testName, const QStringList& extraColumns =QStringList());

  void loadDataSet(const QString& name, const QString& testName, const QStringList& extraColumns= QStringList());

  void loadDataSet(const QString& dataSetName, const QStringList& extraColumns = QStringList()) {
    loadDataSet(dataSetName, dataSetName, extraColumns);
  }

  QString testData(int row, const QString& col);
};
