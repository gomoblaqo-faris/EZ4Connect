#ifndef LANGUAGEMANAGER_H
#define LANGUAGEMANAGER_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTranslator>

#include <functional>

struct LanguageOption
{
    // "en", "zh_CN" or "ms"; the suffix of the translation file.
    QString code;
    // The language's name in that language, so it can be found whatever
    // language the interface is currently in.
    QString nativeName;
};

// Loads the interface translation and Qt's own (standard buttons and
// dialogs) for the chosen language. Installing a translator makes Qt send
// QEvent::LanguageChange to every widget, which is how open windows switch
// without a restart.
//
// An empty choice means "follow the system": the language is picked from the
// system's preferred languages each time it is applied.
class LanguageManager : public QObject
{
    Q_OBJECT

public:
    explicit LanguageManager(QObject *parent = nullptr);
    ~LanguageManager() override;

    static QList<LanguageOption> availableLanguages();

    // The language to load for a choice, given the system's preferred
    // languages in the form QLocale::uiLanguages() returns them. Falls back
    // to English, the language the interface is written in.
    static QString resolve(const QString &choice, const QStringList &uiLanguages);

    void apply(const QString &choice);

    // Applies a saved choice, writes it back at once if it had to be
    // corrected (a language this version does not have, for instance), and
    // saves every later choice.
    void restore(const QString &saved, const std::function<void(const QString &)> &save);

    QString choice() const;
    // The language the interface is actually in: English when the chosen
    // language could not be loaded.
    QString effectiveLanguage() const;

signals:
    // The user's choice changed; empty means "follow the system".
    void choiceChanged(const QString &choice);

private:
    void removeTranslators();
    bool loadTranslators(const QString &language);

    QString currentChoice;
    QString currentLanguage;
    bool applied = false;
    QTranslator applicationTranslator;
    QTranslator qtTranslator;
};

#endif // LANGUAGEMANAGER_H
