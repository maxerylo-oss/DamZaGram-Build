import {
  getRules, getLog, flush, runSchedule, isGuarded, LOOP_GUARD_MS, explainMessage, startWizard,
} from '../../../../plugins/builtin/automations/main.js';
import * as R from '../../../../plugins/builtin/automations/rules.js';
import * as W from '../../../../plugins/builtin/automations/wizard.js';

const { eq, assert } = sandbox;

// --- Харнесс ------------------------------------------------------------------

// Управляемые часы: понедельник, 12.10.2026, 08:00.
let now = new Date(2026, 9, 12, 8, 0, 0, 0).getTime();
Date.now = () => now;
const advance = (ms) => { now += ms; };

// Окна dzg.ui.box: ответы по очереди; поля, которые тест не задал, остаются
// такими, какими их заполнил плагин (как если бы пользователь их не трогал).
const boxes = [];
const answers = [];
const answer = (values, button) => answers.push({ values: values || {}, button: button || 'ok' });
dzg.ui.box = (spec) => {
  boxes.push(spec);
  const next = answers.shift() || { button: 'cancel', values: {} };
  const values = {};
  for (const i of spec.inputs || []) values[i.id] = i.value || '';
  for (const t of spec.toggles || []) values[t.id] = !!t.checked;
  return Promise.resolve({ button: next.button, values: Object.assign(values, next.values) });
};
const lastBox = () => boxes[boxes.length - 1];

const reacts = [];
const forwards = [];
const typing = [];
const mutes = [];
let forwardFails = false;
dzg.messages.react = (p, id, emoji) => { reacts.push({ peerId: String(p), id, emoji }); return Promise.resolve(); };
dzg.messages.forward = (from, id, to) => {
  forwards.push({ from: String(from), id, to: String(to) });
  return forwardFails ? Promise.reject(new Error('CHAT_FORWARDS_RESTRICTED')) : Promise.resolve();
};
dzg.chats.setTyping = (p, action) => { typing.push({ peerId: String(p), action }); return true; };
dzg.chats.mute = (p, seconds) => { mutes.push({ peerId: String(p), seconds }); return true; };
dzg.chats.find = (username) => mock.chats.find((c) => c.username === username) || null;
dzg.chats.resolve = (username) => Promise.resolve(dzg.chats.find(username));
let loaded = [];
dzg.messages.loaded = () => loaded.slice();

const vasya = mock.chats[0];   // '100', личный
const group = mock.chats[1];   // '200', группа @damzachat
const news = mock.chats[2];    // '300', канал @news
const saved = mock.chats[3];   // '1', Избранное
const bot = mock.peer(500, 'Погодный бот', 'bot');

const msg = (over) => mock.message(Object.assign({ date: Math.floor(now / 1000) }, over || {}));
const incoming = (text, over) => sandbox.event('message.new', msg(Object.assign({ text }, over || {})));
const inGroup = (text, over) => incoming(text, Object.assign({ chat: group, peerId: group.id, fromId: '555', fromName: 'Петя' }, over || {}));
const settle = () => mock.tick(400);
const rule = (name) => getRules().find((r) => r.name === name);
const lastLog = () => getLog()[getLog().length - 1];

// Кнопка панели под правилом с таким названием.
function clickFor(name, needle) {
  const items = mock.visible(mock.lastPanel());
  const start = items.findIndex((i) => i.type === 'toggle' && i.text.includes(name));
  if (start < 0) throw new Error('Нет правила «' + name + '» в панели:\n' + mock.panelText());
  const item = items.slice(start).find((i) => i.type === 'button' && i.text.includes(needle));
  if (!item) throw new Error('Нет кнопки «' + needle + '» у правила «' + name + '»');
  item.onClick(item.widget);
}

// --- Манифест -----------------------------------------------------------------

eq(mock.manifest.builtin, true);
eq(mock.manifest.author, '@wetakush');
assert(mock.manifest.enabledByDefault !== false, 'enabled by default');
assert(dzg.commands.list().some((c) => c.name === 'auto'), '/auto registered');
assert(mock.mainMenu.some((i) => i.title === 'Автоматизации'), 'main menu item');

