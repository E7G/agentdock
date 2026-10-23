#pragma once

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QSystemTrayIcon>
#include <QTimer>

class QAction;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QCloseEvent;
class NgrokManager;

// Qt is a local UI/controller only. The upstream Go executable remains the
// complete AgentDock MCP runtime; this app never reimplements MCP tools.
class AgentDockWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit AgentDockWindow(QWidget *parent = nullptr);
    ~AgentDockWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void startCore();
    void stopCore();
    void toggleTunnel();
    void refreshHealth();
    void saveSettings();
    void chooseCore();
    void showWindow();
    void quitApplication();

private:
    void setupUi();
    void setupTray();
    void log(const QString &message);
    void syncUi();
    void copyText(const QString &value);
    QString corePath() const;
    QString localUrl() const;
    QString bearerToken();
    void handleCoreExit(int code, QProcess::ExitStatus status);
    void finishHealthCheck(bool authenticated, bool externalPortOccupied = false);

    QProcess core_;
    NgrokManager *ngrok_ = nullptr;
    QNetworkAccessManager http_;
    QTimer pollTimer_;
    QSystemTrayIcon *tray_ = nullptr;
    QAction *trayStart_ = nullptr;
    QAction *trayTunnel_ = nullptr;
    QLabel *coreStatus_ = nullptr;
    QLabel *tunnelStatus_ = nullptr;
    QLabel *localAddress_ = nullptr;
    QLabel *publicAddress_ = nullptr;
    QLineEdit *coreExecutable_ = nullptr;
    QLineEdit *ngrokToken_ = nullptr;
    QLineEdit *domain_ = nullptr;
    QLineEdit *ngrokExecutable_ = nullptr;
    QSpinBox *port_ = nullptr;
    QPushButton *coreButton_ = nullptr;
    QPushButton *tunnelButton_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    QString publicUrl_;
    bool healthy_ = false;
    bool portOccupied_ = false;
    bool pollPending_ = false;
    bool quitting_ = false;
};
