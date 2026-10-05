#include <QCoreApplication>
#include <QDebug>
#include <QEvent>

#include "presentation/language/languagemanager.h"

namespace
{
class LanguageChangeCounter : public QObject
{
public:
    int count = 0;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange)
        {
            ++count;
        }
        return QObject::eventFilter(watched, event);
    }
};

// Records every choice the manager announces.
class ChoiceRecorder
{
public:
    explicit ChoiceRecorder(LanguageManager &manager)
    {
        QObject::connect(&manager, &LanguageManager::choiceChanged, &context,
                         [this](const QString &choice) { choices.append(choice); });
    }

    QStringList choices;

private:
    QObject context;
};

bool expectResolved(const QString &choice, const QStringList &uiLanguages, const QString &expected)
{
    const QString actual = LanguageManager::resolve(choice, uiLanguages);
    if (actual != expected)
    {
        qCritical() << "resolve(" << choice << "," << uiLanguages << ") gave" << actual
                    << "instead of" << expected;
        return false;
    }
    return true;
}

bool resolvesAnExplicitChoice()
{
    return expectResolved("zh_CN", {"en-US"}, "zh_CN")
        && expectResolved("ms", {"zh-Hans-CN"}, "ms")
        && expectResolved("en", {"zh-Hans-CN"}, "en");
}

bool followsTheSystemWhenNoLanguageIsChosen()
{
    return expectResolved("", {"zh-Hans-CN", "en-US"}, "zh_CN")
        && expectResolved("", {"zh-CN"}, "zh_CN")
        && expectResolved("", {"zh-SG"}, "zh_CN")
        && expectResolved("", {"zh"}, "zh_CN")
        && expectResolved("", {"ms-MY"}, "ms")
        && expectResolved("", {"en-GB", "zh-CN"}, "en")
        // Traditional Chinese is not available, so the next preference wins.
        && expectResolved("", {"zh-Hant-TW", "ms"}, "ms")
        && expectResolved("", {"zh-TW"}, "en")
        && expectResolved("", {"fr-FR", "de-DE"}, "en")
        && expectResolved("", {}, "en");
}

bool treatsAnUnknownChoiceAsTheSystemLanguage()
{
    return expectResolved("xx", {"ms-MY"}, "ms")
        && expectResolved("zh_TW", {"en-US"}, "en");
}

bool offersEveryShippedLanguageInItsOwnName()
{
    const QList<LanguageOption> options = LanguageManager::availableLanguages();
    QStringList codes;
    for (const LanguageOption &option : options)
    {
        codes.append(option.code);
        if (option.nativeName.isEmpty())
        {
            qCritical() << "language" << option.code << "has no name";
            return false;
        }
    }
    if (codes != QStringList{"en", "zh_CN", "ms"})
    {
        qCritical() << "unexpected languages:" << codes;
        return false;
    }
    return options.at(1).nativeName == QStringLiteral(u"\u7B80\u4F53\u4E2D\u6587") // Simplified Chinese
        && options.at(2).nativeName == "Bahasa Melayu";
}

bool switchesTheApplicationLanguageLive()
{
    LanguageManager manager;
    ChoiceRecorder recorder(manager);
    LanguageChangeCounter counter;
    QCoreApplication::instance()->installEventFilter(&counter);

    manager.apply("zh_CN");
    const QString chinese = QCoreApplication::translate("LanguageMenu", "Language");
    const bool chineseLoaded = manager.effectiveLanguage() == "zh_CN"
        && chinese == QStringLiteral(u"\u8BED\u8A00"); // "Language"

    manager.apply("ms");
    const QString malay = QCoreApplication::translate("LanguageMenu", "Language");
    const bool malayLoaded = manager.effectiveLanguage() == "ms" && malay == "Bahasa";

    manager.apply("en");
    const QString english = QCoreApplication::translate("LanguageMenu", "Language");
    const bool englishRestored = manager.effectiveLanguage() == "en" && english == "Language";

    QCoreApplication::instance()->removeEventFilter(&counter);

    if (!chineseLoaded || !malayLoaded || !englishRestored)
    {
        qCritical() << "switching languages gave" << chinese << malay << english;
        return false;
    }
    if (counter.count == 0)
    {
        qCritical() << "no LanguageChange event was sent";
        return false;
    }
    if (recorder.choices != QStringList{"zh_CN", "ms", "en"})
    {
        qCritical() << "choiceChanged announced" << recorder.choices;
        return false;
    }
    return manager.choice() == "en";
}

bool doesNothingWhenTheLanguageStaysTheSame()
{
    LanguageManager manager;
    manager.apply("zh_CN");

    ChoiceRecorder recorder(manager);
    LanguageChangeCounter counter;
    QCoreApplication::instance()->installEventFilter(&counter);
    manager.apply("zh_CN");
    QCoreApplication::instance()->removeEventFilter(&counter);

    manager.apply("en");
    if (counter.count != 0 || recorder.choices != QStringList{"en"})
    {
        qCritical() << "re-applying the same language sent" << counter.count
                    << "LanguageChange events and announced" << recorder.choices;
        return false;
    }
    return true;
}

bool correctsAnUnknownSavedChoiceAndSavesLaterOnes()
{
    QStringList saved;
    const auto save = [&saved](const QString &choice) { saved.append(choice); };

    LanguageManager fromNewerVersion;
    fromNewerVersion.restore("xx", save);
    if (saved != QStringList{QString()})
    {
        qCritical() << "an unknown saved language was not corrected:" << saved;
        return false;
    }
    fromNewerVersion.apply("ms");
    fromNewerVersion.apply("en");
    if (saved != QStringList{QString(), "ms", "en"})
    {
        qCritical() << "later choices were not saved:" << saved;
        return false;
    }

    saved.clear();
    LanguageManager valid;
    valid.restore("zh_CN", save);
    valid.apply("en");
    if (saved != QStringList{"en"})
    {
        qCritical() << "a valid saved language was written back:" << saved;
        return false;
    }
    return true;
}

bool keepsTheSystemChoiceDistinctFromItsResult()
{
    LanguageManager manager;
    manager.apply("");
    // Whatever the machine's language, the choice itself stays "follow the
    // system", so it is saved that way and follows a later system change.
    const bool followsSystem = manager.choice().isEmpty()
        && LanguageManager::availableLanguages().size() == 3
        && !manager.effectiveLanguage().isEmpty();
    manager.apply("en");
    return followsSystem;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return resolvesAnExplicitChoice()
               && followsTheSystemWhenNoLanguageIsChosen()
               && treatsAnUnknownChoiceAsTheSystemLanguage()
               && offersEveryShippedLanguageInItsOwnName()
               && switchesTheApplicationLanguageLive()
               && doesNothingWhenTheLanguageStaysTheSame()
               && keepsTheSystemChoiceDistinctFromItsResult()
               && correctsAnUnknownSavedChoiceAndSavesLaterOnes()
           ? 0
           : 1;
}
