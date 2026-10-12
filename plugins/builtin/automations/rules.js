// Модель правил «Автоматизаций»: разбор полей редактора, проверка и
// нормализация правил (в том числе импортированных), сопоставление с
// сообщением, шаблоны текста, расписание и описания для людей.
// Здесь нет побочных эффектов: всё, что трогает клиент, живёт в main.js.
// Автор: @wetakush

export const FORMAT = 'damzagram-automations';
// Пропущенный запуск по расписанию (клиент спал или был закрыт) ещё
// выполняется, если опоздал не больше чем на столько.
export const GRACE_MS = 10 * 60 * 1000;
export const MAX_ACTIONS = 12;
export const MAX_TOKENS = 50;
export const MAX_RULES = 200;
// Сообщения по расписанию — не чаще раза в столько минут.
export const MIN_SEND_INTERVAL = 5;

export const TRIGGERS = {
  message: { icon: '💬', title: 'Новое сообщение' },
  schedule: { icon: '⏰', title: 'По расписанию' },
  chatOpened: { icon: '📂', title: 'Открытие чата' },
};

const ALL = ['message', 'schedule', 'chatOpened'];

// chatty — действия, которые видит собеседник: их не делаем в чатах с
// ботами и в каналах, пока правило явно не разрешит.
export const ACTIONS = [
  { type: 'notify', icon: '🔔', title: 'Уведомление', short: 'уведомление', triggers: ALL },
  { type: 'reply', icon: '↩️', title: 'Ответить текстом', short: 'ответ', triggers: ALL, chatty: true },
  { type: 'react', icon: '👍', title: 'Поставить реакцию', short: 'реакция', triggers: ['message'], chatty: true },
  { type: 'forward', icon: '↪️', title: 'Переслать в чат', short: 'пересылка', triggers: ['message'] },
  { type: 'saved', icon: '⭐', title: 'Сохранить в Избранное', short: 'в Избранное', triggers: ['message'] },
  { type: 'read', icon: '✅', title: 'Прочитать чат', short: 'прочитать', triggers: ALL },
  { type: 'mute', icon: '🔕', title: 'Выключить звук чата', short: 'без звука', triggers: ALL },
  { type: 'webhook', icon: '🌐', title: 'Webhook: POST с JSON', short: 'webhook', triggers: ALL },
  { type: 'typing', icon: '✍️', title: 'Показать «печатает…»', short: '«печатает…»', triggers: ALL, chatty: true },
];
export const ACTION = Object.fromEntries(ACTIONS.map((a) => [a.type, a]));

export function actionTitle(type, trigger) {
  if (type === 'reply' && trigger === 'schedule') return 'Отправить текст в чаты';
  if (type === 'reply' && trigger === 'chatOpened') return 'Отправить текст в чат';
  return ACTION[type] ? ACTION[type].title : type;
}

export function defaultNotify(trigger) {
  if (trigger === 'schedule') return 'Сработало правило «{rule}» · {time}';
  if (trigger === 'chatOpened') return 'Открыт чат «{chat}»';
  return '{sender} · {chat}: {text}';
}

// --- Мелочи -----------------------------------------------------------------

export const pad = (n) => String(n).padStart(2, '0');

const str = (value, max) => {
  const s = (typeof value === 'string') ? value : ((value === undefined || value === null) ? '' : String(value));
  return s.slice(0, max);
};

export function clamp(value, min, max, fallback) {
  const n = Number(value);
  return Number.isFinite(n) ? Math.min(max, Math.max(min, n)) : fallback;
}

export function clip(text, max) {
  const flat = String(text === undefined || text === null ? '' : text).replace(/\s+/g, ' ').trim();
  return flat.length > max ? flat.slice(0, Math.max(1, max - 1)) + '…' : flat;
}

export const escapeRe = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

export function plural(n, forms) {
  const a = Math.abs(n) % 100;
  const b = a % 10;
  if (a > 10 && a < 20) return forms[2];
  if (b === 1) return forms[0];
  if (b >= 2 && b <= 4) return forms[1];
  return forms[2];
}

export const clock = (ms) => {
  const d = new Date(ms);
  return pad(d.getHours()) + ':' + pad(d.getMinutes());
};

export const dateText = (ms) => {
  const d = new Date(ms);
  return pad(d.getDate()) + '.' + pad(d.getMonth() + 1) + '.' + d.getFullYear();
};

