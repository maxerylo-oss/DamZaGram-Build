// Test plugin for plugins.test.js: records plugins.changed events.
globalThis.pluginsEvents = [];
dzg.on('plugins.changed', (data) => globalThis.pluginsEvents.push(data));
