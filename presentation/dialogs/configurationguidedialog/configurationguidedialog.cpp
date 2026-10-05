#include "configurationguidedialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
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
    backButton = buttonBox->addButton(QString(), QDialogButtonBox::ActionRole);
    nextButton = buttonBox->addButton(QString(), QDialogButtonBox::AcceptRole);
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

    retranslate();
    updateProtocolPage();
}

void ConfigurationGuideDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void ConfigurationGuideDialog::retranslate()
{
    setWindowTitle(tr("Setup Guide"));
    backButton->setText(tr("Back"));

    protocolGroup->setTitle(tr("Server Protocol"));
    atrustDescription->setText(
        tr("For newer Sangfor aTrust servers. Authentication methods can be fetched from the server.")
    );
    easyconnectDescription->setText(tr("For legacy EasyConnect servers."));

    serverAddressLabel->setText(tr("Server address"));
    serverAddressLineEdit->setPlaceholderText(tr("e.g. vpn.example.edu.cn"));
    serverPortLabel->setText(tr("Server port"));

    atrustInfo->setText(
        tr("Fetch the available authentication methods from the server, then choose the one that matches your account.")
    );
    passwordAuthenticationRadioButton->setText(tr("Username and password"));
    certificateAuthenticationRadioButton->setText(tr("Certificate"));
    certificateHint->setText(
        tr("With certificate authentication, the next step asks for the certificate file and password.")
    );

    passwordGroup->setTitle(tr("Account Credentials"));
    usernameLabel->setText(tr("Account"));
    usernameLineEdit->setPlaceholderText(tr("VPN account"));
    passwordLabel->setText(tr("Password"));
    passwordLineEdit->setPlaceholderText(tr("VPN password"));
    totpLabel->setText(tr("TOTP secret"));
    totpSecretLineEdit->setPlaceholderText(tr("Optional: TOTP authenticator secret"));
    phoneGroup->setTitle(tr("Phone Number for SMS Verification"));
    phoneLabel->setText(tr("Phone"));
    phoneNumberLineEdit->setPlaceholderText(tr("Phone number"));
    certificateGroup->setTitle(tr("Certificate Credentials"));
    certificateFileLabel->setText(tr("Certificate file"));
    certificateFileLineEdit->setPlaceholderText(tr("P12 or PFX certificate file"));
    browseCertificateButton->setText(tr("Browse..."));
    certificatePasswordLabel->setText(tr("Certificate password"));
    certificatePasswordLineEdit->setPlaceholderText(tr("Optional: certificate password"));
    certificateTotpLabel->setText(tr("TOTP secret"));
    certificateTotpSecretLineEdit->setPlaceholderText(tr("Optional: TOTP authenticator secret"));
    for (QCheckBox *showCheckBox : {showPasswordCheckBox,
                                    showTotpCheckBox,
                                    showCertificatePasswordCheckBox,
                                    showCertificateTotpCheckBox})
    {
        showCheckBox->setText(tr("Show"));
    }
    ssoLabel->setText(
        tr("This method opens a login page when connecting, so no credentials are needed in advance.")
    );

    updateSelectedAuthentication();
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
    serverAddressLineEdit->setText(
        ProfileSettings::read(*sourceSettings, ProfileSettings::ServerAddress)
    );
    serverAddressLabel = new QLabel(page);
    formLayout->addRow(serverAddressLabel, serverAddressLineEdit);

    serverPortSpinBox = new QSpinBox(page);
    serverPortSpinBox->setRange(1, 65535);
    serverPortSpinBox->setValue(
        ProfileSettings::read(*sourceSettings, ProfileSettings::ServerPort)
    );
    serverPortLabel = new QLabel(page);
    formLayout->addRow(serverPortLabel, serverPortSpinBox);

    pageLayout->addLayout(formLayout);
    pageLayout->addStretch();
    return page;
}

