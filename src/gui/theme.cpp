#include "theme.h"

#include "shadewidget.h"

#include "../qtutil.h"

#include <QtCore/QFile>
#include <QtCore/QMessageLogger>
#include <QtCore/QMetaProperty>
#include <QtCore/QThread>

#include <QtGui/QPainter>
#include <QtGui/QStyleHints>
#include <QtGui/QTextDocument>
#include <QtGui/QWindow>

#include <QtWidgets/QApplication>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLayout>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStyle>
#include <QtWidgets/QTableWidget>

QString* Theme::_defaultStyle = nullptr;

Theme::Theme(QWidget* parent) : QWidget(parent) {}

Theme::~Theme() {}

Theme& Theme::instance() {
  static Theme* theme = nullptr;
  if (!theme) theme = new Theme(nullptr);

  return *theme;
}

void Theme::setDefaultStyle(const QString& style) {
  delete _defaultStyle;
  _defaultStyle = new QString(style);
  qDebug() << *_defaultStyle;
}

void Theme::setup() {
  Q_ASSERT(qApp->thread() == QThread::currentThread());

  if (qApp->metaObject()->className() != qq("QApplication"))
    qFatal("not a gui application...did you use -headless?");

  static bool wasSetup = false;
  if (wasSetup) return;
  wasSetup = true;

  QStyle* styleObject = qApp->style();

  const QString darkStyle = qq("Dark");    // built-in dark stylesheet
  const QString lightStyle = qq("Light");  // built-in light stylesheet
  const QString noStyle = qq("Qt");        // use default qt style
  const QString autoStyle = qq("Auto");    // detect dark or light if possible, fallback to qt

  QString style;
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  style = autoStyle;
#else
  style = darkStyle;
#endif

  if (qEnvironmentVariableIsSet("CBIRD_STYLE")) style = qEnvironmentVariable("CBIRD_STYLE");

  if (_defaultStyle) style = *_defaultStyle;

#if QT_VERSION_MINOR >= 5
  const Qt::ColorScheme hint = qApp->styleHints()->colorScheme();
  if (style == autoStyle) {
    switch (hint) {
      case Qt::ColorScheme::Dark:
        style = darkStyle;
        break;
      case Qt::ColorScheme::Light:
        style = lightStyle;
        break;
      case Qt::ColorScheme::Unknown:
        style = noStyle;
        break;
    }
  }
#else
  const int hint = 0;
  if (style == autoStyle) style = noStyle;
#endif

  QStringList styleSheets;
  if (style == darkStyle)
    styleSheets += qq(":qdarkstyle/dark/darkstyle.qss");
  else if (style == lightStyle)
    styleSheets += qq(":qdarkstyle/light/lightstyle.qss");

  styleSheets += qq(":res/cbird.qss");

  QString styleSheet;
  for (auto& path : styleSheets) {
    QFile f(path);
    if (!f.open(QFile::ReadOnly)) qFatal("failed to open stylesheet: %s", qUtf8Printable(path));
    styleSheet += f.readAll();
  }

  if (!styleSheet.isEmpty()) qApp->setStyleSheet(styleSheet);

  qDebug() << hint << style << styleObject->metaObject()->className() << styleSheets;

  auto& theme = Theme::instance();  // construct Theme

  theme.setProperty("style", style);  // visible in stylesheets
  theme._style = style;               // visible elsewhere
  theme._baseStyle = styleObject;     // useful info

  theme.style()->polish(&theme);  // get properties from cbird.qss
  theme.probe();                  // get properties from system theme

  if (qEnvironmentVariableIsSet("CBIRD_THEME_TOOLBOX")) {
    theme._toolboxActive = true;
    showToolbox();
  }
}

void Theme::probe() {
  QTableWidget widget(&Theme::instance());  // usually has altbase
  widget.style()->polish(&widget);          // initialize palette

  QStyleOptionViewItem option;  // item for table row background/item
  option.initFrom(&widget);
  _base = option.palette.base().color();
  _altBase = option.palette.alternateBase().color();
  _text = option.palette.text().color();

  qDebug() << "base:" << _base.name() << "altbase:" << _altBase.name() << "text:" << _text.name();
}

void Theme::polishWindow(QWidget* window) const {
  WidgetHelper::setWindowTheme(window, _style == "Dark");
}

