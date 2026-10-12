import {
  parseTags, splitNote, cut, filterItems, groupItems, toMarkdown, fileUrl, find, all, chatCount, previewOf,
} from '../../../../plugins/builtin/bookmarks/main.js';

const { eq, assert } = sandbox;

const user = mock.current;
const group = mock.chats[1];
const channel = mock.chats[2];

// dzg.ui.box goes to the native bridge: record the spec and answer it by hand.
const boxes = [];
let boxSeq = 70000;
sandbox.native.showBox = (spec) => {
  const id = ++boxSeq;
  boxes.push({ id, spec });
  return id;
};
async function answer(values, button) {
  const box = boxes[boxes.length - 1];
  assert(box && !box.answered, 'a dialog is open');
  box.answered = true;
  sandbox.settle(box.id, true, { button: button || 'ok', values: values || {} });
  await mock.tick();
  return box.spec;
}
const itemsWrites = () => sandbox.called('storageSet').filter((c) => c.args[0] === 'items').length;
const stored = () => sandbox.storage.items;
const titles = (message) => mock.menu('message', message).map((i) => i.title);

// --- Pure helpers ----------------------------------------------------------

eq(parseTags('#работа #идея, Работа;#ИДЕЯ'), ['работа', 'идея']);
eq(parseTags('#a#b  c'), ['a', 'b', 'c']);
eq(parseTags('#!!!'), []);
eq(parseTags(Array.from({ length: 20 }, (_, i) => '#t' + i).join(' ')).length, 10, 'max 10 tags');
eq(splitNote('вернуться в пятницу #работа #идея'), { note: 'вернуться в пятницу', tags: ['работа', 'идея'] });
eq(splitNote('#идея'), { note: '', tags: ['идея'] });
eq(splitNote('обсудить #123 с Петей'), { note: 'обсудить #123 с Петей', tags: [] }, 'hashtags in the middle stay');
eq(cut('ab😀', 3), 'ab', 'no broken surrogate pair');
eq(fileUrl('C:\\Users\\Me\\DamZaGram Desktop\\tdata'), 'file:///C:/Users/Me/DamZaGram%20Desktop/tdata');
eq(fileUrl('/home/me/data'), 'file:///home/me/data');

// --- Registration ------------------------------------------------------------

assert(mock.mainMenu.some((i) => i.title === 'Закладки' && !i.removed), 'main menu item');
eq(mock.topBar.length, 1, 'top bar button');
let topBar = mock.topBar[0];
assert(!topBar.when(user), 'no top bar button without bookmarks');
assert(dzg.commands.list().some((c) => c.name === 'bm'), '/bm registered');

// --- Add through the message menu -------------------------------------------

const long = 'Длинный текст. '.repeat(40);
const m1 = mock.message({ id: 501, text: long, fromName: 'Вася Пупкин' });
eq(titles(m1).filter((t) => t.includes('заклад')), ['🔖 В закладки']);
assert(!titles(mock.message({ service: true })).length, 'no item for service messages');

mock.menuClick('message', m1, 'В закладки');
eq(boxes.length, 1, 'asks for a note');
assert(boxes[0].spec.text.includes('Длинный текст'), 'dialog shows the message');
eq(boxes[0].spec.inputs.map((i) => i.id), ['note', 'tags']);
await answer({ note: 'позвонить завтра #срочно', tags: '#работа #идея' });
eq(stored().length, 1);
let b1 = stored()[0];
eq(b1.peerId, '100');
eq(b1.msgId, 501);
eq(b1.acc, '1', 'remembers the account');
eq(b1.chatName, 'Вася Пупкин');
eq(b1.fromName, 'Вася Пупкин');
eq(b1.text.length, 300, 'preview is the first 300 chars');
eq(b1.more, true);
eq(b1.note, 'позвонить завтра');
eq(b1.tags, ['работа', 'идея', 'срочно']);
assert(b1.created > 0 && b1.date > 0);
assert(mock.toasts[mock.toasts.length - 1].includes('Добавлено в закладки'), mock.toasts[mock.toasts.length - 1]);
eq(titles(m1).filter((t) => t.includes('заклад')), ['🔖 Убрать из закладок', '📝 Заметка к закладке…']);
assert(topBar.when(user), 'top bar button shows up in the chat');
assert(!topBar.when(group));

// The same message of another account is a different conversation.
const otherAcc = mock.message({ id: 501, selfId: '999' });
eq(find(otherAcc), null);
eq(titles(otherAcc).filter((t) => t.includes('заклад')), ['🔖 В закладки']);

