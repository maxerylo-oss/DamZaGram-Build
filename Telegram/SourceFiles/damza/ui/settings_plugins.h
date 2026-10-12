// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

#include "base/unique_qptr.h"
#include "settings/settings_common.h"
#include "settings/settings_common_session.h"

namespace Ui {
class PopupMenu;
class Show;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Damza::Plugins {
struct PluginState;
} // namespace Damza::Plugins

namespace Settings {

class DamzaPlugins : public Section<DamzaPlugins> {
public:
	DamzaPlugins(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

protected:
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dropEvent(QDropEvent *e) override;

private:
	void setupContent();
	void setupHeader(not_null<Ui::VerticalLayout*> container);
	void setupFooter(not_null<Ui::VerticalLayout*> container);
	void scheduleRebuild();
	void rebuild();
	void addCard(
		not_null<Ui::VerticalLayout*> list,
		const Damza::Plugins::PluginState &state);
	void showInstallMenu(not_null<QWidget*> parent);
	[[nodiscard]] std::shared_ptr<Ui::Show> uiShow() const;
	[[nodiscard]] QString defaultAuthor() const;

	const not_null<Window::SessionController*> _controller;
	Ui::VerticalLayout *_list = nullptr;
	rpl::variable<QString> _summary;
	QString _query;
	base::unique_qptr<Ui::PopupMenu> _menu;
	bool _rebuildScheduled = false;

};

} // namespace Settings
