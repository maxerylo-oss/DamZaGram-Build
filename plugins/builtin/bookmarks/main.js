// Закладки: любое сообщение — в закладки с заметкой и тегами.
// «В закладки» / «Убрать из закладок» в меню сообщения, панель /bm с
// поиском, фильтром по тегам и группировкой по чатам, кнопка в шапке чата,
// экспорт в Markdown. Удалённое сообщение остаётся в закладках с тем
// текстом, который был сохранён.
// Автор: @wetakush

const ITEMS_KEY = 'items';
const EXPORT_KEY = 'lastExport';
const MAX_ITEMS = 5000;
const PREVIEW_LEN = 300;
const NOTE_LEN = 1000;
const MAX_TAGS = 10;
const TAG_LEN = 32;
const TAG_BUTTONS = 6;
const SEARCH_DELAY_MS = 250;
const SAVE_DELAY_MS = 1000;
const TITLE = '🔖 Закладки';

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return (value === undefined || value === null || value === '') ? fallback : value;
};

const pad = (n) => String(n).padStart(2, '0');

/** Первые n символов, не разрывая эмодзи (суррогатную пару). */
export function cut(text, n) {
  const s = String(text || '');
  if (s.length <= n) return s;
  let end = n;
  const code = s.charCodeAt(end - 1);
  if (code >= 0xD800 && code <= 0xDBFF) end--;
  return s.slice(0, end);
}

const ellipsis = (text, n) => {
  const s = String(text || '');
  return s.length > n ? cut(s, n - 1) + '…' : s;
};
const flat = (text) => String(text || '').replace(/\s+/g, ' ').trim();
const norm = (text) => String(text || '').toLowerCase().replace(/ё/g, 'е');

const MEDIA = {
  photo: '🖼 фото', video: '🎬 видео', gif: 'GIF', sticker: 'стикер', voice: '🎤 голосовое',
  videoNote: '⏺ кружок', audio: '🎵 аудио', file: '📎 файл', poll: '📊 опрос', other: 'вложение',
};

const CHAT_ICON = { self: '⭐', user: '👤', bot: '🤖', group: '👥', channel: '📢' };
const chatIcon = (type) => CHAT_ICON[type] || '💬';

function mediaLabel(b) {
  if (!b.media) return '';
  return (MEDIA[b.media] || b.media) + (b.fileName ? ' «' + b.fileName + '»' : '');
}

/** Текст закладки одной строкой: вложение и начало текста. */
export function previewOf(b, n) {
  const text = flat(b.text);
  const media = mediaLabel(b);
  const body = media && text ? media + ': ' + text : (media || text || '(сообщение без текста)');
  return ellipsis(body, n || 220);
}

const author = (b) => (b.out ? 'Я' : (b.fromName || b.chatName || '?'));

export function formatDate(sec, nowMs) {
  if (!sec) return '';
  const d = new Date(sec * 1000);
  const now = new Date(nowMs === undefined ? Date.now() : nowMs);
  const time = pad(d.getHours()) + ':' + pad(d.getMinutes());
  if (d.toDateString() === now.toDateString()) return 'сегодня ' + time;
  const yesterday = new Date(now);
  yesterday.setDate(now.getDate() - 1);
  if (d.toDateString() === yesterday.toDateString()) return 'вчера ' + time;
  return pad(d.getDate()) + '.' + pad(d.getMonth() + 1) +
    (d.getFullYear() !== now.getFullYear() ? '.' + d.getFullYear() : '') + ' ' + time;
}

function fullDate(ms) {
  const d = new Date(ms);
  return pad(d.getDate()) + '.' + pad(d.getMonth() + 1) + '.' + d.getFullYear() + ' ' +
    pad(d.getHours()) + ':' + pad(d.getMinutes());
}

// --- Теги и заметки -------------------------------------------------------

const TAG_JUNK = /[^\p{L}\p{N}_-]/gu;