// A cancelled dialog adds nothing; two clicks open only one dialog.
const m2 = mock.message({ id: 502, chat: group, peerId: group.id, fromName: 'Петя', text: 'идея для релиза', media: 'photo', mediaInfo: { fileName: 'plan.png' } });
mock.menuClick('message', m2, 'В закладки');
mock.menuClick('message', m2, 'В закладки');
eq(boxes.filter((b) => !b.answered).length, 1, 'no second dialog while the first is open');
await answer({ note: 'x' }, 'cancel');
eq(stored().length, 1, 'cancel adds nothing');

// Quick mode: no dialog.
sandbox.settings.askNote = false;
const before = boxes.length;
mock.menuClick('message', m2, 'В закладки');
await mock.tick();
eq(boxes.length, before, 'no dialog with askNote off');
eq(stored().length, 2);
eq(stored()[1].media, 'photo');
eq(stored()[1].fileName, 'plan.png');
eq(stored()[1].chatName, 'DamZaGram Chat');
sandbox.settings.askNote = true;

// A third one, in a channel, through /bm add as a reply.
const m3 = mock.message({ id: 503, chat: channel, peerId: channel.id, fromName: 'Новости', text: 'Вышла новая версия', post: true });
const realGet = dzg.messages.get;
dzg.messages.get = (p, id) => (String((p && p.id) || p) === channel.id && id === 503 ? m3 : null);
sandbox.dispatch('beforeSend', { text: '/bm add прочитать changelog #релиз', chat: channel, peerId: channel.id, replyTo: 503 });
await mock.tick();
dzg.messages.get = realGet;
eq(stored().length, 3);
let b3 = stored()[2];
eq(b3.note, 'прочитать changelog');
eq(b3.tags, ['релиз']);
eq(b3.chatType, 'channel');

// /bm add without a reply explains itself.
mock.command('/bm add что-то');
await mock.tick();
assert(mock.toasts[mock.toasts.length - 1].includes('ответом'), mock.toasts[mock.toasts.length - 1]);
eq(stored().length, 3);

// Chat menu: count.
assert(mock.menu('chat', user).some((i) => i.title.includes('(1)')), JSON.stringify(mock.menu('chat', user)));
assert(!mock.menu('chat', mock.chats[3]).length, 'no item without bookmarks');
eq(chatCount(group.id), 1);

// --- Pure list helpers --------------------------------------------------------

const list = all();
eq(filterItems(list, { query: 'ЗВОНИТЬ' }).map((b) => b.msgId), [501], 'case-insensitive');
eq(filterItems(list, { query: '#рел' }).map((b) => b.msgId), [503], 'tag prefix');
eq(filterItems(list, { query: 'петя фото' }).map((b) => b.msgId), [502], 'all words, sender and media');
eq(filterItems(list, { tag: 'идея' }).map((b) => b.msgId), [501]);
eq(filterItems(list, { peerId: group.id }).map((b) => b.msgId), [502]);
eq(previewOf(list[1]), '🖼 фото «plan.png»: идея для релиза');
const ordered = groupItems(list.map((b, i) => Object.assign({}, b, { created: 1000 + i })), true, 'added');
eq(ordered.map((g) => g.peerId), [channel.id, group.id, user.id], 'chat with the newest bookmark first');
const flatList = groupItems([
  { id: 1, peerId: 'a', created: 1, date: 300 },
  { id: 2, peerId: 'a', created: 2, date: 100 },
  { id: 3, peerId: 'b', created: 3, date: 200 },
], false, 'message');
eq(flatList[0].items.map((b) => b.id), [1, 3, 2], 'sorted by message date');

// --- Panel ---------------------------------------------------------------------

mock.command('/bm');
let panel = mock.lastPanel();
eq(panel.title, '🔖 Закладки (3)', 'count in the title');
let text = mock.panelText(panel);
assert(text.includes('позвонить завтра') && text.includes('идея для релиза') && text.includes('Вышла новая версия'), text);
assert(text.indexOf('Новости') < text.indexOf('DamZaGram Chat') && text.indexOf('DamZaGram Chat') < text.indexOf('Вася Пупкин'), 'newest chat first');
assert(mock.visible(panel).some((i) => i.type === 'label' && i.o.bold && i.text.includes('DamZaGram Chat · 1')), 'group headers');
assert(mock.findItem(panel, 'button', '#работа · 1'), 'tag buttons');
assert(mock.findItem(panel, 'button', '✓ Все теги'), 'no tag selected');

// Live search, debounced.
mock.type(panel, 'changelog');
eq(panel.title, '🔖 Закладки (3)', 'waits for the debounce');
sandbox.runTimers();
eq(panel.title, '🔖 Закладки (1 из 3)');
text = mock.panelText(panel);
assert(text.includes('Вышла новая версия') && !text.includes('позвонить завтра'), text);
mock.submit(panel, 'ничего такого');
assert(mock.panelText(panel).includes('Ничего не нашлось'));
mock.submit(panel, '');
eq(panel.title, '🔖 Закладки (3)');

