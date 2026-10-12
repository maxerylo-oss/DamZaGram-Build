// Быстрые ответы: заготовки текста с переменными.
// Кнопка у поля ввода открывает панель с поиском: клик вставляет ответ в
// поле ввода, «Отправить сразу» отправляет его в открытый чат. /q название
// отправляет ответ вместо команды, а сообщение «;;сокращение» разворачивается
// при отправке. Ответы хранятся в dzg.storage.
// Автор: @wetakush

const SNIPPETS_KEY = 'snippets';
const SEEDED_KEY = 'seeded';
const ANSWERS_KEY = 'answers';

const MAX_SNIPPETS = 300;
const MAX_TEXT = 4096;
const MAX_NAME = 64;
const MAX_SHOWN = 30;
const MAX_ANSWERS = 100;

// Плагин отправляет только по явному действию, но двойной клик или
// залипшая клавиша не должны превращаться в серию сообщений.
const REPEAT_MS = 2000;
const SEND_WINDOW_MS = 10000;
const SEND_LIMIT = 5;
// Неизвестное ;;сокращение не уходит в чат; повторная отправка того же
// текста в течение минуты уходит как есть.
const MISS_PASS_MS = 60000;

const TITLE = '⚡ Быстрые ответы';

export const DEFAULT_SNIPPETS = [
  { name: 'Приветствие', shortcut: 'hi', text: 'Привет, {first}! 👋' },
  { name: 'Спасибо', shortcut: 'ty', text: 'Спасибо, {first}! Очень помогло 🙏' },
  { name: 'Занят', shortcut: 'busy', text: 'Сейчас не могу ответить — напишу, как освобожусь 🙌' },
  { name: 'Опаздываю', shortcut: 'late', text: 'Немного задерживаюсь, буду через {?Через сколько минут?|10} мин. Извини!' },
];

export const VARIABLES_HELP = 'Переменные: {name} — название чата, {first} — имя собеседника '
  + '(в группе — автора сообщения, на которое отвечаешь), {me} — твоё имя, {date} и {time} — '
  + 'дата и время, {clipboard} — буфер обмена, {rest} — текст после «/q название», '
  + '{?Вопрос} — спросить перед вставкой, {?Вопрос|ответ по умолчанию}.';

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return (value === undefined || value === null || value === '') ? fallback : value;
};

const pad = (n) => String(n).padStart(2, '0');
const errorText = (e) => (e && e.message ? e.message : String(e));

/** Для сравнения: нижний регистр, ё = е, пробелы схлопнуты. */
export const norm = (s) => String(s ?? '').toLowerCase().replace(/ё/g, 'е').replace(/\s+/g, ' ').trim();

const oneLine = (s, max) => {
  const text = String(s ?? '').replace(/\s+/g, ' ').trim();
  return text.length > max ? text.slice(0, max - 1) + '…' : text;
};

// --- Переменные -----------------------------------------------------------

const VARIABLES = {
  name: 'name', chat: 'name', 'чат': 'name',
  first: 'first', 'имя': 'first',
  me: 'me', 'я': 'me',
  date: 'date', 'дата': 'date',
  time: 'time', 'время': 'time',
  clipboard: 'clipboard', 'буфер': 'clipboard',
  rest: 'rest', 'текст': 'rest',
};

// Переменная вместе с разделителями вокруг: пустое значение забирает с
// собой лишнюю запятую, и «Привет, {first}!» в группе станет «Привет!».
const TOKEN = /(,[ \t]*|[ \t]+)?\{([^{}\n]{1,200})\}(,[ \t]*)?/g;
const ASK = /\{\?([^{}\n]{0,200})\}/g;

/** Первое слово имени с буквами, без эмодзи и знаков по краям. */
export function firstWord(name) {
  for (const token of String(name ?? '').trim().split(/\s+/)) {
    const word = token.replace(/^[^\p{L}\p{N}]+|[^\p{L}\p{N}]+$/gu, '');
    if (/\p{L}/u.test(word)) return word;
  }
  return '';
}

const splitAsk = (body) => {
  const bar = body.indexOf('|');
  const question = (bar >= 0 ? body.slice(0, bar) : body).trim() || 'Значение';
  const fallback = bar >= 0 ? body.slice(bar + 1).trim() : '';
  return { question, fallback };
};

/** Вопросы {?…} шаблона по порядку, без повторов. */
export function questions(template) {
  const list = [];
  const seen = new Set();
  for (const m of String(template ?? '').matchAll(ASK)) {
    const ask = splitAsk(m[1]);
    if (seen.has(ask.question)) continue;
    seen.add(ask.question);
    list.push(ask);
  }
  return list;
}

/**
 * Подставляет переменные. ctx: { chatName, first, me, now, clipboard
 * (строка или функция), rest, answers: { вопрос: ответ } }. Неизвестные
 * {слова} остаются как есть. Если в шаблоне нет {rest}, текст после
 * названия дописывается в конец.
 */
