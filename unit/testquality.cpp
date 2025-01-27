
#include <QtTest/QtTest>

#include "media.h"
#include "cimgops.h"
#include "testbase.h"
#include "scanner.h"
#include "gui/mediagrouplistwidget.h"
#include "gui/theme.h"

class TestQuality : public TestBase {
  Q_OBJECT

 private Q_SLOTS:
  void initTestCase();
  // void cleanupTestCase() {}

  void testQuality_init();
  void testQuality();

 private:
  MediaGroup readDir(const QString& dir);
  Scanner _scanner;
};

void TestQuality::initTestCase() {
  QString dataDir = getenv("TEST_DATA_DIR");
  QVERIFY(QDir(dataDir).exists());
}

void TestQuality::testQuality_init() {
  Theme::setup();
}

static int horizontal_filter(const uint8_t *s) {
  return (s[1] - s[-2]) * 2 + (s[-1] - s[0]) * 6;
}

static int vertical_filter(const uint8_t *s, int p) {
  return (s[p] - s[-2 * p]) * 2 + (s[-p] - s[0]) * 6;
}

static int variance(int sum, int sum_squared, int size) {
  return sum_squared / size - (sum / size) * (sum / size);
}
// Calculate a blockiness level for a vertical block edge.
// This function returns a new blockiness metric that's defined as

//              p0 p1 p2 p3
//              q0 q1 q2 q3
// block edge ->
//              r0 r1 r2 r3
//              s0 s1 s2 s3

// blockiness =  p0*-2+q0*6+r0*-6+s0*2 +
//               p1*-2+q1*6+r1*-6+s1*2 +
//               p2*-2+q2*6+r2*-6+s2*2 +
//               p3*-2+q3*6+r3*-6+s3*2 ;

// reconstructed_blockiness = abs(blockiness from reconstructed buffer -
//                                blockiness from source buffer,0)
//
// I make the assumption that flat blocks are much more visible than high
// contrast blocks. As such, I scale the result of the blockiness calc
// by dividing the blockiness by the variance of the pixels on either side
// of the edge as follows:
// var_0 = (q0^2+q1^2+q2^2+q3^2) - ((q0 + q1 + q2 + q3) / 4 )^2
// var_1 = (r0^2+r1^2+r2^2+r3^2) - ((r0 + r1 + r2 + r3) / 4 )^2
// The returned blockiness is the scaled value
// Reconstructed blockiness / ( 1 + var_0 + var_1 ) ;
static int blockiness_vertical(const uint8_t* s, int sp, int size) {
  int s_blockiness = 0;
  int sum_0 = 0;
  int sum_sq_0 = 0;
  int i;
  int var_0;
  for (i = 0; i < size; ++i, s += sp) {
    s_blockiness += horizontal_filter(s);
    sum_0 += s[0];
    sum_sq_0 += s[0] * s[0];
  }
  var_0 = variance(sum_0, sum_sq_0, size);

  return s_blockiness / (1 + var_0);
}

// Calculate a blockiness level for a horizontal block edge
// same as above.
static int blockiness_horizontal(const uint8_t* s, int sp, int size) {
  int s_blockiness = 0;
  int sum_0 = 0;
  int sum_sq_0 = 0;
  int i;
  int var_0;
  for (i = 0; i < size; ++i, ++s) {
    s_blockiness += vertical_filter(s, sp);
    sum_0 += s[0];
    sum_sq_0 += s[0] * s[0];
  }
  var_0 = variance(sum_0, sum_sq_0, size);
  s_blockiness = abs(s_blockiness);

  return s_blockiness / (1 + var_0);
}

// This function returns the blockiness for the entire frame currently by
// looking at all borders in steps of 4.
double av1_get_blockiness(const unsigned char* img1, int img1_pitch, int width, int height) {
  double blockiness = 0;
  int i, j;
  for (i = 0; i < height; i += 8, img1 += img1_pitch * 8) {
    for (j = 0; j < width; j += 8) {
      if (i > 0 && i < height && j > 0 && j < width) {
        blockiness += blockiness_vertical(img1 + j, img1_pitch, 8);
        blockiness += blockiness_horizontal(img1 + j, img1_pitch, 8);
      }
    }
  }
  blockiness /= width * height / 64;
  return blockiness;
}

