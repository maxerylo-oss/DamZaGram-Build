// Командная палитра DamZaGram.
// Ctrl+Shift+K (или /palette) открывает панель с поиском по чатам, командам
// плагинов и действиям клиента. Префиксы: "@" — только чаты,
// ">" или "/" — только команды и действия.
// Автор: @wetakush

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return (value === undefined || value === null || value === '') ? fallback : value;
};

// --- Раскладка: "ghbdtn" ищется и как "привет" ---------------------------

const EN = "`qwertyuiop[]asdfghjkl;'zxcvbnm,./~QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>?";
const RU = 'ёйцукенгшщзхъфывапролджэячсмитьбю.ЁЙЦУКЕНГШЩЗХЪФЫВАПРОЛДЖЭЯЧСМИТЬБЮ,';
const EN_TO_RU = new Map();
const RU_TO_EN = new Map();
for (let i = 0; i < EN.length; ++i) {
  EN_TO_RU.set(EN[i], RU[i]);
  RU_TO_EN.set(RU[i], EN[i]);
}

export function switchLayout(text) {
  const latin = /[a-z]/i.test(text);
  const map = latin ? EN_TO_RU : RU_TO_EN;
  return Array.from(text, (ch) => map.get(ch) || ch).join('');
}

// --- Нечёткий поиск -------------------------------------------------------

