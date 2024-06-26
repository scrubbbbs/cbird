/* Media file container and utilities
   Copyright (C) 2021 scrubbbbs
   Contact: screubbbebs@gemeaile.com =~ s/e//g
   Project: https://github.com/scrubbbbs/cbird

   This file is part of cbird.

   cbird is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   cbird is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with cbird; if not, see
   <https://www.gnu.org/licenses/>.  */
#pragma once

// minimize includes since this is used everywhere
#include "cvutil.h" // ColorDescriptor

namespace cv {
class Mat;
class KeyPoint;
struct DMatch;
template<typename _Tp> class Rect_;
typedef Rect_<int> Rect;
}
#include "opencv2/core.hpp" // cv::Mat

class Media;
class VideoContext;

/// List of media, typically needle followed by matches
typedef QVector<Media> MediaGroup;
/// List of groups, typically search results
typedef QVector<MediaGroup> MediaGroupList;
typedef std::vector<cv::KeyPoint> KeyPointList;
typedef cv::Mat KeyPointDescriptors;
typedef std::vector<cv::Rect> KeyPointRectList;
typedef std::vector<uint64_t> KeyPointHashList;
typedef std::vector<cv::DMatch> MatchList;
typedef std::vector<uint64_t> VideoHashList;
typedef QHash<QString, QString> QStringHash;

typedef std::function<QVariant(const Media&)> PropertyFunc;

#define CVMAT_SIZE(x) (x.total() * x.elemSize())
#define VECTOR_SIZE(x) (x.capacity() * sizeof(decltype(x)::value_type))

/**
 * @class VideoIndex
 * @brief Container for index of a single video file
 *
 * Index is compressed by omitting nearby frames,
 * therefore there is also list of frame numbers;
 *
 * @note uint16_t limits video to < 2^16-1 frames indexed
 *
 * fixme: increase this by storing the offset from previous frame (maybe uint8)
 */
class VideoIndex {
 public:
  std::vector<uint16_t> frames;  // frame number
  VideoHashList hashes;          // dct hash

  size_t memSize() const { return sizeof(*this) + VECTOR_SIZE(frames) + VECTOR_SIZE(hashes); }
  bool isEmpty() const { return frames.size() == 0 || hashes.size() == 0; }
  void save(const QString& file) const;
  void load(const QString& file);
};

/**
 * Describes a matching interval
 * @note for video search, units are frames
 * @note if needle is frame grab, dstIn should be set to its frame number
 *
 * fixme: if frame rates differ, len should be in dst units
 */
class MatchRange {
 public:
  int srcIn, dstIn, len; // needle/other pos, match/our pos, match/our len

  MatchRange() {
    srcIn = dstIn = -1;
    len = 0;
  }

  MatchRange(int _srcIn, int _dstIn, int _len) {
    srcIn = _srcIn;
    dstIn = _dstIn;
    len = _len;
  }

  bool operator<(const MatchRange& r) const { return srcIn < r.srcIn; }
};

/**
 * Pass advanced options to loadImage()
 */
class ImageAllocator {
 public:
  virtual uchar* alloc(const QSize& size, QImage::Format fmt) = 0;
  virtual void free(void* ptr) = 0;
};

class ImageLoadOptions {
 public:
  bool fastJpegIdct = false;     // use JDCT_FAST in libjpeg, unused if scaling
  bool readScaled = false;       // scale image down in the decompress phase
  int minSize = 0, maxSize = 0;  // acceptable size range (best-effort, could be bigger)
  ImageAllocator* alloc  = nullptr; // custom image allocator
};

/**
 * A single unit of indexable content such as image, video or audio
 *
 * @details
 *
 * Where the media comes from affects which properties are set. It could
 * represent any type of media file (image, video, audio) as indicated by type()
 *
 * Typically media has a path() for the data source (not required). This could
 * be the local filesystem or a hyperlink
 *
 * The id() is a unique id used for the database, if it is 0 the Media
 * didn't come from a database query, and isValid() is false.
 *
 * The data can be from compressed data (setData()), raw pixel data
 * (setImage()), or loaded from the given path.
 *
 * Not all properties will always be present, it depends on where the Media was
 * constructed.
 *
 * If it was recently processed for indexing, then index properties like hashes,
 * keypoints, descriptors will be present.
 *
 * If it came from a search result (haystack), then only basic properties required
 * to perform the search will be present. In this case Index::findIndexData may supply
 * more information.
 *
 * If it came from a search query image/file (needle), then it might have some
 * index properties, but usually only the ones needed to make the query.
 */
