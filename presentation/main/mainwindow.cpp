#include <QMessageBox>
#include <QSysInfo>
#include <QNetworkInterface>
#include <QClipboard>
#include <QDesktopServices>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QActionGroup>
#include <QInputDialog>
#include <QMargins>
#include <QStyle>
#include <QStyleHints>
#include <QTimer>

#include "mainwindow.h"

#include "application/applicationlogger.h"
#include "application/applicationconstants.h"
#include "application/commandlineoptions.h"
#include "application/profilesettings.h"
#include "application/settingsmigrator.h"
#include "infrastructure/coreprocess/devicetrust.h"
#include "infrastructure/logging/applicationlogfile.h"
#include "infrastructure/storage/applicationpaths.h"
#include "infrastructure/update/updatechecker.h"
#include "presentation/coordinators/connectionuicontroller.h"
#include "presentation/dialogs/configurationguidedialog/configurationguidedialog.h"
#include "presentation/presentationhelpers.h"
#include "ui_mainwindow.h"

namespace
{
bool appendStyleSheet(const QString &path, QString &styleSheet)
{
    QFile styleFile(path);
    if (!styleFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning().noquote() << "Failed to load style resource:" << path;
        return false;
    }

    styleSheet.append(QString::fromUtf8(styleFile.readAll()));
    styleSheet.append('\n');
    return true;
}
}

