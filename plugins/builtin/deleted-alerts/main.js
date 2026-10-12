// Удалённые сообщения: уведомление о каждом удалении и журнал (/deleted).
// Удаления пачкой (например, «очистить историю») сворачиваются
// в одно уведомление.
// Автор: @wetakush

const BURST_MS = 1200;
// Поток удалений (очистка истории, чистка группы) может идти без пауз:
// уведомление всё равно приходит не позже чем через столько.
const MAX_WAIT_MS = 5000;
// Больше чатов в одной пачке — одно общее уведомление вместо россыпи.
const MAX_SEPARATE = 3;
// Журнал пишется на диск не чаще раза в столько, а не на каждое удаление.
const SAVE_MS = 1000;
const LOG_KEY = 'log';

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return (value === undefined || value === null || value === '') ? fallback : value;
};

const pad = (n) => String(n).padStart(2, '0');

function formatTime(ms) {
  const d = new Date(ms);
  const now = new Date();
  const time = pad(d.getHours()) + ':' + pad(d.getMinutes());
  return d.toDateString() === now.toDateString()
    ? time
    : pad(d.getDate()) + '.' + pad(d.getMonth() + 1) + ' ' + time;
}

const MEDIA = {
  photo: '🖼 фото', video: '🎬 видео', gif: 'GIF', sticker: 'стикер', voice: '🎤 голосовое',
  videoNote: '⏺ кружок', audio: '🎵 аудио', file: '📎 файл', poll: '📊 опрос', other: 'вложение',
};

export function describe(message) {
  const text = String(message.text || '').replace(/\s+/g, ' ').trim();
  const media = message.media ? (MEDIA[message.media] || message.media) : '';
  if (media && text) return media + ': ' + text;
  return media || text || '(пусто)';
}

const cut = (text, n) => (text.length > n ? text.slice(0, n - 1) + '…' : text);

export function shouldTrack(message) {
  if (!message || message.service) return false;
  // Одно чтение настроек на событие: в пачке их тысячи.
  const all = dzg.settings.all() || {};
  const on = (key, fallback) => {
    const value = all[key];
    return (value === undefined || value === null || value === '') ? fallback : !!value;
  };
  const type = message.chat && message.chat.type;
  if (on('ignoreOwn', true) && message.out) return false;
  if (on('ignoreBots', true) && type === 'bot') return false;
  if (type === 'channel' && !on('includeChannels', false)) return false;
  if (on('onlyPrivate', true) && type !== 'user' && type !== 'bot') return false;
  return true;
}

// --- Журнал ---------------------------------------------------------------

let unsaved = [];
let saveTimer = 0;

const logSize = () => Math.max(10, Math.min(5000, Number(setting('logSize', 300)) || 300));

function storedLog() {
  const log = dzg.storage.get(LOG_KEY, []);
  return Array.isArray(log) ? log : [];
}

function readLog() {
  return unsaved.length ? storedLog().concat(unsaved).slice(-logSize()) : storedLog();
}

function saveLog() {
  clearTimeout(saveTimer);
  saveTimer = 0;
  if (!unsaved.length) return;
  const log = storedLog().concat(unsaved).slice(-logSize());
  unsaved = [];
  dzg.storage.set(LOG_KEY, log);
}

// Пачка удалений — это сотни событий подряд: копим их в памяти и пишем
// журнал (и перерисовываем панель) раз в SAVE_MS.
function appendLog(entry) {
  unsaved.push(entry);
  // Больше, чем журнал может вместить (logSize до 5000), не держим.
  if (unsaved.length > 10000) unsaved = unsaved.slice(-5000);
  if (!saveTimer) {
    saveTimer = setTimeout(() => {
      saveLog();
      if (panel && panel.isOpen()) render();
    }, SAVE_MS);
  }
}

function entryOf(message) {
  return {
    id: message.id,
    peerId: message.peerId || (message.chat && message.chat.id),
    chatName: message.chat ? message.chat.name : '',
    chatType: message.chat ? message.chat.type : '',
    fromName: message.out ? 'Я' : (message.fromName || ''),
    text: cut(describe(message), 1000),
    date: message.date || 0,
    deletedAt: Date.now(),
  };
}

// --- Уведомления ----------------------------------------------------------

// peerId -> { count, preview: первые 3 записи, authors }: в памяти держим
// только то, что попадёт в уведомление, сколько бы ни удалили.
const pending = new Map();
let flushTimer = 0;
let firstPendingAt = 0;

function notify(title, text, onClick) {
  if (setting('systemNotify', true) && dzg.ui.notify) {
    dzg.ui.notify({ title, text, onClick, silent: !!setting('silent', true) });
  } else {
    dzg.ui.toast(text, title);
  }
}

function notifyChat(batch) {
  const first = batch.preview[0];
  const open = () => dzg.chats.open(first.peerId, first.id);
  if (batch.count === 1) {
    const where = first.chatType === 'user' || first.chatType === 'bot'
      ? first.fromName || first.chatName
      : first.fromName + ' в «' + first.chatName + '»';
    notify('🗑 ' + where + ' удалил(а) сообщение', cut(first.text, 300), open);
    return;
  }
  const authors = Array.from(batch.authors);
  const preview = batch.preview.map((e) => '• ' + cut(e.text, 80)).join('\n');
  notify(
    '🗑 Удалено ' + batch.count + ' сообщ. в «' + first.chatName + '»',
    (authors.length ? 'От: ' + authors.slice(0, 3).join(', ') + (authors.length > 3 ? '…' : '') + '\n' : '') + preview,
    open);
}

