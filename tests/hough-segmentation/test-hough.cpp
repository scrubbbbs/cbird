#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QList>
#include <QMainWindow>
#include <QPixmap>
#include <QPushButton>
#include <QRect>
#include <QScrollArea>
#include <QSlider>
#include <QStyleHints>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <cmath>
#include <functional>
#include <opencv2/opencv.hpp>

#include "../../src/cvutil.h"

// Function to display a list of QImages in a grid layout within a QScrollArea
void showImagesInGridLayout(const QList<QImage>& images, const QString& title = "Image Grid View") {
  // Create a new top-level widget (e.g., QMainWindow)
  QMainWindow* mainWindow = new QMainWindow;
  mainWindow->setWindowTitle(title);

  // Create a central widget to hold the QGridLayout
  QWidget* centralWidget = new QWidget(mainWindow);
  QGridLayout* gridLayout = new QGridLayout(centralWidget);
  gridLayout->setAlignment(Qt::AlignTop); // Align items to the top

  // Create the QScrollArea
  QScrollArea* scrollArea = new QScrollArea(centralWidget);
  scrollArea->setWidgetResizable(true); // Ensure the widget inside can resize
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); //hide horizontal scrollbar

  // Create a widget to hold the grid layout.  This is what will go in the scroll area.
  QWidget* gridWidget = new QWidget();
  QGridLayout* gridLayoutWidget = new QGridLayout(gridWidget);
  gridLayoutWidget->setAlignment(Qt::AlignTop);
  gridWidget->setLayout(gridLayoutWidget);

  // Populate the grid layout with the QImages using QLabel
  int row = 0;
  int col = 0;
  for (int i = 0; i < images.size(); ++i) {
    QImage image = images[i];
    QLabel* label = new QLabel();

    // Scale the image to a reasonable size for display
    if (image.height() > 240) image = image.scaled({360, 240}, Qt::KeepAspectRatio);

    label->setPixmap(QPixmap::fromImage(image));
    label->setAlignment(Qt::AlignCenter); // Center the image in the label
    gridLayoutWidget->addWidget(label, row, col);

    // Add a label for the image name
    QLabel* nameLabel = new QLabel(QString("Image %1").arg(i + 1));
    nameLabel->setAlignment(Qt::AlignCenter);
    gridLayoutWidget->addWidget(nameLabel, row + 1, col); // Place it below the image

    col++;
    if (col > 2) { // Adjust the number of columns as needed (3 in this case)
      col = 0;
      row += 2;    // Increment row by 2 because we added 2 labels (image and name)
    }
  }

  // Set the grid widget as the scroll area's widget
  scrollArea->setWidget(gridWidget);

  // Add the scroll area to the main layout
  gridLayout->addWidget(scrollArea, 0, 0); //span all columns

  auto closeAction = new QAction("Close");
  closeAction->setShortcut(Qt::Key_Escape);
  QObject::connect(closeAction, &QAction::triggered, mainWindow, &QMainWindow::close);
  mainWindow->addAction(closeAction);

  // Set the central widget and resize the main window
  mainWindow->setCentralWidget(centralWidget);
  mainWindow->resize(1920, 1080);
  mainWindow->show();
}

// Global param state
DemosaicParams g_params;
const DemosaicParams g_defaultParams;

// -------- UI Helpers --------

QWidget* createIntControl(QWidget* parent,
                          const QString& label,
                          int min,
                          int max,
                          int* value,
                          int defaultValue,
                          std::function<void()> onChange,
                          bool isOdd = false) {
  QWidget* container = new QWidget(parent);
  QHBoxLayout* layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);

  QLabel* nameLabel = new QLabel(label);
  QSlider* slider = new QSlider(Qt::Horizontal);
  slider->setRange(min, max);
  slider->setValue(*value);

  QSpinBox* spinBox = new QSpinBox;
  spinBox->setRange(min, max);
  spinBox->setValue(*value);

  QPushButton* reset = new QPushButton("Reset");
  reset->setFixedWidth(50);

  layout->addWidget(nameLabel);
  layout->addWidget(slider, 1);
  layout->addWidget(spinBox);
  layout->addWidget(reset);

  QObject::connect(slider, &QSlider::valueChanged, [=](int val) {
    if (isOdd && (val % 2) == 0) {
      if (val > slider->minimum()) val--;
      if (val < slider->maximum()) val++;
      spinBox->setValue(val);
      return;
    }
    *value = val;
    spinBox->setValue(val);
    onChange();
  });

  QObject::connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), [=](int val) {
    if (isOdd && (val % 2) == 0) {
      if (val > slider->minimum()) val--;
      if (val < slider->maximum()) val++;
      slider->setValue(val);
      return;
    }
    *value = val;
    slider->setValue(val);
    onChange();
  });

  QObject::connect(reset, &QPushButton::clicked, [=]() {
    *value = defaultValue;
    slider->setValue(defaultValue);
    spinBox->setValue(defaultValue);
    onChange();
  });

  return container;
}

