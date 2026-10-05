#pragma once

#include <QDialog>
#include "ui_sudowindow.h"

class SudoWindow : public QDialog
{
	Q_OBJECT

public:
	SudoWindow(QWidget *parent = nullptr);

	~SudoWindow() override;

	void keyPressEvent(QKeyEvent *event) override;

signals:
	void sudo(const QString& password, bool save);

protected:
	void changeEvent(QEvent *event) override;

private:
	Ui::SudoWindow *ui;
};