// --- Разбор полей -------------------------------------------------------------

eq(R.parseWhen('09:00').schedule, { kind: 'daily', times: ['09:00'] });
eq(R.parseWhen('18:30, 9:05').schedule, { kind: 'daily', times: ['09:05', '18:30'] });
eq(R.parseWhen('30m').schedule, { kind: 'every', minutes: 30 });
eq(R.parseWhen('каждые 2 часа').schedule, { kind: 'every', minutes: 120 });
eq(R.parseWhen('раз в час').schedule, { kind: 'every', minutes: 60 });
eq(R.parseWhen('15').schedule, { kind: 'every', minutes: 15 });
assert(R.parseWhen('25:00').error && R.parseWhen('0m').error && R.parseWhen('когда-нибудь').error);
assert(R.parseWhen('99999m').error, 'longer than a week');

const env = { current: vasya, self: mock.self, lookup: (id) => dzg.chats.get(id) };
eq(R.parseChats('@DamZaChat, 300, текущий', env), { tokens: ['@damzachat', '300', '100'], names: { 300: 'Новости', 100: 'Вася Пупкин' } });
eq(R.parseChats('избранное', env).tokens, ['1']);
eq(R.parseChats('https://t.me/news', env).tokens, ['@news']);
assert(R.parseChats('текущий', { current: null }).error, 'no open chat');
assert(R.parseChats('что-то странное', env).error);
assert(R.chatMatches(['-100300'], news) && R.chatMatches(['@news'], news) && !R.chatMatches(['@news'], group));

eq(R.parseMedia('голосовое, кружок').list, ['voice', 'videoNote']);
eq(R.parseMedia('только текст').list, ['*none']);
eq(R.parseMedia('фото любое').list, ['*any']);
assert(R.parseMedia('нет, фото').error && R.parseMedia('мемы').error);

assert(R.parsePattern('срочно, asap').re.test('Это СРОЧНО!'));
assert(R.parsePattern('/^\\d{4}$/').re.test('1234') && !R.parsePattern('/^\\d{4}$/').re.test('12345'));
assert(R.parsePattern('/(/').error, 'bad regex');
eq(R.parsePattern('').re, null);

eq(R.renderTemplate('{sender} в {chat}: {text} ({rule}) {unknown}', { sender: 'Вася', chat: 'Чат', text: 'хай', rule: 'П' }), 'Вася в Чат: хай (П) {unknown}');

// Испорченные и чужие правила приводятся в порядок или отбрасываются.
eq(R.normalizeRule({ trigger: { type: 'eval' }, actions: [{ type: 'notify' }] }), null);
eq(R.normalizeRule({ trigger: { type: 'message' }, actions: [{ type: 'shell', cmd: 'rm -rf' }] }), null);
eq(R.normalizeRule({ trigger: { type: 'schedule', schedule: { kind: 'every', minutes: 0 } }, actions: [{ type: 'notify' }] }), null, 'zero interval');
const cleaned = R.normalizeRule({
  trigger: { type: 'message', chats: ['@Good_name', 'bad name!'], in: false, out: false },
  actions: [{ type: 'webhook', url: 'javascript:alert(1)' }, { type: 'reply', text: 'ok' }, { type: 'react', emoji: '' }],
  limit: 1000,
});
eq(cleaned.trigger.chats, ['@good_name']);
eq(cleaned.trigger.in, true, 'some direction is required');
eq(cleaned.actions, [{ type: 'reply', text: 'ok' }]);
eq(cleaned.limit, 60, 'limit is clamped');

// --- Без правил ничего не происходит ------------------------------------------

incoming('срочно!');
eq(mock.sent.length + mock.notifications.length, 0, 'no rules => no-op');

// --- Редактор: новое правило через панель -------------------------------------

