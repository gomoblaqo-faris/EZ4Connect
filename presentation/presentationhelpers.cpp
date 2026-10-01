#include "presentationhelpers.h"

#include <QApplication>
#include <QMessageBox>
#include <QPixmap>
#include <QSizePolicy>
#include <QWidget>

#include "application/applicationconstants.h"

void PresentationHelpers::retainSizeWhenHidden(QWidget *widget)
{
    QSizePolicy policy = widget->sizePolicy();
    policy.setRetainSizeWhenHidden(true);
    widget->setSizePolicy(policy);
}

void PresentationHelpers::showAboutDialog(QWidget *parent)
{
    QMessageBox messageBox(parent);
    messageBox.setWindowTitle("About");
    messageBox.setTextFormat(Qt::RichText);
    const QString repository = ApplicationConstants::RepositoryName;
    messageBox.setText(
        QApplication::applicationDisplayName() + " " + QApplication::applicationVersion() +
        "<br>An improved GUI for ZJU-Connect" +
        "<br>Author: <a href='https://github.com/chenx-dust'>Chenx Dust</a>" +
        "<br>Homepage: <a href='https://github.com/" + repository +
        "'>https://github.com/" + repository + "</a>" +
        "<br><br>Acknowledgements:" +
        "<br><br>ZJU-Connect-for-Windows" +
        "<br>A Qt-based ZJU network client" +
        "<br>Author: <a href='https://myth.cx'>Myth</a>" +
        "<br>Homepage: <a href='https://github.com/Mythologyli/ZJU-Connect-for-Windows'>"
        "https://github.com/Mythologyli/ZJU-Connect-for-Windows</a>" +
        "<br><br>zju-connect" +
        "<br>A Go implementation of the ZJU RVPN client" +
        "<br>Author: <a href='https://myth.cx'>Myth</a>" +
        "<br>Homepage: <a href='https://github.com/Mythologyli/zju-connect'>"
        "https://github.com/Mythologyli/zju-connect</a>" +
        "<br><br>EasierConnect" +
        "<br>An open-source EasyConnect client" +
        "<br>Author: <a href='https://github.com/lyc8503'>lyc8503</a>" +
        "<br>Homepage: <a href='https://github.com/lyc8503/EasierConnect'>"
        "https://github.com/lyc8503/EasierConnect</a>"
    );
    messageBox.setIconPixmap(QPixmap(":/resource/icon.png").scaled(
        100,
        100,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    ));
    messageBox.exec();
}

bool PresentationHelpers::confirmCredentials(
    const QString &username,
    const QString &password
)
{
    if (username.isEmpty() || password.isEmpty())
    {
        return QMessageBox::warning(
            nullptr,
            "Warning",
            "The account or password is empty!\n\nContinue anyway?",
            QMessageBox::Ok,
            QMessageBox::Cancel
        ) == QMessageBox::Ok;
    }

    const auto containsNonAscii = [](const QString &value)
    {
        for (const QChar character : value)
        {
            if (character.unicode() > 127)
            {
                return true;
            }
        }
        return false;
    };
    if (containsNonAscii(username) || containsNonAscii(password))
    {
        return QMessageBox::warning(
            nullptr,
            "Warning",
            "The account or password contains non-ASCII characters!\nCheck your input method.\n\nContinue anyway?",
            QMessageBox::Ok,
            QMessageBox::Cancel
        ) == QMessageBox::Ok;
    }
    return true;
}
