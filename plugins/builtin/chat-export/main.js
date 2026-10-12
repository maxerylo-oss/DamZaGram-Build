// Экспорт чата: история чата в HTML, Markdown или JSON.
// Пункт меню чата «📤 Экспорт чата…» и /export [html|md|json] [N|все] [с [по]].
// История грузится страницами через dzg.messages.iterate; между страницами
// плагин отдаёт управление клиенту (await + таймер), поэтому интерфейс не
// подвисает, а сторожевой таймер в 3 секунды не срабатывает. FLOOD_WAIT и
// сбои сети переживаются: плагин ждёт и продолжает с того же места.
// Файлы — в <папка данных плагинов>/chat-export/exports/.
// Автор: @wetakush

import {
  WRITERS,
  replyEntry,
  formatNumber,
  formatDate,
  formatDateTime,
  messagesWord,
  dayKey,
  safeFileName,
} from './lib/render.js';

const PLUGIN = 'chat-export';
const ORDER = ['html', 'md', 'json'];
const FORMAT_TITLES = { html: 'HTML', md: 'Markdown', json: 'JSON' };
const FORMAT_ALIASES = { html: 'html', htm: 'html', md: 'md', markdown: 'md', json: 'json' };
const PAGE = 100;
const SLICE_MS = 200;
const UI_MS = 250;
// Все форматы держатся в памяти до записи, а на плагин даётся 256 МБ.
const MAX_CHARS = 40000000;
const MAX_FLOOD_WAIT = 15 * 60;
const MAX_RETRIES = 6;
const HISTORY_KEY = 'history';
const LAST_KEY = 'last';
const HISTORY_SIZE = 20;

const USAGE = '/export — окно с настройками\n'
  + '/export html 1000 — последние 1000 сообщений в HTML\n'
  + '/export md все — вся история в Markdown\n'
  + '/export json 2026-01-01 2026-03-31 — за период\n'
  + '/export list — последние экспорты, /export stop — остановить';

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return (value === undefined || value === null || value === '') ? fallback : value;
};

const clock = () => ((globalThis.performance && typeof performance.now === 'function') ? performance.now() : Date.now());
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, Math.max(0, Math.floor(ms) || 0)));
const toast = (text) => dzg.ui.toast(text, '📤 Экспорт чата');

// --- Разбор ввода ---------------------------------------------------------

/**
 * Сколько сообщений: число, «10k» / «10к», «все» / «all» (→ 0).
 * null — пусто (взять значение по умолчанию), NaN — не понял.
 */
export function parseCount(value) {
  const s = String(value ?? '').trim().toLowerCase().replace(/[\s ]+/g, '');
  if (!s) return null;
  if (/^(все|всё|вся|all|full|0)$/.test(s)) return 0;
  const m = /^(\d+(?:[.,]\d+)?)(k|к|тыс\.?)?$/.exec(s);
  if (!m) return NaN;
  const n = Math.round(Number(m[1].replace(',', '.')) * (m[2] ? 1000 : 1));
  return n > 0 ? n : NaN;
}

/**
 * Дата ГГГГ-ММ-ДД (или ДД.ММ.ГГГГ) → unix-секунды начала дня по местному
 * времени; endOfDay — начало следующего дня (граница «по» включительно).
 * 0 — пусто, NaN — не понял или такой даты нет.
 */
export function parseDateInput(value, endOfDay) {
  const s = String(value ?? '').trim();
  if (!s) return 0;
  let y;
  let mo;
  let d;
  let m = /^(\d{4})[-./](\d{1,2})[-./](\d{1,2})$/.exec(s);
  if (m) {
    y = Number(m[1]);
    mo = Number(m[2]);
    d = Number(m[3]);
  } else if ((m = /^(\d{1,2})[./](\d{1,2})[./](\d{4}|\d{2})$/.exec(s))) {
    d = Number(m[1]);
    mo = Number(m[2]);
    y = Number(m[3]);
    if (y < 100) y += 2000;
  } else {
    return NaN;
  }
  const check = new Date(y, mo - 1, d);
  if (check.getFullYear() !== y || check.getMonth() !== mo - 1 || check.getDate() !== d) return NaN;
  return Math.floor(new Date(y, mo - 1, d + (endOfDay ? 1 : 0)).getTime() / 1000);
}

const DATE_LIKE = /^\d{1,4}[-./]\d{1,2}[-./]\d{1,4}$/;

