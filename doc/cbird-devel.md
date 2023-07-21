Release Blockers (v0.7)
=========================


Wish List
=========================

### Command Line
- "-use" special notation to find root index in the parent (tree)
- symbolic names for all enumerations (e.g. "v" == video)
- disable/reduce logging (-verbose, -quiet etc)
- typecheck param values
- presets for multiple parameters
- move/link files rather than delete/rename
- prune groups, keeping/ignoring needle
- rename folders based on filenames
- copy or sync exif data with matching images
- nuke-dups-in: show how many would not be deleted
- group-by breaks the sort/sort doesn't work on group-by?
- -with type <x>, recognize enumerations as in params
- sort result groups too
- option to pre-compute no-reference quality score for each image,
  might be reasonable for automatic deletions

### Indexing
- file/directory name filters for inclusion/exclusion
- store date-modified,size for better updating
- also store container md5 (.zip) for faster updating/verifying
- \>64k frames per video
- capture more common errors in indexer
  - unsupported ICC profile
  - colordescriptor on grayscale image
- console progress bar
- error-log to file
- store symlinks to prevent broken links later on
- ~~use idct scaling to speed up jpeg decompress (wip)~~
- ~~hard links handling~~ added in v0.6
  - ~~isJunction() exclusion from index~~
  - ~~exclusion from index (map inodes during scan)~~
- ~~see if `skip_loop_filter` for h264 decoding is a good idea: about 20% faster decoding, unknown affect on hash quality~~ enabled in v0.6
- ~~same with `SWS_AREA` rescaler instead of `SWS_BICUBIC`~~ enabled in v0.6
- index videos with partition and merge approach to overcome ffmpeg thread limitations

### Search
- search tree for histograms
- csv/json output for other tools, gui wrappers
- sort groups with closest matches first
- fast block-averaging template match with threshold
- ~~filter for hard/soft links~~ added in v0.6
- query, edit negative matches
- refine video search matches using neighboring hashes

### GUI
- barebones index/search gui
- open zip'd files/dirs directly where supported (dolphin/gwenview/nomacs?)
- cv min/max filter 8-bit indexed
- disable/enable relevant actions per selection/other state
- show results in batches as they are being computed, for slow queries
- toggle histogram view
- context menu: copy path to clipboard
- when deleting zip, remove all zip contents from viewer
- ~~remember past deletions and optionally replay them in the future should they reappear (via traal)~~ added "weeds" feature v0.6
- detect breaking of symlinks on delete/rename
- visual indicator of the needle in group view, gets lost when rotating
- ~~side-by-side playback: fix narrow videos~~
- video compare: use the same zoom/pan controls as image view
- select-all, clear-selection
- ~~action groups to compact the context menu~~
- option to force layout to use one row/column

### Misc
- QString/char* sweep
- method declaration sweep
- unecessary "virtual"
- "override"
- "const"
- replace getenv() calls with qt version

Major Bugs
==========================
- might be possible for -remove to corrupt database
  - e.g. -select-type 1 -remove -update
- sws_scale buffer overflow (264x480 yuv420p)
- ~~MBW move folder option broken~~ removed in v0.5.1
- control-c during database write will usually corrupt sqlite database, most likely when the filesystem doesn't support locking

Minor Bugs
=========================
- ~~max matches (-p.mm) is off by one in the final result~~ fixed v0.5.1
- maybe scale up small svg prior to indexing
- 64-bit image support
- colordescriptor somewhat non-deterministic, could be a bug
- auto-vacuum database once in a while, maybe have a delete counter
- dctfeature hash logic seems flawed, needs analysis
- ~~ffmpeg deprecations, requires older branch to compile~~ fixed v0.6.2
- ~~replace qPrintable() used for file path with qUtf8Printable or QString~~ v0.6
- maybe problem with some chars in filenames, dirs ending in "!" are skipped by scanner
- ~~MGLW scale-to-fit does not work when diff image enabled~~ fixed v0.6
- MGLW up/down key selection swaps sides (scroll wheel does not)
- MGLW: delete multi-select as one batch
- MGLW: suppress QIR eof warnings from thread cancellation
- MGLW: load next row loses focus item on some systems (gnome?)
- ~~MGLW: template match (T) hides diff image / doesn't restore after reset (F5)~~ fixed v0.6
- MGLW: rename folder doesn't update all affected viewer paths
- MGLW: difference image clips white/light shades of grayscale images
- MGLW: mouse motion eats the next scroll wheel event  (gnome3+xcb)
- weeds: when deleting a file, do something about broken weeds condition
- weeds: add something to report and fix broken weed records, maybe part of -update
- ~~Theme: background shade stacks up with context menu~~
- CropWidget: redraw ghosting selection rect, also x,y offset problems
- Mac: native yes/no dialog has no shortcut keys ~~and weird icon~~
- AppImage: Fedora: "EGL Not Available" and no window titlebar
  - fix: change qt platform `-platform wayland-egl` or xcb

Theme
=========================

#### qdarkstyle/*/ (QSS widget stylesheet)
- QDarkStyle has been modified to make it grayscale and fix a few issues, todo: fork on github and put the link here
- tools/copystylesheet.sh copies Dark and Light style from QDarkStyle project

#### res/cbird.qss
- contains colors for stuff

#### res/cbird-richtext.css
- css with special markup for rich text, references cbird.qss colors

## Icons
Use the platform script in each target to generate icon bundle from source file (currently either res/cbird.[png|svg])
- mac/make-icons.sh
- windows/make-ico.sh

Conventions
=========================

## Code style
- Use clang-format, .clang-format has the config (Google-derived)
- Two spaces indent
- 100 character line
- Editor: Qt Creator is nice (enable ClangFormat plugin)

## Idioms
- Qt-based classes should use NO_COPY, NO_COPY_NO_DEFAULT.
- Non-qt should use Q_DISABLE_COPY_MOVE, unless copying is desired
- use "super" and "self" to refer to respective classes
- use QT_ASSERT liberally. Assertions are never disabled (yet, maybe some day). Frequent assertions are eventually replaced with error handling
- use qDebug() etc not printf/cout


Unit tests
=========================

Cbird uses the QTest unit test framework. The tests require a compatible test data set, see release page on github.

```shell
export TEST_DATA_DIR=</path/to/cbird-testdata>
cd unit/

# recompile, run all tests with coverage
./unit.sh -clean -coverage

# build coverage report
./coverage.sh

# run one test where <test> is the name of a .pro/.cpp file
# and unit-test is the (optional) test method (testThing)
./unit.sh <test> [unit-test] [...qtest-args]
```
