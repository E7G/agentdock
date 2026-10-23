#include "mainwindow.h"
#include "credentials.h"
#include "ngrokmanager.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QIcon>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTime>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace {
QString generatedSecret()
{
    QString value;
    // System RNG, not a predictable timestamp or settings value.
    for (int i = 0; i < 4; ++i)
        value += QString::number(QRandomGenerator::system()->generate64(), 16).rightJustified(16, QLatin1Char('0'));
    return value;
}
}

AgentDockWindow::AgentDockWindow(QWidget *parent) : QMainWindow(parent), ngrok_(new NgrokManager(this))
{
    setWindowTitle(QStringLiteral("AgentDock · Qt Desktop"));
    setMinimumSize(710, 570);
    resize(870, 700);
    const QIcon brandIcon(QStringLiteral(":/icons/agentdock.png"));
    setWindowIcon(brandIcon.isNull() ? style()->standardIcon(QStyle::SP_ComputerIcon) : brandIcon);
    core_.setProcessChannelMode(QProcess::MergedChannels);
    setupUi();
    setupTray();
    connect(&core_, &QProcess::readyRead, this, [this] {
        QString output = QString::fromLocal8Bit(core_.readAll()).trimmed();
        const QString agentSecret = Credentials::readAgentDockToken();
        const QString ngrokSecret = Credentials::readNgrokToken();
        if (!agentSecret.isEmpty()) output.replace(agentSecret, QStringLiteral("[REDACTED]"));
        if (!ngrokSecret.isEmpty()) output.replace(ngrokSecret, QStringLiteral("[REDACTED]"));
        if (!output.isEmpty()) log(QStringLiteral("AgentDock: ") + output.left(3000));
    });
    connect(&core_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        log(QStringLiteral("核心进程错误：") + core_.errorString());
        syncUi();
    });
    connect(&core_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &AgentDockWindow::handleCoreExit);
    connect(ngrok_, &NgrokManager::statusChanged, this, [this](bool, const QString &url) {
        publicUrl_ = url;
        syncUi();
    });
    connect(ngrok_, &NgrokManager::logMessage, this, &AgentDockWindow::log);
    connect(ngrok_, &NgrokManager::errorOccurred, this, [this](const QString &error) {
        log(error);
        statusBar()->showMessage(error, 8000);
        syncUi();
    });
    connect(&pollTimer_, &QTimer::timeout, this, &AgentDockWindow::refreshHealth);
    pollTimer_.start(1800);
    refreshHealth();
    syncUi();
}

AgentDockWindow::~AgentDockWindow()
{
    pollTimer_.stop();
    if (ngrok_) ngrok_->stop();
    if (core_.state() != QProcess::NotRunning) {
        core_.terminate();
        if (!core_.waitForFinished(1500)) {
            core_.kill();
            core_.waitForFinished(1500);
        }
    }
}

