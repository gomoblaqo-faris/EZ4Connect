#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSet>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTranslator>
#include <QXmlStreamReader>

#include "application/applicationlogger.h"
#include "application/defaultsettings.h"
#include "infrastructure/logging/applicationlogfile.h"
#include "infrastructure/settings/profilemanager.h"
#include "presentation/coordinators/authdialogcoordinator.h"
#include "presentation/dialogs/authinfowindow/authinfowindow.h"
#include "presentation/dialogs/configurationguidedialog/configurationguidedialog.h"
#include "presentation/dialogs/extrasettingwindow/extrasettingwindow.h"
#include "presentation/dialogs/graphcaptchawindow/graphcaptchawindow.h"
#include "presentation/dialogs/loginwindow/loginwindow.h"
#include "presentation/dialogs/settingwindow/settingwindow.h"
#include "presentation/dialogs/sudowindow/sudowindow.h"
#include "presentation/language/languagemanager.h"
#include "presentation/main/mainwindow.h"

// Each test switches to a translator that marks every string the translation
// files know, under the context they list it in, and then checks the open
// window: every text must carry the mark, and what the user had entered must
// still be there after switching back. A text without the mark was never
// passed to tr(), was looked up under another context than lupdate recorded,
// or was not set again when the language changed.
namespace
{
const QString Mark = QStringLiteral("@@");

class MarkingTranslator : public QTranslator
{
public:
    explicit MarkingTranslator(const QSet<QString> &known)
        : known(known)
    {
    }

    QString translate(const char *context, const char *sourceText, const char *, int) const override
    {
        if (sourceText == nullptr || *sourceText == '\0')
        {
            return {};
        }
        const QString contextName = QString::fromUtf8(context);
        const QString source = QString::fromUtf8(sourceText);
        // Qt's own strings, such as standard buttons, are marked too.
        if (contextName.startsWith('Q') || known.contains(contextName + '\n' + source))
        {
            return Mark + source;
        }
        return {};
    }