/** Аргументы /export → { formats, count, from, to, service, action, error }. */
export function parseCommand(args) {
  const result = { formats: [], count: null, from: '', to: '', service: null, action: '', error: '' };
  for (const raw of args || []) {
    const a = String(raw).toLowerCase();
    if (FORMAT_ALIASES[a]) {
      if (!result.formats.includes(FORMAT_ALIASES[a])) result.formats.push(FORMAT_ALIASES[a]);
    } else if (/^(stop|cancel|стоп|отмена|отменить)$/.test(a)) {
      result.action = 'cancel';
    } else if (/^(list|history|список|история)$/.test(a)) {
      result.action = 'list';
    } else if (/^(folder|dir|папка)$/.test(a)) {
      result.action = 'folder';
    } else if (/^(help|помощь|\?)$/.test(a)) {
      result.action = 'help';
    } else if (/^(service|\+service|служебные|\+служебные)$/.test(a)) {
      result.service = true;
    } else if (/^(-service|noservice|-служебные)$/.test(a)) {
      result.service = false;
    } else if (DATE_LIKE.test(a)) {
      if (!result.from) result.from = raw;
      else if (!result.to) result.to = raw;
      else result.error = 'Лишняя дата «' + raw + '»: нужно не больше двух (с и по).';
    } else {
      const n = parseCount(a);
      if (n === null || Number.isNaN(n)) result.error = 'Не понял «' + raw + '».';
      else result.count = n;
    }
  }
  return result;
}

function defaultFormat() {
  const value = String(setting('format', 'html'));
  return ORDER.includes(value) ? value : 'html';
}

function defaultCount() {
  const value = String(setting('count', '1000'));
  if (value === 'all') return 0;
  return Number(value) > 0 ? Math.floor(Number(value)) : 1000;
}

function maxMessages() {
  const value = Math.floor(Number(setting('maxMessages', 100000)));
  return value > 0 ? value : 100000;
}

/**
 * Проверить и дополнить параметры экспорта значениями по умолчанию.
 * raw: { formats, count (null | 0 | n | NaN), from, to, service (null | bool) }.
 */
export function resolveOptions(raw) {
  const formats = ORDER.filter((f) => (raw.formats || []).includes(f));
  if (!formats.length) formats.push(defaultFormat());
  let count = raw.count;
  // Период без числа — значит весь период.
  if (count === null || count === undefined) count = (raw.from || raw.to) ? 0 : defaultCount();
  if (Number.isNaN(count)) return { error: 'Сколько сообщений: число (100, 1000, 10000) или «все».' };
  const cap = maxMessages();
  const all = !count;
  const from = parseDateInput(raw.from, false);
  const to = parseDateInput(raw.to, true);
  if (Number.isNaN(from)) return { error: 'Не понял дату «' + raw.from + '». Формат ГГГГ-ММ-ДД, например 2026-01-31.' };
  if (Number.isNaN(to)) return { error: 'Не понял дату «' + raw.to + '». Формат ГГГГ-ММ-ДД, например 2026-03-31.' };
  if (from && to && from >= to) return { error: 'Дата «с» позже даты «по».' };
  return {
    formats,
    all,
    limit: all ? cap : Math.min(count, cap),
    capped: all || count > cap,
    from,
    to,
    toDay: to ? parseDateInput(raw.to, false) : 0,
    service: (raw.service === null || raw.service === undefined) ? !!setting('includeService', false) : !!raw.service,
  };
}

/** 'HTML + JSON · последние 1 000 · 01.01.2026 — 31.03.2026 · без служебных'. */
export function describe(opts) {
  const parts = [opts.formats.map((f) => FORMAT_TITLES[f]).join(' + ')];
  parts.push(opts.all ? 'все сообщения' : 'последние ' + formatNumber(opts.limit));
  if (opts.from || opts.to) {
    parts.push((opts.from ? formatDate(opts.from) : '…') + ' — ' + (opts.to ? formatDate(opts.toDay || opts.to - 86400) : 'сегодня'));
  }
  parts.push(opts.service ? 'со служебными' : 'без служебных');
  return parts.join(' · ');
}

// --- Повторы при ошибках --------------------------------------------------

const errorString = (e) => String((e && (e.type || e.message)) || e || 'неизвестная ошибка');

/** Через сколько повторить запрос или null, если ошибка не временная. */
export function retryPlan(error, attempt) {
  const text = errorString(error);
  const flood = /(?:FLOOD_WAIT|FLOOD_PREMIUM_WAIT|SLOWMODE_WAIT)_(\d+)/.exec(text);
  const seconds = Number(error && error.seconds) || (flood ? Number(flood[1]) : 0);
  if (seconds || /FLOOD/.test(text)) {
    const wait = seconds || 5;
    if (wait > MAX_FLOOD_WAIT || attempt > 8) return null;
    return { ms: wait * 1000 + 500, flood: true };
  }
  if (/TIMEOUT|TIMED_OUT|INTERNAL|RPC_CALL_FAIL|RPC_MCGET_FAIL|WORKER_BUSY|MSG_WAIT|NETWORK|CONNECTION|(^|\D)5\d\d(\D|$)/i.test(text)) {
    if (attempt > MAX_RETRIES) return null;
    return { ms: Math.min(30000, 1000 * 2 ** attempt), flood: false };
  }
  return null;
}

