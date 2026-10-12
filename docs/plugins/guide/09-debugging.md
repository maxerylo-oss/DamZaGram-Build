# 9. Отладка

Инструменты по мере погружения: лог плагина, DevTools с консолью внутри
плагина, проверка типов в редакторе и юнит-тесты в песочнице без запуска
клиента.

## Лог

```js
console.log('обычная строка', { a: 1 });   // объекты — как JSON
console.info('то же, что log');
console.debug('подробности');
console.warn('предупреждение');
console.error('ошибка');                   // в режиме разработчика — ещё и тост
dzg.log('то же, что console.log');
```

Где смотреть:

- кнопка **Лог** в карточке плагина — последние 500 строк;
- окно **DevTools** — живой хвост лога выбранного плагина;
- общий лог клиента `log.txt` (в папке данных) — каждая строка с
  префиксом `Plugin <id>:`;
- из другого плагина — `dzg.plugins.log(id, 100)`.

Исключение в обработчике, отклонённый промис без `catch`, ошибка загрузки —
всё попадает в лог с полным стеком. Первая строка ошибки показывается в
карточке плагина красным, а в режиме разработчика (включена горячая
перезагрузка) — ещё и тостом «Ошибка плагина: id» (не чаще раза в 3 секунды
на плагин).

## DevTools

**DevTools** — отдельное окно с консолью: кнопка **DevTools** в разделе
«Плагины», кнопка **Консоль** в карточке плагина или
`dzg.plugins.openDevTools(id)`.

- Сверху — выбор плагина. Код выполняется **внутри** выбранного плагина: там
  его `dzg`, его глобальные переменные, его хранилище.
- Снизу — поле ввода, **Ctrl+Enter** выполняет.
- Результат показывается как JSON, ошибка — с пометкой `✕`.
- Работает top-level `await`: результат придёт в лог строкой
  `[devtools] => …`.

```js
dzg.chats.current()
dzg.settings.all()
await dzg.messages.history(dzg.chats.current(), { limit: 3 })
dzg.api.schema('messages.sendReaction')
dzg.ui.find('TopBarWidget').map((w) => w.className())
```

Переменные ES-модуля (`const` в `main.js`) снаружи не видны. То, что нужно
трогать из консоли, положи в `globalThis`:

```js
const state = { queue: [], sent: 0 };
globalThis.debug = { state };   // в консоли: debug.state
```

**Песочница** — служебный плагин `devtools` (тип `script`), который
DevTools создаёт при первом обращении. Там удобно пробовать API, ничего не
ломая в своих плагинах.

## Подсказки и проверка типов

Мастер **Создать плагин** кладёт в папку `jsconfig.json` и `types/*.d.ts`.
Включи в `jsconfig.json` `"checkJs": true` — VS Code подчеркнёт опечатки в
именах методов, неверные типы аргументов и забытые `await`. Из командной
строки то же самое делает TypeScript:

```sh
npx -p typescript tsc --noEmit --checkJs --allowJs --target ES2023 --module ES2022 \
  main.js types/index.d.ts
```

## Тесты в песочнице

`tools/damza/sandbox.py` запускает плагин **без клиента**: настоящая
прелюдия `dzg` и JS-части всех модулей API, а вместо клиента —
мок-мост, который записывает вызовы и возвращает то, что скажет тест.

```sh
tools/damza/sandbox.py path/to/plugin/main.js [test.js] [--script] [--setup=setup.js]
```

- `main.js` загружается как ES-модуль (`--script` — как классический
  скрипт).
- `test.js` выполняется после плагина. Тест прошёл, если не бросил
  исключение. Код выхода не 0 при любой ошибке.
- `--setup=file.js` выполняется до плагина — например, чтобы задать
  ответы моков (`tools/damza/tests/ui.setup.js`).

Нужен QuickJS-ng в `out-qjs/qjs`. Если его нет, собери один раз (это не
сборка клиента, занимает секунды):

```sh
cmake -S Telegram/ThirdParty/quickjs-ng -B out-qjs -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out-qjs --target qjs_exe
```

### Объект `sandbox`

| Член | Что делает |
|---|---|
| `sandbox.event(name, data)` | Прислать событие, как `dzg.on` получит его от клиента |
| `sandbox.dispatch('beforeSend', { text, chat, peerId, replyTo })` | Отправить «сообщение» через хуки и команды |
| `sandbox.dispatch('filter', message)` | Спросить фильтры: `true` — скрыто |
| `sandbox.settle(id, ok, value)` | Завершить асинхронный запрос (промис `box`, `http`, `history`…) |
| `sandbox.runTimers()` | Выполнить все запущенные таймеры один раз |
| `sandbox.calls`, `called(name)`, `last(name)`, `reset()` | Записанные вызовы нативных функций |
| `sandbox.returns[name] = value` | Что вернёт нативная функция `name` |
| `sandbox.native[name] = (...args) => value` | Своя реализация нативной функции |
| `sandbox.settings`, `sandbox.storage` | Настройки и хранилище плагина |
| `sandbox.eq(a, b, msg)`, `sandbox.assert(cond, msg)` | Проверки |