mock.command('/auto');
const panel = mock.lastPanel();
assert(mock.panelText(panel).includes('Правил пока нет'));
mock.click(panel, 'Добавить правило');
assert(mock.panelText(panel).includes('когда срабатывать'));
answer({ text: 'срочно, asap' });                                 // условия
answer({ a_notify: true, a_reply: true });                          // действия
answer({ notifyText: '{sender}: {text}', replyText: 'Принял, {sender}! ({time})' }); // настройка
answer({ name: 'Срочное' });                                       // название
mock.click(panel, 'На новое сообщение');
await settle();
eq(boxes.length, 4, 'four steps');
assert(boxes[0].text.includes('Отправитель') && boxes[0].toggles.some((t) => t.id === 'dirOut' && !t.checked), 'outgoing is off by default');
assert(boxes[2].text.includes('{sender}'), 'template help');
assert(boxes[3].text.includes('Если:') && boxes[3].toggles.some((t) => t.id === 'allowBots'), 'chatty rule asks about bots');
let urgent = rule('Срочное');
assert(urgent, 'rule saved');
eq(sandbox.storage.rules.length, 1, 'persisted');
eq(urgent.limit, 5, 'default limit');
eq(urgent.actions.map((a) => a.type), ['notify', 'reply']);
assert(mock.panelText(panel).includes('Срочное') && mock.panelText(panel).includes('Если:'), 'back to the list');

// --- Срабатывание -------------------------------------------------------------

incoming('Помоги, это срочно', { id: 1001 });
eq(mock.notifications.length, 1);
eq(mock.notifications[0].text, 'Вася Пупкин: Помоги, это срочно');
eq(mock.sent.length, 1);
eq(mock.sent[0].peerId, '100');
eq(mock.sent[0].options.replyTo, 1001);
eq(mock.sent[0].text, 'Принял, Вася Пупкин! (08:00)');
eq(rule('Срочное').stats.fired, 1);
assert(lastLog().text.includes('ответ') && lastLog().kind === 'ok', JSON.stringify(lastLog()));
mock.notifications[0].onClick();
eq(mock.opened[mock.opened.length - 1], { peerId: '100', messageId: 1001 });

// Не подходит по тексту / своё исходящее — ничего.
incoming('просто привет');
incoming('срочно, это я сам', { out: true });
eq(mock.sent.length, 1);

// --- Защита от петель ---------------------------------------------------------

// Свой ответ правила в этом чате (асинхронное эхо) не запускает правила…
assert(isGuarded('100'), 'chat cools down after an automatic reply');
const out = getRules().length;
answer({ dirIn: false, dirOut: true, text: '' });
answer({ a_notify: true });
answer({});
answer({ name: 'Мои сообщения' });
await startWizard(W.emptyForm('message'));
eq(getRules().length, out + 1);
incoming('Принял, Вася Пупкин! (08:00)', { out: true });
eq(mock.notifications.length, 1, 'echo of our reply is ignored');
// …и входящие в «остывающем» чате не получают второго ответа.
incoming('срочно ещё раз');
eq(mock.sent.length, 1, 'no reply while the chat cools down');
eq(mock.notifications.length, 1, 'chatty rule skipped entirely');
assert(lastLog().kind === 'skip' && lastLog().text.includes('защита от петель'), 'guard skip is explained');
incoming('срочно в третий раз');
eq(getLog().filter((e) => e.text.includes('защита от петель')).length, 1, 'guard skip logged once per minute');
advance(LOOP_GUARD_MS + 1);
assert(!isGuarded('100'));
incoming('и снова не я', { out: true });
eq(mock.notifications.length, 2, 'outgoing rule works after the guard');
eq(mock.notifications[1].title, '⚡ Мои сообщения');

// Повторный вход: клиент сообщает об отправленном ответе прямо внутри send().
const realSend = dzg.messages.send;
dzg.messages.send = (p, text, options) => {
  const result = realSend(p, text, options);
  sandbox.event('message.new', msg({ chat: dzg.chats.get(p), peerId: String(p), out: true, text }));
  sandbox.event('message.new', msg({ chat: dzg.chats.get(p), peerId: String(p), text: 'срочно: эхо' }));
  return result;
};
incoming('срочно-срочно');
eq(mock.sent.length, 2, 'one reply, no recursion');
eq(mock.notifications.length, 3, 'only the outer message notified');
dzg.messages.send = realSend;
advance(LOOP_GUARD_MS + 1);

// --- Лимит срабатываний -------------------------------------------------------

