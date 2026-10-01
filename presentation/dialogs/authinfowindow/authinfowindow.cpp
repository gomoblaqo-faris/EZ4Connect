#include "authinfowindow.h"

#include "application/applicationlogger.h"
#include "infrastructure/coreprocess/consoleoutputdecoder.h"
#include "infrastructure/coreprocess/coreexecutable.h"

#include <QMetaEnum>

#include <QDebug>
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QKeyEvent>
#include <QProcess>

AuthInfoWindow::AuthInfoWindow(QWidget *parent)
    : QDialog(parent),
    ui(new Ui::AuthInfoWindow)
{
    ui->setupUi(this);

    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose);

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(
        ui->authInfoListWidget,
        &QListWidget::currentItemChanged,
        this,
        [this](QListWidgetItem *current)
        {
            ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(
                current != nullptr
            );
        }
    );

    connect(this, &QDialog::accepted, [&]() {
        QListWidgetItem *selectedItem = ui->authInfoListWidget->currentItem();
        if (!selectedItem)
            return;
        emit finishAuthInfo(selectedItem->data(Qt::UserRole).toString(),
                            selectedItem->data(Qt::UserRole + 1).toString(),
                            selectedItem->data(Qt::UserRole + 2).toString());
    });

    proc_ = new QProcess(this);
    connect(proc_, &QProcess::readyReadStandardOutput, this,
            [this]() { stdoutBuf_ += proc_->readAllStandardOutput(); });
    connect(proc_, &QProcess::readyReadStandardError, this,
            [this]() { stderrBuf_ += proc_->readAllStandardError(); });
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                // The list is expected on one stream and the core's log lines
                // on the other. Which is which is the core's business, so each
                // stream is tried on its own before the two together.
                QString output;
                QJsonParseError jsonError;
                QJsonDocument doc;
                for (const QByteArray &candidate : {stdoutBuf_, stderrBuf_, stdoutBuf_ + stderrBuf_})
                {
                    output = ConsoleOutputDecoder::decode(candidate);
                    doc = QJsonDocument::fromJson(output.toUtf8(), &jsonError);
                    if (jsonError.error == QJsonParseError::NoError)
                    {
                        break;
                    }
                }
                // Login URLs in the reply can carry a ticket.
                qInfo().noquote() << "Available authentication methods:\n"
                    + ApplicationLogger::redactSecrets(output);
                if (jsonError.error != QJsonParseError::NoError)
                {
                    qWarning().noquote() << "Failed to parse authentication methods: " + jsonError.errorString();
                    ui->label->setText("Failed to fetch authentication methods. Check the server details and try again.");
                    return;
                }
                if (!doc.isArray())
                {
                    qWarning().noquote() << "Failed to parse authentication methods: the response is not a list";
                    ui->label->setText("The server did not return a valid list of authentication methods.");
                    return;
                }
                QJsonArray arr = doc.array();
                for (QJsonValueRef v : arr) {
                    QJsonObject obj = v.toObject();
                    QString authName = obj.value("authName").toString();
                    QString authType = obj.value("authType").toString();
                    QString loginDomain = obj.value("loginDomain").toString();
                    QString loginUrl = obj.value("loginUrl").toString();
                    QListWidgetItem *item =
                        new QListWidgetItem(QString("%1 - %2 - %3 - %4").arg(authName, authType, loginDomain, loginUrl.isEmpty()? "none" : loginUrl));
                    item->setData(Qt::UserRole, authType);
                    item->setData(Qt::UserRole + 1, loginDomain);
                    item->setData(Qt::UserRole + 2, loginUrl);
                    ui->authInfoListWidget->addItem(item);
                }
                ui->label->setText(
                    arr.isEmpty()
                        ? "The server returned no authentication methods."
                        : "Choose an authentication method:"
                );
            });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        qWarning().noquote()
            << QString("Failed to fetch authentication methods: ")
                   + QMetaEnum::fromType<QProcess::ProcessError>().valueToKey(error);
        ui->label->setText("Failed to fetch authentication methods. Check the core executable and the server details.");
    });
}

AuthInfoWindow::~AuthInfoWindow()
{
    delete ui;
}

void AuthInfoWindow::fetchAuthInfo(const QString& serverAddress, int port)
{
    stdoutBuf_.clear();
    stderrBuf_.clear();
    ui->authInfoListWidget->clear();
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    ui->label->setText("Fetching authentication methods, please wait...");
    proc_->start(CoreExecutable::path(),
                 {"-protocol", "atrust", "-server", serverAddress, "-port", QString::number(port), "-auth-info"});
    qInfo().noquote() << "Fetching available authentication methods...";
}