MainWindow::MainWindow(
    ApplicationLogger *logger,
    ApplicationLogFile *logFile,
    QWidget *parent
) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    applicationLogger(logger),
    applicationLogFile(logFile)
{
    const QString overrideConfigPath =
        CommandLineOptions::value(QCoreApplication::arguments(), "--config-path");
    coordinator = new MainWindowCoordinator(this, overrideConfigPath, this);
    profileService = coordinator->profiles();
    currentProfileId = profileService->currentProfileId();
    settings = profileService->settings();

    const bool isFirstLaunch =
        !ProfileSettings::contains(*settings, ProfileSettings::ConfigVersion);
    upgradeSettings();

    ui->setupUi(this);
#ifdef Q_OS_MACOS
    const QMargins centralMargins = ui->centralWidget->contentsMargins();
    ui->centralWidget->setContentsMargins(centralMargins.left(), 12, centralMargins.right(), centralMargins.bottom());
#endif
    ui->logPlainTextEdit->setFont(
        QFontDatabase::systemFont(QFontDatabase::FixedFont)
    );
    QStyleHints *styleHints = QGuiApplication::styleHints();
    applyColorScheme(styleHints->colorScheme());
    connect(styleHints, &QStyleHints::colorSchemeChanged,
            this, &MainWindow::applyColorScheme);
    authenticationDialogs = coordinator->authenticationDialogs();
    connectionSession = coordinator->connection();
    systemProxySession = coordinator->systemProxy();
    updateChecker = coordinator->updates();
    connect(updateChecker, &UpdateChecker::versionInfoChanged, this,
            [this](const VersionInfo &) { updateVersionInfo(); });
    connect(updateChecker, &UpdateChecker::checkFailed, this,
            [this](UpdateComponent component, const QString &)
            {
                const QString componentName =
                    component == UpdateComponent::Ui ? QStringLiteral("UI") : QStringLiteral("core");
                ui->versionLabel->setToolTip(
                    ui->versionLabel->toolTip()
                    + "\nFailed to check for " + componentName + " updates"
                );
            });
    connect(updateChecker, &UpdateChecker::uiUpdateAvailable, this,
            [this](const QString &latestVersion)
            {
                QMessageBox msgBox(this);
                msgBox.setText("UI Update Available");
                msgBox.setInformativeText(
                    "A UI update is available: " + latestVersion + "\nOpen the GitHub releases page?"
                );
                msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
                msgBox.setDefaultButton(QMessageBox::Ok);

                if (msgBox.exec() == QMessageBox::Ok)
                {
                    QDesktopServices::openUrl(
                        QUrl(
                            "https://github.com/" +
                            ApplicationConstants::RepositoryName +
                            "/releases/latest"
                        )
                    );
                }
            });
    connect(applicationLogger, &ApplicationLogger::entryAdded, this,
            [this](const QString &entry)
            {
                ui->logPlainTextEdit->appendPlainText(entry);
            });
    connect(systemProxySession, &SystemProxySession::busyChanged, this,
            [this](bool busy)
            {
                ui->pushButton2->setEnabled(!busy);
                ui->disableProxyAction->setEnabled(!busy);
            });
    connect(systemProxySession, &SystemProxySession::enabledChanged, this,
            [this](bool enabled)
            {
                ui->pushButton2->setText(enabled ? "Clear System Proxy" : "Set System Proxy");
                if (!enabled && connectionSession != nullptr && !connectionSession->isActive())
                {
                    ui->pushButton2->hide();
                }
                updateProfileSummary();
                updateConnectionState(connectionSession->state());
            });
    setupTrayIcon();
    setupProfileMenu();

    setWindowIcon(QIcon(QPixmap(":/resource/icon.png").scaled(
        512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation
    )));

    ui->applicationNameLabel->setText(QApplication::applicationDisplayName());

    updateVersionInfo();
    updateConnectionState(connectionSession->state());

    connect(connectionSession, &ConnectionSession::stateChanged, this,
            &MainWindow::updateConnectionState);


    // File > Quit
    connect(ui->exitAction, &QAction::triggered, this, &MainWindow::gracefullyQuit);

    // File > Settings
    connect(ui->settingAction, &QAction::triggered, this,
            [&]()
            {
                settingWindow = new SettingWindow(this, settings, currentProfileId);
                settingWindow->show();
            });

    connect(ui->settingPushButton, &QPushButton::clicked,
            ui->settingAction, &QAction::trigger);
    connect(ui->guidePushButton, &QPushButton::clicked,
            ui->configurationGuideAction, &QAction::trigger);
    connect(ui->openLogPushButton, &QPushButton::clicked,
            ui->openLogAction, &QAction::trigger);

    // File > Open Log File
    connect(ui->openLogAction, &QAction::triggered, this,
            [this]()
            {
                const QString logFilePath = applicationLogFile->filePath();
                QFileInfo logFileInfo(logFilePath);

                if (logFileInfo.exists())
                {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(logFilePath));
                }
                else
                {
                    QMessageBox::warning(this, "Log File", "The log file could not be created.");
                }
            });

    // File > Clear System Proxy
    connect(ui->disableProxyAction, &QAction::triggered,
            [&]()
            {
                QMessageBox messageBox(this);
                messageBox.setWindowTitle("Clear System Proxy");
                messageBox.setText("Clear the system proxy?");

                messageBox.addButton(QMessageBox::Yes)->setText("Yes");
                messageBox.addButton(QMessageBox::No)->setText("No");
                messageBox.setDefaultButton(QMessageBox::Yes);

                if (messageBox.exec() == QMessageBox::No)
                {
                    return;
                }

                connect(systemProxySession, &SystemProxySession::operationFinished, this,
                        [this](bool enabled)
                        {
                            if (!enabled)
                            {
                                qInfo().noquote() << "System proxy settings cleared";
                            }
                        },
                        Qt::SingleShotConnection);
                connectionUiController->clearSystemProxy();
            });

    // File > Clear Login Cache
    connect(ui->clearClientDataAction, &QAction::triggered, this,
            [&]()
            {
                QMessageBox messageBox(this);
                messageBox.setWindowTitle("Clear Login Cache");
                messageBox.setText("Clear the login cache?");

                messageBox.addButton(QMessageBox::Yes)->setText("Yes");
                messageBox.addButton(QMessageBox::No)->setText("No");
                messageBox.setDefaultButton(QMessageBox::Yes);

                if (messageBox.exec() == QMessageBox::No)
                {
                    return;
                }

                ApplicationPaths::clearClientData(currentProfileId);
                qInfo().noquote() << "Login cache cleared";
            });

    // File > Trust This Device
    connect(ui->trustDeviceAction, &QAction::triggered, this,
            [&]()
            {
                try
                {
                    DeviceTrust::set(this,
                        ProfileSettings::read(*settings, ProfileSettings::Protocol),
                        ProfileSettings::read(*settings, ProfileSettings::ServerAddress),
                        ProfileSettings::read(*settings, ProfileSettings::ServerPort),
                        currentProfileId, true);
                    qInfo().noquote() << "Device trusted";
                    QMessageBox::information(this, "Success", "This device is now trusted.");
                }
                catch (const std::runtime_error &e)
                {
                    qWarning().noquote() << "Failed to trust this device: " + QString(e.what());
                    QMessageBox::critical(this, "Error", "Failed to trust this device:\n" + QString(e.what()));
                }
            });

    // File > Untrust This Device
    connect(ui->untrustDeviceAction, &QAction::triggered, this,
            [&]()
            {
                try
                {
                    DeviceTrust::set(this,
                        ProfileSettings::read(*settings, ProfileSettings::Protocol),
                        ProfileSettings::read(*settings, ProfileSettings::ServerAddress),
                        ProfileSettings::read(*settings, ProfileSettings::ServerPort),
                        currentProfileId, false);
                    qInfo().noquote() << "Device untrusted";
                    QMessageBox::information(this, "Success", "This device is no longer trusted.");
                }
                catch (const std::runtime_error &e)
                {
                    qWarning().noquote() << "Failed to untrust this device: " + QString(e.what());
                    QMessageBox::critical(this, "Error", "Failed to untrust this device:\n" + QString(e.what()));
                }
            });

    // Help > Check for Updates
    connect(ui->checkUpdateAction, &QAction::triggered,
            updateChecker, &UpdateChecker::check);

    // Help > Project Homepage
    connect(ui->projectAction, &QAction::triggered,
            [&]()
            {
                QDesktopServices::openUrl(QUrl(
                    "https://github.com/" + ApplicationConstants::RepositoryName
                ));
            });

    // Help > About
    connect(ui->aboutAction, &QAction::triggered,
            [&]()
            {
                PresentationHelpers::showAboutDialog(this);
            });

    // Copy the log
    connect(ui->copyLogPushButton, &QPushButton::clicked,
            [&]()
            {
                auto logText = ui->logPlainTextEdit->toPlainText();
                QApplication::clipboard()->setText(logText);
            }
    );

    // Clear the log
    connect(ui->clearLogPushButton, &QPushButton::clicked,
            [&]()
            {
                ui->logPlainTextEdit->clear();
            }
    );

    clearLog();
    connectionUiController = new ConnectionUiController(
        this,
        ui->pushButton1,
        ui->pushButton2,
        trayConnectAction,
        connectionSession,
        systemProxySession,
        authenticationDialogs,
        applicationLogger,
        [this]() { return settings; },
        [this]() { return currentProfileId; },
        [this](
            const QString &title,
            const QString &content,
            QSystemTrayIcon::MessageIcon icon
        )
        {
            showNotification(title, content, icon);
        },
        this
    );

    bool shouldConnect =
        ProfileSettings::read(*settings, ProfileSettings::ConnectAfterStart);
    if (qApp->arguments().contains("--connect"))
    {
        shouldConnect = true;
    }
    if (shouldConnect)
    {
        ui->pushButton1->click();
    }

    if (ProfileSettings::read(*settings, ProfileSettings::CheckUpdateAfterStart))
    {
        updateChecker->check();
    }
    else
    {
        updateChecker->markDisabled();
    }

    if (!profileService->silentStartEnabled())
    {
        show();
        if (isFirstLaunch)
        {
            QTimer::singleShot(
                0,
                this,
                &MainWindow::promptFirstLaunchGuide
            );
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (connectionSession != nullptr && connectionSession->isActive())
    {
        event->ignore();
        hide();
        showNotification("EZ4Connect", "Minimized to the system tray. Click the icon to restore the window.", QSystemTrayIcon::MessageIcon::Information);
    }
    else
    {
        event->accept();
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange)
    {
        QWindowStateChangeEvent *stateChangeEvent = static_cast<QWindowStateChangeEvent *>(event);
        if (windowState().testFlag(Qt::WindowMinimized) == true && !(stateChangeEvent->oldState() & Qt::WindowMinimized))
        {
            event->ignore();
            hide();
            showNotification("EZ4Connect", "Minimized to the system tray. Click the icon to restore the window.", QSystemTrayIcon::MessageIcon::Information);
        }
    }
    else
    {
        event->accept();
    }
}

void MainWindow::clearLog()
{
    ui->logPlainTextEdit->clear();
    ui->logPlainTextEdit->appendPlainText(
        "Welcome to " + QApplication::applicationDisplayName() + "\n"
        "Version: " + QApplication::applicationVersion() + "\n"
        "System: " + QSysInfo::prettyProductName() + "\n"
        "Profile: " + (currentProfileId.isEmpty() ? "Default" : currentProfileId) + "\n"
        "Profile path: " + settings->fileName() + "\n");
}

void MainWindow::resetZjuConnectUi()
{
    ui->pushButton1->setText("Connect");
    trayConnectAction->setText("Connect");
    ui->pushButton2->setText("Set System Proxy");
    ui->pushButton2->hide();
    updateConnectionState(ConnectionState::Disconnected);
    updateProfileSummary();
}

void MainWindow::applyColorScheme(Qt::ColorScheme scheme)
{
    const QString themePath = scheme == Qt::ColorScheme::Dark
        ? QStringLiteral(":/resource/mainwindow-dark.qss")
        : QStringLiteral(":/resource/mainwindow-light.qss");

    QString styleSheet;
    if (!appendStyleSheet(QStringLiteral(":/resource/mainwindow.qss"), styleSheet)
        || !appendStyleSheet(themePath, styleSheet))
    {
        return;
    }

    setStyleSheet(styleSheet);
}

void MainWindow::updateConnectionState(ConnectionState state)
{
    QString propertyValue;
    QString title;
    QString detail;

    switch (state)
    {
    case ConnectionState::Disconnected:
        propertyValue = "disconnected";
        title = "Not Connected";
        detail = "Connect to access network resources.";
        break;
    case ConnectionState::Starting:
        propertyValue = "starting";
        title = "Connecting";
        detail = "Starting the core and establishing a secure tunnel.";
        break;
    case ConnectionState::Running:
        propertyValue = "running";
        title = "Connected";
        detail = systemProxySession->isEnabled()
            ? "The VPN tunnel and the system proxy are both active."
            : "The VPN tunnel is running. Enable the system proxy if you need it.";
        break;
    case ConnectionState::Stopping:
        propertyValue = "stopping";
        title = "Disconnecting";
        detail = "Closing the current connection safely.";
        break;
    case ConnectionState::Reconnecting:
        propertyValue = "reconnecting";
        title = "Reconnecting";
        detail = "Connection lost. Retrying according to the reconnect settings.";
        break;
    case ConnectionState::Interrupted:
        propertyValue = "failed";
        title = "Disconnected";
        detail = "The VPN core exited unexpectedly. See the log on the right.";
        break;
    case ConnectionState::Failed:
        propertyValue = "failed";
        title = "Connection Failed";
        detail = "See the log on the right and check your network and account settings.";
        break;
    }

    ui->statusTitleLabel->setText(title);
    ui->statusDetailLabel->setText(detail);

    const QList<QWidget *> styledWidgets{
        ui->statusIndicator,
        ui->pushButton1
    };
    for (QWidget *widget : styledWidgets)
    {
        widget->setProperty("connectionState", propertyValue);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
}

void MainWindow::updateProfileSummary()
{
    const QString profileName = currentProfileId.isEmpty()
        ? QStringLiteral("Default")
        : currentProfileId;
    const QString protocolSetting = ProfileSettings::read(*settings, ProfileSettings::Protocol);
    const QString protocol = protocolSetting.compare(
        "atrust",
        Qt::CaseInsensitive
    ) == 0 ? QStringLiteral("aTrust") : QStringLiteral("EasyConnect");
    const QString server = ProfileSettings::read(*settings, ProfileSettings::ServerAddress).trimmed();

    QStringList details{protocol};
    details.append(server.isEmpty() ? QStringLiteral("No server configured") : server);

    ui->profileNameLabel->setText(profileName);
    ui->profileDetailLabel->setText(details.join(QStringLiteral(" · ")));
}

void MainWindow::setupTrayIcon()
{
    // System tray
    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(
        QIcon(QPixmap(":/resource/icon.png").scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    trayIcon->setToolTip(QApplication::applicationName());
    connect(trayIcon, &QSystemTrayIcon::activated, this, [&](QSystemTrayIcon::ActivationReason reason) {
        switch (reason)
        {
        case QSystemTrayIcon::Context:
            break;
        default:
            if (isHidden())
            {
                show();
            }
            setWindowState(Qt::WindowState::WindowActive);
            setFocus();
            break;
        }
    });

    trayConnectAction = new QAction("Connect", this);
    trayProfileMenu = new QMenu("Profiles", this);
    trayShowAction = new QAction("Show Main Window", this);
    trayCloseAction = new QAction("Quit " + QApplication::applicationName(), this);
    trayCloseAction->setMenuRole(QAction::NoRole);
    trayMenu = new QMenu(this);
    trayMenu->addAction(trayConnectAction);
    trayMenu->addSeparator();
    trayMenu->addMenu(trayProfileMenu);
    trayMenu->addSeparator();
    trayMenu->addAction(trayShowAction);
    trayMenu->addAction(trayCloseAction);
    connect(trayConnectAction, &QAction::triggered, this, [&]() { ui->pushButton1->click(); });
    connect(trayShowAction, &QAction::triggered, this, [&]() {
        show();
        setWindowState(Qt::WindowState::WindowActive);
        setFocus();
    });
    connect(trayCloseAction, &QAction::triggered, this, &MainWindow::gracefullyQuit);
    trayIcon->setContextMenu(trayMenu);
    trayIcon->show();
}

void MainWindow::setupProfileMenu()
{
    // Must be called after setupTrayIcon, which creates trayProfileMenu
    ui->profileMenu->addSeparator();
    newProfileAction = ui->profileMenu->addAction("New Profile");
    renameProfileAction = ui->profileMenu->addAction("Rename Current Profile");
    deleteProfileAction = ui->profileMenu->addAction("Delete Current Profile");
    ui->profileMenu->addSeparator();

    connect(
        ui->configurationGuideAction,
        &QAction::triggered,
        this,
        &MainWindow::openConfigurationGuide
    );
    connect(newProfileAction, &QAction::triggered, this, &MainWindow::createProfile);
    connect(renameProfileAction, &QAction::triggered, this, &MainWindow::renameCurrentProfile);
    connect(deleteProfileAction, &QAction::triggered, this, &MainWindow::deleteCurrentProfile);

    refreshProfileMenu();
}

void MainWindow::refreshProfileMenu()
{
    const QList<QAction *> actions = ui->profileMenu->actions();
    bool remove = false;
    for (QAction *action : actions)
    {
        if (action == ui->configurationGuideAction
            || action == newProfileAction
            || action == renameProfileAction
            || action == deleteProfileAction)
        {
            continue;
        }
        if (action->isSeparator())
        {
            remove = true;
            continue;
        }
        if (remove)
        {
            ui->profileMenu->removeAction(action);
            delete action;
        }
    }

    QActionGroup *switchGroup = new QActionGroup(ui->profileMenu);
    QActionGroup *traySwitchGroup = nullptr;

    if (trayProfileMenu != nullptr)
    {
        trayProfileMenu->clear();
        traySwitchGroup = new QActionGroup(trayProfileMenu);
        traySwitchGroup->setExclusive(true);
    }

    switchGroup->setExclusive(true);
    QAction *action = ui->profileMenu->addAction("Default");
    action->setCheckable(true);
    action->setChecked(currentProfileId.isEmpty());
    switchGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this]()
    {
        switchProfile("");
    });
    if (trayProfileMenu != nullptr)
    {
        QAction *trayAction = trayProfileMenu->addAction("Default");
        trayAction->setCheckable(true);
        trayAction->setChecked(currentProfileId.isEmpty());
        traySwitchGroup->addAction(trayAction);
        connect(trayAction, &QAction::triggered, this, [this]()
        {
            switchProfile("");
        });
    }

    for (const QString &profileId : profileService->profiles())
    {
        QAction *action = ui->profileMenu->addAction(profileId);
        action->setCheckable(true);
        action->setChecked(
            !profileService->usesOverrideConfiguration()
            && profileId == currentProfileId
        );
        switchGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, profileId]()
        {
            switchProfile(profileId);
        });

        if (trayProfileMenu != nullptr)
        {
            QAction *trayAction = trayProfileMenu->addAction(profileId);
            trayAction->setCheckable(true);
            trayAction->setChecked(
                !profileService->usesOverrideConfiguration()
                && profileId == currentProfileId
            );
            traySwitchGroup->addAction(trayAction);
            connect(trayAction, &QAction::triggered, this, [this, profileId]()
            {
                switchProfile(profileId);
            });
        }
    }

    const bool currentProfileIsManaged =
        !currentProfileId.isEmpty()
        && !profileService->usesOverrideConfiguration();
    renameProfileAction->setEnabled(currentProfileIsManaged);
    deleteProfileAction->setEnabled(currentProfileIsManaged);
}

