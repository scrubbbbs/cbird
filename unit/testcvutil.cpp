#include <QtTest/QtTest>

#include "cvutil.h"
#include "hamm.h"

#include "opencv2/highgui/highgui.hpp"  // imread

#if ENABLE_DEPRECATED
#include "pHash.h"
#endif

class TestCvUtil : public QObject {
  Q_OBJECT

  void commonPhashData();

 private Q_SLOTS:
  void initTestCase();

  void testAutocrop_data() {
    loadDataSet("autocrop", QStringList() << "result"
                                          << "path"
                                          << "result/$file");
  }
  void testAutocrop();

  void testQImageToCvImage_data() { loadDataSet("imgformats"); }
  void testQImageToCvImage();

  void testCvImageToQImage_data() { loadDataSet("imgformats"); }
  void testCvImageToQImage();

  void testGrayscale_data() {
    loadDataSet("imgformats",
                QStringList() << "result,fail"
                              << "path,path"
                              << "result/grayscale/$file,fail/grayscale/$file");
  }
  void testGrayscale();
#if DEADCODE
  void testLoadSaveMatrix();
#endif
  void testDctHashCv_data();
  void testDctHashCv();

#ifdef ENABLE_DEPRECATED
  void testPhash_data();
  void testPhash();
#endif

  void testDctHashCvSimilarity_data();
  void testDctHashCvSimilarity_init();
  void testDctHashCvSimilarity_cleanup();
  void testDctHashCvSimilarity();

  void testDctHashCvDissimilarity_data();
  void testDctHashCvDissimilarity_init();
  void testDctHashCvDissimilarity();

  void testColorDescriptor_data() { loadDataSet("colormatch"); }
  void testColorDescriptor();
 private:

  void loadDataSet(const QString& dataSetName,
                   const QStringList& extraColumns = QStringList());
  void readDataSet(const QString& dataSetName,
                   const QStringList& extraColumns = QStringList());
  QString testData(int row, const QString& col);

  QString _dataRoot;
  QString _dataSetName;
  QStringList _dataNames;
  QStringList _dataTypes;
  QVector<QStringList> _dataRows;

  QFile _logFile;
  QVector<uint64_t> _hashes;
};

/*
#define QFETCH_P(type, name)\
    type name = *static_cast<type *>(QTest::qData(qCString(#name),
::qMetaTypeId<type >()))
*/

// was having issues with qPrintable so using this string pool
static const char* qCString(const QString& str) {
  static QMap<QString, const char*> map;

  if (map.contains(str))
    return map[str];
  else {
    const char* cstr = strdup(qPrintable(str));
    map[str] = cstr;
    return cstr;
  }
}

static void viewImages(const char* label, const cv::Mat& img1,
                       const cv::Mat& img2) {
  cv::namedWindow(label, CV_WINDOW_AUTOSIZE);
  cv::moveWindow(label, 100, 100);
  cv::imshow(label, img1);
  cv::waitKey();
  cv::imshow(label, img2);
  cv::waitKey();
  cv::destroyWindow(label);
}

static void viewImages(const char* label, const QImage& img1,
                       const QImage& img2) {
  (void)label;
  (void)img1;
  (void)img2;
  QFAIL("not implemented");
}

void TestCvUtil::initTestCase() {
  QString root = getenv("TEST_DATA_DIR");
  if (root.isEmpty()) qFatal("TEST_DATA_DIR environment is not set");
}

void TestCvUtil::loadDataSet(const QString& dataSetName,
                             const QStringList& extraColumns) {
  readDataSet(dataSetName, extraColumns);

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
      qFatal("unsupported type %s in %s\n", qCString(type),
             qCString(_dataRoot));
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
        qFatal("unsupported type %s in %s\n", qCString(type),
               qCString(_dataRoot));
    }
  }
}