QWidget *ConfigurationGuideDialog::createProtocolPage()
{
    auto *page = new QWidget(this);
    auto *pageLayout = new QVBoxLayout(page);

    protocolGroup = new QGroupBox(page);
    auto *protocolLayout = new QVBoxLayout(protocolGroup);

    atrustRadioButton = new QRadioButton(QStringLiteral("aTrust"), protocolGroup);
    atrustDescription = new QLabel(protocolGroup);
    atrustDescription->setWordWrap(true);

    easyconnectRadioButton = new QRadioButton(QStringLiteral("EasyConnect"), protocolGroup);
    easyconnectDescription = new QLabel(protocolGroup);
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
    atrustInfo = new QLabel(atrustPage);
    atrustInfo->setWordWrap(true);
    selectedAuthenticationLabel = new QLabel(atrustPage);
    selectedAuthenticationLabel->setWordWrap(true);
    fetchAuthenticationButton = new QPushButton(atrustPage);
    atrustLayout->addWidget(atrustInfo);
    atrustLayout->addWidget(selectedAuthenticationLabel);
    atrustLayout->addWidget(fetchAuthenticationButton, 0, Qt::AlignLeft);
    atrustLayout->addStretch();
    authenticationPages->addWidget(atrustPage);

    auto *easyconnectPage = new QWidget(authenticationPages);
    auto *easyconnectLayout = new QVBoxLayout(easyconnectPage);
    passwordAuthenticationRadioButton = new QRadioButton(easyconnectPage);
    certificateAuthenticationRadioButton = new QRadioButton(easyconnectPage);
    certificateHint = new QLabel(easyconnectPage);
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
    passwordGroup = new QGroupBox(passwordPage);
    auto *passwordForm = new QFormLayout(passwordGroup);
    passwordForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    usernameLineEdit = new QLineEdit(passwordGroup);
    usernameLineEdit->setObjectName("guideUsernameLineEdit");
    usernameLabel = new QLabel(passwordGroup);
    passwordForm->addRow(usernameLabel, usernameLineEdit);

    auto *passwordRow = new QWidget(passwordGroup);
    auto *passwordRowLayout = new QHBoxLayout(passwordRow);
    passwordRowLayout->setContentsMargins(0, 0, 0, 0);
    passwordLineEdit = new QLineEdit(passwordRow);
    passwordLineEdit->setObjectName("guidePasswordLineEdit");
    passwordLineEdit->setEchoMode(QLineEdit::Password);
    showPasswordCheckBox = new QCheckBox(passwordRow);
    passwordRowLayout->addWidget(passwordLineEdit, 1);
    passwordRowLayout->addWidget(showPasswordCheckBox);
    passwordLabel = new QLabel(passwordGroup);
    passwordForm->addRow(passwordLabel, passwordRow);

    auto *totpRow = new QWidget(passwordGroup);
    auto *totpRowLayout = new QHBoxLayout(totpRow);
    totpRowLayout->setContentsMargins(0, 0, 0, 0);
    totpSecretLineEdit = new QLineEdit(totpRow);
    totpSecretLineEdit->setObjectName("guideTotpSecretLineEdit");
    totpSecretLineEdit->setEchoMode(QLineEdit::Password);
    showTotpCheckBox = new QCheckBox(totpRow);
    totpRowLayout->addWidget(totpSecretLineEdit, 1);
    totpRowLayout->addWidget(showTotpCheckBox);
    totpLabel = new QLabel(passwordGroup);
    passwordForm->addRow(totpLabel, totpRow);

    passwordPageLayout->addWidget(passwordGroup);
    passwordPageLayout->addStretch();
    credentialPages->addWidget(passwordPage);

    auto *phonePage = new QWidget(credentialPages);
    auto *phonePageLayout = new QVBoxLayout(phonePage);
    phoneGroup = new QGroupBox(phonePage);
    auto *phoneForm = new QFormLayout(phoneGroup);
    phoneForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *phoneRow = new QWidget(phoneGroup);
    auto *phoneRowLayout = new QHBoxLayout(phoneRow);
    phoneRowLayout->setContentsMargins(0, 0, 0, 0);
    countryCodeLineEdit = new QLineEdit(phoneRow);
    countryCodeLineEdit->setObjectName("guideCountryCodeLineEdit");
    countryCodeLineEdit->setMaximumWidth(64);
    countryCodeLineEdit->setPlaceholderText(QStringLiteral("86"));
    countryCodeLineEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression("[0-9]{1,4}"),
        countryCodeLineEdit
    ));
    phoneNumberLineEdit = new QLineEdit(phoneRow);
    phoneNumberLineEdit->setObjectName("guidePhoneNumberLineEdit");
    phoneNumberLineEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression("[0-9]{1,20}"),
        phoneNumberLineEdit
    ));
    phoneRowLayout->addWidget(new QLabel("+", phoneRow));
    phoneRowLayout->addWidget(countryCodeLineEdit);
    phoneRowLayout->addWidget(phoneNumberLineEdit, 1);
    phoneLabel = new QLabel(phoneGroup);
    phoneForm->addRow(phoneLabel, phoneRow);
    phonePageLayout->addWidget(phoneGroup);
    phonePageLayout->addStretch();
    credentialPages->addWidget(phonePage);

    auto *certificatePage = new QWidget(credentialPages);
    auto *certificatePageLayout = new QVBoxLayout(certificatePage);
    certificateGroup = new QGroupBox(certificatePage);
    auto *certificateForm = new QFormLayout(certificateGroup);
    certificateForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto *certificateFileRow = new QWidget(certificateGroup);
    auto *certificateFileRowLayout = new QHBoxLayout(certificateFileRow);
    certificateFileRowLayout->setContentsMargins(0, 0, 0, 0);
    certificateFileLineEdit = new QLineEdit(certificateFileRow);
    certificateFileLineEdit->setObjectName("guideCertificateFileLineEdit");
    browseCertificateButton = new QPushButton(certificateFileRow);
    certificateFileRowLayout->addWidget(certificateFileLineEdit, 1);
    certificateFileRowLayout->addWidget(browseCertificateButton);
    certificateFileLabel = new QLabel(certificateGroup);
    certificateForm->addRow(certificateFileLabel, certificateFileRow);

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
    showCertificatePasswordCheckBox = new QCheckBox(certificatePasswordRow);
    certificatePasswordRowLayout->addWidget(certificatePasswordLineEdit, 1);
    certificatePasswordRowLayout->addWidget(showCertificatePasswordCheckBox);
    certificatePasswordLabel = new QLabel(certificateGroup);
    certificateForm->addRow(certificatePasswordLabel, certificatePasswordRow);

    auto *certificateTotpRow = new QWidget(certificateGroup);
    auto *certificateTotpRowLayout = new QHBoxLayout(certificateTotpRow);
    certificateTotpRowLayout->setContentsMargins(0, 0, 0, 0);
    certificateTotpSecretLineEdit = new QLineEdit(certificateTotpRow);
    certificateTotpSecretLineEdit->setObjectName(
        "guideCertificateTotpSecretLineEdit"
    );
    certificateTotpSecretLineEdit->setEchoMode(QLineEdit::Password);
    showCertificateTotpCheckBox = new QCheckBox(certificateTotpRow);
    certificateTotpRowLayout->addWidget(certificateTotpSecretLineEdit, 1);
    certificateTotpRowLayout->addWidget(showCertificateTotpCheckBox);
    certificateTotpLabel = new QLabel(certificateGroup);
    certificateForm->addRow(certificateTotpLabel, certificateTotpRow);
    certificatePageLayout->addWidget(certificateGroup);
    certificatePageLayout->addStretch();
    credentialPages->addWidget(certificatePage);

    auto *ssoPage = new QWidget(credentialPages);
    auto *ssoPageLayout = new QVBoxLayout(ssoPage);
    ssoLabel = new QLabel(ssoPage);
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
        tr("Choose a Certificate File"),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        tr("P12 certificates (*.p12 *.pfx);;All files (*)")
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
            QMessageBox::warning(this, tr("Invalid Server Address"), tr("The server address is required."));
            return false;
        }
    }
    else if (pages->currentIndex() == 2)
    {
        if (atrustRadioButton->isChecked() && selectedAuthType.isEmpty())
        {
            QMessageBox::warning(
                this,
                tr("No Authentication Method Selected"),
                tr("Fetch and choose an authentication method supported by the server first.")
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
                tr("Incomplete Credentials"),
                tr("Enter the VPN account and password.")
            );
            return false;
        }
        if (credentialPages->currentIndex() == 1
            && (countryCodeLineEdit->text().trimmed().isEmpty()
                || phoneNumberLineEdit->text().trimmed().isEmpty()))
        {
            QMessageBox::warning(
                this,
                tr("Incomplete Phone Number"),
                tr("Enter the country code and phone number.")
            );
            return false;
        }
        if (credentialPages->currentIndex() == 2
            && certificateFileLineEdit->text().trimmed().isEmpty())
        {
            QMessageBox::warning(
                this,
                tr("No Certificate Selected"),
                tr("Choose the P12 or PFX certificate file used to log in.")
            );
            return false;
        }
    }
    return true;
}