bool MainWindow::switchProfile(const QString &profileId)
{
    if (!profileService->usesOverrideConfiguration()
        && profileId == currentProfileId)
    {
        return true;
    }

    if (connectionSession != nullptr && connectionSession->isActive())
    {
        QMessageBox::warning(this, "Switch Failed", "Disconnect the VPN before switching profiles.");
        refreshProfileMenu();
        return false;
    }

    if (settingWindow != nullptr)
    {
        settingWindow->close();
    }

    if (!profileService->switchTo(profileId))
    {
        refreshProfileMenu();
        return false;
    }
    settings = profileService->settings();
    authenticationDialogs->setSettings(settings);
    currentProfileId = profileService->currentProfileId();

    upgradeSettings();
    updateVersionInfo();
    resetZjuConnectUi();
    clearLog();
    refreshProfileMenu();

    qInfo().noquote() << "Switched to profile: " + currentProfileId;
    return true;
}

void MainWindow::createProfile()
{
    if (connectionSession != nullptr && connectionSession->isActive())
    {
        QMessageBox::warning(this, "Cannot Create Profile", "Disconnect the VPN before creating a profile.");
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        "New Profile",
        "Enter a profile name:\n(letters, digits, underscores and hyphens only)",
        QLineEdit::Normal,
        "",
        &ok
    );
    if (!ok)
    {
        return;
    }

    if (settingWindow != nullptr)
    {
        settingWindow->close();
    }

    const QString newProfileId = profileService->createAndSwitch(name);
    if (newProfileId.isEmpty())
    {
        QMessageBox::critical(this, "Create Failed", "Could not create the profile.");
        return;
    }
    // A profile deleted by an older version may have left its login cache
    // behind under this name.
    if (!ApplicationPaths::removeProfileData(newProfileId))
    {
        qWarning().noquote() << "Could not clear old login data for profile: " + newProfileId;
        QMessageBox::warning(
            this,
            "Old Login Data",
            "Login data left under this profile name by an earlier profile could not be cleared.\n"
            "Use File → Clear Login Cache before connecting."
        );
    }

    settings = profileService->settings();
    currentProfileId = profileService->currentProfileId();
    authenticationDialogs->setSettings(settings);
    // The new profile starts as a copy of the previous one, so it still
    // points at that profile's saved passwords.
    ProfileSettings::detachSecrets(*settings);
    upgradeSettings();
    updateVersionInfo();
    resetZjuConnectUi();
    updateVersionInfo();
    clearLog();
    refreshProfileMenu();

    promptConfigurationGuide(
        "Profile Created",
        "Profile \"" + newProfileId + "\" was created. Finish setting it up with the Setup Guide now?"
    );
}

