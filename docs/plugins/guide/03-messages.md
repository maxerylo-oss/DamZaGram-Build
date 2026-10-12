# 3. Сообщения

`dzg.messages` умеет всё, что человек делает с сообщениями в клиенте, и
немного больше. В этом гайде — типичные задачи. Полное описание каждого
метода и опции — на странице модуля [api/messages.md](../api/messages.md).

Все методы работают с **активным** аккаунтом. Чат можно передать строкой
`id` или объектом `Peer`, сообщение — числом `id` или объектом `Message`.

## Объект `Message`

```js
const message = {
  id: 4512, peerId: '…', chat: { id: '…', name: 'DamZaGram chat', type: 'group' /* … */ },
  fromId: '…', fromName: 'Паша', text: 'привет **мир**', date: 1760252400,
  out: false, service: false, post: false, pinned: false, deleted: false,
  replyToId: 0, views: 0, media: 'photo',
  entities: [{ type: 'bold', offset: 7, length: 3 }],
  editDate: 0, groupedId: '', topicId: undefined,
  forwardFrom: undefined,             // есть у пересланных
  mediaInfo: { photoId: '…', width: 1280, height: 720 },
  buttons: undefined,                 // кнопки бота по строкам
  reactions: [{ emoji: '👍', count: 3, chosen: true }],
};
```

- `text` — текст без разметки, форматирование — в `entities` (смещения в
  UTF-16, как `String.length`).
- `date` и `editDate` — unix time в **секундах**: `new Date(m.date * 1000)`.
- `out` — твоё сообщение, `service` — сервисное, `post` — пост канала.
- `media` — тип вложения: `photo`, `video`, `gif`, `sticker`, `voice`,
  `videoNote`, `audio`, `file`, `poll`, `other`.

