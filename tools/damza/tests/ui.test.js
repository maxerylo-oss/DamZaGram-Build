// tools/damza/sandbox.py tools/damza/tests/ui.plugin.js tools/damza/tests/ui.test.js
// Covers the JS half of the `ui` module extensions (api_ui.cpp).
const { eq, assert } = sandbox;

const chat = { id: '777', tgId: 777, name: 'Pavel', username: 'durov', type: 'user', isSelf: false };

const throws = (fn, text) => {
  try {
    fn();
  } catch (e) {
    if (text) assert(String(e.message).includes(text), 'message ' + e.message);
    return;
  }
  throw new Error('Expected an exception' + (text ? ': ' + text : ''));
};
let nextId = 100;
const ids = (name) => { sandbox.native[name] = () => ++nextId; };
const call = (id, ...args) => sandbox.dispatch('ui:call', { id, args });

// --- icons()
sandbox.native.ui_icons = () => ['search', 'settings', 'palette'];
eq(dzg.ui.icons(), ['search', 'settings', 'palette']);
const icons = dzg.ui.icons();
icons.push('mutated');
eq(dzg.ui.icons().length, 3, 'icons() returns a copy');

// --- addTopBarButton
ids('ui_addTopBarButton');
let clicked = null;
let whenArg = null;
const offTop = dzg.ui.addTopBarButton({
  icon: 'search',
  tooltip: 'Find',
  when: (c) => { whenArg = c; return c.type === 'user'; },
  onClick: (c) => { clicked = c; },
});
assert(typeof offTop === 'function', 'remover');
const topSpec = sandbox.last('ui_addTopBarButton').args[0];
eq(topSpec.icon, 'search');
eq(topSpec.text, '');
eq(topSpec.tooltip, 'Find');
assert(topSpec.click > 0 && topSpec.when > 0 && topSpec.click !== topSpec.when, 'callback ids');
call(topSpec.click, chat);
eq(clicked, chat);
eq(call(topSpec.when, chat), true);
eq(whenArg, chat);
eq(call(topSpec.when, Object.assign({}, chat, { type: 'channel' })), false);
const topId = nextId;
offTop();
eq(sandbox.last('ui_remove').args, [topId]);
offTop();
eq(sandbox.called('ui_remove').length, 1, 'remover runs once');
clicked = null;
eq(call(topSpec.click, chat), undefined);
eq(clicked, null, 'callback forgotten after removal');

// Text-only button without `when`.
dzg.ui.addTopBarButton({ text: 'Aa', onClick() {} });
eq(sandbox.last('ui_addTopBarButton').args[0].when, 0);
eq(sandbox.last('ui_addTopBarButton').args[0].text, 'Aa');

// Validation.
throws(() => dzg.ui.addTopBarButton({ icon: 'search' }), 'onClick');
throws(() => dzg.ui.addTopBarButton({ onClick() {} }), 'icon or text');
sandbox.native.ui_addTopBarButton = () => 0;
const refused = dzg.ui.addTopBarButton({ icon: 'search', onClick() {} });
assert(typeof refused === 'function', 'refused registration still returns a remover');
refused();
assert(String(sandbox.last('log').args[1]).includes('refused'), 'refusal is logged');

// Errors inside callbacks are reported, not thrown.
ids('ui_addTopBarButton');
dzg.ui.addTopBarButton({ icon: 'search', onClick() { throw new Error('boom'); } });
const errorsBefore = sandbox.called('log').filter((c) => c.args[0] === 'error').length;
call(sandbox.last('ui_addTopBarButton').args[0].click, chat);
eq(sandbox.called('log').filter((c) => c.args[0] === 'error').length, errorsBefore + 1);

// --- addMainMenuItem
ids('ui_addMainMenuItem');
let menuClicks = 0;
const offMenu = dzg.ui.addMainMenuItem({ title: 'My plugin', icon: 'settings', onClick: () => ++menuClicks });
const menuSpec = sandbox.last('ui_addMainMenuItem').args[0];
eq(menuSpec.title, 'My plugin');
eq(menuSpec.icon, 'settings');
call(menuSpec.click);
eq(menuClicks, 1);
offMenu();
eq(sandbox.last('ui_remove').args, [nextId]);
throws(() => dzg.ui.addMainMenuItem({ onClick() {} }), 'title');

