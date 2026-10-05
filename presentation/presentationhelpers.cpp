#include "presentationhelpers.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QMessageBox>
#include <QPixmap>
#include <QSizePolicy>
#include <QWidget>

#include "application/applicationconstants.h"

namespace
{
// Gives the free functions below a translation context.
class Text
{
    Q_DECLARE_TR_FUNCTIONS(PresentationHelpers)
};

class LanguageChangeWatcher : public QObject
{
public:
    LanguageChangeWatcher(QWidget *widget, std::function<void()> apply)
        : QObject(widget),
          apply(std::move(apply))
    {
        widget->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == parent() && event->type() == QEvent::LanguageChange)
        {
            apply();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> apply;
};

QString credit(const QString &name, const QString &description, const QString &author,
               const QString &authorUrl, const QString &homepage)
{
    return QStringLiteral("<br><br>") + name
        + QStringLiteral("<br>") + description
        + QStringLiteral("<br>")
        + Text::tr("Author: %1").arg(QStringLiteral("<a href='%1'>%2</a>").arg(authorUrl, author))
        + QStringLiteral("<br>")
        + Text::tr("Homepage: %1").arg(QStringLiteral("<a href='%1'>%1</a>").arg(homepage));
}
}

void PresentationHelpers::retainSizeWhenHidden(QWidget *widget)
{
    QSizePolicy policy = widget->sizePolicy();
    policy.setRetainSizeWhenHidden(true);
    widget->setSizePolicy(policy);
}

void PresentationHelpers::showAboutDialog(QWidget *parent)
{
    QMessageBox messageBox(parent);
    messageBox.setTextFormat(Qt::RichText);
    const QString homepage = QStringLiteral("https://github.com/") + ApplicationConstants::RepositoryName;
    translateWith(&messageBox, [&messageBox, homepage]()
    {
        messageBox.setWindowTitle(Text::tr("About"));
        messageBox.setText(
            QApplication::applicationDisplayName() + QStringLiteral(" ") + QApplication::applicationVersion()
            + QStringLiteral("<br>") + Text::tr("An improved GUI for ZJU-Connect")
            + QStringLiteral("<br>")
            + Text::tr("Author: %1").arg(QStringLiteral("<a href='https://github.com/chenx-dust'>Chenx Dust</a>"))
            + QStringLiteral("<br>")
            + Text::tr("Homepage: %1").arg(QStringLiteral("<a href='%1'>%1</a>").arg(homepage))
            + QStringLiteral("<br><br>") + Text::tr("Acknowledgements:")
            + credit(QStringLiteral("ZJU-Connect-for-Windows"), Text::tr("A Qt-based ZJU network client"),
                     QStringLiteral("Myth"), QStringLiteral("https://myth.cx"),
                     QStringLiteral("https://github.com/Mythologyli/ZJU-Connect-for-Windows"))
            + credit(QStringLiteral("zju-connect"), Text::tr("A Go implementation of the ZJU RVPN client"),
                     QStringLiteral("Myth"), QStringLiteral("https://myth.cx"),
                     QStringLiteral("https://github.com/Mythologyli/zju-connect"))
            + credit(QStringLiteral("EasierConnect"), Text::tr("An open-source EasyConnect client"),
                     QStringLiteral("lyc8503"), QStringLiteral("https://github.com/lyc8503"),
                     QStringLiteral("https://github.com/lyc8503/EasierConnect"))
        );
    });
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
            Text::tr("Warning"),
            Text::tr("The account or password is empty!\n\nContinue anyway?"),
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
            Text::tr("Warning"),
            Text::tr("The account or password contains non-ASCII characters!\nCheck your input method.\n\nContinue anyway?"),
            QMessageBox::Ok,
            QMessageBox::Cancel
        ) == QMessageBox::Ok;
    }
    return true;
}

void PresentationHelpers::translateWith(QWidget *widget, std::function<void()> apply)
{
    apply();
    new LanguageChangeWatcher(widget, std::move(apply));
}