Нативные функции называются по модулю: `toast`, `sendMessage`,
`messages_send`, `messages_history`, `chats_list`, `ui_notify`… Список
легко увидеть, напечатав `sandbox.calls` после действия.

### Пример теста

Плагин `/roll`:

```js
// main.js
dzg.commands.register('roll', ({ args, chat }) => {
  const sides = Number(args[0]) || 6;
  dzg.messages.send(chat, `🎲 ${1 + Math.floor(Math.random() * sides)}`);
});
```

Тест:

```js
// roll.test.js
const { eq, assert } = sandbox;
const chat = { id: '42', tgId: 42, name: 'Test', username: '', type: 'group', isSelf: false };

const result = sandbox.dispatch('beforeSend', { text: '/roll 20', chat, peerId: chat.id, replyTo: 0 });
eq(result, { cancel: true }, 'команда не уходит в чат');

const call = sandbox.last('messages_send');
assert(call, 'плагин отправил ответ');
eq(call.args[0], '42');
const value = Number(call.args[1].replace('🎲 ', ''));
assert(value >= 1 && value <= 20, 'число в диапазоне');

print('roll.test.js: ok');
```

```sh
tools/damza/sandbox.py my-plugin/main.js my-plugin/roll.test.js
```

Асинхронные методы возвращают промис, который завершает `sandbox.settle`.
Проще всего заранее задать id запроса, который вернёт нативная функция:

```js
sandbox.native.messages_history = () => 7;          // запрос получит id 7
const promise = dzg.messages.history('42', { limit: 10 });
sandbox.settle(7, true, []);
eq(await promise, []);
```

Тесты всех встроенных плагинов — в `tools/damza/tests/builtins/`, запуск
всех сразу: `tools/damza/tests/builtins/run.py`.

## Сторожевой таймер и лимиты

| Лимит | Значение | Что будет |
|---|---|---|
| Один вызов из клиента в плагин (событие, команда, клик, таймер, хук) | 3 секунды | Скрипт остановится: `Script took longer than 3000 ms and was stopped` |
| Выполнение `main.js` при загрузке | 10 секунд | Плагин не загрузится с той же ошибкой |
| Память | 256 МБ на плагин | Исключение `out of memory` |
| Глубина стека | 1 МБ | `RangeError: Maximum call stack size exceeded` |

Время ожидания `await` в лимит не входит: каждый раз, когда приходит ответ
сервера или срабатывает таймер, отсчёт начинается заново. Поэтому долгую
работу дели на части:

```js
// Плохо: синхронный цикл по огромному массиву может не уложиться в 3 секунды
// Хорошо: порции по 500 элементов с отдачей управления клиенту
async function processAll(items, fn) {
  for (let i = 0; i < items.length; i += 500) {
    items.slice(i, i + 500).forEach(fn);
    await new Promise((resolve) => setTimeout(resolve, 0));
  }
}
```

## Частые ошибки

| Симптом | Причина и решение |
|---|---|
| `SyntaxError` на строке с `import` или `export` | В манифесте `"type": "script"` — `import` работает только в модулях. Убери `type` или используй `dzg.require` |
| `could not load module '…/lib/x'` | Неверный путь в `import`. Пути относительные от импортирующего файла, `./` обязателен |
| `Module not found: ./x.js` | `dzg.require` ищет путь от папки плагина, не от текущего файла |
| `Script took longer than 3000 ms…` | Синхронная работа дольше 3 секунд. Дели на порции, используй `await` |
| `TypeError: … is not a function` у `dzg.xxx` | Опечатка в имени метода. Включи `checkJs` в VS Code |
| Обработчик не вызывается | Опечатка в имени события (`message.New`), или событие только для активного аккаунта |
| Плагин отвечает сам себе бесконечно | В `message.new` не проверен `m.out` |
| Плагин перезагружается без конца | Плагин пишет файлы в свою папку при включённой горячей перезагрузке. Пиши в `dzg.plugin.dataDir` |
| `FLOOD_WAIT_N` | Слишком частые запросы к серверу. Подожди N секунд, кешируй результаты |
| `PEER_NOT_FOUND` | Клиент не знает этот чат. Сначала `dzg.chats.resolve` |
| `Request was not started` | Асинхронный метод не смог начать запрос (нет аккаунта, неверные аргументы). Смотри лог |
| `Intl is not defined`, странный `toLocaleString` | В QuickJS нет `Intl` — форматируй вручную |
| Данные пропали после перезапуска | Переменные не сохраняются — используй `dzg.storage` |
| После выключения плагина остались цвета | Добавь `dzg.on('unload', () => dzg.theme.reset())` |

## Что дальше

- [Публикация](10-publishing.md) — как поделиться плагином.
- [Устройство плагинной системы](../ARCHITECTURE.md) — если нужно залезть
  в C++.