export function formatSpan(seconds) {
  const s = Math.max(0, Math.round(Number(seconds) || 0));
  if (s < 60) return s + ' с';
  if (s < 3600) {
    const m = Math.floor(s / 60);
    return m + ' мин' + (m < 10 && s % 60 ? ' ' + (s % 60) + ' с' : '');
  }
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  return h + ' ч' + (m ? ' ' + m + ' мин' : '');
}

export function errorText(e) {
  const text = errorString(e);
  const flood = /FLOOD(?:_PREMIUM)?_WAIT_(\d+)/.exec(text);
  if (flood) return 'Telegram ограничил частоту запросов: нужно подождать ' + formatSpan(Number(flood[1])) + '.';
  if (/ACCOUNT_CHANGED/.test(text)) return 'Сменился активный аккаунт — экспорт остановлен.';
  if (/CHANNEL_PRIVATE|CHAT_FORBIDDEN|CHANNEL_INVALID|CHAT_ADMIN_REQUIRED/.test(text)) return 'Нет доступа к истории этого чата.';
  if (/PEER_NOT_FOUND|PEER_ID_INVALID/.test(text)) return 'Чат не найден в активном аккаунте.';
  if (/NO_SESSION/.test(text)) return 'Нет активного аккаунта.';
  return text;
}

// --- Файлы ----------------------------------------------------------------

export function exportDir() {
  let base = String(dzg.plugin.dataDir || '').replace(/\\/g, '/');
  if (base && !base.endsWith('/')) base += '/';
  return base + PLUGIN + '/exports/';
}

