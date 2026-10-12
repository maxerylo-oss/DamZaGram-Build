# Плагины DamZaGram

Плагины пишутся на JavaScript и выполняются движком QJSEngine, который встроен в
Qt. Каждый плагин работает в своём движке, со своими глобальными
переменными и хранилищем. Весь код выполняется в главном потоке клиента.

Полные типы API лежат в [`dzg.d.ts`](dzg.d.ts). Примеры плагинов лежат в
[`plugins/examples`](../../plugins/examples).

> Плагины получают **полный доступ** к клиенту, аккаунту, файлам и сети.
> Ставь только тот код, которому доверяешь.

## Быстрый старт

1. Открой **Настройки → AyuGram → Плагины → Открыть папку плагинов**. Это
   `<папка данных>/tdata/plugins/`.
2. Создай в ней папку `my-plugin` с двумя файлами.

`manifest.json`:

```json
{
  "id": "my-plugin",
  "name": "Мой плагин",
  "version": "1.0.0",
  "author": "я",
  "description": "Что делает плагин",
  "main": "main.js",
  "settings": [
    { "key": "word", "type": "string", "title": "Слово", "default": "привет" }
  ]
}
```

`main.js`:

```js
dzg.on('message.new', (m) => {
  if (!m.out && m.text.includes(dzg.settings.get('word'))) {
    dzg.ui.toast(`${m.fromName} в «${m.chat.name}»: ${m.text}`);
  }
});
```

3. Вернись в **Плагины** и включи переключатель рядом с плагином.

Пока включено **«Перезагружать при изменении файлов»**, плагин перезапускается
сам при каждом сохранении. Ошибки показываются тостом и пишутся в **Настройки и лог**.

## Манифест

| Поле | Обязательно | Описание |
|---|---|---|
| `id` | нет (берётся имя папки) | Уникальный id, латиница, цифры и `-` |
| `name` | нет | Название в настройках |
| `version` | нет | Версия, свободный формат |
| `author`, `description` | нет | Показываются в окне плагина |
| `main` | нет (`main.js`) | Входной файл |
| `settings` | нет | Схема настроек, см. ниже |

### Настройки

Из `settings` клиент сам строит окно настроек плагина. У каждой записи такие поля:

- `key` — имя, по которому значение читает `dzg.settings.get(key)`;
- `type` — один из типов:
  - `bool` — чекбокс;
  - `string` — однострочное поле;
  - `text` — многострочное поле;
  - `number` — число;
- `title` — подпись;
- `default` — значение по умолчанию.

Когда пользователь меняет значения, плагин получает событие `settings.changed`.

## Жизненный цикл

1. `main.js` выполняется один раз, сверху вниз. Здесь регистрируй обработчики.
2. Затем приходит событие `ready`.
3. При выключении, перезагрузке или выходе из клиента приходит `unload`. Таймеры,
   пункты меню, фильтры и хуки убираются сами. Свои виджеты и изменения темы
   убирай сам в обработчике `unload`.

Состояние в переменных между перезагрузками **не сохраняется**. Для долговременных
данных есть `dzg.storage`: он сохраняется в
`tdata/plugins_data/<id>.json`.

## Что умеет API

| Модуль | Для чего |
|---|---|
| `dzg.on(event, fn)` | События: `message.new`, `message.edited`, `message.deleted`, `chat.opened`, `session.ready`, `settings.changed`, `ready`, `unload` |
| `dzg.messages` | `send`, `edit`, `delete`, `get`, `loaded` (уже загруженная история), `addFilter` (скрыть сообщения), `onBeforeSend` (переписать или отменить исходящее) |
| `dzg.commands` | Слеш-команды: `/name args` перехватываются и не отправляются |
| `dzg.chats` | `current`, `get`, `find` (по username среди загруженных), `open` |
| `dzg.ui` | Тосты, диалоги (`alert`/`confirm`/`prompt`/`box`), пункты контекстного меню сообщений и чатов, **доступ ко всем виджетам**, создание своих виджетов |
| `dzg.theme` | Менять цвета палитры на лету |
| `dzg.http` | `fetch` с промисами |
| `dzg.fs`, `dzg.clipboard`, `dzg.openUrl` | Файлы, буфер обмена, ссылки |
| `dzg.storage`, `dzg.settings` | Хранилище плагина и его настройки |
| `setTimeout`/`setInterval`, `console` | Как в браузере |
| `dzg.require('./lib.js')` | Разбить плагин на файлы (CommonJS: `module.exports`) |

### Идентификаторы

Чаты передаются как объекты `Peer`, а их `id` — это **строка**. В любой метод можно
передать либо строку `id`, либо сам объект `Peer`. Сообщения адресуются парой
`(peerId, messageId)`.

### Фильтры сообщений

```js
const off = dzg.messages.addFilter((m) => m.fromName === 'Спамер');
// off() — убрать фильтр
```

