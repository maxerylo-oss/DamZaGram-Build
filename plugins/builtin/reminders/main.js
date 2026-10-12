// Напоминания: /remind <когда> <текст>, «Напомнить через…» у сообщения.
// Хранятся в dzg.storage и переживают перезапуск клиента; пропущенные,
// пока клиент был закрыт, срабатывают при следующем запуске.
// Автор: @wetakush

const ITEMS_KEY = 'items';
const MAX_TIMER = 60 * 1000;

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return (value === undefined || value === null || value === '') ? fallback : value;
};

const pad = (n) => String(n).padStart(2, '0');

// --- Разбор времени -------------------------------------------------------

const UNIT = '(секунд[ауы]?|сек|seconds?|secs?|минут[аыу]?|мин|minutes?|mins?|час(?:а|ов)?|hours?|hrs?|дн(?:я|ей)?|день|days?|недел[яиюь]|нед|weeks?|[смчдн]|[smhdw])';
const DURATION = new RegExp('^(\\d+(?:[.,]\\d+)?)\\s*' + UNIT + '(?=[\\s\\d]|$)', 'i');

function unitMs(unit) {
  const u = unit.toLowerCase();
  if (/^(с|s|сек|секунд|sec)/.test(u)) return 1000;
  if (/^(м|m|мин|минут|min)/.test(u)) return 60 * 1000;
  if (/^(ч|h|час|hour|hr)/.test(u)) return 3600 * 1000;
  if (/^(д|d|дн|день|day)/.test(u)) return 86400 * 1000;
  if (/^(н|w|нед|недел|week)/.test(u)) return 7 * 86400 * 1000;
  return 0;
}

function parseClock(token) {
  const m = /^(\d{1,2})[:.](\d{2})$/.exec(token || '');
  if (!m) return null;
  const h = Number(m[1]);
  const min = Number(m[2]);
  return (h < 24 && min < 60) ? { h, min } : null;
}

function parseDate(token) {
  const m = /^(\d{1,2})\.(\d{1,2})(?:\.(\d{2,4}))?$/.exec(token || '');
  if (!m) return null;
  const day = Number(m[1]);
  const month = Number(m[2]);
  if (day < 1 || day > 31 || month < 1 || month > 12) return null;
  let year = m[3] ? Number(m[3]) : 0;
  if (year && year < 100) year += 2000;
  return { day, month, year };
}

/**
 * Разбирает начало строки как время напоминания.
 * Возвращает { at: unix ms, rest: оставшийся текст } или null.
 */
export function parseWhen(input, nowMs) {
  const now = new Date(nowMs === undefined ? Date.now() : nowMs);
  let text = String(input || '').trim().replace(/^(через|in|в|at)\s+/i, '');

  // Длительность: "10m", "1h30m", "1 ч 30 мин", "2д".
  let total = 0;
  let matched = false;
  for (;;) {
    const m = DURATION.exec(text);
    if (!m) break;
    total += Number(m[1].replace(',', '.')) * unitMs(m[2]);
    matched = true;
    text = text.slice(m[0].length).trimStart();
  }
  if (matched) return total > 0 ? { at: now.getTime() + Math.round(total), rest: text } : null;

  const tokens = text.split(/\s+/);
  const take = (n) => tokens.slice(n).join(' ');

  // "завтра [9:00]", "послезавтра", "tomorrow".
  const dayWords = { 'завтра': 1, 'tomorrow': 1, 'послезавтра': 2, 'сегодня': 0, 'today': 0 };
  const word = (tokens[0] || '').toLowerCase();
  if (word in dayWords) {
    const clock = parseClock(tokens[1]);
    const d = new Date(now);
    d.setDate(d.getDate() + dayWords[word]);
    d.setHours(clock ? clock.h : 9, clock ? clock.min : 0, 0, 0);
    if (d.getTime() <= now.getTime()) return null;
    return { at: d.getTime(), rest: take(clock ? 2 : 1) };
  }

  // "25.12 10:00", "25.12.2026".
  const date = parseDate(tokens[0]);
  const dateClock = parseClock(tokens[1]);
  if (date && (dateClock || !parseClock(tokens[0]))) {
    const d = new Date(now);
    d.setFullYear(date.year || now.getFullYear(), date.month - 1, date.day);
    d.setHours(dateClock ? dateClock.h : 9, dateClock ? dateClock.min : 0, 0, 0);
    if (!date.year && d.getTime() <= now.getTime()) d.setFullYear(d.getFullYear() + 1);
    if (d.getTime() <= now.getTime()) return null;
    return { at: d.getTime(), rest: take(dateClock ? 2 : 1) };
  }

  // "18:30": сегодня или завтра, если уже прошло.
  const clock = parseClock(tokens[0]);
  if (clock) {
    const d = new Date(now);
    d.setHours(clock.h, clock.min, 0, 0);
    if (d.getTime() <= now.getTime()) d.setDate(d.getDate() + 1);
    return { at: d.getTime(), rest: take(1) };
  }

  // Просто число — минуты.
  if (/^\d+$/.test(tokens[0] || '') && Number(tokens[0]) > 0) {
    return { at: now.getTime() + Number(tokens[0]) * 60000, rest: take(1) };
  }
  return null;
}

