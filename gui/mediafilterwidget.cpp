#include "mediafilterwidget.h"
#include "mediagrouptablewidget.h"
#include "qtutil.h"

MediaFilterWidget::MediaFilterWidget(QWidget* parent) : QWidget(parent)
{
    QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
    settings.beginGroup(metaObject()->className());

    _match = settings.value("matchMask").toInt();
    if (_match <= 0)
        _match = MediaGroupTableModel::ShowAll;

    _size = settings.value("minSize").toInt();
    if (_size <= 0)
        _size = 0;

    _path = settings.value("path").toString();

    QHBoxLayout* layout = new QHBoxLayout(this);

    constexpr struct {
        const char* label;
        int value;
        int _padding;
    } flags[] = {
        { "All",       MediaGroupTableModel::ShowAll,      0 },
        { "No Match",  MediaGroupTableModel::ShowNoMatch,  0 },
        { "Any Match", MediaGroupTableModel::ShowAnyMatch, 0 },
        { "Bigger",    MediaGroupTableModel::ShowBigger,   0 },
        { "Smaller",   MediaGroupTableModel::ShowSmaller,  0 },
        { nullptr, -1, 0 }
    };

    _matchMenu = new QMenu(this);

    int i = 0;
    while (flags[i].label)
    {
        QAction* action;
        action = _matchMenu->addAction(flags[i].label, this, SLOT(matchMenuTriggered()));
        action->setCheckable(true);
        action->setData(flags[i].value);
        if (_match & flags[i].value)
            action->setChecked(true);
        i++;
    }

    _menuButton = new QPushButton("Match...", this);

    connect(_menuButton, SIGNAL(clicked()),
                   this, SLOT(matchButtonPressed()));

    layout->addWidget(_menuButton);

    layout->addWidget(new QLabel("MinSize:", this));

    // todo: settings
    QComboBox * sizeFilter = new QComboBox(this);
    sizeFilter->addItem("None");
    sizeFilter->addItem("32");
    sizeFilter->addItem("64");
    sizeFilter->addItem("200");
    sizeFilter->addItem("400");
    sizeFilter->addItem("640");
    sizeFilter->addItem("960");
    sizeFilter->addItem("1080");
    sizeFilter->addItem("1200");
    sizeFilter->addItem("1350");
    sizeFilter->addItem("1600");
    sizeFilter->addItem("1920");
    layout->addWidget(sizeFilter);

    sizeFilter->setCurrentIndex(sizeFilter->findText(QString::number(_size)));

    connect(sizeFilter, SIGNAL(currentTextChanged(const QString&)),
                  this, SLOT(sizeTextChanged(const QString&)));

    layout->addWidget(new QLabel("Path:", this));

    QLineEdit* pathFilter = new QLineEdit(this);
    layout->addWidget(pathFilter);
    pathFilter->setText(_path);

    connect(pathFilter, SIGNAL(textChanged(const QString&)),
                 this,  SLOT(pathTextChanged(const QString&)));

    layout->addSpacerItem(new QSpacerItem(1,1, QSizePolicy::Expanding));
}

MediaFilterWidget::~MediaFilterWidget()
{
    QSettings settings(DesktopHelper::settingsFile(), QSettings::IniFormat);
    settings.beginGroup(metaObject()->className());

    settings.setValue("matchMask", _match);
    settings.setValue("minSize", _size);
    settings.setValue("path", _path);
}

void MediaFilterWidget::connectModel(QObject* model, const char* slot)
{
    connect(this, SIGNAL(filterChanged(int,int,const QString&)), model, slot);
    emit filterChanged(_match, _size, _path);
}

void MediaFilterWidget::matchButtonPressed()
{
    QPoint loc = _menuButton->geometry().bottomLeft();
    _matchMenu->popup( this->mapToGlobal( loc ) );
}

void MediaFilterWidget::matchMenuTriggered()
{
    QAction* action = dynamic_cast<QAction*>(sender());
    if (!action)
        return;

    int prev = _match;
    int flag = action->data().toInt();

    if (action->isChecked())
        _match = MediaGroupTableModel::validMatchFlags(_match, flag);
    else
        _match &= ~flag;

    for (QAction* a : _matchMenu->actions())
        a->setChecked( _match & a->data().toInt() );

    if (prev != _match)
        emit filterChanged(_match, _size, _path);
}

void MediaFilterWidget::sizeTextChanged(const QString& text)
{
    bool ok;
    _size = text.toInt(&ok);
    if (!ok)
        _size = 0;

    emit filterChanged(_match, _size, _path);
}

void MediaFilterWidget::pathTextChanged(const QString& text)
{
    _path = text;
    emit filterChanged(_match, _size, _path);
}
