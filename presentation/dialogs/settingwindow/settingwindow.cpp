#include <QFileInfo>
#include <QFileDialog>
#include <QDesktopServices>
#include <QMessageBox>
#include <QHostAddress>
#include <QStandardPaths>

#include "settingwindow.h"
#include "ui_settingwindow.h"
#include "application/applicationconstants.h"
#include "application/defaultsettings.h"
#include "application/profilesettings.h"
#include "infrastructure/platform/autostart.h"
#include "infrastructure/storage/applicationpaths.h"
#include "presentation/presentationhelpers.h"
#include "infrastructure/settings/profilemanager.h"

SettingWindow::SettingWindow(QWidget *parent, QSettings *inputSettings, const QString &profileId) :
    QDialog(parent),
    ui(new Ui::SettingWindow)
{
    ui->setupUi(this);

    this->settings = inputSettings;
    this->profileId = profileId;

    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose);

    loadSettings();

    connect(ui->openLogDirectoryPushButton, &QPushButton::clicked, this, [this]()
    {
        if (!QDesktopServices::openUrl(
                QUrl::fromLocalFile(ApplicationPaths::logDirectory())
            ))
        {
            QMessageBox::warning(this, "Log Directory", "Could not open the log directory.");
        }
    });

    connect(ui->portForwardingPushButton, &QPushButton::clicked,
            [&]()
            {
                extraSettingWindow = new ExtraSettingWindow(this);
                extraSettingWindow->setup(tcpPortForwarding, udpPortForwarding, customDNS, customProxyDomain, extraArguments);

                connect(extraSettingWindow, &ExtraSettingWindow::applied, this,
				[&](const QString& tcpForwarding, const QString& udpForwarding, const QString& customDNS_, const QString& customProxyDomain_, const QString& extraArg)
                    {
                        tcpPortForwarding = tcpForwarding;
                        udpPortForwarding = udpForwarding;
						customDNS = customDNS_;
						customProxyDomain = customProxyDomain_;
						extraArguments = extraArg;
                    });

                extraSettingWindow->show();
            });

    connect(ui->buttonBox->button(QDialogButtonBox::Ok), &QPushButton::clicked, [&]() {
        if (shouldCheckCredential() && !PresentationHelpers::confirmCredentials(
                ui->usernameLineEdit->text(),
                ui->passwordLineEdit->text()
            ))
            return;
        if (isAuthSettingChanged())
            ApplicationPaths::clearClientData(this->profileId);
        applySettings();
        accept();
    });

    connect(ui->buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, [&]() {
        if (shouldCheckCredential() && !PresentationHelpers::confirmCredentials(
                ui->usernameLineEdit->text(),
                ui->passwordLineEdit->text()
            ))
            return;
        if (isAuthSettingChanged())
            ApplicationPaths::clearClientData(this->profileId);
        applySettings();
        loadSettings();
    });

    connect(ui->resetDefaultPushButton, &QPushButton::clicked,
        [&]()
        {
            int status = QMessageBox::warning(this, "Warning", "This will reset all settings. Continue?", QMessageBox::Ok, QMessageBox::Cancel);
            if (status == QMessageBox::Ok)
            {
                const bool secretsRemoved = ProfileSettings::forgetSecrets(*settings);
                settings->clear();
				DefaultSettings::reset(*settings);
				settings->sync();
                loadSettings();
                if (!secretsRemoved)
                {
                    QMessageBox::warning(
                        this,
                        "Saved Passwords Not Removed",
                        "The settings were reset, but the saved passwords could not be removed from "
                        "the system credential store. Remove the EZ4Connect entries there by hand."
                    );
                }
            }
        });

    connect(ui->importPushButton, &QPushButton::clicked,
            [&]()
            {
                QString filename = QFileDialog::getOpenFileName(this, "Choose a Configuration File",
                    QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
                    "Config Ini(*.ini);;All Files(*.*)");
                if (filename.isEmpty()) {
                    QMessageBox::critical(this, "Error", "No configuration file selected. Nothing was changed.");
                    return;
                }
                QSettings newSettings(filename, QSettings::IniFormat);
                for (const auto& key : newSettings.allKeys()) {
                    // The identifier names another profile's saved secrets.
                    if (key == ProfileSettings::SecretId.name)
                        continue;
                    settings->setValue(key, newSettings.value(key));
                }
                // A file without a secret means "none saved". Keeping this
                // profile's old one would send it to the imported server.
                for (const ProfileSettings::SecretKey *secret : {&ProfileSettings::Password,
                                                                 &ProfileSettings::TOTPSecret,
                                                                 &ProfileSettings::CertPassword})
                {
                    if (!newSettings.contains(secret->name))
                        ProfileSettings::write(*settings, *secret, QString());
                }
                ProfileSettings::migrateSecrets(*settings);
                settings->sync();
                loadSettings();
            });

    connect(ui->exportPushButton, &QPushButton::clicked,
            [&]()
            {
                QString filename = QFileDialog::getSaveFileName(this, "Choose Where to Save",
                    QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
                    "Config Ini(*.ini);;All Files(*.*)");
                if (filename.isEmpty())
                {
                    QMessageBox::critical(this, "Error", "No save location selected.");
                    return;
                }
                settings->sync();
                if (QFile::exists(filename))
                    QFile::remove(filename);
                QFile::copy(settings->fileName(), filename);
                if (ProfileSettings::usesSecretStore())
                {
                    // The identifier would let whoever uses the exported file
                    // read and overwrite this profile's saved passwords, and
                    // a password the store refused is still in the file.
                    QSettings exported(filename, QSettings::IniFormat);
                    ProfileSettings::stripSecrets(exported);
                    exported.sync();
                    QMessageBox::information(
                        this,
                        "Passwords Not Exported",
                        "Saved passwords are kept in the system credential store and are not "
                        "part of the exported file. Enter them again after importing it."
                    );
                }
            });

    connect(ui->passwordVisibleCheckBox, &QCheckBox::checkStateChanged,
        [&](Qt::CheckState state)
        {
            ui->passwordLineEdit->setEchoMode(state == Qt::Checked ? QLineEdit::Normal : QLineEdit::Password);
        });

    connect(ui->totpSecretVisibleCheckBox, &QCheckBox::checkStateChanged,
        [&](Qt::CheckState state)
        {
            ui->totpSecretLineEdit->setEchoMode(state == Qt::Checked ? QLineEdit::Normal : QLineEdit::Password);
        });

    connect(ui->certPasswordVisibleCheckBox, &QCheckBox::checkStateChanged,
        [&](Qt::CheckState state)
        {
            ui->certPasswordLineEdit->setEchoMode(state == Qt::Checked ? QLineEdit::Normal : QLineEdit::Password);
        });

    connect(ui->certFileBrowseButton, &QPushButton::clicked,
        [&]()
        {
            QString filename = QFileDialog::getOpenFileName(this, "Choose a Certificate File",
                QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
                "P12 Certificate(*.p12 *.pfx);;All Files(*.*)");
            if (!filename.isEmpty())
            {
                ui->certFileLineEdit->setText(filename);
            }
        });

    connect(ui->authSelectPushButton, &QPushButton::clicked, this, [&]() {
        authInfoWindow = new AuthInfoWindow(this);
        connect(authInfoWindow, &AuthInfoWindow::finishAuthInfo, this,
                [&](const QString &authType, const QString &loginDomain, const QString &loginUrl) {
                    if (authType == "auth/cas")
                    {
                        ui->casRadioButton->setChecked(true);
                        ui->loginUrlLineEdit->setText(loginUrl);
                    }
                    else if (authType == "auth/httpsOauth2")
                    {
                        ui->oauth2RadioButton->setChecked(true);
                        ui->loginUrlLineEdit->setText(loginUrl);
                    }
                    else if (authType == "auth/smsCheckCode")
                    {
                        ui->smsCheckCodeRadioButton->setChecked(true);
                    }
                    else
                    {
                        ui->pswRadioButton->setChecked(true);
                    }
                    ui->loginDomainLineEdit->setText(loginDomain);
        });
        authInfoWindow->fetchAuthInfo(ui->serverAddressLineEdit->text(), ui->serverPortSpinBox->value());
        authInfoWindow->show();
    });
}