export function expand(template, ctx) {
  const c = ctx || {};
  const now = c.now instanceof Date ? c.now : new Date(c.now === undefined ? Date.now() : c.now);
  const answers = c.answers || {};
  let usedRest = false;
  let clipboard = null;
  const value = (body) => {
    if (body.startsWith('?')) {
      const { question } = splitAsk(body.slice(1));
      return Object.prototype.hasOwnProperty.call(answers, question) ? String(answers[question]) : null;
    }
    switch (VARIABLES[body.trim().toLowerCase()]) {
    case 'name': return String(c.chatName || '');
    case 'first': return String(c.first || '');
    case 'me': return String(c.me || '');
    case 'date': return pad(now.getDate()) + '.' + pad(now.getMonth() + 1) + '.' + now.getFullYear();
    case 'time': return pad(now.getHours()) + ':' + pad(now.getMinutes());
    case 'clipboard':
      if (clipboard === null) {
        clipboard = String((typeof c.clipboard === 'function' ? c.clipboard() : c.clipboard) || '');
      }
      return clipboard;
    case 'rest':
      usedRest = true;
      return String(c.rest || '').trim();
    default:
      return null;
    }
  };
  const source = String(template ?? '');
  let out = source.replace(TOKEN, (match, before, body, after, offset) => {
    const v = value(body);
    if (v === null) return match;
    if (v !== '') return (before || '') + v + (after || '');
    if (before) return after || '';
    const lineStart = offset === 0 || source[offset - 1] === '\n';
    return lineStart ? '' : (after || '');
  });
  const rest = String(c.rest || '').trim();
  if (rest && !usedRest) {
    out = out.trim() ? out.trimEnd() + (out.includes('\n') ? '\n' : ' ') + rest : rest;
  }
  return out.trim();
}

// --- Разбор ввода ---------------------------------------------------------

/** ';;Addr' → 'addr'; '' — пусто; null — недопустимые символы. */
export function cleanShortcut(value) {
  const s = String(value ?? '').trim().replace(/^;+/, '').toLowerCase();
  if (!s) return '';
  return /^[\p{L}\p{N}_-]{1,32}$/u.test(s) ? s : null;
}

/** Название по тексту: первые слова первой строки без переменных. */
export function suggestName(text) {
  const line = String(text ?? '').split('\n').find((l) => l.trim()) || '';
  const words = line.replace(/\{[^{}]*\}/g, ' ').replace(/[,.!?:;]+(\s|$)/g, ' ').split(/\s+/).filter(Boolean);
  let name = '';
  for (const word of words) {
    if ((name + ' ' + word).trim().length > 32) break;
    name = (name + ' ' + word).trim();
    if (name.split(' ').length >= 4) break;
  }
  return name || oneLine(line, 32) || 'Без названия';
}

/** «Адрес ;;addr» → { name: 'Адрес', shortcut: 'addr' }. */
export function parseNameAnswer(answer) {
  const text = String(answer ?? '').trim();
  const m = /^(.*?)\s*;;(\S+)$/u.exec(text);
  return m ? { name: m[1].trim(), shortcut: m[2] } : { name: text, shortcut: '' };
}

// --- Хранилище ------------------------------------------------------------

function normalize(raw, index) {
  if (!raw || typeof raw !== 'object' || typeof raw.text !== 'string' || !raw.text.trim()) return null;
  return {
    id: String(raw.id || ('x' + index)),
    name: String(raw.name || '').trim().slice(0, MAX_NAME) || suggestName(raw.text),
    text: raw.text,
    shortcut: typeof raw.shortcut === 'string' ? (cleanShortcut(raw.shortcut) || '') : '',
    uses: Number(raw.uses) || 0,
    lastUsed: Number(raw.lastUsed) || 0,
    created: Number(raw.created) || 0,
  };
}

export function load() {
  const list = dzg.storage.get(SNIPPETS_KEY, []);
  return Array.isArray(list) ? list.map(normalize).filter(Boolean) : [];
}

function save(list) {
  dzg.storage.set(SNIPPETS_KEY, list);
  renderRows();
}

// id без записи в хранилище: импорт сотни ответов не должен сотню раз
// переписывать файл.
let idCounter = 0;

function make({ name, text, shortcut }) {
  const id = 's' + Date.now().toString(36) + '-' + (++idCounter).toString(36);
  return { id, name, text, shortcut: shortcut || '', uses: 0, lastUsed: 0, created: Date.now() };
}

function bump(id) {
  const list = load();
  const item = list.find((s) => s.id === id);
  if (!item) return;
  item.uses += 1;
  item.lastUsed = Date.now();
  dzg.storage.set(SNIPPETS_KEY, list);
}

const byName = (list, name) => list.find((s) => norm(s.name) === norm(name)) || null;
const byShortcut = (list, shortcut) => (shortcut ? list.find((s) => s.shortcut && norm(s.shortcut) === norm(shortcut)) || null : null);

function seed() {
  if (dzg.storage.get(SEEDED_KEY, false)) return;
  if (!load().length) dzg.storage.set(SNIPPETS_KEY, DEFAULT_SNIPPETS.map(make));
  dzg.storage.set(SEEDED_KEY, true);
}

const missingDefaults = () => {
  const list = load();
  return DEFAULT_SNIPPETS.filter((d) => !byName(list, d.name));
};

// --- Поиск и порядок ------------------------------------------------------

