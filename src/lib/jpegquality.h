
//
// Estimate JPEG compression quality setting by comparing quantization tables.
// Inaccurate for quality below 50%
// http://fotoforensics.com/tutorial-estq.php
//
class JpegQuality {
 public:
  bool ok;          // false==not a jpeg, invalid file path, corrupt file etc
  bool isReliable;  // quality >= 50%
  int quality;      // estimated compression quality setting (0-100) (< 0 == invalid)

  // quantization tables as 64 values, row-major order
  // only the first 3 tables are read
  QHash<int, QVector<int>> table;

  JpegQuality() : ok(false), isReliable(false), quality(-1) {}
};

JpegQuality EstimateJpegQuality(QIODevice *io);