void TestCvUtil::readDataSet(const QString& dataSetName,
                             const QStringList& extraColumns) {
  _dataRoot = getenv("TEST_DATA_DIR");
  _dataRoot += "/" + dataSetName + "/";

  _dataNames.clear();
  _dataTypes.clear();
  _dataRows.clear();

  // first row, data field names
  // second row, data field types
  // the rest, test data

  QFile data(_dataRoot + dataSetName + ".csv");
  if (!data.open(QFile::ReadOnly))
    qFatal("failure to open %s", qCString(data.fileName()));

  QStringList lines = QString(data.readAll()).split("\n");
  if (lines.count() < 3)
    qFatal("data set CSV requires at least 3 rows: %s",
           qCString(data.fileName()));

  if (extraColumns.count() != 0 && extraColumns.count() != 3)
    qFatal("extra columns data requires 3 rows: %s", qCString(data.fileName()));

  QStringList names, types;
  int lineNumber = 0;
  int dataRows = 0;
  for (const QString& line : lines) {
    lineNumber++;
    if (line.isEmpty() || line.startsWith("#")) continue;

    QStringList cols = line.trimmed().split(",");
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
        qFatal("type/names column count mismatch in %s",
               qCString(data.fileName()));

      _dataTypes = types;
    } else {
      if (extraColumns.count() > 0)
        for (QString str : extraColumns[2].split(",")) cols.append(str);

      if (cols.count() != types.count())
        qFatal("column count mismatch on line %d: %s", lineNumber,
               qCString(data.fileName()));

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

  if (dataRows <= 0)
    qFatal("no test data was parsed in %s", qCString(data.fileName()));
}

QString TestCvUtil::testData(int row, const QString& col) {
  if (row >= _dataRows.count()) qFatal("row index of range");

  for (int i = 0; i < _dataNames.count(); i++)
    if (_dataNames[i] == col) return _dataRows[row][i];

  qFatal("invalid column name");
}

void TestCvUtil::testAutocrop() {
  QFETCH(QString, file);
  QFETCH(int, range);
  QFETCH(QString, result);

  QVERIFY(QFileInfo(file).exists());

  cv::Mat cropped = cv::imread(qCString(file));
  cv::Mat expected = cv::imread(qCString(result));

  QVERIFY(cropped.rows > 0 && cropped.cols > 0);

  autocrop(cropped, range);

  if (expected.rows == 0 || expected.cols == 0 ||
      hamm64(dctHash64(cropped), dctHash64(expected)) != 0) {
    // viewImages(qCString(file), cropped, expected);
    QString failImg =
        QString("%1/fail/%2").arg(_dataRoot).arg(QFileInfo(file).fileName());

    qWarning("write fail image: %s\n", qPrintable(failImg));
    cv::imwrite(qCString(failImg), cropped);
    QFAIL(qCString(failImg));
  }
}

void TestCvUtil::testQImageToCvImage() {
  QFETCH(QString, file);
  QFETCH(bool, hasAlpha);
  QFETCH(bool, isGray);
  QFETCH(bool, isIndexed);

  // if (file.endsWith("gif"))
  //    QSKIP("opencv can't load gifs");

  QImage qImg(file);
  QVERIFY(!qImg.isNull());

  if (hasAlpha) QVERIFY(qImg.hasAlphaChannel());

  // printf("depth=%d bits=%d format=%d\n", qImg.depth(), qImg.bitPlaneCount(),
  // qImg.format());

  cv::Mat converted;
  qImageToCvImg(qImg, converted);

  // all results are 8 bits per channel
  QVERIFY(converted.depth() == CV_8U);

  if (isGray && isIndexed) {
    // indexed images get converted to rgb; if they are grayscale,
    // cv loader has logic to detect that and load 1-channel gray;
    // qImageToCvImg does not have that logic so force it to grayscale
    grayscale(converted, converted);
  }

  // compare results against opencv imread()
  //
  // without this flag, opencv will load everything as 3-channel color,
  // when this is added we get something close to how QImage loader behaves
  int flag = CV_LOAD_IMAGE_UNCHANGED;

  // opencv will load a 1-bit image as 3-channel otherwise
  if (qImg.depth() == 1) flag = CV_LOAD_IMAGE_GRAYSCALE;

  cv::Mat loaded = cv::imread(qCString(file), flag);
  if (loaded.rows <= 0) QSKIP("opencv can't load this file");

  QCOMPARE(converted.rows, loaded.rows);
  QCOMPARE(converted.cols, loaded.cols);

  if (loaded.depth() == CV_16U) {
    // qt doesn't seem support 16-bit gray pngs or 16-bit tiffs, they
    // are downsampled to 8-bit rgb so the depth will say 8-bit, if we
    // convert the image to 3-channel 8-bit they seem to be equivalent
    loaded = cv::imread(qCString(file), CV_LOAD_IMAGE_COLOR);
  }

  QCOMPARE(converted.depth(), loaded.depth());
  QCOMPARE(converted.channels(), loaded.channels());

#if 1
  QVERIFY(compare(converted, loaded));
#else
  if (!compare(converted, loaded))
    viewImages(qCString(file), converted, loaded);
#endif
}

void TestCvUtil::testCvImageToQImage() {
  QFETCH(QString, file);
  QFETCH(bool, isIndexed);
  QFETCH(bool, isGray);
  QFETCH(int, bpp);

  int flag = CV_LOAD_IMAGE_UNCHANGED;

  // opencv will load a 1-bit image as 3-channel otherwise
  if (bpp == 1) flag = CV_LOAD_IMAGE_GRAYSCALE;

  cv::Mat cvImg = cv::imread(qCString(file), flag);

  if (cvImg.cols <= 0) QSKIP("opencv can't load this file");

  // if we know it's gray make sure it loaded correctly
  if (isGray && cvImg.type() != CV_8UC(1))
    QSKIP("opencv didn't load the image correctly");

  QImage converted;
  cvImgToQImage(cvImg, converted);

  QImage loaded(file);

  // opencv doesn't support indexed color, everything goes to rgb or
  // it may detect if its grayscale
  if (isIndexed) {
    // these checks only apply to 8-bit images
    QCOMPARE(bpp, 8);

    // qt supports indexed color
    QVERIFY(loaded.format() == QImage::Format_Indexed8);

    // opencv seems to make a gray image if it determines the color table
    // is the gray table, otherwise its converted to rgb
    if (isGray)
      QCOMPARE(converted.format(), QImage::Format_Grayscale8);
    else
      QCOMPARE(converted.format(), QImage::Format_RGB32);
  } else if (bpp == 1) {
    // cv loads 1-bit images as grayscale (provided the correct flags supplied)
    QVERIFY(loaded.format() == QImage::Format_Mono);
    QCOMPARE(converted.format(), QImage::Format_Grayscale8);
  } else {
    QCOMPARE(converted, loaded);
  }
}

void TestCvUtil::testGrayscale() {
  QFETCH(QString, file);
  QFETCH(QString, result);
  QFETCH(QString, fail);

  std::string path = qCString(file);
  cv::Mat color, gray;

  color = cv::imread(path, CV_LOAD_IMAGE_UNCHANGED);
  if (color.rows <= 0) QSKIP("opencv failed to load image");

  grayscale(color, gray);

  // jpeg images will fail to verify since they're lossy?
  // use a png for verification
  if (file.endsWith(".jpg")) {
    result += ".png";
    fail += ".png";
  }

  cv::Mat cmp = cv::imread(qCString(result), CV_LOAD_IMAGE_UNCHANGED);

  if (!compare(gray, cmp)) {
    qWarning("write fail image: %s\n", qPrintable(fail));

    cv::imwrite(qCString(fail), gray);

    if (cmp.rows <= 0)
      QFAIL("no verified image");
    else
      QFAIL("grayscale differs from verified image");
  }
}

#if DEADCODE
void TestCvUtil::testLoadSaveMatrix() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  std::vector<uint32_t> mediaIds;
  std::vector<cv::Mat> array;

  readDataSet("imgformats");

  for (int i = 0; i < _dataRows.count(); i++) {
    QString file = testData(i, "file");

    mediaIds.push_back(i);

    array.push_back(cv::imread(qCString(file), CV_LOAD_IMAGE_UNCHANGED));
  }

  QString file = dir.path() + "/test.mat";

  saveMatrixArray(mediaIds, array, file);

  std::vector<uint32_t> mediaIds_after;
  std::vector<cv::Mat> array_after;

  loadMatrixArray(file, mediaIds_after, array_after);

  QCOMPARE(mediaIds, mediaIds_after);

  QCOMPARE(array.size(), array_after.size());
  QCOMPARE((int)array.size(), _dataRows.count());

  for (int i = 0; i < _dataRows.count(); i++)
    QVERIFY(compare(array[i], array[i]));

  // void loadMatrixArray(const QString& path,
  //    std::vector<uint32_t>& mediaIds, std::vector<cv::Mat>& array);
}
#endif