void AgentDockWindow::setupUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("AgentDock 控制中心"));
    QFont titleFont = title->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);
    auto *subtitle = new QLabel(QStringLiteral("原生 Qt Widgets 托盘界面 · 上游 Go 核心 · MCP / 文件 / 系统工具"));
    subtitle->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(subtitle);

    QSettings settings;
    auto *coreCard = new QGroupBox(QStringLiteral("本机 AgentDock 核心"));
    auto *coreLayout = new QVBoxLayout(coreCard);
    auto *statusRow = new QHBoxLayout;
    coreStatus_ = new QLabel(QStringLiteral("正在检查…"));
    coreButton_ = new QPushButton(QStringLiteral("启动核心"));
    statusRow->addWidget(coreStatus_, 1);
    statusRow->addWidget(coreButton_);
    coreLayout->addLayout(statusRow);
    auto *config = new QFormLayout;
    auto *exeRow = new QHBoxLayout;
    coreExecutable_ = new QLineEdit(settings.value(QStringLiteral("coreExecutable")).toString());
    coreExecutable_->setPlaceholderText(QStringLiteral("agentdock.exe（默认为应用旁边或 PATH）"));
    auto *browse = new QPushButton(QStringLiteral("浏览…"));
    exeRow->addWidget(coreExecutable_, 1);
    exeRow->addWidget(browse);
    config->addRow(QStringLiteral("Go 核心"), exeRow);
    port_ = new QSpinBox;
    port_->setRange(1024, 65535);
    port_->setValue(settings.value(QStringLiteral("port"), 8765).toInt());
    config->addRow(QStringLiteral("HTTP 端口"), port_);
    coreLayout->addLayout(config);
    auto *urlRow = new QHBoxLayout;
    localAddress_ = new QLabel;
    localAddress_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *copyLocal = new QPushButton(QStringLiteral("复制 MCP 地址"));
    auto *copyAuth = new QPushButton(QStringLiteral("复制访问令牌"));
    urlRow->addWidget(localAddress_, 1);
    urlRow->addWidget(copyLocal);
    urlRow->addWidget(copyAuth);
    coreLayout->addLayout(urlRow);
    layout->addWidget(coreCard);

    auto *tunnelCard = new QGroupBox(QStringLiteral("ngrok HTTPS 公网连接"));
    auto *tunnelLayout = new QVBoxLayout(tunnelCard);
    auto *tunnelHead = new QHBoxLayout;
    tunnelStatus_ = new QLabel(QStringLiteral("未启动"));
    tunnelButton_ = new QPushButton(QStringLiteral("启动 ngrok"));
    tunnelHead->addWidget(tunnelStatus_, 1);
    tunnelHead->addWidget(tunnelButton_);
    tunnelLayout->addLayout(tunnelHead);
    auto *tunnelForm = new QFormLayout;
    ngrokToken_ = new QLineEdit(Credentials::readNgrokToken());
    ngrokToken_->setEchoMode(QLineEdit::Password);
    ngrokToken_->setPlaceholderText(QStringLiteral("ngrok authtoken（保存到 Windows 凭据管理器）"));
    domain_ = new QLineEdit(settings.value(QStringLiteral("ngrokDomain")).toString());
    domain_->setPlaceholderText(QStringLiteral("可选，已分配的 ngrok 固定域名"));
    ngrokExecutable_ = new QLineEdit(settings.value(QStringLiteral("ngrokExecutable")).toString());
    ngrokExecutable_->setPlaceholderText(QStringLiteral("可选：ngrok.exe 完整路径；Windows 可自动下载"));
    tunnelForm->addRow(QStringLiteral("ngrok 令牌"), ngrokToken_);
    tunnelForm->addRow(QStringLiteral("固定域名"), domain_);
    tunnelForm->addRow(QStringLiteral("程序路径"), ngrokExecutable_);
    tunnelLayout->addLayout(tunnelForm);
    auto *publicRow = new QHBoxLayout;
    publicAddress_ = new QLabel(QStringLiteral("未连接"));
    publicAddress_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *copyPublic = new QPushButton(QStringLiteral("复制公网 MCP 地址"));
    publicRow->addWidget(publicAddress_, 1);
    publicRow->addWidget(copyPublic);
    tunnelLayout->addLayout(publicRow);
    auto *warning = new QLabel(QStringLiteral("安全限制：只有由本应用启动、设置了随机 Bearer Token 且健康检查通过的核心才能开启 ngrok。外部托管的 Core 不会被自动暴露。"));
    warning->setWordWrap(true);
    warning->setStyleSheet(QStringLiteral("color: #b26a00;"));
    tunnelLayout->addWidget(warning);
    layout->addWidget(tunnelCard);

    auto *logCard = new QGroupBox(QStringLiteral("运行日志"));
    auto *logLayout = new QVBoxLayout(logCard);
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->document()->setMaximumBlockCount(500);
    logView_->setPlaceholderText(QStringLiteral("启动结果和服务事件将在这里显示"));
    logLayout->addWidget(logView_);
    layout->addWidget(logCard, 1);
    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("关闭窗口将隐藏到托盘；托盘右键可退出"));

    connect(coreButton_, &QPushButton::clicked, this, [this] {
        if (core_.state() == QProcess::NotRunning) startCore();
        else stopCore();
    });
    connect(tunnelButton_, &QPushButton::clicked, this, &AgentDockWindow::toggleTunnel);
    connect(browse, &QPushButton::clicked, this, &AgentDockWindow::chooseCore);
    connect(copyLocal, &QPushButton::clicked, this, [this] { copyText(localUrl()); });
    connect(copyAuth, &QPushButton::clicked, this, [this] {
        const QString secret = bearerToken();
        if (!secret.isEmpty()) {
            copyText(secret);
            log(QStringLiteral("访问令牌已复制。请勿贴在聊天或公开日志中。"));
        }
    });
    connect(copyPublic, &QPushButton::clicked, this, [this] {
        if (!publicUrl_.isEmpty()) copyText(publicUrl_ + QStringLiteral("/mcp"));
    });
    connect(coreExecutable_, &QLineEdit::editingFinished, this, &AgentDockWindow::saveSettings);
    connect(port_, qOverload<int>(&QSpinBox::valueChanged), this, &AgentDockWindow::saveSettings);
    connect(ngrokToken_, &QLineEdit::editingFinished, this, &AgentDockWindow::saveSettings);
    connect(domain_, &QLineEdit::editingFinished, this, &AgentDockWindow::saveSettings);
    connect(ngrokExecutable_, &QLineEdit::editingFinished, this, &AgentDockWindow::saveSettings);
}

