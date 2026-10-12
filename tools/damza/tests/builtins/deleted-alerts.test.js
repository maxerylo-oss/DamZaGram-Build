import { shouldTrack, describe } from '../../../../plugins/builtin/deleted-alerts/main.js';

const { eq, assert } = sandbox;

const user = mock.current;
const group = mock.chats[1];
const channel = mock.chats[2];

// Filters follow the settings.
assert(shouldTrack(mock.message({ chat: user })), 'private chat tracked');
assert(!shouldTrack(mock.message({ chat: user, out: true })), 'own ignored');
assert(!shouldTrack(mock.message({ chat: group, peerId: group.id })), 'groups ignored with onlyPrivate');
sandbox.settings.onlyPrivate = false;
assert(shouldTrack(mock.message({ chat: group, peerId: group.id })), 'groups tracked');
assert(!shouldTrack(mock.message({ chat: channel, peerId: channel.id })), 'channels off by default');
sandbox.settings.onlyPrivate = true;
eq(describe(mock.message({ media: 'photo', text: 'смотри' })), '🖼 фото: смотри');

// A single deletion: one notification with who/what, click jumps to it.
sandbox.event('message.deleted', mock.message({ id: 42, chat: user, fromName: 'Вася', text: 'секрет' }));
eq(mock.notifications.length, 0, 'waits for the burst window');
sandbox.runTimers();
eq(mock.notifications.length, 1);
const n = mock.notifications[0];
assert(n.title.includes('Вася') && n.title.includes('удалил'), n.title);
eq(n.text, 'секрет');
eq(n.silent, true);
n.onClick();
eq(mock.opened[0], { peerId: '100', messageId: 42 });

// Own messages are ignored.
sandbox.event('message.deleted', mock.message({ chat: user, out: true, text: 'моё' }));
sandbox.runTimers();
eq(mock.notifications.length, 1);

// A burst in one chat collapses into one notification.
for (let i = 0; i < 5; ++i) {
  sandbox.event('message.deleted', mock.message({ id: 100 + i, chat: user, fromName: 'Вася', text: 'м' + i }));
}
sandbox.runTimers();
eq(mock.notifications.length, 2);
assert(mock.notifications[1].title.includes('Удалено 5'), mock.notifications[1].title);

// Journal: persisted, shown in the panel, filtered per chat.
eq(sandbox.storage.log.length, 6);
mock.command('/deleted');
let panel = mock.lastPanel();
let text = mock.panelText(panel);
assert(text.includes('секрет') && text.includes('м4'), text);
assert(text.indexOf('м4') < text.indexOf('секрет'), 'newest first');
const menu = mock.menu('chat', user);
assert(menu.some((i) => i.title.includes('(6)')), JSON.stringify(menu));
assert(!mock.menu('chat', group).length, 'no item for chats without deletions');

// Toast mode.
sandbox.settings.systemNotify = false;
sandbox.event('message.deleted', mock.message({ id: 7, chat: user, fromName: 'Вася', text: 'тост' }));
sandbox.runTimers();
assert(mock.toasts.some((t) => t.includes('тост')), JSON.stringify(mock.toasts));

// Export and clear.
mock.click(panel, 'Сохранить в файл');
const file = Object.keys(mock.files).find((p) => p.endsWith('deleted-alerts-log.txt'));
assert(file && mock.files[file].includes('секрет'), 'exported');
mock.click(panel, 'Очистить журнал');
await mock.tick();
eq(sandbox.storage.log.length, 0);

// A burst (history cleared): the journal is written once, not per message,
// and only one notification comes.
sandbox.settings.systemNotify = true;
let writes = 0;
const realStorageSet = sandbox.native.storageSet;
sandbox.native.storageSet = (key, value) => { if (key === 'log') writes++; sandbox.storage[key] = value; };
const before = mock.notifications.length;
sandbox.reset();
for (let i = 0; i < 500; ++i) {
  sandbox.event('message.deleted', mock.message({ id: 1000 + i, chat: user, fromName: 'Вася', text: 'х' + i }));
}
eq(writes, 0, 'nothing written during the burst');
assert(sandbox.called('settings').length <= 500, 'one settings read per event: ' + sandbox.called('settings').length);
assert(sandbox.called('storageGet').length === 0, 'no storage reads per event');
mock.command('/deleted');
assert(mock.panelText(mock.lastPanel()).includes('х499'), 'unsaved entries are visible');
sandbox.runTimers();
eq(writes, 1, 'one write for the whole burst');
eq(sandbox.storage.log.length, 300, 'log trimmed to logSize');
eq(mock.notifications.length, before + 1, 'one notification for the burst');
assert(mock.notifications[before].title.includes('Удалено 500'), mock.notifications[before].title);
delete sandbox.native.storageSet;
if (realStorageSet) sandbox.native.storageSet = realStorageSet;

// Deletions in many chats at once: one summary instead of a pile.
sandbox.settings.onlyPrivate = false;
const groups = [1, 2, 3, 4, 5].map((n) => mock.peer(2000 + n, 'Группа ' + n, 'group'));
for (const g of groups) {
  sandbox.event('message.deleted', mock.message({ chat: g, peerId: g.id, fromName: 'Спамер', text: 'спам' }));
}
sandbox.runTimers();
eq(mock.notifications.length, before + 2, 'one summary for 5 chats');
assert(mock.notifications[before + 1].title.includes('в 5 чатах'), mock.notifications[before + 1].title);

// A never-ending stream still notifies within MAX_WAIT.
const realNow = Date.now;
let fake = realNow();
Date.now = () => fake;
sandbox.reset();
for (let i = 0; i < 20; ++i) {
  fake += 1000;
  sandbox.event('message.deleted', mock.message({ id: 3000 + i, chat: user, fromName: 'Вася', text: 'поток' }));
}
const delays = sandbox.called('startTimer').map((c) => c.args[0]);
assert(delays[delays.length - 1] === 0, 'flush is due after MAX_WAIT: ' + JSON.stringify(delays));
Date.now = realNow;
sandbox.runTimers();