void MainWindow::openConfigurationGuide()
{
    if (connectionSession != nullptr && connectionSession->isActive())
    {
        QMessageBox::warning(
            this,
            "Cannot Modify Profile",
            "Disconnect the VPN before using the Setup Guide."
        );
        return;
    }

    if (settingWindow != nullptr)
    {
        settingWindow->close();
    }

    const QString oldServerAddress =
        ProfileSettings::read(*settings, ProfileSettings::ServerAddress);
    const int oldServerPort =
        ProfileSettings::read(*settings, ProfileSettings::ServerPort);
    const QString oldProtocol =
        ProfileSettings::read(*settings, ProfileSettings::Protocol);
    const QString oldAuthType =
        ProfileSettings::read(*settings, ProfileSettings::AuthType);
    const QString oldEasyConnectAuthType =
        ProfileSettings::easyConnectAuthType(*settings);
    const QString oldLoginDomain =
        ProfileSettings::read(*settings, ProfileSettings::LoginDomain);
    const QString oldLoginUrl =
        ProfileSettings::read(*settings, ProfileSettings::LoginURL);

    ConfigurationGuideDialog guide(this, settings);
    if (guide.exec() != QDialog::Accepted)
    {
        return;
    }
    guide.applyTo(*settings);

    const bool authenticationSettingsChanged =
        oldServerAddress != ProfileSettings::read(*settings, ProfileSettings::ServerAddress)
        || oldServerPort != ProfileSettings::read(*settings, ProfileSettings::ServerPort)
        || oldProtocol != ProfileSettings::read(*settings, ProfileSettings::Protocol)
        || oldAuthType != ProfileSettings::read(*settings, ProfileSettings::AuthType)
        || oldEasyConnectAuthType
            != ProfileSettings::easyConnectAuthType(*settings)
        || oldLoginDomain != ProfileSettings::read(*settings, ProfileSettings::LoginDomain)
        || oldLoginUrl != ProfileSettings::read(*settings, ProfileSettings::LoginURL);
    if (authenticationSettingsChanged)
    {
        ApplicationPaths::clearClientData(currentProfileId);
    }

    resetZjuConnectUi();
    clearLog();
    qInfo().noquote() << "Current profile updated via the Setup Guide";
}