void AgentDockWindow::setupTray()
{
    tray_ = new QSystemTrayIcon(windowIcon(), this);
    tray_->setToolTip(QStringLiteral("AgentDock Qt · 检查中"));
    auto *menu = new QMenu(this);
    auto *showAction = menu->addAction(QStringLiteral("显示控制中心"));
    trayStart_ = menu->addAction(QStringLiteral("启动核心"));
    trayTunnel_ = menu->addAction(QStringLiteral("启动 ngrok"));
    menu->addSeparator();
    auto *quitAction = menu->addAction(QStringLiteral("退出 AgentDock Qt"));
    connect(showAction, &QAction::triggered, this, &AgentDockWindow::showWindow);
    connect(trayStart_, &QAction::triggered, this, [this] {
        if (core_.state() == QProcess::NotRunning) startCore();
        else stopCore();
    });
    connect(trayTunnel_, &QAction::triggered, this, &AgentDockWindow::toggleTunnel);
    connect(quitAction, &QAction::triggered, this, &AgentDockWindow::quitApplication);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) showWindow();
    });
    tray_->setContextMenu(menu);
    if (QSystemTrayIcon::isSystemTrayAvailable()) tray_->show();
}

void AgentDockWindow::log(const QString &message)
{
    logView_->appendPlainText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss ")) + message.left(3000));
}

QString AgentDockWindow::corePath() const
{
    const QString explicitPath = coreExecutable_->text().trimmed();
    if (!explicitPath.isEmpty()) return explicitPath;
    const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("agentdock.exe"));
    if (QFileInfo::exists(beside)) return beside;
    return QStandardPaths::findExecutable(QStringLiteral("agentdock"));
}

QString AgentDockWindow::localUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1/mcp").arg(port_->value());
}

QString AgentDockWindow::bearerToken()
{
    QString token = Credentials::readAgentDockToken();
    if (!token.isEmpty()) return token;
    token = generatedSecret();
    QString error;
    if (!Credentials::writeAgentDockToken(token, &error)) {
        QMessageBox::critical(this, QStringLiteral("保存访问令牌失败"),
                              QStringLiteral("无法安全保存 Bearer Token，因此禁止启动网络服务：") + error);
        return {};
    }
    return token;
}

void AgentDockWindow::startCore()
{
    if (core_.state() != QProcess::NotRunning || portOccupied_) {
        log(QStringLiteral("当前端口已有服务或启动中，请勿重复启动"));
        return;
    }
    const QString exe = corePath();
    if (exe.isEmpty() || !QFileInfo(exe).isFile()) {
        QMessageBox::warning(this, QStringLiteral("找不到 AgentDock 核心"),
            QStringLiteral("请先构建/安装 agentdock.exe 并放到 Qt 程序旁边，或者在“Go 核心”里选择可执行文件。"));
        return;
    }
    const QString secret = bearerToken();
    if (secret.isEmpty()) return;
    saveSettings();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("AGENTDOCK_HOST"), QStringLiteral("127.0.0.1"));
    env.insert(QStringLiteral("AGENTDOCK_PORT"), QString::number(port_->value()));
    env.insert(QStringLiteral("AGENTDOCK_AUTH_TOKEN"), secret);
    core_.setProcessEnvironment(env);
    core_.setWorkingDirectory(QDir::homePath());
    core_.start(exe, {});
    if (!core_.waitForStarted(2500)) {
        log(QStringLiteral("无法启动核心：") + core_.errorString());
        return;
    }
    log(QStringLiteral("已启动 AgentDock Go 核心，等待 /healthz 响应"));
    syncUi();
    refreshHealth();
}