export function formatIn(ms) {
  const minutes = Math.max(1, Math.round(ms / 60000));
  if (minutes < 60) return minutes + ' мин';
  const hours = Math.floor(minutes / 60);
  const rest = minutes % 60;
  if (hours < 24) return hours + ' ч' + (rest ? ' ' + rest + ' мин' : '');
  const days = Math.floor(hours / 24);
  const restHours = hours % 24;
  return days + ' д' + (restHours ? ' ' + restHours + ' ч' : '');
}

export function formatAt(ms) {
  const d = new Date(ms);
  const now = new Date();
  const tomorrow = new Date(now);
  tomorrow.setDate(now.getDate() + 1);
  const time = pad(d.getHours()) + ':' + pad(d.getMinutes());
  if (d.toDateString() === now.toDateString()) return 'сегодня в ' + time;
  if (d.toDateString() === tomorrow.toDateString()) return 'завтра в ' + time;
  return pad(d.getDate()) + '.' + pad(d.getMonth() + 1) + (d.getFullYear() !== now.getFullYear() ? '.' + d.getFullYear() : '') + ' в ' + time;
}

// --- Хранилище ------------------------------------------------------------

function items() {
  const list = dzg.storage.get(ITEMS_KEY, []);
  return Array.isArray(list) ? list : [];
}

function save(list) {
  list.sort((a, b) => a.at - b.at);
  dzg.storage.set(ITEMS_KEY, list);
  schedule();
  if (panel && panel.isOpen()) render();
}

export function add({ at, text, chat, msgId }) {
  const list = items();
  const id = (dzg.storage.get('seq', 0) || 0) + 1;
  dzg.storage.set('seq', id);
  const item = {
    id,
    at,
    text: String(text || '').trim() || 'Напоминание',
    peerId: chat ? chat.id : '',
    chatName: chat ? chat.name : '',
    msgId: msgId || 0,
    created: Date.now(),
  };
  list.push(item);
  save(list);
  return item;
}

function remove(id) {
  save(items().filter((i) => i.id !== id));
}

function snooze(id, ms) {
  const list = items();
  const item = list.find((i) => i.id === id);
  if (!item) return;
  item.at = Math.max(Date.now(), item.at) + ms;
  save(list);
}

// --- Срабатывание ---------------------------------------------------------

let timer = 0;

function schedule() {
  clearTimeout(timer);
  timer = 0;
  const list = items();
  if (!list.length) return;
  const next = Math.min(...list.map((i) => i.at));
  // Короткий шаг: таймер не уплывёт после сна компьютера.
  const delay = Math.max(0, Math.min(MAX_TIMER, next - Date.now()));
  timer = setTimeout(tick, delay);
}

function fire(item, late) {
  const open = () => {
    if (item.peerId) dzg.chats.open(item.peerId, item.msgId || undefined);
  };
  const where = item.chatName ? ' · ' + item.chatName : '';
  const text = item.text + (late ? '\n(пропущено, должно было сработать ' + formatAt(item.at) + ')' : '');
  if (dzg.ui.notify) {
    dzg.ui.notify({ title: '⏰ Напоминание' + where, text, onClick: open, silent: !!setting('silent', false) });
  }
  dzg.ui.toast(text, '⏰ Напоминание');
  if (setting('sendToSaved', false) && dzg.users && dzg.users.self) {
    const self = dzg.users.self();
    const link = item.peerId && item.msgId && dzg.messages.link ? dzg.messages.link(item.peerId, item.msgId) : null;
    if (self) dzg.messages.send(self.id, '⏰ ' + item.text + (link ? '\n' + link : ''));
  }
}

function tick() {
  timer = 0;
  const now = Date.now();
  const list = items();
  const due = list.filter((i) => i.at <= now + 500);
  if (due.length) {
    for (const item of due) fire(item, now - item.at > 2 * 60 * 1000);
    dzg.storage.set(ITEMS_KEY, list.filter((i) => i.at > now + 500));
    if (panel && panel.isOpen()) render();
  }
  schedule();
}