answer({ text: 'пинг' });
answer({});
answer({ notifyText: 'понг: {text}' });
answer({ name: 'Пинг' });
await startWizard(W.emptyForm('message'));
const notesBeforePing = mock.notifications.length;
for (let i = 0; i < 7; ++i) inGroup('пинг ' + i);
eq(mock.notifications.length, notesBeforePing + 5, 'rate limited to 5 per minute');
eq(mock.notifications[mock.notifications.length - 1].text, 'понг: пинг 4');
eq(rule('Пинг').stats.skipped, 2, 'skips counted');
assert(getLog().some((e) => e.kind === 'skip' && e.text.includes('Лимит 5')), 'limit is logged');
eq(getLog().filter((e) => e.text.includes('Лимит 5')).length, 1, 'limit logged once per minute');
advance(61 * 1000);
inGroup('пинг после паузы');
eq(mock.notifications.length, notesBeforePing + 6, 'limit window slides');

// Общий лимит на все правила.
dzg.settings.set('globalLimit', 2);
advance(61 * 1000);
inGroup('пинг 1');
inGroup('пинг 2');
inGroup('пинг 3');
eq(mock.notifications.length, notesBeforePing + 8, 'global limit');
assert(lastLog().kind === 'skip' && lastLog().text.includes('Общий лимит 2'), lastLog().text);
dzg.settings.set('globalLimit', 30);
advance(61 * 1000);

// --- Боты, каналы, служебные --------------------------------------------------

const sentBeforeBot = mock.sent.length;
const notesBeforeBot = mock.notifications.length;
incoming('срочно погода', { chat: bot, peerId: bot.id, fromId: bot.id, fromName: bot.name });
eq(mock.sent.length, sentBeforeBot, 'no reply to a bot');
eq(mock.notifications.length, notesBeforeBot + 1, 'notification still shown');
assert(lastLog().text.includes('это бот'), lastLog().text);
incoming('срочно в канале', { chat: news, peerId: news.id, fromId: news.id, fromName: news.name, post: true });
eq(mock.sent.length, sentBeforeBot, 'no reply in a channel');
const telegram = mock.peer(777000, 'Telegram', 'user');
const notesBeforeService = mock.notifications.length;
incoming('Код входа: 12345, срочно', { chat: telegram, peerId: telegram.id, fromId: telegram.id });
eq(mock.notifications.length, notesBeforeService, 'service notifications are never touched');
advance(61 * 1000);

// Разрешили ботов — отвечаем.
urgent = rule('Срочное');
urgent.allowBots = true;
incoming('срочно погода 2', { chat: bot, peerId: bot.id, fromId: bot.id, fromName: bot.name });
eq(mock.sent.length, sentBeforeBot + 1, 'allowed bots get a reply');
urgent.allowBots = false;
advance(LOOP_GUARD_MS + 1);

// --- Старые сообщения и другой аккаунт ----------------------------------------

const notesBefore = mock.notifications.length;
incoming('срочно, но старое', { date: Math.floor(now / 1000) - 3600 });
incoming('срочно, но другой аккаунт', { selfId: '999' });
eq(mock.notifications.length, notesBefore, 'old / other-account messages are ignored');
incoming('срочно, мой аккаунт', { selfId: '1' });
eq(mock.notifications.length, notesBefore + 1, 'active account works');
advance(LOOP_GUARD_MS + 1);

// --- Общий выключатель --------------------------------------------------------

mock.command('/auto off');
eq(sandbox.settings.enabled, false);
assert(mock.toasts[mock.toasts.length - 1].includes('остановлены'));
const quiet = mock.sent.length + mock.notifications.length;
incoming('срочно при выключенных правилах');
eq(mock.sent.length + mock.notifications.length, quiet, 'kill switch');
assert(mock.panelText(panel).includes('Остановлены'), 'panel shows the state');
mock.command('/auto on');
eq(sandbox.settings.enabled, true);
incoming('срочно снова');
eq(mock.notifications.length, notesBefore + 2, 'back on');
advance(61 * 1000);

// --- Фильтры: чат, отправитель, медиа, регулярка ------------------------------

