// Автоответчик: «я не в сети» в личных чатах.
// Включается вручную (/away on, пункт главного меню) или по расписанию.
// Одному человеку отвечает не чаще раза в N минут.
// Автор: @wetakush

const STATE_KEY = 'state';
const REPLIED_KEY = 'replied';
const MAX_AGE_SECONDS = 120;
const ECHO_MS = 30 * 1000;

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return (value === undefined || value === null || value === '') ? fallback : value;
};

const DEFAULT_TEXT = 'Привет, {name}! Я сейчас не у компьютера и отвечу, как только вернусь.\n— автоответ DamZaGram';

// --- Расписание -----------------------------------------------------------

export function parseSchedule(text) {
  const ranges = [];
  for (const part of String(text || '').split(/[,;]/)) {
    const m = /^\s*(\d{1,2})[:.](\d{2})\s*[-–—]\s*(\d{1,2})[:.](\d{2})\s*$/.exec(part);
    if (!m) continue;
    const from = Number(m[1]) * 60 + Number(m[2]);
    const to = Number(m[3]) * 60 + Number(m[4]);
    if (from < 24 * 60 && to <= 24 * 60 && from !== to) ranges.push({ from, to });
  }
  return ranges;
}

export function inSchedule(ranges, date) {
  const minutes = date.getHours() * 60 + date.getMinutes();
  return ranges.some(({ from, to }) => (from < to)
    ? (minutes >= from && minutes < to)
    : (minutes >= from || minutes < to));
}

// --- Состояние ------------------------------------------------------------

function state() {
  return Object.assign({ on: false, since: 0, text: '', replies: 0 }, dzg.storage.get(STATE_KEY, {}));
}

function setState(patch) {
  const next = Object.assign(state(), patch);
  dzg.storage.set(STATE_KEY, next);
  updateMenu();
  return next;
}

export function isAway(now) {
  if (state().on) return true;
  return inSchedule(parseSchedule(setting('schedule', '')), now || new Date());
}

export function turnOn(text) {
  setState({ on: true, since: Date.now(), text: String(text || '').trim(), replies: 0 });
  dzg.ui.toast('Автоответчик включён' + (text ? ': «' + String(text).trim() + '»' : ''), '💤 Не в сети');
}

export function turnOff(reason) {
  const s = state();
  if (!s.on) return;
  setState({ on: false, text: '' });
  dzg.ui.toast((reason || 'Автоответчик выключен') + (s.replies ? '. Ответов отправлено: ' + s.replies : ''), '💤 Автоответчик');
}

// --- Ответ ----------------------------------------------------------------

const sentRecently = []; // { peerId, text, at } — чтобы не принять свой автоответ за активность

function firstName(message) {
  const name = String(message.fromName || (message.chat && message.chat.name) || '').trim();
  return name.split(/\s+/)[0] || 'привет';
}

export function replyText(message) {
  const custom = state().text;
  const template = custom || String(setting('text', DEFAULT_TEXT));
  return template.replace(/\{name\}/g, firstName(message));
}

let contacts = null;
let contactsAt = 0;

function isContact(peerId) {
  if (!dzg.users || !dzg.users.contacts) return true;
  if (!contacts || Date.now() - contactsAt > 5 * 60 * 1000) {
    contactsAt = Date.now();
    try {
      contacts = new Set((dzg.users.contacts() || []).map((p) => p.id));
    } catch (e) {
      contacts = new Set();
    }
  }
  return contacts.has(String(peerId));
}

function isEcho(message) {
  const now = Date.now();
  while (sentRecently.length && now - sentRecently[0].at > ECHO_MS) sentRecently.shift();
  const peerId = String(message.peerId || message.chat.id);
  return sentRecently.some((s) => s.peerId === peerId && s.text.trim() === String(message.text || '').trim());
}

function onMessage(m) {
  if (!m || m.service || !m.chat) return;
  if (m.out) {
    if (m.chat.isSelf || isEcho(m)) return;
    if (state().on && setting('autoOff', true)) turnOff('Ты снова пишешь — автоответчик выключен');
    return;
  }
  if (m.chat.type !== 'user' || m.chat.isSelf) return;
  if (m.date && Date.now() / 1000 - m.date > MAX_AGE_SECONDS) return;
  if (!isAway()) return;
  const peerId = String(m.peerId || m.chat.id);
  if (setting('onlyContacts', false) && !isContact(peerId)) return;

  const cooldown = Math.max(0, Number(setting('cooldownMinutes', 60)) || 0) * 60 * 1000;
  const replied = dzg.storage.get(REPLIED_KEY, {}) || {};
  if (replied[peerId] && Date.now() - replied[peerId] < cooldown) return;

  const text = replyText(m);
  if (!dzg.messages.send(peerId, text, { replyTo: m.id })) return;
  sentRecently.push({ peerId, text, at: Date.now() });
  replied[peerId] = Date.now();
  // Не копим старые записи.
  for (const key of Object.keys(replied)) {
    if (Date.now() - replied[key] > Math.max(cooldown, 86400000)) delete replied[key];
  }
  dzg.storage.set(REPLIED_KEY, replied);
  setState({ replies: state().replies + 1 });
  if (setting('notifyOnReply', false)) dzg.ui.toast('Ответил: ' + (m.fromName || m.chat.name), '💤 Автоответчик');
}

dzg.on('message.new', onMessage);

// --- Команда и меню -------------------------------------------------------

function clock(ms) {
  const d = new Date(ms);
  return String(d.getHours()).padStart(2, '0') + ':' + String(d.getMinutes()).padStart(2, '0');
}

function status() {
  const s = state();
  const ranges = setting('schedule', '');
  const lines = [
    s.on ? '💤 Включён вручную' + (s.since ? ' с ' + clock(s.since) : '') : 'Вручную выключен',
    ranges ? 'Расписание: ' + ranges + (isAway() && !s.on ? ' (сейчас активно)' : '') : 'Расписание не задано',
    'Ответов с момента включения: ' + s.replies,
    'Текст: ' + (s.text || String(setting('text', DEFAULT_TEXT))),
  ];
  return lines.join('\n');
}

dzg.commands.register('away', {
  description: 'Автоответчик: /away on [текст] · /away off · /away status',
  run: ({ args, rest }) => {
    const action = (args[0] || '').toLowerCase();
    if (action === 'on' || action === 'вкл') {
      turnOn(rest.slice(args[0].length).trim());
    } else if (action === 'off' || action === 'выкл') {
      if (state().on) turnOff();
      else dzg.ui.toast('Автоответчик и так выключен');
    } else if (action === 'status' || action === 'статус') {
      dzg.ui.alert(status(), '💤 Автоответчик');
    } else if (state().on) {
      turnOff();
    } else {
      turnOn('');
    }
  },
});

let removeMenu = null;

function updateMenu() {
  if (!dzg.ui.addMainMenuItem) return;
  if (removeMenu) removeMenu();
  const on = !!(dzg.storage.get(STATE_KEY, {}) || {}).on;
  removeMenu = dzg.ui.addMainMenuItem({
    title: on ? 'Автоответчик: выключить' : 'Автоответчик: включить',
    icon: 'silent',
    onClick: () => (state().on ? turnOff() : turnOn('')),
  });
}

updateMenu();