void TestCvUtil::commonPhashData() {
  QString root = getenv("TEST_DATA_DIR");
  root += "/phash/";

  QTest::addColumn<QString>("file");
  QTest::addColumn<uint64_t>("hash");

  QFile data(root + "phash.csv");

  QVERIFY(data.open(QFile::ReadOnly));

  QStringList lines = QString(data.readAll()).split("\n");
  for (const QString& line : lines) {
    if (line.isEmpty() || line.startsWith("#")) continue;

    QStringList args = line.trimmed().split(",");
    Q_ASSERT(args.count() == 2);

    QString file = args[0];
    uint64_t hash = args[1].toULongLong();

    QTest::newRow(qCString(file)) << (root + file) << hash;
  }
}

void TestCvUtil::testDctHashCv_data() { commonPhashData(); }

void TestCvUtil::testDctHashCv() {
  QFETCH(QString, file);
  // QFETCH(uint64_t, hash);

  cv::Mat img = cv::imread(qCString(file), cv::IMREAD_GRAYSCALE);

  uint64_t result;

  QBENCHMARK { result = dctHash64(img); }

  Q_UNUSED(result);
}

#if ENABLE_DEPRECATED

void TestCvUtil::testPhash_data() {
  commonPhashData();
  ph_initialize();
}

