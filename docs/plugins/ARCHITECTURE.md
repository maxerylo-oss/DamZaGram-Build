# Устройство плагинной системы (для разработчиков клиента)

Как устроены плагины DamZaGram внутри клиента: движок, мост между C++ и JS,
модули API, хуки в коде Telegram Desktop / AyuGram и инструменты, которыми
это проверяется без сборки. Документация для авторов плагинов — в
[README](README.md). Автор DamZaGram — [@wetakush](https://t.me/wetakush).

Весь код лежит в `Telegram/SourceFiles/damza/`, отдельно от `ayu/`, чтобы
мерж с AyuGram не задевал плагины. Новые `.cpp`/`.h` в `damza/` подхватываются
CMake сами (`file(GLOB_RECURSE … CONFIGURE_DEPENDS)` в `Telegram/CMakeLists.txt`).

```
damza/
  damza_lang.h                Damza::Text(en, ru): строки без lang.strings
  damza_brand.*               Имя, автор, ссылки, «О программе»
  ui/
    settings_plugins.*        Раздел «Плагины» в настройках
    plugin_boxes.*            Окна: настройки плагина, лог, установка, мастер «Создать»
    plugin_devtools.*         Окно DevTools (консоль + живой лог)
    welcome_box.*             «Добро пожаловать / Что нового»
  plugins/
    plugins.h                 Точки входа для остального клиента (хуки)
    plugin_manager.*          Manager: список, вкл/выкл, состояние, hot reload,
                              подписки на сессии, раздача событий и хуков
    plugin_instance.*         Instance: один плагин — свой Js::Runtime, storage, лог
    plugin_js.*               Js::Runtime: обёртка над QuickJS-ng, QVariant <-> JS,
                              загрузчик ES-модулей, сторожевой таймер
    plugin_bridge.*           Bridge (QObject): нативные функции ядра для JS
    plugin_prelude.cpp        JS-прелюдия: из неё собирается публичный объект `dzg`
    plugin_api.*              Реестр модулей API (DZG_API_MODULE)
    api/                      Модули API: app, messages, chats, users, ui, mtproto,
                              plugins, builtin
    ui/                       Состояние и виджеты модуля ui (кнопки, панели, тосты)
    tl/                       Схема TL и кодек JSON <-> TL для dzg.api
    builtin/                  Встроенные плагины, вшитые в бинарник (генерируется)
    plugin_install.*          Установка: папка, .zip, один .js, ссылка; мастер «Создать»
    plugin_types_embedded.*   Вшитые docs/plugins/types/*.d.ts (генерируется)
    plugin_widgets.*          WidgetHandle: слабые ссылки на QWidget для JS
    plugin_data.*             Peer/Message -> QVariantMap, разбор id
```

## Движок: QuickJS-ng

Плагины выполняет [QuickJS-ng](https://github.com/quickjs-ng/quickjs) 0.17
из `Telegram/ThirdParty/quickjs-ng`. Библиотека собирается статически
целью `damza_quickjs` (`Telegram/cmake/lib_quickjs.cmake`, всегда с `-O2`,
даже в Debug — без оптимизаций движок слишком медленный).

`Js::Runtime` (`plugin_js.*`) — один `JSRuntime` + `JSContext` на плагин:

- **Изоляция.** У каждого плагина свой рантайм: глобальные переменные,
  прототипы и сборщик мусора не пересекаются. Всё выполняется в главном
  потоке.
- **Лимиты.** Память — 256 МБ (`JS_SetMemoryLimit`), стек — 1 МБ (в
  Windows 512 КБ: у главного потока там всего 1 МБ). Стек считается от
  самого внешнего входа в **любой** рантайм (`AllEntries`/`StackBase` в
  `plugin_js.cpp`): если плагин вызвал другой плагин (событие, хук
  отправки, `dzg.plugins.evaluate`), цепочка делит один бюджет и не может
  переполнить настоящий стек.
- **Сторожевой таймер.** Каждый вход из C++ в JS оборачивается в
  `Runtime::Entry`: самый внешний вход ставит дедлайн, а
  `JS_SetInterruptHandler` прерывает скрипт, когда время вышло. Лимит —
  10 с на загрузку плагина и 3 с на любой вызов после неё
  (`kLoadTimeLimit`/`kCallTimeLimit` в `plugin_instance.cpp`). Ошибка
  получает префикс `Script took longer than N ms and was stopped.`
- **Микрозадачи.** Когда стек возвращается в C++, `Entry` выполняет очередь
  promise-джобов (`JS_ExecutePendingJob`), поэтому `await` продолжается в
  рамках того же входа. Ожидание нативного запроса в лимит не входит:
  продолжение придёт отдельным входом `settle`.
- **ES-модули.** `main.js` выполняется как модуль (`JS_EVAL_TYPE_MODULE`), если
  в манифесте не указано `"type": "script"`. Загрузчик (`NormalizeModule` /
  `LoadModule`) разрешает пути относительно импортирующего файла (или папки
  плагина), дописывает `.js`, если файла без расширения нет. Пакетов и
  `node_modules` нет. Top-level `await` работает; отклонённый промис модуля
  считается ошибкой загрузки.
- **Необработанные отклонения** промисов пишутся в лог плагина
  (`JS_SetHostPromiseRejectionTracker`).
- **Значения через границу** — только `QVariant`: `null`, `bool`, число,
  строка, `QVariantList`, `QVariantMap` (глубина до 32). Функции, `BigInt`
  и прочие объекты движка через границу не ходят — колбэки хранятся на
  стороне JS под числовыми id, C++ вызывает их через диспатч. Перевод
  JS → C++ (`ToVariant`) защищён: циклическая ссылка становится `null`, на
  одно значение есть бюджет в 4 млн элементов — больше, и нативный вызов
  бросает в JS `RangeError: value is too large for native code`. Если
  чтение свойства бросило исключение (геттер), оно уходит в JS как есть.
- **Нативные функции** ставятся одним объектом `__native` (`installObject`)
  как C-замыкания над `Js::Native` (`Fn<QVariant(const QVariantList&)>`).

## Загрузка плагина

`Instance::load()`:

1. Создаёт `Bridge` и `Js::Runtime` (лимит 10 с).
2. Собирает нативные функции: `Bridge::methods()` (ядро) плюс `natives` всех
   модулей из `ApiModules()`, и ставит их как `globalThis.__native`.
3. Выполняет прелюдию (`plugin_prelude.cpp`, имя файла `dzg:prelude.js`). Она
   строит `dzg`, `console`, таймеры и `globalThis.__dzgDispatch`, а
   внутренний объект `internal` кладёт в `globalThis.__dzgInternal`.
4. Выполняет JS-фрагмент каждого модуля как тело
   `function (native, dzg, internal) { … }` (`dzg:<module>.js`).
5. Удаляет `__native` и `__dzgInternal` из глобальной области — плагин их не
   видит — и запоминает `__dzgDispatch`.
6. Выполняет `main.js` (модулем или скриптом), ставит лимит 3 с.
7. `Manager::loadPlugin` шлёт событие `ready`.

Выгрузка (`Manager::unloadPlugin`): `pluginUnloaded(id)` всех модулей
(убрать кнопки, панели, подписки), затем деструктор `Instance` шлёт
`unload`, закрывает инстанс (`_closed`: ни одной строки JS после
`unload`, даже если удаление виджетов породит события), удаляет `Bridge`
(таймеры, виджеты, HTTP-запросы, цвета темы этого плагина), рантайм и
сохраняет storage.

Загрузка и выгрузка никогда не идут, пока JS какого-либо плагина на
стеке: `setEnabled`, `reload`, `refresh` и `rescan` менеджера сначала
зовут `postponeChange()`. Если идёт другая смена (`_changing`) или
какой-то инстанс `busy()` (синтетический клик по кнопке настроек,
обработчик `unload`…), смена откладывается через `crl::on_main` до
следующей итерации цикла. Иначе освобождение рантайма с JS на стеке было
бы use-after-free.

## Поток вызовов

- **C++ → JS.** Всё идёт через одну функцию `__dzgDispatch(type, payload)`
  (`Instance::dispatch`). Типы ядра:
  - `event` — `dzg.on`; `timer`; `settle` — промисы (`internal.request`);
  - `filter`, `beforeSend` — хуки ленты и отправки (команды обрабатываются
    в `beforeSend`);
  - `menu:collect`, `menu:click` — контекстные меню;
  - `widget` — события созданных виджетов; `unload`.

  Модули регистрируют свои типы через `internal.on(type, fn)`: `ui:call`,
  `ui:drop`, `ui:panelClosed`, `ui:render`, `api:updates`,
  `builtin:commands`, `builtin:run`.
- **JS → C++.** Прелюдия и фрагменты модулей зовут `native.<name>(...)`.
  Асинхронная функция возвращает id запроса (`bridge->nextRequestId()`), JS
  ждёт его в `internal.request`, а C++ потом зовёт
  `bridge->settle(id, ok, value)`.
- **События на все плагины** — `Manager::emitEvent(name, payload)`; рассылка
  идёт по снимку `loadedIds()`, так что плагин может выгрузиться посреди
  рассылки. Событие получают только плагины, у которых есть обработчик:
  прелюдия сообщает о первом и последнем обработчике каждого события через
  `native.setListens(name, on)`, `Instance::listens()` это хранит, а
  `Manager::anyListens(name)` позволяет клиентскому коду не сериализовать
  `Message` впустую. `ready`, `unload` и `settings.changed` уходят одному
  плагину напрямую через `dispatch`.
- **Отложенные события** — `Manager::emitEventLater(name, payload)`: данные
  снимаются сразу, а рассылка идёт по очереди на следующей итерации цикла
  событий (`crl::on_main`), в порядке прихода. Так уходят все события,
  которые рождаются внутри уведомлений клиента: `message.new`,
  `message.edited`, `message.deleted`, `message.reaction`, `chat.typing`,
  `chat.unread`, `user.status`, `app.focus`, `compose.changed`,
  `plugins.changed`. Обработчик плагина не выполняется, пока клиент держит
  сырые указатели на сообщения или перекладывает историю. Сразу (из
  `emitEvent`) приходят только `session.ready`, `chat.opened` и
  `theme.changed`. Новое событие из кода клиента почти всегда должно идти
  через `emitEventLater`.
- **Локальные удаления.** `dzg.messages.delete` удаляет сообщения у себя
  тоже на следующей итерации (`Bridge::deleteMessages`), а не внутри
  вызова: в обработчике `message.new` клиент ещё держит это сообщение.
- **Ошибки** (`Manager::reportError`) могут прийти из JS, который клиент
  вызвал посреди раскладки или отрисовки, поэтому карточка менеджера и
  тост обновляются через `crl::on_main`.

## Модули API

Модуль — один файл в `damza/plugins/api/`, который регистрирует себя
макросом `DZG_API_MODULE` (`plugin_api.h`) и не трогает чужой код:

```cpp
DZG_API_MODULE({
	.name = "example",
	.natives = [](not_null<Bridge*> bridge, std::vector<Js::NativeMethod> &out) {
		out.push_back({ "example_hello", [](const QVariantList &args) {
			return QVariant(u"Hello, "_q + args.value(0).toString());
		} });
	},
	.prelude = [] {
		return QString::fromUtf8(R"DZG_JS(
	dzg.example = {
		hello(name) { return native.example_hello(String(name)); },
	};
)DZG_JS");
	},
	.appStarted = [] { /* подписки на всё приложение */ },
	.sessionStarted = [](not_null<Main::Session*> session) { /* на сессию */ },
	.pluginUnloaded = [](const QString &pluginId) { /* убрать регистрации */ },
});
```

Шаблон — `api/api_app.cpp`. Правила:

- Нативные функции — с префиксом модуля (`messages_history`).
- JS — в литералах `R"DZG_JS( … )DZG_JS"`: так их находит песочница. Один
  литерал — меньше 16 КБ (лимит MSVC), большой фрагмент дели на несколько.
- В асинхронных колбэках держи `QPointer<Bridge>`, а не сырые указатели на
  `Instance`/`Bridge`: плагин могут выгрузить в любой момент.
- MTP-запросы — `session->api().request(MTP…).done(…).fail(…).send()`.
- Id чатов уходят в JS строками через `PeerIdToString`, приходят —
  `internal.toPeerId` в JS и `ResolvePeer` в C++.
- Подписки модуля живут в `Manager::Get().lifetime()` или
  `session->lifetime()`, не в статическом `rpl::lifetime`.
- Строки интерфейса — `Damza::Text("English", "Русский")`.

| Модуль | Файл | Что даёт |
|---|---|---|
| ядро | `plugin_bridge.cpp`, `plugin_prelude.cpp` | `dzg.on`, `storage`, `settings`, `ui` (базовый), `theme`, `chats` (базовый), `messages` (базовый), `commands`, `http`, `fs`, `clipboard`, таймеры, `console` |
| `app` | `api/api_app.cpp` | `dzg.app.language/isNightMode/setNightMode/restart`, `theme.changed` |
| `messages` | `api/api_messages.cpp` | Разметка, история, поиск, пересылка, реакции, файлы, кнопки, `message.reaction` |
| `chats` | `api/api_chats.cpp` | Список, папки, вступление, mute, участники, `chat.typing`, `chat.unread` |
| `users` | `api/api_users.cpp` | `dzg.users`, `dzg.accounts`, `user.status` |
| `ui` | `api/api_ui.cpp`, `plugins/ui/` | Кнопки, меню, горячие клавиши, уведомления, панели, подмена текста, `app.focus`, `compose.changed` |
| `api` | `api/api_mtproto.cpp`, `plugins/tl/` | Сырой MTProto и обновления |
| `plugins` | `api/api_plugins.cpp` | Управление плагинами, `plugins.changed`, `Ctrl+Shift+P` |
| `builtin` | `api/api_builtin.cpp`, `plugins/builtin/` | Установка встроенных плагинов, `dzg.commands.all/run` |

Добавляя метод, допиши его тип в `docs/plugins/types/<модуль>.d.ts` (JSDoc
по-русски), пример — в `docs/plugins/api/<модуль>.md`, затем перегенерируй
справочник и вшитые типы (см. [инструменты](#инструменты)).

## Хуки в коде клиента

Каждый хук — одна-две строки с вызовом в `damza/`, помечены `// DamZaGram`.

| Место | Хук |
|---|---|
| `core/application.cpp`, после `finishFirstShow()` | `Plugins::Start()` |
| `core/application.cpp`, начало `~Application()` | `Plugins::Shutdown()`: обработчики `unload` ещё видят окна и сессии |
| `ayu/utils/telegram_helpers.cpp` `isMessageHidden` | `Plugins::Filtered` (`dzg.messages.addFilter`) |
| `apiwrap.cpp` `ApiWrap::sendMessage` | `Plugins::BeforeSend` (`onBeforeSend`, команды) |
| `data/data_session.cpp` `processMessagesDeleted`, `processNonChannelMessagesDeleted` | `Plugins::MessageDeleted` |
| `history/history_inner_widget.cpp`, `history/view/history_view_context_menu.cpp` | `Plugins::FillMessageMenu` |
| `window/window_peer_menu.cpp` `fillContextMenuActions`, `fillHistoryActions` | `Plugins::FillPeerMenu` |
| `ayu/ui/settings/settings_main.cpp` `BuildCategories` | кнопка раздела «Плагины» |
| `window/window_main_menu.cpp` `MainMenu::setupMenu` | `PluginsUi::AddMainMenuEntry` (пункт «Плагины»), `Plugins::FillMainMenu` (`dzg.ui.addMainMenuItem`) |
| `data/data_document_resolver.cpp` `ResolveDocument` | `PluginsUi::OpenPluginFile` (`*.dzg.zip`, `*.dzg.js`, `*.plugin.js` из чата) |
| `api/api_updates.cpp` `Updates::handleSendActionUpdate` | `Plugins::ChatSendActionReceived` (`chat.typing`, `api/api_chats.h`) |
| `api/api_updates.cpp` `Updates::feedUpdate`, `Updates::applyUpdates` | `Plugins::FeedRawUpdate(s)` (`dzg.api.onUpdate`, `api/api_mtproto.h`; без подписчиков — одна проверка флага) |
| `history/view/history_view_top_bar_widget.cpp` `updateControlsVisibility`, `updateControlsGeometry` | `Plugins::UpdateTopBarButtons`, `Plugins::PlaceTopBarButtons` (`plugins/ui/ui_hooks.h`) |
| `history/history_widget.cpp` конструктор, `updateFieldSize`, `moveFieldControls`; `history/view/controls/history_view_compose_controls.cpp` `init`, `updateControlsGeometry` | `Plugins::RegisterComposeField`, `ComposeButtonsWidth`, `PlaceComposeButtons` (`dzg.ui.compose`, `compose.changed`) |
| `mainwindow.cpp` `MainWindow::updateControlsGeometry` | `Plugins::PlaceSidePanels` (`dzg.ui.panel`) |
| `history/view/history_view_element.cpp` `Element::validateText` | `Plugins::DisplayText` (`addRenderTransform`; без трансформаций — одна проверка) |
| `mainwidget.cpp` конструктор `MainWidget` | `Damza::ShowWelcomeIfNeeded` (окно «Что нового») |

События без хуков: `message.new` и `message.edited` — подписки на
`Data::Session::newItemAdded()` и `Data::Changes::messageUpdates(Edited)`
для каждой сессии (каждого аккаунта, поэтому у `Message` есть `selfId` —
id аккаунта, которому пришло сообщение, из `plugin_data.cpp`); `chat.opened` — `SessionController::activeChatChanges()`
главного окна; `message.reaction` — `MessageUpdate::Flag::NewUnreadReaction`;
`user.status`, `chat.unread` — `PeerUpdate`/`HistoryUpdate`; `app.focus` —
`QGuiApplication::applicationStateChanged`; горячие клавиши — фильтр событий
на `QApplication`.

## Менеджер

- **Состояние.** `tdata/plugins_data/_state.json`: какие плагины включены,
  их настройки, режим разработчика. Хранилище плагина —
  `tdata/plugins_data/<id>.json` (пишется на каждый `storage.set` и при
  выгрузке). Удаление с данными (`UninstallPlugin(id, removeData)`)
  стирает и этот файл, и запись плагина в `_state.json`
  (`forgetPlugin`).
- **Id плагина** проверяется в `readManifest` (`IsValidPluginId`:
  `^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$`): id называет файлы в
  `plugins_data`, поэтому папка с плохим id пропускается с записью в лог.
- **Обновление списка.** `refresh()` перечитывает папку и трогает только
  изменившееся: исчезнувшие плагины и плагины с другим `dir`, `main`,
  `type` или `version` выгружаются, новые включённые загружаются, остальные
  продолжают работать. `rescan()` (кнопка «Перезагрузить все»,
  `dzg.plugins.reloadAll()`) перезапускает все.
- **Горячая перезагрузка** (режим разработчика): `QFileSystemWatcher` на
  папку плагинов и на папку каждого плагина с файлами и подпапками (обход в
  ширину, до 256 путей на плагин, без скрытых и `node_modules`). Изменение
  файла — `reload(id)` через 300 мс; изменение папки плагинов —
  `refresh()`. Запись плагина в собственные файлы (`dzg.fs.write` →
  `Bridge::writeFile` → `Manager::ignoreFileChange`) 1,5 с не считается
  изменением, иначе плагин, который пишет в свою папку, перезапускался бы
  без конца. В режиме разработчика ошибки плагинов показываются тостом
  (не чаще раза в 3 с на плагин).
- **Лог** плагина — последние 500 строк, строка длиннее 16 КБ обрезается.
- **Тема.** Каждый `Bridge` хранит свои переопределения цветов. Перед
  первым из них сохраняется базовая палитра; `reset()` (и выгрузка)
  возвращает базу и накладывает поверх цвета остальных плагинов. Когда
  клиент меняет палитру (другая тема, ночной режим), `api_app.cpp` зовёт
  `Bridge::ForgetThemeOverrides()`: база забывается, а на следующей
  итерации цвета всех плагинов накладываются на новую палитру сами.
  `dzg.theme.get(name)` читает текущий цвет.
- **Встроенные плагины.** `api_builtin.cpp` в `appStarted` (до `rescan`)
  распаковывает `plugins/builtin/*` из бинарника в `tdata/plugins/<id>/`,
  если плагина нет или у установленного `"builtin": true` и версия ниже.
  Папки-симлинки и плагины без `"builtin": true` не трогаются.
  `"enabledByDefault": false` — не включать при первом запуске.
- **Установка** (`plugin_install.*`): staging-папка
  `tdata/plugins_data/_staging/`, затем замена папки плагина. Новый плагин
  и удаление — `refresh()` (остальные плагины продолжают работать),
  обновление существующего — `reload(id)`.
- **Выход.** `Plugins::Shutdown()` вызывается в начале деструктора
  `Core::Application`, пока живы окна и сессии: менеджер сохраняет
  состояние и выгружает плагины (их `unload` ещё может работать с
  клиентом).
- **Горячие клавиши менеджера.** `Ctrl+Shift+P` (`api_plugins.cpp`,
  фильтр событий на `QApplication`) открывает «Настройки → Плагины», только
  если фокус не в `Ui::InputField`: в поле ввода это сочетание оформляет
  спойлер. Командная палитра — встроенный плагин на `dzg.ui.addShortcut`
  (`Ctrl+Shift+K`).

## Инструменты

Сборку клиента для проверки не запускают — для этого есть быстрые
инструменты в `tools/damza/`:

| Инструмент | Что делает |
|---|---|
| `check.py file.cpp …` | Синтаксическая проверка C++ с настоящими флагами из `out/compile_commands.json` (`-fsyntax-only`). Новые файлы берут флаги `plugin_bridge.cpp`. Проверяй и `.cpp`, которые включают изменённый заголовок |
| `sandbox.py main.js [test.js] [--script] [--setup=x.js]` | Запуск плагина без клиента: настоящая прелюдия и все JS-фрагменты модулей (вырезаются из `R"DZG_JS(…)DZG_JS"`) поверх мок-моста `sandbox_mock.js`. Нужен `out-qjs/qjs` |
| `tests/*.test.js`, `tests/builtins/run.py` | Тесты модулей и встроенных плагинов в песочнице |
| `tests/tl_codec_test.sh` | Отдельная сборка и тест TL-кодека (QtCore, без клиента) |
| `tests/install_test.sh` | Тест установщика плагинов на заглушках |
| `gen_tl.py` | Встраивает `api.tl`/`mtproto.tl` в `plugins/tl/tl_schema_data.cpp` и `tl_schema_layer.h`. Перезапускать после обновления схемы (устаревший слой ломает сборку `static_assert`) |
| `embed_builtins.py [--check]` | Вшивает `plugins/builtin/*` в `plugins/builtin/builtin_files.cpp` |
| `embed_types.py` | Вшивает `docs/plugins/types/*.d.ts` в `plugins/plugin_types_embedded.cpp` (для мастера «Создать плагин» и DevTools) |
| `gen_reference.py [--check] [--missing]` | Собирает `docs/plugins/reference.md` из `.d.ts` |
| `check_docs.py [--tsc=…]` | Проверяет код в документации: все блоки ```` ```js ```` разбираются, рецепты из `cookbook.md` запускаются в песочнице и проходят `tsc --checkJs` |
| `build_docs_site.py` | Собирает `docs/plugins/site/index.html` — офлайн-портал документации |

Собрать QuickJS для песочницы (это не сборка клиента):

```sh
cmake -S Telegram/ThirdParty/quickjs-ng -B out-qjs -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out-qjs --target qjs_exe
```

После изменения типов в `docs/plugins/types/`:

```sh
tools/damza/embed_types.py && tools/damza/check.py Telegram/SourceFiles/damza/plugins/plugin_types_embedded.cpp
tools/damza/gen_reference.py
tools/damza/build_docs_site.py
```

## Известные пробелы

- `dzg.ui.notify` — уведомление внутри окна клиента, а не системное: механизм
  уведомлений клиента рассчитан только на сообщения.
- DevTools читает лог плагина опросом раз в 400 мс (нет сигнала о новой
  строке лога).
- `dzg.http` читает тело ответа как UTF-8 — бинарные ответы не поддерживаются.
- `Widget.grab(path)` сохраняет относительный путь от рабочей папки процесса,
  а не от папки плагина.
- Файлы `.plugin` формата exteraGram/AyuGram (Python) не запускаются.
- Автообновление клиента выключено (`disable_autoupdate` в
  `cmake/variables.cmake`): своего сервера обновлений у DamZaGram пока
  нет, а сервер AyuGram подменил бы сборку. Встроенные плагины
  обновляются вместе с новой сборкой.
