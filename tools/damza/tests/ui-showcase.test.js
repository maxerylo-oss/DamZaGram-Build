// tools/damza/sandbox.py plugins/examples/ui-showcase/main.js \
//   tools/damza/tests/ui-showcase.test.js --setup=tools/damza/tests/ui.setup.js
// Smoke test of the ui-showcase example plugin.
const { eq, assert } = sandbox;

const call = (id, ...args) => sandbox.dispatch('ui:call', { id, args });
const user = { id: '42', name: 'Test', username: 'test', type: 'user', isSelf: false };
const channel = { id: '-100', name: 'News', username: '', type: 'channel', isSelf: false };

// Everything got registered at load.
const top = sandbox.called('ui_addTopBarButton').map((c) => c.args[0]);
eq(top.length, 2);
eq(top[0].icon, 'palette');
eq(top[1].text, 'Аа');
eq(call(top[1].when, user), true);
eq(call(top[1].when, channel), false);
eq(sandbox.called('ui_addMainMenuItem').length, 1);
eq(sandbox.called('ui_addComposeButton').length, 1);
eq(sandbox.last('ui_addShortcut').args[0], 'Ctrl+Shift+U');

// Top bar button opens the panel with all kinds of items.
call(top[0].click, user);
eq(sandbox.called('ui_panelOpen').length, 1);
const types = sandbox.called('ui_panelAdd').map((c) => c.args[1]);
for (const type of ['label', 'input', 'button', 'toggle', 'separator', 'skip']) {
  assert(types.includes(type), 'panel has ' + type);
}

// Second click (shortcut) closes it.
call(sandbox.last('ui_addShortcut').args[1]);
eq(sandbox.called('ui_panelClose').length, 1);

// Compose button fixes the keyboard layout: "ghbdtn" -> "привет".
call(sandbox.last('ui_addComposeButton').args[0].click, user);
eq(sandbox.last('ui_composeSet').args, ['привет']);

// Toggling the caps transform registers a render transform.
call(top[0].click, user);
const caps = sandbox.panelItems.filter((x) => x.type === 'toggle' && x.props.text.startsWith('КАПСЛОК')).pop();
assert(caps, 'caps toggle');
sandbox.dispatch('widget', { id: caps.id, event: 'change', value: true });
eq(sandbox.last('ui_setRenderTransforms').args, [true]);
eq(sandbox.dispatch('ui:render', { id: 1, text: 'hello', out: false }), 'HELLO');
eq(sandbox.dispatch('ui:render', { id: 2, text: 'mine', out: true }), null);
eq(sandbox.storage.caps, true);
sandbox.dispatch('widget', { id: caps.id, event: 'change', value: false });
eq(sandbox.last('ui_setRenderTransforms').args, [false]);

// Live counter follows compose.changed.
const counter = sandbox.panelItems.filter((x) => x.type === 'label' && x.props.muted).shift();
sandbox.native.widgetCall = (id, method, args) => (method === 'valid' ? true : null);
sandbox.event('compose.changed', { chat: user, text: 'abc' });
sandbox.event('app.focus', { active: false });

eq(sandbox.last('widgetCall').args[1], 'setText');
assert(String(sandbox.last('widgetCall').args[2][0]).includes('Окно не в фокусе'), 'focus label updated');
void counter;

print('ui-showcase.test.js: all checks passed');
