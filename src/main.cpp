#include <QApplication>

#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("QtDemo"));
    QApplication::setApplicationName(QStringLiteral("SyncFolder"));

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
