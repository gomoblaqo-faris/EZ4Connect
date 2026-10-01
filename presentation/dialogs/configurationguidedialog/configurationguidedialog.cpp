#include "configurationguidedialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStringList>
#include <QVBoxLayout>

#include "application/profilesettings.h"
#include "presentation/dialogs/authinfowindow/authinfowindow.h"

namespace
{
QString normalizedAuthType(const QString &authType)
{
    QString normalized = authType;
    if (normalized.startsWith("auth/"))
    {
        normalized.remove(0, 5);
    }
    return normalized;
}
}

ConfigurationGuideDialog::ConfigurationGuideDialog(
    QWidget *parent,
    const QSettings *settings
)
    : QDialog(parent),
      sourceSettings(settings)
{
    setWindowTitle("Setup Guide");
    setWindowModality(Qt::WindowModal);
    setMinimumSize(520, 340);

    auto *layout = new QVBoxLayout(this);

    stepLabel = new QLabel(this);
    titleLabel = new QLabel(this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    descriptionLabel = new QLabel(this);
    descriptionLabel->setWordWrap(true);

    layout->addWidget(stepLabel);
    layout->addWidget(titleLabel);
    layout->addWidget(descriptionLabel);

    pages = new QStackedWidget(this);
    pages->addWidget(createProtocolPage());
    pages->addWidget(createServerPage());
    pages->addWidget(createAuthenticationPage());
    pages->addWidget(createCredentialsPage());
    layout->addWidget(pages, 1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    backButton = buttonBox->addButton("Back", QDialogButtonBox::ActionRole);
    nextButton = buttonBox->addButton("Next", QDialogButtonBox::AcceptRole);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(backButton, &QPushButton::clicked, this, &ConfigurationGuideDialog::goBack);
    connect(nextButton, &QPushButton::clicked, this, &ConfigurationGuideDialog::goNext);
    connect(
        pages,
        &QStackedWidget::currentChanged,
        this,
        &ConfigurationGuideDialog::updateNavigation
    );

    const QString protocol = ProfileSettings::read(*sourceSettings, ProfileSettings::Protocol);
    if (protocol == "easyconnect")
    {
        easyconnectRadioButton->setChecked(true);
    }
    else
    {
        atrustRadioButton->setChecked(true);
    }

    selectAuthenticationMethod(
        ProfileSettings::read(*sourceSettings, ProfileSettings::AuthType),
        ProfileSettings::read(*sourceSettings, ProfileSettings::LoginDomain),
        ProfileSettings::read(*sourceSettings, ProfileSettings::LoginURL)
    );

    const QString easyconnectAuthType = ProfileSettings::easyConnectAuthType(*sourceSettings);
    certificateAuthenticationRadioButton->setChecked(
        easyconnectAuthType == "certificate"
    );
    passwordAuthenticationRadioButton->setChecked(
        easyconnectAuthType != "certificate"
    );

    usernameLineEdit->setText(
        ProfileSettings::read(*sourceSettings, ProfileSettings::Username)
    );
    loadedPassword = ProfileSettings::read(*sourceSettings, ProfileSettings::Password);
    loadedTotpSecret = ProfileSettings::read(*sourceSettings, ProfileSettings::TOTPSecret);
    loadedCertPassword = ProfileSettings::read(*sourceSettings, ProfileSettings::CertPassword);
    passwordLineEdit->setText(loadedPassword);
    totpSecretLineEdit->setText(loadedTotpSecret);
    certificateTotpSecretLineEdit->setText(totpSecretLineEdit->text());
    countryCodeLineEdit->setText(
        ProfileSettings::read(*sourceSettings, ProfileSettings::PhoneCountryCode)
    );
    phoneNumberLineEdit->setText(
        ProfileSettings::read(*sourceSettings, ProfileSettings::PhoneNumber)
    );
    certificateFileLineEdit->setText(
        ProfileSettings::read(*sourceSettings, ProfileSettings::CertFile)
    );
    certificatePasswordLineEdit->setText(loadedCertPassword);

    updateProtocolPage();
    updateNavigation();
}

void ConfigurationGuideDialog::applyTo(QSettings &settings) const
{
    ProfileSettings::write(settings, ProfileSettings::ServerAddress, serverAddressLineEdit->text().trimmed()
    );
    ProfileSettings::write(settings, ProfileSettings::ServerPort, serverPortSpinBox->value());
    ProfileSettings::write(settings, ProfileSettings::Username, usernameLineEdit->text().trimmed()
    );
    ProfileSettings::writeIfChanged(
        settings, ProfileSettings::Password, loadedPassword, passwordLineEdit->text()
    );
    ProfileSettings::writeIfChanged(
        settings, ProfileSettings::TOTPSecret, loadedTotpSecret, totpSecretLineEdit->text().trimmed()
    );
    ProfileSettings::write(settings, ProfileSettings::CertFile, certificateFileLineEdit->text().trimmed()
    );
    ProfileSettings::writeIfChanged(
        settings, ProfileSettings::CertPassword, loadedCertPassword, certificatePasswordLineEdit->text()
    );
    ProfileSettings::write(settings, ProfileSettings::PhoneCountryCode, countryCodeLineEdit->text().trimmed()
    );
    ProfileSettings::write(settings, ProfileSettings::PhoneNumber, phoneNumberLineEdit->text().trimmed()
    );

    if (atrustRadioButton->isChecked())
    {
        ProfileSettings::write(settings, ProfileSettings::Protocol, "atrust");
        ProfileSettings::write(settings, ProfileSettings::AuthType, selectedAuthType);
        ProfileSettings::write(settings, ProfileSettings::LoginDomain, selectedLoginDomain);
        ProfileSettings::write(settings, ProfileSettings::LoginURL, selectedLoginUrl);
    }
    else
    {
        ProfileSettings::write(settings, ProfileSettings::Protocol, "easyconnect");
        ProfileSettings::write(settings, ProfileSettings::EasyConnectAuthType, certificateAuthenticationRadioButton->isChecked()
                ? "certificate"
                : "password"
        );
    }
    settings.sync();
}

QWidget *ConfigurationGuideDialog::createServerPage()
{
    auto *page = new QWidget(this);
    auto *pageLayout = new QVBoxLayout(page);
    auto *formLayout = new QFormLayout();
    formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    serverAddressLineEdit = new QLineEdit(page);
    serverAddressLineEdit->setPlaceholderText("e.g. vpn.example.edu.cn");
    serverAddressLineEdit->setText(
        ProfileSettings::read(*sourceSettings, ProfileSettings::ServerAddress)
    );
    formLayout->addRow("Server address", serverAddressLineEdit);

    serverPortSpinBox = new QSpinBox(page);
    serverPortSpinBox->setRange(1, 65535);
    serverPortSpinBox->setValue(
        ProfileSettings::read(*sourceSettings, ProfileSettings::ServerPort)
    );
    formLayout->addRow("Server port", serverPortSpinBox);

    pageLayout->addLayout(formLayout);
    pageLayout->addStretch();
    return page;
}

QWidget *ConfigurationGuideDialog::createProtocolPage()
{
    auto *page = new QWidget(this);
    auto *pageLayout = new QVBoxLayout(page);

    auto *protocolGroup = new QGroupBox("Server Protocol", page);
    auto *protocolLayout = new QVBoxLayout(protocolGroup);

    atrustRadioButton = new QRadioButton("aTrust", protocolGroup);
    auto *atrustDescription = new QLabel(
        "For newer Sangfor aTrust servers. Authentication methods can be fetched from the server.",
        protocolGroup
    );
    atrustDescription->setWordWrap(true);

    easyconnectRadioButton = new QRadioButton("EasyConnect", protocolGroup);
    auto *easyconnectDescription = new QLabel(
        "For legacy EasyConnect servers.",
        protocolGroup
    );
    easyconnectDescription->setWordWrap(true);

    protocolLayout->addWidget(atrustRadioButton);
    protocolLayout->addWidget(atrustDescription);
    protocolLayout->addSpacing(12);
    protocolLayout->addWidget(easyconnectRadioButton);
    protocolLayout->addWidget(easyconnectDescription);

    pageLayout->addWidget(protocolGroup);
    pageLayout->addStretch();

    connect(
        atrustRadioButton,
        &QRadioButton::toggled,
        this,
        &ConfigurationGuideDialog::updateProtocolPage
    );
    return page;
}

QWidget *ConfigurationGuideDialog::createAuthenticationPage()
{
    auto *page = new QWidget(this);
    auto *pageLayout = new QVBoxLayout(page);

    authenticationPages = new QStackedWidget(page);

    auto *atrustPage = new QWidget(authenticationPages);
    auto *atrustLayout = new QVBoxLayout(atrustPage);
    auto *atrustInfo = new QLabel(
        "Fetch the available authentication methods from the server, then choose the one that matches your account.",
        atrustPage
    );
    atrustInfo->setWordWrap(true);
    selectedAuthenticationLabel = new QLabel(atrustPage);
    selectedAuthenticationLabel->setWordWrap(true);
    fetchAuthenticationButton = new QPushButton("Fetch Authentication Methods", atrustPage);
    atrustLayout->addWidget(atrustInfo);
    atrustLayout->addWidget(selectedAuthenticationLabel);
    atrustLayout->addWidget(fetchAuthenticationButton, 0, Qt::AlignLeft);
    atrustLayout->addStretch();
    authenticationPages->addWidget(atrustPage);

    auto *easyconnectPage = new QWidget(authenticationPages);
    auto *easyconnectLayout = new QVBoxLayout(easyconnectPage);
    passwordAuthenticationRadioButton = new QRadioButton(
        "Username and password",
        easyconnectPage
    );
    certificateAuthenticationRadioButton = new QRadioButton(
        "Certificate",
        easyconnectPage
    );
    auto *certificateHint = new QLabel(
        "With certificate authentication, the next step asks for the certificate file and password.",
        easyconnectPage
    );
    certificateHint->setWordWrap(true);
    easyconnectLayout->addWidget(passwordAuthenticationRadioButton);
    easyconnectLayout->addWidget(certificateAuthenticationRadioButton);
    easyconnectLayout->addWidget(certificateHint);
    easyconnectLayout->addStretch();
    authenticationPages->addWidget(easyconnectPage);

    pageLayout->addWidget(authenticationPages);

    connect(
        fetchAuthenticationButton,
        &QPushButton::clicked,
        this,
        &ConfigurationGuideDialog::fetchAuthenticationMethods
    );
    connect(
        passwordAuthenticationRadioButton,
        &QRadioButton::toggled,
        this,
        &ConfigurationGuideDialog::updateCredentialsPage
    );
    return page;
}

QWidget *ConfigurationGuideDialog::createCredentialsPage()
{
    auto *page = new QWidget(this);
    auto *pageLayout = new QVBoxLayout(page);
    credentialPages = new QStackedWidget(page);
    credentialPages->setObjectName("credentialPages");

    auto *passwordPage = new QWidget(credentialPages);
    auto *passwordPageLayout = new QVBoxLayout(passwordPage);
    auto *passwordGroup = new QGroupBox("Account Credentials", passwordPage);
    auto *passwordForm = new QFormLayout(passwordGroup);
    passwordForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    usernameLineEdit = new QLineEdit(passwordGroup);
    usernameLineEdit->setObjectName("guideUsernameLineEdit");
    usernameLineEdit->setPlaceholderText("VPN account");
    passwordForm->addRow("Account", usernameLineEdit);

    auto *passwordRow = new QWidget(passwordGroup);
    auto *passwordRowLayout = new QHBoxLayout(passwordRow);
    passwordRowLayout->setContentsMargins(0, 0, 0, 0);
    passwordLineEdit = new QLineEdit(passwordRow);
    passwordLineEdit->setObjectName("guidePasswordLineEdit");
    passwordLineEdit->setEchoMode(QLineEdit::Password);
    passwordLineEdit->setPlaceholderText("VPN password");
    auto *showPasswordCheckBox = new QCheckBox("Show", passwordRow);
    passwordRowLayout->addWidget(passwordLineEdit, 1);
    passwordRowLayout->addWidget(showPasswordCheckBox);
    passwordForm->addRow("Password", passwordRow);

    auto *totpRow = new QWidget(passwordGroup);
    auto *totpRowLayout = new QHBoxLayout(totpRow);
    totpRowLayout->setContentsMargins(0, 0, 0, 0);
    totpSecretLineEdit = new QLineEdit(totpRow);
    totpSecretLineEdit->setObjectName("guideTotpSecretLineEdit");
    totpSecretLineEdit->setEchoMode(QLineEdit::Password);
    totpSecretLineEdit->setPlaceholderText("Optional: TOTP authenticator secret");
    auto *showTotpCheckBox = new QCheckBox("Show", totpRow);
    totpRowLayout->addWidget(totpSecretLineEdit, 1);
    totpRowLayout->addWidget(showTotpCheckBox);
    passwordForm->addRow("TOTP secret", totpRow);

    passwordPageLayout->addWidget(passwordGroup);
    passwordPageLayout->addStretch();
    credentialPages->addWidget(passwordPage);

    auto *phonePage = new QWidget(credentialPages);
    auto *phonePageLayout = new QVBoxLayout(phonePage);
    auto *phoneGroup = new QGroupBox("Phone Number for SMS Verification", phonePage);
    auto *phoneForm = new QFormLayout(phoneGroup);
    phoneForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *phoneRow = new QWidget(phoneGroup);
    auto *phoneRowLayout = new QHBoxLayout(phoneRow);
    phoneRowLayout->setContentsMargins(0, 0, 0, 0);
    countryCodeLineEdit = new QLineEdit(phoneRow);
    countryCodeLineEdit->setObjectName("guideCountryCodeLineEdit");
    countryCodeLineEdit->setMaximumWidth(64);
    countryCodeLineEdit->setPlaceholderText("86");
    countryCodeLineEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression("[0-9]{1,4}"),
        countryCodeLineEdit
    ));
    phoneNumberLineEdit = new QLineEdit(phoneRow);
    phoneNumberLineEdit->setObjectName("guidePhoneNumberLineEdit");
    phoneNumberLineEdit->setPlaceholderText("Phone number");
    phoneNumberLineEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression("[0-9]{1,20}"),
        phoneNumberLineEdit
    ));
    phoneRowLayout->addWidget(new QLabel("+", phoneRow));
    phoneRowLayout->addWidget(countryCodeLineEdit);
    phoneRowLayout->addWidget(phoneNumberLineEdit, 1);
    phoneForm->addRow("Phone", phoneRow);
    phonePageLayout->addWidget(phoneGroup);
    phonePageLayout->addStretch();
    credentialPages->addWidget(phonePage);

    auto *certificatePage = new QWidget(credentialPages);
    auto *certificatePageLayout = new QVBoxLayout(certificatePage);
    auto *certificateGroup = new QGroupBox("Certificate Credentials", certificatePage);
    auto *certificateForm = new QFormLayout(certificateGroup);
    certificateForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto *certificateFileRow = new QWidget(certificateGroup);
    auto *certificateFileRowLayout = new QHBoxLayout(certificateFileRow);
    certificateFileRowLayout->setContentsMargins(0, 0, 0, 0);
    certificateFileLineEdit = new QLineEdit(certificateFileRow);
    certificateFileLineEdit->setObjectName("guideCertificateFileLineEdit");
    certificateFileLineEdit->setPlaceholderText("P12 or PFX certificate file");
    auto *browseCertificateButton = new QPushButton(
        "Browse...",
        certificateFileRow
    );
    certificateFileRowLayout->addWidget(certificateFileLineEdit, 1);
    certificateFileRowLayout->addWidget(browseCertificateButton);
    certificateForm->addRow("Certificate file", certificateFileRow);

    auto *certificatePasswordRow = new QWidget(certificateGroup);
    auto *certificatePasswordRowLayout = new QHBoxLayout(
        certificatePasswordRow
    );
    certificatePasswordRowLayout->setContentsMargins(0, 0, 0, 0);
    certificatePasswordLineEdit = new QLineEdit(certificatePasswordRow);
    certificatePasswordLineEdit->setObjectName(
        "guideCertificatePasswordLineEdit"
    );
    certificatePasswordLineEdit->setEchoMode(QLineEdit::Password);
    certificatePasswordLineEdit->setPlaceholderText("Optional: certificate password");
    auto *showCertificatePasswordCheckBox = new QCheckBox(
        "Show",
        certificatePasswordRow
    );
    certificatePasswordRowLayout->addWidget(certificatePasswordLineEdit, 1);
    certificatePasswordRowLayout->addWidget(showCertificatePasswordCheckBox);
    certificateForm->addRow("Certificate password", certificatePasswordRow);

    auto *certificateTotpRow = new QWidget(certificateGroup);
    auto *certificateTotpRowLayout = new QHBoxLayout(certificateTotpRow);
    certificateTotpRowLayout->setContentsMargins(0, 0, 0, 0);
    certificateTotpSecretLineEdit = new QLineEdit(certificateTotpRow);
    certificateTotpSecretLineEdit->setObjectName(
        "guideCertificateTotpSecretLineEdit"
    );
    certificateTotpSecretLineEdit->setEchoMode(QLineEdit::Password);
    certificateTotpSecretLineEdit->setPlaceholderText(
        "Optional: TOTP authenticator secret"
    );
    auto *showCertificateTotpCheckBox = new QCheckBox(
        "Show",
        certificateTotpRow
    );
    certificateTotpRowLayout->addWidget(certificateTotpSecretLineEdit, 1);
    certificateTotpRowLayout->addWidget(showCertificateTotpCheckBox);
    certificateForm->addRow("TOTP secret", certificateTotpRow);
    certificatePageLayout->addWidget(certificateGroup);
    certificatePageLayout->addStretch();
    credentialPages->addWidget(certificatePage);

    auto *ssoPage = new QWidget(credentialPages);
    auto *ssoPageLayout = new QVBoxLayout(ssoPage);
    auto *ssoLabel = new QLabel(
        "This method opens a login page when connecting, so no credentials are needed in advance.",
        ssoPage
    );
    ssoLabel->setWordWrap(true);
    ssoPageLayout->addWidget(ssoLabel);
    ssoPageLayout->addStretch();
    credentialPages->addWidget(ssoPage);

    pageLayout->addWidget(credentialPages);

    connect(showPasswordCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        passwordLineEdit->setEchoMode(
            checked ? QLineEdit::Normal : QLineEdit::Password
        );
    });
    connect(showTotpCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        totpSecretLineEdit->setEchoMode(
            checked ? QLineEdit::Normal : QLineEdit::Password
        );
    });
    connect(
        showCertificatePasswordCheckBox,
        &QCheckBox::toggled,
        this,
        [this](bool checked) {
            certificatePasswordLineEdit->setEchoMode(
                checked ? QLineEdit::Normal : QLineEdit::Password
            );
        }
    );
    connect(
        showCertificateTotpCheckBox,
        &QCheckBox::toggled,
        this,
        [this](bool checked) {
            certificateTotpSecretLineEdit->setEchoMode(
                checked ? QLineEdit::Normal : QLineEdit::Password
            );
        }
    );
    connect(
        totpSecretLineEdit,
        &QLineEdit::textChanged,
        certificateTotpSecretLineEdit,
        &QLineEdit::setText
    );
    connect(
        certificateTotpSecretLineEdit,
        &QLineEdit::textChanged,
        totpSecretLineEdit,
        &QLineEdit::setText
    );
    connect(
        browseCertificateButton,
        &QPushButton::clicked,
        this,
        &ConfigurationGuideDialog::browseCertificateFile
    );
    return page;
}

