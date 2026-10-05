#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QByteArray>
#include <QObject>
#include <QString>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

struct VersionInfo
{
    QString uiVersion;
    QString uiLatest;
    QString coreVersion;
    QString coreLatest;
};

enum class UpdateComponent
{
    Ui,
    Core
};

class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);

    const VersionInfo &versionInfo() const;

    // A version field holds either a version number or a status word such
    // as "checking". The words are stored in English and translated only
    // when shown, so that they follow a change of language.
    static QString displayValue(const QString &value);
    void markDisabled();
    void check();

    static QString normalizeVersionTag(const QString &tag);
    static std::optional<QString> parseReleaseVersion(const QByteArray &payload);
    static bool isNewerVersion(const QString &currentVersion, const QString &latestVersion);

signals:
    void versionInfoChanged(const VersionInfo &versionInfo);
    void checkFailed(UpdateComponent component, const QString &reason);
    void uiUpdateAvailable(const QString &version);

private:
    void handleUiReply(QNetworkReply *reply);
    void handleCoreReply(QNetworkReply *reply);

    VersionInfo versions;
    QNetworkAccessManager *uiNetworkManager;
    QNetworkAccessManager *coreNetworkManager;
};

#endif // UPDATECHECKER_H
