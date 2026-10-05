#include "languagemenu.h"

#include <QActionGroup>
#include <QEvent>

#include "presentation/language/languagemanager.h"

LanguageMenu::LanguageMenu(LanguageManager *languageManager, QWidget *parent)
    : QMenu(parent),
      languageManager(languageManager),
      choices(new QActionGroup(this))
{
    choices->setExclusive(true);

    systemAction = addAction(QString());
    systemAction->setData(QString());
    addSeparator();
    for (const LanguageOption &option : LanguageManager::availableLanguages())
    {
        QAction *action = addAction(option.nativeName);
        action->setData(option.code);
    }
    for (QAction *action : actions())
    {
        if (!action->isSeparator())
        {
            action->setCheckable(true);
            choices->addAction(action);
        }
    }

    connect(choices, &QActionGroup::triggered, this,
            [this](QAction *action) { this->languageManager->apply(action->data().toString()); });
    connect(languageManager, &LanguageManager::choiceChanged, this, &LanguageMenu::markChoice);

    markChoice(languageManager->choice());
    retranslate();
}

void LanguageMenu::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
    QMenu::changeEvent(event);
}

void LanguageMenu::retranslate()
{
    const QString language = tr("Language");
    // Keep the English word next to a translated title, so someone who
    // picked a language they cannot read can still find the way back.
    setTitle(language == QLatin1String("Language")
                 ? language
                 : language + QStringLiteral(" (Language)"));
    systemAction->setText(tr("System Default"));
}

void LanguageMenu::markChoice(const QString &choice)
{
    for (QAction *action : choices->actions())
    {
        if (action->data().toString() == choice)
        {
            action->setChecked(true);
            return;
        }
    }
}