// --- addShortcut
ids('ui_addShortcut');
let pressed = 0;
const offKey = dzg.ui.addShortcut('Ctrl+Shift+K', () => ++pressed);
eq(sandbox.last('ui_addShortcut').args[0], 'Ctrl+Shift+K');
call(sandbox.last('ui_addShortcut').args[1]);
eq(pressed, 1);
offKey();
sandbox.native.ui_addShortcut = () => 0;
throws(() => dzg.ui.addShortcut('Ctrl+Shift+K, Ctrl+C', () => {}), 'Invalid key sequence');
throws(() => dzg.ui.addShortcut('Ctrl+Shift+K'), 'handler');

// --- notify
let notified = 0;
dzg.ui.notify({ title: 'T', text: 'Body', onClick: () => ++notified });
const notifySpec = sandbox.last('ui_notify').args[0];
eq(notifySpec.title, 'T');
eq(notifySpec.text, 'Body');
eq(notifySpec.silent, false);
assert(notifySpec.click > 0, 'click id');
call(notifySpec.click);
eq(notified, 1);
sandbox.dispatch('ui:drop', { ids: [notifySpec.click] });
call(notifySpec.click);
eq(notified, 1, 'dropped after the toast is gone');
dzg.ui.notify('Just text');
eq(sandbox.last('ui_notify').args[0], { title: '', text: 'Just text', click: 0, silent: false });
dzg.ui.notify({ text: 'quiet', silent: 1 });
eq(sandbox.last('ui_notify').args[0].silent, true);

// --- openLink / refreshButtons
dzg.ui.openLink('https://t.me/wetakush');
eq(sandbox.last('ui_openLink').args, ['https://t.me/wetakush']);
dzg.ui.refreshButtons();
assert(sandbox.last('ui_refreshButtons'), 'refreshButtons');

// --- compose
sandbox.returns.ui_composeGet = null;
eq(dzg.ui.compose.get(), null);
sandbox.returns.ui_composeGet = { chat, text: 'hi' };
eq(dzg.ui.compose.get().text, 'hi');
sandbox.returns.ui_composeSet = true;
eq(dzg.ui.compose.set('new text'), true);
eq(sandbox.last('ui_composeSet').args, ['new text']);
sandbox.returns.ui_composeInsert = false;
eq(dzg.ui.compose.insert(42), false);
eq(sandbox.last('ui_composeInsert').args, ['42']);
dzg.ui.compose.focus();
assert(sandbox.last('ui_composeFocus'), 'focus');
ids('ui_addComposeButton');
let composeClicked = null;
const offCompose = dzg.ui.compose.addButton({ icon: 'palette', tooltip: 'Fix', onClick: (c) => { composeClicked = c; } });
const composeSpec = sandbox.last('ui_addComposeButton').args[0];
eq(composeSpec.tooltip, 'Fix');
call(composeSpec.click, chat);
eq(composeClicked, chat);
offCompose();
eq(sandbox.last('ui_remove').args, [nextId]);

// --- panel
sandbox.native.ui_panelOpen = () => ({ id: 11, content: 12, reused: false });
ids('ui_panelAdd');
sandbox.returns.ui_panelClose = true;
const panel = dzg.ui.panel({ title: 'Panel', width: 400 });
eq(sandbox.last('ui_panelOpen').args[0], { title: 'Panel', width: 400 });
eq(panel.id, 11);
eq(panel.widget.id, 11);
eq(panel.content.id, 12);
assert(panel.isOpen(), 'open');
assert(dzg.ui.panel() === panel, 'one panel per plugin');
eq(sandbox.last('ui_panelOpen').args[0], { title: 'Sandbox', width: 340 });

const label = panel.addLabel('Hello', { bold: true });
eq(sandbox.last('ui_panelAdd').args, [11, 'label', { text: 'Hello', bold: true, muted: false }]);
eq(label.id, nextId);

let buttonClicks = 0;
const button = panel.addButton('Go', () => ++buttonClicks, { style: 'light', icon: 'search' });
eq(sandbox.last('ui_panelAdd').args, [11, 'button', { text: 'Go', style: 'light', icon: 'search' }]);
sandbox.dispatch('widget', { id: button.id, event: 'click' });
eq(buttonClicks, 1);