// --- Панель ---------------------------------------------------------------

let panel = null;

function render() {
  if (!panel) return;
  panel.clear();
  const list = items();
  panel.addLabel(list.length
    ? 'Активных напоминаний: ' + list.length
    : 'Напоминаний нет. Напиши в любом чате: /remind 10m текст', { muted: true });
  panel.addSeparator();
  for (const item of list) {
    panel.addLabel('⏰ ' + formatAt(item.at) + ' · через ' + formatIn(item.at - Date.now()), { bold: true });
    panel.addLabel(item.text + (item.chatName ? '\n' + item.chatName : ''));
    if (item.peerId) panel.addButton('↪ открыть', () => dzg.chats.open(item.peerId, item.msgId || undefined), { style: 'light' });
    panel.addButton('+1 час', () => snooze(item.id, 3600 * 1000), { style: 'light' });
    panel.addButton('✕ удалить', () => remove(item.id), { style: 'light' });
  }
  panel.addSeparator();
  panel.addLabel('Форматы: 10m, 1h30m, 2д, 18:30, завтра 9:00, 25.12 10:00', { muted: true });
}

export function show() {
  if (!dzg.ui.panel) {
    dzg.ui.alert(items().map((i) => formatAt(i.at) + ' — ' + i.text).join('\n') || 'Напоминаний нет', 'Напоминания');
    return;
  }
  if (!panel || !panel.isOpen()) {
    panel = dzg.ui.panel({ title: '⏰ Напоминания', width: 360 });
    panel.on('close', () => { panel = null; });
  }
  render();
}

// --- Команды и меню -------------------------------------------------------

function confirmAdded(item) {
  dzg.ui.toast('Напомню ' + formatAt(item.at) + ' (через ' + formatIn(item.at - Date.now()) + ')', '⏰ ' + item.text);
}

async function remindCommand({ rest, chat, replyTo }) {
  if (!rest || rest === 'list') {
    show();
    return;
  }
  if (rest === 'clear') {
    if (await dzg.ui.confirm('Удалить все напоминания?', { confirmText: 'Удалить', cancelText: 'Отмена' })) save([]);
    return;
  }
  const when = parseWhen(rest);
  if (!when) {
    dzg.ui.toast('Не понял время. Примеры: /remind 10m текст, /remind 18:30 текст, /remind завтра 9:00 текст');
    return;
  }
  confirmAdded(add({ at: when.at, text: when.rest, chat, msgId: replyTo || 0 }));
}

dzg.commands.register('remind', {
  description: 'Напомнить: /remind 10m текст, /remind 18:30 текст, /remind завтра 9:00 текст',
  run: remindCommand,
});

dzg.commands.register('reminders', {
  description: 'Список напоминаний',
  run: () => show(),
});

async function askAndAdd(title, base) {
  const answer = await dzg.ui.prompt(title, {
    text: 'Когда напомнить? 10m, 1h30m, 18:30, завтра 9:00. Через пробел можно дописать текст.',
    placeholder: '1h',
    value: String(setting('defaultDelay', '1h')),
    confirmText: 'Напомнить',
    cancelText: 'Отмена',
  });
  if (answer === null || answer === undefined) return;
  const when = parseWhen(answer);
  if (!when) {
    dzg.ui.toast('Не понял время: ' + answer);
    return;
  }
  confirmAdded(add(Object.assign({}, base, { at: when.at, text: when.rest || base.text })));
}

const snippet = (m) => {
  const text = String(m.text || '').replace(/\s+/g, ' ').trim();
  const head = text ? (text.length > 120 ? text.slice(0, 119) + '…' : text) : (m.media ? '[' + m.media + ']' : 'сообщение');
  return (m.out ? '' : (m.fromName ? m.fromName + ': ' : '')) + head;
};

dzg.ui.addMessageMenuItem({
  title: '⏰ Напомнить через…',
  onClick: (m) => askAndAdd('Напомнить о сообщении', { text: snippet(m), chat: m.chat, msgId: m.id }),
});

dzg.ui.addChatMenuItem({
  title: '⏰ Напомнить об этом чате…',
  onClick: (chat) => askAndAdd('Напомнить о чате «' + chat.name + '»', { text: 'Зайти в «' + chat.name + '»', chat, msgId: 0 }),
});

if (dzg.ui.addMainMenuItem) {
  dzg.ui.addMainMenuItem({ title: 'Напоминания', icon: 'schedule', onClick: show });
}

// Пропущенные за время, пока клиент был закрыт, сработают сразу.
schedule();
dzg.on('ready', schedule);

dzg.on('unload', () => {
  clearTimeout(timer);
  if (panel) panel.close();
});
