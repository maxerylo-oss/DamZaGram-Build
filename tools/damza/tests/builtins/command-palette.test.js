import { fuzzyScore, switchLayout, search, layoutAlternative } from '../../../../plugins/builtin/command-palette/main.js';

const { eq, assert } = sandbox;

// Pure helpers.
eq(switchLayout('ghbdtn'), 'привет');
eq(switchLayout('руддщ'), 'hello');
assert(fuzzyScore('vas', 'Вася Пупкин') < 0, 'latin does not match cyrillic directly');
assert(fuzzyScore('вас', 'Вася Пупкин') > fuzzyScore('впк', 'Вася Пупкин'), 'prefix beats subsequence');
assert(fuzzyScore('впк', 'Вася Пупкин') > 0, 'subsequence matches');
assert(fuzzyScore('xyz', 'Вася') < 0);

// Registration.
assert(mock.shortcuts.some((s) => s.keys === 'Ctrl+Shift+K'), 'Ctrl+Shift+K registered');
// In the Russian layout the same key arrives as "Л": bound too.
eq(layoutAlternative('Ctrl+Shift+K'), 'Ctrl+Shift+Л');
eq(layoutAlternative('Alt+p'), 'Alt+З');
eq(layoutAlternative('Ctrl+Shift+Space'), null);
eq(layoutAlternative('Ctrl+1'), null);
assert(mock.shortcuts.some((s) => s.keys === 'Ctrl+Shift+Л' && !s.removed), 'Ctrl+Shift+Л registered');
assert(mock.mainMenu.some((m) => m.title === 'Командная палитра'), 'main menu item');
assert(dzg.commands.list().some((c) => c.name === 'palette'), '/palette registered');

// Open with the shortcut: input, hint, result buttons.
mock.shortcut('Ctrl+Shift+K');
let panel = mock.lastPanel();
assert(panel && panel.open, 'panel opened');
assert(mock.input(panel), 'has input');
assert(mock.visible(panel).filter((i) => i.type === 'button').length > 0, 'initial results shown');

// Wrong layout finds the chat: "dfcz" -> "вася".
mock.type(panel, 'dfcz');
let text = mock.panelText(panel);
assert(text.split('\n').find((l) => l.includes('Вася')), 'layout-fixed search finds Вася:\n' + text);

// Enter opens the first result and closes the panel.
mock.submit(panel);
await mock.tick();
eq(mock.opened[mock.opened.length - 1].peerId, '100');
assert(!panel.open, 'panel closed after pick');

// "@" limits to chats.
mock.command('/palette');
panel = mock.lastPanel();
mock.type(panel, '@нов');
text = mock.panelText(panel);
assert(text.includes('Новости'), 'channel found');
assert(!text.includes('Настройки'), 'no actions with @ prefix');

// ">" finds actions; night mode toggles.
mock.type(panel, '>ночн');
text = mock.panelText(panel);
assert(text.includes('ночной режим'), 'night mode action:\n' + text);
mock.click(panel, 'ночной режим');
await mock.tick();
eq(mock.night, false, 'night mode toggled off');

// Commands insert into the compose field.
mock.shortcut('Ctrl+Shift+K');
panel = mock.lastPanel();
mock.type(panel, '/pal');
mock.click(panel, '/palette');
await mock.tick();
eq(mock.compose.text, '/palette ');

// Deep links for settings.
mock.shortcut('Ctrl+Shift+K');
panel = mock.lastPanel();
mock.type(panel, 'папки');
mock.click(panel, 'папки');
await mock.tick();
eq(mock.links[mock.links.length - 1], 'tg://settings/folders');

// The layout twin toggles the palette as well.
mock.shortcut('Ctrl+Shift+Л');
assert(mock.lastPanel().open, 'opened by Ctrl+Shift+Л');
mock.shortcut('Ctrl+Shift+Л');
assert(!mock.lastPanel().open, 'closed by Ctrl+Shift+Л');

// Toggle: the shortcut closes an open palette.
mock.shortcut('Ctrl+Shift+K');
panel = mock.lastPanel();
mock.shortcut('Ctrl+Shift+K');
assert(!panel.open, 'second press closes');

// Frecency: a picked item ranks first for an empty query.
const sources = {
  chats: () => [
    { key: 'chat:a', kind: 'chat', icon: '', title: 'A', subtitle: '', fields: ['A'], boost: 0, run() {} },
    { key: 'chat:100', kind: 'chat', icon: '', title: 'B', subtitle: '', fields: ['B'], boost: 0, run() {} },
  ],
  commands: () => [],
  actions: () => [],
};
eq(search('', sources)[0].key, 'chat:100', 'recently used chat goes first');

// Shortcut is rebound on settings change.
sandbox.settings.shortcut = 'Ctrl+Shift+P';
sandbox.event('settings.changed', { key: 'shortcut', value: 'Ctrl+Shift+P' });
assert(mock.shortcuts.some((s) => s.keys === 'Ctrl+Shift+P' && !s.removed), 'new shortcut');
assert(mock.shortcuts.filter((s) => s.keys === 'Ctrl+Shift+K').every((s) => s.removed), 'old shortcut removed');
assert(mock.shortcuts.filter((s) => s.keys === 'Ctrl+Shift+Л').every((s) => s.removed), 'old layout twin removed');
assert(mock.shortcuts.some((s) => s.keys === 'Ctrl+Shift+З' && !s.removed), 'new layout twin');

// Plugins are toggled from the palette.
mock.shortcut('Ctrl+Shift+P');
panel = mock.lastPanel();
mock.type(panel, 'автоответ');
mock.click(panel, 'Включить плагин «Автоответчик»');
await mock.tick();
eq(mock.plugins[1].enabled, true);
mock.shortcut('Ctrl+Shift+P');
panel = mock.lastPanel();
mock.type(panel, '>менеджер');
mock.click(panel, 'Менеджер плагинов');
await mock.tick();
eq(mock.managerOpened, 1);

// "?" searches messages in all chats, debounced, and opens the hit.
mock.searchResults = [mock.message({ id: 321, chat: mock.chats[1], peerId: '200', fromName: 'Маша', text: 'релиз в пятницу' })];
mock.shortcut('Ctrl+Shift+P');
panel = mock.lastPanel();
mock.type(panel, '?релиз');
assert(mock.panelText(panel).includes('Ищу'), 'searching state');
sandbox.runTimers();
await mock.tick();
eq(mock.lastSearch.peer, null);
eq(mock.lastSearch.query, 'релиз');
assert(mock.panelText(panel).includes('Маша: релиз в пятницу'), mock.panelText(panel));
mock.submit(panel);
await mock.tick();
eq(mock.opened[mock.opened.length - 1], { peerId: '200', messageId: 321 });

// Welcome hint once.
sandbox.event('ready');
sandbox.runTimers();
assert(mock.toasts.some((t) => t.includes('командная палитра')), JSON.stringify(mock.toasts));
const toasts = mock.toasts.length;
sandbox.event('ready');
sandbox.runTimers();
eq(mock.toasts.length, toasts, 'welcome only once');

// Typing reads the usage history once per query, not once per item.
mock.shortcut('Ctrl+Shift+P');
panel = mock.lastPanel();
sandbox.reset();
mock.type(panel, 'ва');
eq(sandbox.called('storageGet').filter((c) => c.args[0] === 'recent').length, 1, 'one read per keystroke');
mock.shortcut('Ctrl+Shift+P');
