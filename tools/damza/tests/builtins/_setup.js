// Shared --setup for the built-in plugin tests (tools/damza/tests/builtins/).
// Replaces the API v2 surface that touches the client (ui extensions,
// chats, messages, users, app, theme, http) with recording mocks, so the
// tests don't depend on which modules are already implemented.
// Tests reach the state through `globalThis.mock`.
(function () {
	let seq = 0;
	const peer = (id, name, type, extra) => Object.assign({
		id: String(id), tgId: Number(id), name, username: '', type, isSelf: false,
	}, extra || {});

	const self = peer(1, 'Я', 'user', { isSelf: true, username: 'wetakush' });
	const mock = {
		self,
		current: peer(100, 'Вася Пупкин', 'user', { username: 'vasya' }),
		chats: [],
		history: {},
		panels: [],
		shortcuts: [],
		topBar: [],
		mainMenu: [],
		composeButtons: [],
		notifications: [],
		toasts: [],
		alerts: [],
		prompts: [],
		promptAnswers: [],
		confirmAnswer: true,
		opened: [],
		links: [],
		sent: [],
		edited: [],
		read: [],
		compose: null,
		clipboard: '',
		night: true,
		theme: {},
		themeResets: 0,
		http: [],
		httpHandler: null,
		files: {},
		peer,
	};

	mock.chats = [
		Object.assign(peer(100, 'Вася Пупкин', 'user', { username: 'vasya' }), { unread: 3, mentions: 0, pinned: true, muted: false, archived: false, lastMessage: null }),
		Object.assign(peer(200, 'DamZaGram Chat', 'group', { username: 'damzachat' }), { unread: 0, mentions: 0, pinned: false, muted: true, archived: false, lastMessage: null }),
		Object.assign(peer(300, 'Новости', 'channel', { username: 'news' }), { unread: 12, mentions: 0, pinned: false, muted: false, archived: false, lastMessage: null }),
		Object.assign(peer(1, 'Избранное', 'user', { isSelf: true }), { unread: 0, mentions: 0, pinned: false, muted: false, archived: false, lastMessage: null }),
	];

	const findChat = (p) => {
		const id = (p && typeof p === 'object') ? String(p.id) : String(p);
		return mock.chats.find((c) => c.id === id) || (mock.current && mock.current.id === id ? mock.current : null);
	};

	mock.message = (over) => Object.assign({
		id: ++seq,
		peerId: mock.current.id,
		chat: mock.current,
		fromId: mock.current.id,
		fromName: mock.current.name,
		text: 'привет',
		date: Math.floor(Date.now() / 1000),
		out: false,
		service: false,
		post: false,
		pinned: false,
		deleted: false,
		replyToId: 0,
		views: 0,
		entities: [],
		editDate: 0,
		groupedId: '',
	}, over || {});

	const makePanel = (options) => {
		const state = { options, title: options.title, items: [], open: true, handlers: {} };
		const wrap = (item) => {
			const w = {
				id: ++seq,
				text() { return item.text; },
				setText(t) { item.text = String(t); },
				setVisible(v) { item.visible = !!v; },
				visible() { return item.visible !== false; },
				show() { item.visible = true; },
				hide() { item.visible = false; },
				valid() { return state.open && !item.destroyed; },
				setFocus() { item.focused = true; },
				setEnabled(v) { item.enabled = v; },
				destroy() { item.destroyed = true; },
				on() { return w; },
			};
			item.widget = w;
			state.items.push(item);
			return w;
		};
		const api = {
			widget: { id: ++seq, valid: () => state.open, setFocus() {}, raise() {}, setVisible() {} },
			setTitle(t) { state.title = String(t); },
			clear() { state.items.forEach((i) => { i.destroyed = true; }); state.items = []; },
			addLabel(text, o) { return wrap({ type: 'label', text: String(text), o: o || {} }); },
			addButton(text, onClick, o) { return wrap({ type: 'button', text: String(text), onClick, o: o || {} }); },
			addInput(o) { return wrap({ type: 'input', text: (o && o.value) || '', o: o || {} }); },
			addToggle(text, checked, onChange) { return wrap({ type: 'toggle', text: String(text), checked: !!checked, onChange }); },
			addSeparator() { state.items.push({ type: 'separator' }); },
			addSkip() { state.items.push({ type: 'skip' }); },
			close() {
				if (!state.open) return;
				state.open = false;
				for (const fn of state.handlers.close || []) fn();
			},
			on(event, fn) { (state.handlers[event] = state.handlers[event] || []).push(fn); return api; },
			isOpen() { return state.open; },
		};
		state.api = api;
		mock.panels.push(state);
		return api;
	};

	mock.lastPanel = () => mock.panels[mock.panels.length - 1] || null;
	mock.openPanels = () => mock.panels.filter((p) => p.open);
	mock.visible = (panel) => (panel || mock.lastPanel()).items.filter((i) => !i.destroyed && i.visible !== false);
	mock.panelText = (panel) => mock.visible(panel).map((i) => i.text || '').join('\n');
	mock.findItem = (panel, type, needle) => mock.visible(panel).find((i) => i.type === type && String(i.text).includes(needle));
	mock.click = (panel, needle) => {
		const item = mock.findItem(panel, 'button', needle);
		if (!item) throw new Error('No button containing "' + needle + '" in:\n' + mock.panelText(panel));
		item.onClick(item.widget);
		return item;
	};
	mock.input = (panel) => mock.visible(panel).find((i) => i.type === 'input');
	mock.type = (panel, text) => {
		const input = mock.input(panel);
		input.text = text;
		if (input.o.onChange) input.o.onChange(text);
	};
	mock.submit = (panel, text) => {
		const input = mock.input(panel);
		if (text !== undefined) input.text = text;
		if (input.o.onSubmit) input.o.onSubmit(input.text);
	};
	mock.tick = async (n) => { for (let i = 0; i < (n || 30); ++i) await null; };
	mock.command = (text, chat) => sandbox.dispatch('beforeSend', {
		text, chat: chat || mock.current, peerId: (chat || mock.current).id, replyTo: 0,
	});
	mock.menu = (kind, target) => sandbox.dispatch('menu:collect', { kind, target });
	mock.menuClick = (kind, target, needle) => {
		const items = mock.menu(kind, target);
		const item = items.find((i) => i.title.includes(needle));
		if (!item) throw new Error('No menu item "' + needle + '" in ' + JSON.stringify(items));
		sandbox.dispatch('menu:click', { kind, id: item.id, target });
		return item;
	};
	mock.shortcut = (keys) => {
		const s = mock.shortcuts.find((x) => x.keys === keys && !x.removed);
		if (!s) throw new Error('No shortcut ' + keys);
		s.fn();
	};

	const registry = (list, entry) => {
		list.push(entry);
		return () => { entry.removed = true; };
	};

	Object.assign(dzg.ui, {
		toast(text, title) { mock.toasts.push(title ? title + ': ' + text : String(text)); },
		alert(text, title) { mock.alerts.push({ text, title }); return Promise.resolve(); },
		confirm(text) { mock.alerts.push({ text, confirm: true }); return Promise.resolve(mock.confirmAnswer); },
		prompt(title, options) {
			mock.prompts.push({ title, options });
			const answer = mock.promptAnswers.length ? mock.promptAnswers.shift() : null;
			return Promise.resolve(answer);
		},
		icons() { return ['search', 'settings', 'info', 'stats', 'premium', 'star', 'schedule', 'silent', 'palette', 'chat', 'user', 'users', 'channel', 'bot', 'saved', 'code', 'night']; },
		addTopBarButton(spec) { return registry(mock.topBar, Object.assign({}, spec)); },
		addMainMenuItem(spec) { return registry(mock.mainMenu, Object.assign({}, spec)); },
		addShortcut(keys, fn) { return registry(mock.shortcuts, { keys, fn }); },
		notify(spec) { mock.notifications.push(Object.assign({}, spec)); },
		openLink(url) { mock.links.push(url); },
		panel(options) { return makePanel(Object.assign({ width: 340 }, options || {})); },
		compose: {
			get() { return mock.compose; },
			set(text) { if (!mock.current) return false; mock.compose = { chat: mock.current, text: String(text) }; return true; },
			insert(text) { if (!mock.current) return false; mock.compose = { chat: mock.current, text: ((mock.compose && mock.compose.text) || '') + text }; return true; },
			focus() { mock.composeFocused = true; },
			addButton(spec) { return registry(mock.composeButtons, Object.assign({}, spec)); },
		},
	});

	Object.assign(dzg.chats, {
		current() { return mock.current; },
		get(p) { return findChat(p); },
		open(p, messageId) { mock.opened.push({ peerId: String((p && p.id) || p), messageId: messageId || 0 }); return true; },
		list(options) {
			const o = options || {};
			return mock.chats.slice(0, o.limit || 100);
		},
		entry(p) { return mock.chats.find((c) => c.id === String((p && p.id) || p)) || null; },
		folders() { return []; },
		mute() {},
	});

	Object.assign(dzg.messages, {
		send(p, text, options) { mock.sent.push({ peerId: String((p && p.id) || p), text, options: options || {} }); return true; },
		edit(p, id, text) { mock.edited.push({ peerId: String((p && p.id) || p), id, text }); return true; },
		get(p, id) { return null; },
		history(p, options) {
			const id = String((p && p.id) || p);
			const o = options || {};
			const all = mock.history[id] || [];
			const limit = o.limit || 50;
			const before = o.offsetId ? all.filter((m) => m.id < o.offsetId) : all;
			return Promise.resolve(before.slice(Math.max(0, before.length - limit)));
		},
		markRead(p) { mock.read.push(String((p && p.id) || p)); },
		link(p, id) { return 'https://t.me/c/' + String((p && p.id) || p) + '/' + id; },
	});

	mock.plugins = [
		{ id: 'reminders', name: 'Напоминания', version: '1.0.0', enabled: true, loaded: true, builtin: true },
		{ id: 'auto-reply', name: 'Автоответчик', version: '1.0.0', enabled: false, loaded: false, builtin: true },
	];
	mock.managerOpened = 0;
	dzg.plugins = Object.assign(dzg.plugins || {}, {
		list() { return mock.plugins.map((p) => Object.assign({}, p)); },
		setEnabled(id, enabled) { const p = mock.plugins.find((x) => x.id === id); if (p) p.enabled = !!enabled; return !!p; },
		openManager() { mock.managerOpened++; },
		openDevTools() { mock.devToolsOpened = true; },
	});

	mock.searchResults = [];
	dzg.messages.search = (p, query, options) => {
		mock.lastSearch = { peer: p, query, options };
		return Promise.resolve(mock.searchResults);
	};

	dzg.users = Object.assign(dzg.users || {}, {
		self() { return mock.self; },
		contacts() { return []; },
	});

	Object.assign(dzg.app, {
		isNightMode() { return mock.night; },
		setNightMode(v) { mock.night = !!v; },
		restart() { mock.restarted = true; },
		language() { return { id: 'ru', baseId: '', name: 'Русский' }; },
	});

	Object.assign(dzg.theme, {
		set(name, color) { mock.theme[name] = color; return true; },
		setMany(map) { Object.assign(mock.theme, map); return true; },
		reset() { mock.theme = {}; mock.themeResets++; },
	});

	dzg.http.fetch = (url, options) => {
		mock.http.push({ url, options: options || {} });
		const handler = mock.httpHandler || (() => ({ status: 404, body: '' }));
		return Promise.resolve(handler(url, options || {})).then((r) => ({
			status: r.status,
			ok: r.status >= 200 && r.status < 300,
			headers: r.headers || {},
			url,
			text: () => Promise.resolve(typeof r.body === 'string' ? r.body : JSON.stringify(r.body)),
			json: () => Promise.resolve(typeof r.body === 'string' ? JSON.parse(r.body) : r.body),
		}));
	};

	Object.assign(dzg.clipboard, {
		get() { return mock.clipboard; },
		set(text) { mock.clipboard = String(text); },
	});
	Object.assign(dzg.fs, {
		write(path, text) { mock.files[path] = String(text); return true; },
		read(path) { return Object.prototype.hasOwnProperty.call(mock.files, path) ? mock.files[path] : null; },
		exists(path) { return Object.prototype.hasOwnProperty.call(mock.files, path); },
	});
	dzg.openUrl = (url) => { mock.links.push(url); };

	// Manifest defaults, like the client fills them in.
	mock.loadDefaults = (manifestPath) => {
		const manifest = JSON.parse(std.loadFile(manifestPath));
		for (const entry of manifest.settings || []) {
			if (!(entry.key in sandbox.settings)) sandbox.settings[entry.key] = entry.default;
		}
		return manifest;
	};

	// Settings written by the plugin are visible to it right away.
	sandbox.native.setSetting = (key, value) => {
		sandbox.settings[key] = value;
		sandbox.event('settings.changed', { key, value });
	};

	globalThis.mock = mock;
})();