/** "#работа #идея", "работа, идея" → ['работа', 'идея']. */
export function parseTags(input) {
  const result = [];
  for (const raw of String(input || '').split(/[\s,;#]+/)) {
    const tag = cut(raw.replace(TAG_JUNK, '').toLowerCase(), TAG_LEN);
    if (tag && !result.includes(tag)) result.push(tag);
    if (result.length >= MAX_TAGS) break;
  }
  return result;
}

/**
 * Хэштеги в конце заметки становятся тегами: "позвонить #работа" →
 * { note: 'позвонить', tags: ['работа'] }. Хэштеги в середине остаются текстом.
 */
export function splitNote(input) {
  let note = String(input || '').trim();
  const tags = [];
  const tail = /(^|\s)#([\p{L}\p{N}_-]+)$/u;
  for (let m = tail.exec(note); m; m = tail.exec(note)) {
    tags.unshift(m[2]);
    note = note.slice(0, m.index).trimEnd();
  }
  return { note: cut(note, NOTE_LEN), tags: parseTags(tags.join(' ')) };
}

const tagLine = (tags) => tags.map((t) => '#' + t).join(' ');

/** Теги и сколько раз встречаются, частые первыми. */
export function tagStats(list) {
  const counts = new Map();
  for (const b of list) {
    for (const tag of b.tags) counts.set(tag, (counts.get(tag) || 0) + 1);
  }
  return Array.from(counts, ([tag, count]) => ({ tag, count }))
    .sort((a, b) => b.count - a.count || (a.tag < b.tag ? -1 : 1));
}

// --- Аккаунты -------------------------------------------------------------

// Закладка помнит аккаунт: в двух аккаунтах один и тот же id чата и
// сообщения — это разные переписки.
function currentAcc() {
  try {
    const self = dzg.users && dzg.users.self ? dzg.users.self() : null;
    return self && self.id ? String(self.id) : '';
  } catch (e) {
    return '';
  }
}

const accOf = (message) => String((message && message.selfId) || currentAcc());
const sameAcc = (b, acc) => !b.acc || !acc || b.acc === acc;

// --- Хранилище ------------------------------------------------------------

let items = [];
let nextId = 1;
const byKey = new Map();
const loose = new Set(); // "peerId|msgId" — быстрый отказ в обработчиках событий
const loosePeers = new Set();
const perChat = new Map(); // "acc|peerId" → число закладок
let saveTimer = 0;
let dirty = false;

const keyOf = (acc, peerId, msgId) => acc + '|' + peerId + '|' + msgId;

function normalize(raw) {
  if (!raw || typeof raw !== 'object') return null;
  const peerId = String(raw.peerId || '');
  const msgId = Number(raw.msgId) || 0;
  if (!peerId || msgId <= 0) return null;
  return Object.assign({}, raw, {
    id: Number(raw.id) || 0,
    acc: String(raw.acc || ''),
    peerId,
    msgId,
    chatName: String(raw.chatName || ''),
    fromName: String(raw.fromName || ''),
    text: String(raw.text || ''),
    note: String(raw.note || ''),
    tags: Array.isArray(raw.tags) ? parseTags(raw.tags.join(' ')) : [],
    date: Number(raw.date) || 0,
    created: Number(raw.created) || 0,
  });
}

function reindex() {
  byKey.clear();
  loose.clear();
  loosePeers.clear();
  perChat.clear();
  for (const b of items) {
    byKey.set(keyOf(b.acc, b.peerId, b.msgId), b);
    loose.add(b.peerId + '|' + b.msgId);
    loosePeers.add(b.peerId);
    const chatKey = b.acc + '|' + b.peerId;
    perChat.set(chatKey, (perChat.get(chatKey) || 0) + 1);
  }
}

function load() {
  const list = dzg.storage.get(ITEMS_KEY, []);
  items = (Array.isArray(list) ? list : []).map(normalize).filter(Boolean);
  nextId = items.reduce((max, b) => Math.max(max, b.id), 0) + 1;
  for (const b of items) {
    if (!b.id) b.id = nextId++;
  }
  reindex();
}

function save() {
  clearTimeout(saveTimer);
  saveTimer = 0;
  dirty = false;
  dzg.storage.set(ITEMS_KEY, items);
}

// Правки и удаления приходят пачками (очистка истории): пишем на диск
// не чаще раза в SAVE_DELAY_MS.
function saveLater() {
  dirty = true;
  if (!saveTimer) saveTimer = setTimeout(save, SAVE_DELAY_MS);
}

export function all() {
  return items.slice();
}

const byId = (id) => items.find((b) => b.id === id) || null;

export function findBy(acc, peerId, msgId) {
  const p = String(peerId || '');
  const id = Number(msgId) || 0;
  if (!loose.has(p + '|' + id)) return null;
  if (!acc) return items.find((b) => b.peerId === p && b.msgId === id) || null;
  return byKey.get(keyOf(acc, p, id)) || byKey.get(keyOf('', p, id)) || null;
}

/** Закладка для сообщения или null. Быстро: вызывается из меню и событий. */
export function find(message) {
  if (!message) return null;
  const peerId = String(message.peerId || (message.chat && message.chat.id) || '');
  if (!loose.has(peerId + '|' + Number(message.id))) return null;
  return findBy(accOf(message), peerId, message.id);
}

/** Сколько закладок в чате у активного аккаунта. */
export function chatCount(peerId, acc) {
  const p = String(peerId || '');
  if (!loosePeers.has(p)) return 0;
  const a = acc === undefined ? currentAcc() : acc;
  if (!a) return items.filter((b) => b.peerId === p).length;
  return (perChat.get(a + '|' + p) || 0) + (perChat.get('|' + p) || 0);
}

function scopeItems() {
  const acc = currentAcc();
  return items.filter((b) => sameAcc(b, acc));
}

export function canBookmark(message) {
  return !!message && !message.service && Number(message.id) > 0 &&
    !!(message.peerId || (message.chat && message.chat.id));
}

/** Что сохраняется из сообщения: всё, чтобы показать закладку без него. */
export function snapshot(message, acc) {
  const chat = message.chat || {};
  const text = String(message.text || '');
  return {
    acc: String(acc || ''),
    peerId: String(message.peerId || chat.id || ''),
    chatName: chat.isSelf ? 'Избранное' : String(chat.name || ''),
    chatType: chat.isSelf ? 'self' : String(chat.type || ''),
    msgId: Number(message.id) || 0,
    fromName: String(message.fromName || ''),
    out: !!message.out,
    date: Number(message.date) || 0,
    text: cut(text, PREVIEW_LEN),
    more: text.length > PREVIEW_LEN,
    media: message.media || '',
    fileName: (message.mediaInfo && message.mediaInfo.fileName) || '',
    deleted: !!message.deleted,
  };
}

let lastRemoved = null;

function changed() {
  reindex();
  save();
  if (dzg.ui.refreshButtons) {
    try {
      dzg.ui.refreshButtons();
    } catch (e) {
      console.warn('refreshButtons:', String(e));
    }
  }
  if (panel && panel.isOpen()) renderAll();
}

/** Добавить или обновить закладку. null — не влезла в лимит. */
export function upsert(message, note, tags) {
  const cleanNote = cut(String(note || '').trim(), NOTE_LEN);
  const cleanTags = parseTags((tags || []).join(' '));
  const existing = find(message);
  if (existing) {
    existing.note = cleanNote;
    existing.tags = cleanTags;
    changed();
    return existing;
  }
  if (items.length >= MAX_ITEMS) {
    dzg.ui.toast('В закладках уже ' + MAX_ITEMS + ' сообщений. Удали ненужные, чтобы добавить новые.', TITLE);
    return null;
  }
  const b = Object.assign({ id: nextId++ }, snapshot(message, accOf(message)), {
    note: cleanNote,
    tags: cleanTags,
    created: Date.now(),
  });
  items.push(b);
  changed();
  return b;
}

export function remove(id) {
  const b = byId(id);
  if (!b) return null;
  items = items.filter((x) => x !== b);
  lastRemoved = b;
  changed();
  return b;
}

function undo() {
  const b = lastRemoved;
  lastRemoved = null;
  if (b && !findBy(b.acc, b.peerId, b.msgId)) items.push(b);
  changed();
}

// --- Добавление и заметки -------------------------------------------------

const dialogs = new Set();

async function askNote(title, b, current) {
  const known = tagStats(scopeItems()).slice(0, 10).map((t) => '#' + t.tag).join(' ');
  const who = b.out ? 'Я' : (b.fromName || '');
  const result = await dzg.ui.box({
    title,
    text: (who ? who + ': ' : '') + previewOf(b, 160) +
      '\n\nХэштеги в конце заметки тоже станут тегами.' + (known ? '\nТвои теги: ' + known : ''),
    inputs: [
      { id: 'note', placeholder: 'Заметка (можно оставить пустой)', value: current.note || '' },
      { id: 'tags', placeholder: 'Теги: #работа #идея', value: tagLine(current.tags || []) },
    ],
    buttons: [
      { id: 'ok', text: 'Сохранить' },
      { id: 'cancel', text: 'Отмена' },
    ],
  });
  if (!result || result.button !== 'ok') return null;
  const values = result.values || {};
  const split = splitNote(values.note);
  return { note: split.note, tags: parseTags(String(values.tags || '') + ' ' + split.tags.join(' ')) };
}

/**
 * Добавить сообщение в закладки. preset — готовые заметка и теги (из
 * /bm add): тогда окно не показывается.
 */
export async function bookmark(message, preset) {
  if (!canBookmark(message)) return null;
  const key = keyOf(accOf(message), String(message.peerId || message.chat.id), Number(message.id));
  if (dialogs.has(key)) return null;
  const existing = find(message);
  let data = {
    note: existing ? existing.note : '',
    tags: existing ? existing.tags : [],
  };
  if (preset) {
    data = {
      note: preset.note || data.note,
      tags: parseTags(data.tags.concat(preset.tags || []).join(' ')),
    };
  } else if (setting('askNote', true)) {
    dialogs.add(key);
    try {
      data = await askNote(existing ? '📝 Заметка к закладке' : TITLE + ': добавить', existing || snapshot(message, ''), data);
    } finally {
      dialogs.delete(key);
    }
    if (!data) return null;
  }
  const b = upsert(message, data.note, data.tags);
  if (b) {
    dzg.ui.toast((b.tags.length ? tagLine(b.tags) + ' · ' : '') + 'все закладки — /bm',
      existing ? '🔖 Закладка обновлена' : '🔖 Добавлено в закладки');
  }
  return b;
}

async function unbookmark(message) {
  const b = find(message);
  if (!b) return;
  if (b.note) {
    const ok = await dzg.ui.confirm('Убрать сообщение из закладок? Заметка «' + ellipsis(b.note, 60) + '» тоже удалится.', {
      title: TITLE, confirmText: 'Убрать', cancelText: 'Отмена', danger: true,
    });
    if (!ok) return;
  }
  if (remove(b.id)) dzg.ui.toast('Убрано из закладок. Вернуть можно в панели /bm', TITLE);
}

async function editNote(id) {
  const b = byId(id);
  if (!b) return;
  const key = 'edit|' + id;
  if (dialogs.has(key)) return;
  dialogs.add(key);
  let data = null;
  try {
    data = await askNote('📝 Заметка к закладке', b, b);
  } finally {
    dialogs.delete(key);
  }
  if (!data) return;
  const current = byId(id);
  if (!current) {
    dzg.ui.toast('Эту закладку уже удалили', TITLE);
    return;
  }
  current.note = data.note;
  current.tags = data.tags;
  changed();
  dzg.ui.toast(current.note || current.tags.length ? 'Заметка сохранена' : 'Заметка очищена', TITLE);
}

function openBookmark(id) {
  const b = byId(id);
  if (!b) return;
  if (!dzg.chats.open(b.peerId, b.msgId)) {
    dzg.ui.toast('Не получилось открыть «' + (b.chatName || 'чат') + '»: его нет у этого аккаунта или он ещё не загружен.' +
      (b.deleted ? ' Сообщение удалено, текст сохранён в закладке.' : ''), TITLE);
  }
}

// --- Сообщения изменились или удалены ------------------------------------

function onDeleted(message) {
  const b = find(message);
  if (!b || b.deleted) return;
  b.deleted = true;
  b.deletedAt = Date.now();
  saveLater();
  scheduleList();
}

function onEdited(message) {
  const b = find(message);
  if (!b || b.deleted) return;
  if (message.deleted) {
    onDeleted(message);
    return;
  }
  if (!setting('trackEdits', true)) return;
  const text = String(message.text || '');
  const media = message.media || '';
  // Реакции, просмотры, кнопки — тоже «правка»: текст тот же, ничего не пишем.
  if ((!text && !media) || (cut(text, PREVIEW_LEN) === b.text && media === (b.media || ''))) return;
  b.text = cut(text, PREVIEW_LEN);
  b.more = text.length > PREVIEW_LEN;
  b.media = media;
  b.edited = true;
  saveLater();
  scheduleList();
}

dzg.on('message.deleted', onDeleted);
dzg.on('message.edited', onEdited);

// --- Фильтр, группы -------------------------------------------------------

function haystack(b) {
  return norm([b.text, b.note, b.chatName, b.fromName, tagLine(b.tags), mediaLabel(b)].join('\n'));
}

/** Поиск: все слова должны найтись; слово с # ищется среди тегов. */
export function filterItems(list, view) {
  const terms = norm(view.query).split(/\s+/).filter(Boolean);
  return list.filter((b) => {
    if (view.peerId && b.peerId !== view.peerId) return false;
    if (view.tag && !b.tags.includes(view.tag)) return false;
    if (!terms.length) return true;
    const hay = haystack(b);
    return terms.every((t) => (t.length > 1 && t.startsWith('#')
      ? b.tags.some((tag) => norm(tag).startsWith(t.slice(1)))
      : hay.includes(t)));
  });
}

/**
 * Новые сверху; с byChat — группы по чатам, выше тот чат, где закладка
 * свежее. sortBy: 'added' — по времени добавления, 'message' — по дате
 * сообщения.
 */
export function groupItems(list, byChat, sortBy) {
  const time = sortBy === 'message' ? (b) => (b.date ? b.date * 1000 : b.created) : (b) => b.created;
  const sorted = list.slice().sort((a, b) => time(b) - time(a) || b.id - a.id);
  if (!byChat) return [{ peerId: '', chatName: '', chatType: '', items: sorted }];
  const groups = new Map();
  for (const b of sorted) {
    let group = groups.get(b.peerId);
    if (!group) {
      group = { peerId: b.peerId, chatName: b.chatName, chatType: b.chatType, items: [] };
      groups.set(b.peerId, group);
    }
    group.items.push(b);
  }
  return Array.from(groups.values());
}

function chatTitle(peerId, fallback) {
  let peer = null;
  try {
    peer = dzg.chats.get(peerId);
  } catch (e) {
    peer = null;
  }
  if (peer && peer.isSelf) return 'Избранное';
  if (peer && peer.name) return peer.name;
  if (fallback) return fallback;
  const b = items.find((x) => x.peerId === peerId && x.chatName);
  return b ? b.chatName : 'Чат ' + peerId;
}

// --- Экспорт в Markdown ---------------------------------------------------

const mdLine = (text) => flat(text);

function linkOf(b) {
  if (b.chatType !== 'group' && b.chatType !== 'channel') return null;
  if (!dzg.messages.link) return null;
  try {
    return dzg.messages.link(b.peerId, b.msgId) || null;
  } catch (e) {
    return null;
  }
}

export function toMarkdown(list, nowMs) {
  const now = nowMs === undefined ? Date.now() : nowMs;
  const lines = ['# 🔖 Закладки', '', 'Экспорт из DamZaGram ' + fullDate(now) + ' · закладок: ' + list.length, ''];
  for (const group of groupItems(list, true, 'added')) {
    lines.push('## ' + chatIcon(group.chatType) + ' ' + mdLine(group.chatName || 'Чат ' + group.peerId) +
      ' (' + group.items.length + ')', '');
    for (const b of group.items) {
      lines.push('### ' + mdLine(author(b)) + ' · ' + (b.date ? fullDate(b.date * 1000) : 'дата неизвестна'), '');
      const text = String(b.text || '').trim();
      if (text) {
        for (const line of text.split('\n')) lines.push(line.trim() ? '> ' + line : '>');
        if (b.more) lines.push('> …');
        lines.push('');
      }
      const media = mediaLabel(b);
      if (media) lines.push('- Вложение: ' + media);
      if (b.note) lines.push('- Заметка: ' + mdLine(b.note));
      if (b.tags.length) lines.push('- Теги: ' + tagLine(b.tags));
      const link = linkOf(b);
      if (link) lines.push('- Ссылка: ' + link);
      if (b.created) lines.push('- Добавлено: ' + fullDate(b.created));
      if (b.deleted) lines.push('- Сообщение удалено, текст сохранён в закладке');
      lines.push('');
    }
  }
  return lines.join('\n');
}

function exportDir() {
  let dir = String(dzg.plugin.dataDir || dzg.plugin.dir || '');
  if (dir && !/[\\/]$/.test(dir)) dir += '/';
  return dir + 'bookmarks/';
}

/** Путь в file://-ссылку: "C:\\Users\\Я" → "file:///C:/Users/%D0%AF". */
export function fileUrl(path) {
  let p = String(path || '').replace(/\\/g, '/');
  if (!p.startsWith('/')) p = '/' + p;
  return 'file://' + encodeURI(p);
}

const dirName = (path) => String(path).replace(/[\\/][^\\/]*$/, '');

function openFolder() {
  const path = dzg.storage.get(EXPORT_KEY, '') || exportDir() + 'bookmarks.md';
  dzg.openUrl(fileUrl(dirName(path)));
}

/** Записать все закладки в Markdown. Возвращает путь или null. */
export function exportAll() {
  if (!items.length) {
    dzg.ui.toast('Закладок пока нет — экспортировать нечего', TITLE);
    return null;
  }
  const path = exportDir() + 'bookmarks.md';
  let ok = false;
  try {
    ok = dzg.fs.write(path, toMarkdown(items));
  } catch (e) {
    console.error('Экспорт закладок:', e);
  }
  if (!ok) {
    dzg.ui.toast('Не удалось записать ' + path, TITLE);
    return null;
  }
  dzg.storage.set(EXPORT_KEY, path);
  const inPanel = !!(panel && panel.isOpen());
  dzg.ui.toast('Сохранено закладок: ' + items.length + '\n' + path, '🔖 Экспорт в Markdown');
  if (setting('openFolderAfterExport', false)) {
    openFolder();
  } else if (!inPanel && dzg.ui.notify) {
    // В панели есть кнопка «Открыть папку»; из команды — по клику на уведомление.
    dzg.ui.notify({ title: '📂 Экспорт закладок готов', text: 'Нажми, чтобы открыть папку:\n' + dirName(path), onClick: openFolder, silent: true });
  }
  if (inPanel) renderList();
  return path;
}

// --- Панель ---------------------------------------------------------------

let panel = null;
let view = { query: '', tag: '', peerId: '', limit: 30 };
let rows = [];
let tagButtons = [];
let listTimer = 0;

const pageSize = () => Math.max(5, Math.min(200, Math.round(Number(setting('pageSize', 30)) || 30)));

const EMPTY = 'Закладок пока нет. Нажми правой кнопкой на любое сообщение → «🔖 В закладки». ' +
  'Или ответь на сообщение командой /bm add заметка #тег.';

function panelTitle(found, total) {
  const where = view.peerId ? ' · ' + ellipsis(chatTitle(view.peerId), 24) : '';
  return TITLE + where + ' (' + (found === total ? total : found + ' из ' + total) + ')';
}

const tagLabel = (t) => (view.tag === t.tag ? '✓ ' : '') + (t.tag ? '#' + t.tag + ' · ' + t.count : 'Все теги');

function pickTag(tag) {
  view.tag = view.tag === tag ? '' : tag;
  view.limit = pageSize();
  for (const t of tagButtons) {
    if (t.widget && t.widget.setText) t.widget.setText(tagLabel(t));
  }
  renderList();
}

function scheduleList() {
  if (!panel) return;
  clearTimeout(listTimer);
  listTimer = setTimeout(renderList, SEARCH_DELAY_MS);
}

function renderEntry(b, put, chatKnown) {
  const flags = (b.deleted ? ' · 🗑 удалено' : '') + (b.edited && !b.deleted ? ' · изменено' : '');
  const where = chatKnown ? '' : chatTitle(b.peerId, b.chatName) + ' — ';
  put(panel.addLabel(where + author(b) + ' · ' + formatDate(b.date || Math.floor(b.created / 1000)) + flags, { muted: true }));
  let body = previewOf(b, 220);
  if (b.note) body += '\n📝 ' + b.note;
  if (b.tags.length) body += '\n' + tagLine(b.tags);
  put(panel.addLabel(body));
  put(panel.addButton('↪ Открыть', () => openBookmark(b.id), { style: 'light' }));
  put(panel.addButton('📝 Изменить заметку', () => editNote(b.id), { style: 'light' }));
  put(panel.addButton('✕ Удалить', () => remove(b.id), { style: 'light' }));
}

// Список пересоздаётся целиком: панель — вертикальная колонка, удалённые
// виджеты уходят из неё сами, новые встают в конец (под поиском и тегами).
function renderList() {
  if (!panel) return;
  clearTimeout(listTimer);
  listTimer = 0;
  for (const w of rows) {
    if (w && w.destroy) w.destroy();
  }
  rows = [];
  const put = (w) => {
    if (w) rows.push(w);
    return w;
  };
  const scope = scopeItems();
  const base = view.peerId ? scope.filter((b) => b.peerId === view.peerId) : scope;
  const found = filterItems(base, view);
  panel.setTitle(panelTitle(found.length, base.length));

  if (lastRemoved) put(panel.addButton('↩ Вернуть закладку: ' + previewOf(lastRemoved, 50), undo, { style: 'light' }));
  if (!base.length) {
    put(panel.addLabel(view.peerId ? 'В этом чате закладок нет.' : EMPTY, { muted: true }));
  } else if (!found.length) {
    put(panel.addLabel('Ничего не нашлось' + (view.tag ? ' с тегом #' + view.tag : '') + '. Попробуй другой запрос.', { muted: true }));
  } else {
    const grouped = !view.peerId && !!setting('groupByChat', true);
    const groups = groupItems(found, grouped, String(setting('sortBy', 'added')));
    let shown = 0;
    for (const group of groups) {
      if (shown >= view.limit) break;
      if (grouped) {
        put(panel.addLabel(chatIcon(group.chatType) + ' ' + chatTitle(group.peerId, group.chatName) + ' · ' + group.items.length, { bold: true }));
      }
      for (const b of group.items) {
        if (shown >= view.limit) break;
        renderEntry(b, put, grouped || !!view.peerId);
        shown++;
      }
    }
    if (found.length > shown) {
      const more = Math.min(pageSize(), found.length - shown);
      put(panel.addButton('Показать ещё ' + more + ' (всего ' + found.length + ')', () => {
        view.limit += pageSize();
        renderList();
      }, { style: 'light' }));
    }
  }
  if (items.length > scope.length) {
    put(panel.addLabel('Ещё ' + (items.length - scope.length) + ' — в других аккаунтах. Переключись на аккаунт, чтобы увидеть их.', { muted: true }));
  }
  if (items.length) put(panel.addButton('⬇ Экспорт всех закладок в Markdown', () => exportAll(), { style: 'light' }));
  if (dzg.storage.get(EXPORT_KEY, '')) put(panel.addButton('📂 Открыть папку с экспортом', openFolder, { style: 'light' }));
}

function renderAll(focus) {
  if (!panel) return;
  clearTimeout(listTimer);
  listTimer = 0;
  panel.clear();
  rows = [];
  tagButtons = [];
  const input = panel.addInput({
    placeholder: 'Поиск: текст, заметка, чат, автор или #тег',
    value: view.query,
    onChange: (text) => {
      view.query = text;
      view.limit = pageSize();
      scheduleList();
    },
    onSubmit: (text) => {
      view.query = text;
      view.limit = pageSize();
      renderList();
    },
  });
  if (view.peerId) {
    panel.addButton('✕ Только «' + ellipsis(chatTitle(view.peerId), 40) + '» — показать все чаты', () => {
      view.peerId = '';
      view.tag = '';
      view.limit = pageSize();
      renderAll();
    }, { style: 'light' });
  }
  const base = scopeItems().filter((b) => !view.peerId || b.peerId === view.peerId);
  const tags = tagStats(base);
  if (view.tag && !tags.some((t) => t.tag === view.tag)) view.tag = '';
  if (tags.length) {
    let shown = tags.slice(0, TAG_BUTTONS);
    if (view.tag && !shown.some((t) => t.tag === view.tag)) {
      shown = shown.slice(0, TAG_BUTTONS - 1).concat(tags.filter((t) => t.tag === view.tag));
    }
    for (const t of [{ tag: '', count: base.length }].concat(shown)) {
      const entry = Object.assign({}, t);
      entry.widget = panel.addButton(tagLabel(entry), () => pickTag(entry.tag), { style: 'light' });
      tagButtons.push(entry);
    }
    if (tags.length > shown.length) {
      panel.addLabel('Ещё тегов: ' + (tags.length - shown.length) + '. Набери #тег в поиске.', { muted: true });
    }
  }
  panel.addSeparator();
  renderList();
  if (focus && input && input.setFocus) input.setFocus();
}

/** Открыть панель. options: { query, peerId } — поиск или только этот чат. */
export function show(options) {
  const o = options || {};
  view = { query: String(o.query || '').trim(), tag: '', peerId: o.peerId ? String(o.peerId) : '', limit: pageSize() };
  if (!dzg.ui.panel) {
    const list = groupItems(filterItems(scopeItems(), view), false, 'added')[0].items.slice(0, 15);
    dzg.ui.alert(list.map((b) => author(b) + ': ' + previewOf(b, 80) + (b.note ? ' — ' + b.note : '')).join('\n') || EMPTY, TITLE);
    return;
  }
  if (!panel || !panel.isOpen()) {
    const current = dzg.ui.panel({ title: TITLE, width: 400 });
    panel = current;
    current.on('close', () => {
      if (panel !== current) return;
      panel = null;
      rows = [];
      tagButtons = [];
      clearTimeout(listTimer);
      listTimer = 0;
      lastRemoved = null;
    });
  } else if (panel.show) {
    panel.show();
  }
  renderAll(true);
}

// --- Команды, меню, кнопки ------------------------------------------------

async function addByReply(chat, replyTo, text) {
  if (!chat || !replyTo) {
    dzg.ui.toast('Отправь «/bm add заметка #тег» ответом на сообщение или выбери «🔖 В закладки» в его меню.', TITLE);
    return;
  }
  let message = null;
  try {
    message = dzg.messages.get(chat, replyTo);
  } catch (e) {
    message = null;
  }
  if (!message && dzg.messages.fetch) {
    try {
      message = await dzg.messages.fetch(chat, replyTo);
    } catch (e) {
      message = null;
    }
  }
  if (!message) {
    message = { id: Number(replyTo), peerId: String(chat.id), chat, text: '', fromName: '', date: 0, out: false, service: false };
  }
  await bookmark(message, text ? splitNote(text) : null);
}

const HELP = [
  '/bm — все закладки: поиск, теги, группы по чатам',
  '/bm текст или /bm #тег — сразу с поиском',
  '/bm here — закладки этого чата',
  '/bm add заметка #тег — ответом на сообщение',
  '/bm export — сохранить всё в Markdown',
].join('\n');

async function command({ args, rest, chat, replyTo }) {
  const action = String(args[0] || '').toLowerCase();
  const tail = rest.slice(args[0] ? args[0].length : 0).trim();
  if (action === 'add' || action === '+') {
    await addByReply(chat, replyTo, tail);
  } else if (action === 'here' || action === 'тут' || action === 'здесь') {
    show({ peerId: chat ? chat.id : '' });
  } else if (action === 'export' || action === 'экспорт') {
    exportAll();
  } else if (action === 'help' || action === 'помощь') {
    dzg.ui.alert(HELP, TITLE);
  } else {
    show({ query: rest });
  }
}

dzg.commands.register('bm', {
  description: 'Закладки: /bm [поиск или #тег], /bm here, /bm add заметка #тег (ответом), /bm export',
  run: command,
});

dzg.commands.register('bookmarks', {
  description: 'Закладки — то же, что /bm',
  run: command,
});

dzg.ui.addMessageMenuItem({
  title: (message) => (find(message) ? '🔖 Убрать из закладок' : '🔖 В закладки'),
  when: canBookmark,
  onClick: (message) => (find(message) ? unbookmark(message) : bookmark(message)),
});

dzg.ui.addMessageMenuItem({
  title: '📝 Заметка к закладке…',
  when: (message) => canBookmark(message) && !!find(message),
  onClick: (message) => {
    const b = find(message);
    if (b) editNote(b.id);
  },
});

dzg.ui.addChatMenuItem({
  title: (chat) => '🔖 Закладки в этом чате (' + chatCount(chat.id) + ')',
  when: (chat) => !!chat && chatCount(chat.id) > 0,
  onClick: (chat) => show({ peerId: chat.id }),
});

if (dzg.ui.addMainMenuItem) {
  dzg.ui.addMainMenuItem({ title: 'Закладки', icon: 'saved', onClick: () => show() });
}

let removeTopBar = null;

function syncTopBar() {
  const want = !!setting('topBarButton', true) && !!dzg.ui.addTopBarButton;
  if (want && !removeTopBar) {
    removeTopBar = dzg.ui.addTopBarButton({
      icon: 'saved',
      tooltip: 'Закладки в этом чате',
      when: (chat) => !!chat && chatCount(chat.id) > 0,
      onClick: (chat) => show({ peerId: chat ? chat.id : '' }),
    });
  } else if (!want && removeTopBar) {
    removeTopBar();
    removeTopBar = null;
  }
}

dzg.on('settings.changed', ({ key }) => {
  if (key === 'topBarButton') syncTopBar();
  if (panel && panel.isOpen() && (key === 'groupByChat' || key === 'sortBy' || key === 'pageSize')) {
    view.limit = pageSize();
    renderList();
  }
});

// Другой аккаунт — другой набор закладок в панели.
dzg.on('session.ready', () => {
  if (panel && panel.isOpen()) renderAll();
});

dzg.on('unload', () => {
  if (dirty) save();
  clearTimeout(listTimer);
  if (removeTopBar) {
    removeTopBar();
    removeTopBar = null;
  }
  if (panel) {
    const current = panel;
    panel = null;
    current.close();
  }
});

load();
syncTopBar();
