# `dzg.messages` — сообщения: разметка, история, поиск, реакции, файлы, кнопки

Модуль расширяет базовые `dzg.messages.send / edit / delete / get / loaded`
всем, что делает человек с сообщениями в клиенте: отправка с Markdown и HTML,
отложенные сообщения, история и поиск на сервере, пересылка, реакции,
закрепление, прочитанность, отправка и скачивание файлов, ссылки на
сообщения и нажатие кнопок ботов.

Типы: [`types/messages.d.ts`](../types/messages.d.ts). Исходник модуля:
`Telegram/SourceFiles/damza/plugins/api/api_messages.cpp`, поля `Message` —
`damza/plugins/plugin_data.cpp`. Автор модуля — [@wetakush](https://t.me/wetakush).

Общие правила:

- Везде, где нужен чат, можно передать `Peer` или его `id` (строку). Вместо
  id сообщения можно передать сам объект `Message`.
- Синхронные методы не бросают исключений, если чат или сообщение не найдены:
  они возвращают `null`, `false` или `[]`.
- Асинхронные методы возвращают `Promise`. При ошибке он отклоняется с `Error`,
  в `message` — код ошибки Telegram (`FLOOD_WAIT_10`, `MESSAGE_ID_INVALID`,
  `CHAT_ADMIN_REQUIRED`...) или код модуля (`PEER_NOT_FOUND`,
  `MESSAGE_NOT_FOUND`, `NO_MEDIA`, `BUTTON_NOT_FOUND`...).
- Если нужного сообщения нет в памяти клиента, `react`, `forward`, `download`,
  `clickButton` и `fetch` сами загрузят его с сервера.
- Всё работает с **активным** аккаунтом.

## Шпаргалка

| Метод | Что делает | Результат |
|---|---|---|
| `send(peer, text, { parse, replyTo, silent, scheduleDate, noWebpage, topicId, entities, runHooks })` | Отправить текст | `boolean` |
| `edit(peer, id, text, { parse, noWebpage, entities })` | Изменить текст | `boolean` |
| `parse(text, mode = 'markdown')` | Разобрать Markdown / HTML | `{ text, entities }` |
| `format(message, mode = 'html')` | Собрать HTML / Markdown из сообщения | `string` |
| `history(peer, { limit, offsetId, addOffset, offsetDate, minId, maxId })` | История с сервера | `Promise<Message[]>` |
| `iterate(peer, { limit, batch, offsetId })` | Вся история страницами | `AsyncGenerator<Message>` |
| `search(peer \| null, query, { limit, from, filter, ... })` | Поиск в чате или везде | `Promise<Message[]>` |
| `fetch(peer, id \| ids)` | Сообщение с сервера по id | `Promise<Message \| null>` / `Promise<Message[]>` |
| `forward(from, ids, to, { silent, dropAuthor, dropCaptions, scheduleDate, topicId })` | Переслать | `Promise<void>` |
| `react(peer, id, emoji \| null)` | Поставить / снять реакцию | `Promise<void>` |
| `reactions(peer, id)` | Какие реакции доступны | `string[]` |
| `pin(peer, id, { silent, unpin, oneSide })` / `unpin(peer, id)` | Закрепить / открепить | `Promise<void>` |
| `markRead(peer, { tillId })` | Прочитать чат | `boolean` |
| `sendFile(peer, path \| paths, { caption, parse, asDocument, album, spoiler, ... })` | Отправить файлы | `boolean` |
| `download(peer, id, path?)` | Скачать файл / фото | `Promise<string>` (путь) |
| `link(peer, id, { thread, private })` | Ссылка t.me на сообщение | `string \| null` |
| `clickButton(peer, id, row, column \| text, { open })` | Нажать кнопку бота | `Promise<{ answer?, url?, alert? }>` |

Событие: `message.reaction` (см. [ниже](#события)).

## Новые поля `Message`

Все сообщения, которые отдаёт API (события `message.new`, `message.edited`,
`message.deleted`, `get`, `loaded`, `history`, `search`...), теперь содержат:

```ts
{
  // ...поля ядра: id, peerId, chat, fromId, fromName, text, date, out, ...
  entities: [{ type: 'bold', offset: 0, length: 5 }, ...], // форматирование
  editDate: 1700000000,      // 0 — не редактировалось
  groupedId: '1340...',      // id альбома, '' — не в альбоме
  forwardFrom: { name: 'Канал', peerId: '-100...', date: 1690000000, messageId: 42 },
  mediaInfo: { fileName: 'a.pdf', mime: 'application/pdf', size: 12345 },
  buttons: [[{ text: 'Купить', type: 'callback', kind: 'callback', data: 'buy:1' }]],
  reactions: [{ emoji: '👍', count: 3, chosen: true }],
  topicId: 1,                // только в форумах
}
```

- **`entities`** — форматирование текста. `offset` и `length` считаются в
  UTF-16, как `String.length` в JS, поэтому `text.slice(e.offset, e.offset + e.length)`
  даёт ровно отформатированный кусок. Типы: `bold`, `italic`, `underline`,
  `strikethrough`, `spoiler`, `code`, `pre` (+ `language`), `blockquote`
  (+ `collapsed`), `textLink` (+ `url`), `textMention` (+ `userId`),
  `customEmoji` (+ `documentId`), а также найденные клиентом `url`, `email`,
  `mention`, `hashtag`, `cashtag`, `botCommand`, `phone`, `bankCard`,
  `mediaTimestamp`.
- **`forwardFrom`** есть только у пересланных. `peerId` — если автор известен
  клиенту (у скрытых отправителей есть только `name`).
- **`mediaInfo`** — подробности о вложении: у файлов `fileName`, `mime`,
  `size`, у видео и аудио `duration` (секунды), у фото и видео `width`,
  `height`, плюс `documentId` / `photoId` и `spoiler`.
- **`buttons`** — кнопки бота по строкам. `type` — грубый тип (`url`,
  `callback`, `other`), `kind` — точный (`webView`, `switchInline`,
  `requestPhone`, `copyText`...). У `url` есть `url`, у `callback` — `data`.
- **`reactions`** — реакции под сообщением. `emoji` — сам эмодзи, id
  премиум-эмодзи строкой или `'paid'` для звёзд. `chosen` — твоя реакция.

```js
// Ссылки из сообщения, включая скрытые под текстом
const links = (m) => m.entities
  .filter((e) => e.type === 'url' || e.type === 'textLink')
  .map((e) => e.url || m.text.slice(e.offset, e.offset + e.length));
```

## Отправка и редактирование

### `dzg.messages.send(peer, text, options?)`

Отправляет текст тем же путём, что и поле ввода: сообщение сразу появляется
в чате, длинный текст делится на части, ссылки, упоминания и хэштеги
находятся автоматически. Возвращает `true`, если отправка началась, и
`false`, если чат не найден или текст пустой.

| Опция | По умолчанию | Описание |
|---|---|---|
| `parse` | `'none'` | `'markdown'` (или `'md'`), `'html'`, `'none'` — см. [разметку](#разметка) |
| `replyTo` | — | Ответ: id сообщения или `Message` |
| `topicId` | — | Тема форума |
| `silent` | `false` | Без звука у получателей |
| `scheduleDate` | — | Отложить: unix time в секундах, миллисекунды или `Date`. Дата в прошлом — отправить сразу |
| `noWebpage` | `false` | Не прикреплять превью ссылки |
| `entities` | — | Готовое форматирование (`MessageEntity[]`), добавляется к результату `parse` |
| `runHooks` | `false` | Пропустить через `onBeforeSend` и команды плагинов (если хук поменяет текст, форматирование сбросится) |

```js
const chat = dzg.chats.current();
dzg.messages.send(chat, '**Готово!** Отчёт: [ссылка](https://example.com)', { parse: 'markdown' });
dzg.messages.send(chat, '<b>Важно</b> <tg-spoiler>секрет</tg-spoiler>', { parse: 'html', silent: true });

// Напомнить себе в «Избранном» завтра в 9:00
const at = new Date();
at.setDate(at.getDate() + 1);
at.setHours(9, 0, 0, 0);
dzg.messages.send(dzg.users.self(), 'Позвонить маме', { scheduleDate: at });
```

Форматирование полученного сообщения переносится как есть — так можно
скопировать сообщение с жирным, ссылками и премиум-эмодзи без пересылки:

```js
dzg.messages.send(target, m.text, { entities: m.entities });
```

### `dzg.messages.edit(peer, id, text, options?)`

Меняет текст своего сообщения (или подпись к медиа). Понимает те же `parse`,
`entities` и `noWebpage`. Спойлер на медиа сохраняется. Возвращает `false`,
если сообщение не найдено; ошибку сервера (`MESSAGE_NOT_MODIFIED`,
`MESSAGE_EDIT_TIME_EXPIRED`) пишет в лог плагина.

```js
dzg.messages.edit(chat, 1234, '~~старая цена~~ **новая цена**', { parse: 'md' });
```

## Разметка

### Markdown

Синтаксис как в поле ввода клиента плюс несколько вещей из Bot API:

| Пишешь | Получаешь |
|---|---|
| `**жирный**` | **жирный** |
| `__курсив__` | _курсив_ |
| `~~зачёркнутый~~` | ~~зачёркнутый~~ |
| `\|\|спойлер\|\|` | спойлер |
| `` `код` `` | `код` |
| ` ```js` ... ` ``` ` | блок кода с языком |
| `[текст](https://site)` | ссылка |
| `[Паша](tg://user?id=123)` | упоминание по id (пользователь должен быть известен клиенту) |
| `![👍](tg://emoji?id=5368324170671202286)` | премиум-эмодзи |
| `> строка` | цитата (несколько строк подряд — одна цитата) |
| `**>` первая строка … последняя`\|\|` | сворачиваемая цитата |
| `\*` | экранирование: `\` перед `` \ ` * _ ~ \| [ ] ( ) > ! # + - = . { } `` |

Форматирование можно вкладывать: `**жирный __и курсив__**`. Внутри обычных
ссылок вида `https://...` разметка не срабатывает, поэтому
`http://x.com/__init__.py` останется как есть. Подчёркивания в Markdown нет
(как и в клиенте) — используй HTML `<u>`.

### HTML

Как в Bot API: `<b>`, `<strong>`, `<i>`, `<em>`, `<u>`, `<ins>`, `<s>`,
`<strike>`, `<del>`, `<code>`, `<pre>`, `<pre><code class="language-js">`,
`<a href="...">`, `<a href="tg://user?id=123">`, `<tg-spoiler>`,
`<span class="tg-spoiler">`, `<blockquote>`, `<blockquote expandable>`,
`<tg-emoji emoji-id="...">`, `<br>`. Сущности `&lt; &gt; &amp; &quot; &#123; &#x1F600;`
раскрываются. Неизвестные теги убираются, их текст остаётся.

### `dzg.messages.parse(text, mode = 'markdown')`

Разбирает разметку без отправки и возвращает `{ text, entities }` — удобно,
чтобы проверить результат или собрать сообщение по частям.

```js
const { text, entities } = dzg.messages.parse('**Hi** <i>there</i>', 'markdown');
// text: 'Hi <i>there</i>', entities: [{ type: 'bold', offset: 0, length: 2 }]
```

### `dzg.messages.format(message, mode = 'html')`

Обратная операция: собирает HTML или Markdown из `Message` (или из
`{ text, entities }`, или из `(text, mode, entities)`). Пересекающиеся
сущности корректно разбиваются, HTML экранируется.

```js
// Скопировать сообщение с форматированием в Markdown
dzg.ui.addMessageMenuItem({
  title: 'Копировать как Markdown',
  onClick: (m) => dzg.clipboard.set(dzg.messages.format(m, 'markdown')),
});
```

## История и поиск

### `dzg.messages.history(peer, options?)`

Запрашивает историю у сервера (а не только то, что клиент держит в памяти,
как `loaded`). Сообщения попадают в данные клиента, поэтому потом доступны и
через `get`. Результат — **от старых к новым**.

| Опция | По умолчанию | Описание |
|---|---|---|
| `limit` | `50` | Сколько сообщений. Больше 100 — модуль сам догрузит страницами |
| `offsetId` | `0` | Брать сообщения старше этого id (0 — с самого нового) |
| `addOffset` | `0` | Сдвиг от `offsetId`. Отрицательный — взять более новые (не больше 100) |
| `offsetDate` | — | Брать сообщения старше этой даты (секунды или `Date`) |
| `minId`, `maxId` | — | Только сообщения с id в этих пределах |

```js
// Последние 300 сообщений и самые активные авторы
const list = await dzg.messages.history(chat, { limit: 300 });
const top = {};
for (const m of list) top[m.fromName] = (top[m.fromName] || 0) + 1;
dzg.log(Object.entries(top).sort((a, b) => b[1] - a[1]).slice(0, 5));
```

### `dzg.messages.iterate(peer, options?)`

Асинхронный генератор по всей истории — **от новых к старым**, страницами
по `batch` (до 100). Останавливается, когда история кончилась или набрано
`limit`. Можно выйти из цикла через `break` — лишних запросов не будет.

```js
let photos = 0;
for await (const m of dzg.messages.iterate(chat, { limit: 5000 })) {
  if (m.media === 'photo') photos++;
  if (m.date < Date.now() / 1000 - 7 * 86400) break; // только за неделю
}
dzg.ui.toast(`Фото за неделю: ${photos}`);
```

Не гоняй историю огромных чатов без нужды: на частые запросы сервер
отвечает `FLOOD_WAIT_N`.

### `dzg.messages.search(peer | null, query, options?)`

Поиск на сервере. С `peer` — внутри чата, с `null` — по всем чатам (как
поиск в списке чатов). Результат — от старых к новым; больше 100 —
догрузит страницами.

| Опция | Описание |
|---|---|
| `limit` | По умолчанию 50 |
| `from` | Только от этого отправителя (в «Избранном» — сохранённые из этого чата) |
| `filter` | `'photo'`, `'video'`, `'photoVideo'`, `'document'`, `'url'`, `'gif'`, `'voice'`, `'music'`, `'roundVideo'`, `'roundVoice'`, `'mentions'`, `'pinned'`, `'geo'`, `'contacts'`, `'chatPhotos'`, `'poll'` |
| `topicId` | Тема форума |
| `minDate`, `maxDate` | Период (секунды или `Date`) |
| `offsetId`, `addOffset` | Ручное листание |
| `folder` | Только глобальный: `0` — основной список, `1` — архив |
| `type` | Только глобальный: `'channels'`, `'groups'` или `'users'` |

Пустой `query` с фильтром работает (например, все ссылки чата). Пустой
`query` без фильтра сервер отклоняет (`SEARCH_QUERY_EMPTY`).

```js
// Все ссылки, которые я присылал в этот чат
const found = await dzg.messages.search(chat, '', { filter: 'url', from: dzg.users.self(), limit: 200 });

// Где во всех каналах упоминали DamZaGram за последние сутки
const day = await dzg.messages.search(null, 'DamZaGram', {
  type: 'channels',
  minDate: new Date(Date.now() - 86400e3),
});
```

### `dzg.messages.fetch(peer, id | ids)`

Загружает сообщения по id с сервера, если клиент их ещё не знает. С одним id
возвращает `Message | null`, с массивом — массив найденных.

```js
const original = await dzg.messages.fetch(chat, m.replyToId);
```

## Действия с сообщениями

### `dzg.messages.forward(fromPeer, ids, toPeer, options?)`

Пересылает одно или несколько сообщений через ту же логику, что и кнопка
«Переслать», включая пересылку защищённого контента средствами AyuGram.
Промис выполняется, когда сервер принял пересылку.

| Опция | Описание |
|---|---|
| `dropAuthor` | Без указания автора (как «Скрыть имя отправителя») |
| `dropCaptions` | Без автора и без подписей к медиа |
| `silent` | Без звука |
| `scheduleDate` | Отложить |
| `topicId` | Тема форума в чате назначения |

```js
await dzg.messages.forward(chat, [m.id], dzg.chats.find('mychannel'), { dropAuthor: true });
```

### `dzg.messages.react(peer, id, emoji, options?)`

Ставит реакцию так же, как клик по ней: сразу видна в клиенте и уходит на
сервер. `emoji` — обычный эмодзи (`'👍'`, вариации вроде `'❤️'` и `'❤'`
равнозначны) или id премиум-эмодзи строкой. `null` убирает твои реакции.
`{ addToRecent: true }` добавит реакцию в недавние.

Промис отклоняется с `REACTION_INVALID`, если такую реакцию нельзя поставить
в этом чате, и с `REACTION_NOT_ALLOWED`, если в чате реакции запрещены.
Без Premium новая реакция заменяет старую.

```js
dzg.on('message.new', (m) => {
  if (!m.out && /спасибо/i.test(m.text)) dzg.messages.react(m.chat, m.id, '❤');
});
```

### `dzg.messages.reactions(peer, id)`

Список эмодзи, которые можно поставить на загруженное сообщение (учитывает
настройки чата). Пусто, если сообщение не загружено.

### `dzg.messages.pin(peer, id, options?)` / `dzg.messages.unpin(peer, id)`

Закрепляет или открепляет сообщение. Опции: `silent` — без уведомления
участников, `unpin` — открепить, `oneSide` — в личном чате закрепить только у
себя.

### `dzg.messages.markRead(peer, options?)`

Читает чат целиком или до `tillId` через обычную логику клиента. Режим
призрака AyuGram действует: если отправка «прочитано» выключена, сервер
ничего не узнает. Возвращает `false`, если чат не найден.

## Файлы

### `dzg.messages.sendFile(peer, path | paths, options?)`

Отправляет файлы так же, как перетаскивание в окно чата: фото и видео
сжимаются (если не `asDocument`), несколько файлов собираются в альбомы по 10.
Относительные пути считаются от папки плагина. Возвращает `false`, если чат не
найден, отправлять файлы в чат нельзя, или файл пустой / слишком большой /
это папка (подробность — в логе плагина).

| Опция | По умолчанию | Описание |
|---|---|---|
| `caption` | — | Подпись (с `parse` — с разметкой) |
| `parse`, `entities` | — | Разметка подписи |
| `asDocument` | `false` | Файлом, без сжатия |
| `album` | `true` | Группировать несколько файлов в альбом |
| `spoiler` | `false` | Фото и видео под спойлер |
| `replyTo`, `topicId`, `silent`, `scheduleDate` | — | Как у `send` |

```js
dzg.messages.sendFile(chat, ['shots/1.png', 'shots/2.png'], {
  caption: '**Скриншоты** сборки',
  parse: 'markdown',
});
```

### `dzg.messages.download(peer, id, path?)`

Скачивает файл (документ, видео, голосовое, стикер...) или фото из сообщения
и возвращает путь к файлу.

- без `path` — в папку загрузок из настроек клиента (если файл уже скачан,
  просто вернётся его путь);
- `path` — папка (существующая или с `/` на конце) — файл ляжет в неё с
  исходным именем, без перезаписи существующих (`name (2).ext`);
- `path` — путь к файлу — сохранится ровно туда.

Ошибки: `NO_MEDIA`, `DOWNLOAD_FAILED`, `DOWNLOAD_CANCELLED`, `WRITE_FAILED`.

```js
dzg.ui.addMessageMenuItem({
  title: 'Скачать в папку плагина',
  when: (m) => !!m.mediaInfo,
  onClick: async (m) => {
    const file = await dzg.messages.download(m.chat, m.id, dzg.plugin.dataDir + 'files/');
    dzg.ui.toast('Сохранено: ' + file);
  },
});
```

## Ссылки и кнопки

### `dzg.messages.link(peer, id, options?)`

Ссылка на сообщение в канале или супергруппе: `https://t.me/name/123` или
`https://t.me/c/1234567/123` для приватных. Для личных чатов и обычных групп —
`null`. `{ private: true }` — всегда вид `t.me/c/...`, `{ thread: true }` — ссылка
в контексте обсуждения / темы.

### `dzg.messages.clickButton(peer, id, row, column, options?)`

Нажимает кнопку бота. Кнопку можно указать позицией (строка и столбец с
нуля, как в `message.buttons`) или текстом: `clickButton(chat, id, 'Купить')`
— сначала ищется точное совпадение, потом подстрока без учёта регистра.

| Кнопка | Что происходит | Результат |
|---|---|---|
| `callback`, `game` | Запрос боту, как при клике | `{ answer?, url?, alert? }` |
| `url` | Ничего не открывается (с `open: true` — открывается) | `{ url }` |
| остальные (webView, запрос номера, switchInline...) | Клиент делает то же, что при клике; нужно открытое окно (`NO_WINDOW`) | `{}` |
| текстовая кнопка клавиатуры | Отправляет её текст ответом на сообщение (как клик) | `{}` |

По умолчанию ответ бота **не показывается** — плагин получает его в
результате. С `{ open: true }` клиент ведёт себя как при обычном клике:
показывает ответ, открывает ссылки.

```js
// Автокликер: жмём «Получить бонус» в сообщениях бота
dzg.on('message.new', async (m) => {
  const button = m.buttons?.flat().find((b) => b.text.includes('бонус'));
  if (!button) return;
  const { answer } = await dzg.messages.clickButton(m.chat, m.id, button.text);
  if (answer) dzg.ui.toast(answer, 'Бот ответил');
});
```

## События

### `message.reaction`

```ts
Message // с полем reactions
```

Кто-то поставил реакцию на твоё сообщение (у клиента появилась новая
непрочитанная реакция).

```js
dzg.on('message.reaction', (m) => {
  const top = (m.reactions || []).map((r) => `${r.emoji}×${r.count}`).join(' ');
  dzg.ui.toast(`${m.chat.name}: ${top}`, 'Новая реакция');
});
```

## Полный пример: архиватор канала

Плагин по команде `/backup` выгружает последние N сообщений текущего чата в
Markdown-файл и скачивает вложения рядом.

```js
dzg.commands.register('backup', {
  description: 'Сохранить историю чата в Markdown: /backup 500',
  async run({ args, chat }) {
    const limit = Number(args[0]) || 200;
    const dir = dzg.plugin.dataDir + 'backup-' + chat.id + '/';
    const lines = [];
    for (const m of await dzg.messages.history(chat, { limit })) {
      const when = new Date(m.date * 1000).toISOString().slice(0, 16).replace('T', ' ');
      let line = `**${m.fromName}** _${when}_\n${dzg.messages.format(m, 'markdown')}`;
      if (m.mediaInfo) {
        try {
          const file = await dzg.messages.download(chat, m.id, dir);
          line += `\n📎 ${file}`;
        } catch (e) {
          line += `\n📎 (${e.message})`;
        }
      }
      lines.push(line);
    }
    dzg.fs.write(dir + 'history.md', lines.join('\n\n'));
    dzg.ui.toast(`Сохранено ${lines.length} сообщений в ${dir}`);
  },
});
```