answer({ chats: '@damzachat', sender: '@petya, Маша', text: '/^#todo\\b/i', media: '' });
answer({ a_notify: false, a_react: true, a_forward: true, a_saved: true, a_read: true, a_mute: true, a_typing: true });
answer({ emoji: '🔥', forwardTo: '@news', muteHours: '0,5' });
answer({ name: 'Задачи', allowChannels: false });
await startWizard(W.emptyForm('message'));
const todo = rule('Задачи');
assert(todo, 'todo rule saved');
eq(todo.trigger.chats, ['@damzachat']);
eq(todo.actions.find((a) => a.type === 'forward').to, { id: '300', name: 'Новости' }, 'forward target resolved to id');
eq(todo.actions.find((a) => a.type === 'mute').hours, 0.5);

const realGet = dzg.chats.get;
dzg.chats.get = (p) => (String(p) === '555' ? mock.peer(555, 'Петя', 'user', { username: 'petya' }) : realGet(p));
inGroup('#todo купить молоко', { id: 2001 });
dzg.chats.get = realGet;
await settle();
eq(reacts, [{ peerId: '200', id: 2001, emoji: '🔥' }]);
eq(forwards, [{ from: '200', id: 2001, to: '300' }, { from: '200', id: 2001, to: '1' }], 'forward + saved');
eq(mock.read[mock.read.length - 1], '200');
eq(mutes, [{ peerId: '200', seconds: 1800 }]);
eq(typing, [{ peerId: '200', action: 'typing' }]);
assert(isGuarded('300') && isGuarded('1'), 'forward targets cool down too');

// Не тот отправитель / не тот чат / не тот текст.
const reactsBefore = reacts.length;
advance(LOOP_GUARD_MS + 1);
inGroup('#todo от Васи', { fromId: '777', fromName: 'Вася' });
inGroup('#todo от Маши Ивановой', { fromId: '778', fromName: 'Маша Иванова', id: 2002 });
incoming('#todo в личке', { fromName: 'Маша' });
inGroup('не #todo', { fromName: 'Маша' });
eq(reacts.length, reactsBefore + 1, 'only the Маша message in the group matched');
eq(reacts[reacts.length - 1].id, 2002);
advance(61 * 1000);

// Медиа: голосовые из личек в Избранное, с запасным вариантом копией.
answer({ media: 'голосовое, кружок', kGroup: false, kChannel: false });
answer({ a_notify: false, a_saved: true });
answer({ name: 'Голосовые' });
await startWizard(W.emptyForm('message'));
assert(rule('Голосовые'), 'voices rule');
eq(boxes[boxes.length - 1].toggles.some((t) => t.id === 'allowBots'), false, 'no bot question without chatty actions');
forwards.length = 0;
incoming('', { media: 'photo' });
eq(forwards.length, 0, 'photo does not match');
forwardFails = true;
const sentBeforeVoice = mock.sent.length;
incoming('', { media: 'voice', id: 3001 });
await settle();
forwardFails = false;
eq(forwards.length, 1);
eq(mock.sent.length, sentBeforeVoice + 1, 'fallback copy');
eq(mock.sent[mock.sent.length - 1].peerId, '1');
assert(mock.sent[mock.sent.length - 1].text.includes('[голосовое]'));
advance(61 * 1000);

// --- Пробный режим и webhook --------------------------------------------------

mock.httpHandler = () => ({ status: 500, body: 'oops' });
answer({ text: 'деплой' });
answer({ a_notify: false, a_webhook: true, a_reply: true });
answer({ replyText: 'ок', webhookUrl: 'ftp://nope' });                        // плохой адрес
answer({ webhookUrl: 'https://hooks.example.com/dzg' });                       // исправили
answer({ name: 'Деплой', dryRun: true });
await startWizard(W.emptyForm('message'));
assert(mock.alerts.some((a) => String(a.text).includes('https://')), 'bad url explained');
const sentBeforeDry = mock.sent.length;
incoming('деплой готов');
await settle();
eq(mock.sent.length, sentBeforeDry, 'dry run sends nothing');
eq(mock.http.length, 0, 'dry run calls nothing');
eq(lastLog().kind, 'dry');
assert(lastLog().text.includes('пробно') && lastLog().text.includes('webhook hooks.example.com'), lastLog().text);

