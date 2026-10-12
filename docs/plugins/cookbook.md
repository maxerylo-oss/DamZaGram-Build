# Рецепты

Короткие готовые плагины. Каждый рецепт — целый `main.js`: создай плагин
мастером (**Настройки → Плагины → Создать плагин**), вставь код и сохрани.
Если рецепту нужны настройки, рядом есть кусок `manifest.json` для поля
`settings`. Всё написано только на настоящем API — каждый рецепт
загружается в песочнице `tools/damza/sandbox.py` и проходит проверку типов.

| # | Рецепт | Что использует |
|---|---|---|
| 1 | [Автоперевод исходящих](#автоперевод-исходящих) | `onBeforeSend`, `http`, меню чата |
| 2 | [Заглушить всех ботов](#заглушить-всех-ботов) | `chats.list`, `chats.mute` |
| 3 | [Удалённые сообщения в файл](#удалённые-сообщения-в-файл) | `message.deleted`, `fs` |
| 4 | [Экспорт чата в JSON](#экспорт-чата-в-json) | `messages.iterate`, `fs`, `sendFile` |
| 5 | [Команда /weather](#команда-weather) | `commands`, `http` |
| 6 | [Ежедневный дайджест](#ежедневный-дайджест) | таймеры, `chats.list`, `messages.send` |
| 7 | [Подсветка упоминаний](#подсветка-упоминаний) | `addRenderTransform` |
| 8 | [Панель быстрых ответов](#панель-быстрых-ответов) | `ui.panel`, `addTopBarButton`, `compose` |
| 9 | [Журнал «печатает…»](#журнал-печатает) | `chat.typing`, `storage` |
| 10 | [Автоответ «я занят»](#автоответ-я-занят) | `message.new`, настройки |
| 11 | [Сердечко на «спасибо»](#сердечко-на-спасибо) | `messages.react` |
| 12 | [Скрыть сообщения по стоп-словам](#скрыть-сообщения-по-стоп-словам) | `addFilter`, `settings.changed` |
| 13 | [Копировать как Markdown](#копировать-как-markdown) | меню сообщения, `format` |
| 14 | [Ссылка на сообщение одним кликом](#ссылка-на-сообщение-одним-кликом) | `messages.link`, `clipboard` |
| 15 | [Горячая клавиша «Избранное»](#горячая-клавиша-избранное) | `addShortcut` |
| 16 | [Следить, кто в сети](#следить-кто-в-сети) | `user.status`, `notify` |
| 17 | [Скачать все фото из чата](#скачать-все-фото-из-чата) | `search`, `download` |
| 18 | [Отложить сообщение: /later](#отложить-сообщение-later) | `scheduleDate` |
| 19 | [Топ авторов чата](#топ-авторов-чата) | `messages.history` |
| 20 | [Ночной режим по расписанию](#ночной-режим-по-расписанию) | `app.setNightMode` |
| 21 | [Свой акцентный цвет](#свой-акцентный-цвет) | `theme`, `settings.changed` |
| 22 | [Кнопка «прочитать всё»](#кнопка-прочитать-всё) | `addTopBarButton`, `markRead` |
| 23 | [Канал → Избранное по ключевым словам](#канал--избранное-по-ключевым-словам) | `message.new`, `forward` |
| 24 | [Кто онлайн в группе](#кто-онлайн-в-группе) | `api.call` |
| 25 | [Опрос командой /poll](#опрос-командой-poll) | `api.call`, `bytes` |
| 26 | [Предупреждение о длинном сообщении](#предупреждение-о-длинном-сообщении) | `compose.changed` |

## Автоперевод исходящих

Включи перевод для чата в его контекстном меню — всё, что ты пишешь в этот
чат, будет уходить на выбранном языке. Хук перед отправкой синхронный,
поэтому он отменяет исходную отправку, а переведённый текст плагин
отправляет сам.

```json
"settings": [
  { "key": "lang", "type": "select", "title": "Язык перевода", "default": "en",
    "options": [ { "value": "en", "title": "English" }, { "value": "de", "title": "Deutsch" }, { "value": "uk", "title": "Українська" } ] }
]
```

```js
const chats = new Set(dzg.storage.get('chats', []));
const save = () => dzg.storage.set('chats', [...chats]);

async function translate(text, lang) {
  const url = 'https://translate.googleapis.com/translate_a/single?client=gtx&sl=auto&dt=t'
    + `&tl=${lang}&q=${encodeURIComponent(text)}`;
  const response = await dzg.http.fetch(url, { timeout: 10_000 });
  if (!response.ok) throw new Error('HTTP ' + response.status);
  const data = await response.json();
  return data[0].map((part) => part[0]).join('');
}

dzg.ui.addChatMenuItem({
  title: (chat) => (chats.has(chat.id) ? 'Выключить автоперевод' : 'Включить автоперевод'),
  onClick(chat) {
    if (chats.has(chat.id)) chats.delete(chat.id);
    else chats.add(chat.id);
    save();
    dzg.ui.toast(chats.has(chat.id) ? 'Автоперевод включён' : 'Автоперевод выключен', chat.name);
  },
});

dzg.messages.onBeforeSend(({ text, chat, replyTo }) => {
  if (!chats.has(chat.id) || text.startsWith('/')) return;
  translate(text, String(dzg.settings.get('lang') || 'en'))
    .then((translated) => dzg.messages.send(chat, translated, { replyTo }))
    .catch((e) => {
      dzg.ui.toast('Перевод не удался, отправляю как есть: ' + e.message);
      dzg.messages.send(chat, text, { replyTo });
    });
  return false;
});
```

## Заглушить всех ботов

Команда `/mutebots` выключает уведомления во всех чатах с ботами.

```js
dzg.commands.register('mutebots', {
  description: 'Выключить уведомления у всех ботов',
  run() {
    const bots = dzg.chats.list({ type: 'bot', limit: 0 }).filter((c) => !c.muted);
    for (const bot of bots) dzg.chats.mute(bot);
    dzg.ui.toast(bots.length ? `Заглушено ботов: ${bots.length}` : 'Все боты уже без звука');
  },
});
```

## Удалённые сообщения в файл

Каждое удалённое сообщение дописывается строкой JSON в
`plugins_data/deleted-log/deleted.jsonl`. Пишем пачками раз в 5 секунд,
чтобы не трогать диск на каждое событие.

```js
const dir = dzg.plugin.dataDir + 'deleted-log/';
const file = dir + 'deleted.jsonl';
let buffer = [];
let timer = 0;

const flush = () => {
  timer = 0;
  if (!buffer.length) return;
  const old = dzg.fs.read(file) || '';
  dzg.fs.write(file, old + buffer.join('\n') + '\n');
  buffer = [];
};

dzg.on('message.deleted', (m) => {
  buffer.push(JSON.stringify({
    at: new Date().toISOString(),
    chat: m.chat.name,
    from: m.fromName,
    date: m.date,
    text: m.text,
    media: m.media || null,
  }));
  if (!timer) timer = setTimeout(flush, 5000);
});

dzg.on('unload', flush);

dzg.commands.register('deletedfile', () => dzg.ui.toast(file, 'Лог удалённых'));
```

## Экспорт чата в JSON

`/export 1000` выгружает последние 1000 сообщений текущего чата в JSON и
присылает файл в «Избранное».

```js
dzg.commands.register('export', {
  description: 'Экспорт чата в JSON: /export [сколько]',
  async run({ chat, args }) {
    const limit = Math.min(Number(args[0]) || 500, 20_000);
    dzg.ui.toast(`Выгружаю до ${limit} сообщений…`, chat.name);
    const messages = [];
    for await (const m of dzg.messages.iterate(chat, { limit })) {
      messages.push({
        id: m.id, date: m.date, from: m.fromName, text: m.text,
        media: m.media || null, replyTo: m.replyToId || null,
      });
    }
    messages.reverse();
    const safeName = chat.name.replace(/[^\p{L}\p{N}_-]+/gu, '_');
    const path = `${dzg.plugin.dataDir}export/${safeName}-${Date.now()}.json`;
    if (!dzg.fs.write(path, JSON.stringify({ chat, exported: Date.now(), messages }, null, 2))) {
      dzg.ui.toast('Не удалось записать файл');
      return;
    }
    const me = dzg.users.self();
    if (me) dzg.messages.sendFile(me, path, {
      caption: `Экспорт «${chat.name}»: ${messages.length} сообщений`,
      asDocument: true,
    });
  },
});
```

## Команда /weather

`/weather Москва` отправляет в чат погоду с wttr.in.

```js
dzg.commands.register('weather', {
  description: 'Погода: /weather город',
  async run({ chat, rest, replyTo }) {
    const city = rest || 'Moscow';
    try {
      const r = await dzg.http.fetch(`https://wttr.in/${encodeURIComponent(city)}?format=3&lang=ru`);
      const text = (await r.text()).trim();
      dzg.messages.send(chat, r.ok ? text : `Не нашёл «${city}»`, { replyTo });
    } catch (e) {
      dzg.ui.toast('wttr.in недоступен: ' + e.message);
    }
  },
});
```

## Ежедневный дайджест

Каждый день в 21:00 присылает в «Избранное» сводку непрочитанных чатов.

```json
"settings": [
  { "key": "hour", "type": "number", "title": "Час отправки", "default": 21, "min": 0, "max": 23, "integer": true }
]
```

```js
let timer = 0;

function digest() {
  const me = dzg.users.self();
  if (!me) return;
  const chats = dzg.chats.list({ unreadOnly: true, limit: 0 })
    .filter((c) => !c.muted && !c.isSelf)
    .sort((a, b) => b.mentions - a.mentions || b.unread - a.unread)
    .slice(0, 20);
  const lines = chats.map((c) => `• ${c.name}: ${c.unread}${c.mentions ? ` (упоминаний: ${c.mentions})` : ''}`);
  dzg.messages.send(me, lines.length
    ? `**Дайджест за день**\n${lines.join('\n')}`
    : '**Дайджест за день**\nВсё прочитано', { parse: 'markdown', silent: true });
}

function schedule() {
  clearTimeout(timer);
  const next = new Date();
  next.setHours(Number(dzg.settings.get('hour')) || 21, 0, 0, 0);
  if (next <= new Date()) next.setDate(next.getDate() + 1);
  timer = setTimeout(() => { digest(); schedule(); }, next.getTime() - Date.now());
}

schedule();
dzg.on('settings.changed', schedule);
dzg.commands.register('digestnow', digest);
```

## Подсветка упоминаний

Сообщения, где упомянули тебя по юзернейму или имени, получают метку 🔔
прямо в тексте. Само сообщение не меняется — только отображение.

```js
const me = dzg.users.self();
const needles = me
  ? [me.username && '@' + me.username.toLowerCase(), me.name.split(' ')[0].toLowerCase()].filter(Boolean)
  : [];

dzg.messages.addRenderTransform((m) => {
  if (m.out || !m.text || !needles.length) return null;
  const text = m.text.toLowerCase();
  return needles.some((n) => text.includes(n)) ? '🔔 ' + m.text : null;
});
```

## Панель быстрых ответов

Кнопка в шапке чата открывает панель с заготовками. Клик вставляет текст в
поле ввода, новые заготовки добавляются полем внизу.

```js
let replies = dzg.storage.get('replies', ['Скоро буду', 'Перезвоню позже', 'Спасибо!']);
let panel = null;

function build() {
  panel = dzg.ui.panel({ title: 'Быстрые ответы', width: 320 });
  panel.clear();
  panel.addLabel('Клик — вставить в поле ввода', { muted: true });
  for (const reply of replies) {
    panel.addButton(reply, () => dzg.ui.compose.insert(reply), { style: 'row', icon: 'reply' });
  }
  panel.addSeparator();
  panel.addInput({
    placeholder: 'Новая заготовка (Enter)',
    onSubmit(text) {
      if (!text.trim()) return;
      replies = [...replies, text.trim()];
      dzg.storage.set('replies', replies);
      build();
    },
  });
  panel.addButton('Удалить последнюю', () => {
    replies = replies.slice(0, -1);
    dzg.storage.set('replies', replies);
    build();
  }, { style: 'light' });
}

dzg.ui.addTopBarButton({
  icon: 'reply',
  tooltip: 'Быстрые ответы',
  onClick() {
    if (panel && panel.isOpen()) panel.close();
    else build();
  },
});
```

## Журнал «печатает…»

Записывает, кто и где начинал печатать или записывать голосовые. `/typinglog`
показывает последние 20 записей.

```js
const log = dzg.storage.get('typing', []);
let dirty = false;

/** @type {Record<string, string>} */
const names = {
  typing: 'печатает', record_voice: 'записывает голосовое', record_round: 'записывает кружок',
  upload_photo: 'отправляет фото', upload_document: 'отправляет файл', choose_sticker: 'выбирает стикер',
};

dzg.on('chat.typing', ({ chat, user, action }) => {
  if (action === 'cancel') return;
  const time = new Date().toTimeString().slice(0, 8);
  log.push(`${time} ${user.name} ${names[action] || action} в «${chat.name}»`);
  if (log.length > 300) log.splice(0, log.length - 300);
  dirty = true;
});

setInterval(() => {
  if (dirty) { dzg.storage.set('typing', log); dirty = false; }
}, 30_000);
dzg.on('unload', () => dzg.storage.set('typing', log));

dzg.commands.register('typinglog', () => {
  dzg.ui.alert(log.slice(-20).join('\n') || 'Пока пусто', 'Кто печатал');
});
```

## Автоответ «я занят»

Отвечает в личных чатах, пока включено, не чаще раза в час на человека.
Это минимальная версия: расписание, подстановку имени и исключения легко
добавить поверх.

```json
"settings": [
  { "key": "enabled", "type": "bool", "title": "Отвечать", "default": false },
  { "key": "text", "type": "text", "title": "Текст ответа", "default": "Я сейчас занят, отвечу позже." }
]
```

```js
const lastReply = new Map();
const HOUR = 3600 * 1000;

dzg.on('message.new', (m) => {
  if (!dzg.settings.get('enabled')) return;
  if (m.out || m.service || m.chat.type !== 'user' || m.chat.isSelf) return;
  if (m.selfId !== dzg.users.self()?.id) return; // ответ уйдёт от активного аккаунта
  const last = lastReply.get(m.peerId) || 0;
  if (Date.now() - last < HOUR) return;
  lastReply.set(m.peerId, Date.now());
  dzg.messages.send(m.chat, String(dzg.settings.get('text')), { replyTo: m.id });
});

dzg.commands.register('away', ({ args }) => {
  dzg.settings.set('enabled', args[0] !== 'off');
  dzg.ui.toast(dzg.settings.get('enabled') ? 'Автоответ включён' : 'Автоответ выключен');
});
```

## Сердечко на «спасибо»

```js
dzg.on('message.new', (m) => {
  if (m.out || m.service || !/спасибо|благодарю|thank/iu.test(m.text)) return;
  if (m.selfId !== dzg.users.self()?.id) return; // реакцию ставит активный аккаунт
  // Если в чате сердечко запрещено, промис отклонится с REACTION_INVALID
  dzg.messages.react(m.chat, m.id, '❤').catch((e) => console.warn('react:', e.message));
});
```

## Скрыть сообщения по стоп-словам

Сообщения со словами из настроек пропадают из ленты. Список меняется в
окне настроек — лента пересчитывается сразу.

```json
"settings": [
  { "key": "words", "type": "text", "title": "Стоп-слова", "default": "реклама\nскидка",
    "description": "По одному слову или фразе в строке" }
]
```

```js
let words = [];
const load = () => {
  words = String(dzg.settings.get('words') || '')
    .split('\n').map((w) => w.trim().toLowerCase()).filter(Boolean);
};
load();

dzg.messages.addFilter((m) => {
  if (m.out || !words.length) return false;
  const text = m.text.toLowerCase();
  return words.some((w) => text.includes(w));
});

dzg.on('settings.changed', ({ key }) => {
  if (key !== 'words') return;
  load();
  dzg.messages.refilter();
});
```

## Копировать как Markdown

```js
dzg.ui.addMessageMenuItem({
  title: 'Копировать как Markdown',
  when: (m) => m.text.length > 0,
  onClick(m) {
    dzg.clipboard.set(dzg.messages.format(m, 'markdown'));
    dzg.ui.toast('Скопировано с разметкой');
  },
});

dzg.ui.addMessageMenuItem({
  title: 'Копировать как HTML',
  when: (m) => m.entities.length > 0,
  onClick: (m) => dzg.clipboard.set(dzg.messages.format(m, 'html')),
});
```

## Ссылка на сообщение одним кликом

```js
dzg.ui.addMessageMenuItem({
  title: 'Скопировать ссылку (t.me)',
  when: (m) => m.chat.type === 'channel' || m.chat.type === 'group',
  onClick(m) {
    const link = dzg.messages.link(m.chat, m.id);
    if (!link) return dzg.ui.toast('У этого чата нет ссылок на сообщения');
    dzg.clipboard.set(link);
    dzg.ui.toast(link, 'Ссылка скопирована');
  },
});
```

## Горячая клавиша «Избранное»

```js
dzg.ui.addShortcut('Ctrl+Shift+S', () => {
  const me = dzg.users.self();
  if (me) dzg.chats.open(me);
});

dzg.ui.addShortcut('Ctrl+Shift+U', () => {
  const [first] = dzg.chats.list({ unreadOnly: true, limit: 1 });
  if (first) dzg.chats.open(first);
  else dzg.ui.toast('Непрочитанных нет');
});
```

## Следить, кто в сети

Пункт в меню чата «Следить за статусом» — и уведомление, когда человек
появится в сети.

```js
const watched = new Set(dzg.storage.get('watched', []));

dzg.ui.addChatMenuItem({
  title: (chat) => (watched.has(chat.id) ? 'Не следить за статусом' : 'Следить за статусом'),
  when: (chat) => chat.type === 'user' && !chat.isSelf,
  onClick(chat) {
    if (watched.has(chat.id)) watched.delete(chat.id);
    else watched.add(chat.id);
    dzg.storage.set('watched', [...watched]);
  },
});

dzg.on('user.status', ({ user, online }) => {
  if (!online || !watched.has(user.id)) return;
  dzg.ui.notify({
    title: 'В сети',
    text: `${user.name} сейчас онлайн`,
    onClick: () => dzg.chats.open(user),
  });
});
```

## Скачать все фото из чата

`/photos 50` скачивает последние 50 фото текущего чата в
`plugins_data/photos/<чат>/`.

```js
dzg.commands.register('photos', {
  description: 'Скачать фото чата: /photos [сколько]',
  async run({ chat, args }) {
    const limit = Math.min(Number(args[0]) || 30, 500);
    const found = await dzg.messages.search(chat, '', { filter: 'photo', limit });
    const dir = `${dzg.plugin.dataDir}photos/${chat.tgId}/`;
    let done = 0;
    for (const m of found) {
      try {
        await dzg.messages.download(chat, m.id, dir);
        done++;
      } catch (e) {
        console.warn(`#${m.id}: ${e.message}`);
      }
    }
    dzg.ui.notify({ title: 'Фото скачаны', text: `${done} из ${found.length} → ${dir}` });
  },
});
```

## Отложить сообщение: /later

`/later 30m Купить молоко` отправит текст в этот чат через 30 минут
(как отложенное сообщение Telegram — сработает, даже если клиент закрыт).

```js
const UNITS = { s: 1, m: 60, h: 3600, d: 86400 };

dzg.commands.register('later', {
  description: 'Отложить: /later 30m текст (s, m, h, d)',
  run({ chat, args, replyTo }) {
    const match = /^(\d+)([smhd])$/.exec(args[0] || '');
    const text = args.slice(1).join(' ');
    if (!match || !text) return dzg.ui.toast('Формат: /later 30m текст');
    const seconds = Number(match[1]) * UNITS[match[2]];
    const ok = dzg.messages.send(chat, text, {
      scheduleDate: Math.floor(Date.now() / 1000) + seconds,
      replyTo: replyTo || undefined,
    });
    dzg.ui.toast(ok ? `Отправлю через ${args[0]}` : 'Не получилось');
  },
});
```

## Топ авторов чата

```js
dzg.commands.register('top', {
  description: 'Топ авторов последних N сообщений: /top 1000',
  async run({ chat, args }) {
    const limit = Math.min(Number(args[0]) || 500, 5000);
    const history = await dzg.messages.history(chat, { limit });
    const counts = new Map();
    for (const m of history) {
      if (m.service) continue;
      counts.set(m.fromName, (counts.get(m.fromName) || 0) + 1);
    }
    const lines = [...counts].sort((a, b) => b[1] - a[1]).slice(0, 10)
      .map(([name, n], i) => `${i + 1}. ${name} — ${n} (${Math.round(n * 100 / history.length)}%)`);
    dzg.ui.alert(lines.join('\n') || 'Пусто', `Топ из ${history.length} сообщений`);
  },
});
```

## Ночной режим по расписанию

```json
"settings": [
  { "key": "from", "type": "number", "title": "Ночь с (час)", "default": 22, "min": 0, "max": 23, "integer": true },
  { "key": "to", "type": "number", "title": "Ночь до (час)", "default": 7, "min": 0, "max": 23, "integer": true }
]
```

```js
function check() {
  const hour = new Date().getHours();
  const from = Number(dzg.settings.get('from'));
  const to = Number(dzg.settings.get('to'));
  const night = from > to ? (hour >= from || hour < to) : (hour >= from && hour < to);
  if (dzg.app.isNightMode() !== night) dzg.app.setNightMode(night);
}

check();
setInterval(check, 60_000);
dzg.on('settings.changed', check);
```

## Свой акцентный цвет

Акцент из настроек красит кнопки, выделение и ссылки. После смены темы
или ночного режима клиент сам возвращает цвета плагина на новую палитру,
а при выгрузке плагина убирает их.

```json
"settings": [
  { "key": "accent", "type": "color", "title": "Акцент", "default": "#C8F135" }
]
```

```js
function apply() {
  const accent = String(dzg.settings.get('accent') || '#C8F135');
  dzg.theme.setMany({
    windowBgActive: accent,
    activeButtonBg: accent,
    dialogsBgActive: accent,
    checkboxFg: accent,
    sliderBgActive: accent,
    historyLinkOutFg: accent,
    dialogsUnreadBg: accent,
  });
}

apply();
dzg.on('settings.changed', ({ key }) => { if (key === 'accent') apply(); });
```

## Кнопка «прочитать всё»

Кнопка в шапке появляется только там, где есть непрочитанные.

```js
dzg.ui.addTopBarButton({
  icon: 'markRead',
  tooltip: 'Прочитать всё',
  when: (chat) => (dzg.chats.entry(chat)?.unread || 0) > 0,
  onClick(chat) {
    dzg.chats.markRead(chat);
    dzg.ui.refreshButtons();
  },
});

dzg.on('chat.unread', () => dzg.ui.refreshButtons());
```

## Канал → Избранное по ключевым словам

Посты каналов со словами из настроек пересылаются в «Избранное».

```json
"settings": [
  { "key": "keywords", "type": "string", "title": "Слова через запятую", "default": "вакансия, javascript" }
]
```

```js
dzg.on('message.new', async (m) => {
  if (!m.post || m.out) return;
  const words = String(dzg.settings.get('keywords') || '')
    .split(',').map((w) => w.trim().toLowerCase()).filter(Boolean);
  const text = m.text.toLowerCase();
  if (!words.some((w) => text.includes(w))) return;
  const me = dzg.users.self();
  if (!me || m.selfId !== me.id) return; // пост пришёл другому аккаунту
  try {
    await dzg.messages.forward(m.chat, m.id, me, { silent: true });
  } catch (e) {
    console.warn('forward:', e.message);
  }
});
```

## Кто онлайн в группе

Кнопка в шапке групп показывает, сколько участников сейчас в сети —
напрямую через Telegram API.

```js
dzg.ui.addTopBarButton({
  icon: 'users',
  tooltip: 'Сколько онлайн',
  when: (chat) => chat.type === 'group',
  async onClick(chat) {
    try {
      const { onlines } = await dzg.api.call('messages.getOnlines', { peer: chat });
      dzg.ui.toast(`Сейчас в сети: ${onlines}`, chat.name);
    } catch (e) {
      dzg.ui.toast(e.message);
    }
  },
});
```

## Опрос командой /poll

`/poll Обед? | Пицца | Суши | Шаурма` создаёт опрос в текущем чате.

```js
dzg.commands.register('poll', {
  description: 'Опрос: /poll вопрос | вариант | вариант',
  async run({ chat, rest }) {
    const [question, ...answers] = rest.split('|').map((s) => s.trim()).filter(Boolean);
    if (!question || answers.length < 2) return dzg.ui.toast('Формат: /poll вопрос | вариант | вариант');
    await dzg.api.call('messages.sendMedia', {
      peer: chat,
      message: '',
      media: {
        _: 'inputMediaPoll',
        poll: {
          _: 'poll',
          id: '0',
          question: { text: question, entities: [] },
          answers: answers.slice(0, 10).map((text, i) => ({
            _: 'pollAnswer',
            text: { text, entities: [] },
            option: dzg.api.toBase64(String(i)),
          })),
        },
      },
    });
  },
});
```

## Предупреждение о длинном сообщении

```js
let warned = false;

dzg.on('compose.changed', ({ text }) => {
  if (text.length > 4096 && !warned) {
    warned = true;
    dzg.ui.toast(`${text.length} символов — сообщение уйдёт несколькими частями`);
  } else if (text.length <= 4096) {
    warned = false;
  }
});
```

---

Есть идея рецепта? Напиши автору — [@wetakush](https://t.me/wetakush).