SettingWindow::~SettingWindow()
{
    delete ui;
}

bool SettingWindow::shouldCheckCredential()
{
    if (ui->atrustRadioButton->isChecked())
        return ui->pswRadioButton->isChecked();
    else
        return !ui->certFileLineEdit->text().isEmpty();
}

void SettingWindow::loadSettings()
{
    ui->configVersionLabel->setText(
        "Profile config version: " + QString::number(ProfileSettings::read(*settings, ProfileSettings::ConfigVersion)) +
        "\nApp config version: " +
        QString::number(ApplicationConstants::ConfigVersion)
    );
    ui->usernameLineEdit->setText(ProfileSettings::read(*settings, ProfileSettings::Username));
    loadedPassword = ProfileSettings::read(*settings, ProfileSettings::Password);
    loadedTotpSecret = ProfileSettings::read(*settings, ProfileSettings::TOTPSecret);
    loadedCertPassword = ProfileSettings::read(*settings, ProfileSettings::CertPassword);
    ui->passwordLineEdit->setText(loadedPassword);
    ui->totpSecretLineEdit->setText(loadedTotpSecret);
    ui->certFileLineEdit->setText(ProfileSettings::read(*settings, ProfileSettings::CertFile));
    ui->certPasswordLineEdit->setText(loadedCertPassword);
    ui->credentialsAsArgumentsCheckBox->setChecked(
        ProfileSettings::read(*settings, ProfileSettings::CredentialsAsArguments)
    );

    ProfileManager profileManager;
    ui->autoStartCheckBox->setChecked(profileManager.autoStartEnabled());
    ui->silentStartCheckBox->setChecked(profileManager.silentStartEnabled());
    ui->connectAfterStartCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::ConnectAfterStart));
    ui->checkUpdateAfterStartCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::CheckUpdateAfterStart));
    ui->autoSetProxyCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::AutoSetProxy));
    ui->reconnectTimeSpinBox->setValue(ProfileSettings::read(*settings, ProfileSettings::ReconnectTime));
    ui->autoReconnectCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::AutoReconnect));
	ui->systemProxyBypassLineEdit->setText(ProfileSettings::read(*settings, ProfileSettings::SystemProxyBypass));
    ui->suppressProxyOverrideWarningCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::SuppressProxyOverrideWarning));


    ui->serverAddressLineEdit->setText(ProfileSettings::read(*settings, ProfileSettings::ServerAddress));
    ui->serverPortSpinBox->setValue(ProfileSettings::read(*settings, ProfileSettings::ServerPort));
    ui->dnsLineEdit->setText(ProfileSettings::read(*settings, ProfileSettings::DNS));
    ui->dnsAutoCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::DNSAuto));
    ui->secondaryDnsLineEdit->setText(ProfileSettings::read(*settings, ProfileSettings::SecondaryDNS));
    ui->localDnsServerLineEdit->setText(
        ProfileSettings::read(*settings, ProfileSettings::LocalDNSServer)
    );
    ui->dnsServerBindLineEdit->setText(
        ProfileSettings::read(*settings, ProfileSettings::DNSServerBind)
    );
    ui->dnsTTLSpinBox->setValue(ProfileSettings::read(*settings, ProfileSettings::DNSTTL));
    ui->socks5PortSpinBox->setValue(ProfileSettings::read(*settings, ProfileSettings::SOCKS5Port));
    ui->httpPortSpinBox->setValue(ProfileSettings::read(*settings, ProfileSettings::HTTPPort));
    ui->shadowsocksUrlLineEdit->setText(ProfileSettings::read(*settings, ProfileSettings::ShadowsocksURL));
    ui->dialDirectProxyLineEdit->setText(ProfileSettings::read(*settings, ProfileSettings::DialDirectProxy));
    ui->updateBestNodesIntervalSpinBox->setValue(
        ProfileSettings::read(*settings, ProfileSettings::UpdateBestNodesInterval));

    if (ProfileSettings::read(*settings, ProfileSettings::Protocol) == "atrust")
        ui->atrustRadioButton->setChecked(true);
    else
        ui->easyconnectRadioButton->setChecked(true);
    ui->loginDomainLineEdit->setText(ProfileSettings::read(*settings, ProfileSettings::LoginDomain));
    auto authType = ProfileSettings::read(*settings, ProfileSettings::AuthType);
    if (authType == "smsCheckCode")
        ui->smsCheckCodeRadioButton->setChecked(true);
    else if (authType == "cas")
        ui->casRadioButton->setChecked(true);
    else if (authType == "httpsOauth2")
        ui->oauth2RadioButton->setChecked(true);
    else
        ui->pswRadioButton->setChecked(true);
    ui->loginUrlLineEdit->setText(ProfileSettings::read(*settings, ProfileSettings::LoginURL));
    ui->countryCodeLineEdit->setText(ProfileSettings::read(*settings, ProfileSettings::PhoneCountryCode));
    ui->phoneNumberLineEdit->setText(ProfileSettings::read(*settings, ProfileSettings::PhoneNumber));

    ui->multiLineCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::MultiLine));
    ui->keepAliveCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::KeepAlive));
    ui->keepAliveUrlLineEdit->setText(ProfileSettings::read(*settings, ProfileSettings::KeepAliveURL));
    ui->bindInterfaceLineEdit->setText(ProfileSettings::read(*settings, ProfileSettings::BindInterface));
    ui->outsideAccessCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::OutsideAccess));

    ui->skipDomainResourceCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::SkipDomainResource));
    ui->disableServerConfigCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::DisableServerConfig));
    ui->proxyAllCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::ProxyAll));
    
    ui->zjuDefaultCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::ZJUDefault));
    ui->disableDNSCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::DisableZJUDNS));
    ui->detailedDebugCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::Debug));
    ui->debugPcapCheckBox->setChecked(
        ProfileSettings::read(*settings, ProfileSettings::DebugPCAP)
    );
    ui->debugTlsLogCheckBox->setChecked(
        ProfileSettings::read(*settings, ProfileSettings::DebugTLSLog)
    );

    ui->tunCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::TUNMode));
    ui->routeCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::AddRoute));
    ui->dnsHijackCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::DNSHijack));
    ui->fakeIPCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::FakeIP));
    ui->tcpTunnelModeCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::TCPTunnelMode));
    ui->autoDetectInterfaceCheckBox->setChecked(ProfileSettings::read(*settings, ProfileSettings::AutoDetectInterface));

    tcpPortForwarding = ProfileSettings::read(*settings, ProfileSettings::TCPPortForwarding);
    udpPortForwarding = ProfileSettings::read(*settings, ProfileSettings::UDPPortForwarding);
	customDNS = ProfileSettings::read(*settings, ProfileSettings::CustomDNS);
	customProxyDomain = ProfileSettings::read(*settings, ProfileSettings::CustomProxyDomain);
    extraArguments = ProfileSettings::read(*settings, ProfileSettings::ExtraArguments);

    ui->routeCheckBox->setEnabled(ui->tunCheckBox->isChecked());
    ui->dnsHijackCheckBox->setEnabled(ui->tunCheckBox->isChecked());
    ui->fakeIPCheckBox->setEnabled(ui->tunCheckBox->isChecked());

	ui->dnsLineEdit->setDisabled(ui->dnsAutoCheckBox->isChecked());
}