void AgentDockWindow::stopCore()
{
    if (ngrok_->isRunning()) ngrok_->stop();
    if (core_.state() == QProcess::NotRunning) {
        log(QStringLiteral("核心不是由此窗口启动的，不会结束其他程序"));
        return;
    }
    core_.terminate();
    QTimer::singleShot(1800, this, [this] {
        if (core_.state() != QProcess::NotRunning) core_.kill();
    });
    syncUi();
}

void AgentDockWindow::handleCoreExit(int code, QProcess::ExitStatus)
{
    healthy_ = false;
    if (ngrok_->isRunning()) ngrok_->stop();
    log(QStringLiteral("AgentDock 核心已退出（代码 %1）").arg(code));
    syncUi();
}

void AgentDockWindow::toggleTunnel()
{
    if (ngrok_->isRunning()) {
        ngrok_->stop();
        publicUrl_.clear();
        syncUi();
        return;
    }
    if (core_.state() != QProcess::Running || !healthy_) {
        QMessageBox::warning(this, QStringLiteral("禁止公网暴露"),
            QStringLiteral("必须先使用此 Qt 应用启动 AgentDock 核心，并确认它已通过健康检查。"));
        return;
    }
    if (bearerToken().isEmpty()) return;
    const QString ngrokSecret = ngrokToken_->text().trimmed();
    if (ngrokSecret.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("缺少 ngrok token"),
                             QStringLiteral("请填写 ngrok authtoken。MCP Bearer Token 与 ngrok authtoken 是两种独立凭据。"));
        return;
    }
    QString error;
    if (!Credentials::writeNgrokToken(ngrokSecret, &error)) {
        QMessageBox::warning(this, QStringLiteral("无法安全保存 ngrok token"), error);
        return;
    }
    saveSettings();
    ngrok_->setExecutable(ngrokExecutable_->text().trimmed());
    ngrok_->start(port_->value(), ngrokSecret, domain_->text().trimmed());
    syncUi();
}

void AgentDockWindow::finishHealthCheck(bool authenticated, bool externalPortOccupied)
{
    healthy_ = authenticated && core_.state() == QProcess::Running;
    portOccupied_ = externalPortOccupied && core_.state() == QProcess::NotRunning;
    pollPending_ = false;
    if (!healthy_ && ngrok_->isRunning()) {
        ngrok_->stop();
        log(QStringLiteral("AgentDock 核心或 Bearer 校验失败，已自动关闭公网隧道"));
    }
    syncUi();
}

void AgentDockWindow::refreshHealth()
{
    if (pollPending_) return;
    pollPending_ = true;
    const QUrl base(QStringLiteral("http://127.0.0.1:%1").arg(port_->value()));
    QNetworkRequest healthReq(base.resolved(QUrl(QStringLiteral("/healthz"))));
    healthReq.setTransferTimeout(1300);
    auto *healthReply = http_.get(healthReq);
    connect(healthReply, &QNetworkReply::finished, this, [this, healthReply, base] {
        const bool healthOK = healthReply->error() == QNetworkReply::NoError &&
            healthReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        healthReply->deleteLater();
        if (!healthOK || core_.state() != QProcess::Running) {
            finishHealthCheck(false, healthOK);
            return;
        }
        const QString secret = Credentials::readAgentDockToken();
        if (secret.isEmpty()) {
            finishHealthCheck(false);
            return;
        }
        // A successful public /healthz alone is insufficient for public
        // tunnelling. Check that this port really enforces AgentDock's Bearer
        // authentication, and that our stored token is accepted by /mcp.
        QNetworkRequest deniedReq(base.resolved(QUrl(QStringLiteral("/mcp"))));
        deniedReq.setRawHeader("Accept", "application/json, text/event-stream");
        deniedReq.setTransferTimeout(1300);
        auto *deniedReply = http_.get(deniedReq);
        connect(deniedReply, &QNetworkReply::finished, this,
                [this, deniedReply, base, secret] {
            const int deniedStatus = deniedReply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            deniedReply->deleteLater();
            if (deniedStatus != 401 || core_.state() != QProcess::Running) {
                finishHealthCheck(false);
                return;
            }
            QNetworkRequest authorizedReq(base.resolved(QUrl(QStringLiteral("/mcp"))));
            authorizedReq.setRawHeader("Accept", "application/json, text/event-stream");
            authorizedReq.setRawHeader("Authorization", QByteArray("Bearer ") + secret.toUtf8());
            authorizedReq.setTransferTimeout(1300);
            auto *authorizedReply = http_.get(authorizedReq);
            connect(authorizedReply, &QNetworkReply::finished, this,
                    [this, authorizedReply] {
                const int status = authorizedReply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
                authorizedReply->deleteLater();
                // AgentDock replies 405 for authenticated GET /mcp on the
                // current Streamable HTTP endpoint. Accept other non-auth
                // client responses for backward-compatible MCP versions.
                const bool authenticated = status == 200 || status == 202 ||
                    status == 204 || status == 400 || status == 405 || status == 406;
                finishHealthCheck(authenticated);
            });
        });
    });
}