export function formatWhen(ms, now) {
  if (!ms) return '—';
  const base = new Date(now === undefined ? Date.now() : now);
  const d = new Date(ms);
  const key = (x) => x.getFullYear() + '-' + x.getMonth() + '-' + x.getDate();
  const shift = (days) => {
    const x = new Date(base);
    x.setDate(x.getDate() + days);
    return key(x);
  };
  const time = clock(ms);
  if (key(d) === key(base)) return 'сегодня в ' + time;
  if (key(d) === shift(-1)) return 'вчера в ' + time;
  if (key(d) === shift(1)) return 'завтра в ' + time;
  return pad(d.getDate()) + '.' + pad(d.getMonth() + 1) +
    (d.getFullYear() !== base.getFullYear() ? '.' + d.getFullYear() : '') + ' в ' + time;
}

export function hoursText(hours) {
  if (hours < 1) return Math.max(1, Math.round(hours * 60)) + ' мин';
  if (Number.isInteger(hours)) return hours + ' ч';
  return hours.toFixed(1).replace('.', ',') + ' ч';
}

export const host = (url) => String(url || '').replace(/^https?:\/\//i, '').split(/[/?#]/)[0];

// --- Чаты -------------------------------------------------------------------

const HERE = new Set(['текущий', 'текущая', 'текущее', 'этот', 'здесь', 'current', 'this', 'here']);
const SAVED = new Set(['избранное', 'saved', 'me', 'self', 'я']);
const USERNAME = /^@?([a-z][a-z0-9_]{2,31})$/i;
const NUMERIC = /^-?\d{1,20}$/;

export const KIND_LABEL = { private: 'личный чат', group: 'группа', channel: 'канал' };

export function kindOf(peer) {
  const type = peer && peer.type;
  if (type === 'user' || type === 'bot') return 'private';
  if (type === 'channel') return 'channel';
  return 'group';
}

/**
 * Список чатов из поля редактора: @username, id, ссылки t.me/…,
 * «текущий» (открытый сейчас чат) и «избранное».
 * env: { current, self, lookup(id) -> Peer|null }.
 * Возвращает { tokens, names } или { error }.
 */
export function parseChats(input, env) {
  const e = env || {};
  const tokens = [];
  const names = {};
  const add = (token, name) => {
    if (tokens.includes(token)) return;
    tokens.push(token);
    if (name) names[token] = String(name).slice(0, 128);
  };
  for (const raw of String(input || '').split(/[\s,;]+/)) {
    const part = raw.replace(/^[«"']+|[»"']+$/g, '').replace(/^(?:https?:\/\/)?(?:t|telegram)\.me\//i, '');
    if (!part) continue;
    const low = part.toLowerCase();
    if (HERE.has(low)) {
      if (!e.current) return { error: 'Нет открытого чата: «текущий» не к чему привязать. Открой чат или напиши его @username.' };
      add(String(e.current.id), e.current.name);
    } else if (SAVED.has(low)) {
      if (!e.self) return { error: 'Аккаунт ещё не загрузился — не знаю, где «Избранное».' };
      add(String(e.self.id), 'Избранное');
    } else if (NUMERIC.test(part)) {
      const peer = e.lookup ? e.lookup(part) : null;
      if (peer && peer.id) add(String(peer.id), peer.isSelf ? 'Избранное' : peer.name);
      else add(part, '');
    } else if (USERNAME.test(part)) {
      add('@' + USERNAME.exec(part)[1].toLowerCase(), '');
    } else {
      return { error: 'Не понял «' + part + '». Нужен @username, id чата, «текущий» или «избранное».' };
    }
    if (tokens.length > MAX_TOKENS) return { error: 'Слишком много чатов: не больше ' + MAX_TOKENS + '.' };
  }
  return { tokens, names };
}

export function chatMatches(tokens, peer) {
  if (!tokens || !tokens.length) return true;
  if (!peer) return false;
  const id = String(peer.id);
  const tg = String(peer.tgId || '');
  const username = peer.username ? '@' + String(peer.username).toLowerCase() : '';
  return tokens.some((t) => (t[0] === '@')
    ? (username !== '' && t === username)
    : (t === id || (tg !== '' && (t === tg || t === '-' + tg || t === '-100' + tg))));
}

export function chatLabel(token, names) {
  const name = names && names[token];
  if (name) return name;
  return token;
}

// --- Отправитель ------------------------------------------------------------

/** «@vasya, 12345, Вася Пупкин» -> ['@vasya', '12345', 'Вася Пупкин'] или { error }. */
export function parseSenders(input) {
  const list = [];
  for (const raw of String(input || '').split(/\s*[,;\n]\s*/)) {
    const s = raw.trim();
    if (!s) continue;
    if (s[0] === '@') {
      if (!USERNAME.test(s)) return { error: 'Не понял отправителя «' + s + '»: после @ нужен юзернейм.' };
      list.push(s.toLowerCase());
    } else {
      list.push(s.slice(0, 64));
    }
    if (list.length > MAX_TOKENS) return { error: 'Слишком много отправителей: не больше ' + MAX_TOKENS + '.' };
  }
  return { list: Array.from(new Set(list)) };
}

export function compileSenders(list) {
  if (!list || !list.length) return null;
  const result = { ids: new Set(), usernames: new Set(), names: [] };
  for (const s of list) {
    if (NUMERIC.test(s)) result.ids.add(s);
    else if (s[0] === '@') result.usernames.add(s.slice(1).toLowerCase());
    else result.names.push(s.toLowerCase());
  }
  return result;
}

/** lookup(id) нужен только для юзернеймов и «голых» id: зовётся лениво. */
export function senderMatches(cs, m, lookup) {
  if (!cs) return true;
  const fromId = String(m.fromId || '');
  if (cs.ids.has(fromId)) return true;
  const fromName = String(m.fromName || '').toLowerCase();
  if (cs.names.some((n) => fromName.includes(n))) return true;
  if (!cs.usernames.size && !cs.ids.size && !cs.names.length) return false;
  const peer = (lookup && fromId) ? lookup(fromId) : null;
  if (!peer) return false;
  const username = String(peer.username || '').toLowerCase();
  if (username && (cs.usernames.has(username) || cs.names.includes(username))) return true;
  const tg = String(peer.tgId || '');
  return tg !== '' && (cs.ids.has(tg) || cs.ids.has('-' + tg) || cs.ids.has('-100' + tg));
}

// --- Текст ------------------------------------------------------------------

/**
 * «срочно, asap» — любое из слов (без учёта регистра);
 * «/шаблон/флаги» — регулярное выражение (без флагов — без учёта регистра).
 * Возвращает { re } (re = null, если условия нет) или { error }.
 */
export function parsePattern(input) {
  const s = String(input || '').trim();
  if (!s) return { re: null };
  const m = /^\/([\s\S]+)\/([a-z]*)$/.exec(s);
  if (m) {
    const flags = Array.from(new Set((m[2] || 'i').replace(/[gyd]/g, ''))).join('');
    const unicode = (flags.includes('u') || flags.includes('v')) ? flags : flags + 'u';
    try {
      return { re: new RegExp(m[1], unicode) };
    } catch (e) {
      try {
        return { re: new RegExp(m[1], flags) };
      } catch (e2) {
        return { error: 'Ошибка в регулярном выражении: ' + (e2 && e2.message ? e2.message : e2) };
      }
    }
  }
  const words = splitWords(s);
  if (!words.length) return { re: null };
  return { re: new RegExp(words.map(escapeRe).join('|'), 'iu'), words };
}

export const isRegexText = (s) => /^\/[\s\S]+\/[a-z]*$/.test(String(s || '').trim());

export const splitWords = (s) => String(s || '').split(/\s*[,;\n]\s*/).map((w) => w.trim()).filter(Boolean);

// --- Медиа ------------------------------------------------------------------

const MEDIA_WORDS = {
  'фото': 'photo', 'фотка': 'photo', 'фотки': 'photo', 'фотография': 'photo', 'фотографии': 'photo', 'картинка': 'photo', 'картинки': 'photo', 'photo': 'photo', 'photos': 'photo',
  'видео': 'video', 'video': 'video', 'videos': 'video',
  'гиф': 'gif', 'гифка': 'gif', 'гифки': 'gif', 'gif': 'gif', 'анимация': 'gif',
  'стикер': 'sticker', 'стикеры': 'sticker', 'sticker': 'sticker', 'stickers': 'sticker',
  'голосовое': 'voice', 'голосовые': 'voice', 'голосовуха': 'voice', 'войс': 'voice', 'voice': 'voice',
  'кружок': 'videoNote', 'кружки': 'videoNote', 'кружочек': 'videoNote', 'videonote': 'videoNote', 'video_note': 'videoNote',
  'аудио': 'audio', 'музыка': 'audio', 'песня': 'audio', 'audio': 'audio', 'music': 'audio',
  'файл': 'file', 'файлы': 'file', 'документ': 'file', 'документы': 'file', 'file': 'file', 'document': 'file',
  'опрос': 'poll', 'опросы': 'poll', 'poll': 'poll',
  'другое': 'other', 'other': 'other',
  'любое': '*any', 'любой': '*any', 'любые': '*any', 'медиа': '*any', 'вложение': '*any', 'any': '*any',
  'нет': '*none', 'none': '*none', 'текст': '*none',
};

export const MEDIA_LABEL = {
  photo: 'фото', video: 'видео', gif: 'GIF', sticker: 'стикер', voice: 'голосовое',
  videoNote: 'кружок', audio: 'аудио', file: 'файл', poll: 'опрос', other: 'другое',
  '*any': 'любое медиа', '*none': 'без медиа',
};

export function parseMedia(input) {
  let s = String(input || '').toLowerCase().trim();
  if (!s) return { list: [] };
  s = s.replace(/без\s+медиа|без\s+вложений|только\s+текст/g, 'нет').replace(/любое\s+медиа|с\s+медиа/g, 'любое');
  const set = new Set();
  for (const word of s.split(/[\s,;]+/).filter(Boolean)) {
    const value = MEDIA_WORDS[word];
    if (!value) {
      return { error: 'Не знаю тип медиа «' + word + '». Можно: фото, видео, гиф, стикер, голосовое, кружок, аудио, файл, опрос, «любое» или «нет».' };
    }
    set.add(value);
  }
  return cleanMediaSet(set);
}

function cleanMediaSet(set) {
  if (set.has('*none') && set.size > 1) return { error: '«нет» (только текст) нельзя сочетать с типами медиа.' };
  if (set.has('*any')) return { list: ['*any'] };
  return { list: Array.from(set) };
}

export function mediaMatches(list, m) {
  if (!list || !list.length) return true;
  if (list[0] === '*none') return !m.media;
  if (list[0] === '*any') return !!m.media;
  return !!m.media && list.includes(m.media);
}

// --- Расписание -------------------------------------------------------------

/** «09:00», «09:00, 18:30» — каждый день; «30», «30m», «2h», «каждые 15 мин» — интервал. */
export function parseWhen(input) {
  const original = String(input || '').trim();
  let s = original.toLowerCase();
  if (!s) return { error: 'Укажи, когда запускать: например 09:00 или 30m.' };
  const parts = s.split(/[\s,;]+/).filter(Boolean);
  if (parts.every((p) => /^\d{1,2}[:.]\d{2}$/.test(p))) {
    const times = [];
    for (const p of parts) {
      const [h, m] = p.split(/[:.]/).map(Number);
      if (h > 23 || m > 59) return { error: 'Нет такого времени: ' + p + '.' };
      times.push(pad(h) + ':' + pad(m));
    }
    const unique = Array.from(new Set(times)).sort();
    if (unique.length > 24) return { error: 'Не больше 24 запусков в день.' };
    return { schedule: { kind: 'daily', times: unique } };
  }
  s = s.replace(/^(?:каждые|каждый|каждую|каждое|раз\s+в|every)\s*/, '');
  if (/^(?:час|hour|h|ч)$/.test(s)) return { schedule: { kind: 'every', minutes: 60 } };
  if (/^(?:минуту|минута|minute|min|мин)$/.test(s)) return { schedule: { kind: 'every', minutes: 1 } };
  const m = /^(\d+(?:[.,]\d+)?)\s*(m|min|mins|minutes?|м|мин|минут[аыу]?|h|hr|hrs|hours?|ч|час(?:а|ов)?)?$/.exec(s);
  if (!m) return { error: 'Не понял «' + original + '». Примеры: 09:00 · 09:00, 18:30 · 30m · 2h.' };
  const n = Number(m[1].replace(',', '.'));
  const hours = !!m[2] && /^(?:h|hr|hour|ч|час)/.test(m[2]);
  const minutes = Math.round(n * (hours ? 60 : 1));
  if (!(minutes >= 1)) return { error: 'Интервал — не меньше минуты.' };
  if (minutes > 10080) return { error: 'Интервал — не больше недели (10080 минут).' };
  return { schedule: { kind: 'every', minutes } };
}

export function whenText(schedule) {
  if (!schedule) return '';
  if (schedule.kind === 'daily') return schedule.times.join(', ');
  return (schedule.minutes % 60 === 0) ? (schedule.minutes / 60) + 'h' : schedule.minutes + 'm';
}

export function describeSchedule(schedule, weekdays) {
  if (!schedule) return 'расписание не задано';
  const days = weekdays ? 'по будням' : '';
  if (schedule.kind === 'daily') {
    return (days || 'каждый день') + ' в ' + schedule.times.join(', ');
  }
  const m = schedule.minutes;
  let every;
  if (m === 60) every = 'каждый час';
  else if (m % 60 === 0) every = 'каждые ' + (m / 60) + ' ч';
  else if (m === 1) every = 'каждую минуту';
  else every = 'каждые ' + m + ' мин';
  return every + (days ? ' ' + days : '');
}

const isWeekend = (d) => d.getDay() === 0 || d.getDay() === 6;

/**
 * Пора ли запускать правило по расписанию. Возвращает время слота (мс)
 * или 0. Пропущенные слоты не копятся: из нескольких просроченных
 * запустится только последний, и не позже GRACE_MS после него.
 */
export function dueSlot(rule, now) {
  const t = rule.trigger;
  const s = t.schedule;
  if (!s) return 0;
  const st = rule.state || {};
  const armed = Number(st.armedAt) || Number(rule.created) || 0;
  const d = new Date(now);
  if (t.weekdays && isWeekend(d)) return 0;
  if (s.kind === 'every') {
    const last = Math.max(Number(st.lastRun) || 0, armed);
    return (now - last >= s.minutes * 60000) ? now : 0;
  }
  let best = 0;
  for (const time of s.times) {
    const [h, m] = time.split(':').map(Number);
    const slot = new Date(d.getFullYear(), d.getMonth(), d.getDate(), h, m, 0, 0).getTime();
    if (slot <= now && now - slot <= GRACE_MS && slot >= armed && slot > (Number(st.lastSlot) || 0)) {
      best = Math.max(best, slot);
    }
  }
  return best;
}

/** Когда правило запустится в следующий раз (мс) или 0. */
export function nextRun(rule, now) {
  const t = rule.trigger;
  const s = t.schedule;
  if (!s) return 0;
  const st = rule.state || {};
  if (s.kind === 'every') {
    const last = Math.max(Number(st.lastRun) || 0, Number(st.armedAt) || Number(rule.created) || 0);
    let at = Math.max(now, last + s.minutes * 60000);
    for (let i = 0; i < 8 && t.weekdays && isWeekend(new Date(at)); ++i) {
      const d = new Date(at);
      d.setDate(d.getDate() + 1);
      d.setHours(0, 0, 0, 0);
      at = d.getTime();
    }
    return at;
  }
  const base = new Date(now);
  for (let day = 0; day < 8; ++day) {
    const d = new Date(base.getFullYear(), base.getMonth(), base.getDate() + day);
    if (t.weekdays && isWeekend(d)) continue;
    for (const time of s.times) {
      const [h, m] = time.split(':').map(Number);
      const slot = new Date(d.getFullYear(), d.getMonth(), d.getDate(), h, m, 0, 0).getTime();
      if (slot > now) return slot;
    }
  }
  return 0;
}

// --- Шаблоны текста ---------------------------------------------------------

export const TEMPLATE_HELP = '{sender} — отправитель, {chat} — чат, {text} — текст сообщения, {time} и {date} — время и дата, {rule} — название правила';

export function messageText(m) {
  if (!m) return '';
  const text = String(m.text || '').trim();
  if (text) return text;
  return m.media ? '[' + (MEDIA_LABEL[m.media] || m.media) + ']' : '';
}

export function renderTemplate(template, vars) {
  return String(template || '').replace(/\{(sender|chat|text|time|date|rule)\}/g, (all, key) => {
    const value = vars[key];
    return (value === undefined || value === null) ? '' : String(value);
  });
}

// --- Нормализация -----------------------------------------------------------

function cleanTokens(list) {
  const out = [];
  for (const item of Array.isArray(list) ? list : []) {
    const s = str(item, 64).trim();
    if (!s) continue;
    let token = null;
    if (NUMERIC.test(s)) token = s;
    else if (USERNAME.test(s)) token = '@' + USERNAME.exec(s)[1].toLowerCase();
    if (token && !out.includes(token)) out.push(token);
    if (out.length >= MAX_TOKENS) break;
  }
  return out;
}

function cleanNames(names, tokens) {
  const out = {};
  if (!names || typeof names !== 'object') return out;
  for (const token of tokens) {
    const name = names[token];
    if (typeof name === 'string' && name) out[token] = name.slice(0, 128);
  }
  return out;
}

function cleanStrings(list, max) {
  const out = [];
  for (const item of Array.isArray(list) ? list : []) {
    const s = str(item, max).trim();
    if (s && !out.includes(s)) out.push(s);
    if (out.length >= MAX_TOKENS) break;
  }
  return out;
}

function cleanMedia(list) {
  const valid = new Set(Object.keys(MEDIA_LABEL));
  const set = new Set((Array.isArray(list) ? list : []).map(String).filter((x) => valid.has(x)));
  const result = cleanMediaSet(set);
  return result.list || [];
}

function cleanTypes(types) {
  const o = (types && typeof types === 'object') ? types : {};
  const result = { private: o.private !== false, group: o.group !== false, channel: o.channel !== false };
  if (!result.private && !result.group && !result.channel) return { private: true, group: true, channel: true };
  return result;
}

function cleanSchedule(s) {
  if (!s || typeof s !== 'object') return null;
  if (s.kind === 'every') {
    const minutes = Math.round(Number(s.minutes));
    return (minutes >= 1 && minutes <= 10080) ? { kind: 'every', minutes } : null;
  }
  if (s.kind === 'daily') {
    const times = [];
    for (const t of Array.isArray(s.times) ? s.times : []) {
      const m = /^(\d{1,2}):(\d{2})$/.exec(String(t));
      if (m && Number(m[1]) < 24 && Number(m[2]) < 60) times.push(pad(Number(m[1])) + ':' + m[2]);
    }
    const unique = Array.from(new Set(times)).sort().slice(0, 24);
    return unique.length ? { kind: 'daily', times: unique } : null;
  }
  return null;
}

export function normalizeAction(a, trigger) {
  if (!a || typeof a !== 'object') return null;
  const meta = ACTION[a.type];
  if (!meta || !meta.triggers.includes(trigger)) return null;
  const type = a.type;
  switch (type) {
    case 'notify':
      return { type, text: str(a.text, 1000) || defaultNotify(trigger) };
    case 'reply': {
      const text = str(a.text, 4096);
      return text.trim() ? { type, text } : null;
    }
    case 'react': {
      const emoji = str(a.emoji, 64).trim();
      return (emoji && !/\s/.test(emoji)) ? { type, emoji } : null;
    }
    case 'forward': {
      const to = (a.to && typeof a.to === 'object') ? a.to : { id: a.to };
      const id = str(to.id, 64).trim();
      if (!NUMERIC.test(id) && !USERNAME.test(id)) return null;
      const token = NUMERIC.test(id) ? id : '@' + USERNAME.exec(id)[1].toLowerCase();
      return { type, to: { id: token, name: str(to.name, 128) } };
    }
    case 'mute':
      return { type, hours: clamp(a.hours, 0.05, 8760, 8) };
    case 'webhook': {
      const url = str(a.url, 2048).trim();
      return /^https?:\/\/[^\s/?#]+\S*$/i.test(url) ? { type, url } : null;
    }
    default:
      return { type };
  }
}

function cleanStats(stats) {
  const s = (stats && typeof stats === 'object') ? stats : {};
  return {
    fired: Math.max(0, Math.floor(Number(s.fired) || 0)),
    skipped: Math.max(0, Math.floor(Number(s.skipped) || 0)),
    lastAt: Math.max(0, Number(s.lastAt) || 0),
  };
}

function cleanState(state) {
  const s = (state && typeof state === 'object') ? state : {};
  return {
    armedAt: Math.max(0, Number(s.armedAt) || 0),
    lastRun: Math.max(0, Number(s.lastRun) || 0),
    lastSlot: Math.max(0, Number(s.lastSlot) || 0),
  };
}

/**
 * Привести правило (из хранилища, редактора или чужого JSON) к
 * безопасному виду: известные поля, разумные пределы, только допустимые
 * для триггера действия. null — правило не годится.
 */
export function normalizeRule(raw, options) {
  const opts = options || {};
  if (!raw || typeof raw !== 'object' || !raw.trigger || typeof raw.trigger !== 'object') return null;
  const t = raw.trigger;
  const type = t.type;
  if (!Object.prototype.hasOwnProperty.call(TRIGGERS, type)) return null;
  const trigger = { type, chats: cleanTokens(t.chats) };
  trigger.names = cleanNames(t.names, trigger.chats);
  if (type === 'message') {
    trigger.sender = cleanStrings(t.sender, 64);
    trigger.text = str(t.text, 2000);
    trigger.media = cleanMedia(t.media);
    trigger.in = t.in !== false;
    trigger.out = !!t.out;
    if (!trigger.in && !trigger.out) trigger.in = true;
  }
  if (type === 'schedule') {
    trigger.schedule = cleanSchedule(t.schedule);
    if (!trigger.schedule) return null;
    trigger.weekdays = !!t.weekdays;
  } else {
    trigger.types = cleanTypes(t.types);
  }
  const seen = new Set();
  const actions = [];
  for (const a of Array.isArray(raw.actions) ? raw.actions : []) {
    const action = normalizeAction(a, type);
    // Одинаковые действия без параметров повторять незачем.
    const key = action && (['saved', 'read', 'typing'].includes(action.type) ? action.type : null);
    if (!action || (key && seen.has(key))) continue;
    if (key) seen.add(key);
    actions.push(action);
    if (actions.length >= MAX_ACTIONS) break;
  }
  if (!actions.length) return null;
  if (type === 'schedule' && trigger.schedule.kind === 'every' && actions.some((a) => a.type === 'reply')) {
    trigger.schedule.minutes = Math.max(MIN_SEND_INTERVAL, trigger.schedule.minutes);
  }
  const rule = {
    id: str(raw.id, 40),
    name: str(raw.name, 80).replace(/\s+/g, ' ').trim(),
    enabled: raw.enabled !== false,
    dryRun: !!raw.dryRun,
    limit: Math.round(clamp(raw.limit, 1, 60, opts.defaultLimit || 5)),
    allowBots: !!raw.allowBots,
    allowChannels: !!raw.allowChannels,
    trigger,
    actions,
    created: Number(raw.created) > 0 ? Number(raw.created) : (opts.now || Date.now()),
    stats: cleanStats(raw.stats),
    state: cleanState(raw.state),
  };
  if (!rule.name) rule.name = autoName(rule);
  return rule;
}

export function autoName(rule) {
  const t = rule.trigger;
  const first = rule.actions.length ? ACTION[rule.actions[0].type].short : 'действие';
  let what;
  if (t.type === 'schedule') {
    what = describeSchedule(t.schedule, t.weekdays);
    what = what[0].toUpperCase() + what.slice(1);
  } else if (t.type === 'chatOpened') {
    what = 'Открыт ' + (t.chats.length ? chatLabel(t.chats[0], t.names) : 'чат');
  } else if (t.text) {
    what = isRegexText(t.text) ? clip(t.text, 24) : '«' + clip(splitWords(t.text)[0] || t.text, 20) + '»';
  } else if (t.media && t.media.length) {
    what = MEDIA_LABEL[t.media[0]];
    what = what[0].toUpperCase() + what.slice(1);
  } else if (t.sender && t.sender.length) {
    what = 'От ' + t.sender[0];
  } else if (t.chats.length) {
    what = chatLabel(t.chats[0], t.names);
  } else {
    what = t.out && !t.in ? 'Мои сообщения' : 'Сообщения';
  }
  return clip(what + ' → ' + first, 60);
}

// --- Проверка ---------------------------------------------------------------

export function compileRule(rule) {
  const t = rule.trigger;
  const compiled = {
    rule,
    error: '',
    chatty: rule.actions.some((a) => ACTION[a.type] && ACTION[a.type].chatty),
    re: null,
    senders: null,
  };
  if (t.type === 'message') {
    const p = parsePattern(t.text);
    if (p.error) compiled.error = p.error;
    compiled.re = p.re || null;
    compiled.senders = compileSenders(t.sender);
  }
  return compiled;
}

const kindsText = (types) => {
  if (!types || (types.private && types.group && types.channel)) return '';
  const list = [];
  if (types.private) list.push('личные');
  if (types.group) list.push('группы');
  if (types.channel) list.push('каналы');
  return 'только ' + list.join(' и ');
};

/** Проверка чата (тип и список) — общая для сообщений и открытия чата. */
function checkChat(t, chat, check) {
  const kind = kindOf(chat);
  const name = (chat && chat.name) || '?';
  if (t.types && !check(!!t.types[kind], 'Тип чата: ' + KIND_LABEL[kind], 'Тип «' + KIND_LABEL[kind] + '» в правиле не выбран (' + kindsText(t.types) + ')')) return false;
  if (t.chats.length && !check(chatMatches(t.chats, chat), 'Чат «' + name + '» есть в списке', 'Чат «' + name + '» не из списка правила')) return false;
  return true;
}

function checker(out) {
  const state = { ok: true };
  state.check = (pass, good, bad) => {
    if (out) out.push({ ok: !!pass, text: pass ? good : bad });
    if (!pass) state.ok = false;
    // Без out (обычная работа) — выходим на первом несовпадении.
    return !!pass || !!out;
  };
  return state;
}

/**
 * Подходит ли сообщение под правило. С out — собирает в него все
 * проверки ({ ok, text }) для кнопки «Проверить».
 */
export function evaluate(compiled, m, env, out) {
  const t = compiled.rule.trigger;
  const c = checker(out || null);
  if (compiled.error && !c.check(false, '', compiled.error)) return false;
  if (!c.check(m.out ? t.out : t.in,
    m.out ? 'Исходящее сообщение — правило их учитывает' : 'Входящее сообщение',
    m.out ? 'Это твоё исходящее сообщение, а правило — только для входящих' : 'Это входящее сообщение, а правило — только для исходящих')) return false;
  if (!checkChat(t, m.chat, c.check)) return false;
  if (compiled.senders && !c.check(senderMatches(compiled.senders, m, env && env.lookup),
    'Отправитель «' + (m.fromName || '?') + '» подходит',
    'Отправитель «' + (m.fromName || '?') + '» не подходит (нужно: ' + t.sender.join(', ') + ')')) return false;
  if (compiled.re) {
    compiled.re.lastIndex = 0;
    const hit = compiled.re.exec(String(m.text || ''));
    if (!c.check(!!hit, 'В тексте есть «' + (hit ? clip(hit[0], 40) : '') + '»',
      isRegexText(t.text) ? 'Текст не подходит под ' + clip(t.text, 40) : 'В тексте нет ни одного из слов: ' + clip(t.text, 60))) return false;
  }
  if (t.media.length && !c.check(mediaMatches(t.media, m),
    'Медиа: ' + (m.media ? MEDIA_LABEL[m.media] || m.media : 'нет'),
    'Медиа «' + (m.media ? MEDIA_LABEL[m.media] || m.media : 'нет') + '» не подходит (нужно: ' + t.media.map((x) => MEDIA_LABEL[x]).join(', ') + ')')) return false;
  return c.ok;
}

/** То же для триггера «открытие чата». */
export function evaluateChat(compiled, chat, out) {
  const c = checker(out || null);
  checkChat(compiled.rule.trigger, chat, c.check);
  return c.ok;
}

// --- Описания ---------------------------------------------------------------

export function describeTrigger(rule) {
  const t = rule.trigger;
  const parts = [];
  if (t.type === 'message') {
    parts.push(t.in && t.out ? 'любое сообщение' : (t.out ? 'моё сообщение' : 'входящее сообщение'));
  } else if (t.type === 'schedule') {
    parts.push(describeSchedule(t.schedule, t.weekdays));
  } else {
    parts.push('открыт чат');
  }
  const kinds = kindsText(t.types);
  if (kinds) parts.push(kinds);
  if (t.chats.length) {
    const list = t.chats.slice(0, 4).map((x) => chatLabel(x, t.names)).join(', ') + (t.chats.length > 4 ? ' и ещё ' + (t.chats.length - 4) : '');
    parts.push((t.type === 'schedule' ? 'чаты: ' : 'в ') + list);
  }
  if (t.sender && t.sender.length) parts.push('от ' + t.sender.slice(0, 3).join(', ') + (t.sender.length > 3 ? '…' : ''));
  if (t.text) {
    parts.push(isRegexText(t.text)
      ? 'текст ~ ' + clip(t.text, 40)
      : 'текст: ' + splitWords(t.text).slice(0, 4).map((w) => '«' + clip(w, 20) + '»').join(', ') + (splitWords(t.text).length > 4 ? '…' : ''));
  }
  if (t.media && t.media.length) parts.push('медиа: ' + t.media.map((x) => MEDIA_LABEL[x]).join(', '));
  return parts.join(' · ');
}

export function describeAction(a, trigger) {
  const meta = ACTION[a.type];
  switch (a.type) {
    case 'notify': return meta.icon + ' уведомление';
    case 'reply': return meta.icon + ' ' + (trigger === 'message' ? 'ответ' : 'текст') + ' «' + clip(a.text, 30) + '»';
    case 'react': return a.emoji + ' реакция';
    case 'forward': return meta.icon + ' переслать в «' + (a.to.name || a.to.id) + '»';
    case 'mute': return meta.icon + ' без звука на ' + hoursText(a.hours);
    case 'webhook': return meta.icon + ' webhook ' + host(a.url);
    default: return meta.icon + ' ' + meta.short;
  }
}

export function describeActions(rule) {
  return rule.actions.map((a) => describeAction(a, rule.trigger.type)).join(', ');
}

// --- Экспорт и импорт -------------------------------------------------------

export function exportRules(rules) {
  return JSON.stringify({
    type: FORMAT,
    version: 1,
    rules: rules.map((r) => ({
      name: r.name,
      enabled: r.enabled,
      dryRun: r.dryRun,
      limit: r.limit,
      allowBots: r.allowBots,
      allowChannels: r.allowChannels,
      trigger: r.trigger,
      actions: r.actions,
    })),
  }, null, 2);
}

/** { rules, invalid } или { error }. Правила без id, счётчиков и состояния. */
export function parseImport(text, options) {
  const s = String(text || '').trim();
  if (!s) return { error: 'Буфер обмена пуст. Скопируй JSON с правилами и попробуй снова.' };
  let data;
  try {
    data = JSON.parse(s);
  } catch (e) {
    return { error: 'В буфере не JSON: ' + (e && e.message ? e.message : e) };
  }
  let list;
  if (Array.isArray(data)) list = data;
  else if (data && Array.isArray(data.rules)) list = data.rules;
  else if (data && data.trigger) list = [data];
  else return { error: 'Не вижу правил: нужен JSON, который даёт «Экспорт».' };
  const rules = [];
  let invalid = 0;
  for (const raw of list.slice(0, MAX_RULES)) {
    const rule = normalizeRule(Object.assign({}, raw, { id: '', created: 0, stats: null, state: null }), options);
    if (rule) rules.push(rule);
    else invalid++;
  }
  invalid += Math.max(0, list.length - MAX_RULES);
  return { rules, invalid };
}
