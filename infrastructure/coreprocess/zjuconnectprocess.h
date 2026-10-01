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

};

#endif // ZJUCONNECTPROCESS_H