const deploy = rule('Деплой');
deploy.dryRun = false;
advance(LOOP_GUARD_MS + 1);
incoming('деплой ещё раз', { id: 4001 });
await settle();
eq(mock.http.length, 1);
eq(mock.http[0].url, 'https://hooks.example.com/dzg');
eq(mock.http[0].options.method, 'POST');
const payload = mock.http[0].options.json;
eq(payload.rule.name, 'Деплой');
eq(payload.message.id, 4001);
eq(payload.message.text, 'деплой ещё раз');
eq(payload.chat.id, '100');
assert(getLog().some((e) => e.kind === 'error' && e.text.includes('ответил 500')), 'webhook error logged');
advance(61 * 1000);

// --- Расписание ---------------------------------------------------------------

now = new Date(2026, 9, 12, 8, 59, 0, 0).getTime();
answer({ when: '09:00, 18:30', chats: 'избранное, @damzachat' });
answer({ a_notify: true, a_reply: true });
answer({ notifyText: 'Пора: {rule}', replyText: 'Доброе утро! {date}' });
answer({ name: 'Утро' });
mock.click(panel, 'Добавить правило');
mock.click(panel, 'По расписанию');
await settle();
const morning = rule('Утро');
assert(morning, 'schedule rule saved');
eq(morning.trigger.chats, ['1', '200'], 'usernames resolved to ids');
const sentBeforeSchedule = mock.sent.length;
runSchedule(now);
eq(mock.sent.length, sentBeforeSchedule, 'not yet');
now = new Date(2026, 9, 12, 9, 0, 20, 0).getTime();
runSchedule(now);
eq(mock.sent.length, sentBeforeSchedule + 2, 'sent to both chats');
eq(mock.sent[mock.sent.length - 2], { peerId: '1', text: 'Доброе утро! 12.10.2026', options: {} });
eq(mock.notifications[mock.notifications.length - 1].text, 'Пора: Утро');
runSchedule(now + 30000);
eq(mock.sent.length, sentBeforeSchedule + 2, 'once per slot');
// Клиент был закрыт весь день: пропущенный слот не догоняется.
now = new Date(2026, 9, 12, 20, 0, 0, 0).getTime();
runSchedule(now);
eq(mock.sent.length, sentBeforeSchedule + 2, 'missed slot beyond grace is skipped');
now = new Date(2026, 9, 13, 9, 3, 0, 0).getTime();
runSchedule(now);
eq(mock.sent.length, sentBeforeSchedule + 4, 'next day, within grace');
eq(R.nextRun(morning, now), new Date(2026, 9, 13, 18, 30, 0, 0).getTime());

// Интервал и будни.
const every = R.normalizeRule({ trigger: { type: 'schedule', schedule: { kind: 'every', minutes: 30 }, weekdays: true }, actions: [{ type: 'notify' }], created: 1, state: { armedAt: now } });
eq(R.dueSlot(every, now + 29 * 60000), 0);
assert(R.dueSlot(every, now + 30 * 60000) > 0);
const saturday = new Date(2026, 9, 17, 12, 0).getTime();
eq(R.dueSlot(every, saturday), 0, 'weekdays only');
// Сообщения по расписанию — не чаще раза в 5 минут (уведомления можно чаще).
eq(R.normalizeRule({ trigger: { type: 'schedule', schedule: { kind: 'every', minutes: 1 } }, actions: [{ type: 'reply', text: 'спам' }] }).trigger.schedule.minutes, 5);
eq(R.normalizeRule({ trigger: { type: 'schedule', schedule: { kind: 'every', minutes: 1 } }, actions: [{ type: 'notify' }] }).trigger.schedule.minutes, 1);
const alertsBefore = mock.alerts.length;
answer({ when: '1m', chats: 'избранное' });
answer({ a_notify: false, a_reply: true });
answer({}, 'cancel');
await startWizard(W.emptyForm('schedule'));
assert(mock.alerts.slice(alertsBefore).some((a) => String(a.text).includes('не чаще раза в 5 минут')), 'wizard refuses minute-spam');
answer({ when: '10:00', chats: '' });
answer({ a_notify: false, a_reply: true });
answer({}, 'cancel');
await startWizard(W.emptyForm('schedule'));
assert(mock.alerts[mock.alerts.length - 1].text.includes('нужны чаты'), 'schedule send needs chats');
// Правило, созданное в 09:03, не стреляет за 09:00.
const late = R.normalizeRule({ trigger: { type: 'schedule', schedule: { kind: 'daily', times: ['09:00'] } }, actions: [{ type: 'notify' }], created: new Date(2026, 9, 13, 9, 3).getTime() });
eq(R.dueSlot(late, new Date(2026, 9, 13, 9, 4).getTime()), 0);
const morningToggle = mock.visible(panel).find((i) => i.type === 'toggle' && i.text.includes('Утро'));
morningToggle.onChange(false);
eq(rule('Утро').enabled, false, 'toggle in the panel');
eq(sandbox.storage.rules.find((r) => r.name === 'Утро').enabled, false, 'saved right away');
advance(86400000);
runSchedule(new Date(2026, 9, 14, 9, 0, 30).getTime());
eq(mock.sent.length, sentBeforeSchedule + 4, 'disabled rule is silent');

