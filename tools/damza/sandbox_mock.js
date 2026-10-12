// Mock of the native bridge for tools/damza/sandbox.py.
// Every native not defined here is auto-stubbed: it records the call and
// returns `sandbox.returns[name]` (or null). Tests can override any native
// through `sandbox.native[name] = (...args) => value` before calling.
(function () {
	const calls = [];
	const timers = new Map();
	let nextId = 0;
	const returns = {};
	const base = {
		log(level, text) { print('[' + level + '] ' + text); },
		pluginInfo() { return { id: 'sandbox', name: 'Sandbox', version: '0.0.0', author: '', description: '', dir: '/tmp/sandbox', dataDir: '/tmp/sandbox-data/' }; },
		appInfo() { return { name: 'DamZaGram', version: '0.0.0', apiVersion: 2, platform: 'sandbox', os: 'sandbox', pluginsPath: '/tmp/', devMode: true }; },
		settings() { return Object.assign({}, sandbox.settings); },
		storageGet(key) { return Object.prototype.hasOwnProperty.call(sandbox.storage, key) ? sandbox.storage[key] : null; },
		storageSet(key, value) { sandbox.storage[key] = value; },
		storageRemove(key) { delete sandbox.storage[key]; },
		storageKeys() { return Object.keys(sandbox.storage); },
		startTimer(ms, repeat) { const id = ++nextId; timers.set(id, { ms, repeat }); return id; },
		stopTimer(id) { timers.delete(id); },
		showBox() { return ++nextId; },
		httpRequest() { return ++nextId; },
		nextRequestId() { return ++nextId; },
	};
	const native = new Proxy(base, {
		get(target, key) {
			return (...args) => {
				calls.push({ name: String(key), args });
				if (typeof sandbox.native[key] === 'function') return sandbox.native[key](...args);
				if (key in target) return target[key](...args);
				return Object.prototype.hasOwnProperty.call(returns, key) ? returns[key] : null;
			};
		},
	});
	globalThis.sandbox = {
		calls,
		returns,
		native: {},
		settings: {},
		storage: {},
		dispatch(type, payload) { return globalThis.__sandboxDispatch(type, payload); },
		event(name, data) { return globalThis.__sandboxDispatch('event', { name, data }); },
		settle(id, ok, value) { return globalThis.__sandboxDispatch('settle', { id, ok, value }); },
		runTimers() { for (const id of Array.from(timers.keys())) { const t = timers.get(id); if (!t) continue; if (!t.repeat) timers.delete(id); globalThis.__sandboxDispatch('timer', id); } },
		called(name) { return calls.filter((c) => c.name === name); },
		last(name) { const list = calls.filter((c) => c.name === name); return list.length ? list[list.length - 1] : null; },
		reset() { calls.length = 0; },
		assert(cond, message) { if (!cond) throw new Error('Assertion failed: ' + (message || '')); },
		eq(a, b, message) { const x = JSON.stringify(a), y = JSON.stringify(b); if (x !== y) throw new Error('Expected ' + y + ' but got ' + x + (message ? ' (' + message + ')' : '')); },
	};
	globalThis.__native = native;
})();