QWidget* createFloatControl(QWidget* parent,
                            const QString& labelText,
                            float min,
                            float max,
                            float step,
                            float& value,
                            float defaultValue,
                            std::function<void()> onChange) {
  QWidget* container = new QWidget(parent);
  QHBoxLayout* layout = new QHBoxLayout(container);
  QLabel* label = new QLabel(labelText);
  QDoubleSpinBox* spin = new QDoubleSpinBox;
  spin->setRange(min, max);
  spin->setSingleStep(step);
  spin->setValue(value);
  QPushButton* reset = new QPushButton("Reset");
  layout->addWidget(label);
  layout->addWidget(spin);
  layout->addWidget(reset);
  QObject::connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                   [&value, onChange](double val) {
                     value = static_cast<float>(val);
                     onChange();
                   });
  QObject::connect(reset, &QPushButton::clicked, [&value, spin, defaultValue, onChange]() {
    value = defaultValue;
    spin->setValue(value);
    onChange();
  });
  return container;
}

// -------- Main GUI Window --------
class DemosaicTestWindow : public QMainWindow {
 public:
  DemosaicTestWindow(const cv::Mat& inputImage)
      : inputImage(inputImage) {
    setWindowTitle("Demosaic Parameter Tester");
    resize(2500, 1500);

    QWidget* central = new QWidget(this);
    QHBoxLayout* mainLayout = new QHBoxLayout(central);

    central->setObjectName("centralWidget");
    // central->setAttribute(Qt::WA_OpaquePaintEvent, true);
    // central->setAutoFillBackground(true);
    central->setStyleSheet("#centralWidget { background-color: #888; }");

    // Left + Middle image displays
    imageLabel1 = new QLabel;
    imageLabel2 = new QLabel;
    imageLabel1->setAlignment(Qt::AlignCenter);
    imageLabel2->setAlignment(Qt::AlignCenter);

    QWidget* leftImageWidget = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftImageWidget);
    leftLayout->addWidget(imageLabel1);
    mainLayout->addWidget(leftImageWidget, 1);

    QWidget* middleImageWidget = new QWidget(this);
    QVBoxLayout* middleLayout = new QVBoxLayout(middleImageWidget);
    middleLayout->addWidget(imageLabel2);
    mainLayout->addWidget(middleImageWidget, 1);

    // Controls on the right
    QWidget* controlPanel = new QWidget(this);
    QVBoxLayout* controls = new QVBoxLayout(controlPanel);

    controls->addWidget(createIntControl(this, "Clip Histogram", 0, 100,
                                         &g_params.clipHistogramPercent,
                                         g_defaultParams.clipHistogramPercent, [=] { rerun(); }));

    controls->addWidget(createFloatControl(this, "Border Thresh", 0.00f, 100.0f, 0.5f,
                                           g_params.borderThresh, g_defaultParams.borderThresh,
                                           [=] { rerun(); }));

    controls->addWidget(createIntControl(
        this, "Pre-Blur Kernel", 1, 15, &g_params.preBlurKernel, g_defaultParams.preBlurKernel,
        [=] { rerun(); }, true));
    controls->addWidget(createIntControl(
        this, "Post-Blur Kernel", 1, 15, &g_params.postBlurKernel, g_defaultParams.postBlurKernel,
        [=] { rerun(); }, true));