// --- Открытие чата ------------------------------------------------------------

answer({ chats: '', kGroup: false, kChannel: false });
answer({ a_notify: false, a_read: true });
answer({ name: 'Читать личку' });
await startWizard(W.emptyForm('chatOpened'));
const readBefore = mock.read.length;
sandbox.event('chat.opened', group);
eq(mock.read.length, readBefore, 'group is not private');
sandbox.event('chat.opened', vasya);
eq(mock.read[mock.read.length - 1], '100');

// Открытие чата с ответом: повторное открытие за 10 секунд не пишет снова.
answer({ chats: 'текущий' });
answer({ a_notify: false, a_reply: true });
answer({ replyText: 'Я тут, {chat}' });
answer({ name: 'Привет при открытии' });
await startWizard(W.emptyForm('chatOpened'));
const sentBeforeOpen = mock.sent.length;
sandbox.event('chat.opened', vasya);
sandbox.event('chat.opened', vasya);
eq(mock.sent.length, sentBeforeOpen + 1, 're-open within the guard is quiet');
eq(mock.sent[mock.sent.length - 1], { peerId: '100', text: 'Я тут, Вася Пупкин', options: {} });
sandbox.event('chat.opened', news);
eq(mock.sent.length, sentBeforeOpen + 1, 'only the chosen chat');
setRuleOff('Привет при открытии');
function setRuleOff(name) {
  const toggle = mock.visible(mock.lastPanel()).find((i) => i.type === 'toggle' && i.text.includes(name));
  toggle.onChange(false);
}
advance(LOOP_GUARD_MS + 1);

// --- Проверка на последнем сообщении ------------------------------------------

mock.current = vasya;
loaded = [msg({ id: 9001, text: 'старое' }), msg({ id: 9002, text: 'а это срочно', fromName: 'Вася Пупкин' }), msg({ id: 9000, text: 'ещё старее' })];
advance(61 * 1000);
answer({}, 'close');
clickFor('Срочное', 'Проверить на последнем сообщении');
await settle();
let check = lastBox();
assert(check.text.includes('а это срочно') && check.text.includes('✅ В тексте есть «срочно»'), check.text);
assert(check.text.includes('Правило сработает'), 'preview');
assert(check.buttons.some((b) => b.id === 'run'), 'can run now');
const sentBeforeTest = mock.sent.length;
eq(mock.sent.length, sentBeforeTest, 'checking does nothing by itself');
answer({}, 'run');
clickFor('Срочное', 'Проверить на последнем сообщении');
await settle();
eq(mock.sent.length, sentBeforeTest + 1, 'run from the check');
eq(mock.sent[mock.sent.length - 1].options.replyTo, 9002);
advance(LOOP_GUARD_MS + 1);

loaded = [msg({ id: 9100, text: 'ничего особенного' })];
answer({}, 'close');
clickFor('Срочное', 'Проверить на последнем сообщении');
await settle();
check = lastBox();
assert(check.text.includes('❌') && check.text.includes('не сработает'), check.text);
eq(check.buttons.map((b) => b.id), ['close']);