void ConfigurationGuideDialog::goBack()
{
    if (pages->currentIndex() > 0)
    {
        pages->setCurrentIndex(pages->currentIndex() - 1);
    }
}

void ConfigurationGuideDialog::goNext()
{
    if (!validateCurrentPage())
    {
        return;
    }

    if (pages->currentIndex() == pages->count() - 1)
    {
        accept();
        return;
    }
    pages->setCurrentIndex(pages->currentIndex() + 1);
}

void ConfigurationGuideDialog::updateProtocolPage()
{
    if (authenticationPages != nullptr)
    {
        authenticationPages->setCurrentIndex(
            atrustRadioButton->isChecked() ? 0 : 1
        );
    }
    updateCredentialsPage();
}

void ConfigurationGuideDialog::updateCredentialsPage()
{
    if (credentialPages == nullptr)
    {
        return;
    }

    if (!atrustRadioButton->isChecked())
    {
        credentialPages->setCurrentIndex(
            certificateAuthenticationRadioButton->isChecked() ? 2 : 0
        );
        return;
    }

    if (selectedAuthType == "smsCheckCode")
    {
        credentialPages->setCurrentIndex(1);
    }
    else if (selectedAuthType == "cas" || selectedAuthType == "httpsOauth2")
    {
        credentialPages->setCurrentIndex(3);
    }
    else
    {
        credentialPages->setCurrentIndex(0);
    }
}