class Media {
 public:
  /// media type
  enum {
    TypeImage = 1,
    TypeVideo = 2,
    TypeAudio = 3  // todo: implement indexer for audio files
  };

  /**
   * Flags are set on matching media, relative to the needle
   *
   * the more bits that are present generally mean higher quality
   * and help to classify matches besides the score() alone
   */
  enum {
    MatchExact = 1 << 0,             /// checksums match
    MatchBiggerDimensions = 1 << 1,  /// dimensionally bigger
    MatchBiggerFile = 1 << 2,        /// file size is bigger
    MatchLessCompressed = 1 << 3,    /// lower compression ratio
    MatchIsWeed = 1 << 4,            /// checksum matches known weed
  };

  /// QImage::text() keys, added by image loader
  static constexpr const char* ImgKey_FileSize = "fileSize";  // uncompressed size
  static constexpr const char* ImgKey_FileName = "name";      // original file name
  static constexpr const char* ImgKey_FileFormat = "format";  // jpg, gif etc
  static constexpr const char* ImgKey_FileWidth = "width";    // original/pre-scaled width
  static constexpr const char* ImgKey_FileHeight = "height";  // original/pre-scaled height

  // hooks to external things
  // fixme: gui functions don't belong here
  static void playSideBySide(const Media& left, float seekLeft, const Media& right,
                             float seekRight);
  static void openMedia(const Media& m, float seek = 0);
  static void revealMedia(const Media& m);

  static void print(const Media& media);
  static void printGroup(const MediaGroup& group);
  static void printGroupList(const MediaGroupList& list);
  static bool groupCompareByContents(const MediaGroup& s1, const MediaGroup& s2);
  static void mergeGroupList(MediaGroupList& list);
  static void expandGroupList(MediaGroupList& list);

  static void sortGroupList(MediaGroupList& list, const QStringList& properties);
  static void sortGroup(MediaGroup& group, const QStringList& properties);
  static void sortGroups(MediaGroupList& group, const QStringList& properties);


  static int indexInGroupByPath(MediaGroup& group, const QString& path);
  static QString greatestPathPrefix(const MediaGroup& group);
  static QString greatestPathPrefix(const MediaGroupList& list);

  static MediaGroupList splitGroup(const MediaGroup& group, int chunkSize = 1);
  static MediaGroupList groupBy(const MediaGroup& group, const QString& expr);

  /**
   * @return true if property can be obtained from readMetadata()
   *
   * @note there are certain things we'd like to have as properties
   * that are not stored in the database, like file size, file create/modify date etc.
   * This function indicates when such properties must be read from the system
   */
  static bool isExternalProperty(const QString& expr);

  /**
   * @return function that evaluates expr with Media argument
   *
   * @note useful for implementing generic sorting/grouping
   */
  static PropertyFunc propertyFunc(const QString& expr);

  /**
   * @param expr function[,args][#function[,args] ...]
   * @return function that takes one QVariant argument and returns QVariant
   * @note chain of functions is possible, separated by #
   */
  static std::function<QVariant(const QVariant&)> unaryFunc(const QString& expr);

  /// construct empty Media with image type
  Media() { setDefaults(); }

  /**
   * Construct from decompressed image
   * @param originalSize Size of the compressed data, if known
   *
   * fixme: inconsistency: unlike other constructors, this will compute
   * certain hashes from the image
   */
  Media(const QImage& qImg, int originalSize = 0);

  Media(const QString& path, int type = TypeImage, int width = -1, int height = -1);

  Media(const QString& path, int type, int width, int height, const QString& md5,
        const uint64_t dctHash);

//  Media(const QString& path, int type, int width, int height, const QString& md5,
//        const uint64_t dctHash, const ColorDescriptor& colorDescriptor,
//        const KeyPointList& keyPoints, const KeyPointDescriptors& descriptors);

  /**
   * Compare by score (for sorting matches)
   * fixme: maybe remove this, confusion with ==
   */
  bool operator<(const Media& other) const {
    // exact matches go to the front regardless of score
    return (_matchFlags && _matchFlags == MatchExact) || _score < other._score;
  }

