# Устройство плагинной системы (для разработчиков клиента)

Код лежит в `Telegram/SourceFiles/damza/`, отдельно от `ayu/`, чтобы мерж с
AyuGram не задевал плагины.

```
damza/
  damza_lang.h              Damza::Text(en, ru): строки без lang.strings
  plugins/
    plugins.h               Точки входа для остального клиента (хуки)
    plugin_manager.*        Manager: список, вкл/выкл, состояние, hot reload,
                            подписки на сессии, раздача событий и хуков
    plugin_instance.*       Instance: один плагин, свой QJSEngine, storage, лог
    plugin_bridge.*         Bridge (QObject): нативные методы для JS
    plugin_prelude.cpp      JS-обёртка, из которой собирается публичный объект `dzg`
    plugin_widgets.*        WidgetHandle: слабая ссылка на QWidget для JS
    plugin_data.*           Peer/Message -> QVariantMap, разбор id
  ui/settings_plugins.*     Раздел «Плагины» в настройках AyuGram
```

## Поток вызовов

- **C++ → JS.** Всё идёт через одну функцию `__dzgDispatch(type, payload)` из
  прелюдии. Типы вызовов:
  - `event`, `timer`, `settle` (промисы `box` и `http`);
  - `filter`, `beforeSend`;
  - `menu:collect`, `menu:click`;
  - `widget`, `unload`.
- **JS → C++.** Прелюдия зовёт методы `Bridge` с `Q_INVOKABLE`. Сам `Bridge`
  плагинам не виден: глобальная переменная `__native` удаляется сразу после
  прелюдии.

## Хуки в коде клиента

| Место | Хук |
|---|---|
| `core/application.cpp`, после `finishFirstShow()` | `Plugins::Start()` |
| `ayu/utils/telegram_helpers.cpp` `isMessageHidden` | `Plugins::Filtered` |
| `apiwrap.cpp` `ApiWrap::sendMessage` | `Plugins::BeforeSend` |
| `data/data_session.cpp` `processMessagesDeleted`, `processNonChannelMessagesDeleted` | `Plugins::MessageDeleted` |
| `history/history_inner_widget.cpp`, `history/view/history_view_context_menu.cpp` | `Plugins::FillMessageMenu` |
| `window/window_peer_menu.cpp` `fillContextMenuActions`, `fillHistoryActions` | `Plugins::FillPeerMenu` |
| `ayu/ui/settings/settings_main.cpp` `BuildCategories` | кнопка раздела «Плагины» |

События `message.new` и `message.edited` берутся из подписок на
`Data::Session::newItemAdded()` и `Data::Changes::messageUpdates(Edited)`, а
`chat.opened` — из `SessionController::activeChatChanges()`. Код клиента для них
не менялся.

## Как добавить метод в API

1. Добавь `Q_INVOKABLE` в `Bridge` (`plugin_bridge.h/.cpp`). Аргументы и
   возвращаемые значения: `QString`, числа, `bool`, `QVariantMap`, `QVariantList`
   или `QJSValue`.
2. Оберни его в прелюдии (`plugin_prelude.cpp`) в нужном модуле `dzg.*`.
3. Опиши в `docs/plugins/dzg.d.ts` и, если нужно, в `README.md`.
4. Не держи сырые указатели на данные клиента дольше одного вызова. Плагин
   может быть выгружен в любой момент, поэтому колбэки ловят `QPointer<Bridge>`
   или ищут плагин по id через `Manager::Get().instance(id)`.

## Известные пробелы (кандидаты на следующие итерации)

- Сырые MTProto-вызовы: нужен сериализатор TL из `api.tl` в
  `MTP::details::SerializedRequest`.
- Подгрузка истории с сервера (`messages.history`), медиа (скачать и отправить
  файл), реакции, пересылка.
- Хук трансформации текста при отрисовке (`Element::validateText`).
- Кнопки плагинов в верхней панели и поле ввода (`TopBarWidget`,
  `HistoryWidget::moveFieldControls`).
- Сторожевой таймер для зависших скриптов (`QJSEngine::setInterrupted` из
  другого потока).
- Установка плагина из файла, присланного в чате (заглушка уже есть в
  `ayu/ui/boxes/plugin_info_box.cpp`).
- Сборка на Windows: длинный raw-string в `plugin_prelude.cpp` упрётся в лимит
  MSVC в 16 КБ, его нужно разбить на части; для статической сборки Qt нужно
  подключить `Qt6Qml`.
