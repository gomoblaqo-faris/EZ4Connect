#include "authdialogcoordinator.h"

#include <QCheckBox>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QWidget>

#include "presentation/dialogs/graphcaptchawindow/graphcaptchawindow.h"
#include "presentation/dialogs/loginwindow/loginwindow.h"
#include "presentation/dialogs/ssologinwebview/ssologinwebview.h"
#include "presentation/dialogs/sudowindow/sudowindow.h"
#include "presentation/presentationhelpers.h"

AuthDialogCoordinator::AuthDialogCoordinator(QWidget *parentWidget, QObject *parent)
    : AuthPrompter(parent),
      parentWidget(parentWidget)
{
}

void AuthDialogCoordinator::requestLogin(
    const QString &username,
    const QString &password
)
{
    if (loginWindow != nullptr)
    {
        loginWindow->raise();
        loginWindow->activateWindow();
        return;
    }

    loginWindow = new LoginWindow(parentWidget);
    loginWindow->setAttribute(Qt::WA_DeleteOnClose);
    loginWindow->setDetail(username, password);
    connect(loginWindow, &LoginWindow::login, this,
            &AuthDialogCoordinator::loginSubmitted);
    loginWindow->show();
}

void AuthDialogCoordinator::requestPhoneNumber(
    const QString &countryCode,
    const QString &phoneNumber
)
{
    if (phoneNumberDialog != nullptr)
    {
        phoneNumberDialog->raise();
        phoneNumberDialog->activateWindow();
        return;
    }

    auto *dialog = new QDialog(parentWidget);
    phoneNumberDialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);

    auto *layout = new QVBoxLayout(dialog);
    auto *promptLabel = new QLabel(dialog);
    layout->addWidget(promptLabel);

    auto *phoneLayout = new QHBoxLayout;
    phoneLayout->addWidget(new QLabel("+", dialog));
    auto *countryCodeEdit = new QLineEdit(countryCode, dialog);
    countryCodeEdit->setObjectName("countryCodeLineEdit");
    countryCodeEdit->setMaximumWidth(60);
    phoneLayout->addWidget(countryCodeEdit);
    phoneLayout->addWidget(new QLabel("-", dialog));
    auto *phoneNumberEdit = new QLineEdit(phoneNumber, dialog);
    phoneNumberEdit->setObjectName("phoneNumberLineEdit");
    phoneLayout->addWidget(phoneNumberEdit);
    layout->addLayout(phoneLayout);

    auto *saveCheckBox = new QCheckBox(dialog);
    layout->addWidget(saveCheckBox);

    PresentationHelpers::translateWith(
        dialog,
        [dialog, promptLabel, countryCodeEdit, phoneNumberEdit, saveCheckBox]()
        {
            dialog->setWindowTitle(tr("SMS Verification"));
            promptLabel->setText(tr("Enter the phone number that will receive the code:"));
            countryCodeEdit->setPlaceholderText(tr("Code"));
            phoneNumberEdit->setPlaceholderText(tr("Phone number"));
            saveCheckBox->setText(tr("Remember phone number (can be removed in Settings)"));
        }
    );

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        dialog
    );
    connect(
        buttonBox,
        &QDialogButtonBox::accepted,
        dialog,
        [this, dialog, countryCodeEdit, phoneNumberEdit, saveCheckBox]()
        {
            const QString submittedCountryCode = countryCodeEdit->text().trimmed();
            const QString submittedPhoneNumber = phoneNumberEdit->text().trimmed();
            if (submittedCountryCode.isEmpty() || submittedPhoneNumber.isEmpty())
            {
                QMessageBox::warning(
                    dialog,
                    tr("Warning"),
                    tr("The country code and phone number are required.")
                );
                return;
            }

            emit phoneNumberSubmitted(
                submittedCountryCode,
                submittedPhoneNumber,
                saveCheckBox->isChecked()
            );
            dialog->accept();
        }
    );
    connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (phoneNumber.isEmpty())
    {
        phoneNumberEdit->setFocus();
    }
    else
    {
        countryCodeEdit->setFocus();
    }
    dialog->show();
}

void AuthDialogCoordinator::requestSudoPassword()
{
    if (sudoWindow != nullptr)
    {
        sudoWindow->raise();
        sudoWindow->activateWindow();
        return;
    }

    sudoWindow = new SudoWindow(parentWidget);
    sudoWindow->setAttribute(Qt::WA_DeleteOnClose);
    connect(sudoWindow, &SudoWindow::sudo, this,
            &AuthDialogCoordinator::sudoPasswordSubmitted);
    connect(sudoWindow, &QDialog::rejected, this,
            [this]()
            {
                emit sudoPasswordSubmitted({}, false);
            });
    sudoWindow->show();
}

void AuthDialogCoordinator::requestGraphCaptcha(const QString &graphFile, bool textInputMode)
{
    qInfo().noquote() << "Captcha required";
    if (graphCaptchaWindow != nullptr)
    {
        graphCaptchaWindow->setGraph(graphFile, textInputMode);
        graphCaptchaWindow->raise();
        graphCaptchaWindow->activateWindow();
        return;
    }

    graphCaptchaWindow = new GraphCaptchaWindow(parentWidget);
    graphCaptchaWindow->setAttribute(Qt::WA_DeleteOnClose);
    graphCaptchaWindow->setGraph(graphFile, textInputMode);
    connect(graphCaptchaWindow, &GraphCaptchaWindow::finishCaptcha, this,
            [this](const QByteArray &captcha)
            {
                qInfo().noquote() << "Captcha submitted";
                emit interactiveInputSubmitted(captcha + "\n");
            });
    connect(graphCaptchaWindow, &GraphCaptchaWindow::cancelled, this,
            [this]()
            {
                qInfo().noquote() << "Captcha entry cancelled";
                emit interactiveInputCancelled();
            });
    graphCaptchaWindow->show();
}

