#include "HttpClientTabWidget.h"

#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QTableWidget>

#include "ui_HttpClientTabWidget.h"

HttpClientTabWidget::HttpClientTabWidget(QWidget *parent)
    : QWidget(parent),
      _ui(new Ui::HttpClientTabWidget)
{
    _ui->setupUi(this);
}

HttpClientTabWidget::~HttpClientTabWidget()
{
    delete _ui;
}

QLineEdit *HttpClientTabWidget::serverUrlEdit() const
{
    return _ui->serverUrlEdit;
}

QLineEdit *HttpClientTabWidget::accessTokenEdit() const
{
    return _ui->accessTokenEdit;
}

QPushButton *HttpClientTabWidget::fetchSourcesButton() const
{
    return _ui->fetchSourcesButton;
}

QTableWidget *HttpClientTabWidget::availableSourcesTableWidget() const
{
    return _ui->availableSourcesTableWidget;
}

QLineEdit *HttpClientTabWidget::targetRootEdit() const
{
    return _ui->targetRootEdit;
}

QPushButton *HttpClientTabWidget::browseTargetRootButton() const
{
    return _ui->browseTargetRootButton;
}

QPushButton *HttpClientTabWidget::addSelectedSourcesButton() const
{
    return _ui->addSelectedSourcesButton;
}

QTableView *HttpClientTabWidget::pairTableView() const
{
    return _ui->pairTableView;
}

QPushButton *HttpClientTabWidget::removePairButton() const
{
    return _ui->removePairButton;
}

QPushButton *HttpClientTabWidget::syncAllPairsButton() const
{
    return _ui->syncAllPairsButton;
}