void MainWindow::promptFirstLaunchGuide()
{
    promptConfigurationGuide(
        "Welcome to EZ4Connect",
        "This looks like the first launch. Set up a VPN server now?"
    );
}

void MainWindow::promptConfigurationGuide(
    const QString &windowTitle,
    const QString &text
)
{
    QMessageBox messageBox(this);
    messageBox.setWindowTitle(windowTitle);
    messageBox.setText(text);
    messageBox.setInformativeText(
        "The Setup Guide helps you choose a protocol and enter the server address, authentication method and credentials."
    );

    QPushButton *startButton = messageBox.addButton(
        "Open Setup Guide",
        QMessageBox::AcceptRole
    );
    messageBox.addButton("Later", QMessageBox::RejectRole);
    messageBox.setDefaultButton(startButton);
    messageBox.exec();

    if (messageBox.clickedButton() == startButton)
    {
        openConfigurationGuide();
    }
}

void MainWindow::renameCurrentProfile()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, "Rename Profile", "Enter a new profile name:\n(letters, digits, underscores and hyphens only)", QLineEdit::Normal, currentProfileId, &ok);
    if (!ok)
    {
        return;
    }

    const QString normalizedName = profileService->normalizeProfileId(name);
    if (normalizedName.isEmpty())
    {
        QMessageBox::warning(this, "Rename Failed", "The profile name cannot be empty.");
        return;
    }
    if (normalizedName == currentProfileId)
    {
        return;
    }
    if (connectionSession != nullptr && connectionSession->isActive())
    {
        QMessageBox::warning(this, "Rename Failed", "Disconnect the VPN before renaming the profile.");
        return;
    }

    if (settingWindow != nullptr)
    {
        settingWindow->close();
    }
    const QString previousProfileId = currentProfileId;
    if (!profileService->renameCurrent(normalizedName))
    {
        QMessageBox::warning(this, "Rename Failed", "A profile with that name already exists, or this profile cannot be renamed.");
        return;
    }
    if (!ApplicationPaths::moveProfileData(previousProfileId, normalizedName))
    {
        qWarning().noquote() << "Could not move login data to the renamed profile: " + normalizedName;
        QMessageBox::warning(
            this,
            "Login Data Not Moved",
            "The profile was renamed, but its login data could not be moved.\n"
            "You may have to log in and trust this device again."
        );
    }

    currentProfileId = profileService->currentProfileId();
    settings = profileService->settings();
    authenticationDialogs->setSettings(settings);
    updateVersionInfo();
    refreshProfileMenu();
    clearLog();
    qInfo().noquote() << "Current profile renamed to: " + currentProfileId;
}

