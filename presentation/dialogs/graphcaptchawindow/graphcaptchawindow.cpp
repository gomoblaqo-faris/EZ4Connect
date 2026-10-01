#include "graphcaptchawindow.h"

#include "captchacanvas.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

GraphCaptchaWindow::GraphCaptchaWindow(QWidget *parent) : QDialog(parent), ui(new Ui::GraphCaptchaWindow)
{
    ui->setupUi(this);

    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose);

    connect(ui->buttonBox->button(QDialogButtonBox::Reset), &QAbstractButton::clicked, [&]() {
        ui->canvas->clearAll();
    });
}

GraphCaptchaWindow::~GraphCaptchaWindow()
{
    delete ui;
}

void GraphCaptchaWindow::setGraph(const QString &graphFile, bool useTextInput)
{
    textInputMode = useTextInput;
    QPixmap graph;
    QFile imageFile(graphFile);
    if (imageFile.open(QIODevice::ReadOnly))
    {
        graph.loadFromData(imageFile.readAll());
    }
    ui->canvas->setImage(graph);
    ui->canvas->setEnabled(!textInputMode);
    ui->codeLabel->setVisible(textInputMode);
    ui->codeEdit->setVisible(textInputMode);
    ui->buttonBox->button(QDialogButtonBox::Reset)->setVisible(!textInputMode);
    if (textInputMode)
    {
        ui->codeEdit->clear();
        ui->codeEdit->setFocus();
    }
}

void GraphCaptchaWindow::accept()
{
    if (ui->canvas->image().isNull())
    {
        QMessageBox::warning(this, "Captcha", "The captcha image failed to load. Please try connecting again.");
        return;
    }

    if (textInputMode)
    {
        const QString code = ui->codeEdit->text().trimmed();
        if (code.isEmpty())
        {
            QMessageBox::warning(this, "Captcha", "Enter the characters shown in the image.");
            ui->codeEdit->setFocus();
            return;
        }
        emit finishCaptcha(code.toLocal8Bit());
    }
    else
    {
        QJsonObject obj;
        QJsonArray points;
        for (const auto &point : ui->canvas->pointsPx())
        {
            QJsonArray pointArray;
            pointArray.append(int(point.x()));
            pointArray.append(int(point.y()));
            points.append(pointArray);
        }
        obj.insert("coordinates", points);
        obj.insert("width", ui->canvas->image().width());
        obj.insert("height", ui->canvas->image().height());
        emit finishCaptcha(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }

    QDialog::accept();
}

void GraphCaptchaWindow::reject()
{
    emit cancelled();
    QDialog::reject();
}
