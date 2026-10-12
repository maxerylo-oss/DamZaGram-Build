// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
//
// Module "plugins": manage other plugins from JS (list, enable, reload,
// install from a URL or a path, uninstall, evaluate code in another plugin)
// plus the Ctrl+Shift+P shortcut that opens the plugin manager.
#include "damza/plugins/plugin_api.h"

#include "damza/plugins/plugin_bridge.h"
#include "damza/plugins/plugin_install.h"
#include "damza/plugins/plugin_instance.h"
#include "damza/plugins/plugin_manager.h"
#include "damza/ui/plugin_boxes.h"
#include "damza/ui/plugin_devtools.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_session_controller.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QKeyEvent>
#include <QtWidgets/QApplication>

namespace Damza::Plugins {
namespace {

using Args = QVariantList;

[[nodiscard]] QString Str(const Args &args, int index) {
	return args.value(index).toString();
}

[[nodiscard]] QVariantMap Describe(const PluginState &state) {
	const auto &m = state.manifest;
	return {
		{ u"id"_q, m.id },
		{ u"name"_q, m.name },
		{ u"version"_q, m.version },
		{ u"author"_q, m.author },
		{ u"description"_q, m.description },
		{ u"dir"_q, m.dir },
		{ u"type"_q, m.type },
		{ u"enabled"_q, state.enabled },
		{ u"loaded"_q, state.loaded },
		{ u"builtin"_q, IsBuiltinDirectory(m.dir) },
		{ u"hasSettings"_q, !m.settings.isEmpty() },
		{ u"error"_q, state.error },
	};
}

[[nodiscard]] QVariantList DescribeAll() {
	auto result = QVariantList();
	if (Manager::Exists()) {
		for (const auto &state : Manager::Get().plugins()) {
			result.push_back(Describe(state));
		}
	}
	return result;
}

[[nodiscard]] QVariantMap DescribePackage(const Package &package) {
	return {
		{ u"id"_q, package.id() },
		{ u"name"_q, package.name() },
		{ u"version"_q, package.version() },
		{ u"author"_q, package.author() },
		{ u"description"_q, package.description() },
		{ u"files"_q, int(package.files.size()) },
		{ u"size"_q, double(package.totalSize()) },
		{ u"singleFile"_q, package.generatedManifest },
		{ u"source"_q, package.source },
		{ u"installed"_q, FindPlugin(package.id()) != nullptr },
	};
}

void Later(Fn<void()> callback) {
	crl::on_main([=] {
		if (Manager::Exists()) {
			callback();
		}
	});
}

[[nodiscard]] Fn<void(bool, QVariant)> Settler(
		not_null<Bridge*> bridge,
		int requestId) {
	const auto weak = QPointer<Bridge>(bridge.get());
	return [=](bool ok, QVariant value) {
		if (const auto strong = weak.data()) {
			strong->settle(requestId, ok, value);
		}
	};
}

[[nodiscard]] QString Signature() {
	auto result = QStringList();
	for (const auto &state : Manager::Get().plugins()) {
		result.push_back(u"%1:%2:%3:%4"_q.arg(
			state.manifest.id,
			state.manifest.version,
			state.enabled ? u"1"_q : u"0"_q,
			state.loaded ? u"1"_q : u"0"_q));
	}
	return result.join(';');
}

[[nodiscard]] bool InTextField() {
	for (auto w = QApplication::focusWidget(); w; w = w->parentWidget()) {
		if (dynamic_cast<Ui::InputField*>(w)) {
			return true;
		}
	}
	return false;
}

class ManagerShortcut final : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *object, QEvent *e) override {
		const auto type = e->type();
		if (type != QEvent::KeyPress && type != QEvent::ShortcutOverride) {
			return false;
		}
		const auto key = static_cast<QKeyEvent*>(e);
		const auto modifiers = key->modifiers() & (Qt::ShiftModifier
			| Qt::ControlModifier
			| Qt::AltModifier
			| Qt::MetaModifier);
		const auto isP = (key->key() == Qt::Key_P)
			|| (key->nativeVirtualKey() == Qt::Key_P);
		if (!isP || modifiers != (Qt::ControlModifier | Qt::ShiftModifier)) {
			return false;
		} else if (InTextField()) {
			// Ctrl+Shift+P formats a spoiler in message fields.
			return false;
		} else if (type == QEvent::ShortcutOverride) {
			e->accept();
			return true;
		} else if (!key->isAutoRepeat()) {
			Later([] {
				if (const auto controller = Manager::Get().activeController()) {
					PluginsUi::ShowPluginsSection(controller);
				}
			});
		}
		return true;
	}

};