  /// Compare by path (for <container>.exists())
  bool operator==(const Media& other) const { return path() == other.path(); }

  /**
   * @return unique id in the database
   * @note the id is 0 until inserted into or queried from a database
   */
  int id() const { return _id; }
  void setId(int id) { _id = id; }

  /// @return true if stored in a database (id >0)
  bool isValid() const { return _id != 0; }

  /**
   * @return wide classification such as audio, video, image
   * @return TypeXYZ enum
   * fixme: use typed enum
   */
  int type() const { return _type; }
  void setType(int type) { _type = type; }
  static QString typeString(int mediaType);
  static QStringList typeFlagsString(int typeFlags);

  /**
   * @return base64 checksum of file content, identifies exact duplicates
   * @note may exclude embedded metadata/tags (currently only JPEG)
   */
  const QString& md5() const { return _md5; }
  void setMd5(const QString& md5) { _md5 = md5; }

  /**
   * The resource path
   * @return URI or local file path
   * @note not required to be valid, if image() or data() provide resource
   * @note only local paths are loadable
   */
  const QString& path() const { return _path; }
  void setPath(const QString& path) { _path = path; }

  /// fast path components
  QString dirPath() const { return path().left(path().lastIndexOf("/")); } // fixme: use archivePath?
  QString name() const { return path().mid(path().lastIndexOf("/") + 1); } // fixme: use archivePath?
  QString suffix() const {
    QString s = name();
    int dot = s.lastIndexOf(".");
    return dot >= 0 ? s.mid(dot+1) : "";
  }
  QString completeBaseName() const {
    QString s = name();
    return s.mid(0, s.lastIndexOf("."));
  };  // w/o suffix

  /**
   * MIME content type, provided by the source
   * @note not provided by loader
   */
  const QString& contentType() const { return _contentType; }
  void setContentType(const QString& type) { _contentType = type; }

  /// @return 64-bit DCT-based image hash, based on pHash
  uint64_t dctHash() const { return _dctHash; }

  /// @return compressed/reduced color histogram
  const ColorDescriptor& colorDescriptor() const { return _colorDescriptor; }

  /**
   * Uncompressed image data
   * @note usually discarded ASAP to conserve memory, and data() is retained if
   *       anything (see: IndexParam::retainImage)
   * @note to discard use setImage(QImage())
   */
  const QImage& image() const { return _img; }
  void setImage(const QImage& img) { _img = img; }

  /**
   * Size of the compressed source data, if known
   * @note 0 until data() is set, or readMetadata() is called.
   * @note if setImage() was used, this won't be be known unless ImgKey_FileSize
   * is present
   */
  int64_t originalSize() const { return _origSize; }
  qint64 fileSize() const { return _origSize; }

  /**
   * File compression ratio
   * @note compares image() raw size to originalSize()
   */
  float compressionRatio() const { return _compressionRatio; }

  /**
   * Match score, >=0, lower is better
   * @note the needle/query has a score of -1 so it appears first when sorting
   */
  int score() const { return _score; }
  void setScore(int score) { _score = score; }

  /**
   * Position of media in original source document or data source
   * @note not stored in databases, supplied by clients
   * @fixme should be deprecated, use attributes() instead
   * @default -1
   */
  int position() const { return _position; }
  void setPosition(int pos) { _position = pos; }

  /// MatchXYZ flags, usually relative to the query image
  int matchFlags() const { return _matchFlags; }
  void setMatchFlags(int flags) { _matchFlags = flags; }

  int width() const { return _width; }
  void setWidth(int width) { _width = width; }

  int height() const { return _height; }
  void setHeight(int height) { _height = height; }

  int resolution() const { return _width * _height; }

  float aspectRatio() const { return float(_width) / _height; }

  bool isWeed() const { return _matchFlags & MatchIsWeed; }
  void setIsWeed(bool set = true) {
    if (set)
      _matchFlags |= MatchIsWeed;
    else
      _matchFlags &= ~MatchIsWeed;
  }