const SEPARATOR = /[\s\-_@.,:;/()«»"'·]/;

export function fuzzyScore(query, target) {
  const q = query.toLowerCase().trim();
  const t = String(target || '').toLowerCase();
  if (!q) return 0;
  if (!t) return -1;
  const index = t.indexOf(q);
  if (index === 0) return 1000 - t.length * 0.5;
  if (index > 0) {
    return (SEPARATOR.test(t[index - 1]) ? 850 : 600) - index - t.length * 0.2;
  }
  let score = 300;
  let from = 0;
  let previous = -2;
  for (const ch of q) {
    if (ch === ' ') continue;
    const found = t.indexOf(ch, from);
    if (found < 0) return -1;
    if (found === previous + 1) score += 18;
    if (found === 0 || SEPARATOR.test(t[found - 1])) score += 12;
    score -= Math.min(found - from, 12);
    previous = found;
    from = found + 1;
  }
  return score - t.length * 0.2;
}

function bestScore(query, fields, alternative) {
  let best = -1;
  for (const field of fields) {
    if (!field) continue;
    best = Math.max(best, fuzzyScore(query, field));
    if (alternative !== query) best = Math.max(best, fuzzyScore(alternative, field) - 40);
  }
  return best;
}

// --- Частота использования ------------------------------------------------

const RECENT_KEY = 'recent';

function usageBoost(recent, key) {
  const entry = recent[key];
  if (!entry) return 0;
  const days = (Date.now() - entry.last) / 86400000;
  return Math.min(entry.count, 20) * 6 + Math.max(0, 60 - days * 10);
}

function remember(key) {
  const recent = dzg.storage.get(RECENT_KEY, {});
  const entry = recent[key] || { count: 0, last: 0 };
  recent[key] = { count: entry.count + 1, last: Date.now() };
  const keys = Object.keys(recent);
  if (keys.length > 200) {
    keys.sort((a, b) => recent[a].last - recent[b].last);
    for (const k of keys.slice(0, keys.length - 200)) delete recent[k];
  }
  dzg.storage.set(RECENT_KEY, recent);
}

// --- Источники ------------------------------------------------------------

const ICONS = { user: 'user', bot: 'bot', group: 'users', channel: 'channel', unknown: 'chat' };

function currentChat() {
  return dzg.chats.current ? dzg.chats.current() : null;
}

function selfPeer() {
  try { return dzg.users && dzg.users.self ? dzg.users.self() : null; } catch (e) { return null; }
}

function chatItems() {
  let list = [];
  try {
    list = (dzg.chats.list && dzg.chats.list({ limit: 500 })) || [];
  } catch (e) {
    console.warn('chats.list недоступен:', e);
  }
  const includeArchived = setting('includeArchived', true);
  return list
    .filter((chat) => includeArchived || !chat.archived)
    .map((chat, index) => {
      const saved = chat.isSelf;
      const parts = [];
      if (chat.username) parts.push('@' + chat.username);
      if (chat.unread) parts.push(chat.unread + ' непрочит.');
      if (chat.archived) parts.push('архив');
      if (chat.muted) parts.push('🔕');
      return {
        key: 'chat:' + chat.id,
        kind: 'chat',
        icon: saved ? 'saved' : (ICONS[chat.type] || 'chat'),
        title: saved ? 'Избранное' : chat.name,
        subtitle: parts.join(' · '),
        fields: [saved ? 'Избранное Saved Messages' : chat.name, chat.username],
        // Порядок списка чатов клиента — тоже сигнал.
        boost: Math.max(0, 40 - index * 0.2) + (chat.pinned ? 15 : 0) + (chat.unread ? 10 : 0),
        run: () => dzg.chats.open(chat.id),
      };
    });
}

function commandItems() {
  let list = [];
  try {
    list = (dzg.commands.all ? dzg.commands.all() : dzg.commands.list()) || [];
  } catch (e) {
    list = dzg.commands.list();
  }
  return list.map((command) => ({
    key: 'cmd:' + command.name,
    kind: 'command',
    icon: 'code',
    title: '/' + command.name,
    subtitle: [command.description, command.pluginName].filter(Boolean).join(' · '),
    fields: ['/' + command.name, command.name, command.description],
    boost: 0,
    run: () => runCommand(command),
  }));
}

function runCommand(command) {
  const text = '/' + command.name + ' ';
  if (dzg.ui.compose && dzg.ui.compose.set && dzg.ui.compose.set(text)) {
    dzg.ui.compose.focus();
    return;
  }
  if (dzg.commands.run && dzg.commands.run('/' + command.name)) return;
  dzg.ui.toast('Открой чат, чтобы выполнить /' + command.name);
}

function deepLink(url) {
  return () => {
    if (dzg.ui.openLink) dzg.ui.openLink(url);
    else dzg.openUrl(url);
  };
}

function chatEntry(chat) {
  if (!chat) return null;
  if (dzg.chats.entry) return dzg.chats.entry(chat.id);
  if (!dzg.chats.list) return null;
  return (dzg.chats.list({ limit: 500 }) || []).find((c) => c.id === chat.id) || null;
}

function actionItems() {
  const chat = currentChat();
  const night = dzg.app.isNightMode ? dzg.app.isNightMode() : false;
  const actions = [
    {
      id: 'night',
      title: night ? 'Выключить ночной режим' : 'Включить ночной режим',
      keywords: 'тема ночь день dark light night mode тёмная светлая',
      run: () => dzg.app.setNightMode(!night),
    },
    {
      id: 'saved',
      icon: 'saved',
      title: 'Избранное',
      keywords: 'saved messages сохранённые заметки',
      run: () => {
        const self = selfPeer();
        if (self) dzg.chats.open(self.id);
        else deepLink('tg://settings/saved-messages')();
      },
    },
    { id: 'settings', icon: 'settings', title: 'Настройки', keywords: 'settings параметры', run: deepLink('tg://settings') },
    { id: 'ayu', icon: 'ghost', title: 'Настройки DamZaGram / AyuGram', keywords: 'ayugram damzagram ayu settings призрак ghost', run: deepLink('tg://ayusettings') },
    { id: 'folders', icon: 'folder', title: 'Настройки: папки', keywords: 'folders папки фильтры', run: deepLink('tg://settings/folders') },
    { id: 'notifications', icon: 'bell', title: 'Настройки: уведомления', keywords: 'notifications звук уведомления', run: deepLink('tg://settings/notifications') },
    { id: 'privacy', icon: 'lock', title: 'Настройки: конфиденциальность', keywords: 'privacy безопасность приватность', run: deepLink('tg://settings/privacy') },
    { id: 'passcode', icon: 'lock', title: 'Код-пароль', keywords: 'passcode пароль блокировка lock', run: deepLink('tg://settings/privacy/passcode') },
    { id: 'devices', icon: 'devices', title: 'Активные сеансы', keywords: 'devices sessions устройства сеансы', run: deepLink('tg://settings/devices') },
    { id: 'appearance', icon: 'customize', title: 'Настройки: оформление', keywords: 'appearance chat settings оформление тема фон', run: deepLink('tg://settings/appearance') },
    { id: 'themes', icon: 'palette', title: 'Темы', keywords: 'themes темы палитра', run: deepLink('tg://settings/appearance/themes') },
    { id: 'wallpapers', icon: 'photo', title: 'Фон чата', keywords: 'wallpaper обои фон background', run: deepLink('tg://settings/appearance/wallpapers') },
    { id: 'language', icon: 'translate', title: 'Язык', keywords: 'language язык перевод translate', run: deepLink('tg://settings/language') },
    { id: 'storage', icon: 'storage', title: 'Память и кэш', keywords: 'storage cache данные кеш память очистить', run: deepLink('tg://settings/data/storage') },
    { id: 'proxy', icon: 'network', title: 'Прокси', keywords: 'proxy прокси socks mtproto', run: deepLink('tg://settings/data/proxy') },
    { id: 'advanced', icon: 'experimental', title: 'Продвинутые настройки', keywords: 'advanced продвинутые экспорт', run: deepLink('tg://settings/advanced') },
    { id: 'shortcuts', icon: 'shortcut', title: 'Горячие клавиши', keywords: 'shortcuts клавиши hotkeys', run: deepLink('tg://settings/appearance/shortcuts') },
    { id: 'contacts', icon: 'users', title: 'Контакты', keywords: 'contacts контакты люди', run: deepLink('tg://contacts') },
    { id: 'new-group', icon: 'addUser', title: 'Создать группу', keywords: 'new group создать группа', run: deepLink('tg://new/group') },
    { id: 'new-channel', icon: 'channel', title: 'Создать канал', keywords: 'new channel создать канал', run: deepLink('tg://new/channel') },
    { id: 'calls', icon: 'phone', title: 'Звонки', keywords: 'calls звонки', run: deepLink('tg://settings/calls') },
    {
      id: 'plugins-folder',
      icon: 'manage',
      title: 'Открыть папку плагинов',
      keywords: 'plugins плагины папка folder',
      run: () => dzg.openUrl('file:///' + String(dzg.app.pluginsPath || '').replace(/^\/+/, '')),
    },
    {
      id: 'restart',
      icon: 'restore',
      title: 'Перезапустить DamZaGram',
      keywords: 'restart перезапуск перезагрузка reload',
      run: async () => {
        if (await dzg.ui.confirm('Перезапустить клиент?', { confirmText: 'Перезапустить', cancelText: 'Отмена' })) {
          dzg.app.restart();
        }
      },
    },
    {
      id: 'author',
      icon: 'star',
      title: 'DamZaGram — автор @wetakush',
      keywords: 'about о программе автор author wetakush damzagram',
      run: deepLink('https://t.me/wetakush'),
    },
  ];
  if (chat) {
    const entry = chatEntry(chat);
    actions.push(
      {
        id: 'read',
        icon: 'markRead',
        title: 'Прочитать «' + chat.name + '»',
        keywords: 'mark read прочитать отметить прочитанным',
        run: () => dzg.messages.markRead(chat.id),
      },
      {
        id: 'mute',
        icon: entry && entry.muted ? 'unmute' : 'mute',
        title: (entry && entry.muted ? 'Включить уведомления «' : 'Заглушить на 8 часов «') + chat.name + '»',
        keywords: 'mute unmute звук заглушить уведомления',
        run: () => dzg.chats.mute(chat.id, entry && entry.muted ? false : 8 * 3600),
      },
      {
        id: 'archive',
        icon: entry && entry.archived ? 'unarchive' : 'archive',
        title: (entry && entry.archived ? 'Вернуть из архива «' : 'В архив «') + chat.name + '»',
        keywords: 'archive архив архивировать',
        run: () => dzg.chats.archive(chat.id, !(entry && entry.archived)),
      },
      {
        id: 'copy-link',
        icon: 'link',
        title: 'Скопировать ссылку на «' + chat.name + '»',
        keywords: 'link ссылка copy скопировать',
        run: () => {
          const link = chat.username
            ? 'https://t.me/' + chat.username
            : (chat.type === 'user' ? 'tg://user?id=' + chat.tgId : 'https://t.me/c/' + chat.tgId);
          dzg.clipboard.set(link);
          dzg.ui.toast('Скопировано: ' + link);
        },
      },
      {
        id: 'copy-id',
        icon: 'copy',
        title: 'Скопировать id «' + chat.name + '»',
        keywords: 'id copy айди идентификатор',
        run: () => {
          dzg.clipboard.set(String(chat.tgId));
          dzg.ui.toast('id ' + chat.tgId + ' скопирован');
        },
      },
    );
  }
  return actions.map((action) => ({
    key: 'act:' + action.id,
    kind: 'action',
    icon: action.icon || 'night',
    title: action.title,
    subtitle: '',
    fields: [action.title, action.keywords],
    boost: 0,
    run: action.run,
  }));
}

function pluginItems() {
  if (!dzg.plugins || !dzg.plugins.list) return [];
  const items = [{
    key: 'act:plugins-manager',
    kind: 'action',
    icon: 'manage',
    title: 'Менеджер плагинов',
    subtitle: 'Ctrl+Shift+P',
    fields: ['Менеджер плагинов', 'plugins manager плагины'],
    boost: 0,
    run: () => dzg.plugins.openManager(),
  }];
  if (dzg.plugins.openDevTools) {
    items.push({
      key: 'act:plugins-devtools',
      kind: 'action',
      icon: 'code',
      title: 'DevTools плагинов',
      subtitle: 'консоль, логи, перезагрузка',
      fields: ['DevTools плагинов', 'devtools console консоль отладка debug'],
      boost: 0,
      run: () => dzg.plugins.openDevTools(),
    });
  }
  for (const plugin of dzg.plugins.list()) {
    if (plugin.id === dzg.plugin.id) continue;
    items.push({
      key: 'plugin:' + plugin.id,
      kind: 'action',
      icon: plugin.enabled ? 'check' : 'plus',
      title: (plugin.enabled ? 'Выключить плагин «' : 'Включить плагин «') + plugin.name + '»',
      subtitle: plugin.builtin ? 'встроенный' : plugin.version,
      fields: [plugin.name, plugin.id + ' плагин plugin'],
      boost: 0,
      run: () => {
        dzg.plugins.setEnabled(plugin.id, !plugin.enabled);
        dzg.ui.toast((plugin.enabled ? 'Выключен: ' : 'Включён: ') + plugin.name);
      },
    });
  }
  return items;
}

// --- Поиск сообщений: "?текст" --------------------------------------------

const pad = (n) => String(n).padStart(2, '0');

function messageItems(messages) {
  return messages.slice().reverse().map((m) => {
    const d = new Date(m.date * 1000);
    const text = String(m.text || (m.media ? '[' + m.media + ']' : '')).replace(/\s+/g, ' ').trim();
    return {
      key: 'msg:' + m.peerId + ':' + m.id,
      kind: 'message',
      icon: 'search',
      title: (m.out ? 'Я' : (m.fromName || m.chat.name)) + ': ' + (text.length > 80 ? text.slice(0, 79) + '…' : text),
      subtitle: m.chat.name + ' · ' + pad(d.getDate()) + '.' + pad(d.getMonth() + 1),
      fields: [],
      boost: 0,
      run: () => dzg.chats.open(m.peerId, m.id),
    };
  });
}

// --- Поиск ----------------------------------------------------------------

export function search(query, sources) {
  let text = String(query || '').trim();
  let kinds = { chat: true, command: true, action: true };
  if (text.startsWith('@')) {
    kinds = { chat: true };
    text = text.slice(1);
  } else if (text.startsWith('>')) {
    kinds = { command: true, action: true };
    text = text.slice(1).trim();
  } else if (text.startsWith('/')) {
    kinds = { command: true };
  }
  const limit = Math.max(1, Math.min(30, Number(setting('maxResults', 10)) || 10));
  const items = [];
  if (kinds.chat && setting('showChats', true)) items.push(...sources.chats());
  if (kinds.command && setting('showCommands', true)) items.push(...sources.commands());
  if (kinds.action && setting('showActions', true)) items.push(...sources.actions(), ...(sources.plugins ? sources.plugins() : []));

  // Сотни элементов на каждое нажатие клавиши: хранилище читаем один раз.
  const recent = dzg.storage.get(RECENT_KEY, {}) || {};
  const alternative = switchLayout(text);
  const scored = [];
  for (const item of items) {
    const base = text ? bestScore(text, item.fields, alternative) : 0;
    if (base < 0) continue;
    const kindBias = text ? 0 : (item.kind === 'chat' ? 5 : 0);
    scored.push({ item, score: base + item.boost + usageBoost(recent, item.key) + kindBias });
  }
  scored.sort((a, b) => b.score - a.score);
  return scored.slice(0, limit).map((s) => s.item);
}

// --- Панель ---------------------------------------------------------------

let panel = null;
let results = [];
let rows = [];
let sources = null;

function label(item) {
  return item.subtitle ? item.title + '   ·   ' + item.subtitle : item.title;
}

// Строки результатов пересоздаются на каждый запрос: панель — это
// вертикальный список, удалённая строка уходит из него сама.
let searchTimer = 0;
let searchSeq = 0;

function show(items, emptyText) {
  results = items;
  for (const row of rows) {
    if (row && row.destroy) row.destroy();
  }
  rows = results.map((item, index) => panel.addButton(label(item), () => pick(index), { style: 'row', icon: item.icon }));
  if (!results.length) {
    rows.push(panel.addLabel(emptyText || 'Ничего не нашлось. Попробуй другую раскладку или префикс.', { muted: true }));
  }
}

function render(query) {
  if (!panel) return;
  clearTimeout(searchTimer);
  const text = String(query || '').trim();
  if (text.startsWith('?') && dzg.messages.search) {
    const q = text.slice(1).trim();
    const seq = ++searchSeq;
    if (q.length < 2) {
      show([], 'Поиск по сообщениям во всех чатах: ?слово');
      return;
    }
    show([], 'Ищу «' + q + '»…');
    searchTimer = setTimeout(async () => {
      try {
        const found = await dzg.messages.search(null, q, { limit: Math.max(5, Number(setting('maxResults', 10)) || 10) });
        if (panel && seq === searchSeq) show(messageItems(found || []), 'Сообщений с «' + q + '» не нашлось.');
      } catch (e) {
        if (panel && seq === searchSeq) show([], 'Поиск не удался: ' + (e && e.message ? e.message : e));
      }
    }, 350);
    return;
  }
  ++searchSeq;
  show(search(text, sources));
}

function pick(index) {
  const item = results[index];
  if (!item) return;
  remember(item.key);
  close();
  Promise.resolve().then(() => item.run());
}

function cached(fn) {
  let value = null;
  return () => (value || (value = fn()));
}

export function open() {
  if (panel && panel.isOpen()) {
    close();
    return;
  }
  if (!dzg.ui.panel) {
    dzg.ui.toast('Палитре нужен dzg.ui.panel — обнови DamZaGram');
    return;
  }
  // Снимок данных на время одного открытия палитры.
  sources = {
    chats: cached(chatItems),
    commands: cached(commandItems),
    actions: cached(actionItems),
    plugins: cached(pluginItems),
  };
  panel = dzg.ui.panel({ title: 'Командная палитра', width: 420 });
  panel.on('close', () => {
    panel = null;
    rows = [];
    results = [];
  });
  const input = panel.addInput({
    placeholder: 'Чат, /команда или действие…',
    onChange: (text) => render(text),
    onSubmit: () => pick(0),
  });
  panel.addLabel('Enter — первый результат · @ — чаты · > — действия · ? — поиск по сообщениям · ' + setting('shortcut', 'Ctrl+Shift+K') + ' — закрыть', { muted: true });
  panel.addSeparator();
  render('');
  if (input && input.setFocus) input.setFocus();
}

export function close() {
  if (panel) {
    const current = panel;
    panel = null;
    current.close();
  }
}

// --- Регистрация ----------------------------------------------------------

/**
 * Та же клавиша в русской раскладке: при ней клиент получает не K, а Л,
 * и Ctrl+Shift+K без второй привязки не сработал бы. "Ctrl+Shift+K" →
 * "Ctrl+Shift+Л"; null, если у клавиши нет кириллической пары.
 */
export function layoutAlternative(keys) {
  const m = /^(.*\+)?([^+])$/.exec(String(keys || '').trim());
  if (!m) return null;
  const mapped = EN_TO_RU.get(m[2].toLowerCase());
  if (!mapped || !/^[а-яё]$/i.test(mapped)) return null;
  return (m[1] || '') + mapped.toUpperCase();
}

let removeShortcuts = [];

function bindShortcut() {
  for (const remove of removeShortcuts) remove();
  removeShortcuts = [];
  if (!dzg.ui.addShortcut) return;
  const keys = String(setting('shortcut', 'Ctrl+Shift+K')).trim();
  if (!keys) return;
  try {
    removeShortcuts.push(dzg.ui.addShortcut(keys, open));
  } catch (e) {
    dzg.ui.toast('Не получилось назначить «' + keys + '»: ' + (e && e.message ? e.message : e), 'Командная палитра');
    return;
  }
  const alternative = layoutAlternative(keys);
  if (alternative) {
    try {
      removeShortcuts.push(dzg.ui.addShortcut(alternative, open));
    } catch (e) {
      // Не вышло — останется основная клавиша.
    }
  }
}

bindShortcut();

dzg.commands.register('palette', {
  description: 'Командная палитра: чаты, команды, действия',
  run: () => open(),
});

if (dzg.ui.addMainMenuItem) {
  dzg.ui.addMainMenuItem({ title: 'Командная палитра', icon: 'search', onClick: open });
}

dzg.on('settings.changed', ({ key }) => {
  if (key === 'shortcut') bindShortcut();
});

// Первый запуск: подсказать, что палитра есть.
dzg.on('ready', () => {
  if (dzg.storage.get('welcomed', false)) return;
  dzg.storage.set('welcomed', true);
  setTimeout(() => {
    dzg.ui.toast('Нажми ' + setting('shortcut', 'Ctrl+Shift+K') + ' — чаты, команды и настройки в одном окне', 'DamZaGram · командная палитра');
  }, 4000);
});

dzg.on('unload', () => {
  clearTimeout(searchTimer);
  close();
});