QString LastSignature;
bool SignatureKnown = false;
bool ChangeScheduled = false;

void CheckChanged() {
	if (ChangeScheduled) {
		return;
	}
	ChangeScheduled = true;
	Later([] {
		ChangeScheduled = false;
		const auto now = Signature();
		if (SignatureKnown && now == LastSignature) {
			return;
		}
		const auto notify = SignatureKnown;
		SignatureKnown = true;
		LastSignature = now;
		if (notify) {
			Manager::Get().emitEventLater(
				u"plugins.changed"_q,
				QVariantMap{ { u"plugins"_q, DescribeAll() } });
		}
	});
}

} // namespace

DZG_API_MODULE({
	.name = "plugins",
	.natives = [](not_null<Bridge*> bridge, std::vector<Js::NativeMethod> &out) {
		out.push_back({ "plugins_path", [](const Args &) {
			return QVariant(Manager::Get().pluginsPath());
		} });
		out.push_back({ "plugins_list", [](const Args &) {
			return QVariant(DescribeAll());
		} });
		out.push_back({ "plugins_get", [](const Args &a) {
			const auto state = FindPlugin(Str(a, 0));
			return state ? QVariant(Describe(*state)) : QVariant();
		} });
		out.push_back({ "plugins_setEnabled", [](const Args &a) {
			const auto id = Str(a, 0);
			const auto enabled = a.value(1).toBool();
			if (!FindPlugin(id)) {
				return QVariant(false);
			}
			Later([=] { Manager::Get().setEnabled(id, enabled); });
			return QVariant(true);
		} });
		out.push_back({ "plugins_reload", [](const Args &a) {
			const auto id = Str(a, 0);
			if (id.isEmpty()) {
				Later([] { Manager::Get().reloadAll(); });
				return QVariant(true);
			} else if (!FindPlugin(id)) {
				return QVariant(false);
			}
			Later([=] { Manager::Get().reload(id); });
			return QVariant(true);
		} });
		out.push_back({ "plugins_log", [](const Args &a) {
			const auto plugin = Manager::Get().instance(Str(a, 0));
			if (!plugin) {
				return QVariant(QVariantList());
			}
			const auto &lines = plugin->logLines();
			const auto limit = std::max(a.value(1).toInt(), 1);
			auto result = QVariantList();
			for (auto i = std::max(int(lines.size()) - limit, 0)
				; i < lines.size()
				; ++i) {
				result.push_back(lines[i]);
			}
			return QVariant(result);
		} });
		out.push_back({ "plugins_settings", [](const Args &a) {
			return QVariant(
				Manager::Get().settingsValues(Str(a, 0)).toVariantMap());
		} });
		out.push_back({ "plugins_setSetting", [](const Args &a) {
			const auto id = Str(a, 0);
			const auto key = Str(a, 1);
			const auto value = QJsonValue::fromVariant(a.value(2));
			if (!FindPlugin(id) || key.isEmpty()) {
				return QVariant(false);
			}
			Later([=] { Manager::Get().setSettingValue(id, key, value); });
			return QVariant(true);
		} });
		out.push_back({ "plugins_eval", [=](const Args &a) {
			const auto id = Str(a, 0);
			const auto self = bridge->instance()->manifest().id;
			const auto plugin = Manager::Get().instance(id);
			auto error = QString();
			auto result = QString();
			if (id == self) {
				error = u"Use eval() for your own plugin."_q;
			} else if (!plugin) {
				error = u"Plugin is not loaded: %1"_q.arg(id);
			} else if (plugin->busy()) {
				error = u"Plugin is busy: %1"_q.arg(id);
			} else {
				result = plugin->evaluate(Str(a, 1), &error);
			}
			return QVariant(error.isEmpty()
				? QVariantMap{ { u"ok"_q, true }, { u"result"_q, result } }
				: QVariantMap{ { u"ok"_q, false }, { u"error"_q, error } });
		} });
		out.push_back({ "plugins_inspect", [=](const Args &a) {
			const auto source = Str(a, 0);
			const auto requestId = bridge->nextRequestId();
			const auto settle = Settler(bridge, requestId);
			Later([=] {
				DownloadPackage(source, [=](Package package) {
					if (!package.valid()) {
						settle(false, package.error.isEmpty()
							? u"Bad plugin package."_q
							: package.error);
					} else {
						settle(true, DescribePackage(package));
					}
				});
			});
			return QVariant(requestId);
		} });
		out.push_back({ "plugins_install", [=](const Args &a) {
			const auto source = Str(a, 0);
			const auto enable = a.value(1).toBool();
			const auto requestId = bridge->nextRequestId();
			const auto settle = Settler(bridge, requestId);
			Later([=] {
				DownloadPackage(source, [=](Package package) {
					Later([=] {
						const auto result = InstallPackage(package, enable);
						if (result.error.isEmpty()) {
							settle(true, result.id);
						} else {
							settle(false, result.error);
						}
					});
				});
			});
			return QVariant(requestId);
		} });
		out.push_back({ "plugins_promptInstall", [=](const Args &a) {
			const auto source = Str(a, 0);
			const auto requestId = bridge->nextRequestId();
			const auto settle = Settler(bridge, requestId);
			Later([=] {
				const auto controller = Manager::Get().activeController();
				if (!controller) {
					settle(false, u"No active window."_q);
					return;
				}
				const auto show = controller->uiShow();
				DownloadPackage(source, [=](Package package) {
					PluginsUi::ShowInstallBox(show, std::move(package));
					settle(true, QVariant());
				});
			});
			return QVariant(requestId);
		} });
		out.push_back({ "plugins_uninstall", [=](const Args &a) {
			const auto id = Str(a, 0);
			const auto removeData = a.value(1).toBool();
			const auto requestId = bridge->nextRequestId();
			const auto settle = Settler(bridge, requestId);
			Later([=] {
				auto error = QString();
				if (UninstallPlugin(id, removeData, &error)) {
					settle(true, QVariant());
				} else {
					settle(false, error);
				}
			});
			return QVariant(requestId);
		} });
		out.push_back({ "plugins_openManager", [](const Args &) {
			Later([] {
				if (const auto controller = Manager::Get().activeController()) {
					PluginsUi::ShowPluginsSection(controller);
				}
			});
			return QVariant();
		} });
		out.push_back({ "plugins_openDevTools", [](const Args &a) {
			const auto id = Str(a, 0);
			Later([=] { PluginsUi::ShowDevTools(id); });
			return QVariant();
		} });
	},
	.prelude = [] {
		return QString::fromUtf8(R"DZG_JS(
	const self = () => dzg.plugin.id;
	const pluginId = (value) => {
		if (value === undefined || value === null || value === '') return self();
		if (typeof value === 'object') return String(value.id || '');
		return String(value);
	};
	const plugins = {
		get path() { return native.plugins_path(); },
		list() { return native.plugins_list() || []; },
		get(id) { return native.plugins_get(pluginId(id)) || null; },
		setEnabled(id, enabled) {
			return !!native.plugins_setEnabled(pluginId(id), !!enabled);
		},
		enable(id) { return plugins.setEnabled(id, true); },
		disable(id) { return plugins.setEnabled(id, false); },
		reload(id) { return !!native.plugins_reload(pluginId(id)); },
		reloadAll() { return !!native.plugins_reload(''); },
		log(id, limit) {
			return native.plugins_log(pluginId(id), Math.max(1, Number(limit) || 100)) || [];
		},
		settings(id) { return native.plugins_settings(pluginId(id)) || {}; },
		setSetting(id, key, value) {
			return !!native.plugins_setSetting(pluginId(id), String(key), value);
		},
		evaluate(id, code) {
			const result = native.plugins_eval(pluginId(id), String(code)) || {};
			if (!result.ok) throw new Error(result.error || 'evaluate failed');
			return result.result;
		},
		inspect(source) {
			return internal.request(() => native.plugins_inspect(String(source)));
		},
		install(source, options) {
			const enable = !(options && options.enable === false);
			return internal.request(() => native.plugins_install(String(source), enable));
		},
		promptInstall(source) {
			return internal.request(() => native.plugins_promptInstall(String(source)));
		},
		uninstall(id, options) {
			const removeData = !!(options && options.removeData);
			return internal.request(() => native.plugins_uninstall(pluginId(id), removeData));
		},
		openManager() { native.plugins_openManager(); },
		openDevTools(id) { native.plugins_openDevTools(id ? pluginId(id) : ''); },
	};
	dzg.plugins = plugins;
)DZG_JS");
	},
	.appStarted = [] {
		if (const auto app = QCoreApplication::instance()) {
			app->installEventFilter(new ManagerShortcut(app));
		}
		Manager::Get().pluginsChanges(
		) | rpl::on_next([] {
			CheckChanged();
		}, Manager::Get().lifetime());
	},
});

} // namespace Damza::Plugins