    bool isEmpty() const override
    {
        return false;
    }

private:
    QSet<QString> known;
};

QSet<QString> knownMessages()
{
    QSet<QString> known;
    QFile file(QStringLiteral(EZ4CONNECT_TRANSLATION_FILE));
    if (!file.open(QIODevice::ReadOnly))
    {
        qCritical() << "cannot read" << file.fileName();
        return known;
    }
    QXmlStreamReader xml(&file);
    QString context;
    while (!xml.atEnd())
    {
        if (xml.readNext() != QXmlStreamReader::StartElement)
        {
            continue;
        }
        if (xml.name() == QLatin1String("name"))
        {
            context = xml.readElementText();
        }
        else if (xml.name() == QLatin1String("source"))
        {
            known.insert(context + '\n' + xml.readElementText());
        }
    }
    return known;
}

// Product names, symbols and examples that are the same in every language,
// and the languages' own names in the language menu.
QSet<QString> untranslatedTexts()
{
    QSet<QString> texts{
        QStringLiteral("EZ4Connect"),
        QStringLiteral("aTrust"),
        QStringLiteral("EasyConnect"),
        QStringLiteral("+"),
        QStringLiteral("-"),
        QStringLiteral("86"),
    };
    for (const LanguageOption &option : LanguageManager::availableLanguages())
    {
        texts.insert(option.nativeName);
    }
    return texts;
}
const QSet<QString> Untranslated = untranslatedTexts();

// Widgets that show the user's data rather than text of the app's own.
const QSet<QString> UserData{
    // The profile's protocol and server.
    QStringLiteral("profileDetailLabel"),
};

bool isSeedable(const QLineEdit *lineEdit)
{
    const QObject *owner = lineEdit->parent();
    return !lineEdit->isReadOnly()
        && qobject_cast<const QAbstractSpinBox *>(owner) == nullptr
        && qobject_cast<const QComboBox *>(owner) == nullptr;
}

// Fills in what a user could have entered, so that a language change that
// resets it is noticed.
void seedUserInput(QWidget *window)
{
    for (QLineEdit *lineEdit : window->findChildren<QLineEdit *>())
    {
        if (isSeedable(lineEdit))
        {
            lineEdit->setText(QStringLiteral("seed ") + lineEdit->objectName());
        }
    }
}

QStringList stateOf(const QWidget *window)
{
    QStringList state;
    for (const QWidget *widget : window->findChildren<QWidget *>())
    {
        const QString name = widget->objectName();
        state.append(name + QStringLiteral(" visible=") + QString::number(widget->isVisibleTo(window))
                     + QStringLiteral(" enabled=") + QString::number(widget->isEnabled()));
        if (const auto *lineEdit = qobject_cast<const QLineEdit *>(widget))
        {
            if (isSeedable(lineEdit))
            {
                state.append(name + QStringLiteral(" text=") + lineEdit->text());
            }
        }
        else if (const auto *button = qobject_cast<const QAbstractButton *>(widget))
        {
            state.append(name + QStringLiteral(" checked=") + QString::number(button->isChecked()));
        }
        else if (const auto *comboBox = qobject_cast<const QComboBox *>(widget))
        {
            state.append(name + QStringLiteral(" index=") + QString::number(comboBox->currentIndex()));
        }
        else if (const auto *tabs = qobject_cast<const QTabWidget *>(widget))
        {
            state.append(name + QStringLiteral(" tab=") + QString::number(tabs->currentIndex()));
        }
        else if (const auto *stack = qobject_cast<const QStackedWidget *>(widget))
        {
            state.append(name + QStringLiteral(" page=") + QString::number(stack->currentIndex()));
        }
    }
    return state;
}

QStringList unmarkedTexts(const QWidget *window)
{
    QStringList unmarked;
    const auto check = [&unmarked](const QObject *owner, const char *what, const QString &text)
    {
        if (!text.trimmed().isEmpty() && !text.startsWith(Mark) && !Untranslated.contains(text)
            && !UserData.contains(owner->objectName()))
        {
            unmarked.append(QStringLiteral("%1 %2: %3").arg(owner->objectName(), what, text));
        }
    };

    check(window, "title", window->windowTitle());
    for (const QWidget *widget : window->findChildren<QWidget *>())
    {
        check(widget, "tool tip", widget->toolTip());
        for (const QAction *action : widget->actions())
        {
            check(action, "action", action->text());
        }
        if (const auto *label = qobject_cast<const QLabel *>(widget))
        {
            check(label, "label", label->text());
        }
        else if (const auto *button = qobject_cast<const QAbstractButton *>(widget))
        {
            check(button, "button", button->text());
        }
        else if (const auto *group = qobject_cast<const QGroupBox *>(widget))
        {
            check(group, "group", group->title());
        }
        else if (const auto *comboBox = qobject_cast<const QComboBox *>(widget))
        {
            for (int index = 0; index < comboBox->count(); ++index)
            {
                check(comboBox, "item", comboBox->itemText(index));
            }
        }
        else if (const auto *tabs = qobject_cast<const QTabWidget *>(widget))
        {
            for (int index = 0; index < tabs->count(); ++index)
            {
                check(tabs, "tab", tabs->tabText(index));
            }
        }
        else if (const auto *list = qobject_cast<const QListWidget *>(widget))
        {
            for (int row = 0; row < list->count(); ++row)
            {
                check(list, "item", list->item(row)->text());
            }
        }
        if (const auto *lineEdit = qobject_cast<const QLineEdit *>(widget))
        {
            check(lineEdit, "placeholder", lineEdit->placeholderText());
        }
    }
    return unmarked;
}

void deliverLanguageChange()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
    QCoreApplication::processEvents();
}

// Switches to the marking translator while the window is open, checks it,
// and switches back, which must restore the English text and keep what the
// user entered.
bool followsLanguageChanges(QWidget *window, const QString &englishTitle)
{
    seedUserInput(window);
    const QStringList stateBefore = stateOf(window);

    static const QSet<QString> known = knownMessages();
    MarkingTranslator translator(known);
    QCoreApplication::installTranslator(&translator);
    deliverLanguageChange();
    const QStringList unmarked = unmarkedTexts(window);
    const QStringList stateTranslated = stateOf(window);
    QCoreApplication::removeTranslator(&translator);
    deliverLanguageChange();
    const QStringList stateAfter = stateOf(window);

    const char *className = window->metaObject()->className();
    if (known.isEmpty())
    {
        return false;
    }
    if (!unmarked.isEmpty())
    {
        qCritical().noquote() << className << "kept text from before the language change:\n  "
                                     + unmarked.join(QStringLiteral("\n  "));
        return false;
    }
    for (const QStringList *state : {&stateTranslated, &stateAfter})
    {
        if (*state != stateBefore)
        {
            for (int index = 0; index < stateBefore.size() && index < state->size(); ++index)
            {
                if (stateBefore.at(index) != state->at(index))
                {
                    qCritical() << className << "changed what the user set:" << stateBefore.at(index)
                                << "became" << state->at(index);
                    break;
                }
            }
            return false;
        }
    }
    if (window->windowTitle() != englishTitle)
    {
        qCritical() << className << "did not switch back to English:" << window->windowTitle();
        return false;
    }
    return true;
}