export function sortSnippets(list, mode) {
  const copy = list.slice();
  if (mode === 'name') {
    copy.sort((a, b) => {
      const x = norm(a.name);
      const y = norm(b.name);
      return x < y ? -1 : (x > y ? 1 : 0);
    });
  } else if (mode === 'created') {
    copy.sort((a, b) => (a.created || 0) - (b.created || 0));
  } else {
    copy.sort((a, b) => (b.uses || 0) - (a.uses || 0) || (b.lastUsed || 0) - (a.lastUsed || 0) || (a.created || 0) - (b.created || 0));
  }
  return copy;
}

/** Поиск по названию, сокращению и тексту; «;;x» — только по сокращению. */
export function search(list, query, mode) {
  const sorted = sortSnippets(list, mode);
  const q = norm(query);
  if (!q) return sorted;
  const onlyShortcut = q.startsWith(';;') ? q.slice(2) : null;
  const words = q.split(' ');
  const scored = [];
  for (const s of sorted) {
    const name = norm(s.name);
    const shortcut = norm(s.shortcut);
    const text = norm(s.text);
    let score = 0;
    if (onlyShortcut !== null) {
      if (shortcut && shortcut.startsWith(onlyShortcut)) score = shortcut === onlyShortcut ? 5 : 4;
    } else if (name === q || shortcut === q) {
      score = 5;
    } else if (name.startsWith(q) || (shortcut && shortcut.startsWith(q))) {
      score = 4;
    } else if (name.includes(q)) {
      score = 3;
    } else if (text.includes(q)) {
      score = 2;
    } else if (words.every((w) => name.includes(w) || text.includes(w))) {
      score = 1;
    }
    if (score) scored.push({ s, score });
  }
  return scored.sort((a, b) => b.score - a.score).map((x) => x.s);
}

const afterWords = (raw, count) => raw.replace(new RegExp('^\\s*(?:\\S+\\s*){' + count + '}'), '').trim();

/**
 * Разбор «/q название [текст]»: полное название (самое длинное), затем
 * сокращение, затем единственный ответ, который начинается с первого слова.
 */
export function resolve(list, query) {
  const raw = String(query ?? '').trim();
  if (!raw) return { snippet: null, rest: '', matches: [] };
  const q = norm(raw);
  const longest = list.slice().sort((a, b) => norm(b.name).length - norm(a.name).length);
  for (const s of longest) {
    const name = norm(s.name);
    if (name && (q === name || q.startsWith(name + ' '))) {
      return { snippet: s, rest: afterWords(raw, name.split(' ').length), matches: [s] };
    }
  }
  const first = norm(raw.split(/\s+/)[0]).replace(/^;;/, '');
  const exact = byShortcut(list, first);
  if (exact) return { snippet: exact, rest: afterWords(raw, 1), matches: [exact] };
  const matches = first
    ? list.filter((s) => norm(s.name).startsWith(first) || (s.shortcut && norm(s.shortcut).startsWith(first)))
    : [];
  if (matches.length === 1) return { snippet: matches[0], rest: afterWords(raw, 1), matches };
  return { snippet: null, rest: '', matches };
}

// --- Импорт и экспорт -----------------------------------------------------

export function exportJson(list) {
  return JSON.stringify({
    type: 'damzagram.quick-replies',
    version: 1,
    snippets: list.map((s) => (s.shortcut
      ? { name: s.name, shortcut: s.shortcut, text: s.text }
      : { name: s.name, text: s.text })),
  }, null, 2);
}

/**
 * Понимает экспорт плагина, массив { name, text, shortcut } (или строк) и
 * объект { "название": "текст" }. Бросает Error с понятным текстом.
 */
export function parseImport(raw) {
  let data;
  try {
    data = JSON.parse(String(raw ?? '').trim());
  } catch (e) {
    throw new Error('в буфере обмена не JSON');
  }
  let entries;
  if (Array.isArray(data)) {
    entries = data;
  } else if (data && typeof data === 'object' && Array.isArray(data.snippets)) {
    entries = data.snippets;
  } else if (data && typeof data === 'object') {
    entries = Object.entries(data).map(([name, text]) => ({ name, text }));
  } else {
    throw new Error('ожидался список быстрых ответов');
  }
  const items = [];
  let skipped = 0;
  for (const entry of entries) {
    const text = typeof entry === 'string' ? entry : (entry && typeof entry.text === 'string' ? entry.text : '');
    if (!text.trim() || text.length > MAX_TEXT) {
      ++skipped;
      continue;
    }
    const rawName = entry && typeof entry === 'object' ? (entry.name || entry.title || '') : '';
    const name = String(rawName).trim().slice(0, MAX_NAME) || suggestName(text);
    const shortcut = entry && typeof entry === 'object' ? (cleanShortcut(entry.shortcut) || '') : '';
    items.push({ name, text, shortcut });
  }
  if (!items.length) throw new Error('не нашлось ни одного быстрого ответа');
  return { items, skipped };
}