let submitted = null;
let changed = null;
const input = panel.addInput({ placeholder: 'Type', value: 'v', multiline: true, onSubmit: (t) => { submitted = t; }, onChange: (t) => { changed = t; } });
eq(sandbox.last('ui_panelAdd').args, [11, 'input', { placeholder: 'Type', value: 'v', multiline: true }]);
sandbox.dispatch('widget', { id: input.id, event: 'change', value: 'ab' });
sandbox.dispatch('widget', { id: input.id, event: 'submit', value: 'abc' });
eq(changed, 'ab');
eq(submitted, 'abc');

let toggled = null;
const toggle = panel.addToggle('Flag', 1, (on) => { toggled = on; });
eq(sandbox.last('ui_panelAdd').args, [11, 'toggle', { text: 'Flag', checked: true }]);
sandbox.dispatch('widget', { id: toggle.id, event: 'change', value: false });
eq(toggled, false);

eq(panel.addSeparator(), undefined);
eq(sandbox.last('ui_panelAdd').args, [11, 'separator', {}]);
panel.addSkip(20);
eq(sandbox.last('ui_panelAdd').args, [11, 'skip', { px: 20 }]);

panel.setTitle('New').setWidth(500).scrollToBottom().show();
const sets = sandbox.called('ui_panelSet').map((c) => c.args);
eq(sets.slice(-4), [[11, { title: 'New' }], [11, { width: 500 }], [11, { scrollToBottom: true }], [11, { activate: true }]]);

panel.clear();
eq(sandbox.last('ui_panelClear').args, [11]);
sandbox.dispatch('widget', { id: button.id, event: 'click' });
eq(buttonClicks, 1, 'handlers dropped by clear()');

let closes = 0;
panel.on('close', (p) => { assert(p === panel, 'close gets the panel'); ++closes; });
panel.close();
eq(sandbox.last('ui_panelClose').args, [11]);
eq(closes, 1);
assert(!panel.isOpen(), 'closed');
panel.close();
eq(closes, 1, 'close fires once');
eq(panel.addLabel('late'), null, 'closed panel ignores add*');

// Closed by the user (X button): the client sends ui:panelClosed.
sandbox.native.ui_panelOpen = () => ({ id: 21, content: 22, reused: false });
const second = dzg.ui.panel({ title: 'Second' });
assert(second !== panel, 'new panel after close');
let secondCloses = 0;
second.on('close', () => ++secondCloses);
sandbox.dispatch('ui:panelClosed', { id: 21 });
eq(secondCloses, 1);
assert(!second.isOpen(), 'closed by the client');

sandbox.native.ui_panelOpen = () => null;
throws(() => dzg.ui.panel({ title: 'x' }), 'no main window');

// --- render transforms
const message = { id: 5, peerId: '777', text: 'secret plan', out: false };
eq(sandbox.dispatch('ui:render', message), null, 'nothing registered');
const offUpper = dzg.messages.addRenderTransform((m) => m.text.toUpperCase());
eq(sandbox.last('ui_setRenderTransforms').args, [true]);
const offCensor = dzg.messages.addRenderTransform((m) => m.text.replace(/SECRET/g, '******'));
eq(sandbox.dispatch('ui:render', message), '****** PLAN', 'transforms chain');
const offNull = dzg.messages.addRenderTransform(() => null);
eq(sandbox.dispatch('ui:render', message), '****** PLAN', 'null keeps the text');
offUpper();
eq(sandbox.dispatch('ui:render', message), null, 'censor does not match lowercase');
offCensor();
offNull();
eq(sandbox.last('ui_setRenderTransforms').args, [false]);
const offThrow = dzg.messages.addRenderTransform(() => { throw new Error('bad'); });
eq(sandbox.dispatch('ui:render', message), null, 'exceptions are swallowed');
offThrow();
dzg.messages.refreshRender();
assert(sandbox.last('ui_refreshRender'), 'refreshRender');
throws(() => dzg.messages.addRenderTransform('nope'), 'must be a function');

// --- events from the client
sandbox.event('app.focus', { active: false });
sandbox.event('compose.changed', { chat, text: 'typing' });
eq(globalThis.uiEvents, [
  { name: 'app.focus', data: { active: false } },
  { name: 'compose.changed', data: { chat, text: 'typing' } },
]);

print('ui.test.js: all checks passed');
