// Test plugin for ui.test.js: records ui events.
globalThis.uiEvents = [];
for (const name of ['app.focus', 'compose.changed']) {
  dzg.on(name, (data) => globalThis.uiEvents.push({ name, data }));
}