void SettingWindow::applySettings()
{
    ProfileManager profileManager;
    bool oldAutoStart = profileManager.autoStartEnabled();
    bool newAutoStart = ui->autoStartCheckBox->isChecked();
    if (oldAutoStart != newAutoStart)
    {
        const OperationStatus status = AutoStart::setEnabled(newAutoStart);
        if (status.succeeded)
        {
            profileManager.setAutoStartEnabled(newAutoStart);
        }
        else
        {
            QMessageBox::critical(
                this,
                newAutoStart
                    ? "Failed to Enable Launch at Login"
                    : "Failed to Disable Launch at Login",
                status.error
            );
        }
    }
    profileManager.setSilentStartEnabled(ui->silentStartCheckBox->isChecked());

    ProfileSettings::write(*settings, ProfileSettings::Username, ui->usernameLineEdit->text());
    ProfileSettings::writeIfChanged(
        *settings, ProfileSettings::Password, loadedPassword, ui->passwordLineEdit->text()
    );
    ProfileSettings::writeIfChanged(
        *settings, ProfileSettings::TOTPSecret, loadedTotpSecret, ui->totpSecretLineEdit->text()
    );
    ProfileSettings::write(*settings, ProfileSettings::CertFile, ui->certFileLineEdit->text());
    ProfileSettings::writeIfChanged(
        *settings, ProfileSettings::CertPassword, loadedCertPassword, ui->certPasswordLineEdit->text()
    );
    loadedPassword = ui->passwordLineEdit->text();
    loadedTotpSecret = ui->totpSecretLineEdit->text();
    loadedCertPassword = ui->certPasswordLineEdit->text();
    ProfileSettings::write(*settings, ProfileSettings::CredentialsAsArguments, ui->credentialsAsArgumentsCheckBox->isChecked()
    );

    ProfileSettings::write(*settings, ProfileSettings::ConnectAfterStart, ui->connectAfterStartCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::CheckUpdateAfterStart, ui->checkUpdateAfterStartCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::AutoSetProxy, ui->autoSetProxyCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::ReconnectTime, ui->reconnectTimeSpinBox->value());
    ProfileSettings::write(*settings, ProfileSettings::AutoReconnect, ui->autoReconnectCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::SystemProxyBypass, ui->systemProxyBypassLineEdit->text());
    ProfileSettings::write(*settings, ProfileSettings::SuppressProxyOverrideWarning, ui->suppressProxyOverrideWarningCheckBox->isChecked());


    ProfileSettings::write(*settings, ProfileSettings::ServerAddress, ui->serverAddressLineEdit->text());
    ProfileSettings::write(*settings, ProfileSettings::ServerPort, ui->serverPortSpinBox->value());
    ProfileSettings::write(*settings, ProfileSettings::DNS, ui->dnsLineEdit->text());
    ProfileSettings::write(*settings, ProfileSettings::DNSAuto, ui->dnsAutoCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::SecondaryDNS, ui->secondaryDnsLineEdit->text());
    ProfileSettings::write(*settings, ProfileSettings::LocalDNSServer, ui->localDnsServerLineEdit->text().trimmed()
    );
    ProfileSettings::write(*settings, ProfileSettings::DNSServerBind, ui->dnsServerBindLineEdit->text().trimmed()
    );
    ProfileSettings::write(*settings, ProfileSettings::DNSTTL, ui->dnsTTLSpinBox->value());
    ProfileSettings::write(*settings, ProfileSettings::SOCKS5Port, ui->socks5PortSpinBox->value());
    ProfileSettings::write(*settings, ProfileSettings::HTTPPort, ui->httpPortSpinBox->value());
    ProfileSettings::write(*settings, ProfileSettings::ShadowsocksURL, ui->shadowsocksUrlLineEdit->text());
    ProfileSettings::write(*settings, ProfileSettings::DialDirectProxy, ui->dialDirectProxyLineEdit->text());
    ProfileSettings::write(*settings, ProfileSettings::UpdateBestNodesInterval, ui->updateBestNodesIntervalSpinBox->value());

    ProfileSettings::write(*settings, ProfileSettings::Protocol, ui->atrustRadioButton->isChecked() ? "atrust" : "easyconnect");
    ProfileSettings::write(*settings, ProfileSettings::EasyConnectAuthType, ui->certFileLineEdit->text().isEmpty() ? "password" : "certificate"
    );
    ProfileSettings::write(*settings, ProfileSettings::LoginDomain, ui->loginDomainLineEdit->text());
    QString authType;
    if (ui->smsCheckCodeRadioButton->isChecked())
        authType = "smsCheckCode";
    else if (ui->casRadioButton->isChecked())
        authType = "cas";
    else if (ui->oauth2RadioButton->isChecked())
        authType = "httpsOauth2";
    else
        authType = "psw";
    ProfileSettings::write(*settings, ProfileSettings::AuthType, authType);
    ProfileSettings::write(*settings, ProfileSettings::LoginURL, ui->loginUrlLineEdit->text());
    ProfileSettings::write(*settings, ProfileSettings::PhoneCountryCode, ui->countryCodeLineEdit->text());
    ProfileSettings::write(*settings, ProfileSettings::PhoneNumber, ui->phoneNumberLineEdit->text());

    ProfileSettings::write(*settings, ProfileSettings::MultiLine, ui->multiLineCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::KeepAlive, ui->keepAliveCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::KeepAliveURL, ui->keepAliveUrlLineEdit->text().trimmed());
    ProfileSettings::write(*settings, ProfileSettings::BindInterface, ui->bindInterfaceLineEdit->text().trimmed());
    ProfileSettings::write(*settings, ProfileSettings::OutsideAccess, ui->outsideAccessCheckBox->isChecked());

    ProfileSettings::write(*settings, ProfileSettings::SkipDomainResource, ui->skipDomainResourceCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::DisableServerConfig, ui->disableServerConfigCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::ProxyAll, ui->proxyAllCheckBox->isChecked());

    ProfileSettings::write(*settings, ProfileSettings::DisableZJUDNS, ui->disableDNSCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::ZJUDefault, ui->zjuDefaultCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::Debug, ui->detailedDebugCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::DebugPCAP, ui->debugPcapCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::DebugTLSLog, ui->debugTlsLogCheckBox->isChecked());

    ProfileSettings::write(*settings, ProfileSettings::TUNMode, ui->tunCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::AddRoute, ui->routeCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::DNSHijack, ui->dnsHijackCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::FakeIP, ui->fakeIPCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::TCPTunnelMode, ui->tcpTunnelModeCheckBox->isChecked());
    ProfileSettings::write(*settings, ProfileSettings::AutoDetectInterface, ui->autoDetectInterfaceCheckBox->isChecked());


    ProfileSettings::write(*settings, ProfileSettings::TCPPortForwarding, tcpPortForwarding);
    ProfileSettings::write(*settings, ProfileSettings::UDPPortForwarding, udpPortForwarding);
    ProfileSettings::write(*settings, ProfileSettings::CustomDNS, customDNS);
    ProfileSettings::write(*settings, ProfileSettings::CustomProxyDomain, customProxyDomain);
    ProfileSettings::write(*settings, ProfileSettings::ExtraArguments, extraArguments);

    ProfileSettings::write(*settings, ProfileSettings::ConfigVersion, ApplicationConstants::ConfigVersion
    );

    settings->sync();
}