Все поля — в [справочнике](../reference.md#message).

## Где взять сообщения

| Нужно | Метод | Откуда |
|---|---|---|
| Реагировать на новые | `dzg.on('message.new', fn)` | Событие |
| Одно сообщение, которое клиент уже видел | `dzg.messages.get(chat, id)` | Память клиента, синхронно |
| Последние N в чате, без сети | `dzg.messages.loaded(chat, 50)` | Память клиента, синхронно |
| Историю с сервера | `await dzg.messages.history(chat, { limit: 500 })` | Сервер, от старых к новым |
| Всю историю по одному | `for await (const m of dzg.messages.iterate(chat))` | Сервер, от новых к старым |
| Найти по тексту или типу | `await dzg.messages.search(chat, 'текст', { filter: 'url' })` | Сервер |
| Найти во всех чатах | `await dzg.messages.search(null, 'DamZaGram')` | Сервер |
| Сообщение по id, даже незагруженное | `await dzg.messages.fetch(chat, id)` | Сервер |

```js
// Что было в чате за последние сутки
const dayAgo = Date.now() / 1000 - 86400;
const today = [];
for await (const m of dzg.messages.iterate(chat, { limit: 3000 })) {
  if (m.date < dayAgo) break;          // дальше — старее суток, хватит
  today.push(m);
}
today.reverse();                        // от старых к новым
```

Не гоняй историю огромных чатов без нужды: на частые запросы сервер отвечает
`FLOOD_WAIT_N`, и промис отклонится с этой ошибкой.

## Отправка

```js
const chat = dzg.chats.current();

dzg.messages.send(chat, 'Просто текст');
dzg.messages.send(chat, '**Жирный**, __курсив__, `код`, [ссылка](https://t.me/wetakush)', { parse: 'markdown' });
dzg.messages.send(chat, '<b>Важно:</b> <tg-spoiler>секрет</tg-spoiler>', { parse: 'html' });
```

`send` возвращает `true`, если сообщение принято к отправке (оно сразу
появляется в чате), и `false`, если чат не найден или текст пустой. Длинный
текст сам делится на части, ссылки и упоминания находятся автоматически.

Опции:

```js
dzg.messages.send(chat, 'Ответ', {
  replyTo: message,                 // id или Message
  silent: true,                     // без звука у получателей
  noWebpage: true,                  // без превью ссылки
  topicId: 15,                      // тема форума
  scheduleDate: new Date(Date.now() + 3600e3), // отложить на час
});
```

`scheduleDate` принимает `Date`, секунды или миллисекунды — сообщение
появится в «Отложенных».

### Разметка

`parse: 'markdown'` понимает синтаксис поля ввода клиента: `**жирный**`,
`__курсив__`, `~~зачёркнутый~~`, `||спойлер||`, `` `код` ``, блоки
```` ```js ````, `[текст](url)`, `> цитата`, упоминание
`[Паша](tg://user?id=123)` и премиум-эмодзи `![👍](tg://emoji?id=…)`.
`parse: 'html'` — теги Bot API (`<b>`, `<i>`, `<u>`, `<s>`, `<code>`,
`<pre>`, `<a href>`, `<tg-spoiler>`, `<blockquote expandable>`,
`<tg-emoji emoji-id>`). Подчёркивания в Markdown нет — только `<u>`.

Разобрать разметку без отправки и собрать обратно:

```js
const { text, entities } = dzg.messages.parse('**Hi** there', 'markdown');
// text: 'Hi there', entities: [{ type: 'bold', offset: 0, length: 2 }]

const html = dzg.messages.format(message, 'html');     // из Message
const md = dzg.messages.format(message, 'markdown');
```

Скопировать сообщение с форматированием в другой чат без пересылки:

```js
dzg.messages.send(target, message.text, { entities: message.entities });
```

### Хуки и команды при отправке из плагина

Сообщения из `dzg.messages.send` **не** проходят через `onBeforeSend` и
команды плагинов — так плагин не зациклится сам на себе. Если обработка
нужна, передай `{ runHooks: true }`.

## Правка и удаление

```js
dzg.messages.edit(chat, id, 'Новый текст');
dzg.messages.edit(chat, id, '~~старая цена~~ **новая цена**', { parse: 'markdown' });

dzg.messages.delete(chat, [id1, id2]);                // только у себя
dzg.messages.delete(chat, id, { revoke: true });     // у всех
```

`edit` и `delete` синхронно возвращают `true`, если запрос ушёл. Если сервер
отказал (например, `MESSAGE_EDIT_TIME_EXPIRED`), ошибка пишется в лог
плагина.

## Пересылка, реакции, закрепление

```js
await dzg.messages.forward(chat, [m.id], target);                       // переслать
await dzg.messages.forward(chat, ids, target, { dropAuthor: true });    // без автора
await dzg.messages.react(chat, m.id, '🔥');                              // реакция
await dzg.messages.react(chat, m.id, null);                             // убрать свои
const allowed = dzg.messages.reactions(chat, m.id);                     // что можно поставить
await dzg.messages.pin(chat, m.id, { silent: true });                   // закрепить
await dzg.messages.unpin(chat, m.id);
dzg.messages.markRead(chat);                                            // прочитать чат
```

`markRead` уважает режим призрака AyuGram: если отправка «прочитано»
выключена, сервер ничего не узнает.

## Файлы

```js
// Отправить: пути от папки плагина или абсолютные
dzg.messages.sendFile(chat, 'report.pdf', { caption: 'Отчёт за неделю' });
dzg.messages.sendFile(chat, ['a.jpg', 'b.jpg', 'c.jpg'], { caption: '**Альбом**', parse: 'markdown' });
dzg.messages.sendFile(chat, 'raw.png', { asDocument: true });   // без сжатия

// Скачать вложение и получить путь к файлу
const path = await dzg.messages.download(chat, m.id);                         // в загрузки клиента
const copy = await dzg.messages.download(chat, m.id, dzg.plugin.dataDir + 'media/'); // в папку
```

Подробности о вложении — в `m.mediaInfo`: имя файла, MIME, размер,
длительность, размеры картинки.

## Ссылки и кнопки ботов

```js
const url = dzg.messages.link(chat, m.id);   // 'https://t.me/channel/123' или null
```

Кнопки бота лежат в `m.buttons` — массив строк, в строке — кнопки слева
направо. Нажать можно по позиции или по тексту:

```js
const answer = await dzg.messages.clickButton(chat, m.id, 0, 1);   // строка 0, кнопка 1
const other = await dzg.messages.clickButton(chat, m.id, 'Подтвердить');
if (answer.answer) dzg.ui.toast(answer.answer, 'Бот ответил');
```

По умолчанию ответ бота не показывается пользователю — его получает плагин.
`{ open: true }` повторяет обычный клик: покажет ответ и откроет ссылку.

## Ошибки

Асинхронные методы отклоняют промис с `Error`, в `message` — код ошибки:
Telegram (`FLOOD_WAIT_10`, `CHAT_WRITE_FORBIDDEN`, `MESSAGE_ID_INVALID`) или
модуля (`PEER_NOT_FOUND`, `MESSAGE_NOT_FOUND`, `NO_MEDIA`,
`BUTTON_NOT_FOUND`).

```js
try {
  await dzg.messages.forward(chat, ids, target);
} catch (e) {
  const wait = /FLOOD_WAIT_(\d+)/.exec(e.message);
  if (wait) setTimeout(retry, Number(wait[1]) * 1000);
  else dzg.ui.toast('Не получилось: ' + e.message);
}
```

## Что дальше

- [Команды и хуки](06-commands-and-hooks.md) — перехват исходящих, фильтры
  ленты, подмена текста на экране.
- [api/messages.md](../api/messages.md) — все опции, разметка подробно,
  пример «архиватор канала».
- [Сырой MTProto](08-raw-mtproto.md) — если нужно то, чего нет в
  `dzg.messages` (опросы, медиа по ссылке, черновики…).
