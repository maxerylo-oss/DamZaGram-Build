// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
//
// dzg.reactions: show the total reaction count of a post in its bottom info
// line, next to views and forwards. A plugin opts in with showTotal(true);
// the native bottom-info renderer then sums item->reactions() and appends it.
#include "damza/plugins/plugin_api.h"
#include "damza/plugins/plugins.h"

#include "damza/plugins/plugin_bridge.h"
#include "damza/plugins/plugin_instance.h"

#include "base/flat_set.h"

namespace Damza::Plugins {
namespace {

// Plugin ids that asked for the total-reactions line.
base::flat_set<QString> ShowTotalPlugins;

} // namespace

bool ShowReactionsTotal() {
	return !ShowTotalPlugins.empty();
}

DZG_API_MODULE({
	.name = "reactions",
	.natives = [](not_null<Bridge*> bridge, std::vector<Js::NativeMethod> &out) {
		const auto id = bridge->instance()->manifest().id;
		out.push_back({ "reactions_setShowTotal", [=](const QVariantList &args) {
			if (args.value(0).toBool()) {
				ShowTotalPlugins.emplace(id);
			} else {
				ShowTotalPlugins.remove(id);
			}
			return QVariant();
		} });
	},
	.prelude = [] {
		return QString::fromUtf8(R"DZG_JS(
	dzg.reactions = {
		// Показывать суммарное число реакций в строке под постом,
		// рядом с просмотрами и пересылками. true/false.
		showTotal(enable) { native.reactions_setShowTotal(enable !== false); },
	};
)DZG_JS");
	},
	.pluginUnloaded = [](const QString &id) {
		ShowTotalPlugins.remove(id);
	},
});

} // namespace Damza::Plugins