    controls->addWidget(createIntControl(this, "Canny Thresh 1", 0, 255, &g_params.cannyThresh1,
                                         g_defaultParams.cannyThresh1, [=] { rerun(); }));
    controls->addWidget(createIntControl(this, "Canny Thresh 2", 0, 255, &g_params.cannyThresh2,
                                         g_defaultParams.cannyThresh2, [=] { rerun(); }));
    controls->addWidget(createIntControl(this, "Horizontal Margin", 0, 100, &g_params.hMargin,
                                         g_defaultParams.hMargin, [=] { rerun(); }));
    controls->addWidget(createIntControl(this, "Vertical Margin", 0, 100, &g_params.vMargin,
                                         g_defaultParams.vMargin, [=] { rerun(); }));
    controls->addWidget(createIntControl(this, "Grid Tolerance", 0, 20, &g_params.gridTolerance,
                                         g_defaultParams.gridTolerance, [=] { rerun(); }));
    controls->addWidget(createIntControl(this, "Min Grid Spacing", 8, 256, &g_params.minGridSpacing,
                                         g_defaultParams.minGridSpacing, [=] { rerun(); }));

    controls->addWidget(createFloatControl(this, "Hough Rho", 0.1f, 2.0f, 0.1f, g_params.houghRho,
                                           g_defaultParams.houghRho, [=] { rerun(); }));
    controls->addWidget(createFloatControl(this, "Hough Theta", 0.01f, 90.0f, 0.01f,
                                           g_params.houghTheta, g_defaultParams.houghTheta,
                                           [=] { rerun(); }));
    controls->addWidget(createFloatControl(this, "Hough Thresh Factor", 0.1f, 2.0f, 0.05f,
                                           g_params.houghThreshFactor,
                                           g_defaultParams.houghThreshFactor, [=] { rerun(); }));

    auto* button = new QPushButton("Show Rects");
    connect(button, &QPushButton::pressed, this, &DemosaicTestWindow::showRects);
    controls->addWidget(button);

    controls->addStretch();
    mainLayout->addWidget(controlPanel, 1);

    setCentralWidget(central);

    runTimer = new QTimer;
    runTimer->setSingleShot(true);
    connect(runTimer, &QTimer::timeout, this, &DemosaicTestWindow::rerun_actual);

    rerun();
  }

 protected:
  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Escape) close();
  }

 private:
  const cv::Mat& inputImage;
  QLabel* imageLabel1;
  QLabel* imageLabel2;
  QTimer* runTimer;

  QVector<QRect> _rects;

  void showRects() const {
    QImage img;
    cvImgToQImage(inputImage, img);
    QList<QImage> images;
    for (int i = 0; i < _rects.count(); ++i) {
      auto& r = _rects[i];
      images.append(img.copy(r));
    }
    showImagesInGridLayout(images);
  }

  void rerun() { runTimer->start(100); }

  void rerun_actual() {
    _rects.clear();
    QList<QImage> outImages;
    demosaicHough(inputImage, _rects, g_params, &outImages);

    if (outImages.isEmpty()) return;

    int width = this->width() / 3;
    int height = this->height();

    if (outImages.size() > 0)
      imageLabel1->setPixmap(QPixmap::fromImage(
          outImages[0].scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    if (outImages.size() > 1)
      imageLabel2->setPixmap(QPixmap::fromImage(
          outImages[1].scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
  }
};

QFileInfoList getFilesRecursive(const QString& path) {
  QDir dir(path);
  Q_ASSERT(dir.exists());

  QStringList filters;
  filters << "*.jpg" << "*.jpeg" << "*.png" << "*.bmp" << "*.tiff";

  auto list = dir.entryInfoList(filters, QDir::Files);

  const auto dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
  qDebug() << dirs;
  for (auto& d : dirs)
    list.append(getFilesRecursive(d.absoluteFilePath()));

  return list;
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);

  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <image_directory>" << std::endl;
    return 1;
  }

  QDir dir(QString::fromLocal8Bit(argv[1]));
  if (!dir.exists()) {
    std::cerr << "Directory does not exist: " << argv[1] << std::endl;
    return 1;
  }

  QFileInfoList files = getFilesRecursive(dir.absolutePath());

  if (files.isEmpty()) {
    qDebug() << "No image files found in directory: " << argv[1];
    return 1;
  }

  for (const QFileInfo& fileInfo : files) {
    qDebug() << "Testing: " << fileInfo.fileName();

    cv::Mat img = cv::imread(fileInfo.absoluteFilePath().toStdString());
    if (img.empty()) {
      qDebug() << "Failed to load: " << fileInfo.fileName();
      continue;
    }

    // Create a new test window for this image
    DemosaicTestWindow* window = new DemosaicTestWindow(img);
    window->setWindowTitle(fileInfo.fileName());
    window->show();

    app.exec();    // Run event loop for this window

    delete window; // Clean up after window closes
  }

  return 0;
}
