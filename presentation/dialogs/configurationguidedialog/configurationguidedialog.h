#ifndef CONFIGURATIONGUIDEDIALOG_H
#define CONFIGURATIONGUIDEDIALOG_H

#include <QDialog>
#include <QString>

class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSettings;
class QSpinBox;
class QStackedWidget;
class QWidget;

class ConfigurationGuideDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConfigurationGuideDialog(
        QWidget *parent,
        const QSettings *settings
    );

    void applyTo(QSettings &settings) const;

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void goBack();

    void goNext();

    void updateProtocolPage();

    void fetchAuthenticationMethods();

private:
    QWidget *createServerPage();

    QWidget *createProtocolPage();

    QWidget *createAuthenticationPage();

    QWidget *createCredentialsPage();

    bool validateCurrentPage();

    void updateNavigation();

    void updateCredentialsPage();

    void browseCertificateFile();

    void selectAuthenticationMethod(
        const QString &authType,
        const QString &loginDomain,
        const QString &loginUrl
    );

    QString authenticationMethodName(const QString &authType) const;

    // Sets every text in the guide in the current language.
    void retranslate();

    void updateSelectedAuthentication();

    const QSettings *sourceSettings;

    QLabel *stepLabel;
    QLabel *titleLabel;
    QLabel *descriptionLabel;
    QStackedWidget *pages;
    QPushButton *backButton;
    QPushButton *nextButton;

    QLineEdit *serverAddressLineEdit;
    QSpinBox *serverPortSpinBox;
    QLabel *serverAddressLabel;
    QLabel *serverPortLabel;

    QGroupBox *protocolGroup;
    QRadioButton *atrustRadioButton;
    QLabel *atrustDescription;
    QRadioButton *easyconnectRadioButton;
    QLabel *easyconnectDescription;

    QStackedWidget *authenticationPages;
    QLabel *atrustInfo;
    QLabel *certificateHint;
    QLabel *selectedAuthenticationLabel;
    QPushButton *fetchAuthenticationButton;
    QRadioButton *passwordAuthenticationRadioButton;
    QRadioButton *certificateAuthenticationRadioButton;

    QStackedWidget *credentialPages = nullptr;
    QLineEdit *usernameLineEdit;
    QLineEdit *passwordLineEdit;
    QLineEdit *totpSecretLineEdit;
    QLineEdit *countryCodeLineEdit;
    QLineEdit *phoneNumberLineEdit;
    QLineEdit *certificateFileLineEdit;
    QLineEdit *certificatePasswordLineEdit;
    QLineEdit *certificateTotpSecretLineEdit;

    QGroupBox *passwordGroup;
    QLabel *usernameLabel;
    QLabel *passwordLabel;
    QLabel *totpLabel;
    QCheckBox *showPasswordCheckBox;
    QCheckBox *showTotpCheckBox;
    QGroupBox *phoneGroup;
    QLabel *phoneLabel;
    QGroupBox *certificateGroup;
    QLabel *certificateFileLabel;
    QLabel *certificatePasswordLabel;
    QLabel *certificateTotpLabel;
    QPushButton *browseCertificateButton;
    QCheckBox *showCertificatePasswordCheckBox;
    QCheckBox *showCertificateTotpCheckBox;
    QLabel *ssoLabel;

    QString selectedAuthType;
    QString selectedLoginDomain;
    QString selectedLoginUrl;

    // What the secret fields were filled with, to tell which ones changed.
    QString loadedPassword;
    QString loadedTotpSecret;
    QString loadedCertPassword;
};

#endif // CONFIGURATIONGUIDEDIALOG_H