/** Слияние: совпадающие по названию обновляются, новые добавляются. */
export function merge(existing, items) {
  const list = existing.map((s) => Object.assign({}, s));
  let added = 0;
  let updated = 0;
  let conflicts = 0;
  let overflow = 0;
  for (const item of items) {
    const same = byName(list, item.name);
    let shortcut = item.shortcut;
    const owner = byShortcut(list, shortcut);
    if (owner && owner !== same) {
      shortcut = '';
      ++conflicts;
    }
    if (same) {
      if (same.text !== item.text || (shortcut && same.shortcut !== shortcut)) {
        same.text = item.text;
        if (shortcut) same.shortcut = shortcut;
        ++updated;
      }
    } else if (list.length >= MAX_SNIPPETS) {
      ++overflow;
    } else {
      list.push(make({ name: item.name, text: item.text, shortcut }));
      ++added;
    }
  }
  return { list, added, updated, conflicts, overflow };
}

// --- Контекст и отправка --------------------------------------------------

function currentChat() {
  const compose = dzg.ui.compose && dzg.ui.compose.get ? dzg.ui.compose.get() : null;
  return (compose && compose.chat) || dzg.chats.current();
}

function firstFor(chat, replyTo) {
  if (!chat) return '';
  if (replyTo && dzg.messages.get) {
    const message = dzg.messages.get(chat.id, replyTo);
    if (message && !message.out && message.fromName) return firstWord(message.fromName);
  }
  return (chat.type === 'user' && !chat.isSelf) ? firstWord(chat.name) : '';
}

function contextFor(chat, replyTo, rest, answers) {
  const self = dzg.users && dzg.users.self ? dzg.users.self() : null;
  return {
    chatName: chat ? chat.name : '',
    first: firstFor(chat, replyTo),
    me: self ? firstWord(self.name) : '',
    clipboard: () => dzg.clipboard.get(),
    rest: rest || '',
    answers: answers || {},
  };
}

// Сущности, которые клиент находит в тексте сам: разметкой они не считаются.
const AUTO_ENTITIES = new Set(['url', 'email', 'mention', 'hashtag', 'cashtag', 'botCommand', 'phone', 'bankCard', 'mediaTimestamp']);

function hasMarkup(text) {
  if (!dzg.messages.parse) return false;
  try {
    const parsed = dzg.messages.parse(text, 'markdown');
    return parsed.text !== text || parsed.entities.some((e) => !AUTO_ENTITIES.has(e.type));
  } catch (e) {
    return false;
  }
}

function sendOptions(text, replyTo) {
  const options = {};
  if (replyTo) options.replyTo = replyTo;
  if (setting('markdown', true) && hasMarkup(text)) options.parse = 'markdown';
  return options;
}

const sendTimes = [];
let lastSend = { key: '', at: 0 };

function allowSend(key) {
  const now = Date.now();
  if (key === lastSend.key && now - lastSend.at < REPEAT_MS) {
    dzg.ui.toast('Этот ответ только что ушёл в чат', TITLE);
    return false;
  }
  while (sendTimes.length && now - sendTimes[0] > SEND_WINDOW_MS) sendTimes.shift();
  if (sendTimes.length >= SEND_LIMIT) {
    dzg.ui.toast('Слишком часто: подожди пару секунд', TITLE);
    return false;
  }
  sendTimes.push(now);
  lastSend = { key, at: now };
  return true;
}

function sendText(chat, text, replyTo, key) {
  if (!allowSend(key + ':' + chat.id)) return false;
  if (!dzg.messages.send(chat.id, text, sendOptions(text, replyTo))) {
    dzg.ui.toast('Не получилось отправить в «' + chat.name + '»', TITLE);
    return false;
  }
  return true;
}

function insertText(text) {
  const compose = dzg.ui.compose;
  if (compose && compose.insert && compose.insert(text)) {
    if (compose.focus) compose.focus();
    return true;
  }
  dzg.clipboard.set(text);
  dzg.ui.toast('Поле ввода не найдено — текст скопирован в буфер обмена', TITLE);
  return false;
}

function rememberAnswers(answers) {
  const stored = Object.assign({}, dzg.storage.get(ANSWERS_KEY, {}) || {});
  for (const [question, answer] of Object.entries(answers)) {
    delete stored[question];
    stored[question] = String(answer).slice(0, 200);
  }
  const keys = Object.keys(stored);
  for (const key of keys.slice(0, Math.max(0, keys.length - MAX_ANSWERS))) delete stored[key];
  dzg.storage.set(ANSWERS_KEY, stored);
}

/** Задаёт вопросы {?…} и подставляет переменные. null — пользователь отменил. */
async function fill(snippet, chat, replyTo, rest, confirmText) {
  const asks = questions(snippet.text);
  const answers = {};
  if (asks.length) {
    const remembered = dzg.storage.get(ANSWERS_KEY, {}) || {};
    for (let i = 0; i < asks.length; ++i) {
      const { question, fallback } = asks[i];
      const answer = await dzg.ui.prompt(question, {
        text: '«' + snippet.name + '»' + (asks.length > 1 ? ' · вопрос ' + (i + 1) + ' из ' + asks.length : ''),
        value: remembered[question] !== undefined ? String(remembered[question]) : fallback,
        confirmText: i === asks.length - 1 ? confirmText : 'Дальше',
        cancelText: 'Отмена',
      });
      if (answer === null || answer === undefined) return null;
      answers[question] = String(answer).trim();
    }
    rememberAnswers(answers);
  }
  return expand(snippet.text, contextFor(chat, replyTo, rest, answers));
}