  /**
   * key/value store for clients
   * @note clients are free to set any values, untouched by
   * anything else
   * @note not to be used by database queries
   * @note gui/command line uses these:
   *       "filter" - the property key/value used for filtering
   *       "group"  - the property key/value used for group-by
   *       "sort"   - the property key used for sorting
   */
  const QStringHash& attributes() const { return _attrs; }
  void copyAttributes(const Media& other) { _attrs = other.attributes(); }
  void setAttribute(const char* key, const QString& value) { _attrs[key] = value; }
  void unsetAttribute(const char* key) { _attrs.remove(key); }

  /**
   * id sources can use for referencing external/non-indexed media
   * @deprecated should be using attributes() for this
   */
  Q_DECL_DEPRECATED const QString& uid() const { return _uid; }
  Q_DECL_DEPRECATED void setUid(const QString& uid) { _uid = uid; }

  // fixme: private interface or refactor
  void setColorDescriptor(const ColorDescriptor& desc) { _colorDescriptor = desc; }

  /// @deprecated use readMetadata
  Q_DECL_DEPRECATED void setCompressionRatio(float ratio) { _compressionRatio = ratio; }

  /**
   * region of interest from a matcher
   * usually a sub-rectangle (though it can
   * be any shape) where the match is applicable.
   * @note as set by template matcher, indicates the sub-rectangle matched from
   * the query image
   */
  const QVector<QPoint>& roi() const { return _roi; }
  void setRoi(const QVector<QPoint>& roi) { _roi = roi; }

  /**
   * matrix transform from query to this image
   * @note only set by template matcher, indicates the transform
   * (scale/rotate/translate) from the query image
   */
  const QTransform& transform() const { return _transform; }
  void setTransform(const QTransform& mat) { _transform = mat; }

  /**
   * range that matched between query and this
   * @note used to seek to the matching frames for display purposes
   */
  const MatchRange& matchRange() const { return _matchRange; }
  void setMatchRange(const MatchRange& range) { _matchRange = range; }

  /**
   * return a color that could be used to label a match in a gui
   * @note The color varies based on characteristics of the match (MatchXXX
   * enum) and perhaps the score
   */
  QColor matchColor() const;

  /**
   * save match in a csv for analysis or building test dataset
   * @param match media that matched this
   * @param matchIndex its index in the match set
   * @param numMatches total number of matches in the set
   */
  void recordMatch(const Media& match, int matchIndex, int numMatches) const;

  /**
   * compressed source data
   * @note can be retained instead of image() to save memory
   */
  const QByteArray& data() const { return _data; }
  void setData(const QByteArray& data) {
    _data = data;
    _origSize = data.size();
  }

  /**
   * approximation of current memory usage (self)
   * @note memory usage can be lowered substantially with setImage()/setData()
   */
  size_t memSize() const;

  /**
   * return device for reading which could be from disk or in-memory
   * @note the caller is responsible for freeing
   */
  QIODevice* ioDevice() const;

  /**
   * generate an icon
   * @see loadImage()
   */
  QIcon loadIcon(const QSize& size) const;

  /**
   * decompress image and optionally rescale
   * @param size If the width or height==0, constrain
   *        in the other dimension, preserving aspect ratio
   * @param future If set, future->isCancelled() will stop decompressing early
   * @param options Advanced loading options
   * @note will use image(), data() or read from disk as needed
   * @note calls loadImage() (static) as needed
   */
  QImage loadImage(const QSize& size = QSize(),
                   QFuture<void>* future = nullptr,
                   const ImageLoadOptions& options=ImageLoadOptions()) const;

  /**
   * return true if image can be reloaded from data() or path()
   * if true, then decompressed image can be released via setImage(QImage())
   * @return
   */
  bool isReloadable() const;

  /**
   * decompress image and optionally rescale
   * @param data Compressed image data
   * @param size Rescale to the non-zero dimension
   * @param name label for debug messages, e.g. the file name or url
   * @note  all image loaders eventually call this
   * @note  EXIF orientation flag will be used to transform the image
   */
  static QImage loadImage(const QByteArray& data, const QSize& size = QSize(),
                          const QString& name = QString(), const QFuture<void>* future = nullptr,
                          const ImageLoadOptions& options = ImageLoadOptions());

  /**
   * scale image using Qt's "smooth" filter
   * @param size Use the dimension > 0, keeping the aspect ratio
   */
  static QImage constrainedResize(const QImage& img, const QSize& size);

  /**
   * load properties from file system, image(), or data() if present
   * (prefer data())
   * @note will not decompress image
   * @note image() may be required to get everything
   */
  void readMetadata();