void ConfigurationGuideDialog::browseCertificateFile()
{
    const QString fileName = QFileDialog::getOpenFileName(
        this,
        "Choose a Certificate File",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        "P12 Certificate (*.p12 *.pfx);;All Files (*)"
    );
    if (!fileName.isEmpty())
    {
        certificateFileLineEdit->setText(fileName);
    }
}

void ConfigurationGuideDialog::fetchAuthenticationMethods()
{
    auto *authInfoWindow = new AuthInfoWindow(this);
    connect(
        authInfoWindow,
        &AuthInfoWindow::finishAuthInfo,
        this,
        &ConfigurationGuideDialog::selectAuthenticationMethod
    );
    authInfoWindow->fetchAuthInfo(
        serverAddressLineEdit->text().trimmed(),
        serverPortSpinBox->value()
    );
    authInfoWindow->exec();
}

bool ConfigurationGuideDialog::validateCurrentPage()
{
    if (pages->currentIndex() == 1)
    {
        if (serverAddressLineEdit->text().trimmed().isEmpty())
        {
            QMessageBox::warning(this, "Invalid Server Address", "The server address is required.");
            return false;
        }
    }
    else if (pages->currentIndex() == 2)
    {
        if (atrustRadioButton->isChecked() && selectedAuthType.isEmpty())
        {
            QMessageBox::warning(
                this,
                "No Authentication Method Selected",
                "Fetch and choose an authentication method supported by the server first."
            );
            return false;
        }
    }
    else if (pages->currentIndex() == 3)
    {
        if (credentialPages->currentIndex() == 0
            && (usernameLineEdit->text().trimmed().isEmpty()
                || passwordLineEdit->text().isEmpty()))
        {
            QMessageBox::warning(
                this,
                "Incomplete Credentials",
                "Enter the VPN account and password."
            );
            return false;
        }
        if (credentialPages->currentIndex() == 1
            && (countryCodeLineEdit->text().trimmed().isEmpty()
                || phoneNumberLineEdit->text().trimmed().isEmpty()))
        {
            QMessageBox::warning(
                this,
                "Incomplete Phone Number",
                "Enter the country code and phone number."
            );
            return false;
        }
        if (credentialPages->currentIndex() == 2
            && certificateFileLineEdit->text().trimmed().isEmpty())
        {
            QMessageBox::warning(
                this,
                "No Certificate Selected",
                "Choose the P12 or PFX certificate file used to log in."
            );
            return false;
        }
    }
    return true;
}