void ConfigurationGuideDialog::updateNavigation()
{
    // Built on every call, not once, so that they follow a language change.
    const QStringList titles{
        tr("Choose Protocol"),
        tr("Configure Server"),
        tr("Choose Authentication Method"),
        tr("Enter Credentials")
    };
    const QStringList descriptions{
        tr("Choose the access protocol your server actually uses."),
        tr("Enter the address and port given by your VPN provider."),
        tr("Choose the authentication method the server offers for your account."),
        tr("Enter the login details this authentication method needs when connecting.")
    };

    const int pageIndex = pages->currentIndex();
    stepLabel->setText(tr("Step %1 / %2").arg(pageIndex + 1).arg(pages->count()));
    titleLabel->setText(titles.value(pageIndex));
    descriptionLabel->setText(descriptions.value(pageIndex));
    backButton->setEnabled(pageIndex > 0);
    nextButton->setText(
        pageIndex == pages->count() - 1 ? tr("Save") : tr("Next")
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

    updateSelectedAuthentication();
    updateCredentialsPage();
}

void ConfigurationGuideDialog::updateSelectedAuthentication()
{
    if (selectedAuthType.isEmpty())
    {
        selectedAuthenticationLabel->setText(tr("Selected: %1").arg(tr("Not selected")));
        fetchAuthenticationButton->setText(tr("Fetch Authentication Methods"));
        return;
    }

    QString details = tr("Selected: %1").arg(authenticationMethodName(selectedAuthType));
    if (!selectedLoginDomain.isEmpty())
    {
        details += QLatin1Char('\n') + tr("Login domain: %1").arg(selectedLoginDomain);
    }
    selectedAuthenticationLabel->setText(details);
    fetchAuthenticationButton->setText(tr("Fetch Authentication Methods Again"));
}

QString ConfigurationGuideDialog::authenticationMethodName(
    const QString &authType
) const
{
    if (authType == "psw")
    {
        return tr("Username and password");
    }
    if (authType == "smsCheckCode")
    {
        return tr("SMS code");
    }
    if (authType == "cas")
    {
        return QStringLiteral("CAS");
    }
    if (authType == "httpsOauth2")
    {
        return QStringLiteral("OAuth2");
    }
    return authType.isEmpty() ? tr("Not selected") : authType;
}