Фильтр вызывается при отрисовке ленты, поэтому должен быть **быстрым и без
побочных эффектов**. Результат кэшируется на каждое сообщение. Если условие
изменилось (например, в настройках), вызови `dzg.messages.refilter()`.

### Хук перед отправкой и команды

```js
dzg.messages.onBeforeSend(({ text }) => text.replace(/\bкек\b/g, 'kek'));

dzg.commands.register('roll', ({ chat, replyTo }) => {
  dzg.messages.send(chat.id, '🎲 ' + (1 + Math.floor(Math.random() * 6)), { replyTo });
});
```

Сообщения, которые плагин отправил через `dzg.messages.send`, по умолчанию **не**
проходят через `onBeforeSend`, так что зациклиться нельзя. Если обработка нужна,
передай опцию `{ runHooks: true }`.

### Контекстные меню

```js
dzg.ui.addMessageMenuItem({
  title: 'Перевести в верхний регистр',
  when: (m) => m.out && !!m.text,
  onClick: (m) => dzg.messages.edit(m.peerId, m.id, m.text.toUpperCase()),
});

dzg.ui.addChatMenuItem({
  title: (chat) => `Скопировать id ${chat.tgId}`,
  onClick: (chat) => dzg.clipboard.set(String(chat.tgId)),
});
```

### Доступ к интерфейсу

Весь интерфейс клиента состоит из виджетов Qt. `dzg.ui.mainWindow()` возвращает
корень дерева, а `Widget.find(className)` ищет внутри него.

```js
const win = dzg.ui.mainWindow();
console.log(JSON.stringify(win.dump(3), null, 2)); // посмотреть структуру

// Спрятать верхнюю панель чата.
for (const bar of win.find('TopBarWidget')) bar.hide();
```

`className()` возвращает настоящее имя C++ класса, например
`HistoryView::TopBarWidget` или `Dialogs::InnerWidget`. Искать можно по полному
имени или по последней части.

Свои виджеты создаются через `dzg.ui.create`:

```js
const panel = dzg.ui.create('panel', null, { x: 20, y: 80, width: 240, height: 120, color: '#cc1e1e1e', radius: 12 });
dzg.ui.create('label', panel, { x: 12, y: 10, width: 200, height: 24, text: 'Привет' });
dzg.ui.create('button', panel, { x: 12, y: 60, width: 120, height: 40, text: 'Закрыть', onClick: () => panel.destroy() });
```

Доступные типы: `label`, `button`, `input`, `checkbox`, `panel`. Они выглядят
так же, как стандартные элементы Telegram.

### Тема

```js
dzg.theme.setMany({ windowBgActive: '#ff7a45', msgOutBg: '#3a2a20' });
dzg.on('unload', () => dzg.theme.reset());
```

Имена цветов совпадают с ключами `.tdesktop-theme`. Полный список лежит в
`Telegram/lib_ui/ui/colors.palette`.

### HTTP

```js
dzg.http.fetch('https://api.github.com/repos/AyuGram/AyuGramDesktop')
  .then((r) => r.json())
  .then((repo) => dzg.ui.toast('⭐ ' + repo.stargazers_count));
```

Запросы идут напрямую, без CORS и без прокси Telegram.

## Отладка

- `console.log/warn/error` пишутся в лог плагина (**Настройки и лог** в окне
  плагина) и в общий `log.txt` клиента с префиксом `Plugin <id>`.
- Необработанное исключение показывает тост с первой строкой ошибки и
  записывает в лог полный стек.
- Если плагин не загрузился, ошибка видна под его названием в списке плагинов.

## Какой JavaScript поддерживается

QJSEngine — это примерно **ES2017**, плюс `?.` и `??`.

- **Работает:** `let`/`const`, стрелочные функции, классы, деструктуризация,
  rest и spread для массивов и аргументов, шаблонные строки, `Map`/`Set`,
  генераторы, `Promise`, `for...of`.
- **Не работает:**
  - `async`/`await` — используй `.then()`;
  - `{...obj}` — используй `Object.assign`;
  - `??=`;
  - приватные поля `#x`;
  - ES-модули (`import`) — используй `dzg.require`.
- **Подставлено DamZaGram:** `globalThis`, `queueMicrotask`,
  `Object.fromEntries`, `Array.prototype.flat`/`flatMap`,
  `String.prototype.replaceAll`, `Promise.allSettled`.

## Ограничения версии API 1

- Сырые MTProto-запросы (`api.call`) пока не поддерживаются, доступны только
  обёртки выше.
- `messages.loaded` отдаёт только то, что клиент уже загрузил. Подгрузки
  истории с сервера пока нет.
- Защиты от бесконечного цикла нет: `while (true) {}` повесит клиент.
- Файлы `.plugin` формата exteraGram (Python) не поддерживаются.