// Tag filter.
mock.click(panel, '#идея · 1');
eq(panel.title, '🔖 Закладки (1 из 3)');
assert(mock.findItem(panel, 'button', '✓ #идея'), 'selected tag is marked');
mock.click(panel, '✓ #идея');
eq(panel.title, '🔖 Закладки (3)', 'second click clears the tag');

// Open.
mock.click(panel, 'Открыть');
eq(mock.opened[mock.opened.length - 1], { peerId: channel.id, messageId: 503 });

// Edit note.
mock.submit(panel, 'звонить');
mock.click(panel, 'Изменить заметку');
const editSpec = boxes[boxes.length - 1].spec;
eq(editSpec.inputs[0].value, 'позвонить завтра', 'current note prefilled');
eq(editSpec.inputs[1].value, '#работа #идея #срочно', 'current tags prefilled');
await answer({ note: 'перезвонить в 18:00', tags: '#работа' });
b1 = all().find((b) => b.msgId === 501);
eq(b1.note, 'перезвонить в 18:00');
eq(b1.tags, ['работа']);
panel = mock.lastPanel();
assert(mock.panelText(panel).includes('перезвонить в 18:00'));
assert(!mock.findItem(panel, 'button', '#идея'), 'unused tag button is gone');

// Delete with undo.
mock.submit(panel, '');
const writes = itemsWrites();
mock.click(panel, 'Удалить');
eq(all().length, 2);
eq(itemsWrites(), writes + 1, 'saved right away');
eq(panel.title, '🔖 Закладки (2)');
mock.click(panel, 'Вернуть закладку');
eq(all().length, 3, 'undo');
assert(!mock.findItem(panel, 'button', 'Вернуть закладку'));

// --- Deleted and edited messages ----------------------------------------------

const realNow = Date.now;
sandbox.reset();
sandbox.event('message.deleted', mock.message({ id: 9999, text: 'чужое' }));
sandbox.event('message.edited', mock.message({ id: 9999, text: 'чужое' }));
eq(sandbox.called('storageSet').length, 0, 'unrelated events write nothing');
eq(sandbox.called('settings').length, 0, 'unrelated events read no settings');

sandbox.event('message.deleted', m2);
sandbox.event('message.edited', Object.assign({}, m2, { text: '' }));
let b2 = all().find((b) => b.msgId === 502);
eq(b2.deleted, true);
eq(b2.text, 'идея для релиза', 'stored preview kept');
eq(itemsWrites(), 0, 'save is debounced');
sandbox.runTimers();
eq(itemsWrites(), 1, 'one save for the batch');
assert(mock.panelText(mock.lastPanel()).includes('🗑 удалено'), mock.panelText(mock.lastPanel()));
assert(topBar.when(group), 'deleted bookmarks still count');

// Edits update the preview; reactions-only edits do nothing.
sandbox.reset();
sandbox.event('message.edited', Object.assign({}, m3, { text: 'Вышла новая версия 2.0' }));
b3 = all().find((b) => b.msgId === 503);
eq(b3.text, 'Вышла новая версия 2.0');
eq(b3.edited, true);
sandbox.runTimers();
sandbox.reset();
sandbox.event('message.edited', Object.assign({}, m3, { text: 'Вышла новая версия 2.0', reactions: [{ emoji: '👍', count: 1 }] }));
sandbox.runTimers();
eq(itemsWrites(), 0, 'no write when the text did not change');
sandbox.settings.trackEdits = false;
sandbox.event('message.edited', Object.assign({}, m3, { text: 'другое' }));
eq(all().find((b) => b.msgId === 503).text, 'Вышла новая версия 2.0', 'trackEdits off');
sandbox.settings.trackEdits = true;
Date.now = realNow;

// --- Export ------------------------------------------------------------------------

panel = mock.lastPanel();
mock.click(panel, 'Экспорт всех закладок');
const path = '/tmp/sandbox-data/bookmarks/bookmarks.md';
const md = mock.files[path];
assert(md, 'written to the data dir: ' + Object.keys(mock.files));
assert(md.startsWith('# 🔖 Закладки'), md);
assert(md.includes('закладок: 3'));
assert(md.includes('## 📢 Новости (1)') && md.includes('## 👥 DamZaGram Chat (1)') && md.includes('## 👤 Вася Пупкин (1)'), md);
assert(md.includes('> Вышла новая версия 2.0'), 'quoted text');
assert(md.includes('- Заметка: перезвонить в 18:00'));
assert(md.includes('- Теги: #работа'));
assert(md.includes('- Вложение: 🖼 фото «plan.png»'));
assert(md.includes('- Ссылка: https://t.me/c/300/503'), 'links for channels');
assert(md.includes('- Сообщение удалено'));
assert(md.includes('> …'), 'cut preview marked');
const exportToast = mock.toasts[mock.toasts.length - 1];
assert(exportToast.includes(path), exportToast);
eq(sandbox.storage.lastExport, path);
mock.click(mock.lastPanel(), 'Открыть папку');
eq(mock.links[mock.links.length - 1], 'file:///tmp/sandbox-data/bookmarks');
assert(toMarkdown([]).includes('закладок: 0'));