void MainWindow::deleteCurrentProfile()
{
    if (currentProfileId.isEmpty())
    {
        QMessageBox::warning(this, "Delete Failed", "The default profile cannot be deleted.");
        return;
    }

    QMessageBox messageBox(this);
    messageBox.setWindowTitle("Delete Profile");
    messageBox.setText("Delete the current profile \"" + currentProfileId + "\"?");
    messageBox.addButton(QMessageBox::Yes)->setText("Yes");
    messageBox.addButton(QMessageBox::No)->setText("No");
    messageBox.setDefaultButton(QMessageBox::No);
    if (messageBox.exec() != QMessageBox::Yes)
    {
        return;
    }

    const QString removedProfileId = currentProfileId;
    const QString removedSecretId =
        ProfileSettings::read(*settings, ProfileSettings::SecretId);
    if (connectionSession != nullptr && connectionSession->isActive())
    {
        QMessageBox::warning(this, "Delete Failed", "Disconnect the VPN before deleting the profile.");
        return;
    }
    if (settingWindow != nullptr)
    {
        settingWindow->close();
    }
    if (!profileService->removeCurrentAndSwitchToDefault())
    {
        QMessageBox::warning(this, "Delete Failed", "Could not delete the profile file.");
        return;
    }
    if (!ProfileSettings::forgetSecrets(removedSecretId))
    {
        QMessageBox::warning(
            this,
            "Saved Passwords Not Removed",
            "The profile was deleted, but its saved passwords could not be removed from the "
            "system credential store. Remove the EZ4Connect entries there by hand."
        );
    }
    if (!ApplicationPaths::removeProfileData(removedProfileId))
    {
        qWarning().noquote() << "Could not remove login data of the deleted profile: " + removedProfileId;
        QMessageBox::warning(
            this,
            "Login Data Not Removed",
            "The profile was deleted, but its login data could not be removed."
        );
    }

    currentProfileId = profileService->currentProfileId();
    settings = profileService->settings();
    authenticationDialogs->setSettings(settings);
    upgradeSettings();
    updateVersionInfo();
    resetZjuConnectUi();
    clearLog();
    refreshProfileMenu();
    qInfo().noquote() << "Deleted profile: " + removedProfileId;
}

