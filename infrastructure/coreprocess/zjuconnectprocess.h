#ifndef ZJUCONNECTPROCESS_H
#define ZJUCONNECTPROCESS_H

#include <QtCore>

#include <memory>

#include "application/coreprocess.h"
#include "infrastructure/coreprocess/coreoutputbuffer.h"

class ZjuConnectProcess : public CoreProcess
{
Q_OBJECT

public:
    explicit ZjuConnectProcess(QObject *parent = nullptr);

    ~ZjuConnectProcess() override;

    void start(const ConnectionProfile &profile) override;

    void stop() override;

    void writeInput(const QByteArray &data) override;

    // How long a core asked to stop may take before it is killed.
    void setKillDelayMs(int delayMs);

private:
    QString copyCoreForAppImage(const QString &programPath);

    QTemporaryDir &privateTempDir();

    void processOutput(CoreOutputBuffer &buffer, const QByteArray &data, bool flushPending = false);

    void processOutputLines(const QList<QByteArray> &lines);

    QProcess *zjuConnectProcess;

    std::unique_ptr<QTemporaryDir> tempDir;
    QString copiedCoreSourcePath;
    QString copiedCorePath;

    CoreOutputBuffer standardOutputBuffer;
    CoreOutputBuffer standardErrorBuffer;
    bool stopRequested = false;
    // Whether this launch went through sudo, the only case in which a sudo
    // password prompt in the output is genuine.
    bool launchedThroughSudo = false;
    QTimer killTimer;

};

#endif // ZJUCONNECTPROCESS_H
