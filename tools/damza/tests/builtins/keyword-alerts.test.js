import { compile, match, snippet, addWord } from '../../../../plugins/builtin/keyword-alerts/main.js';

const { eq, assert } = sandbox;

// Whole words work with Cyrillic.
const [p] = compile(['дамза'], true);
assert(p.re.test('Привет, Дамза!'), 'whole word');
assert(!p.re.test('дамзаграм'), 'not a part of a word');
assert(compile(['дамза'], false)[0].re.test('дамзаграм'), 'substring mode');
assert(compile(['/срочн(о|ый)/'], true)[0].re.test('СРОЧНО нужен ответ'), 'regex, case-insensitive by default');
eq(compile(['/[/'], true).length, 0, 'bad regex skipped');
eq(snippet('очень длинное сообщение про релиз', 24, 5), 'очень длинное сообщение «про р»елиз');

const group = mock.chats[1];   // muted
const channel = mock.chats[2];
const user = mock.current;

// Empty list: nothing happens.
sandbox.event('message.new', mock.message({ text: 'релиз' }));
eq(mock.notifications.length, 0);

// /kw add stores the word in the settings.
mock.command('/kw add релиз');
eq(sandbox.settings.words, 'релиз');
mock.command('/kw add /wetakush/');
eq(sandbox.settings.words, 'релиз\n/wetakush/');

sandbox.event('message.new', mock.message({ id: 5, chat: channel, peerId: channel.id, fromName: 'Новости', text: 'Сегодня релиз DamZaGram!' }));
eq(mock.notifications.length, 1);
const n = mock.notifications[0];
assert(n.title.includes('«релиз»') && n.title.includes('Новости'), n.title);
assert(n.text.includes('«релиз»'), n.text);
n.onClick();
eq(mock.opened[0], { peerId: '300', messageId: 5 });

// Cooldown per chat.
sandbox.event('message.new', mock.message({ id: 6, chat: channel, peerId: channel.id, text: 'ещё релиз' }));
eq(mock.notifications.length, 1, 'cooldown');
eq(sandbox.storage.hits.length, 2, 'but logged');

// Muted chats are skipped unless enabled.
sandbox.event('message.new', mock.message({ id: 7, chat: group, peerId: group.id, text: 'релиз завтра' }));
eq(mock.notifications.length, 1, 'muted skipped');
sandbox.settings.includeMuted = true;
sandbox.event('settings.changed', { key: 'includeMuted', value: true });
sandbox.event('message.new', mock.message({ id: 8, chat: group, peerId: group.id, text: 'релиз завтра' }));
eq(mock.notifications.length, 2, 'muted included');

// Own messages and old messages are ignored.
sandbox.event('message.new', mock.message({ chat: user, out: true, text: 'мой релиз' }));
sandbox.event('message.new', mock.message({ chat: user, text: 'старый релиз', date: Math.floor(Date.now() / 1000) - 3600 }));
eq(mock.notifications.length, 2);

// onlyGroups.
sandbox.settings.onlyGroups = true;
sandbox.event('settings.changed', { key: 'onlyGroups', value: true });
assert(!match(mock.message({ chat: user, text: 'релиз' })), 'private skipped');
sandbox.settings.onlyGroups = false;
sandbox.event('settings.changed', { key: 'onlyGroups', value: false });
assert(match(mock.message({ chat: user, text: 'пиши @wetakush' })), 'regex word');

// Per-chat exclusion through the chat menu.
mock.menuClick('chat', user, 'не искать здесь');
assert(!match(mock.message({ chat: user, text: 'релиз' })), 'excluded');
assert(mock.menu('chat', user)[0].title.includes('снова искать'));

// Panel: list, remove, add through the input.
mock.command('/keywords');
const panel = mock.lastPanel();
assert(mock.panelText(panel).includes('релиз'));
mock.click(panel, '✕  релиз');
eq(sandbox.settings.words, '/wetakush/');
mock.submit(mock.lastPanel(), 'дедлайн');
eq(sandbox.settings.words, '/wetakush/\nдедлайн');
assert(!addWord('Дедлайн'), 'no duplicates');
