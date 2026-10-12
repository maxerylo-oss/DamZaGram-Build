// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
//
// API module template: natives + JS fragment + module-wide events.
// Copy this file to start a new module.
#include "damza/plugins/plugin_api.h"

#include "core/application.h"
#include "damza/plugins/plugin_bridge.h"
#include "damza/plugins/plugin_manager.h"
#include "lang/lang_instance.h"
#include "window/themes/window_theme.h"

namespace Damza::Plugins {

DZG_API_MODULE({
	.name = "app",
	.natives = [](not_null<Bridge*> bridge, std::vector<Js::NativeMethod> &out) {
		using Args = QVariantList;
		out.push_back({ "app_language", [](const Args &) {
			return QVariant(QVariantMap{
				{ u"id"_q, Lang::GetInstance().id() },
				{ u"baseId"_q, Lang::GetInstance().baseId() },
				{ u"name"_q, Lang::GetInstance().nativeName() },
			});
		} });
		out.push_back({ "app_isNightMode", [](const Args &) {
			return QVariant(Window::Theme::IsNightMode());
		} });
		out.push_back({ "app_setNightMode", [](const Args &args) {
			const auto night = args.value(0).toBool();
			if (Window::Theme::IsNightMode() != night) {
				// Toggling destroys theme-dependent widgets: do it later.
				crl::on_main([] { Window::Theme::ToggleNightMode(); });
			}
			return QVariant();
		} });
		out.push_back({ "app_restart", [](const Args &) {
			crl::on_main([] { Core::Restart(); });
			return QVariant();
		} });
	},
	.prelude = [] {
		return QString::fromUtf8(R"DZG_JS(
	Object.assign(dzg.app, {
		language() { return native.app_language(); },
		isNightMode() { return !!native.app_isNightMode(); },
		setNightMode(night) { native.app_setNightMode(!!night); },
		restart() { native.app_restart(); },
	});
)DZG_JS");
	},
	.appStarted = [] {
		// Fires for night mode switches and for any other theme the user
		// applies, so plugins can put their colors back on top of it.
		Window::Theme::Background()->updates(
		) | rpl::filter([](const Window::Theme::BackgroundUpdate &update) {
			return update.paletteChanged();
		}) | rpl::on_next([](const Window::Theme::BackgroundUpdate &) {
			Bridge::ForgetThemeOverrides();
			if (Manager::Exists()) {
				Manager::Get().emitEvent(
					u"theme.changed"_q,
					QVariantMap{
						{ u"night"_q, Window::Theme::IsNightMode() },
					});
			}
		}, Manager::Get().lifetime());
	},
});

} // namespace Damza::Plugins