let busy = false;

/**
 * Использовать ответ: mode 'insert' — в поле ввода, 'send' — сразу в чат.
 * options: { chat, replyTo, rest, fromPanel }.
 */
export async function use(id, mode, options) {
  const o = options || {};
  if (busy) {
    dzg.ui.toast('Сначала закончи с предыдущим быстрым ответом', TITLE);
    return false;
  }
  const snippet = load().find((s) => s.id === id);
  if (!snippet) return false;
  const chat = o.chat !== undefined ? o.chat : currentChat();
  if (mode === 'send' && !chat) {
    dzg.ui.toast('Открой чат, чтобы отправить быстрый ответ', TITLE);
    return false;
  }
  busy = true;
  try {
    const text = await fill(snippet, chat, o.replyTo || 0, o.rest || '', mode === 'send' ? 'Отправить' : 'Вставить');
    if (text === null) return false;
    if (!text) {
      dzg.ui.toast('«' + snippet.name + '» получился пустым', TITLE);
      return false;
    }
    if (text.length > MAX_TEXT) {
      dzg.ui.toast('Получилось длиннее 4096 символов — Telegram такое не отправит', TITLE);
      return false;
    }
    if (mode === 'send') {
      if (!sendText(chat, text, o.replyTo || 0, snippet.id)) return false;
    } else {
      insertText(text);
    }
    bump(snippet.id);
    if (o.fromPanel && setting('closeAfterUse', true)) closePanel();
    return true;
  } catch (e) {
    dzg.ui.toast('Не получилось: ' + errorText(e), TITLE);
    return false;
  } finally {
    busy = false;
  }
}

// --- Редактор -------------------------------------------------------------

/** Проверка полей редактора. { error } или { value: { name, shortcut, text } }. */
export function validate(values, selfId, list) {
  const text = String(values.text ?? '').replace(/\s+$/, '');
  if (!text.trim()) return { error: 'Напиши текст ответа' };
  if (text.length > MAX_TEXT) return { error: 'Слишком длинно: Telegram отправит не больше 4096 символов' };
  const name = (String(values.name ?? '').trim() || suggestName(text)).slice(0, MAX_NAME);
  const same = byName(list, name);
  if (same && same.id !== selfId) return { error: '«' + same.name + '» уже есть — выбери другое название' };
  const shortcut = cleanShortcut(values.shortcut);
  if (shortcut === null) return { error: 'Сокращение — одно слово из букв, цифр, _ или -' };
  const owner = byShortcut(list, shortcut);
  if (owner && owner.id !== selfId) return { error: 'Сокращение ;;' + shortcut + ' уже у «' + owner.name + '»' };
  return { value: { name, shortcut, text } };
}

let editing = false;

export async function edit(id, draft) {
  if (editing) return;
  const current = id ? load().find((s) => s.id === id) : null;
  if (id && !current) return;
  if (!current && load().length >= MAX_SNIPPETS) {
    dzg.ui.toast('Уже ' + MAX_SNIPPETS + ' быстрых ответов — удали ненужные', TITLE);
    return;
  }
  editing = true;
  try {
    let values = Object.assign({
      name: current ? current.name : '',
      shortcut: current ? current.shortcut : '',
      text: current ? current.text : '',
    }, draft || {});
    let result = null;
    // Ошибка в полях — окно открывается снова с тем же текстом.
    for (let attempt = 0; attempt < 5 && !result; ++attempt) {
      const answer = await dzg.ui.box({
        title: current ? 'Изменить быстрый ответ' : 'Новый быстрый ответ',
        text: VARIABLES_HELP,
        inputs: [
          { id: 'name', placeholder: 'Название', value: values.name },
          { id: 'shortcut', placeholder: 'Сокращение для ;; (необязательно)', value: values.shortcut },
          { id: 'text', placeholder: 'Текст ответа', value: values.text, multiline: true },
        ],
        buttons: [{ id: 'ok', text: 'Сохранить' }]
          .concat(current ? [{ id: 'delete', text: 'Удалить', danger: true }] : [])
          .concat([{ id: 'cancel', text: 'Отмена' }]),
      });
      if (!answer || answer.button === 'cancel') return;
      if (answer.button === 'delete') {
        await remove(current.id, false);
        return;
      }
      const v = answer.values || {};
      values = { name: String(v.name ?? ''), shortcut: String(v.shortcut ?? ''), text: String(v.text ?? '') };
      const checked = validate(values, current ? current.id : null, load());
      if (checked.error) {
        dzg.ui.toast(checked.error, TITLE);
      } else {
        result = checked.value;
      }
    }
    if (!result) return;
    const list = load();
    const target = current ? list.find((s) => s.id === current.id) : null;
    if (target) {
      Object.assign(target, result);
    } else {
      list.push(make(result));
    }
    save(list);
    dzg.ui.toast('Сохранено: «' + result.name + '»' + (result.shortcut ? ' · ;;' + result.shortcut : ''), TITLE);
  } catch (e) {
    dzg.ui.toast('Не получилось сохранить: ' + errorText(e), TITLE);
  } finally {
    editing = false;
  }
}

