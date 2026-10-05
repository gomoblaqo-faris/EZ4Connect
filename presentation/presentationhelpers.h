#ifndef PRESENTATIONHELPERS_H
#define PRESENTATIONHELPERS_H

#include <QString>

#include <functional>

class QWidget;

namespace PresentationHelpers
{
void retainSizeWhenHidden(QWidget *widget);
void showAboutDialog(QWidget *parent = nullptr);
bool confirmCredentials(const QString &username, const QString &password);

// Calls apply now, and again each time the interface language changes while
// the widget exists. For dialogs assembled in code, which have no
// changeEvent of their own to put their text back in the new language.
void translateWith(QWidget *widget, std::function<void()> apply);
}

#endif // PRESENTATIONHELPERS_H
