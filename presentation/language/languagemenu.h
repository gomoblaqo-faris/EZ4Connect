#ifndef LANGUAGEMENU_H
#define LANGUAGEMENU_H

#include <QMenu>

class QAction;
class QActionGroup;
class LanguageManager;

// File > Language: "System default" plus every shipped language, each named
// in its own language. Choosing one switches the interface at once.
class LanguageMenu : public QMenu
{
    Q_OBJECT

public:
    LanguageMenu(LanguageManager *languageManager, QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslate();
    void markChoice(const QString &choice);

    LanguageManager *languageManager;
    QActionGroup *choices;
    QAction *systemAction;
};

#endif // LANGUAGEMENU_H
