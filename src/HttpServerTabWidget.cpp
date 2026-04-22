#include "HttpServerTabWidget.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>

#include "ui_HttpServerTabWidget.h"

HttpServerTabWidget::HttpServerTabWidget(QWidget *parent)
    : QWidget(parent),
      _ui(new Ui::HttpServerTabWidget)
{
    _ui->setupUi(this);
}

HttpServerTabWidget::~HttpServerTabWidget()
{
    delete _ui;
}

QTableWidget *HttpServerTabWidget::sharedFolderTableWidget() const
{
    return _ui->sharedFolderTableWidget;
}

QPushButton *HttpServerTabWidget::addSharedFolderButton() const
{
    return _ui->addSharedFolderButton;
}

QPushButton *HttpServerTabWidget::editSharedFolderButton() const
{
    return _ui->editSharedFolderButton;
}

QPushButton *HttpServerTabWidget::removeSharedFolderButton() const
{
    return _ui->removeSharedFolderButton;
}

QSpinBox *HttpServerTabWidget::httpServerPortSpinBox() const
{
    return _ui->httpServerPortSpinBox;
}

QLineEdit *HttpServerTabWidget::httpServerTokenEdit() const
{
    return _ui->httpServerTokenEdit;
}

QPushButton *HttpServerTabWidget::startHttpServerButton() const
{
    return _ui->startHttpServerButton;
}

QPushButton *HttpServerTabWidget::stopHttpServerButton() const
{
    return _ui->stopHttpServerButton;
}

QPlainTextEdit *HttpServerTabWidget::httpServerEndpointEdit() const
{
    return _ui->httpServerEndpointEdit;
}