void AgentDockWindow::saveSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("coreExecutable"), coreExecutable_->text().trimmed());
    settings.setValue(QStringLiteral("port"), port_->value());
    settings.setValue(QStringLiteral("ngrokDomain"), domain_->text().trimmed());
    settings.setValue(QStringLiteral("ngrokExecutable"), ngrokExecutable_->text().trimmed());
    QString error;
    if (!ngrokToken_->text().isEmpty() &&
        !Credentials::writeNgrokToken(ngrokToken_->text().trimmed(), &error))
        log(error);
}

void AgentDockWindow::chooseCore()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 AgentDock 核心"), QDir::homePath(),
                                                      QStringLiteral("AgentDock (agentdock.exe);;程序 (*.exe)"));
    if (!path.isEmpty()) {
        coreExecutable_->setText(path);
        saveSettings();
    }
}

void AgentDockWindow::copyText(const QString &value)
{
    QApplication::clipboard()->setText(value);
    statusBar()->showMessage(QStringLiteral("已复制到剪贴板"), 2500);
}

void AgentDockWindow::syncUi()
{
    const bool owned = core_.state() != QProcess::NotRunning;
    coreStatus_->setText(owned ? (healthy_ ? QStringLiteral("● 核心运行正常") : QStringLiteral("● 核心启动/检查中"))
                          : (portOccupied_ ? QStringLiteral("● 端口已被外部服务使用") : QStringLiteral("○ 核心已停止")));
    coreButton_->setText(owned ? QStringLiteral("停止核心") : QStringLiteral("启动核心"));
    coreButton_->setEnabled(owned || !portOccupied_);
    port_->setEnabled(!owned && !ngrok_->isRunning());
    localAddress_->setText(localUrl());
    tunnelButton_->setText(ngrok_->isRunning() ? QStringLiteral("停止 ngrok") : QStringLiteral("启动 ngrok"));
    tunnelButton_->setEnabled(ngrok_->isRunning() || (owned && healthy_));
    tunnelStatus_->setText(ngrok_->isRunning()
        ? (publicUrl_.isEmpty() ? QStringLiteral("● 正在建立隧道") : QStringLiteral("● HTTPS 已连接"))
        : QStringLiteral("○ ngrok 已停止"));
    publicAddress_->setText(publicUrl_.isEmpty() ? QStringLiteral("尚未分配公网地址")
                                                 : publicUrl_ + QStringLiteral("/mcp"));
    if (tray_) tray_->setToolTip(QStringLiteral("AgentDock Qt · %1 · %2")
        .arg(healthy_ ? QStringLiteral("在线") : QStringLiteral("离线"),
             ngrok_->isRunning() ? QStringLiteral("ngrok 已启动") : QStringLiteral("本地")));
    if (trayStart_) trayStart_->setText(owned ? QStringLiteral("停止核心") : QStringLiteral("启动核心"));
    if (trayTunnel_) {
        trayTunnel_->setText(ngrok_->isRunning() ? QStringLiteral("停止 ngrok") : QStringLiteral("启动 ngrok"));
        trayTunnel_->setEnabled(ngrok_->isRunning() || (owned && healthy_));
    }
}

void AgentDockWindow::showWindow()
{
    showNormal();
    activateWindow();
    raise();
}

void AgentDockWindow::quitApplication()
{
    quitting_ = true;
    close();
    qApp->quit();
}

void AgentDockWindow::closeEvent(QCloseEvent *event)
{
    if (!quitting_ && tray_ && tray_->isVisible()) {
        hide();
        tray_->showMessage(QStringLiteral("AgentDock Qt"), QStringLiteral("仍在后台托盘运行；右键图标可以彻底退出。"),
                           QSystemTrayIcon::Information, 2000);
        event->ignore();
        return;
    }
    event->accept();
}