eq(mock.notifications.length, 0, 'no extra notification when the panel has the button');

// /bm export from the chat: a notification opens the folder on click.
mock.lastPanel().api.close();
let links = mock.links.length;
mock.command('/bm export');
eq(mock.notifications.length, 1);
eq(mock.notifications[0].silent, true);
eq(mock.links.length, links, 'nothing opened by itself');
mock.notifications[0].onClick();
eq(mock.links[mock.links.length - 1], 'file:///tmp/sandbox-data/bookmarks');
// ...or opens it right away.
sandbox.settings.openFolderAfterExport = true;
links = mock.links.length;
mock.command('/bm export');
eq(mock.links.length, links + 1, 'folder opened');
eq(mock.notifications.length, 1, 'no notification then');
sandbox.settings.openFolderAfterExport = false;

// Top bar button follows the setting.
sandbox.settings.topBarButton = false;
sandbox.event('settings.changed', { key: 'topBarButton', value: false });
assert(topBar.removed, 'removed when switched off');
sandbox.settings.topBarButton = true;
sandbox.event('settings.changed', { key: 'topBarButton', value: true });
eq(mock.topBar.filter((b) => !b.removed).length, 1, 'added back');
const topBarOld = topBar;
topBar = mock.topBar[mock.topBar.length - 1];
assert(topBar !== topBarOld && topBar.when(group));

// --- Chat-only view (top bar / /bm here) ------------------------------------------

topBar.onClick(group);
panel = mock.lastPanel();
eq(panel.title, '🔖 Закладки · DamZaGram Chat (1)');
text = mock.panelText(panel);
assert(text.includes('идея для релиза') && !text.includes('Вышла новая версия'), text);
mock.click(panel, 'показать все чаты');
eq(panel.title, '🔖 Закладки (3)');
mock.command('/bm here');
eq(mock.lastPanel().title, '🔖 Закладки · Вася Пупкин (1)');
mock.command('/bm #релиз');
eq(mock.lastPanel().title, '🔖 Закладки (1 из 3)', '/bm with a query');

// Paging.
sandbox.settings.pageSize = 5;
sandbox.settings.askNote = false;
for (let i = 0; i < 7; ++i) {
  mock.menuClick('message', mock.message({ id: 600 + i, text: 'пачка ' + i }), 'В закладки');
}
await mock.tick();
eq(all().length, 10);
mock.command('/bm пачка');
panel = mock.lastPanel();
eq(panel.title, '🔖 Закладки (7 из 10)');
eq(mock.visible(panel).filter((i) => i.type === 'button' && i.text === '↪ Открыть').length, 5, 'first page');
mock.click(panel, 'Показать ещё 2');
eq(mock.visible(panel).filter((i) => i.type === 'button' && i.text === '↪ Открыть').length, 7, 'second page');
assert(mock.panelText(panel).indexOf('пачка 6') < mock.panelText(panel).indexOf('пачка 0'), 'newest first');
sandbox.settings.pageSize = 30;

// --- Remove from the menu ----------------------------------------------------------

// With a note: asks first.
mock.confirmAnswer = false;
mock.menuClick('message', m1, 'Убрать из закладок');
await mock.tick();
assert(find(m1), 'kept when not confirmed');
mock.confirmAnswer = true;
mock.menuClick('message', m1, 'Убрать из закладок');
await mock.tick();
eq(find(m1), null);
eq(titles(m1).filter((t) => t.includes('заклад')), ['🔖 В закладки']);
eq(chatCount(user.id), 7, 'count follows removals');
assert(topBar.when(user), 'the batch keeps the button');
// Without a note: no question.
const alerts = mock.alerts.length;
mock.menuClick('message', mock.message({ id: 600 }), 'Убрать из закладок');
await mock.tick();
eq(mock.alerts.length, alerts, 'no confirm without a note');
eq(all().length, 8);

// --- Survives a restart: storage holds plain data ----------------------------------

const snapshot = JSON.parse(JSON.stringify(sandbox.storage.items));
assert(snapshot.every((b) => b.peerId && b.msgId > 0 && Array.isArray(b.tags)), 'valid records');

// --- Unload -------------------------------------------------------------------------

sandbox.event('message.deleted', mock.message({ id: 601 }));
sandbox.dispatch('unload');
assert(sandbox.storage.items.find((b) => b.msgId === 601).deleted, 'pending save flushed on unload');
assert(topBar.removed, 'top bar button removed');
eq(mock.openPanels().length, 0, 'panel closed');