/** Путь → file:///… для dzg.openUrl (пробелы, кириллица, # и ?). */
export function fileUrl(path) {
  const clean = String(path || '').replace(/\\/g, '/').replace(/^\/+/, '');
  return 'file:///' + encodeURI(clean).replace(/#/g, '%23').replace(/\?/g, '%3F');
}

function openFolder() {
  dzg.openUrl(fileUrl(exportDir()));
}

function openFile(path) {
  if (dzg.fs.exists && !dzg.fs.exists(path)) {
    toast('Файл не найден — возможно, его удалили:\n' + path);
    return;
  }
  dzg.openUrl(fileUrl(path));
}

function uniqueBase(dir, base, formats) {
  const taken = (name) => formats.some((f) => dzg.fs.exists && dzg.fs.exists(dir + name + '.' + f));
  if (!taken(base)) return base;
  for (let i = 2; i < 100; ++i) {
    if (!taken(base + '-' + i)) return base + '-' + i;
  }
  return base + '-' + Date.now();
}

function history() {
  const list = dzg.storage.get(HISTORY_KEY, []);
  return Array.isArray(list) ? list : [];
}

function remember(entry) {
  dzg.storage.set(HISTORY_KEY, [entry].concat(history()).slice(0, HISTORY_SIZE));
}

function lastOptions() {
  const value = dzg.storage.get(LAST_KEY, null);
  return (value && typeof value === 'object') ? value : {};
}

// --- Панель ---------------------------------------------------------------

let panel = null;
// Номер текущего содержимого панели: прогресс экспорта обновляет панель,
// только пока в ней показан именно он.
let view = 0;
let job = null;
let boxOpen = false;
let unloaded = false;

/** Идёт ли сейчас экспорт (для тестов и других плагинов). */
export function busy() {
  return !!job;
}

function openPanel(title) {
  ++view;
  if (panel && panel.isOpen()) {
    panel.clear();
    panel.setTitle(title);
    if (typeof panel.show === 'function') panel.show();
    return panel;
  }
  const p = dzg.ui.panel({ title, width: 360 });
  panel = p;
  p.on('close', () => {
    if (panel === p) panel = null;
  });
  return p;
}

const BAR_CELLS = 14;

export function progressBar(fraction) {
  const n = Math.max(0, Math.min(BAR_CELLS, Math.round(fraction * BAR_CELLS)));
  return '▰'.repeat(n) + '▱'.repeat(BAR_CELLS - n);
}

/** Тексты прогресса: полоска, счётчики со скоростью и ETA, до какой даты дошли. */
export function progressInfo(j, now) {
  const elapsed = Math.max(0.001, ((now === undefined ? clock() : now) - j.started) / 1000);
  const fractions = [];
  let target = 0;
  if (!j.opts.all) {
    target = j.opts.limit;
    fractions.push(j.exported / j.opts.limit);
  }
  if (j.total && !j.opts.from && !j.opts.to) {
    const goal = Math.min(j.total, j.opts.limit);
    fractions.push(j.scanned / goal);
    if (!target) target = goal;
  }
  if (j.opts.from && j.lastDate) {
    const top = Math.min(j.opts.to || Infinity, j.firstDate || Infinity, Date.now() / 1000);
    if (top > j.opts.from) fractions.push((top - j.lastDate) / (top - j.opts.from));
  }
  const fraction = fractions.length ? Math.max(0, Math.min(0.99, Math.max(...fractions))) : -1;
  const speed = j.scanned / elapsed;
  const parts = ['Выгружено ' + formatNumber(j.exported) + (target ? ' из ' + formatNumber(target) : '')];
  if (j.scanned >= PAGE) parts.push(formatNumber(speed) + ' сообщ./с');
  if (fraction > 0.02 && j.scanned >= PAGE) parts.push('осталось ≈ ' + formatSpan(elapsed * (1 - fraction) / fraction));
  return {
    fraction,
    bar: fraction >= 0 ? progressBar(fraction) + '  ' + Math.floor(fraction * 100) + '%' : '⏳ ' + formatNumber(j.scanned) + ' просмотрено',
    status: parts.join(' · '),
    where: j.lastDate ? 'Дошли до ' + formatDateTime(j.lastDate) : 'Загружаю историю…',
  };
}

function uiAlive(j) {
  return !!(j.ui && panel && j.panel === panel && j.view === view && panel.isOpen());
}

function updateProgress(j, force) {
  const t = clock();
  if (!force && t - j.lastUi < UI_MS) return;
  j.lastUi = t;
  if (!uiAlive(j)) return;
  const info = progressInfo(j, t);
  j.ui.bar.setText(info.bar);
  j.ui.status.setText(j.waitText || info.status);
  j.ui.where.setText(info.where);
}

function renderProgress(j) {
  const p = openPanel('📤 Экспорт чата');
  p.addLabel('«' + j.chat.name + '»', { bold: true });
  p.addLabel(describe(j.opts), { muted: true });
  p.addSkip(4);
  const ui = {
    bar: p.addLabel(''),
    status: p.addLabel(''),
    where: p.addLabel('', { muted: true }),
  };
  p.addSeparator();
  p.addButton('Остановить и сохранить', () => {
    if (job !== j || j.cancelled) return;
    j.stopEarly = true;
    ui.status.setText('Останавливаю и сохраняю загруженное…');
  }, { style: 'light' });
  p.addButton('Отменить экспорт', () => {
    if (job !== j) return;
    j.cancelled = true;
    ui.status.setText('Отменяю…');
  }, { style: 'light' });
  p.addLabel('Панель можно закрыть: экспорт продолжится, а по окончании придёт уведомление.', { muted: true });
  j.ui = ui;
  j.panel = p;
  j.view = view;
  updateProgress(j, true);
}

// --- Экспорт --------------------------------------------------------------

function linkFor(j, id) {
  if (!j.linksOn || typeof dzg.messages.link !== 'function') return null;
  let url = null;
  try {
    url = dzg.messages.link(j.chat, id);
  } catch (e) {
    url = null;
  }
  if (!url) j.linksOn = false;
  return url || null;
}

function selfId() {
  const me = (dzg.users && typeof dzg.users.self === 'function') ? dzg.users.self() : null;
  return me ? String(me.id) : '';
}

// Шапка: описание и число участников, а для «всех» — сколько сообщений в
// чате, чтобы показать процент и ETA. Ничего не ждёт: что успеет, то и будет.
function prefetch(j) {
  if (typeof dzg.chats.info === 'function') {
    Promise.resolve().then(() => dzg.chats.info(j.chat)).then((info) => { j.info = info || null; }, () => {});
  }
  if (!j.opts.from && !j.opts.to && dzg.api && typeof dzg.api.call === 'function') {
    Promise.resolve().then(() => dzg.api.call('messages.getHistory', {
      peer: j.chat, offset_id: 0, offset_date: 0, add_offset: 0, limit: 1, max_id: 0, min_id: 0, hash: 0,
    }, { applyResult: false, timeout: 10000, floodSleep: 0 })).then((r) => {
      const n = Number(r && r.count) || ((r && Array.isArray(r.messages)) ? r.messages.length : 0);
      if (n > 0) j.total = n;
    }, () => {});
  }
}

function writers(j) {
  return j.formats.map((f) => j.writers[f]).filter(Boolean);
}

function accept(j, m) {
  if (j.needReplies) j.replies.set(m.id, replyEntry(m));
  // Писатель получает сообщение вместе с соседом постарше: так он знает,
  // начинается ли с него новый день и тот же ли это автор.
  if (j.pending) {
    for (const w of writers(j)) w.add(j.pending, m);
  }
  j.pending = m;
  j.exported++;
  if (!j.newest) j.newest = m.date;
  j.oldest = m.date;
}

function flushPending(j) {
  if (!j.pending) return;
  for (const w of writers(j)) w.add(j.pending, null);
  j.pending = null;
}

const charsOf = (j) => writers(j).reduce((sum, w) => sum + w.chars, 0);

// Между страницами отдаём управление клиенту: таймер — это новый вызов
// плагина со своим лимитом в 3 секунды. true — пора остановиться.
async function pace(j) {
  if (j.cancelled || j.stopEarly || unloaded) return true;
  j.sincePause++;
  const t = clock();
  if (j.sincePause < PAGE && t - j.sliceStart < SLICE_MS) return false;
  const pageDone = j.sincePause >= PAGE;
  j.sincePause = 0;
  if (j.selfId && selfId() && selfId() !== j.selfId) throw new Error('ACCOUNT_CHANGED');
  updateProgress(j, false);
  const delay = Math.max(0, Math.min(5000, Number(setting('pageDelay', 150)) || 0));
  await sleep(pageDone ? delay : 0);
  j.sliceStart = clock();
  return j.cancelled || j.stopEarly || unloaded;
}

async function waitRetry(j, plan) {
  const steps = Math.max(1, Math.ceil(plan.ms / 1000));
  for (let left = steps; left > 0; --left) {
    j.waitText = (plan.flood ? '⏳ Telegram просит подождать: ещё ' : '⚠️ Сбой связи, повтор через ') + formatSpan(left);
    updateProgress(j, true);
    await sleep(1000);
    if (j.cancelled || j.stopEarly || unloaded) break;
  }
  j.waitText = '';
  updateProgress(j, true);
  return j.cancelled || j.stopEarly || unloaded;
}

async function collect(j) {
  const o = j.opts;
  let offsetId = 0;
  if (o.to) {
    // Сразу к нужной дате, а не листать всё, что новее.
    try {
      const before = await dzg.messages.history(j.chat, { limit: 1, offsetDate: o.to });
      if (!before || !before.length) return;
      offsetId = before[before.length - 1].id + 1;
    } catch (e) {
      offsetId = 0;
    }
    if (j.cancelled || unloaded) return;
  }
  let attempt = 0;
  for (;;) {
    try {
      for await (const m of dzg.messages.iterate(j.chat, { offsetId, batch: PAGE })) {
        if (!m || !m.id) continue;
        offsetId = m.id;
        attempt = 0;
        j.scanned++;
        j.lastDate = m.date;
        if (!j.firstDate) j.firstDate = m.date;
        if (o.from && m.date < o.from) return;
        if (!(o.to && m.date >= o.to) && (o.service || !m.service)) {
          accept(j, m);
          if (j.exported >= o.limit) {
            if (o.capped) j.truncated = 'Достигнут предел в ' + formatNumber(o.limit) + ' ' + messagesWord(o.limit) + ' (настройка «Максимум сообщений»).';
            return;
          }
          if (charsOf(j) > MAX_CHARS) {
            j.truncated = 'Файл стал слишком большим — выгружены последние ' + formatNumber(j.exported) + ' ' + messagesWord(j.exported) + '. Остальное выгрузи по диапазону дат.';
            return;
          }
        }
        if (await pace(j)) return;
      }
      return;
    } catch (e) {
      if (j.cancelled || j.stopEarly || unloaded) return;
      const plan = retryPlan(e, ++attempt);
      if (!plan) throw e;
      console.warn('chat-export: ' + errorString(e) + ', повтор через ' + plan.ms + ' мс');
      if (await waitRetry(j, plan)) return;
    }
  }
}

async function writeFiles(j) {
  flushPending(j);
  if (uiAlive(j)) j.ui.status.setText('Сохраняю файлы…');
  await sleep(0);
  const dir = exportDir();
  const name = safeFileName(j.chat.name, 'chat_' + (j.chat.tgId || j.chat.id));
  const base = uniqueBase(dir, name + '-' + dayKey(Date.now() / 1000), j.formats);
  const meta = {
    chat: j.chat,
    info: j.info,
    count: j.exported,
    first: j.oldest,
    last: j.newest,
    exportedAt: Date.now(),
    truncated: j.truncated || (j.stopEarly ? 'Экспорт остановлен вручную: выгружены последние ' + formatNumber(j.exported) + ' ' + messagesWord(j.exported) + '.' : '')
      || (j.error ? 'Экспорт прерван ошибкой: ' + j.error : ''),
    theme: String(setting('htmlTheme', 'auto')),
    filter: {
      count: j.opts.all ? null : j.opts.limit,
      from: j.opts.from ? dayKey(j.opts.from) : null,
      to: j.opts.to ? dayKey(j.opts.toDay || j.opts.to - 86400) : null,
      includeService: !!j.opts.service,
    },
  };
  const files = [];
  const failed = [];
  for (const f of j.formats) {
    const writer = j.writers[f];
    if (!writer) continue;
    const text = writer.finish(meta);
    j.writers[f] = null;
    const file = base + '.' + f;
    if (dzg.fs.write(dir + file, text)) files.push({ format: f, path: dir + file, name: file });
    else failed.push(FORMAT_TITLES[f]);
    await sleep(0);
  }
  return { files, failed, meta };
}

function sendToSaved(j, files) {
  const me = selfId();
  if (!me) {
    toast('Нет активного аккаунта');
    return;
  }
  const ok = dzg.messages.sendFile(me, files.map((f) => f.path), {
    asDocument: true,
    caption: '📤 Экспорт «' + j.chat.name + '»: ' + formatNumber(j.exported) + ' ' + messagesWord(j.exported),
  });
  toast(ok ? 'Отправляю в Избранное…' : 'Не удалось отправить файл');
}

function renderDone(j, result) {
  const { files, failed, meta } = result;
  const p = openPanel('📤 Экспорт чата');
  p.addLabel('✅ Готово: «' + j.chat.name + '»', { bold: true });
  const seconds = (clock() - j.started) / 1000;
  p.addLabel(formatNumber(j.exported) + ' ' + messagesWord(j.exported)
    + (j.oldest ? ' · ' + formatDate(j.oldest) + ' — ' + formatDate(j.newest) : '')
    + ' · за ' + formatSpan(seconds));
  if (meta.truncated) p.addLabel('⚠️ ' + meta.truncated, { muted: true });
  if (failed.length) p.addLabel('Не удалось записать: ' + failed.join(', '), { muted: true });
  for (const f of files) p.addLabel(FORMAT_TITLES[f.format] + ': ' + f.name, { muted: true });
  p.addSeparator();
  for (const f of files) p.addButton('Открыть ' + FORMAT_TITLES[f.format], () => openFile(f.path), { style: 'light' });
  p.addButton('Открыть папку', openFolder, { style: 'light' });
  p.addButton('Скопировать путь', () => {
    dzg.clipboard.set(files.map((f) => f.path).join('\n'));
    toast('Путь скопирован');
  }, { style: 'light' });
  let sentAt = 0;
  p.addButton('Отправить в Избранное', () => {
    if (Date.now() - sentAt < 10000) return;
    sentAt = Date.now();
    sendToSaved(j, files);
  }, { style: 'light' });
  p.addButton('Экспортировать ещё…', () => openOptions(j.chat), { style: 'light' });
}

// Итог без файлов: отмена, ошибка, пустой период. Панель открывается заново,
// только если есть что сохранить, иначе хватает всплывающей подсказки.
function renderEnd(j, title, text, onSave) {
  if (unloaded) return;
  if (!uiAlive(j) && !onSave) {
    toast(title + (text ? '\n' + text : ''));
    return;
  }
  const p = openPanel('📤 Экспорт чата');
  const status = p.addLabel(title, { bold: true });
  j.panel = p;
  j.view = view;
  j.ui = { status, bar: status, where: status };
  if (text) p.addLabel(text, { muted: true });
  p.addSeparator();
  if (onSave) p.addButton('Сохранить загруженное (' + formatNumber(j.exported) + ')', onSave, { style: 'light' });
  p.addButton('Экспортировать заново…', () => openOptions(j.chat), { style: 'light' });
  p.addButton('Закрыть', () => p.close(), { style: 'light' });
}

async function saveResult(j) {
  const result = await writeFiles(j);
  if (!result.files.length) {
    if (uiAlive(j)) renderEnd(j, '⚠️ Не удалось записать файлы', exportDir(), null);
    else toast('Не удалось записать файлы в ' + exportDir());
    return result;
  }
  remember({
    at: Date.now(),
    chat: j.chat.name,
    peerId: j.chat.id,
    count: j.exported,
    files: result.files.map((f) => ({ format: f.format, path: f.path })),
  });
  if (uiAlive(j)) renderDone(j, result);
  const text = '«' + j.chat.name + '»: ' + formatNumber(j.exported) + ' ' + messagesWord(j.exported)
    + ' → ' + result.files.map((f) => f.name).join(', ') + '\nНажми «Открыть», чтобы открыть папку.';
  if (typeof dzg.ui.notify === 'function') {
    dzg.ui.notify({ title: '📤 Экспорт готов', text, onClick: openFolder });
  } else {
    toast(text);
  }
  if (setting('openAfter', false)) openFile(result.files[0].path);
  return result;
}

/**
 * Запустить экспорт. Возвращает { files, failed, meta } или null, если
 * экспорт отменён, сорвался или в диапазоне нет сообщений.
 */
export async function startExport(chat, opts) {
  if (unloaded || !chat) return null;
  if (job) {
    toast('Уже идёт экспорт «' + job.chat.name + '». Дождись окончания или отмени его.');
    renderProgress(job);
    return null;
  }
  if (typeof dzg.messages.iterate !== 'function' || typeof dzg.ui.panel !== 'function') {
    toast('Этой версии DamZaGram не хватает API для экспорта');
    return null;
  }
  const j = {
    chat,
    opts,
    formats: opts.formats.slice(),
    selfId: selfId(),
    started: clock(),
    sliceStart: clock(),
    lastUi: 0,
    sincePause: 0,
    scanned: 0,
    exported: 0,
    total: 0,
    firstDate: 0,
    lastDate: 0,
    oldest: 0,
    newest: 0,
    cancelled: false,
    stopEarly: false,
    waitText: '',
    truncated: '',
    error: '',
    info: null,
    pending: null,
    replies: new Map(),
    needReplies: opts.formats.some((f) => f !== 'json'),
    linksOn: !!setting('messageLinks', true) && chat.type !== 'user' && chat.type !== 'bot' && !chat.isSelf,
    writers: {},
    ui: null,
    panel: null,
    view: 0,
  };
  const ctx = { chat, replies: j.replies, link: (id) => linkFor(j, id) };
  for (const f of j.formats) j.writers[f] = new WRITERS[f](ctx);
  job = j;
  try {
    renderProgress(j);
    prefetch(j);
    try {
      await collect(j);
    } catch (e) {
      j.error = errorText(e);
      console.error('chat-export: ' + errorString(e));
    }
    if (unloaded) return null;
    if (j.cancelled) {
      renderEnd(j, 'Экспорт отменён', 'Файлы не сохранены.', null);
      return null;
    }
    if (j.error) {
      const exported = j.exported;
      renderEnd(j, '⚠️ Экспорт прерван', j.error + (exported ? '\nУже выгружено: ' + formatNumber(exported) + ' ' + messagesWord(exported) + '.' : ''),
        exported ? () => {
          if (job) {
            toast('Сначала дождись окончания экспорта «' + job.chat.name + '»');
            return;
          }
          job = j;
          saveResult(j)
            .catch((e) => toast('Не удалось сохранить: ' + errorString(e)))
            .finally(() => { if (job === j) job = null; });
        } : null);
      return null;
    }
    if (!j.exported) {
      renderEnd(j, 'Нет сообщений для экспорта', (opts.from || opts.to) ? 'В выбранном периоде сообщений нет.' : 'В чате нет подходящих сообщений.', null);
      return null;
    }
    return await saveResult(j);
  } finally {
    if (job === j) job = null;
  }
}

// --- Окно настроек экспорта -----------------------------------------------

/** Окно «Экспорт чата»: форматы, сколько сообщений, служебные, период. */
export async function openOptions(chat, prefill, attempt) {
  if (unloaded) return null;
  const target = chat || dzg.chats.current();
  if (!target) {
    toast('Открой чат, который нужно экспортировать');
    return null;
  }
  if (job) {
    toast('Уже идёт экспорт «' + job.chat.name + '»');
    renderProgress(job);
    return null;
  }
  if (boxOpen) return null;
  const last = lastOptions();
  const v = prefill || {};
  const formats = v.formats || (Array.isArray(last.formats) && last.formats.length ? last.formats : [defaultFormat()]);
  const defaultCountText = defaultCount() ? String(defaultCount()) : 'все';
  const service = (v.service !== undefined) ? v.service : (last.service !== undefined ? !!last.service : !!setting('includeService', false));
  boxOpen = true;
  let result;
  try {
    result = await dzg.ui.box({
      title: '📤 Экспорт чата',
      text: '«' + target.name + '»\n\nСколько сообщений: 100, 1000, 10000 или «все» — считая от самых новых.'
        + ' Даты — ГГГГ-ММ-ДД, их можно не заполнять. Можно выбрать несколько форматов сразу.',
      inputs: [
        { id: 'count', placeholder: 'Сколько: 100 / 1000 / 10000 / все', value: String(v.count ?? last.count ?? defaultCountText) },
        { id: 'from', placeholder: 'С даты, ГГГГ-ММ-ДД (необязательно)', value: String(v.from || '') },
        { id: 'to', placeholder: 'По дату включительно, ГГГГ-ММ-ДД (необязательно)', value: String(v.to || '') },
      ],
      toggles: [
        { id: 'html', text: 'HTML — красивая страница для браузера', checked: formats.includes('html') },
        { id: 'md', text: 'Markdown — текстовая расшифровка', checked: formats.includes('md') },
        { id: 'json', text: 'JSON — все данные для программ', checked: formats.includes('json') },
        { id: 'service', text: 'Включать служебные сообщения', checked: service },
      ],
      buttons: [
        { id: 'ok', text: 'Экспортировать' },
        { id: 'cancel', text: 'Отмена' },
      ],
    });
  } finally {
    boxOpen = false;
  }
  if (!result || result.button !== 'ok' || unloaded) return null;
  const values = result.values || {};
  const raw = {
    formats: ORDER.filter((f) => values[f] === true),
    count: parseCount(values.count),
    from: String(values.from || ''),
    to: String(values.to || ''),
    service: values.service === true,
  };
  const opts = resolveOptions(raw);
  if (opts.error) {
    toast(opts.error);
    if ((attempt || 0) >= 4) return null;
    return openOptions(target, { formats: raw.formats, count: values.count, from: raw.from, to: raw.to, service: raw.service }, (attempt || 0) + 1);
  }
  dzg.storage.set(LAST_KEY, { formats: opts.formats, count: String(values.count || '').trim() || defaultCountText, service: raw.service });
  return startExport(target, opts);
}

// --- Последние экспорты ---------------------------------------------------

export function showHistory() {
  const p = openPanel('📤 Экспорты чатов');
  if (job) {
    const running = job;
    p.addLabel('Идёт экспорт «' + running.chat.name + '»', { bold: true });
    p.addButton('Показать прогресс', () => { if (job === running) renderProgress(running); }, { style: 'light' });
    p.addSeparator();
  }
  const list = history();
  if (!list.length) {
    p.addLabel('Пока ничего не экспортировано. Меню чата → «📤 Экспорт чата…» или /export в любом чате.', { muted: true });
  }
  for (const entry of list) {
    const count = Number(entry.count) || 0;
    p.addLabel('«' + entry.chat + '» · ' + formatNumber(count) + ' ' + messagesWord(count), { bold: true });
    const files = Array.isArray(entry.files) ? entry.files : [];
    p.addLabel(formatDateTime((Number(entry.at) || 0) / 1000) + ' · ' + files.map((f) => FORMAT_TITLES[f.format] || f.format).join(', '), { muted: true });
    for (const f of files) p.addButton('Открыть ' + (FORMAT_TITLES[f.format] || f.format), () => openFile(f.path), { style: 'light' });
  }
  p.addSeparator();
  p.addButton('Открыть папку с экспортами', openFolder, { style: 'light' });
  if (list.length) {
    p.addButton('Очистить список (файлы останутся)', () => {
      dzg.storage.set(HISTORY_KEY, []);
      showHistory();
    }, { style: 'light' });
  }
}

// --- Команда и меню -------------------------------------------------------

async function exportCommand({ args, chat }) {
  const parsed = parseCommand(args);
  if (parsed.error) {
    toast(parsed.error + '\n\n' + USAGE);
    return;
  }
  switch (parsed.action) {
    case 'cancel':
      if (job) {
        // Итог покажет сам экспорт: в панели или подсказкой.
        job.cancelled = true;
      } else {
        toast('Сейчас ничего не экспортируется');
      }
      return;
    case 'list':
      showHistory();
      return;
    case 'folder':
      openFolder();
      return;
    case 'help':
      dzg.ui.alert(USAGE, '📤 Экспорт чата');
      return;
    default:
      break;
  }
  const target = chat || dzg.chats.current();
  if (!target) {
    toast('Открой чат, который нужно экспортировать');
    return;
  }
  if (!args || !args.length) {
    await openOptions(target);
    return;
  }
  if (job) {
    toast('Уже идёт экспорт «' + job.chat.name + '». Остановить: /export stop');
    renderProgress(job);
    return;
  }
  const opts = resolveOptions(parsed);
  if (opts.error) {
    toast(opts.error);
    return;
  }
  await startExport(target, opts);
}

dzg.commands.register('export', {
  description: 'Экспорт чата в файл: /export [html|md|json] [N|все] [с по]',
  run: exportCommand,
});

dzg.ui.addChatMenuItem({
  title: '📤 Экспорт чата…',
  onClick: (chat) => openOptions(chat),
});

dzg.on('unload', () => {
  unloaded = true;
  if (job) job.cancelled = true;
  if (panel) panel.close();
});
