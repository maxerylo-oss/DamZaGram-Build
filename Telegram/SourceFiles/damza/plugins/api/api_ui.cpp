// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
//
// Module `ui` extensions: top bar / compose / main menu buttons, shortcuts,
// notifications, links, the message field, side panels and render
// transforms. State and client hooks live in damza/plugins/ui/.
#include "damza/plugins/plugin_api.h"

#include "damza/plugins/plugin_bridge.h"
#include "damza/plugins/plugin_instance.h"
#include "damza/plugins/ui/ui_state.h"
#include "damza/plugins/ui/ui_widgets.h"

namespace Damza::Plugins {
namespace {

[[nodiscard]] QString PluginId(not_null<Bridge*> bridge) {
	return bridge->instance()->manifest().id;
}

[[nodiscard]] UiExt::ButtonSpec ParseButton(
		not_null<Bridge*> bridge,
		const QVariantMap &map) {
	const auto pluginId = PluginId(bridge);
	return {
		.click = { pluginId, map.value(u"click"_q).toInt() },
		.when = { pluginId, map.value(u"when"_q).toInt() },
		.icon = map.value(u"icon"_q).toString(),
		.text = map.value(u"text"_q).toString(),
		.tooltip = map.value(u"tooltip"_q).toString(),
	};
}

} // namespace

DZG_API_MODULE({
	.name = "ui",
	.natives = [](not_null<Bridge*> bridge, std::vector<Js::NativeMethod> &out) {
		using Args = QVariantList;
		const auto str = [](const Args &a, int i) {
			return a.value(i).toString();
		};
		const auto num = [](const Args &a, int i) {
			return int(a.value(i).toDouble());
		};
		const auto map = [](const Args &a, int i) {
			return a.value(i).toMap();
		};
		out.push_back({ "ui_icons", [](const Args &) {
			return QVariant(UiExt::IconNames());
		} });
		out.push_back({ "ui_addTopBarButton", [=](const Args &a) {
			return QVariant(UiExt::AddTopBarButton(
				ParseButton(bridge, map(a, 0))));
		} });
		out.push_back({ "ui_addComposeButton", [=](const Args &a) {
			return QVariant(UiExt::AddComposeButton(
				ParseButton(bridge, map(a, 0))));
		} });
		out.push_back({ "ui_addMainMenuItem", [=](const Args &a) {
			const auto spec = map(a, 0);
			return QVariant(UiExt::AddMainMenuItem({
				.click = { PluginId(bridge), spec.value(u"click"_q).toInt() },
				.title = spec.value(u"title"_q).toString(),
				.icon = spec.value(u"icon"_q).toString(),
			}));
		} });
		out.push_back({ "ui_addShortcut", [=](const Args &a) {
			return QVariant(UiExt::AddShortcut(
				{ PluginId(bridge), num(a, 1) },
				str(a, 0)));
		} });
		out.push_back({ "ui_remove", [=](const Args &a) {
			return QVariant(UiExt::Remove(PluginId(bridge), num(a, 0)));
		} });
		out.push_back({ "ui_refreshButtons", [](const Args &) {
			UiExt::RefreshButtons();
			return QVariant();
		} });
		out.push_back({ "ui_notify", [=](const Args &a) {
			UiExt::Notify(PluginId(bridge), map(a, 0));
			return QVariant();
		} });
		out.push_back({ "ui_openLink", [=](const Args &a) {
			UiExt::OpenLink(str(a, 0));
			return QVariant();
		} });
		out.push_back({ "ui_composeGet", [](const Args &) {
			return UiExt::ComposeGet();
		} });
		out.push_back({ "ui_composeSet", [=](const Args &a) {
			return QVariant(UiExt::ComposeSet(str(a, 0)));
		} });
		out.push_back({ "ui_composeInsert", [=](const Args &a) {
			return QVariant(UiExt::ComposeInsert(str(a, 0)));
		} });
		out.push_back({ "ui_composeFocus", [](const Args &) {
			UiExt::ComposeFocus();
			return QVariant();
		} });
		out.push_back({ "ui_panelOpen", [=](const Args &a) {
			return UiExt::PanelOpen(bridge, map(a, 0));
		} });
		out.push_back({ "ui_panelAdd", [=](const Args &a) {
			return UiExt::PanelAdd(bridge, num(a, 0), str(a, 1), map(a, 2));
		} });
		out.push_back({ "ui_panelSet", [=](const Args &a) {
			return QVariant(UiExt::PanelSet(bridge, num(a, 0), map(a, 1)));
		} });
		out.push_back({ "ui_panelClear", [=](const Args &a) {
			return QVariant(UiExt::PanelClear(bridge, num(a, 0)));
		} });
		out.push_back({ "ui_panelClose", [=](const Args &a) {
			return QVariant(UiExt::PanelClose(bridge, num(a, 0)));
		} });
		out.push_back({ "ui_panelIsOpen", [=](const Args &a) {
			return QVariant(UiExt::PanelIsOpen(bridge, num(a, 0)));
		} });
		out.push_back({ "ui_setRenderTransforms", [=](const Args &a) {
			UiExt::SetRenderTransforms(PluginId(bridge), a.value(0).toBool());
			return QVariant();
		} });
		out.push_back({ "ui_refreshRender", [](const Args &) {
			UiExt::RefreshRender();
			return QVariant();
		} });
	},
	.prelude = [] {
		return QString::fromUtf8(R"DZG_JS(
	const ui = dzg.ui;
	const callbacks = new Map();
	let callbackSeq = 0;

	const str = (v) => ((v === undefined || v === null) ? '' : String(v));
	const keep = (fn) => {
		if (typeof fn !== 'function') return 0;
		const id = ++callbackSeq;
		callbacks.set(id, fn);
		return id;
	};
	const forget = (...ids) => {
		for (const id of ids) {
			if (id) callbacks.delete(id);
		}
	};
	const need = (fn, name) => {
		if (typeof fn !== 'function') {
			throw new TypeError(name + ' must be a function');
		}
		return fn;
	};
	const once = (fn) => {
		let done = false;
		return () => {
			if (done) return;
			done = true;
			fn();
		};
	};

	internal.on('ui:call', (p) => {
		const fn = callbacks.get(p && p.id);
		if (typeof fn !== 'function') return undefined;
		const result = internal.safe('ui callback', fn, ...((p && p.args) || []));
		return (result && typeof result.then === 'function') ? undefined : result;
	});
	internal.on('ui:drop', (p) => forget(...((p && p.ids) || [])));

	let iconList = null;
	const iconNames = () => {
		if (!iconList) iconList = native.ui_icons() || [];
		return iconList;
	};
	const checkIcon = (icon, where) => {
		const names = iconNames();
		if (icon && names.length && !names.includes(String(icon))) {
			console.warn(where + ': unknown icon "' + icon + '", see dzg.ui.icons()');
		}
	};
	const buttonSpec = (s, where) => {
		const spec = s || {};
		need(spec.onClick, where + ': onClick');
		if (!spec.icon && !spec.text) {
			throw new TypeError(where + ': icon or text is required');
		}
		checkIcon(spec.icon, where);
		return spec;
	};
	const addButton = (nativeName, s, where) => {
		const spec = buttonSpec(s, where);
		const click = keep(spec.onClick);
		const when = keep(spec.when);
		const id = native[nativeName]({
			icon: str(spec.icon),
			text: str(spec.text),
			tooltip: str(spec.tooltip),
			click,
			when,
		});
		if (!id) {
			forget(click, when);
			console.warn(where + ': the client refused the button');
			return () => {};
		}
		return once(() => {
			native.ui_remove(id);
			forget(click, when);
		});
	};

	Object.assign(ui, {
		icons() {
			return iconNames().slice();
		},
		addTopBarButton(spec) {
			return addButton('ui_addTopBarButton', spec, 'addTopBarButton');
		},
		addMainMenuItem(spec) {
			const s = spec || {};
			need(s.onClick, 'addMainMenuItem: onClick');
			if (!s.title) throw new TypeError('addMainMenuItem: title is required');
			checkIcon(s.icon, 'addMainMenuItem');
			const click = keep(s.onClick);
			const id = native.ui_addMainMenuItem({ title: str(s.title), icon: str(s.icon), click });
			if (!id) {
				forget(click);
				console.warn('addMainMenuItem: the client refused the item');
				return () => {};
			}
			return once(() => {
				native.ui_remove(id);
				forget(click);
			});
		},
		addShortcut(keys, fn) {
			need(fn, 'addShortcut: handler');
			const click = keep(fn);
			const id = native.ui_addShortcut(str(keys), click);
			if (!id) {
				forget(click);
				throw new Error('Invalid key sequence: "' + keys + '" (use one combination like "Ctrl+Shift+K")');
			}
			return once(() => {
				native.ui_remove(id);
				forget(click);
			});
		},
		notify(spec) {
			const s = (typeof spec === 'string') ? { text: spec } : (spec || {});
			native.ui_notify({
				title: str(s.title),
				text: str(s.text),
				click: keep(s.onClick),
				silent: !!s.silent,
			});
		},
		openLink(url) {
			native.ui_openLink(str(url));
		},
		refreshButtons() {
			native.ui_refreshButtons();
		},
	});

	ui.compose = {
		get() {
			return native.ui_composeGet() || null;
		},
		set(text) {
			return !!native.ui_composeSet(str(text));
		},
		insert(text) {
			return !!native.ui_composeInsert(str(text));
		},
		focus() {
			native.ui_composeFocus();
		},
		addButton(spec) {
			return addButton('ui_addComposeButton', spec, 'compose.addButton');
		},
	};

	let currentPanel = null;
	internal.on('ui:panelClosed', (p) => {
		if (currentPanel && p && currentPanel.id === p.id) {
			currentPanel.__markClosed();
		}
	});

	const makePanel = (id, contentId) => {
		let open = true;
		const closeHandlers = [];
		const created = new Set();
		const forgetWidgets = () => {
			for (const widgetId of created) internal.widgetHandlers.delete(widgetId);
			created.clear();
		};
		const add = (type, props, on) => {
			if (!open) return null;
			const widgetId = native.ui_panelAdd(id, type, props);
			if (!widgetId) return null;
			const widget = internal.wrapWidget(widgetId);
			created.add(widgetId);
			if (on && Object.keys(on).length) {
				internal.widgetHandlers.set(widgetId, { widget, on });
			}
			return widget;
		};
		const panel = {
			get id() { return id; },
			get widget() { return internal.wrapWidget(id); },
			get content() { return internal.wrapWidget(contentId); },
			setTitle(title) {
				if (open) native.ui_panelSet(id, { title: str(title) });
				return panel;
			},
			setWidth(width) {
				if (open) native.ui_panelSet(id, { width: Number(width) || 340 });
				return panel;
			},
			show() {
				if (open) native.ui_panelSet(id, { activate: true });
				return panel;
			},
			scrollToBottom() {
				if (open) native.ui_panelSet(id, { scrollToBottom: true });
				return panel;
			},
			clear() {
				if (open) {
					native.ui_panelClear(id);
					forgetWidgets();
				}
				return panel;
			},
			addLabel(text, options) {
				const o = options || {};
				return add('label', { text: str(text), bold: !!o.bold, muted: !!o.muted });
			},
			addButton(text, onClick, options) {
				need(onClick, 'panel.addButton: onClick');
				const o = options || {};
				checkIcon(o.icon, 'panel.addButton');
				return add('button', { text: str(text), style: str(o.style), icon: str(o.icon) }, {
					click: (widget) => onClick(widget),
				});
			},
			addInput(options) {
				const o = options || {};
				const on = {};
				if (typeof o.onChange === 'function') on.change = (widget, value) => o.onChange(str(value), widget);
				if (typeof o.onSubmit === 'function') on.submit = (widget, value) => o.onSubmit(str(value), widget);
				return add('input', {
					placeholder: str(o.placeholder),
					value: str(o.value),
					multiline: !!o.multiline,
				}, on);
			},
			addToggle(text, checked, onChange) {
				const on = (typeof onChange === 'function')
					? { change: (widget, value) => onChange(!!value, widget) }
					: {};
				return add('toggle', { text: str(text), checked: !!checked }, on);
			},
			addSeparator() {
				add('separator', {});
			},
			addSkip(px) {
				add('skip', { px: Number(px) || 0 });
			},
			close() {
				if (!open) return;
				native.ui_panelClose(id);
				panel.__markClosed();
			},
			on(event, fn) {
				if (event === 'close' && typeof fn === 'function') closeHandlers.push(fn);
				return panel;
			},
			isOpen() {
				return open;
			},
		};
		Object.defineProperty(panel, '__markClosed', {
			enumerable: false,
			value: () => {
				if (!open) return;
				open = false;
				forgetWidgets();
				if (currentPanel === panel) currentPanel = null;
				for (const fn of closeHandlers.slice()) internal.safe('panel.on(close)', fn, panel);
			},
		});
		return panel;
	};

	ui.panel = (options) => {
		const o = options || {};
		const result = native.ui_panelOpen({
			title: str(o.title || (dzg.plugin && dzg.plugin.name) || ''),
			width: Number(o.width) || 340,
		});
		if (!result || !result.id) {
			throw new Error('dzg.ui.panel: no main window with an open account');
		}
		if (currentPanel && currentPanel.id === result.id && currentPanel.isOpen()) {
			return currentPanel;
		}
		currentPanel = makePanel(result.id, result.content);
		return currentPanel;
	};

	const transforms = [];
	const syncTransforms = () => native.ui_setRenderTransforms(transforms.length > 0);
	Object.assign(dzg.messages, {
		addRenderTransform(fn) {
			need(fn, 'addRenderTransform');
			const entry = { fn };
			transforms.push(entry);
			syncTransforms();
			return once(() => {
				const index = transforms.indexOf(entry);
				if (index >= 0) transforms.splice(index, 1);
				syncTransforms();
			});
		},
		refreshRender() {
			native.ui_refreshRender();
		},
	});
	internal.on('ui:render', (message) => {
		let text = null;
		for (const entry of transforms.slice()) {
			const input = (text === null) ? message : Object.assign({}, message, { text });
			const result = internal.safe('renderTransform', entry.fn, input);
			if (typeof result === 'string') text = result;
		}
		return (text === message.text) ? null : text;
	});
)DZG_JS");
	},
	.appStarted = [] {
		UiExt::AppStarted();
	},
	.pluginUnloaded = [](const QString &pluginId) {
		UiExt::PluginUnloaded(pluginId);
	},
});

} // namespace Damza::Plugins
