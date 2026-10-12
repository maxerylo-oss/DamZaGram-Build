# 2. События

События — главный способ узнать, что происходит в клиенте. Подписка одна для
всего: `dzg.on(имя, обработчик)`.

```js
const off = dzg.on('message.new', (message) => {
  console.log(message.chat.name, '>', message.text);
});

off();                                  // отписаться
dzg.once('session.ready', (me) => {});  // только первое срабатывание
dzg.off('message.new', handler);        // отписать конкретную функцию
```

`on` и `once` возвращают функцию отписки. При выгрузке плагина все подписки
снимаются сами.

## Все события

| Событие | Данные | Когда приходит |
|---|---|---|
| `ready` | — | Плагин загружен (и после каждой горячей перезагрузки) |
| `unload` | — | Плагин выгружается: выключили, перезагрузили, обновили, клиент закрывается |
| `session.ready` | `Peer` (ты) | Аккаунт авторизован и его данные доступны |
| `message.new` | `Message` | Новое сообщение в любом чате — входящее или исходящее |
| `message.edited` | `Message` | Сообщение изменилось |
| `message.deleted` | `Message` | Сервер сообщил об удалении; сообщение ещё доступно |
| `message.reaction` | `Message` | Кто-то поставил реакцию на твоё сообщение |
| `chat.opened` | `Peer` | Пользователь открыл чат в главном окне |
| `chat.typing` | `{ chat, user, action, topicId }` | Кто-то печатает, записывает голосовое, выбирает стикер… |
| `chat.unread` | `{ chat, unread, unreadMark, mentions }` | Изменилось число непрочитанных в чате |
| `user.status` | `{ user, online, lastSeen, kind }` | Пользователь появился в сети или вышел |
| `settings.changed` | `{ key, value }` | Пользователь изменил настройку плагина |
| `theme.changed` | `{ night }` | Включили или выключили ночной режим |
| `app.focus` | `{ active }` | Окно клиента стало активным или неактивным |
| `compose.changed` | `{ chat, text }` | Изменился текст в поле ввода (не чаще раза в ~300 мс) |
| `plugins.changed` | `{ plugins }` | Плагин установили, удалили, включили, выключили или обновили |

