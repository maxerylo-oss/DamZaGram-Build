// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

#include "damza/plugins/plugin_js.h"

namespace Main {
class Session;
} // namespace Main

namespace Damza::Plugins {

class Bridge;

// An API module adds native functions and a JS fragment to every plugin.
// Each module lives in its own file under damza/plugins/api/ and registers
// itself with DZG_API_MODULE, so modules never touch each other's code.
//
// natives:  push Js::NativeMethod entries for one plugin. Prefix names with
//           the module name ("messages_history") to avoid collisions; the
//           lambdas may capture `bridge`, which outlives them.
// prelude:  JS source run once per plugin as the body of
//           function (native, dzg, internal) { ... } after the core prelude.
//           Extend `dzg.<module>` there; see plugin_prelude.cpp for
//           what `internal` provides.
// pluginUnloaded: per-plugin cleanup of module-wide registries.
// appStarted / sessionStarted: optional module-wide setup (subscriptions
//           that call Manager::emitEvent). Called once, not per plugin.
//           Keep subscriptions in Manager::Get().lifetime() (app-wide) or
//           session->lifetime(), never in a static rpl::lifetime.
struct ApiModule {
	const char *name = nullptr;
	Fn<void(not_null<Bridge*> bridge, std::vector<Js::NativeMethod> &out)> natives;
	Fn<QString()> prelude;
	Fn<void()> appStarted;
	Fn<void(not_null<Main::Session*> session)> sessionStarted;
	// Called right before a plugin's runtime is destroyed: drop anything
	// the module registered for that plugin (buttons, shortcuts, ...).
	Fn<void(const QString &pluginId)> pluginUnloaded;
};

[[nodiscard]] std::vector<ApiModule> &ApiModules();

struct ApiModuleRegistrar {
	explicit ApiModuleRegistrar(ApiModule module) {
		ApiModules().push_back(std::move(module));
	}
};

} // namespace Damza::Plugins

#define DZG_API_CONCAT_(a, b) a##b
#define DZG_API_CONCAT(a, b) DZG_API_CONCAT_(a, b)
#define DZG_API_MODULE(...) \
	static const ::Damza::Plugins::ApiModuleRegistrar \
		DZG_API_CONCAT(DzgApiModule_, __LINE__)(::Damza::Plugins::ApiModule __VA_ARGS__)