// Меню сообщения: какие правила сработают.
const explained = explainMessage(msg({ text: 'это срочно' }));
assert(explained.includes('✅ Срочное') && explained.includes('❌ Задачи'), explained);
mock.menuClick('message', msg({ text: 'срочно' }), 'Какие правила');
assert(mock.alerts[mock.alerts.length - 1].text.includes('✅ Срочное'));

// --- Правка правила -----------------------------------------------------------

const statsBefore = rule('Срочное').stats.fired;
answer({});                         // условия как есть
answer({});                         // действия как есть
answer({ replyText: 'Скоро отвечу' });
answer({ name: 'Срочное!', limit: '3' });
clickFor('Срочное', 'Изменить');
await settle();
assert(boxes[boxes.length - 4].inputs.find((i) => i.id === 'text').value === 'срочно, asap', 'form is prefilled');
const edited = rule('Срочное!');
assert(edited, 'renamed');
eq(edited.id, urgent.id, 'same id');
eq(edited.stats.fired, statsBefore, 'stats kept');
eq(edited.limit, 3);
eq(edited.actions.find((a) => a.type === 'reply').text, 'Скоро отвечу');

// Ошибка в шаге — окно показывается снова с введёнными значениями.
answer({ text: '/(/' });
answer({ text: 'ok' });
answer({});
answer({});
answer({ name: 'Регулярка' });
await startWizard(W.emptyForm('message'));
assert(mock.alerts.some((a) => String(a.text).includes('регулярном')), 'regex error shown');
assert(rule('Регулярка'), 'saved after the fix');

// Закрытое окно — ничего не сохраняется.
const countBefore = getRules().length;
answer({ text: 'x' }, 'cancel');
await startWizard(W.emptyForm('message'));
eq(getRules().length, countBefore);

// --- Удаление -----------------------------------------------------------------

mock.confirmAnswer = true;
clickFor('Регулярка', 'Удалить');
await settle();
assert(!rule('Регулярка'), 'deleted');
assert(!sandbox.storage.rules.some((r) => r.name === 'Регулярка'));

// --- Экспорт и импорт ---------------------------------------------------------

mock.command('/auto export');
const exported = JSON.parse(mock.clipboard);
eq(exported.type, 'damzagram-automations');
eq(exported.rules.length, getRules().length);
assert(!('stats' in exported.rules[0]) && !('id' in exported.rules[0]), 'no runtime fields');

const before = getRules().length;
mock.clipboard = JSON.stringify({ rules: [exported.rules[0], { trigger: { type: 'nope' } }] });
mock.command('/auto import');
await settle();
eq(getRules().length, before + 1, 'valid rule imported');
const imported = getRules()[getRules().length - 1];
eq(imported.enabled, false, 'imported rules start disabled');
eq(imported.name, exported.rules[0].name + ' (2)', 'name collision resolved');
assert(imported.id !== getRules()[0].id);
assert(mock.alerts[mock.alerts.length - 1].text.includes('пропущено: 1'), 'invalid counted');

mock.clipboard = 'не json';
mock.command('/auto import');
await settle();
assert(mock.alerts[mock.alerts.length - 1].text.includes('не JSON'));

// --- Шаблоны и меню чата ------------------------------------------------------

mock.click(panel, 'Добавить правило');
assert(mock.panelText(panel).includes('Голосовые и кружки'), 'presets listed');
answer({});
answer({});
answer({});
answer({});
mock.click(panel, 'Автоответ на «привет»');
await settle();
const hello = rule('Автоответ на «привет»');
assert(hello && hello.dryRun && hello.limit === 2, 'preset keeps dry run and limit');

answer({});
answer({ a_notify: true });
answer({});
answer({ name: 'Только DamZaGram' });
mock.menuClick('chat', group, 'Правило для этого чата');
await settle();
eq(rule('Только DamZaGram').trigger.chats, ['200']);

// --- Выгрузка -----------------------------------------------------------------

flush();
sandbox.event('unload');
assert(!panel.open, 'panel closed on unload');
eq(sandbox.storage.rules.length, getRules().length, 'everything is on disk');
