// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/plugins/plugin_api.h"

namespace Damza::Plugins {

std::vector<ApiModule> &ApiModules() {
	static auto result = std::vector<ApiModule>();
	return result;
}

} // namespace Damza::Plugins
