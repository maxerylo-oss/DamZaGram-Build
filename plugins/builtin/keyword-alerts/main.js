// Ключевые слова: уведомление, когда в любом чате пишут нужное слово.
// Слова — в настройке words (по одному в строке, /регулярка/флаги тоже
// можно). Управление: /keywords, /kw add слово, /kw del слово.
// Автор: @wetakush

const HITS_KEY = 'hits';
const EXCLUDED_KEY = 'excluded';
const MUTED_TTL = 60 * 1000;
const MAX_AGE_SECONDS = 10 * 60;

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return (value === undefined || value === null) ? fallback : value;
};

const escape = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

// --- Шаблоны --------------------------------------------------------------

export function wordList() {
  return String(setting('words', '') || '').split('\n').map((s) => s.trim()).filter(Boolean);
}

export function compile(lines, wholeWords) {
  const result = [];
  for (const line of lines) {
    const re = /^\/(.+)\/([a-z]*)$/.exec(line);
    try {
      if (re) {
        const flags = (re[2] || 'i').replace(/[gyu]/g, '');
        let compiled;
        try { compiled = new RegExp(re[1], flags + 'u'); } catch (e) { compiled = new RegExp(re[1], flags); }
        result.push({ label: line, re: compiled });
      } else {
        const body = escape(line);
        const source = wholeWords ? '(?<![\\p{L}\\p{N}_])' + body + '(?![\\p{L}\\p{N}_])' : body;
        result.push({ label: line, re: new RegExp(source, 'iu') });
      }
    } catch (e) {
      console.warn('Плохой шаблон «' + line + '»:', String(e));
    }
  }
  return result;
}

let patterns = [];
// Настройки читаются на каждое сообщение: держим их копию в памяти.
let cfg = {};

function rebuild() {
  cfg = {
    ignoreOwn: !!setting('ignoreOwn', true),
    onlyGroups: !!setting('onlyGroups', false),
    includeMuted: !!setting('includeMuted', false),
  };
  patterns = compile(wordList(), !!setting('wholeWords', true));
}

rebuild();

// --- Чаты без звука -------------------------------------------------------

let muted = new Set();
let mutedAt = 0;

function isMuted(peerId) {
  if (dzg.chats.entry) {
    const entry = dzg.chats.entry(String(peerId));
    return !!(entry && entry.muted);
  }
  if (!dzg.chats.list) return false;
  if (Date.now() - mutedAt > MUTED_TTL) {
    mutedAt = Date.now();
    try {
      muted = new Set((dzg.chats.list({ limit: 1000 }) || []).filter((c) => c.muted).map((c) => c.id));
    } catch (e) {
      muted = new Set();
    }
  }
  return muted.has(String(peerId));
}

function excluded() {
  const list = dzg.storage.get(EXCLUDED_KEY, []);
  return Array.isArray(list) ? list : [];
}

// --- Проверка сообщения ---------------------------------------------------

export function match(message) {
  if (!patterns.length || !message || message.service) return null;
  const text = String(message.text || '');
  if (!text) return null;
  const type = message.chat ? message.chat.type : '';
  if (cfg.ignoreOwn && message.out) return null;
  if (cfg.onlyGroups && type !== 'group' && type !== 'channel') return null;
  let hit = null;
  for (const p of patterns) {
    const m = p.re.exec(text);
    if (m) {
      hit = { pattern: p.label, found: m[0], index: m.index };
      break;
    }
  }
  if (!hit) return null;
  // Это вызывается на каждое сообщение: обращения к хранилищу и списку
  // чатов — только для совпавших.
  const peerId = String(message.peerId || (message.chat && message.chat.id) || '');
  if (excluded().includes(peerId)) return null;
  if (!cfg.includeMuted && isMuted(peerId)) return null;
  return hit;
}

export function snippet(text, index, length) {
  const flat = String(text).replace(/\s+/g, ' ');
  const start = Math.max(0, index - 60);
  const end = Math.min(flat.length, index + length + 80);
  return (start > 0 ? '…' : '') +
    flat.slice(start, index) + '«' + flat.slice(index, index + length) + '»' + flat.slice(index + length, end) +
    (end < flat.length ? '…' : '');
}

const lastNotified = new Map();

function onMessage(message) {
  if (message.date && Date.now() / 1000 - message.date > MAX_AGE_SECONDS) return;
  const hit = match(message);
  if (!hit) return;
  const peerId = String(message.peerId || message.chat.id);
  const entry = {
    pattern: hit.pattern,
    peerId,
    msgId: message.id,
    chatName: message.chat ? message.chat.name : '',
    fromName: message.fromName || '',
    text: snippet(message.text, hit.index, hit.found.length),
    at: Date.now(),
  };
  const hits = dzg.storage.get(HITS_KEY, []);
  dzg.storage.set(HITS_KEY, (Array.isArray(hits) ? hits : []).concat([entry]).slice(-100));
  if (panel && panel.isOpen()) render();

  const cooldown = Math.max(0, Number(setting('cooldown', 10)) || 0) * 1000;
  const last = lastNotified.get(peerId) || 0;
  if (Date.now() - last < cooldown) return;
  lastNotified.set(peerId, Date.now());

  const title = '🔔 «' + hit.found + '» · ' + entry.chatName;
  const text = (entry.fromName && entry.fromName !== entry.chatName ? entry.fromName + ': ' : '') + entry.text;
  const open = () => dzg.chats.open(peerId, message.id);
  if (setting('systemNotify', true) && dzg.ui.notify) {
    dzg.ui.notify({ title, text, onClick: open });
  } else {
    dzg.ui.toast(text, title);
  }
}