void TestQuality::testQuality() {
  QVERIFY(true);
  return;  // disable the test for now

  MediaGroup files = readDir("100x/original");
  files += readDir("100x/dissimilar");

  for (int jpeg_quality=10; jpeg_quality<100; ++jpeg_quality) {

  int numTested=0, numFailed=0; double qDiff=0;
  for (auto& m1 : files) {

    QImage img1 = m1.loadImage();
    QVERIFY2(!img1.isNull(), qUtf8Printable(m1.path()));
    QImage img2=img1;

    auto compressionScore=[](QIODevice* file) {
      int sizeBefore = file->size();
      QImageReader reader(file);
      QImage img;
      reader.read(&img);

      QBuffer buffer;
      buffer.open(QIODevice::ReadWrite);
      img.save(&buffer, "jpg", 50);
      int sizeAfter = buffer.size();
      double score = abs(sizeBefore-sizeAfter)/(double)sizeBefore;
      qDebug() <<  sizeBefore << sizeAfter << img.width() << img.height() << score;
      return score;
    };

    //              p0 p1 p2 p3
    //              q0 q1 q2 q3
    // block edge ->
    //              r0 r1 r2 r3
    //              s0 s1 s2 s3

    // blockiness =  p0*-2+q0*6+r0*-6+s0*2 +
    //               p1*-2+q1*6+r1*-6+s1*2 +
    //               p2*-2+q2*6+r2*-6+s2*2 +
    //               p3*-2+q3*6+r3*-6+s3*2 ;

    //
    // 0,1,2,3 | 4,5,6,7 |
    //

    auto blockingScore=[](const QImage& img) {
      cv::Mat cvImg;
      qImageToCvImg(img, cvImg);
      grayscale(cvImg,cvImg);
      int stride = cvImg.step[0];
      return av1_get_blockiness( cvImg.ptr(0), stride, cvImg.cols, cvImg.rows);
    };

    {
      //img2 = img2.scaledToWidth(img1.width()*0.75, Qt::SmoothTransformation);
      QBuffer buffer;
      buffer.open(QIODevice::ReadWrite);
      img2.save(&buffer, "jpg", jpeg_quality);
      buffer.seek(0);
      QImageReader ir(&buffer);
      ir.read(&img2);
    }

    double score1 = blockingScore(img1);
    double score2 = blockingScore(img2);

    qDebug() << score1 << score2;
#if  COMPRESSION
    double score1 = compressionScore(m1.ioDevice());


    Media m2=m1;
    double score2;
    {
      img1 = m2.loadImage();
      img1 = img1.scaledToWidth(img1.width()*0.75, Qt::SmoothTransformation);
      QBuffer buffer;
      buffer.open(QIODevice::ReadWrite);
      img1.save(&buffer, "jpg", 90);
      buffer.seek(0);
      score2 = compressionScore(&buffer);
    }
#endif

#if  0
    QImage img2 = Media::loadImage(buffer.readAll());
    QVERIFY(!img2.isNull());

    QVERIFY(img1.width() == img2.width());

    m1.setImage(img1);
    m1.setPath("m1");
    QVector<QImage> visuals1;
    int score1 = qualityScore(m1, &visuals1);

    m2.setImage(img2);
    m2.setPath("m2");
    QVector<QImage> visuals2;
    int score2 = qualityScore(m2, &visuals2);
#endif
    // QEXPECT_FAIL("", "", Continue);
    // QCOMPARE_GE(score1,score2);
    if (score1 >= score2) {
      numFailed++;
      if (0) {
        MediaWidgetOptions opt;
        MediaGroupListWidget widget({{Media(img1), Media(img2)}}, opt);
        widget.show();
        do {
          qApp->processEvents();
        } while (!widget.isHidden());
      }
    }

    numTested++;
    qDiff += abs(score1-score2);
    m1.setImage(QImage());
    }
    QVERIFY(numTested > 0);
    //qDebug() << "failed=" << numFailed*100 / numTested << "%" << "qDiff" << qDiff/numTested;
    qDebug() << "plot@" << jpeg_quality << "," << numFailed*100 / numTested << "," << qDiff/numTested;
  }
}

MediaGroup TestQuality::readDir(const QString& dir) {
  QString dataDir = qEnvironmentVariable("TEST_DATA_DIR") + "/" + dir;

  MediaGroup g;
  QDirIterator it(dataDir, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    QFileInfo info = it.nextFileInfo();
    if (!info.isFile() || !_scanner.imageTypes().contains(info.suffix()))
      continue;

    qDebug() << info.filePath();

    Media m(info.filePath());
    m.readMetadata();
    g += m;
  }
  return g;
}

QTEST_MAIN(TestQuality)
#include "testquality.moc"