function flush() {
  clearTimeout(flushTimer);
  flushTimer = 0;
  firstPendingAt = 0;
  const batches = Array.from(pending.values());
  pending.clear();
  if (batches.length > MAX_SEPARATE) {
    const total = batches.reduce((sum, b) => sum + b.count, 0);
    const chats = batches.slice(0, 3).map((b) => '«' + b.preview[0].chatName + '» — ' + b.count).join('\n');
    notify(
      '🗑 Удалено ' + total + ' сообщ. в ' + batches.length + ' чатах',
      chats + (batches.length > 3 ? '\n…' : ''),
      () => show(null));
    return;
  }
  for (const batch of batches) notifyChat(batch);
}

dzg.on('message.deleted', (message) => {
  if (!shouldTrack(message)) return;
  const entry = entryOf(message);
  appendLog(entry);
  const key = String(entry.peerId);
  let batch = pending.get(key);
  if (!batch) {
    batch = { count: 0, preview: [], authors: new Set() };
    pending.set(key, batch);
  }
  batch.count++;
  if (batch.preview.length < 3) batch.preview.push(entry);
  if (entry.fromName && batch.authors.size < 10) batch.authors.add(entry.fromName);
  const now = Date.now();
  if (!firstPendingAt) firstPendingAt = now;
  clearTimeout(flushTimer);
  flushTimer = setTimeout(flush, Math.max(0, Math.min(BURST_MS, firstPendingAt + MAX_WAIT_MS - now)));
});

// --- Панель журнала -------------------------------------------------------

let panel = null;
let panelFilter = null; // peerId или null

function render() {
  if (!panel) return;
  panel.clear();
  const all = readLog();
  const list = (panelFilter ? all.filter((e) => String(e.peerId) === panelFilter) : all).slice().reverse();
  panel.setTitle(panelFilter && list.length ? '🗑 Удалённые: ' + list[0].chatName : '🗑 Удалённые сообщения');
  panel.addLabel(list.length
    ? 'В журнале ' + list.length + ' ' + (panelFilter ? 'из этого чата' : 'записей') + '. Новые сверху.'
    : 'Пока ничего не удаляли. Журнал пополняется сам.', { muted: true });
  if (panelFilter) panel.addButton('Показать все чаты', () => { panelFilter = null; render(); }, { style: 'light' });
  panel.addSeparator();
  for (const entry of list.slice(0, 50)) {
    const where = panelFilter || entry.chatType === 'user' ? '' : ' · ' + entry.chatName;
    panel.addLabel(formatTime(entry.deletedAt) + where + ' — ' + (entry.fromName || '?'), { bold: true });
    panel.addLabel(cut(entry.text, 400));
    panel.addButton('↪ открыть', () => dzg.chats.open(entry.peerId, entry.id), { style: 'light' });
  }
  if (list.length > 50) panel.addLabel('…и ещё ' + (list.length - 50) + '. Полный журнал — «Сохранить в файл».', { muted: true });
  panel.addSeparator();
  panel.addButton('Сохранить в файл', exportLog, { style: 'light' });
  panel.addButton('Очистить журнал', clearLog, { style: 'light' });
}

export function show(filterPeerId) {
  panelFilter = filterPeerId ? String(filterPeerId) : null;
  if (!dzg.ui.panel) {
    const list = readLog().slice(-10).reverse();
    dzg.ui.alert(list.map((e) => formatTime(e.deletedAt) + ' ' + e.fromName + ': ' + e.text).join('\n') || 'Журнал пуст', 'Удалённые');
    return;
  }
  if (!panel || !panel.isOpen()) {
    panel = dzg.ui.panel({ title: '🗑 Удалённые сообщения', width: 380 });
    panel.on('close', () => { panel = null; });
  }
  render();
}

function exportLog() {
  const lines = readLog().map((e) => {
    const sent = e.date ? new Date(e.date * 1000).toISOString() : '';
    return '[' + new Date(e.deletedAt).toISOString() + '] ' + e.chatName + ' — ' + e.fromName +
      (sent ? ' (отправлено ' + sent + ')' : '') + ':\n' + e.text + '\n';
  });
  const path = dzg.plugin.dataDir + 'deleted-alerts-log.txt';
  if (dzg.fs.write(path, lines.join('\n'))) {
    dzg.ui.toast('Журнал сохранён: ' + path);
  } else {
    dzg.ui.toast('Не удалось записать ' + path);
  }
}

async function clearLog() {
  const ok = await dzg.ui.confirm('Очистить журнал удалённых сообщений?', { confirmText: 'Очистить', cancelText: 'Отмена' });
  if (!ok) return;
  clearTimeout(saveTimer);
  saveTimer = 0;
  unsaved = [];
  dzg.storage.set(LOG_KEY, []);
  render();
}

dzg.commands.register('deleted', {
  description: 'Журнал удалённых сообщений (/deleted here — только этот чат)',
  run: ({ args, chat }) => {
    if (args[0] === 'clear') {
      clearLog();
    } else {
      show(args[0] === 'here' && chat ? chat.id : null);
    }
  },
});

dzg.ui.addChatMenuItem({
  title: (chat) => {
    const count = readLog().filter((e) => String(e.peerId) === String(chat.id)).length;
    return '🗑 Удалённые в этом чате' + (count ? ' (' + count + ')' : '');
  },
  when: (chat) => readLog().some((e) => String(e.peerId) === String(chat.id)),
  onClick: (chat) => show(chat.id),
});

dzg.on('unload', () => {
  saveLog();
  flush();
  if (panel) panel.close();
});