dzg.on('message.new', onMessage);

// --- Управление -----------------------------------------------------------

function setWords(list) {
  dzg.settings.set('words', list.join('\n'));
  rebuild();
  if (panel && panel.isOpen()) render();
}

export function addWord(word) {
  const value = String(word || '').trim();
  if (!value) return false;
  const list = wordList();
  if (list.some((w) => w.toLowerCase() === value.toLowerCase())) return false;
  list.push(value);
  setWords(list);
  return true;
}

export function removeWord(word) {
  const value = String(word || '').trim().toLowerCase();
  const list = wordList();
  const next = list.filter((w) => w.toLowerCase() !== value);
  if (next.length === list.length) return false;
  setWords(next);
  return true;
}

let panel = null;

function render() {
  if (!panel) return;
  panel.clear();
  const words = wordList();
  panel.addLabel(words.length ? 'Слежу за словами:' : 'Список пуст — добавь слово ниже.', { bold: true });
  for (const word of words) {
    panel.addButton('✕  ' + word, () => removeWord(word), { style: 'light' });
  }
  panel.addInput({
    placeholder: 'Новое слово или /регулярка/ и Enter',
    onSubmit: (value) => {
      if (!addWord(value)) dzg.ui.toast('Пусто или уже есть в списке');
    },
  });
  panel.addSeparator();
  panel.addToggle('Искать и в чатах без звука', !!setting('includeMuted', false), (v) => dzg.settings.set('includeMuted', !!v));
  panel.addToggle('Только группы и каналы', !!setting('onlyGroups', false), (v) => dzg.settings.set('onlyGroups', !!v));
  panel.addToggle('Только целые слова', !!setting('wholeWords', true), (v) => dzg.settings.set('wholeWords', !!v));
  const skipped = excluded();
  if (skipped.length) panel.addLabel('Исключено чатов: ' + skipped.length + ' (меню чата → «Ключевые слова: …»)', { muted: true });
  panel.addSeparator();
  const hits = dzg.storage.get(HITS_KEY, []).slice().reverse();
  panel.addLabel(hits.length ? 'Последние срабатывания' : 'Срабатываний пока не было', { bold: true });
  for (const hit of hits.slice(0, 20)) {
    const d = new Date(hit.at);
    const time = String(d.getHours()).padStart(2, '0') + ':' + String(d.getMinutes()).padStart(2, '0');
    panel.addLabel(time + ' · ' + hit.chatName + (hit.fromName ? ' — ' + hit.fromName : '') + '\n' + hit.text);
    panel.addButton('↪ открыть', () => dzg.chats.open(hit.peerId, hit.msgId), { style: 'light' });
  }
  if (hits.length) panel.addButton('Очистить журнал', () => { dzg.storage.set(HITS_KEY, []); render(); }, { style: 'light' });
}

export function show() {
  if (!dzg.ui.panel) {
    dzg.ui.alert('Слова: ' + (wordList().join(', ') || 'нет') + '\nДобавить: /kw add слово', 'Ключевые слова');
    return;
  }
  if (!panel || !panel.isOpen()) {
    panel = dzg.ui.panel({ title: '🔔 Ключевые слова', width: 360 });
    panel.on('close', () => { panel = null; });
  }
  render();
}

dzg.commands.register('keywords', {
  description: 'Ключевые слова: список, добавление, журнал',
  run: () => show(),
});

dzg.commands.register('kw', {
  description: '/kw add слово · /kw del слово · /kw list',
  run: ({ args, rest }) => {
    const action = (args[0] || '').toLowerCase();
    const value = rest.slice(args[0] ? args[0].length : 0).trim();
    if (action === 'add' || action === '+') {
      dzg.ui.toast(addWord(value) ? 'Слежу за «' + value + '»' : 'Пусто или уже есть в списке');
    } else if (action === 'del' || action === 'rm' || action === '-') {
      dzg.ui.toast(removeWord(value) ? 'Больше не слежу за «' + value + '»' : 'Такого слова нет');
    } else {
      show();
    }
  },
});

dzg.ui.addChatMenuItem({
  title: (chat) => (excluded().includes(String(chat.id))
    ? '🔔 Ключевые слова: снова искать здесь'
    : '🔕 Ключевые слова: не искать здесь'),
  when: () => wordList().length > 0,
  onClick: (chat) => {
    const id = String(chat.id);
    const list = excluded();
    const next = list.includes(id) ? list.filter((x) => x !== id) : list.concat([id]);
    dzg.storage.set(EXCLUDED_KEY, next);
    dzg.ui.toast(next.includes(id) ? 'Не ищу ключевые слова в «' + chat.name + '»' : 'Снова ищу в «' + chat.name + '»');
  },
});

dzg.on('settings.changed', ({ key }) => {
  rebuild();
  if (key === 'includeMuted') mutedAt = 0;
});

dzg.on('unload', () => {
  if (panel) panel.close();
});