void AuthDialogCoordinator::requestSmsCode(bool showSkipSecondaryAuthOption)
{
    qInfo().noquote() << "SMS code required";

    QDialog dialog(parentWidget);

    auto *layout = new QVBoxLayout(&dialog);
    auto *promptLabel = new QLabel(&dialog);
    layout->addWidget(promptLabel);

    auto *codeEdit = new QLineEdit(&dialog);
    layout->addWidget(codeEdit);

    auto *skipCheckBox = new QCheckBox(&dialog);
    skipCheckBox->setVisible(showSkipSecondaryAuthOption);
    layout->addWidget(skipCheckBox);

    PresentationHelpers::translateWith(&dialog, [&dialog, promptLabel, skipCheckBox]()
    {
        dialog.setWindowTitle(tr("SMS Code"));
        promptLabel->setText(tr("Enter the SMS code:"));
        skipCheckBox->setText(tr("Skip SMS verification in future"));
    });

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        &dialog
    );
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (!accepted)
    {
        qInfo().noquote() << "SMS code entry cancelled";
        emit interactiveInputCancelled();
        return;
    }

    QByteArray input;
    input = codeEdit->text().toLocal8Bit();
    if (showSkipSecondaryAuthOption && skipCheckBox->isChecked())
    {
        input.prepend('$');
    }

    qInfo().noquote() << "SMS code submitted";
    emit interactiveInputSubmitted(input + "\n");
}

void AuthDialogCoordinator::requestRadiusCode(bool showSkipSecondaryAuthOption)
{
    qInfo().noquote() << "RADIUS token (SMS code) required";

    QDialog dialog(parentWidget);

    auto *layout = new QVBoxLayout(&dialog);
    auto *promptLabel = new QLabel(&dialog);
    layout->addWidget(promptLabel);

    auto *codeEdit = new QLineEdit(&dialog);
    layout->addWidget(codeEdit);

    auto *skipCheckBox = new QCheckBox(&dialog);
    skipCheckBox->setVisible(showSkipSecondaryAuthOption);
    layout->addWidget(skipCheckBox);

    PresentationHelpers::translateWith(&dialog, [&dialog, promptLabel, skipCheckBox]()
    {
        dialog.setWindowTitle(tr("RADIUS Token"));
        promptLabel->setText(tr("Enter the SMS code you received (RADIUS token):"));
        skipCheckBox->setText(tr("Skip secondary authentication in future"));
    });

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        &dialog
    );
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (!accepted)
    {
        qInfo().noquote() << "RADIUS token entry cancelled";
        emit interactiveInputCancelled();
        return;
    }

    QByteArray input;
    input = codeEdit->text().toLocal8Bit();
    if (showSkipSecondaryAuthOption && skipCheckBox->isChecked())
    {
        input.prepend('$');
    }

    qInfo().noquote() << "RADIUS token submitted";
    emit interactiveInputSubmitted(input + "\n");
}

void AuthDialogCoordinator::requestTotpCode()
{
    qInfo().noquote() << "TOTP code required";
    bool accepted = false;
    const QString totp = QInputDialog::getText(
        parentWidget,
        tr("TOTP Code"),
        tr("Enter the TOTP code:"),
        QLineEdit::Normal,
        "",
        &accepted
    );
    if (!accepted)
    {
        qInfo().noquote() << "TOTP code entry cancelled";
        emit interactiveInputCancelled();
        return;
    }

    qInfo().noquote() << "TOTP code submitted";
    emit interactiveInputSubmitted(totp.toLocal8Bit() + "\n");
}

void AuthDialogCoordinator::requestSsoLogin(const QUrl &serverUrl, const QUrl &loginUrl)
{
    if (ssoLoginWebView != nullptr)
    {
        ssoLoginWebView->raise();
        ssoLoginWebView->activateWindow();
        return;
    }

    ssoLoginWebView = new SsoLoginWebView(parentWidget);
    ssoLoginWebView->setAttribute(Qt::WA_DeleteOnClose);
    ssoLoginWebView->setCallbackServerUrl(serverUrl);
    ssoLoginWebView->setInitialUrl(loginUrl);
    connect(ssoLoginWebView, &SsoLoginWebView::loginCompleted, this,
            [this](const QString &url)
            {
                emit interactiveInputSubmitted(url.toLocal8Bit() + "\n");
            });
    ssoLoginWebView->show();
}

AuthPrompter::ProxyOverwriteAnswer AuthDialogCoordinator::askProxyOverwrite()
{
    QMessageBox messageBox(
        QMessageBox::Warning,
        tr("Warning"),
        tr("A system proxy is already configured (possibly by Clash or another proxy app).\n"
           "Overwrite the current system proxy settings?"),
        QMessageBox::Yes | QMessageBox::No,
        parentWidget
    );
    auto *dontShowCheckBox = new QCheckBox(tr("Don't ask again"));
    messageBox.setCheckBox(dontShowCheckBox);

    ProxyOverwriteAnswer answer;
    answer.overwrite = messageBox.exec() == QMessageBox::Yes;
    answer.remember = answer.overwrite && dontShowCheckBox->isChecked();
    return answer;
}
