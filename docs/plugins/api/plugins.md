# `dzg.plugins` — плагины, которые управляют плагинами

Модуль даёт плагину доступ к менеджеру плагинов: посмотреть, что
установлено, включить и выключить, перезагрузить, поставить новый плагин
по ссылке, удалить, заглянуть в лог и даже выполнить код внутри другого
плагина. На этом можно собрать магазин плагинов, автообновление,
«профили» (наборы включённых плагинов) или свою панель разработчика.

Типы: [`types/plugins.d.ts`](../types/plugins.d.ts). Исходник модуля:
`Telegram/SourceFiles/damza/plugins/api/api_plugins.cpp`.
Автор модуля — [@wetakush](https://t.me/wetakush).

Как устанавливать плагины руками, создавать свои и пользоваться
DevTools — в [manager.md](../manager.md).

| Метод | Что делает | Результат |
|---|---|---|
| `path` | Папка плагинов | `string` |
| `list()` | Все установленные плагины | `InstalledPlugin[]` |
| `get(id?)` | Один плагин (по умолчанию — текущий) | `InstalledPlugin \| null` |
| `setEnabled(id, on)`, `enable(id)`, `disable(id)` | Включить / выключить | `boolean` |
| `reload(id?)`, `reloadAll()` | Перезагрузить один / все | `boolean` |
| `log(id?, limit = 100)` | Хвост лога | `string[]` |
| `settings(id?)` | Значения настроек | `object` |
| `setSetting(id, key, value)` | Поменять настройку чужого плагина | `boolean` |
| `evaluate(id, code)` | Выполнить код в другом плагине | `string` (или `throw`) |
| `inspect(source)` | Прочитать пакет без установки | `Promise<PluginPackageInfo>` |
| `install(source, { enable = true })` | Тихо установить или обновить | `Promise<string>` (id) |
| `promptInstall(source)` | Скачать и показать окно установки | `Promise<void>` |
| `uninstall(id, { removeData })` | Удалить (встроенный — выключить) | `Promise<void>` |
| `openManager()` | Открыть «Настройки → Плагины» | — |
| `openDevTools(id?)` | Открыть окно DevTools | — |

Событие: `plugins.changed` — `{ plugins: InstalledPlugin[] }`.

Правила:

- Там, где нужен id, можно передать сам объект `InstalledPlugin`. Пустой id
  означает текущий плагин.
- `setEnabled`, `reload`, `setSetting` и `uninstall` применяются **после**
  текущего обработчика, а не синхронно: плагин может выключить или
  перезагрузить даже сам себя, не ломая свой стек вызовов. Менеджер вообще
  не грузит и не выгружает плагины, пока на стеке JS любого плагина, —
  изменение ждёт, пока стек размотается.
- Установка нового плагина и удаление не перезапускают остальные плагины:
  менеджер перечитывает папку и трогает только изменившееся. Обновление
  плагина перезапускает только его (в том числе вызвавший плагин, если он
  обновляет сам себя).
- `uninstall(id, { removeData: true })` стирает `storage` плагина и
  забывает его настройки и состояние «включён».
- `source` для `inspect`, `install` и `promptInstall` — это https-ссылка на
  `.zip` или `.js`, ссылка на файл или репозиторий GitHub
  (`github.com/user/repo` скачается как архив ветки по умолчанию),
  `file://` или абсолютный путь к папке, `.zip` или `.js`. Обычный
  `http://` разрешён только для `localhost`.
- `install` ставит плагин **без** подтверждения пользователя. Плагины и так
  имеют полный доступ к клиенту, но вежливее показывать окно —
  `promptInstall`.

## Список и состояние

```js
for (const p of dzg.plugins.list()) {
  const state = p.error ? `ошибка: ${p.error}` : p.loaded ? 'работает' : 'выключен';
  console.log(`${p.name} ${p.version} — ${state}${p.builtin ? ' (встроенный)' : ''}`);
}

dzg.on('plugins.changed', ({ plugins }) => {
  const broken = plugins.filter((p) => p.error);
  if (broken.length) dzg.ui.toast(`С ошибками: ${broken.map((p) => p.name).join(', ')}`);
});
```

## Профили: включить нужный набор

```js
const profiles = {
  work: ['chat-export', 'chat-stats'],
  chill: ['damza-theme'],
};

dzg.commands.register('profile', ({ args }) => {
  const wanted = new Set(profiles[args[0]] || []);
  for (const p of dzg.plugins.list()) {
    if (p.id === dzg.plugin.id) continue;
    dzg.plugins.setEnabled(p.id, wanted.has(p.id));
  }
  dzg.ui.toast(`Профиль ${args[0]} включён`);
});
```

## Магазин плагинов

```js
const catalog = 'https://example.com/damza-plugins.json'; // [{ "url": "..." }, ...]

dzg.commands.register('store', async () => {
  const list = await (await dzg.http.fetch(catalog)).json();
  for (const entry of list) {
    const info = await dzg.plugins.inspect(entry.url);
    const verb = info.installed ? 'Обновить' : 'Установить';
    if (await dzg.ui.confirm(`${info.name} ${info.version}\n${info.description}`, { confirmText: verb })) {
      await dzg.plugins.promptInstall(entry.url);
    }
  }
});
```

## Автообновление с GitHub

```js
const source = 'https://github.com/someone/my-plugin';

dzg.on('ready', async () => {
  const remote = await dzg.plugins.inspect(source);
  const local = dzg.plugins.get(remote.id);
  if (local && local.version !== remote.version) {
    dzg.ui.notify({
      title: `${remote.name}: вышла ${remote.version}`,
      text: 'Нажми, чтобы обновить',
      onClick: () => dzg.plugins.promptInstall(source),
    });
  }
});
```

## Код в другом плагине

`evaluate` выполняет код в **глобальной** области другого плагина (как
консоль DevTools) и возвращает результат текстом. Переменные внутри
ES-модуля снаружи не видны — то, что нужно отлаживать, плагин может
положить в `globalThis`.

```js
const count = dzg.plugins.evaluate('chat-export', 'dzg.storage.keys().length');
console.log('Ключей в хранилище chat-export:', count);
```

Свой собственный плагин так вызвать нельзя (получишь `Error`): для этого
есть обычный `eval`.

## Горячая клавиша

`Ctrl+Shift+P` в любом окне клиента открывает «Настройки → Плагины», если
фокус не в поле ввода: в поле ввода это сочетание оформляет спойлер, как в
Telegram Desktop. Если какой-то плагин зарегистрирует то же сочетание
через `dzg.ui.addShortcut`, сработает плагин.