void TestCvUtil::testPhash() {
  QFETCH(QString, file);
  // QFETCH(uint64_t, hash);

  // cv::Mat img = cv::imread(qCString(file));
  CImg<uint8_t> img;
  img.load(qCString(file));
  img = img.RGBtoYCbCr().channel(0);

  uint64_t result;

  QBENCHMARK {
    // result = dctHash64(img);
    ph_dct_imagehash_cimg(img, result);
  }

  Q_UNUSED(result);
}
#endif

void TestCvUtil::testDctHashCvSimilarity_data() {
  QString root = getenv("TEST_DATA_DIR");
  root += "/100x/";

  QTest::addColumn<QString>("root");
  QTest::addColumn<int>("index");

  for (int i = 0; i < 100; i++) {
    QTest::newRow(qCString(QString::number(i + 1))) << root << i + 1;
  }
}

void TestCvUtil::testDctHashCvSimilarity_init() {
  // NOTE: init/cleanup are not called when exec individual test case
  _logFile.setFileName(QString("%1.csv").arg(QTest::currentTestFunction()));
  Q_ASSERT(_logFile.open(QFile::ReadWrite));
}

void TestCvUtil::testDctHashCvSimilarity_cleanup() {
  // printf("cleanup\n");
  //_logFile.close();
}

void TestCvUtil::testDctHashCvSimilarity() {
  QFETCH(QString, root);
  QFETCH(int, index);

  QStringList mods;
  mods << "original"
       << "scale256"
       << "scale224"
       << "scale192"
       << "scale160"
       << "scale128"
       << "scale96"
       << "scale64"
       << "scale32";

  QString original = QString("%1/original/%2.jpg").arg(root).arg(index);

  cv::Mat img = cv::imread(qCString(original), cv::IMREAD_GRAYSCALE);
  QVERIFY(!img.empty());

  uint64_t origHash = dctHash64(img);

  QStringList dist;
  dist.append(QString::number(index));

  for (const QString& mod : mods) {
    QString modFile = QString("%1/%2/%3.jpg").arg(root).arg(mod).arg(index);

    img = cv::imread(qCString(modFile), cv::IMREAD_GRAYSCALE);
    //		printf("%s\n", qCString(modFile));
    QVERIFY(!img.empty());
    uint64_t modHash = dctHash64(img);

    int hamm = hamm64(origHash, modHash);

    dist.append(QString::number(hamm));
  }

  Q_ASSERT(_logFile.isOpen());
  _logFile.write(qCString(dist.join(",") + "\n"));
}

void TestCvUtil::testDctHashCvDissimilarity_data() {
  QTest::addColumn<int>("index");

  for (int i = 0; i < 100; i++)
    QTest::newRow(qCString(QString::number(i + 1))) << i;
}

void TestCvUtil::testDctHashCvDissimilarity_init() {
  _logFile.close();
  _logFile.setFileName(QString("%1.csv").arg(QTest::currentTestFunction()));
  _logFile.open(QFile::ReadWrite);

  // ph_initialize();

  QString root = getenv("TEST_DATA_DIR");
  root += "/100x/";

  _hashes.clear();

  for (int i = 0; i < 100; i++) {
    QString original = QString("%1/dissimilar/%2.jpg").arg(root).arg(i + 1);
    // printf("%s\n", qCString(original));

    cv::Mat img = cv::imread(qCString(original), cv::IMREAD_GRAYSCALE);

    uint64_t origHash = dctHash64(img);
/*
    for (int i = 0; i < 64; i++)
      if (origHash & (1 << i))
        printf("1");
      else
        printf("0");
    printf("\n");
*/
    _hashes.append(origHash);
  }
}

void TestCvUtil::testDctHashCvDissimilarity() {
  QFETCH(int, index);

  uint64_t hash = _hashes[index];

  for (int i = 0; i < 100; i++) {
    if (index > i) {
      int dist = hamm64(hash, _hashes[i]);

      QStringList cols;
      cols.append(QString::number(index));
      cols.append(QString::number(i));
      cols.append(QString::number(dist));
      _logFile.write(qCString(cols.join(",") + "\n"));
    }
  }
}

void TestCvUtil::testColorDescriptor() {
  QFETCH(QString, file1);
  QFETCH(QString, file2);
  QFETCH(int, distance);

  QVERIFY(QFileInfo(file1).exists());
  QVERIFY(QFileInfo(file2).exists());

  cv::Mat img1 = cv::imread(qCString(file1));
  cv::Mat img2 = cv::imread(qCString(file2));

  ColorDescriptor desc1, desc2;
  ColorDescriptor::create(img1, desc1);
  ColorDescriptor::create(img2, desc2);

  float dist = ColorDescriptor::distance(desc1, desc2);
  QCOMPARE(int(dist), distance);
}

QTEST_MAIN(TestCvUtil)
#include "testcvutil.moc"
