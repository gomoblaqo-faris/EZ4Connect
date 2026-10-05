#ifndef SETTINGWINDOW_H
#define SETTINGWINDOW_H

#include <QDialog>
#include <QSettings>
#include "ui_settingwindow.h"
#include "presentation/dialogs/extrasettingwindow/extrasettingwindow.h"
#include "presentation/dialogs/authinfowindow/authinfowindow.h"

namespace Ui
{
    class SettingWindow;
}

class SettingWindow : public QDialog
{
Q_OBJECT

public:
    explicit SettingWindow(QWidget *parent = nullptr, QSettings *settings = nullptr, const QString &profileId = "");

    ~SettingWindow() override;

protected:
    void changeEvent(QEvent *event) override;

private:
    void updateConfigVersionLabel();

private:
    void loadSettings();
    void applySettings();
    bool isAuthSettingChanged();
    bool shouldCheckCredential();

    Ui::SettingWindow *ui;

    QSettings *settings;
    QString profileId;

    ExtraSettingWindow *extraSettingWindow;
    AuthInfoWindow *authInfoWindow;

    QString tcpPortForwarding;
    QString udpPortForwarding;
    QString customDNS;
    QString customProxyDomain;
    QString extraArguments;

    // What the secret fields were filled with, to tell which ones changed.
    QString loadedPassword;
    QString loadedTotpSecret;
    QString loadedCertPassword;
    QString loadedShadowsocksUrl;
};

#endif //SETTINGWINDOW_H