void MainWindow::upgradeSettings()
{
    const SettingsMigrationAction action = SettingsMigrator::prepare(*settings);
    if (action == SettingsMigrationAction::MigrateAutoStart)
    {
        profileService->migrateAutoStartSetting(
            ProfileSettings::read(*settings, ProfileSettings::LegacyAutoStart)
        );
    }
    else if (action == SettingsMigrationAction::RecommendReset)
    {
        QMessageBox msgBox;
        msgBox.setText("Configuration Format Updated");
        msgBox.setInformativeText("Restoring the default settings is recommended to use the improved configuration.\n\nRestore the defaults?");
        msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Cancel);

        const bool reset = msgBox.exec() == QMessageBox::Ok;
        SettingsMigrator::finish(*settings, reset);
        ProfileSettings::migrateSecrets(*settings);
        if (reset)
        {
            QMessageBox::information(this, "Done", "Default settings restored.");
        }
        return;
    }
    SettingsMigrator::finish(*settings, false);
    ProfileSettings::migrateSecrets(*settings);
}

void MainWindow::updateVersionInfo()
{
    const VersionInfo &versionInfo = updateChecker->versionInfo();
    ui->versionLabel->setText("Version " + versionInfo.uiVersion);
    ui->versionLabel->setToolTip(
        "UI version: " + versionInfo.uiVersion + " (latest: " + versionInfo.uiLatest + ")\n"
        "Core version: " + versionInfo.coreVersion + " (latest: " + versionInfo.coreLatest + ")"
    );
    updateProfileSummary();
}

