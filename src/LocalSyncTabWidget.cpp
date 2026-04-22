#include "LocalSyncTabWidget.h"

#include <QComboBox>
#include <QPushButton>
#include <QTableView>

#include "ui_LocalSyncTabWidget.h"

LocalSyncTabWidget::LocalSyncTabWidget(QWidget *parent)
    : QWidget(parent),
      _ui(new Ui::LocalSyncTabWidget)
{
    _ui->setupUi(this);
}

LocalSyncTabWidget::~LocalSyncTabWidget()
{
    delete _ui;
}

QPushButton *LocalSyncTabWidget::addPairButton() const
{
    return _ui->addPairButton;
}

QPushButton *LocalSyncTabWidget::removePairButton() const
{
    return _ui->removePairButton;
}

QTableView *LocalSyncTabWidget::pairTableView() const
{
    return _ui->pairTableView;
}

QPushButton *LocalSyncTabWidget::startMonitorButton() const
{
    return _ui->startMonitorButton;
}

QPushButton *LocalSyncTabWidget::stopMonitorButton() const
{
    return _ui->stopMonitorButton;
}

QPushButton *LocalSyncTabWidget::syncAllPairsButton() const
{
    return _ui->syncAllPairsButton;
}

QComboBox *LocalSyncTabWidget::maxParallelSyncComboBox() const
{
    return _ui->maxParallelSyncComboBox;
}

QComboBox *LocalSyncTabWidget::compareModeComboBox() const
{
    return _ui->compareModeComboBox;
}