export async function remove(id, ask) {
  const item = load().find((s) => s.id === id);
  if (!item) return false;
  if (ask && !(await dzg.ui.confirm('Удалить быстрый ответ «' + item.name + '»?', {
    confirmText: 'Удалить', cancelText: 'Отмена', danger: true,
  }))) return false;
  save(load().filter((s) => s.id !== id));
  dzg.ui.toast('Удалено: «' + item.name + '»', TITLE);
  return true;
}

// --- Сохранение из сообщения ----------------------------------------------

// Разметка, которую поле ввода клиента само применяет при отправке
// (ссылок [текст](url) среди неё нет — они остались бы в тексте как есть).
const KEPT_ENTITIES = new Set(['bold', 'italic', 'strikethrough', 'code', 'pre', 'spoiler']);

function messageText(m) {
  const plain = String(m.text || '');
  if (!setting('markdown', true) || !Array.isArray(m.entities) || !dzg.messages.format) return plain;
  const entities = m.entities.filter((e) => e && KEPT_ENTITIES.has(e.type));
  if (!entities.length) return plain;
  try {
    const markdown = dzg.messages.format({ text: plain, entities }, 'markdown');
    return (typeof markdown === 'string' && markdown.trim()) ? markdown : plain;
  } catch (e) {
    return plain;
  }
}

let saving = false;

export async function saveFromMessage(m) {
  if (saving || !m) return;
  saving = true;
  try {
    const text = messageText(m);
    if (!text.trim()) return;
    if (text.length > MAX_TEXT) {
      dzg.ui.toast('Сообщение длиннее 4096 символов', TITLE);
      return;
    }
    const answer = await dzg.ui.prompt('Сохранить как быстрый ответ', {
      text: 'Название, по которому его легко найти. Через пробел можно сразу добавить сокращение: «Адрес ;;addr».',
      placeholder: 'Название',
      value: suggestName(m.text),
      confirmText: 'Сохранить',
      cancelText: 'Отмена',
    });
    if (answer === null || answer === undefined) return;
    const parsed = parseNameAnswer(answer);
    const name = (parsed.name || suggestName(text)).slice(0, MAX_NAME);
    let shortcut = cleanShortcut(parsed.shortcut);
    const notes = [];
    if (shortcut === null) {
      notes.push('сокращение — одно слово из букв и цифр, сохранил без него');
      shortcut = '';
    }
    const list = load();
    const same = byName(list, name);
    const owner = byShortcut(list, shortcut);
    if (owner && owner !== same) {
      notes.push(';;' + shortcut + ' уже у «' + owner.name + '», сохранил без сокращения');
      shortcut = '';
    }
    if (same) {
      const replace = await dzg.ui.confirm('«' + same.name + '» уже есть. Заменить его текст?', {
        confirmText: 'Заменить', cancelText: 'Отмена',
      });
      if (!replace) return;
      // За время вопроса список мог поменяться: пишем в свежую копию.
      const fresh = load();
      const target = fresh.find((s) => s.id === same.id);
      if (!target) return;
      target.text = text;
      if (shortcut) target.shortcut = shortcut;
      save(fresh);
    } else {
      if (list.length >= MAX_SNIPPETS) {
        dzg.ui.toast('Уже ' + MAX_SNIPPETS + ' быстрых ответов — удали ненужные', TITLE);
        return;
      }
      list.push(make({ name, text, shortcut }));
      save(list);
    }
    dzg.ui.toast('Сохранено: «' + name + '»' + (shortcut ? ' · ;;' + shortcut : '') + (notes.length ? '\n' + notes.join('\n') : ''), TITLE);
  } catch (e) {
    dzg.ui.toast('Не получилось сохранить: ' + errorText(e), TITLE);
  } finally {
    saving = false;
  }
}

// --- Импорт, экспорт, стандартные -----------------------------------------

export function exportToClipboard() {
  const list = load();
  if (!list.length) {
    dzg.ui.toast('Экспортировать нечего: список пуст', TITLE);
    return;
  }
  dzg.clipboard.set(exportJson(list));
  dzg.ui.toast('Скопировано ' + list.length + ' — JSON можно вставить на другом компьютере через «Импорт»', TITLE);
}

let importing = false;

export async function importFromClipboard() {
  if (importing) return;
  importing = true;
  try {
    let parsed;
    try {
      parsed = parseImport(dzg.clipboard.get());
    } catch (e) {
      dzg.ui.toast('Импорт не удался: ' + errorText(e) + '. Скопируй JSON, который дал «Экспорт».', TITLE);
      return;
    }
    const list = load();
    const replacing = parsed.items.filter((item) => byName(list, item.name)).length;
    const ok = await dzg.ui.confirm(
      'Импортировать ' + parsed.items.length + ' из буфера обмена?'
        + (replacing ? '\nСовпадающих по названию: ' + replacing + ' — их текст заменится.' : ''),
      { confirmText: 'Импортировать', cancelText: 'Отмена' });
    if (!ok) return;
    const result = merge(load(), parsed.items);
    save(result.list);
    const parts = ['добавлено ' + result.added, 'обновлено ' + result.updated];
    if (result.conflicts) parts.push('без сокращения ' + result.conflicts + ' (занято)');
    if (result.overflow) parts.push('не влезло ' + result.overflow + ' (максимум ' + MAX_SNIPPETS + ')');
    if (parsed.skipped) parts.push('пропущено ' + parsed.skipped);
    dzg.ui.toast('Импорт: ' + parts.join(', '), TITLE);
  } finally {
    importing = false;
  }
}