Типы данных (`Peer`, `Message` и остальные) — в
[справочнике](../reference.md#события).

## Жизненный цикл: `ready` и `unload`

```js
let timer = 0;

dzg.on('ready', () => {
  timer = setInterval(checkSomething, 60_000);
  dzg.ui.toast('Плагин запущен', dzg.plugin.name);
});

dzg.on('unload', () => {
  // Таймеры и регистрации клиент уберёт сам. Здесь — только то,
  // что плагин поменял напрямую в чужом интерфейсе или теме.
  dzg.theme.reset();
});
```

`ready` приходит сразу после выполнения `main.js`. Если в `main.js` есть
top-level `await` долгой операции (например, сетевого запроса), `ready`
может прийти раньше, чем код после `await` выполнится, — регистрируй
обработчики до первого `await`.

## Аккаунт: `session.ready`

`session.ready` приходит, когда аккаунт вошёл и загрузился: при старте
клиента, при входе в новый аккаунт и при первом открытии добавленного
аккаунта. Плагин, который включили, когда клиент уже работал, этого
события не получит. Надёжный шаблон:

```js
const start = (me) => {
  console.log('Работаю от имени', me.name);
};

const me = dzg.users.self();
if (me) start(me);
else dzg.once('session.ready', start);
```

## Сообщения

### `message.new`

Приходит на **каждое** новое сообщение: входящие, твои собственные (в том
числе отправленные плагинами), сервисные («X вступил в группу»), во всех
аккаунтах, в которые выполнен вход. Отсюда два правила:

```js
dzg.on('message.new', (m) => {
  if (m.out || m.service) return;   // 1. не реагируй на свои и сервисные
  if (m.chat.type === 'channel') return;
  // 2. никогда не отвечай на всё подряд без условия — легко устроить петлю
  if (/^пинг$/i.test(m.text)) dzg.messages.send(m.chat, 'понг', { replyTo: m });
});
```

История, загруженная с сервера (`dzg.messages.history`, прокрутка чата
вверх), событий `message.new` не порождает — только действительно новые
сообщения.

### `message.edited`

Приходит, когда сообщение изменили: отредактировали текст, обновились
кнопки бота, медиа. Сравнить со старой версией можно, если сохранить её
заранее:

```js
const cache = new Map();
const key = (m) => m.peerId + ':' + m.id;

dzg.on('message.new', (m) => cache.set(key(m), m.text));
dzg.on('message.edited', (m) => {
  const before = cache.get(key(m));
  if (before !== undefined && before !== m.text) {
    console.log(`${m.fromName} исправил: «${before}» → «${m.text}»`);
  }
  cache.set(key(m), m.text);
});
```

### `message.deleted`

Сервер сообщил об удалении. Сообщение в этот момент ещё целое, а если
включено сохранение удалённых сообщений AyuGram, оно останется в чате с
пометкой. Поле `deleted` у таких сообщений потом равно `true`.

```js
dzg.on('message.deleted', (m) => {
  if (!m.out && m.text) dzg.ui.notify({ title: `Удалено в «${m.chat.name}»`, text: m.text });
});
```

### `message.reaction`

Кто-то поставил реакцию на **твоё** сообщение (появилась новая
непрочитанная реакция). Список реакций — в `message.reactions`.

```js
dzg.on('message.reaction', (m) => {
  const top = (m.reactions || []).map((r) => `${r.emoji}×${r.count}`).join(' ');
  dzg.ui.toast(`${top} на «${m.text.slice(0, 40)}»`, m.chat.name);
});
```

## Чаты и люди

### `chat.opened`

Пользователь открыл чат в главном окне. Удобно, чтобы показать что-то про
чат или подготовить данные заранее.

```js
dzg.on('chat.opened', async (chat) => {
  if (chat.type !== 'user') return;
  const info = await dzg.users.info(chat);
  if (info.commonChats > 0) dzg.ui.toast(`Общих групп: ${info.commonChats}`);
});
```

### `chat.typing`

```js
dzg.on('chat.typing', ({ chat, user, action }) => {
  if (action === 'cancel') return;
  console.log(`${user.name} в «${chat.name}»: ${action}`);
});
```

`action` — одно из `typing`, `record_voice`, `upload_photo`,
`choose_sticker` и других (`dzg.chats.typingActions`). Повторы одного
действия приходят не чаще раза в 4 секунды. Когда действие закончилось
(явно или по таймауту около 6,5 секунды), приходит `action: 'cancel'`.
Только для активного аккаунта.

### `chat.unread`

```js
dzg.on('chat.unread', ({ chat, unread, mentions }) => {
  if (mentions > 0) dzg.ui.toast(`Тебя упомянули в «${chat.name}» (${unread} непрочитанных)`);
});
```

Изменения собираются пачкой за 300 мс, одинаковые значения не повторяются.
При старте клиента приходит по событию на каждый чат с непрочитанными.

### `user.status`

```js
const watch = new Set(dzg.storage.get('watch', []));
dzg.on('user.status', ({ user, online }) => {
  if (online && watch.has(user.id)) dzg.ui.notify(`${user.name} в сети`);
});
```

`kind` уточняет статус: `online`, `offline`, `recently`, `lastWeek`,
`lastMonth`, `longAgo`, `hidden`. Боты пропускаются, одинаковые статусы
подряд отбрасываются. Первые 15 секунд после входа в аккаунт приходят только
изменения уже известных статусов — иначе загрузка списка чатов завалила бы
плагин событиями.

## Интерфейс и плагин

### `settings.changed`

Приходит на **каждый** изменённый ключ после того, как пользователь нажал
**Сохранить** в окне настроек плагина (или другой плагин вызвал
`dzg.plugins.setSetting`).

```js
dzg.on('settings.changed', ({ key, value }) => {
  if (key === 'highlight') dzg.messages.refreshRender();
});
```

### `theme.changed`, `app.focus`, `compose.changed`

```js
dzg.on('theme.changed', ({ night }) => applyAccent(night));

dzg.on('app.focus', ({ active }) => {
  if (active) dzg.ui.toast('С возвращением!');
});

dzg.on('compose.changed', ({ chat, text }) => {
  if (text.length > 4096) dzg.ui.toast('Сообщение будет разбито на части');
});
```

`compose.changed` приходит не чаще раза в ~300 мс и несёт последний текст.

### `plugins.changed`

Список плагинов изменился. Пригодится «магазину» или профилям плагинов, см.
[api/plugins.md](../api/plugins.md).

## Правила хорошего обработчика

- **Быстро.** Обработчик выполняется в главном потоке клиента. Один вызов
  не должен работать дольше 3 секунд — иначе его остановит сторожевой
  таймер. Тяжёлое — через `await` или порциями по таймеру.
- **Без исключений наружу.** Исключение в обработчике не сломает клиент и
  других подписчиков: оно попадёт в лог плагина (и тостом — в режиме
  разработчика). Но лучше ловить ожидаемые ошибки самому.
- **`async` можно.** Обработчик может быть `async`: отклонённый промис тоже
  попадёт в лог.
- **Не реагируй на себя.** Проверяй `message.out`, когда отвечаешь на
  сообщения.
- **Дебаунс.** Если на событие нужно делать дорогую работу, собирай
  события пачкой:

```js
let queue = [];
let scheduled = 0;
dzg.on('message.new', (m) => {
  queue.push(m);
  if (!scheduled) {
    scheduled = setTimeout(() => {
      const batch = queue;
      queue = [];
      scheduled = 0;
      console.log('За 2 секунды пришло', batch.length, 'сообщений');
    }, 2000);
  }
});
```

## Сырые обновления

Если нужно событие, которого нет в списке (например, изменение аватарки
или голосование в опросе), подпишись на сырые обновления сервера через
`dzg.api.onUpdate('updateMessagePoll', fn)` — см. [«Сырой MTProto»](08-raw-mtproto.md).

## Что дальше

- [Сообщения](03-messages.md) — что делать с `Message`, который пришёл в
  событии.
- [Команды и хуки](06-commands-and-hooks.md) — перехват отправки.