void Theme::showWindow(QWidget* window, bool maximized) const {
  polishWindow(window);
  WidgetHelper::hackShowWindow(window, maximized);
}

QString Theme::richTextStyleSheet() const {
  // not thread-safe due to static qregexp
  Q_ASSERT(qApp->thread() == QThread::currentThread());

  QFile f(qq(":res/cbird-richtext.css"));
  if (!f.open(QFile::ReadOnly)) {
    qWarning("failed to open rich text stylesheet");
    return "";
  }

  const QString inCss = f.readAll();
  QStringView subject(inCss);

  QString outCss;
  outCss.reserve(subject.length());  // macro expansion makes it smaller

  const auto parse = [](const QStringView& str, QStringView& macro,
                        QList<QStringView>& args, int&start, int& end) {
    int i0 = str.indexOf(ll("qt("));
    int i1 = str.indexOf(ll("theme("));
    if (i0 < 0 && i1 < 0) return false; // nothing found
    if (i0 < 0) i0 = INT_MAX; // if not found, its always > the other one
    if (i1 < 0) i1 = INT_MAX;
    int offset = 0;
    if (i0 < i1) { // choose token that occurred first
      start = i0, offset = 3; // "qt("
    } else {
      start = i1, offset = 6; // "theme("
    }

    end = str.indexOf(lc(')'), start);
    if (end < 0) return false;

    macro = str.mid(start, offset-1);
    args = str.mid(start+offset, end-start-offset).split(lc(','));
    return true;
  };

  QStringView macro;
  QList<QStringView> args;
  int start, end;
  while (parse(subject, macro, args, start, end)) {
    QColor color(Qt::white);
    if (macro == ll("qt")) {
      if (Q_UNLIKELY(args.count() != 2)) qFatal("qt() requires two arguments");
      auto& group = args[0];
      auto& role = args[1];
      if (Q_UNLIKELY(group != ll("normal")))
        qFatal("invalid color group: %s", group.toUtf8().constData());
      else if (role == ll("base"))
        color = _base;
      else if (role == ll("altbase"))
        color = _altBase;
      else if (role == ll("text"))
        color = _text;
      else
        qFatal("invalid color role: %s", group.toUtf8().constData());
    } else if (macro == ll("theme")) {
      if (Q_UNLIKELY(args.count() != 1)) qFatal("theme() requires one argument");
      const auto propertyName = args[0].toUtf8();
      const QVariant v = this->property(propertyName.constData());
      if (Q_UNLIKELY(!v.isValid()))
        qFatal("theme() invalid property: %s", propertyName.constData());
      color = qvariant_cast<QColor>(v);
    }

    outCss += subject.mid(0, start);
    outCss += color.name();
    subject = subject.mid(end+1);
  }
  outCss += subject;

  // qDebug().noquote() << outCss;

  return outCss;
}

void Theme::drawRichText(QPainter* painter, const QRect& r, const QString& text) {
  Q_ASSERT(QThread::currentThread() == qApp->thread());
  static QTextDocument* td = nullptr;

  if (Q_UNLIKELY(!td)) {
    td = new QTextDocument;
    td->setDocumentMargin(0);
    td->setDefaultStyleSheet(Theme::instance().richTextStyleSheet());
  }

  if (Q_UNLIKELY(_toolboxActive)) td->setDefaultStyleSheet(Theme::instance().richTextStyleSheet());

  td->setHtml(text);

  painter->save();
  painter->translate(r.x(), r.y());
  QRect rect1 = QRect(0, 0, r.width(), r.height());

  td->drawContents(painter, rect1);

  painter->restore();
}

int Theme::execDialog(QMessageBox* dialog) const {

  // NOTE: warning icon is bugged (qt 6.5.1) set it ourselves
#ifdef Q_OS_MAC
  if (dialog->icon()==QMessageBox::Warning) {
    auto style = dialog->style();
    int iconSize = style->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, dialog);
    QIcon icon = style->standardIcon(QStyle::SP_MessageBoxWarning);
    dialog->setIconPixmap(icon.pixmap(QSize(iconSize,iconSize), dialog->devicePixelRatio()));
  }
#endif

  return execDialog((QDialog*)dialog);
}