export function restoreDefaults() {
  const missing = missingDefaults();
  if (!missing.length) {
    dzg.ui.toast('Все стандартные ответы на месте', TITLE);
    return;
  }
  const list = load();
  for (const d of missing) {
    list.push(make({ name: d.name, text: d.text, shortcut: byShortcut(list, d.shortcut) ? '' : d.shortcut }));
  }
  save(list);
  dzg.ui.toast('Вернул: ' + missing.map((d) => '«' + d.name + '»').join(', '), TITLE);
}

// --- Панель ---------------------------------------------------------------

let panel = null;
let searchInput = null;
let rows = [];
let query = '';
let editMode = false;

function renderRows() {
  if (!panel || !panel.isOpen()) return;
  for (const row of rows) {
    if (row && row.destroy) row.destroy();
  }
  rows = [];
  const add = (widget) => {
    if (widget) rows.push(widget);
    return widget;
  };
  const all = load();
  const found = search(all, query, setting('sort', 'uses'));
  const q = query.trim();
  if (!all.length) {
    add(panel.addLabel('Быстрых ответов пока нет. Создай первый кнопкой ниже или сохрани любое сообщение через его меню.', { muted: true }));
  } else if (!found.length) {
    add(panel.addLabel('Ничего не нашлось по «' + q + '».', { muted: true }));
    if (!q.startsWith(';;')) add(panel.addButton('Создать «' + oneLine(q, 40) + '»', () => edit(null, { name: q.slice(0, MAX_NAME) }), { icon: 'plus' }));
  }
  for (const s of found.slice(0, MAX_SHOWN)) {
    const title = s.name + (s.shortcut ? '   ·   ;;' + s.shortcut : '');
    if (editMode) {
      add(panel.addButton('✎ ' + title, () => edit(s.id), { style: 'row' }));
      add(panel.addLabel(oneLine(s.text, 140), { muted: true }));
      add(panel.addButton('✕ Удалить', () => remove(s.id, true), { style: 'light' }));
    } else {
      add(panel.addButton(title, () => use(s.id, 'insert', { fromPanel: true }), { style: 'row' }));
      add(panel.addLabel(oneLine(s.text, 140), { muted: true }));
      add(panel.addButton('➤ Отправить сразу', () => use(s.id, 'send', { fromPanel: true }), { style: 'light' }));
    }
  }
  if (found.length > MAX_SHOWN) {
    add(panel.addLabel('…и ещё ' + (found.length - MAX_SHOWN) + '. Уточни поиск.', { muted: true }));
  }
  if (!q) {
    add(panel.addLabel('Управление', { bold: true }));
    add(panel.addButton('Новый быстрый ответ', () => edit(null), { icon: 'plus' }));
    add(panel.addButton('Экспорт в буфер обмена', exportToClipboard, { icon: 'upload' }));
    add(panel.addButton('Импорт из буфера обмена', importFromClipboard, { icon: 'download' }));
    const missing = missingDefaults().length;
    if (missing) add(panel.addButton('Вернуть стандартные (' + missing + ')', restoreDefaults, { icon: 'restore' }));
    add(panel.addLabel(VARIABLES_HELP + '\n/q название — отправить по названию, /q — этот список.', { muted: true }));
  }
}

function pickFirst() {
  const first = search(load(), query, setting('sort', 'uses'))[0];
  if (!first) return;
  if (editMode) edit(first.id);
  else use(first.id, 'insert', { fromPanel: true });
}

export function openPanel(initialQuery) {
  if (!dzg.ui.panel) {
    const list = sortSnippets(load(), setting('sort', 'uses'));
    dzg.ui.alert(list.map((s) => s.name + (s.shortcut ? ' (;;' + s.shortcut + ')' : '') + ' — ' + oneLine(s.text, 60)).join('\n') || 'Список пуст', TITLE);
    return;
  }
  const initial = String(initialQuery ?? '');
  if (panel && panel.isOpen()) {
    if (initialQuery !== undefined && searchInput) {
      query = initial;
      searchInput.setText(initial);
    }
    panel.show();
    renderRows();
    return;
  }
  query = initial;
  editMode = false;
  panel = dzg.ui.panel({ title: TITLE, width: 380 });
  panel.on('close', () => {
    panel = null;
    searchInput = null;
    rows = [];
  });
  searchInput = panel.addInput({
    placeholder: 'Поиск: название, текст или ;;сокращение',
    value: initial,
    onChange: (text) => {
      query = String(text ?? '');
      renderRows();
    },
    onSubmit: () => pickFirst(),
  });
  panel.addLabel('Клик — вставить в поле ввода · «Отправить сразу» — в открытый чат · Enter — первый в списке', { muted: true });
  panel.addToggle('Режим правки', false, (checked) => {
    editMode = !!checked;
    renderRows();
  });
  panel.addSeparator();
  renderRows();
  if (searchInput && searchInput.setFocus) searchInput.setFocus();
}