  /**
   * read metadata using exiv2
   * @param keys list of exiv2 tag names (prefix is optional)
   * @param type of metdata (exif, iptc, or xmp)
   * @return list of keys.length() with null or values found
   */
  QVariantList readEmbeddedMetadata(const QStringList& keys, const QString& type) const;

  /// @section index processing
  // fixme: should be moved into index subclasses, but some have multiple uses
  void makeKeyPoints(const cv::Mat& cvImg, int numKeyPoints, KeyPointList &outKeypoints) const;  // *FeaturesIndex, TemplateMatcher
  void makeKeyPointDescriptors(const cv::Mat& cvImg, KeyPointList &keyPoints,
                               KeyPointDescriptors& outDescriptors) const;  // CvFeaturesIndex
  void makeKeyPointHashes(const cv::Mat& cvImg, const KeyPointList& keyPoints,
                          KeyPointHashList& outHashes) const;              // DctFeaturesIndex
  void makeVideoIndex(
      VideoContext& video, int threshold, VideoIndex& outIndex,
      const std::function<void(int)>& progressCb) const;  // DctVideoIndex

  //  const KeyPointList& keyPoints() const;
  const KeyPointDescriptors& keyPointDescriptors() const { return _descriptors; }
//  const KeyPointRectList& keyPointRects() const;
  const KeyPointHashList& keyPointHashes() const { return _kpHashes; }
  const VideoIndex& videoIndex() const { return _videoIndex; }

  /**
   * compose a path to a resource that is indirect (e.g. inside an
   * archive)
   * @param parent path of parent, cannot be virtual
   * @param child path of child
   * @return
   */
  static QString virtualPath(const QString& parent, const QString& child) {
    return parent + ":" + child;
  }

  /// test if path is a zip
  static bool isArchive(const QString& path) { return path.endsWith(".zip"); }

  /// test if path is a zip file member
  static bool isArchived(const QString& path) { return path.contains(".zip:"); }
  bool isArchived() const { return isArchived(_path); }

  /// decompose a virtual path, assuming it was for a zip file
  QT_DEPRECATED static void archivePaths(const QString& path, QString& parent, QString& child) {
    auto parts = path.split(".zip:");
    parent = parts[0] + ".zip";
    child = parts.count() > 1 ? parts[1] : "";
  }

  QT_DEPRECATED void archivePaths(QString& parent, QString& child) const {
    archivePaths(_path, &parent, &child);
  }

  static void archivePaths(const QString& path, QString* parent, QString* child = nullptr) {
    auto parts = path.split(".zip:");
    if (parent) *parent = parts.at(0) + ".zip";
    if (child) *child = parts.count() > 1 ? parts.at(1) : "";
  }

  void archivePaths(QString* parent, QString* child = nullptr) const {
    archivePaths(_path, parent, child);
  }

  /**
   * count all things in the archive containing this
   * @note doesn't touch any database, only the archive itself
   */
  int archiveCount() const;

  /// return virtualPath() list of contents
  static QStringList listArchive(const QString& path);

  /// get the runtime/compiled version of exif library
  static QStringList exifVersion();

  /// get list of property names and attributes
  static QList<QPair<const char*, const char*> > propertyList();

  void setKeyPointDescriptors(const KeyPointDescriptors& desc) { _descriptors = desc; }
  void setKeyPointHashes(const KeyPointHashList& hashes) { _kpHashes = hashes; }
  void setVideoIndex(const VideoIndex& index) { _videoIndex = index; }

 private:
  void setDefaults();
  void imageHash();
//  void setKeyPoints(const KeyPointList& keyPoints);
//  void setKeyPointRects(const KeyPointRectList& rects);

  int _id;
  int _type;
  QString _path;
  QString _contentType;
  QString _md5;
  uint64_t _dctHash;
  ColorDescriptor _colorDescriptor;
  QImage _img;
  int64_t _origSize;
  float _compressionRatio;

  int _score;
  int _position;
  int _matchFlags;
  int _width, _height;

  MatchRange _matchRange;

  QByteArray _data;

  QVector<QPoint> _roi;
  QTransform _transform;

  QString _uid;
  QStringHash _attrs;

  KeyPointHashList _kpHashes;
  KeyPointDescriptors _descriptors;
  VideoIndex _videoIndex;
};