bool SettingWindow::isAuthSettingChanged()
{
    if (ui->atrustRadioButton->isChecked() == false &&
        ProfileSettings::read(*settings, ProfileSettings::Protocol) != "atrust")
        return false;
    if (ui->atrustRadioButton->isChecked() == true &&
        ProfileSettings::read(*settings, ProfileSettings::Protocol) != "atrust")
        return true;
    QString currentAuthType;
    if (ui->casRadioButton->isChecked())
        currentAuthType = "cas";
    else if (ui->oauth2RadioButton->isChecked())
        currentAuthType = "httpsOauth2";
    else if (ui->smsCheckCodeRadioButton->isChecked())
        currentAuthType = "smsCheckCode";
    else
        currentAuthType = "psw";

    return currentAuthType != ProfileSettings::read(*settings, ProfileSettings::AuthType) ||
           ui->loginDomainLineEdit->text() != ProfileSettings::read(*settings, ProfileSettings::LoginDomain) ||
           ((currentAuthType == "cas" || currentAuthType == "httpsOauth2") &&
            ui->loginUrlLineEdit->text() != ProfileSettings::read(*settings, ProfileSettings::LoginURL)) ||
           ui->serverAddressLineEdit->text() != ProfileSettings::read(*settings, ProfileSettings::ServerAddress) ||
           ui->serverPortSpinBox->value() != ProfileSettings::read(*settings, ProfileSettings::ServerPort);
}
