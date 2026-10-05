#include <QAction>
#include <QApplication>
#include <QDebug>

#include "presentation/language/languagemanager.h"
#include "presentation/language/languagemenu.h"

namespace
{
QAction *actionFor(const QMenu &menu, const QString &code)
{
    for (QAction *action : menu.actions())
    {
        if (!action->isSeparator() && action->data().toString() == code)
        {
            return action;
        }
    }
    return nullptr;
}

QString checkedChoice(const QMenu &menu)
{
    for (QAction *action : menu.actions())
    {
        if (action->isChecked())
        {
            return action->data().toString();
        }
    }
    return QStringLiteral("<none>");
}

bool switchesLanguageFromTheMenu()
{
    LanguageManager manager;
    manager.apply("en");
    LanguageMenu menu(&manager);

    if (menu.title() != "Language" || checkedChoice(menu) != "en")
    {
        qCritical() << "unexpected starting state:" << menu.title() << checkedChoice(menu);
        return false;
    }

    actionFor(menu, "ms")->trigger();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
    if (manager.choice() != "ms" || menu.title() != "Bahasa (Language)")
    {
        qCritical() << "choosing Malay gave" << manager.choice() << menu.title();
        return false;
    }

    // A change made elsewhere moves the check mark too.
    manager.apply("");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
    const bool followsManager = checkedChoice(menu).isEmpty();
    manager.apply("en");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);

    if (!followsManager || menu.title() != "Language")
    {
        qCritical() << "the menu did not follow the manager:" << checkedChoice(menu) << menu.title();
        return false;
    }
    return true;
}

bool namesEveryLanguageInItsOwnLanguage()
{
    LanguageManager manager;
    manager.apply("ms");
    LanguageMenu menu(&manager);
    const bool named = actionFor(menu, "en")->text() == "English"
        && actionFor(menu, "zh_CN")->text() == LanguageManager::availableLanguages().at(1).nativeName
        && actionFor(menu, "ms")->text() == "Bahasa Melayu"
        && actionFor(menu, "")->text() == QStringLiteral("Ikut Sistem");
    manager.apply("en");
    if (!named)
    {
        qCritical() << "a language is not named in its own language";
    }
    return named;
}
}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    return switchesLanguageFromTheMenu()
               && namesEveryLanguageInItsOwnLanguage()
           ? 0
           : 1;
}