int Theme::execDialog(QDialog* dialog) const {
#ifdef Q_OS_MAC
  dialog->setWindowModality(Qt::WindowModal); // sheet already shades the window
#else
  std::unique_ptr<ShadeWidget> shade;
  if (dialog->parentWidget()) shade.reset(new ShadeWidget(dialog->parentWidget()));
#endif

  polishWindow(dialog);
  WidgetHelper::hackShowWindow(dialog);
  return dialog->exec();
}

int Theme::execInputDialog(QInputDialog* dialog, const QString& title, const QString& label,
                           const QString& text, const QStringList& completions) const {
  dialog->setInputMode(QInputDialog::TextInput);
  dialog->setWindowTitle(title);
  dialog->setLabelText(label);

  if (completions.count() > 0) {
    dialog->setComboBoxItems(completions);
    dialog->setComboBoxEditable(true);
  }
  dialog->setTextValue(text);

  return execDialog(dialog);
}

QString Theme::getExistingDirectory(const QString& action, const QString& label,
                                    const QString& dirPath, QWidget* parent) const {
  QFileDialog dialog(parent, action);

  // we have to move to a subdirectory so use the basic dialog,the extra
  // features are doing anything for us. and it is much faster on windows
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOptions(QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog |
                    QFileDialog::HideNameFilterDetails  | QFileDialog::DontResolveSymlinks |
                    QFileDialog::DontUseCustomDirectoryIcons);
  dialog.setDirectory(dirPath);
  dialog.setLabelText(QFileDialog::FileName, label);
  dialog.setLabelText(QFileDialog::Accept, action);

  int result = Theme::instance().execDialog(&dialog);
  if (result != QFileDialog::Accepted) return QString();

  return dialog.selectedFiles().value(0);
}

void Theme::showToolbox() {
  QStringList props;
  auto dump_props = [&props](QObject* o) {
    auto mo = o->metaObject();
    do {
      for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        QString name = mo->property(i).name();
        if (name.endsWith(ll("_base")) || name.endsWith(ll("_altbase"))) props.append(name);
      }
    } while ((mo = mo->superClass()));
  };

  dump_props(&Theme::instance());

  auto* window = new QWidget;
  QString info = Theme::instance().property("style").toString() + " | " +
                 Theme::instance()._baseStyle->metaObject()->className();
  window->setWindowTitle(info);
  window->setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);

  auto* hLayout = new QHBoxLayout(window);
  auto* vLayout = new QVBoxLayout(window);
  vLayout->setAlignment(Qt::AlignRight);
  hLayout->addLayout(vLayout);

  for (auto& name : std::as_const(props)) {
    QVariant prop = Theme::instance().property(qUtf8Printable(name));
    QColor color = qvariant_cast<QColor>(prop);

    auto* cLayout = new QHBoxLayout(window);
    vLayout->addLayout(cLayout);

    QPushButton* button = new QPushButton(name, window);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    cLayout->addWidget(button);

    QLineEdit* edit = new QLineEdit(color.name(), window);
    cLayout->addWidget(edit);

    QPalette palette = window->palette();
    palette.setColor(QPalette::Button, Theme::instance()._base);
    palette.setColor(QPalette::ButtonText, color);
    button->setPalette(palette);

    connect(button, &QPushButton::pressed, button, [button, edit, window, name] {
      QVariant prop = Theme::instance().property(qUtf8Printable(name));
      QColor color = qvariant_cast<QColor>(prop);

      QColorDialog colorDialog(color, window);
      colorDialog.setWindowTitle(name);
      colorDialog.setMouseTracking(false);
      colorDialog.move(window->geometry().topRight());

      connect(&colorDialog, &QColorDialog::currentColorChanged, button,
              [name](const QColor& color) {
                Theme::instance().setProperty(qUtf8Printable(name), color);
                for (auto w : qApp->allWindows()) w->requestUpdate();
              });

      if (QDialog::Accepted != Theme::instance().execDialog(&colorDialog)) {
        Theme::instance().setProperty(qUtf8Printable(name), color);
        for (auto w : qApp->allWindows()) w->requestUpdate();
        return;
      }

      QPalette palette = window->palette();
      palette.setColor(QPalette::Button, Theme::instance()._base);
      palette.setColor(QPalette::ButtonText, color);
      button->setPalette(palette);

      edit->setText(color.name());
      for (auto w : qApp->allWindows()) w->requestUpdate();
    });
  }

  Theme::instance().showWindow(window);
}