bool retranslatesTheSetupGuide(QSettings &settings)
{
    ConfigurationGuideDialog guide(nullptr, &settings);
    // A later page, with a choice made, has to stay as it is.
    for (QStackedWidget *stack : guide.findChildren<QStackedWidget *>())
    {
        if (stack->count() == 4 && stack->objectName() != QLatin1String("credentialPages"))
        {
            stack->setCurrentIndex(2);
        }
    }
    return followsLanguageChanges(&guide, "Setup Guide");
}

bool retranslatesTheSettingsWindow(QSettings &settings)
{
    auto *window = new SettingWindow(nullptr, &settings, QString());
    if (auto *tabs = window->findChild<QTabWidget *>())
    {
        tabs->setCurrentIndex(1);
    }
    const bool followed = followsLanguageChanges(window, "Settings");
    delete window;
    return followed;
}

bool retranslatesTheSmallerDialogs()
{
    LoginWindow login;
    SudoWindow sudo;
    GraphCaptchaWindow captcha;
    ExtraSettingWindow extraSettings;
    AuthInfoWindow authInfo;
    for (QDialog *dialog : QList<QDialog *>{&login, &sudo, &captcha, &extraSettings, &authInfo})
    {
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    return followsLanguageChanges(&login, login.windowTitle())
        && followsLanguageChanges(&sudo, sudo.windowTitle())
        && followsLanguageChanges(&captcha, captcha.windowTitle())
        && followsLanguageChanges(&extraSettings, extraSettings.windowTitle())
        && followsLanguageChanges(&authInfo, authInfo.windowTitle());
}

bool retranslatesTheMainWindow(const QString &logPath)
{
    // A profile as the app leaves it after its first start, so that no
    // first-launch question opens over the window.
    const ProfileManager profiles;
    {
        QSettings profile(profiles.profilePath(profiles.activeProfile()), QSettings::IniFormat);
        DefaultSettings::reset(profile);
        profile.sync();
    }

    ApplicationLogger logger;
    ApplicationLogFile logFile(logPath);
    LanguageManager languages;
    languages.apply("en");
    auto *window = new MainWindow(&logger, &logFile, &languages);
    deliverLanguageChange();
    const bool followed = followsLanguageChanges(window, window->windowTitle());
    delete window;
    return followed;
}

bool retranslatesDialogsBuiltInCode()
{
    QWidget parent;
    AuthDialogCoordinator coordinator(&parent);
    coordinator.requestPhoneNumber("86", "");
    QDialog *phoneDialog = parent.findChild<QDialog *>();
    if (phoneDialog == nullptr)
    {
        qCritical() << "the phone number dialog was not created";
        return false;
    }
    phoneDialog->setAttribute(Qt::WA_DeleteOnClose, false);
    const bool followed = followsLanguageChanges(phoneDialog, "SMS Verification");
    delete phoneDialog;
    return followed;
}
}

// Test mode moves the app's storage aside, but keeps it between runs. Start
// from nothing, so that profiles left by an earlier run cannot show up.
bool startWithEmptyStorage()
{
    for (const auto location : {QStandardPaths::AppConfigLocation, QStandardPaths::AppLocalDataLocation})
    {
        const QString path = QStandardPaths::writableLocation(location);
        if (!path.contains(QLatin1String("qttest")))
        {
            qCritical() << "refusing to clear storage outside test mode:" << path;
            return false;
        }
        QDir(path).removeRecursively();
    }
    return true;
}

int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("EZ4Connect"));
    QApplication::setApplicationDisplayName(QStringLiteral("EZ4Connect"));
    if (!startWithEmptyStorage())
    {
        return 1;
    }

    QTemporaryDir directory;
    QSettings settings(directory.filePath("profile.ini"), QSettings::IniFormat);
    DefaultSettings::reset(settings);

    return retranslatesTheSetupGuide(settings)
               && retranslatesTheSettingsWindow(settings)
               && retranslatesTheSmallerDialogs()
               && retranslatesDialogsBuiltInCode()
               && retranslatesTheMainWindow(directory.filePath("ez4connect.log"))
           ? 0
           : 1;
}