export function closePanel() {
  if (!panel) return;
  const current = panel;
  panel = null;
  searchInput = null;
  rows = [];
  current.close();
}

function togglePanel() {
  if (panel && panel.isOpen()) closePanel();
  else openPanel();
}

// --- /q -------------------------------------------------------------------

async function quickCommand({ rest, chat, replyTo }) {
  if (!rest) {
    openPanel();
    return;
  }
  const found = resolve(load(), rest);
  if (!found.snippet) {
    const word = rest.split(/\s+/)[0];
    dzg.ui.toast(found.matches.length
      ? 'Под «' + word + '» подходит несколько ответов — выбери в списке'
      : 'Быстрого ответа «' + word + '» нет — вот весь список', TITLE);
    openPanel(found.matches.length ? word : '');
    return;
  }
  // Без чата (dzg.commands.run из другого плагина) — в поле ввода.
  await use(found.snippet.id, chat ? 'send' : 'insert', { chat: chat || null, replyTo, rest: found.rest });
}

dzg.commands.register('q', {
  description: 'Быстрые ответы: /q — список, /q название [текст] — отправить',
  run: quickCommand,
});

// --- ;;сокращение перед отправкой -----------------------------------------

const SHORTCUT_MESSAGE = /^;;([\p{L}\p{N}_-]{1,32})$/u;
let missed = { text: '', peerId: '', at: 0 };

// Вернуть текст в поле ввода, если оно ещё пустое. Позже текущей отправки:
// клиент очищает поле после хуков.
function restoreCompose(text) {
  setTimeout(() => {
    const compose = dzg.ui.compose;
    if (!compose || !compose.get || !compose.set) return;
    const state = compose.get();
    if (state && !state.text) compose.set(text);
  }, 0);
}

export function beforeSend(ctx) {
  if (!setting('shortcuts', true)) return undefined;
  const text = String(ctx.text ?? '');
  const m = SHORTCUT_MESSAGE.exec(text.trim());
  if (!m) return undefined;
  const snippet = byShortcut(load(), m[1]);
  if (!snippet) {
    const now = Date.now();
    if (missed.text === text && missed.peerId === ctx.peerId && now - missed.at < MISS_PASS_MS) {
      missed = { text: '', peerId: '', at: 0 };
      return undefined;
    }
    missed = { text, peerId: ctx.peerId, at: now };
    dzg.ui.toast('Быстрого ответа ;;' + m[1] + ' нет. Отправь ещё раз, чтобы ушло как есть.', TITLE);
    restoreCompose(text);
    return { cancel: true };
  }
  if (busy) {
    dzg.ui.toast('Сначала закончи с предыдущим быстрым ответом', TITLE);
    restoreCompose(text);
    return { cancel: true };
  }
  const chat = ctx.chat || dzg.chats.get(ctx.peerId);
  const replyTo = Number(ctx.replyTo) || 0;
  // С вопросами — окно ввода после отправки, ответ уйдёт сам.
  if (questions(snippet.text).length) {
    setTimeout(() => use(snippet.id, 'send', { chat, replyTo }), 0);
    return { cancel: true };
  }
  const expanded = expand(snippet.text, contextFor(chat, replyTo, ''));
  if (!expanded) {
    dzg.ui.toast('«' + snippet.name + '» получился пустым', TITLE);
    return { cancel: true };
  }
  bump(snippet.id);
  // Разметку клиент к подменённому тексту не применит — отправим сами.
  if (chat && setting('markdown', true) && hasMarkup(expanded)) {
    setTimeout(() => sendText(chat, expanded, replyTo, snippet.id), 0);
    return { cancel: true };
  }
  return expanded;
}

dzg.messages.onBeforeSend(beforeSend);

// --- Меню и кнопки --------------------------------------------------------

export function pickIcon(available) {
  const list = Array.isArray(available) ? available : [];
  return ['chat', 'reply', 'edit', 'customize'].find((name) => list.includes(name)) || list[0] || 'chat';
}

const icon = () => pickIcon(dzg.ui.icons ? dzg.ui.icons() : []);

dzg.ui.addMessageMenuItem({
  title: '⚡ Сохранить как быстрый ответ',
  when: (m) => !!(m && !m.service && String(m.text || '').trim()),
  onClick: (m) => saveFromMessage(m),
});

if (dzg.ui.addMainMenuItem) {
  dzg.ui.addMainMenuItem({ title: 'Быстрые ответы', icon: icon(), onClick: () => openPanel() });
}

let removeComposeButton = null;

function syncComposeButton() {
  const want = !!setting('composeButton', true);
  if (want && !removeComposeButton && dzg.ui.compose && dzg.ui.compose.addButton) {
    removeComposeButton = dzg.ui.compose.addButton({
      icon: icon(),
      tooltip: 'Быстрые ответы',
      onClick: () => togglePanel(),
    });
  } else if (!want && removeComposeButton) {
    removeComposeButton();
    removeComposeButton = null;
  }
}

dzg.on('settings.changed', ({ key }) => {
  if (key === 'composeButton') syncComposeButton();
  if (key === 'sort') renderRows();
});

dzg.on('unload', () => {
  closePanel();
  if (removeComposeButton) {
    removeComposeButton();
    removeComposeButton = null;
  }
});

seed();
syncComposeButton();
