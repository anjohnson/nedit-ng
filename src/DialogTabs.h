
#ifndef DIALOG_TABS_H_
#define DIALOG_TABS_H_

#include "Dialog.h"
#include "ui_DialogTabs.h"

class DocumentWidget;

class DialogTabs final : public Dialog {
public:
	Q_OBJECT
public:
    explicit DialogTabs(DocumentWidget *document, QWidget *parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
	~DialogTabs() override = default;

private Q_SLOTS:
	void on_buttonBox_accepted();
	void on_buttonBox_helpRequested();

private:
	void checkEmulateTabs_toggled(bool checked);
	void connectSlots();

private:
	Ui::DialogTabs ui;
	DocumentWidget *document_;
};

#endif
