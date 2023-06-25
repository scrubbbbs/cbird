#include "index.h"
#include "paramsdefs.h"

bool SearchParams::mediaSupported(const Media& needle) const {
  int type = 0;
  switch (needle.type()) {
    case Media::TypeImage: type = FlagImage; break;
    case Media::TypeVideo: type = FlagVideo; break;
    case Media::TypeAudio: type = FlagAudio; break;
  }
  return queryTypes & type;
}

bool SearchParams::mediaReady(const Media &needle) const {
  bool ok = false;
  switch (algo) {
  case AlgoCVFeatures:
    ok = needle.id() != 0 || needle.keyPointDescriptors().rows > 0;
    break;
  case AlgoDCTFeatures:
    ok = needle.id() != 0 || needle.keyPointHashes().size() > 0;
    break;
  case AlgoColor:
    ok = needle.id() != 0 || needle.colorDescriptor().numColors > 0;
    break;
  case AlgoVideo:
    ok = needle.id() != 0 ||
        (needle.type() == Media::TypeVideo && needle.videoIndex().hashes.size()>0) ||
        (needle.type() == Media::TypeImage && needle.dctHash() != 0);
    break;
  default:
    ok = needle.dctHash() != 0;
  }
  return ok;
}

SearchParams::SearchParams() {
  static const QVector<NamedValue> emptyValues;
  static const QVector<int> emptyRange;
  static const QVector<int> percent{1, 100};
  static const QVector<int> positive{0, INT_MAX};
  static const QVector<int> nonzero{1, INT_MAX};

  int counter = 0;

  {
    static const QVector<NamedValue> values{
        {AlgoDCT, "dct", "DCT image hash"},
        {AlgoDCTFeatures, "fdct", "DCT image hashes of features"},
        {AlgoCVFeatures, "orb", "ORB descriptors of features"},
        {AlgoColor, "color", "Color histogram"},
        {AlgoVideo, "video", "DCT image hashes of video frames"}};
    add({"alg", "Search algorithm", Value::Enum, counter++,
         SET_ENUM("alg", algo, values), GET(algo), GET_CONST(values), NO_RANGE});
  }

  {
    static const QVector<int> range{0, 65};
    add({"dht", "DCT hash distance threshold", Value::Int, counter++,
         SET_INT(dctThresh), GET(dctThresh), NO_NAMES, GET_CONST(range)});
  }

  {
    static const QVector<int> range{0, 100};
    add({"odt", "ORB descriptor distance threshold", Value::Int, counter++,
         SET_INT(cvThresh), GET(cvThresh), NO_NAMES, GET_CONST(range)});
  }

  add({"mt", "Maximum threshold to try, until minMatches are found", Value::Int, counter++,
       SET_INT(maxThresh), GET(maxThresh), NO_NAMES, GET_CONST(positive)});

  add({"mn", "Minimum matches per needle", Value::Int, counter++,
       SET_INT(minMatches), GET(minMatches), NO_NAMES, GET_CONST(nonzero)});

  add({"mm", "Maximum matches per needle", Value::Int, counter++,
       SET_INT(maxMatches), GET(maxMatches), NO_NAMES, GET_CONST(nonzero)});

  {
    static const QVector<NamedValue> bits{
        {MirrorNone, "none", "No flipping"},
        {MirrorHorizontal, "h", "Flip horizontally"},
        {MirrorVertical, "v", "Flip vertically"},
        {MirrorBoth, "b", "Flip horizontal and vertical"}};
    add({"refl", "Also search reflections of needle", Value::Flags, counter++,
         SET_FLAGS("refl", mirrorMask, bits), GET(mirrorMask), GET_CONST(bits), NO_RANGE});
  }

  {
    static const QVector<NamedValue> bits{
        {FlagImage, "i", "Image files"},
        {FlagVideo, "v", "Video files"},
        {FlagAudio, "a", "Audio files"}};
    add({"types", "Enabled needle media types", Value::Flags, counter++,
         SET_FLAGS("types", queryTypes, bits), GET(queryTypes), GET_CONST(bits), NO_RANGE});
  }

  add({"tm", "Enable template match result filter", Value::Bool, counter++,
       SET_BOOL(templateMatch), GET(templateMatch), NO_NAMES, NO_RANGE});

  add({"tnf", "Template match number of needle features", Value::Int, counter++,
       SET_INT(needleFeatures), GET(needleFeatures), NO_NAMES, GET_CONST(nonzero)});

  add({"thf", "Template match number of haystack features", Value::Int, counter++,
       SET_INT(haystackFeatures), GET(haystackFeatures), NO_NAMES, GET_CONST(nonzero)});

  add({"tdht", "Template matcher DCT hash threshold", Value::Int, counter++,
       SET_INT(tmThresh), GET(tmThresh), NO_NAMES, GET_CONST(positive)});

  add({"diag", "Enable diagnostic/verbose output", Value::Bool, counter++,
       SET_BOOL(verbose), GET(verbose), NO_NAMES, NO_RANGE});

  add({"neg", "Enable negative match result filter", Value::Bool, counter++,
       SET_BOOL(negativeMatch), GET(negativeMatch), NO_NAMES, NO_RANGE});

  add({"crop", "Enable de-letterbox/autocrop pre-filter", Value::Bool, counter++,
       SET_BOOL(autoCrop), GET(autoCrop), NO_NAMES, NO_RANGE});

  add({"vtrim", "Number of frames go ignore at start/end (video)", Value::Int, counter++,
       SET_INT(skipFrames), GET(skipFrames), NO_NAMES, GET_CONST(positive)});

  add({"vfm", "Minimum number of frames matched per video", Value::Int, counter++,
       SET_INT(minFramesMatched), GET(minFramesMatched), NO_NAMES, GET_CONST(positive)});

  add({"vfn", "Minimum percent of frames near each other", Value::Int, counter++,
       SET_INT(minFramesNear), GET(minFramesNear), NO_NAMES, GET_CONST(percent)});

  add({"fg", "Filter Groups: remove duplicate groups from result: {a,b}=={b,a}", Value::Bool, counter++,
       SET_BOOL(filterGroups), GET(filterGroups), NO_NAMES, NO_RANGE});

  add({"fp", "Filter Parent: remove items in the same directory as needle", Value::Bool, counter++,
       SET_BOOL(filterParent), GET(filterParent), NO_NAMES, NO_RANGE});

  add({"fs", "Filter Self: remove item that matched itself", Value::Bool, counter++,
       SET_BOOL(filterSelf), GET(filterSelf), NO_NAMES, NO_RANGE});

  add({"mg", "Merge n-connected groups:", Value::Int, counter++,
       SET_INT(mergeGroups), GET(mergeGroups), NO_NAMES, GET_CONST(positive)});

  add({"eg", "Expand groups to make pairs {a,b,c}=>{a,b}+{a,c}", Value::Bool, counter++,
       SET_BOOL(expandGroups), GET(expandGroups), NO_NAMES, NO_RANGE});

  // link algo change to also set the query media type,
  // "-p.alg video -p.types 3" == "-p.alg video"
  for (int i = 0; i < NumAlgos; ++i) {
    int types = FlagImage;
    if (i == AlgoVideo) types |= FlagVideo;
    link("alg", i, "types", types);
  }
}
