#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QXmlStreamReader>

#include <algorithm>

// Checks translation files: every message is translated, and a translation
// keeps the placeholders and markup of its source text. Run on the files in
// translations/, and on copies refreshed by lupdate, so a string added to the
// code without being translated fails too.
namespace
{
struct Message
{
    QString context;
    QString source;
    QStringList translations;
    bool vanished = false;
};

QStringList sorted(QStringList values)
{
    std::sort(values.begin(), values.end());
    return values;
}

QStringList matches(const QString &text, const QRegularExpression &pattern)
{
    QStringList found;
    for (const QRegularExpressionMatch &match : pattern.globalMatch(text))
    {
        found.append(match.captured());
    }
    return sorted(found);
}

// The tags themselves, in the order they appear: a translation may move
// words around, but not the markup.
QStringList inOrder(const QString &text, const QRegularExpression &pattern)
{
    QStringList found;
    for (const QRegularExpressionMatch &match : pattern.globalMatch(text))
    {
        found.append(match.captured());
    }
    return found;
}

bool readMessages(const QString &path, QString &language, QList<Message> &messages)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        qCritical() << "cannot read" << path;
        return false;
    }

    QXmlStreamReader xml(&file);
    QString context;
    Message message;
    while (!xml.atEnd())
    {
        xml.readNext();
        if (!xml.isStartElement())
        {
            if (xml.isEndElement() && xml.name() == QLatin1String("message"))
            {
                messages.append(message);
            }
            continue;
        }
        const auto name = xml.name();
        if (name == QLatin1String("TS"))
        {
            language = xml.attributes().value("language").toString();
        }
        else if (name == QLatin1String("name"))
        {
            context = xml.readElementText();
        }
        else if (name == QLatin1String("message"))
        {
            message = Message{context, {}, {}, false};
        }
        else if (name == QLatin1String("source"))
        {
            message.source = xml.readElementText();
        }
        else if (name == QLatin1String("translation"))
        {
            const QString type = xml.attributes().value("type").toString();
            message.vanished = type == QLatin1String("vanished") || type == QLatin1String("obsolete");
            // A plural message holds one <numerusform> per form instead of text.
            QString text;
            while (!(xml.isEndElement() && xml.name() == QLatin1String("translation")) && !xml.atEnd())
            {
                xml.readNext();
                if (xml.isCharacters())
                {
                    text += xml.text();
                }
                else if (xml.isStartElement() && xml.name() == QLatin1String("numerusform"))
                {
                    message.translations.append(xml.readElementText());
                }
            }
            if (message.translations.isEmpty())
            {
                message.translations.append(text);
            }
        }
    }
    if (xml.hasError())
    {
        qCritical() << path << "is not valid:" << xml.errorString();
        return false;
    }
    return true;
}

bool checkFile(const QString &path)
{
    QString language;
    QList<Message> messages;
    if (!readMessages(path, language, messages))
    {
        return false;
    }

    const QString expectedLanguage =
        QFileInfo(path).completeBaseName().section('_', 1);
    if (language != expectedLanguage)
    {
        qCritical() << path << "declares language" << language << "instead of" << expectedLanguage;
        return false;
    }

    static const QRegularExpression placeholder(QStringLiteral("%\\d"));
    static const QRegularExpression markup(QStringLiteral("<[^>]+>"));
    QStringList problems;
    int checked = 0;
    for (const Message &message : messages)
    {
        if (message.vanished)
        {
            continue;
        }
        ++checked;
        const QString where = message.context + QStringLiteral(": \"") + message.source + '"';
        for (const QString &translation : message.translations)
        {
            if (translation.trimmed().isEmpty())
            {
                problems.append(QStringLiteral("not translated: ") + where);
            }
            else if (matches(translation, placeholder) != matches(message.source, placeholder))
            {
                problems.append(QStringLiteral("placeholders differ: ") + where);
            }
            else if (inOrder(translation, markup) != inOrder(message.source, markup))
            {
                problems.append(QStringLiteral("markup differs: ") + where);
            }
        }
    }

    if (checked == 0)
    {
        qCritical() << path << "has no messages";
        return false;
    }
    if (!problems.isEmpty())
    {
        qCritical().noquote() << path + QStringLiteral(":\n  ") + problems.join(QStringLiteral("\n  "))
                                     + QStringLiteral("\nRefresh the files with the update_translations "
                                                      "target and translate the new entries.");
        return false;
    }
    return true;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    const QStringList files = application.arguments().mid(1);
    if (files.isEmpty())
    {
        qCritical() << "usage: translations_test <file.ts>...";
        return 2;
    }
    bool allPassed = true;
    for (const QString &file : files)
    {
        allPassed = checkFile(file) && allPassed;
    }
    return allPassed ? 0 : 1;
}