void ConfigurationGuideDialog::updateNavigation()
{
    static const QStringList titles{
        "Choose Protocol",
        "Configure Server",
        "Choose Authentication Method",
        "Enter Credentials"
    };
    static const QStringList descriptions{
        "Choose the access protocol your server actually uses.",
        "Enter the address and port given by your VPN provider.",
        "Choose the authentication method the server offers for your account.",
        "Enter the login details this authentication method needs when connecting."
    };

    const int pageIndex = pages->currentIndex();
    stepLabel->setText(
        QString("Step %1 / %2").arg(pageIndex + 1).arg(pages->count())
    );
    titleLabel->setText(titles.value(pageIndex));
    descriptionLabel->setText(descriptions.value(pageIndex));
    backButton->setEnabled(pageIndex > 0);
    nextButton->setText(
        pageIndex == pages->count() - 1 ? "Save" : "Next"
    );
    updateProtocolPage();
}

void ConfigurationGuideDialog::selectAuthenticationMethod(
    const QString &authType,
    const QString &loginDomain,
    const QString &loginUrl
)
{
    selectedAuthType = normalizedAuthType(authType);
    selectedLoginDomain = loginDomain;
    selectedLoginUrl = loginUrl;

    QString details = "Selected: " + authenticationMethodName(selectedAuthType);
    if (!selectedLoginDomain.isEmpty())
    {
        details += "\nLogin domain: " + selectedLoginDomain;
    }
    selectedAuthenticationLabel->setText(details);
    fetchAuthenticationButton->setText("Fetch Authentication Methods Again");
    updateCredentialsPage();
}

QString ConfigurationGuideDialog::authenticationMethodName(
    const QString &authType
) const
{
    if (authType == "psw")
    {
        return "Username and password";
    }
    if (authType == "smsCheckCode")
    {
        return "SMS code";
    }
    if (authType == "cas")
    {
        return "CAS";
    }
    if (authType == "httpsOauth2")
    {
        return "OAuth2";
    }
    return authType.isEmpty() ? "Not selected" : authType;
}
