// Антиреклама: скрывает рекламные посты фильтром сообщений.
// Маркировка (erid, «Реклама. ООО …»), хэштеги, свои слова и регулярки.
// Включается и выключается по чатам из меню чата, статистика — /antiads.
// Автор: @wetakush

const OVERRIDES_KEY = 'overrides'; // peerId -> true (фильтровать) | false (не трогать)
const STATS_KEY = 'stats';

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return (value === undefined || value === null) ? fallback : value;
};

export const BUILTIN = [
  /(?:^|[^\p{L}\p{N}])erid\s*[:=]?\s*[\w-]{6,}/iu,
  /#(?:реклама|рекламa|ad|ads|sponsored|промо|партн[её]р)(?![\p{L}\p{N}_])/iu,
  /на\s+правах\s+рекламы/iu,
  /(?:^|\n)\s*реклама\.?\s*(?:ооо|ип|ао|пао|зао|инн|огрн)/iu,
  /рекламодатель\s*[:—-]/iu,
  /партн[её]рский\s+(?:материал|пост)/iu,
  /\bsponsored\s+post\b/iu,
];

export const STRICT = [
  /по\s+промокоду/iu,
  /промокод\s*[:«"]/iu,
  /переходи(?:те)?\s+по\s+ссылке/iu,
  /подписывай(?:тесь|ся)\s+на\s+(?:канал|наш)/iu,
  /розыгрыш|разыгрываем/iu,
  /скидк[аиу]\s+(?:до\s+)?\d+\s*%/iu,
];

const escape = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

export function compileWords(text) {
  return String(text || '').split('\n').map((s) => s.trim()).filter(Boolean).map((line) => {
    const m = /^\/(.+)\/([a-z]*)$/.exec(line);
    try {
      if (m) return new RegExp(m[1], (m[2] || 'i').replace(/[gy]/g, ''));
      return new RegExp(escape(line), 'iu');
    } catch (e) {
      console.warn('Антиреклама: плохая регулярка', line);
      return null;
    }
  }).filter(Boolean);
}

// --- Состояние (в памяти: фильтр вызывается очень часто) ------------------

let patterns = [];
let onlyChannels = true;
let overrides = {};
let paused = false;
const seen = new Set();
let sessionHidden = 0;
let stats = { total: 0, byChat: {} };
let saveTimer = 0;

function compile() {
  onlyChannels = !!setting('onlyChannels', true);
  patterns = BUILTIN
    .concat(setting('strict', false) ? STRICT : [])
    .concat(compileWords(setting('words', '')));
}

function loadState() {
  overrides = Object.assign({}, dzg.storage.get(OVERRIDES_KEY, {}));
  stats = Object.assign({ total: 0, byChat: {} }, dzg.storage.get(STATS_KEY, {}));
}

compile();
loadState();

export function activeIn(chat) {
  if (!chat) return false;
  const override = overrides[chat.id];
  if (override === true || override === false) return override;
  return chat.type === 'channel' || !onlyChannels;
}

export function isAd(text) {
  return !!text && patterns.some((re) => re.test(text));
}

function count(message) {
  const key = message.peerId + ':' + message.id;
  if (seen.has(key)) return;
  seen.add(key);
  if (seen.size > 20000) seen.clear();
  sessionHidden++;
  stats.total++;
  const chat = stats.byChat[message.peerId] || { name: message.chat.name, count: 0 };
  chat.count++;
  chat.name = message.chat.name;
  stats.byChat[message.peerId] = chat;
  if (!saveTimer) {
    saveTimer = setTimeout(() => {
      saveTimer = 0;
      dzg.storage.set(STATS_KEY, stats);
      if (panel && panel.isOpen()) render();
    }, 2000);
  }
}

dzg.messages.addFilter((message) => {
  if (paused || message.out || message.service) return false;
  if (!activeIn(message.chat)) return false;
  const text = message.text || '';
  if (!isAd(text)) return false;
  count(message);
  return true;
});

function refilter() {
  dzg.messages.refilter();
}

dzg.on('settings.changed', () => {
  compile();
  refilter();
  if (panel && panel.isOpen()) render();
});

// --- Меню чата ------------------------------------------------------------

function setOverride(chat, value) {
  const defaultState = chat.type === 'channel' || !setting('onlyChannels', true);
  if (value === defaultState) delete overrides[chat.id];
  else overrides[chat.id] = value;
  dzg.storage.set(OVERRIDES_KEY, overrides);
  refilter();
}

dzg.ui.addChatMenuItem({
  title: (chat) => {
    if (!activeIn(chat)) return '🛡 Антиреклама: включить здесь';
    const hidden = stats.byChat[chat.id] ? stats.byChat[chat.id].count : 0;
    return '🛡 Антиреклама: выключить здесь' + (hidden ? ' (скрыто ' + hidden + ')' : '');
  },
  when: (chat) => chat.type === 'channel' || chat.type === 'group',
  onClick: (chat) => {
    const enable = !activeIn(chat);
    setOverride(chat, enable);
    dzg.ui.toast(enable ? 'Скрываю рекламу в «' + chat.name + '»' : 'Больше не трогаю «' + chat.name + '»', '🛡 Антиреклама');
  },
});

// --- Панель ---------------------------------------------------------------

let panel = null;

function render() {
  if (!panel) return;
  panel.clear();
  panel.addLabel('Скрыто за всё время: ' + stats.total + ' · за сеанс: ' + sessionHidden, { bold: true });
  panel.addLabel(paused ? '⏸ Пауза: реклама сейчас видна' : 'Фильтр работает' + (setting('strict', false) ? ' (строгий режим)' : ''), { muted: true });
  panel.addButton(paused ? 'Снова скрывать рекламу' : 'Показать скрытое', () => {
    paused = !paused;
    refilter();
    render();
  }, { style: 'light' });
  panel.addSeparator();
  panel.addToggle('Только каналы', !!setting('onlyChannels', true), (v) => dzg.settings.set('onlyChannels', !!v));
  panel.addToggle('Строгий режим', !!setting('strict', false), (v) => dzg.settings.set('strict', !!v));
  const top = Object.entries(stats.byChat).sort((a, b) => b[1].count - a[1].count).slice(0, 10);
  if (top.length) {
    panel.addSeparator();
    panel.addLabel('Больше всего рекламы', { bold: true });
    panel.addLabel(top.map(([, c], i) => (i + 1) + '. ' + c.name + ' — ' + c.count).join('\n'));
  }
  const off = Object.keys(overrides).filter((id) => overrides[id] === false);
  const on = Object.keys(overrides).filter((id) => overrides[id] === true);
  if (off.length || on.length) {
    panel.addSeparator();
    panel.addLabel('Исключения: выключено в ' + off.length + ', включено вручную в ' + on.length, { muted: true });
    panel.addButton('Сбросить исключения', () => {
      overrides = {};
      dzg.storage.set(OVERRIDES_KEY, overrides);
      refilter();
      render();
    }, { style: 'light' });
  }
  panel.addButton('Обнулить счётчик', () => {
    stats = { total: 0, byChat: {} };
    sessionHidden = 0;
    seen.clear();
    dzg.storage.set(STATS_KEY, stats);
    render();
  }, { style: 'light' });
}

export function show() {
  if (!dzg.ui.panel) {
    dzg.ui.alert('Скрыто постов: ' + stats.total + ' (за сеанс ' + sessionHidden + ')', 'Антиреклама');
    return;
  }
  if (!panel || !panel.isOpen()) {
    panel = dzg.ui.panel({ title: '🛡 Антиреклама', width: 340 });
    panel.on('close', () => { panel = null; });
  }
  render();
}

dzg.commands.register('antiads', {
  description: 'Антиреклама: счётчик, пауза, настройки',
  run: () => show(),
});

dzg.on('unload', () => {
  if (saveTimer) dzg.storage.set(STATS_KEY, stats);
  if (panel) panel.close();
});
