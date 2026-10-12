# 7. Сеть и хранение

Плагину часто нужно сходить в интернет, что-то запомнить и дать
пользователю настроить себя. Для этого есть `dzg.http`, `dzg.storage`,
`dzg.settings`, `dzg.fs` и `dzg.clipboard`.

## HTTP

`dzg.http.fetch` похож на браузерный `fetch`:

```js
const response = await dzg.http.fetch('https://api.github.com/repos/AyuGram/AyuGramDesktop');
if (response.ok) {
  const repo = await response.json();
  dzg.ui.toast(`★ ${repo.stargazers_count}`);
} else {
  console.warn('GitHub ответил', response.status);
}
```

Отправить JSON, заголовки, таймаут:

```js
const r = await dzg.http.fetch('https://api.example.com/v1/notes', {
  method: 'POST',
  headers: { Authorization: 'Bearer ' + dzg.settings.get('token') },
  json: { text: 'Привет', tags: ['damzagram'] },   // сам выставит Content-Type
  timeout: 10_000,                                   // мс, по умолчанию 30 000
});
```

| Опция | Описание |
|---|---|
| `method` | `GET` (по умолчанию), `POST`, `PUT`, `PATCH`, `DELETE`… |
| `headers` | Объект заголовков |
| `body` | Тело строкой (уйдёт как UTF-8) |
| `json` | Объект: сериализуется, `Content-Type: application/json` |
| `timeout` | Таймаут передачи в мс |

Ответ: `status`, `ok` (200–299), `url` (после редиректов), `headers`
(имена в нижнем регистре), `text()` и `json()` — оба возвращают промис.

Что важно знать:

- Промис **отклоняется только при сетевой ошибке** (нет сети, таймаут, DNS).
  Ответы 404 и 500 — обычный ответ с `ok === false`.
- Тело ответа читается как текст UTF-8. Бинарные данные (картинки, архивы)
  через `fetch` не получить — для файлов из Telegram есть
  `dzg.messages.download`.
- Ограничений CORS нет, редиректы выполняются (кроме перехода с https на
  http). По умолчанию заголовок `User-Agent: DamZaGram/<версия>`.
- Запросы асинхронные и не блокируют клиент — можно ждать сколько угодно,
  лимит 3 секунды на вызов к `await` не относится.

Повтор при ошибке:

```js
async function fetchJson(url, attempts = 3) {
  for (let i = 1; ; i++) {
    try {
      const r = await dzg.http.fetch(url, { timeout: 15_000 });
      if (r.ok) return await r.json();
      if (r.status < 500 || i >= attempts) throw new Error('HTTP ' + r.status);
    } catch (e) {
      if (i >= attempts) throw e;
    }
    await new Promise((resolve) => setTimeout(resolve, 1000 * i));
  }
}
```

## Хранилище: `dzg.storage`

Маленькая база «ключ → значение» для плагина. Значение — всё, что
сериализуется в JSON: числа, строки, массивы, объекты.

```js
const seen = dzg.storage.get('seen', 0) + 1;    // второй аргумент — значение по умолчанию
dzg.storage.set('seen', seen);
dzg.storage.set('profile', { name: 'Паша', tags: ['a', 'b'] });
dzg.storage.remove('old');
dzg.storage.keys();                              // ['seen', 'profile']
```

- Хранится в `tdata/plugins_data/<id>.json` и переживает перезапуски
  плагина и клиента, обновления плагина.
- Каждый `set` сразу записывает файл целиком. Не вызывай `set` в цикле на
  тысячи итераций — собери данные и запиши один раз, или сохраняй по
  таймеру.
- `get` возвращает копию: изменения объекта нужно записать обратно через
  `set`.
- Удаляется вместе с плагином, если при удалении выбрать «стереть данные».

```js
// Удобная обёртка: объект, который сохраняется не чаще раза в 5 секунд
const state = dzg.storage.get('state', { count: 0, chats: {} });
let saveTimer = 0;
const save = () => {
  if (!saveTimer) saveTimer = setTimeout(() => { saveTimer = 0; dzg.storage.set('state', state); }, 5000);
};
dzg.on('unload', () => dzg.storage.set('state', state));
```

## Настройки: `dzg.settings`

Настройки — это значения из схемы `settings` в `manifest.json`, которые
пользователь меняет в окне **Настройки** плагина
([типы полей](../README.md#схема-настроек)).

```js
dzg.settings.get('limit');        // значение или default из схемы
dzg.settings.all();               // { limit: 50, mode: 'short', … }
dzg.settings.set('limit', 100);   // поменять из кода

dzg.on('settings.changed', ({ key, value }) => {
  console.log(key, '→', value);
});
```

- `get` всегда возвращает актуальное значение — кешировать не нужно.
- `settings.changed` приходит на каждый изменённый ключ: после «Сохранить» в
  окне, после `dzg.settings.set` из самого плагина и после
  `dzg.plugins.setSetting` из другого плагина.
- Чем отличаются от `storage`: настройки видит и меняет пользователь в
  готовом окне, `storage` — внутренние данные плагина.
- Секреты (API-ключи) удобно хранить в настройке типа `string` — их видно
  только в окне настроек и в `tdata/plugins_data/_state.json`.

## Файлы: `dzg.fs`

```js
dzg.fs.write(dzg.plugin.dataDir + 'my-plugin/export.json', JSON.stringify(data, null, 2));
const text = dzg.fs.read('templates/reply.txt');     // относительно папки плагина
dzg.fs.exists('config.json');
dzg.fs.list(dzg.plugin.dataDir);                     // имена файлов и папок
```

- Относительные пути считаются от папки плагина, абсолютные — как есть.
- Только текст в UTF-8. `read` вернёт `null`, если файла нет. `write`
  создаёт недостающие папки и возвращает `false` при ошибке.
- Свои данные пиши в `dzg.plugin.dataDir` (общая папка данных плагинов,
  заведи в ней подпапку со своим id), а не в папку плагина: запись в
  корень папки плагина при включённой горячей перезагрузке перезапустит
  плагин.

## Буфер обмена и ссылки

```js
dzg.clipboard.set('Скопировано плагином');
const text = dzg.clipboard.get();

dzg.openUrl('https://github.com/wetakush');       // внешний браузер
dzg.ui.openLink('https://t.me/wetakush');         // через клиент: t.me, tg://
```

## Таймеры

```js
const id = setInterval(() => console.log('тик'), 60_000);
clearInterval(id);
setTimeout(() => dzg.ui.toast('Прошло 5 секунд'), 5000);
```

Таймеры работают как в браузере и останавливаются сами при выгрузке
плагина. Таймер не переживает перезапуск клиента: для напоминаний на
завтра храни время в `storage` и при старте плагина заводи таймер заново
(или используй отложенные сообщения: `send(..., { scheduleDate })`).

Ежедневная задача в 9:00:

```js
function everyDayAt(hour, minute, fn) {
  const next = new Date();
  next.setHours(hour, minute, 0, 0);
  if (next <= new Date()) next.setDate(next.getDate() + 1);
  setTimeout(() => {
    fn();
    everyDayAt(hour, minute, fn);
  }, next.getTime() - Date.now());
}
everyDayAt(9, 0, () => dzg.ui.notify('Доброе утро!'));
```

## Что дальше

- [Сырой MTProto](08-raw-mtproto.md) — когда HTTP мало и нужно в сам Telegram.
- [Рецепты](../cookbook.md): погода через HTTP, экспорт чата в JSON,
  ежедневный дайджест.
