// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/plugins/plugin_prelude.h"

namespace Damza::Plugins {

// The core of the public `dzg` API, built on top of the native bridge.
// API modules (damza/plugins/api/) extend it through `internal`:
//   internal.native            the native bridge (also passed directly)
//   internal.safe(where, fn, ...args)  call plugin code, log exceptions
//   internal.report(where, error)      log an exception
//   internal.request(start)    Promise settled by Bridge::settle(id, ...);
//                              start() must return the request id
//   internal.on(type, fn)      handle a dispatch type sent from C++
//   internal.emit(name, data)  fire a `dzg.on` event from JS
//   internal.toPeerId(x)       Peer | string -> string id
//   internal.wrapWidget(id)    numeric widget id -> Widget object
//   internal.wrapWidgets(ids)  array version
//   internal.widgetHandlers    Map id -> { widget, on } for created widgets
//   internal.updateHooks()     re-announce filter / send hooks to C++
//   internal.hooks             { filters, sendHooks, commands, menus }
// Keep in sync with docs/plugins/types/*.d.ts.
QString Prelude() {
	return QString::fromUtf8(R"DZG_PRELUDE(
(function (native) {
	'use strict';

	const handlers = new Map();
	const dispatchHandlers = new Map();
	const timers = new Map();
	const pending = new Map();
	const widgetHandlers = new Map();
	const filters = [];
	const sendHooks = [];
	const commands = new Map();
	const menus = { message: [], chat: [] };
	const modules = new Map();
	let menuSeq = 0;

	const format = (args) => args.map((a) => {
		if (typeof a === 'string') return a;
		if (a instanceof Error) return a.stack ? (a + '\n' + a.stack) : String(a);
		try { return JSON.stringify(a); } catch (e) { return String(a); }
	}).join(' ');

	const report = (where, e) => {
		const text = (e && e.stack) ? (e + '\n' + e.stack) : String(e);
		native.log('error', where + ': ' + text);
	};

	const safe = (where, fn, ...args) => {
		try {
			const result = fn(...args);
			if (result && typeof result.then === 'function') {
				result.then(undefined, (e) => report(where, e));
			}
			return result;
		} catch (e) {
			report(where, e);
			return undefined;
		}
	};

	const updateHooks = () => native.setHooks(
		filters.length > 0,
		sendHooks.length > 0 || commands.size > 0);

	const remover = (list, item, after) => () => {
		const index = list.indexOf(item);
		if (index >= 0) list.splice(index, 1);
		if (after) after();
	};

	const console = {
		log: (...a) => native.log('info', format(a)),
		info: (...a) => native.log('info', format(a)),
		debug: (...a) => native.log('debug', format(a)),
		warn: (...a) => native.log('warn', format(a)),
		error: (...a) => native.log('error', format(a)),
	};

	const startTimer = (fn, ms, repeat, args) => {
		if (typeof fn !== 'function') throw new TypeError('Timer callback must be a function');
		const id = native.startTimer(Math.max(0, Number(ms) || 0), repeat);
		timers.set(id, { fn, repeat, args });
		return id;
	};
	const stopTimer = (id) => {
		if (timers.delete(id)) native.stopTimer(id);
	};

	const request = (start) => new Promise((resolve, reject) => {
		const id = start();
		if (!id) {
			reject(new Error('Request was not started'));
			return;
		}
		pending.set(id, { resolve, reject });
	});

	const toPeerId = (value) => {
		if (value === undefined || value === null) return '';
		if (typeof value === 'object') return String(value.peerId || value.id || '');
		return String(value);
	};

	// Widgets are numeric ids on the native side.
	const widgetProto = {};
	const plainMethods = [
		'valid', 'className', 'classChain', 'name', 'setName',
		'geometry', 'globalGeometry', 'setGeometry', 'move', 'resize',
		'visible', 'setVisible', 'show', 'hide', 'raise', 'lower', 'update',
		'setOpacity', 'setEnabled', 'setFocus', 'destroy',
		'prop', 'setProp', 'propNames', 'click', 'typeText', 'grab',
		'text', 'setText', 'dump',
	];
	for (const method of plainMethods) {
		widgetProto[method] = function (...args) {
			return native.widgetCall(this.id, method, args);
		};
	}
	const wrapWidget = (id) => (id ? Object.freeze(Object.create(widgetProto, {
		id: { value: id, enumerable: true },
	})) : null);
	const wrapWidgets = (ids) => (ids || []).map(wrapWidget).filter(Boolean);
	for (const method of ['parentWidget', 'window', 'childAt']) {
		widgetProto[method] = function (...args) {
			return wrapWidget(native.widgetCall(this.id, method, args));
		};
	}
	for (const method of ['childWidgets', 'find']) {
		widgetProto[method] = function (...args) {
			return wrapWidgets(native.widgetCall(this.id, method, args));
		};
	}
	widgetProto.on = function (event, fn) {
		const entry = widgetHandlers.get(this.id) || { widget: this, on: {} };
		entry.on[event] = fn;
		widgetHandlers.set(this.id, entry);
		return this;
	};
	widgetProto.toString = function () {
		return '[Widget ' + this.id + ']';
	};

	const plugin = native.pluginInfo();

	const dzg = {
		version: 2,
		plugin,
		app: native.appInfo(),

		on(event, fn) {
			if (!handlers.has(event)) handlers.set(event, []);
			handlers.get(event).push(fn);
			return () => dzg.off(event, fn);
		},
		off(event, fn) {
			const list = handlers.get(event);
			if (!list) return;
			const index = list.indexOf(fn);
			if (index >= 0) list.splice(index, 1);
		},
		once(event, fn) {
			const off = dzg.on(event, (data) => { off(); fn(data); });
			return off;
		},

		log: console.log,

		require(path) {
			if (modules.has(path)) return modules.get(path).exports;
			const code = native.readPluginFile(path);
			if (typeof code !== 'string') {
				throw new Error('Module not found: ' + path);
			}
			const module = { exports: {} };
			modules.set(path, module);
			const fn = new Function('module', 'exports', 'require', 'dzg', code);
			fn(module, module.exports, dzg.require, dzg);
			return module.exports;
		},

		storage: {
			get(key, fallback) {
				const value = native.storageGet(String(key));
				return (value === undefined || value === null) ? fallback : value;
			},
			set(key, value) { native.storageSet(String(key), value); },
			remove(key) { native.storageRemove(String(key)); },
			keys() { return native.storageKeys(); },
		},

		settings: {
			get(key) { return native.settings()[key]; },
			all() { return native.settings(); },
			set(key, value) { native.setSetting(String(key), value); },
		},

		ui: {
			toast(text, title) { native.toast(String(text), title ? String(title) : ''); },
			box(spec) {
				return request(() => native.showBox(spec || {}));
			},
			alert(text, title) {
				return dzg.ui.box({ title, text, buttons: [{ id: 'ok', text: 'OK' }] })
					.then(() => undefined);
			},
			confirm(text, options) {
				const o = options || {};
				return dzg.ui.box({
					title: o.title,
					text,
					buttons: [
						{ id: 'ok', text: o.confirmText || 'OK', danger: !!o.danger },
						{ id: 'cancel', text: o.cancelText || 'Cancel' },
					],
				}).then((r) => r.button === 'ok');
			},
			prompt(title, options) {
				const o = options || {};
				return dzg.ui.box({
					title,
					text: o.text,
					inputs: [{ id: 'value', placeholder: o.placeholder || '', value: o.value || '', multiline: !!o.multiline }],
					buttons: [
						{ id: 'ok', text: o.confirmText || 'OK' },
						{ id: 'cancel', text: o.cancelText || 'Cancel' },
					],
				}).then((r) => (r.button === 'ok') ? r.values.value : null);
			},
			addMessageMenuItem(item) {
				const entry = Object.assign({ id: 'm' + (++menuSeq) }, item);
				menus.message.push(entry);
				return remover(menus.message, entry);
			},
			addChatMenuItem(item) {
				const entry = Object.assign({ id: 'c' + (++menuSeq) }, item);
				menus.chat.push(entry);
				return remover(menus.chat, entry);
			},
			mainWindow() { return wrapWidget(native.mainWindow()); },
			focused() { return wrapWidget(native.focusedWidget()); },
			find(className, name) { return wrapWidgets(native.findWidgets(className || '', name || '')); },
			create(type, parent, props) {
				const p = Object.assign({}, props || {});
				const on = Object.assign({}, p.on || {});
				delete p.on;
				if (typeof p.onClick === 'function') { on.click = p.onClick; delete p.onClick; }
				if (typeof p.onChange === 'function') { on.change = p.onChange; delete p.onChange; }
				if (typeof p.onSubmit === 'function') { on.submit = p.onSubmit; delete p.onSubmit; }
				const widget = wrapWidget(native.createWidget(parent ? parent.id : 0, String(type), p));
				if (widget) widgetHandlers.set(widget.id, { widget, on });
				return widget;
			},
			repaint() { native.repaintAll(); },
		},

		theme: {
			set(name, color) { return native.setThemeColor(String(name), String(color)); },
			setMany(map) {
				let ok = true;
				for (const name of Object.keys(map || {})) {
					ok = native.setThemeColor(name, String(map[name])) && ok;
				}
				return ok;
			},
			reset() { native.resetTheme(); },
			get(name) { return native.themeColor(String(name)); },
		},

		chats: {
			current() { return native.currentChat(); },
			get(peerId) { return native.getPeer(toPeerId(peerId)); },
			find(username) { return native.findPeer(String(username).replace(/^@/, '')); },
			open(peerId, messageId) { return native.openChat(toPeerId(peerId), Number(messageId) || 0); },
		},

		messages: {
			send(peerId, text, options) {
				return native.sendMessage(toPeerId(peerId), String(text), options || {});
			},
			edit(peerId, messageId, text) {
				return native.editMessage(toPeerId(peerId), Number(messageId), String(text));
			},
			delete(peerId, ids, options) {
				const list = Array.isArray(ids) ? ids : [ids];
				return native.deleteMessages(toPeerId(peerId), list.map(Number), !!(options && options.revoke));
			},
			get(peerId, messageId) {
				return native.getMessage(toPeerId(peerId), Number(messageId));
			},
			loaded(peerId, limit) {
				return native.loadedMessages(toPeerId(peerId), Number(limit) || 100);
			},
			addFilter(fn) {
				filters.push(fn);
				updateHooks();
				native.refilter();
				return remover(filters, fn, () => { updateHooks(); native.refilter(); });
			},
			onBeforeSend(fn) {
				sendHooks.push(fn);
				updateHooks();
				return remover(sendHooks, fn, updateHooks);
			},
			refilter() { native.refilter(); },
		},

		commands: {
			register(name, spec) {
				const key = String(name).replace(/^\//, '').toLowerCase();
				const entry = (typeof spec === 'function') ? { run: spec } : Object.assign({}, spec);
				commands.set(key, entry);
				updateHooks();
				return () => { commands.delete(key); updateHooks(); };
			},
			list() {
				return Array.from(commands.entries()).map(([name, c]) => ({ name, description: c.description || '' }));
			},
		},

		http: {
			fetch(url, options) {
				const o = options || {};
				let body = o.body;
				const headers = Object.assign({}, o.headers || {});
				if (o.json !== undefined) {
					body = JSON.stringify(o.json);
					if (!headers['Content-Type'] && !headers['content-type']) {
						headers['Content-Type'] = 'application/json';
					}
				}
				return request(() => native.httpRequest({
					url: String(url),
					method: (o.method || 'GET').toUpperCase(),
					headers,
					body: (body === undefined || body === null) ? '' : String(body),
					timeout: Number(o.timeout) || 30000,
				})).then((r) => ({
					status: r.status,
					ok: r.status >= 200 && r.status < 300,
					headers: r.headers,
					url: r.url,
					text: () => Promise.resolve(r.body),
					json: () => Promise.resolve().then(() => JSON.parse(r.body)),
				}));
			},
		},

		fs: {
			read(path) { return native.readFile(String(path)); },
			write(path, text) { return native.writeFile(String(path), String(text)); },
			list(path) { return native.listDir(String(path)); },
			exists(path) { return native.fileExists(String(path)); },
		},

		clipboard: {
			get() { return native.clipboardText(); },
			set(text) { native.setClipboardText(String(text)); },
		},

		openUrl(url) { native.openUrl(String(url)); },
	};

	const emit = (name, data) => {
		const list = handlers.get(name);
		if (!list) return;
		for (const fn of list.slice()) safe('on(' + name + ')', fn, data);
	};

	const settle = (id, ok, value) => {
		const entry = pending.get(id);
		if (!entry) return;
		pending.delete(id);
		if (ok) {
			entry.resolve(value);
		} else {
			entry.reject((value instanceof Error) ? value : new Error(String(value)));
		}
	};

	const runCommand = (payload) => {
		const text = String(payload.text || '');
		const match = /^\/([A-Za-z0-9_]+)(?:@\S+)?(?:\s+([\s\S]*))?$/.exec(text);
		if (!match) return false;
		const command = commands.get(match[1].toLowerCase());
		if (!command || typeof command.run !== 'function') return false;
		const rest = (match[2] || '').trim();
		safe('command /' + match[1], command.run, {
			args: rest.length ? rest.split(/\s+/) : [],
			rest,
			text,
			chat: payload.chat,
			replyTo: payload.replyTo,
		});
		return true;
	};

	const internal = {
		native,
		safe,
		report,
		request,
		emit,
		toPeerId,
		wrapWidget,
		wrapWidgets,
		widgetHandlers,
		updateHooks,
		hooks: { filters, sendHooks, commands, menus },
		on(type, fn) { dispatchHandlers.set(type, fn); },
	};

	globalThis.__dzgDispatch = function (type, payload) {
		switch (type) {
		case 'event':
			emit(payload.name, payload.data);
			return undefined;
		case 'timer': {
			const timer = timers.get(payload);
			if (!timer) return undefined;
			if (!timer.repeat) timers.delete(payload);
			safe('timer', timer.fn, ...(timer.args || []));
			return undefined;
		}
		case 'settle':
			settle(payload.id, payload.ok, payload.value);
			return undefined;
		case 'filter':
			for (const fn of filters) {
				if (safe('filter', fn, payload) === true) return true;
			}
			return false;
		case 'beforeSend': {
			if (runCommand(payload)) return { cancel: true };
			let text = payload.text;
			for (const fn of sendHooks.slice()) {
				const result = safe('onBeforeSend', fn, Object.assign({}, payload, { text }));
				if (result === false || (result && result.cancel)) return { cancel: true };
				if (typeof result === 'string') text = result;
				else if (result && typeof result.text === 'string') text = result.text;
			}
			return { text };
		}
		case 'menu:collect': {
			const list = menus[payload.kind] || [];
			const result = [];
			for (const m of list) {
				if (m.when && !safe('menu.when', m.when, payload.target)) continue;
				const title = (typeof m.title === 'function') ? safe('menu.title', m.title, payload.target) : m.title;
				if (title) result.push({ id: m.id, title: String(title) });
			}
			return result;
		}
		case 'menu:click': {
			const list = menus[payload.kind] || [];
			const m = list.find((x) => x.id === payload.id);
			if (m && m.onClick) safe('menu.onClick', m.onClick, payload.target);
			return undefined;
		}
		case 'widget': {
			const entry = widgetHandlers.get(payload.id);
			if (!entry) return undefined;
			if (payload.event === 'destroyed') {
				widgetHandlers.delete(payload.id);
				const fn = entry.on.destroyed;
				if (fn) safe('widget.destroyed', fn, entry.widget);
				return undefined;
			}
			const fn = entry.on[payload.event];
			if (fn) safe('widget.' + payload.event, fn, entry.widget, payload.value);
			return undefined;
		}
		case 'unload':
			emit('unload', undefined);
			for (const id of timers.keys()) native.stopTimer(id);
			timers.clear();
			return undefined;
		}
		const handler = dispatchHandlers.get(type);
		if (handler) {
			try {
				return handler(payload);
			} catch (e) {
				report('dispatch ' + type, e);
			}
		}
		return undefined;
	};

	globalThis.dzg = dzg;
	globalThis.__dzgInternal = internal;
	globalThis.console = console;
	globalThis.setTimeout = (fn, ms, ...args) => startTimer(fn, ms, false, args);
	globalThis.setInterval = (fn, ms, ...args) => startTimer(fn, ms, true, args);
	globalThis.clearTimeout = stopTimer;
	globalThis.clearInterval = stopTimer;
	if (typeof globalThis.queueMicrotask !== 'function') {
		globalThis.queueMicrotask = (fn) => { Promise.resolve().then(fn); };
	}
})(globalThis.__native);
)DZG_PRELUDE");
}

} // namespace Damza::Plugins
