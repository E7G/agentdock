#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMessageBox>
#include <QSystemTrayIcon>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("E7G"));
    QCoreApplication::setApplicationName(QStringLiteral("AgentDockQt"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("AgentDock Qt Desktop"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setQuitOnLastWindowClosed(false);

    AgentDockWindow window;
    window.show();
    return app.exec();
}
