#include "languagemanager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QLocale>

namespace
{
const QString English = QStringLiteral("en");
// Where the build puts the compiled interface translations.
const QString TranslationPrefix = QStringLiteral(":/i18n/ez4connect_");

bool isAvailable(const QString &code)
{
    for (const LanguageOption &option : LanguageManager::availableLanguages())
    {
        if (option.code == code)
        {
            return true;
        }
    }
    return false;
}

QString languageFor(const QLocale &locale)
{
    switch (locale.language())
    {
    case QLocale::English:
        return English;
    case QLocale::Malay:
        return QStringLiteral("ms");
    case QLocale::Chinese:
        // Only Simplified Chinese is translated. Traditional Chinese readers
        // get their next preferred language instead.
        if (locale.script() == QLocale::SimplifiedHanScript)
        {
            return QStringLiteral("zh_CN");
        }
        return {};
    default:
        return {};
    }
}
}

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
{
}

LanguageManager::~LanguageManager()
{
    removeTranslators();
}

QList<LanguageOption> LanguageManager::availableLanguages()
{
    return {
        {English, QStringLiteral("English")},
        // "Simplified Chinese", written in Chinese.
        {QStringLiteral("zh_CN"), QStringLiteral(u"\u7B80\u4F53\u4E2D\u6587")},
        {QStringLiteral("ms"), QStringLiteral("Bahasa Melayu")},
    };
}

QString LanguageManager::resolve(const QString &choice, const QStringList &uiLanguages)
{
    if (isAvailable(choice))
    {
        return choice;
    }
    for (const QString &name : uiLanguages)
    {
        const QString language = languageFor(QLocale(name));
        if (!language.isEmpty())
        {
            return language;
        }
    }
    return English;
}

void LanguageManager::apply(const QString &choice)
{
    // An unknown saved value, from a later version for example, means
    // "follow the system" rather than a language that cannot be loaded.
    const QString normalizedChoice = isAvailable(choice) ? choice : QString();
    const QString language = resolve(normalizedChoice, QLocale::system().uiLanguages());
    const bool choiceIsNew = !applied || normalizedChoice != currentChoice;

    if (!applied || language != currentLanguage || normalizedChoice != currentChoice)
    {
        removeTranslators();
        QString loaded = language;
        if (language != English && !loadTranslators(language))
        {
            removeTranslators();
            loaded = English;
        }
        // Following the system keeps its regional formats, such as dates and
        // numbers, even when the interface falls back to English.
        QLocale::setDefault(normalizedChoice.isEmpty() ? QLocale::system() : QLocale(loaded));
        currentLanguage = loaded;
    }

    applied = true;
    currentChoice = normalizedChoice;
    if (choiceIsNew)
    {
        emit choiceChanged(currentChoice);
    }
}

void LanguageManager::restore(
    const QString &saved,
    const std::function<void(const QString &)> &save
)
{
    apply(saved);
    if (currentChoice != saved)
    {
        save(currentChoice);
    }
    connect(this, &LanguageManager::choiceChanged, this, save);
}

QString LanguageManager::choice() const
{
    return currentChoice;
}

QString LanguageManager::effectiveLanguage() const
{
    return currentLanguage;
}

void LanguageManager::removeTranslators()
{
    // Each removal makes Qt send QEvent::LanguageChange; it does nothing
    // for a translator that is not installed.
    QCoreApplication::removeTranslator(&qtTranslator);
    QCoreApplication::removeTranslator(&applicationTranslator);
}

bool LanguageManager::loadTranslators(const QString &language)
{
    if (!applicationTranslator.load(TranslationPrefix + language))
    {
        qWarning().noquote() << "The interface translation for" << language
                             << "could not be loaded; the interface stays in English";
        return false;
    }
    QCoreApplication::installTranslator(&applicationTranslator);

    // Qt's own strings: standard buttons, file dialogs and the macOS
    // application menu. Packages ship either the per-module catalog or the
    // "qt_" one that includes it.
    const QString qtTranslations = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (qtTranslator.load(QStringLiteral("qtbase_") + language, qtTranslations)
        || qtTranslator.load(QStringLiteral("qt_") + language, qtTranslations))
    {
        QCoreApplication::installTranslator(&qtTranslator);
    }
    else
    {
        qInfo().noquote() << "Qt has no translation of its own for" << language
                          << "here; standard buttons stay in English";
    }
    return true;
}