void MainWindow::showNotification(const QString &title, const QString &content, QSystemTrayIcon::MessageIcon icon)
{
    disconnect(trayIcon, &QSystemTrayIcon::messageClicked, nullptr, nullptr);
    trayIcon->showMessage(
        title,
        content,
        icon,
        10000
    );

    connect(trayIcon, &QSystemTrayIcon::messageClicked, this, [&]()
    {
        disconnect(trayIcon, &QSystemTrayIcon::messageClicked, nullptr, nullptr);

        show();
        setWindowState(Qt::WindowState::WindowActive);
    });
}

void MainWindow::cleanUpWhenQuit()
{
    // Save the settings
    if (ProfileSettings::read(*settings, ProfileSettings::ConfigVersion) <=
        ApplicationConstants::ConfigVersion)
    {
        ProfileSettings::write(
            *settings,
            ProfileSettings::ConfigVersion,
            ApplicationConstants::ConfigVersion
        );
    }
    settings->sync();

    // Clear the system proxy
    coordinator->prepareForShutdown();
}

void MainWindow::gracefullyQuit()
{
    if (connectionSession != nullptr && connectionSession->isActive())
    {
        connect(connectionSession, &ConnectionSession::finished, qApp,
                [](ZJU_ERROR) { QApplication::quit(); });
        ui->pushButton1->click();
    }
    else
    {
        qApp->quit();
    }
}

MainWindow::~MainWindow()
{
    delete coordinator;
    coordinator = nullptr;

    QObject::disconnect(applicationLogger, nullptr, this, nullptr);
    delete ui;
    ui = nullptr;
}
