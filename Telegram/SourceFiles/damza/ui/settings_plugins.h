// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

#include "settings/settings_common.h"
#include "settings/settings_common_session.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

class DamzaPlugins : public Section<DamzaPlugins> {
public:
	DamzaPlugins(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
	void rebuild(not_null<Ui::VerticalLayout*> list);

	const not_null<Window::SessionController*> _controller;

};

} // namespace Settings
