import { parseSchedule, inSchedule, isAway, replyText } from '../../../../plugins/builtin/auto-reply/main.js';

const { eq, assert } = sandbox;

eq(mock.manifest.enabledByDefault, false, 'disabled by default');

// Schedule, including ranges over midnight.
const night = parseSchedule('23:00-08:00, 13:00-14:00');
eq(night.length, 2);
const at = (h, m) => new Date(2026, 9, 12, h, m);
assert(inSchedule(night, at(23, 30)) && inSchedule(night, at(7, 59)) && inSchedule(night, at(13, 15)));
assert(!inSchedule(night, at(8, 0)) && !inSchedule(night, at(12, 0)));
eq(parseSchedule('мусор').length, 0);

// Off: no replies.
const user = mock.current;
sandbox.event('message.new', mock.message({ id: 1, text: 'привет' }));
eq(mock.sent.length, 0);
assert(mock.mainMenu.some((i) => !i.removed && i.title.includes('включить')));

// /away on: replies once per cooldown, with the name filled in.
mock.command('/away on');
assert(isAway());
assert(mock.mainMenu.some((i) => !i.removed && i.title.includes('выключить')), 'menu title updated');
sandbox.event('message.new', mock.message({ id: 2, fromName: 'Вася Пупкин', text: 'ты тут?' }));
eq(mock.sent.length, 1);
eq(mock.sent[0].peerId, '100');
eq(mock.sent[0].options.replyTo, 2);
assert(mock.sent[0].text.startsWith('Привет, Вася!'), mock.sent[0].text);
sandbox.event('message.new', mock.message({ id: 3, text: 'ау' }));
eq(mock.sent.length, 1, 'cooldown');

// The echo of our own auto-reply doesn't count as activity.
sandbox.event('message.new', mock.message({ id: 4, out: true, text: mock.sent[0].text }));
assert(isAway(), 'still away after echo');

// Groups, bots, old messages are ignored.
const group = mock.chats[1];
sandbox.event('message.new', mock.message({ chat: group, peerId: group.id, text: 'всем привет' }));
const bot = mock.peer(500, 'Bot', 'bot');
sandbox.event('message.new', mock.message({ chat: bot, peerId: bot.id, text: '/start' }));
const other = mock.peer(600, 'Петя', 'user');
sandbox.event('message.new', mock.message({ chat: other, peerId: other.id, text: 'старое', date: Math.floor(Date.now() / 1000) - 3600 }));
eq(mock.sent.length, 1);

// Custom text for this session.
mock.command('/away on Я в отпуске до понедельника');
sandbox.event('message.new', mock.message({ chat: other, peerId: other.id, fromName: 'Петя', text: 'йо' }));
eq(mock.sent[1].text, 'Я в отпуске до понедельника');
eq(replyText(mock.message({ fromName: 'Маша' })), 'Я в отпуске до понедельника');

// Writing yourself turns manual mode off.
sandbox.event('message.new', mock.message({ chat: other, peerId: other.id, out: true, text: 'я вернулся' }));
assert(!isAway(), 'auto off on activity');
assert(mock.toasts[mock.toasts.length - 1].includes('выключен'));

// Only contacts.
sandbox.settings.onlyContacts = true;
mock.command('/away on');
const stranger = mock.peer(700, 'Незнакомец', 'user');
sandbox.event('message.new', mock.message({ chat: stranger, peerId: stranger.id, text: 'купи' }));
eq(mock.sent.length, 2, 'not a contact');
mock.command('/away off');
assert(!isAway());
