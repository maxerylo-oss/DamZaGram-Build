# Справочник API плагинов DamZaGram

Полный список того, что есть у плагина: каждый метод, поле, событие и тип данных API версии 2. Справочник **собран автоматически** из [`types/*.d.ts`](types/index.d.ts) скриптом `tools/damza/gen_reference.py`, поэтому совпадает с подсказками VS Code. Объяснения и примеры — в [гайдах](README.md#гайды), [рецептах](cookbook.md) и страницах модулей `api/*.md`.

Условные обозначения: `?` — необязательный параметр или поле; `PeerId | Peer` — можно передать id чата (строку) или сам объект; `Promise<T>` — асинхронный метод, используй `await`. Автор DamZaGram — [@wetakush](https://t.me/wetakush).

## Содержание

- [Глобальные функции](#глобальные-функции)
- [`dzg`](#dzg)
- [`dzg.plugin`](#dzgplugin)
- [`dzg.app`](#dzgapp)
- [`dzg.storage`](#dzgstorage)
- [`dzg.settings`](#dzgsettings)
- [`dzg.ui`](#dzgui)
- [`dzg.ui.compose`](#dzguicompose)
- [`dzg.theme`](#dzgtheme)
- [`dzg.chats`](#dzgchats)
- [`dzg.messages`](#dzgmessages)
- [`dzg.commands`](#dzgcommands)
- [`dzg.http`](#dzghttp)
- [`dzg.fs`](#dzgfs)
- [`dzg.clipboard`](#dzgclipboard)
- [`dzg.users`](#dzgusers)
- [`dzg.accounts`](#dzgaccounts)
- [`dzg.api`](#dzgapi)
- [`dzg.plugins`](#dzgplugins)
- [`dzg.builtin`](#dzgbuiltin)
- [События](#события)
- [Типы данных](#типы-данных)
- [Псевдонимы типов](#псевдонимы-типов)

## Глобальные функции

Есть в любом плагине без импорта, как в браузере.

- `console.log(...args: unknown[]): void` — Строка в лог плагина (уровень info).
- `console.info(...args: unknown[]): void` — То же, что log.
- `console.debug(...args: unknown[]): void` — Отладочная строка.
- `console.warn(...args: unknown[]): void` — Предупреждение.
- `console.error(...args: unknown[]): void` — Ошибка: в режиме разработчика ещё и тост.
- `setTimeout(fn: (...args: any[]) => void, ms?: number, ...args: any[]): number` — Вызвать fn через ms миллисекунд. Возвращает id таймера.
- `setInterval(fn: (...args: any[]) => void, ms?: number, ...args: any[]): number` — Вызывать fn каждые ms миллисекунд.
- `clearTimeout(id: number): void` — Отменить setTimeout.
- `clearInterval(id: number): void` — Отменить setInterval.
- `queueMicrotask(fn: () => void): void` — Выполнить fn сразу после текущего кода (микрозадача).
- `globalThis`, `Promise`, `Map`, `Set`, `BigInt`, `atob`/`btoa`, `performance.now()` — стандартная библиотека ES2023 (QuickJS-ng). `Intl`, `TextEncoder`, `fetch`, `structuredClone` нет — см. [окружение JS](README.md#окружение-javascript).

## `dzg`

Тип `Dzg`. Гайд: [guide/01-first-plugin.md](guide/01-first-plugin.md).

- `readonly version: 2` — Версия API плагинов (2).
- [`dzg.plugin`](#dzgplugin) — Информация о плагине.
- [`dzg.app`](#dzgapp) — Информация о клиенте и модуль app.
- `on<K extends keyof EventMap>(event: K, handler: (data: EventMap[K]) => void): () => void` — Подписаться на событие; возвращает функцию отписки. Клиент присылает плагину только события, на которые есть обработчик. События сообщений, реакций, статусов, «печатает…», непрочитанных, фокуса, поля ввода и plugins.changed приходят на следующей итерации цикла событий; ready, unload, session.ready, chat.opened, settings.changed и theme.changed — сразу. Каждый вызов обработчика должен уложиться в 3 секунды.
- `off<K extends keyof EventMap>(event: K, handler: (data: EventMap[K]) => void): void` — Отписать обработчик.
- `once<K extends keyof EventMap>(event: K, handler: (data: EventMap[K]) => void): () => void` — Подписаться на одно срабатывание.
- `log(...args: unknown[]): void` — То же, что console.log.
- `require<T = any>(path: string): T` — CommonJS-модуль из папки плагина: `module.exports` файла, путь от папки плагина. Модуль выполняется один раз и кешируется. В ES-модулях удобнее `import`.
- [`dzg.storage`](#dzgstorage) — Постоянное хранилище плагина: `tdata/plugins_data/<id>.json`.
- [`dzg.settings`](#dzgsettings) — Настройки плагина из схемы `settings` в manifest.json.
- [`dzg.ui`](#dzgui) — Интерфейс: тосты, окна, меню, кнопки, панели, виджеты.
- [`dzg.theme`](#dzgtheme) — Цвета палитры на лету.
- [`dzg.chats`](#dzgchats) — Чаты, папки, участники.
- [`dzg.messages`](#dzgmessages) — Сообщения: отправка, история, поиск, фильтры, хуки.
- [`dzg.commands`](#dzgcommands) — Слеш-команды.
- [`dzg.http`](#dzghttp) — HTTP-запросы.
- [`dzg.fs`](#dzgfs) — Файлы.
- [`dzg.clipboard`](#dzgclipboard) — Буфер обмена.
- `openUrl(url: string): void` — Открыть ссылку в браузере ОС.
- [`dzg.users`](#dzgusers) — Пользователи и контакты.
- [`dzg.accounts`](#dzgaccounts) — Аккаунты, в которые выполнен вход.
- [`dzg.api`](#dzgapi) — Сырой MTProto: любой метод Telegram API.
- [`dzg.plugins`](#dzgplugins) — Управление плагинами.
- [`dzg.builtin`](#dzgbuiltin) — Встроенные плагины.

## `dzg.plugin`

Тип `PluginInfo`. Гайд: [README.md](README.md#анатомия-плагина).

- `id: string` — id плагина из manifest.json.
- `name: string` — Название плагина.
- `version: string` — Версия плагина.
- `author: string` — Автор плагина.
- `description: string` — Описание плагина.
- `dir: string` — Папка плагина (с завершающим слешем не гарантируется).
- `dataDir: string` — Папка для данных плагинов, с завершающим слешем.

## `dzg.app`

Тип `AppInfo`. Гайд: [guide/05-ui.md](guide/05-ui.md#тема-и-ночной-режим).

- `name: 'DamZaGram'` — Всегда 'DamZaGram'.
- `version: string` — Версия клиента, например '5.16.4'.
- `apiVersion: number` — Версия API плагинов (2).
- `platform: string` — Платформа Qt: 'windows', 'macos', 'arch', 'ubuntu'...
- `os: string` — Название ОС для людей.
- `pluginsPath: string` — Папка плагинов, с завершающим слешем.
- `devMode: boolean` — Включена горячая перезагрузка (режим разработчика): ошибки плагинов показываются тостом.
- `language(): AppLanguage` — Текущий язык интерфейса.
- `isNightMode(): boolean` — Включён ли ночной режим (тёмная тема).
- `setNightMode(night: boolean): void` — Включить или выключить ночной режим. Переключение происходит чуть позже (после текущего обработчика), затем приходит 'theme.changed'.
- `restart(): void` — Перезапустить клиент.

## `dzg.storage`

Тип `DzgStorage`. Гайд: [guide/07-network-and-storage.md](guide/07-network-and-storage.md#хранилище-dzgstorage).

- `get<T = unknown>(key: string, fallback?: T): T` — Значение по ключу или fallback.
- `set(key: string, value: unknown): void` — Записать значение, сразу сохраняется на диск. Значение должно сериализоваться в JSON. Циклическая ссылка сохранится как null; слишком большое значение (около 4 млн элементов) — RangeError.
- `remove(key: string): void` — Удалить ключ.
- `keys(): string[]` — Все ключи.

## `dzg.settings`

Тип `DzgSettings`. Гайд: [guide/07-network-and-storage.md](guide/07-network-and-storage.md#настройки-dzgsettings).

- `get<T = unknown>(key: string): T` — Значение настройки (или default из схемы).
- `all(): Record<string, unknown>` — Все настройки объектом.
- `set(key: string, value: unknown): void` — Записать настройку (сохраняется, приходит settings.changed).

## `dzg.ui`

Тип `DzgUi`. Гайд: [guide/05-ui.md](guide/05-ui.md), подробно: [api/ui.md](api/ui.md).

- `toast(text: string, title?: string): void` — Короткое всплывающее сообщение внизу окна.
- `box(spec: BoxSpec): Promise<BoxResult>` — Окно с полями, чекбоксами и кнопками.
- `alert(text: string, title?: string): Promise<void>` — Окно с текстом и кнопкой OK.
- `confirm(text: string, options?: { title?: string; confirmText?: string; cancelText?: string; danger?: boolean }): Promise<boolean>` — Окно «Да / Отмена». true, если нажали кнопку подтверждения. danger — красная кнопка.
- `prompt(title: string, options?: { text?: string; placeholder?: string; value?: string; multiline?: boolean; confirmText?: string; cancelText?: string }): Promise<string | null>` — Окно с полем ввода. null, если пользователь отменил.
- `addMessageMenuItem(item: MenuItem<Message>): () => void` — Пункт в контекстном меню сообщения. Возвращает функцию, которая убирает пункт.
- `addChatMenuItem(item: MenuItem<Peer>): () => void` — Пункт в контекстном меню чата (список чатов и шапка). Возвращает функцию, которая убирает пункт.
- `mainWindow(): Widget | null` — Корневой виджет главного окна.
- `focused(): Widget | null` — Виджет с фокусом клавиатуры.
- `find(className?: string, name?: string): Widget[]` — Найти виджеты по C++ классу и objectName. Поиск по всем окнам приложения.
- `create(type: WidgetType, parent: Widget | null, props?: CreateProps): Widget | null` — Создать свой виджет: label, button, input, checkbox, panel. parent: null — главное окно.
- `repaint(): void` — Перерисовать все окна клиента.
- `icons(): DzgIconName[]` — Имена иконок для опции icon.
- `addTopBarButton(spec: TopBarButtonSpec): () => void` — Кнопка в шапке чата. Возвращает функцию, которая её убирает.
- `addMainMenuItem(spec: MainMenuItemSpec): () => void` — Пункт главного меню (☰). Возвращает функцию, которая его убирает.
- `addShortcut(keys: string, handler: () => void): () => void` — Горячая клавиша на всё приложение, синтаксис Qt: 'Ctrl+Shift+K', 'Alt+F1', 'F8'. Только одно сочетание (без аккордов 'Ctrl+Shift+K, Ctrl+C') — иначе исключение. Без Ctrl/Alt/Meta и не F-клавиша — не срабатывает, пока фокус в текстовом поле. Сочетание плагина важнее сочетаний клиента.
- `notify(spec: NotifySpec | string): void` — Уведомление поверх окна клиента. Строка — то же, что { text }.
- `openLink(url: string): void` — Ссылка через клиент: t.me/..., tg://..., обычные — в браузере.
- `refreshButtons(): void` — Пересчитать when() у кнопок в шапке, если условие изменилось.
- [`dzg.ui.compose`](#dzguicompose) — Поле ввода сообщения.
- `panel(options?: PanelOptions): Panel` — Открыть (или вернуть уже открытую) боковую панель плагина.

## `dzg.ui.compose`

Тип `DzgCompose`. Гайд: [guide/05-ui.md](guide/05-ui.md#поле-ввода), подробно: [api/ui.md](api/ui.md#поле-ввода).

- `get(): ComposeState | null` — Текущее поле ввода: в фокусе, иначе последнее, где был фокус, в активном окне. null — поля нет.
- `set(text: string): boolean` — Заменить весь текст поля. Курсор — в конец. false — поля нет.
- `insert(text: string): boolean` — Вставить текст в позицию курсора (заменяет выделение).
- `focus(): void` — Активировать окно и поставить фокус в поле.
- `addButton(spec: ComposeButtonSpec): () => void` — Кнопка рядом с полем ввода во всех чатах, темах и ответах.

## `dzg.theme`

Тип `DzgTheme`. Гайд: [guide/05-ui.md](guide/05-ui.md#тема-и-ночной-режим).

- `set(name: string, color: string): boolean` — Имя цвета палитры Telegram (windowBg, msgOutBg, ...). Цвет: #RRGGBB или #AARRGGBB.
- `setMany(colors: Record<string, string>): boolean` — Поменять несколько цветов сразу: { windowBg: '#101010', ... }.
- `reset(): void` — Убрать цвета, заданные этим плагином. Цвета других плагинов остаются. Вызывается сам при выгрузке.
- `get(name: string): string | null` — Текущий цвет палитры: '#rrggbb' или '#aarrggbb'; null для неизвестного имени.

## `dzg.chats`

Тип `DzgChats`. Гайд: [guide/04-chats-and-users.md](guide/04-chats-and-users.md), подробно: [api/chats.md](api/chats.md).

- `current(): Peer | null` — Открытый сейчас чат или null.
- `get(peerId: PeerId | Peer): Peer | null` — Peer по id или null.
- `find(username: string): Peer | null` — Чат по юзернейму. Ищет только среди уже загруженных клиентом чатов.
- `open(peerId: PeerId | Peer, messageId?: number): boolean` — Открыть чат (и прокрутить к сообщению).
- `readonly typingActions: readonly TypingAction[]` — Все действия, которые принимает setTyping.
- `list(options?: ChatListOptions): ChatListEntry[]` — Чаты, которые клиент уже загрузил, в том порядке, как в списке слева (закреплённые сверху). Запросов к серверу нет.
- `entry(peer: PeerId | Peer): ChatListEntry | null` — Состояние одного чата: счётчики, mute, архив. null, если чат неизвестен.
- `folders(): ChatFolder[]` — Папки чатов пользователя (без «Все чаты»).
- `resolve(username: string): Promise<Peer | null>` — Найти чат или пользователя на сервере. Понимает 'durov', '@durov', 't.me/durov', 'tg://resolve?domain=durov' и номера телефонов ('+79991234567'). null, если никого нет.
- `join(usernameOrInviteLink: string | Peer): Promise<Peer>` — Вступить в группу или канал по юзернейму, ссылке t.me/... или приглашению (t.me/+hash, t.me/joinchat/hash, tg://join?invite=hash). Можно передать и Peer уже известного канала. Если ты уже участник — просто вернёт чат. Ошибки — коды сервера: 'INVITE_HASH_EXPIRED', 'INVITE_REQUEST_SENT', 'CHANNELS_TOO_MUCH', 'JOIN_VERIFICATION_REQUIRED' и т.п.
- `leave(peer: PeerId | Peer): Promise<void>` — Выйти из группы или канала. История чата не удаляется.
- `mute(peer: PeerId | Peer, seconds?: number | boolean): boolean` — Выключить уведомления. seconds: число секунд; true, 0 или не задано — навсегда; false — включить обратно.
- `unmute(peer: PeerId | Peer): boolean` — То же, что mute(peer, false).
- `archive(peer: PeerId | Peer, archived?: boolean): boolean` — Переместить в архив (по умолчанию) или вернуть из него.
- `pin(peer: PeerId | Peer, pinned?: boolean, options?: { folder?: number }): boolean` — Закрепить или открепить чат. По умолчанию в основном списке, { folder } — в папке. При превышении лимита клиент покажет своё окно. false, если нет открытого окна аккаунта.
- `markUnread(peer: PeerId | Peer, unread?: boolean): boolean` — true — пометить непрочитанным, false — прочитать всё.
- `markRead(peer: PeerId | Peer): boolean` — Прочитать все сообщения чата (то же, что markUnread(peer, false)).
- `setTyping(peer: PeerId | Peer, action?: TypingAction, options?: { topicId?: number }): boolean` — Показать собеседникам статус «печатает…» и т.п. Один вызов держит статус около 5 секунд, для долгого статуса повторяй раз в 4–5 секунд. 'cancel' снимает статус. Уважает режим призрака AyuGram. Неизвестное действие бросает TypeError.
- `members(peer: PeerId | Peer, options?: ChatMembersOptions): Promise<ChatMember[]>` — Участники группы или канала с сервера (в канале нужны права админа).
- `info(peer: PeerId | Peer): Promise<ChatInfo>` — Полная информация о чате, канале или пользователе с сервера.
- `create(options: ChatCreateOptions | string): Promise<Peer>` — Создать группу, супергруппу или канал. Строка — название обычной группы.
- `addMembers(peer: PeerId | Peer, users: PeerId | Peer | (PeerId | Peer)[]): Promise<{ added: number; missing: PeerId[] }>` — Пригласить пользователей. missing — id тех, кого пригласить не вышло (запретили приглашать себя в настройках приватности и т.п.).
- `kick(peer: PeerId | Peer, user: PeerId | Peer, options?: { ban?: boolean }): Promise<void>` — Удалить участника. { ban: true } — ещё и заблокировать в чате (только супергруппы и каналы).
- `setTitle(peer: PeerId | Peer, title: string): Promise<void>` — Переименовать группу или канал.
- `setAbout(peer: PeerId | Peer, about: string): Promise<void>` — Поменять описание группы или канала.
- `createInviteLink(peer: PeerId | Peer, options?: InviteLinkOptions): Promise<string>` — Создать новую ссылку-приглашение. Возвращает ссылку.
- `clearHistory(peer: PeerId | Peer, options?: { revoke?: boolean }): boolean` — Очистить историю. { revoke: true } — удалить и у собеседника (где это возможно).

## `dzg.messages`

Тип `DzgMessages`. Гайд: [guide/03-messages.md](guide/03-messages.md), подробно: [api/messages.md](api/messages.md).

- `delete(peerId: PeerId | Peer, ids: number | number[], options?: { revoke?: boolean }): boolean` — Удалить сообщения (revoke: true — и у собеседника). Локально удаляет сразу после текущего обработчика, а не внутри вызова.
- `get(peerId: PeerId | Peer, messageId: number): Message | null` — Сообщение из памяти клиента или null.
- `loaded(peerId: PeerId | Peer, limit?: number): Message[]` — Сообщения из памяти клиента. Последние сообщения, которые клиент уже загрузил в память. Запросов к серверу нет.
- `addFilter(filter: (message: Message) => boolean): () => void` — Скрыть сообщения из ленты. Вернуть true — скрыть сообщение. Вызывается часто: фильтр должен быть быстрым.
- `onBeforeSend(hook: BeforeSendHook): () => void` — Хук перед отправкой: переписать или отменить исходящее сообщение. Возвращает функцию отписки.
- `refilter(): void` — Пересчитать фильтры, например после смены настроек.
- `send(peer: PeerId | Peer, text: string, options?: MessagesSendOptions): boolean` — Отправить текст (с Markdown/HTML, ответом, отложенно...). true — сообщение принято к отправке.
- `edit(peer: PeerId | Peer, messageId: number, text: string, options?: MessagesEditOptions): boolean` — Изменить текст. Возвращает false, если сообщение не найдено, ещё не отправлено или это предложенный пост, ждущий ответа.
- `parse(text: string, mode?: TextParseMode): FormattedText` — Разобрать Markdown (по умолчанию) или HTML в текст и форматирование.
- `format(message: FormattedText | Message, mode?: 'html' | 'markdown' | 'md'): string` — Обратно: текст и форматирование в HTML (по умолчанию) или Markdown.
- `format(text: string, mode: 'html' | 'markdown' | 'md', entities: MessageEntity[]): string` — вариант вызова.
- `history(peer: PeerId | Peer, options?: MessagesHistoryOptions): Promise<Message[]>` — История с сервера, от старых к новым.
- `iterate(peer: PeerId | Peer, options?: MessagesIterateOptions): AsyncGenerator<Message, void, void>` — Вся история страницами, от новых к старым: for await (const m of dzg.messages.iterate(chat)).
- `search(peer: PeerId | Peer | null, query: string, options?: MessagesSearchOptions): Promise<Message[]>` — Поиск в чате или по всем чатам (peer = null). Результат от старых к новым.
- `fetch(peer: PeerId | Peer, messageId: number): Promise<Message | null>` — Сообщение с сервера, даже если клиент его не загружал.
- `fetch(peer: PeerId | Peer, messageIds: number[]): Promise<Message[]>` — вариант вызова.
- `forward(fromPeer: PeerId | Peer, ids: number | Message | (number | Message)[], toPeer: PeerId | Peer, options?: MessagesForwardOptions): Promise<void>` — Переслать одно или несколько сообщений в другой чат. Служебные (и звонки) и неотправленные пропускаются; если переслать нечего — MESSAGE_NOT_FORWARDABLE.
- `react(peer: PeerId | Peer, messageId: number, emoji: string | null, options?: { addToRecent?: boolean }): Promise<void>` — Поставить реакцию. null — убрать свои реакции. Премиум-эмодзи — строкой с id.
- `reactions(peer: PeerId | Peer, messageId: number): string[]` — Реакции, которые можно поставить на сообщение (если оно загружено).
- `pin(peer: PeerId | Peer, messageId: number, options?: { silent?: boolean; unpin?: boolean; oneSide?: boolean }): Promise<void>` — Закрепить сообщение ({ unpin: true } — открепить).
- `unpin(peer: PeerId | Peer, messageId: number): Promise<void>` — Открепить сообщение.
- `markRead(peer: PeerId | Peer, options?: { tillId?: number }): boolean` — Прочитать чат (до конца или до tillId). Учитывает режим призрака AyuGram.
- `sendFile(peer: PeerId | Peer, path: string | string[], options?: MessagesSendFileOptions): boolean` — Отправить файл или несколько файлов (пути относительно папки плагина или абсолютные).
- `download(peer: PeerId | Peer, messageId: number, path?: string): Promise<string>` — Скачать файл или фото из сообщения. Без path — в папку загрузок клиента. Можно передать папку.
- `link(peer: PeerId | Peer, messageId: number, options?: { thread?: boolean; private?: boolean }): string | null` — Ссылка на сообщение (t.me/...). null для личных чатов и обычных групп.
- `clickButton(peer: PeerId | Peer, messageId: number, row: number, column: number, options?: { open?: boolean }): Promise<MessagesButtonAnswer>` — Нажать кнопку бота по позиции (строка, столбец) или по тексту.
- `clickButton(peer: PeerId | Peer, messageId: number, text: string, options?: { open?: boolean }): Promise<MessagesButtonAnswer>` — вариант вызова.
- `addRenderTransform(transform: RenderTransform): () => void` — Подменить текст сообщений при отрисовке. Результат кешируется для каждого сообщения, функция вызывается снова, когда текст меняется или когда трансформации добавляются/убираются.
- `refreshRender(): void` — Сбросить кеш и перерисовать: например, после смены настроек плагина.

## `dzg.commands`

Тип `DzgCommands`. Гайд: [guide/06-commands-and-hooks.md](guide/06-commands-and-hooks.md), подробно: [api/builtin.md](api/builtin.md).

- `register(name: string, spec: CommandSpec | CommandSpec['run']): () => void` — Зарегистрировать слеш-команду. Команда срабатывает, когда пользователь отправляет "/name ...". Сообщение не уходит.
- `list(): { name: string; description: string }[]` — Команды этого плагина.
- `all(): PluginCommandInfo[]` — Команды всех загруженных плагинов, включая свои.
- `run(text: string, chat?: Peer | PeerId): boolean` — Выполнить команду любого плагина так, будто пользователь отправил её в чат. text: '/digest 50' или 'digest 50'. chat: чат-контекст, по умолчанию текущий. false — такой команды нет или её плагин сейчас занят.

## `dzg.http`

Тип `DzgHttp`. Гайд: [guide/07-network-and-storage.md](guide/07-network-and-storage.md#http).

- `fetch(url: string, options?: FetchOptions): Promise<FetchResponse>` — HTTP-запрос. Промис отклоняется только при сетевой ошибке; 404 и 500 — обычный ответ с ok = false.

## `dzg.fs`

Тип `DzgFs`. Гайд: [guide/07-network-and-storage.md](guide/07-network-and-storage.md#файлы-dzgfs).

- `read(path: string): string | null` — null, если файла нет. Относительный путь — от папки плагина.
- `write(path: string, text: string): boolean` — Записать текст в UTF-8, создав папки. Запись в папку своего плагина не вызывает горячую перезагрузку, но данные лучше держать в dzg.plugin.dataDir.
- `list(path: string): string[]` — Имена файлов и папок внутри папки.
- `exists(path: string): boolean` — Есть ли файл или папка.

## `dzg.clipboard`

Тип `DzgClipboard`. Гайд: [guide/07-network-and-storage.md](guide/07-network-and-storage.md#буфер-обмена-и-ссылки).

- `get(): string` — Текст из буфера обмена.
- `set(text: string): void` — Положить текст в буфер обмена.

## `dzg.users`

Тип `DzgUsers`. Гайд: [guide/04-chats-and-users.md](guide/04-chats-and-users.md#пользователи), подробно: [api/users.md](api/users.md).

- `self(): Peer | null` — Ты сам (активный аккаунт). null до входа в аккаунт.
- `get(peer: PeerId | Peer): Peer | null` — Peer по id. То же, что dzg.chats.get.
- `status(peer: PeerId | Peer): UserStatus | null` — Текущий статус из памяти клиента, без запроса. null, если это не пользователь.
- `isBlocked(peer: PeerId | Peer): boolean` — Заблокирован ли пользователь (по данным клиента).
- `info(peer: PeerId | Peer): Promise<UserInfo>` — Полная информация с сервера (users.getFullUser).
- `contacts(): Peer[]` — Контакты, которые загрузил клиент.
- `block(peer: PeerId | Peer, blocked?: boolean): Promise<void>` — Заблокировать (по умолчанию) или разблокировать.
- `unblock(peer: PeerId | Peer): Promise<void>` — То же, что block(peer, false).
- `commonChats(peer: PeerId | Peer, options?: { limit?: number }): Promise<Peer[]>` — Общие группы и каналы, до 100.
- `addContact(peer: PeerId | Peer, options?: { firstName?: string; lastName?: string; phone?: string; sharePhone?: boolean }): Promise<void>` — Добавить в контакты или переименовать контакт. Пустые поля берутся из профиля. sharePhone — показать этому человеку свой номер.
- `deleteContact(peer: PeerId | Peer): Promise<void>` — Удалить из контактов.

## `dzg.accounts`

Тип `DzgAccounts`. Гайд: [guide/04-chats-and-users.md](guide/04-chats-and-users.md#несколько-аккаунтов), подробно: [api/users.md](api/users.md#dzgaccounts).

- `list(): AccountEntry[]` — Аккаунты, в которые выполнен вход.
- `current(): AccountEntry | null` — Активный аккаунт.
- `switchTo(index: number | AccountEntry): boolean` — Переключиться на аккаунт. Переключение происходит чуть позже, после него придёт событие 'session.ready' (если аккаунт открыт впервые) и API начнёт работать с новым аккаунтом. false, если аккаунта с таким index нет.

## `dzg.api`

Тип `DzgApi`. Гайд: [guide/08-raw-mtproto.md](guide/08-raw-mtproto.md), подробно: [api/mtproto.md](api/mtproto.md).

- `call<T = any>(method: string, params?: Record<string, any>, options?: ApiCallOptions): Promise<T>` — Вызывает любой метод Telegram API: `dzg.api.call('messages.getHistory', {...})`. Поля — как в api.tl; `random_id` и `hash` можно не передавать — подставятся сами (случайный id и 0). Там, где ждут InputPeer / InputUser / InputChannel, можно передать Peer, Message, id чата или 'me'. Промис отклоняется с ApiError ('400: PEER_ID_INVALID').
- `inputPeer(peer: TLInputPeerLike): TLObject | null` — InputPeer для чата по известному клиенту access_hash; null, если клиент его не знает.
- `inputUser(peer: TLInputPeerLike): TLObject | null` — InputUser; null, если это не пользователь или он неизвестен. 'me' -> inputUserSelf.
- `inputChannel(peer: TLInputPeerLike): TLObject | null` — InputChannel; null, если это не канал / супергруппа.
- `peerId(peer: TLObject | null | undefined): PeerId | null` — TL-peer -> id чата DamZaGram (PeerId) для dzg.chats.get / dzg.messages и т. п. Понимает peerUser / peerChat / peerChannel, inputPeer*, inputUser, inputChannel, а также объекты user / chat / channel из ответов. null для остального.
- `onUpdate(type: string | string[] | '*', fn: (update: TLObject, meta: ApiUpdateMeta) => void): () => void` — Сырые обновления от сервера (объекты типа Update): 'updateUserTyping', 'updateNewMessage'... '*' — все. Можно передать массив типов. Бросает TypeError для неизвестного типа или не-Update (например 'updateShortMessage'). Возвращает функцию отписки.
- `schema(name: string): TLSchemaInfo | null` — Описание метода или конструктора из схемы; принимает и id ('0x4423e6c5'). null, если не найден.
- `constructors(type: string): string[]` — Имена конструкторов типа: constructors('InputPeer') -> ['inputPeerEmpty', 'inputPeerSelf', ...].
- `search(query: string, limit?: number): string[]` — Поиск по именам методов и конструкторов (без учёта регистра).
- `encode(value: any, type?: string): string` — TL-объект -> base64 его бинарного представления. type по умолчанию 'Object' (любой конструктор). Бросает Error.
- `decode(data: string | Uint8Array | ArrayBuffer | number[], type?: string): any` — base64 / байты -> TL-объект. type: 'Object', `'Vector<long>'`, 'Message'... Бросает Error.
- `toBase64(data: string | Uint8Array | ArrayBuffer | number[]): string` — Байты или строка (как UTF-8) -> base64.
- `fromBase64(base64: string): Uint8Array` — base64 (и base64url) -> байты, или строка при as = 'text'.
- `fromBase64(base64: string, as: 'text'): string` — вариант вызова.
- `readonly layer: number` — Слой (layer) схемы, на котором работает клиент.

## `dzg.plugins`

Тип `DzgPlugins`. Гайд: [guide/10-publishing.md](guide/10-publishing.md), подробно: [api/plugins.md](api/plugins.md).

- `readonly path: string` — Папка плагинов: `<папка данных>/tdata/plugins/`.
- `list(): InstalledPlugin[]` — Все установленные плагины, включая выключенные.
- `get(id?: string): InstalledPlugin | null` — Плагин по id (по умолчанию — текущий) или null.
- `setEnabled(id: string, enabled: boolean): boolean` — Включить или выключить плагин. Применяется сразу после текущего обработчика (не синхронно): менеджер не грузит и не выгружает плагины, пока на стеке JS любого плагина. false — нет такого плагина.
- `enable(id: string): boolean` — То же, что setEnabled(id, true).
- `disable(id: string): boolean` — То же, что setEnabled(id, false).
- `reload(id?: string): boolean` — Перезагрузить плагин (по умолчанию — текущий) после текущего обработчика.
- `reloadAll(): boolean` — Перечитать папку плагинов и перезапустить все плагины (после текущего обработчика).
- `log(id?: string, limit?: number): string[]` — Последние строки лога плагина (по умолчанию 100). Пусто, если не загружен.
- `settings(id?: string): Record<string, unknown>` — Текущие значения настроек плагина (с учётом значений по умолчанию).
- `setSetting(id: string, key: string, value: unknown): boolean` — Поменять настройку другого плагина; он получит событие settings.changed.
- `evaluate(id: string, code: string): string` — Выполнить код в глобальной области другого загруженного плагина. Возвращает результат текстом (JSON, если получается), бросает Error.
- `inspect(source: string): Promise<PluginPackageInfo>` — Прочитать пакет по ссылке или пути без установки. Принимает https-ссылки на .zip/.js, страницы файлов и репозитории GitHub, file:// и абсолютные пути (папка, .zip, .js).
- `install(source: string, options?: { enable?: boolean }): Promise<string>` — Тихо установить или обновить плагин (без окна подтверждения) и вернуть его id. Новый плагин ставится без перезапуска остальных; обновление перезапускает только обновлённый плагин (если это вызвавший плагин, промис не дождётся ответа). Для магазинов плагинов удобнее promptInstall.
- `promptInstall(source: string): Promise<void>` — Скачать пакет и показать пользователю окно установки (манифест, предупреждение о полном доступе). Промис выполняется, когда окно показано.
- `uninstall(id: string, options?: { removeData?: boolean }): Promise<void>` — Удалить плагин; остальные продолжают работать. Встроенные только выключаются. removeData: стереть storage и забыть настройки и состояние.
- `openManager(): void` — Открыть раздел «Плагины» в настройках.
- `openDevTools(id?: string): void` — Открыть окно DevTools (консоль + живой лог) для плагина.

## `dzg.builtin`

Тип `DzgBuiltin`. Подробно: [api/builtin.md](api/builtin.md).

- `list(): BuiltinPluginInfo[]` — Встроенные плагины и их состояние после установки на старте.

## События

Подписка: `dzg.on(name, (data) => { ... })`, отписка — функция, которую вернул `on`. Подробно: [гайд по событиям](guide/02-events.md).

- `ready` → `void` — Плагин загружен и готов. Срабатывает и после горячей перезагрузки.
- `unload` → `void` — Плагин выгружается: выключен, перезагружен или клиент закрывается (окна и аккаунты ещё доступны). Регистрации, таймеры и цвета dzg.theme клиент уберёт сам.
- `session.ready` → `Peer` — Аккаунт авторизован и его данные доступны.
- `message.new` → `Message` — Новое сообщение (входящее или исходящее) появилось в любом чате любого аккаунта (см. Message.selfId). Приходит на следующей итерации цикла событий, а не внутри уведомления клиента.
- `message.edited` → `Message` — Сообщение изменилось. Приходит на следующей итерации цикла событий.
- `message.deleted` → `Message` — Сервер сообщил об удалении. Данные — снимок на момент удаления, сам обработчик вызывается на следующей итерации: без сохранения удалённых AyuGram dzg.messages.get его уже может не найти.
- `chat.opened` → `Peer` — Пользователь открыл чат.
- `settings.changed` → `{ key: string; value: unknown }` — Пользователь поменял настройку плагина в окне настроек.
- `theme.changed` → `{ night: boolean }` — Сменилась палитра: включили или выключили ночной режим, применили другую тему. Цвета плагинов (dzg.theme) клиент сам наложит на новую палитру на следующей итерации цикла событий.
- `message.reaction` → `Message` — Кто-то поставил реакцию на твоё сообщение (новая непрочитанная реакция). Приходит на следующей итерации цикла событий.
- `chat.typing` → `{ chat: Peer; user: Peer; action: TypingAction; topicId: number }` — Кто-то печатает, записывает голосовое, выбирает стикер... Повторы одного действия приходят не чаще раза в 4 секунды. Когда действие закончилось (явно или по таймауту ~6.5 с), приходит action 'cancel'. Только для активного аккаунта.
- `chat.unread` → `{ chat: Peer; unread: number; unreadMark: boolean; mentions: number }` — Изменилось число непрочитанных в чате или пометка «непрочитано». Изменения собираются пачкой за 300 мс, одинаковые значения не повторяются.
- `user.status` → `{ user: Peer; online: boolean; lastSeen: number; kind: UserStatusKind }` — Пользователь появился в сети или вышел из неё (только активный аккаунт). Повторные одинаковые статусы отбрасываются. Первые 15 секунд после входа в аккаунт приходят только изменения уже известных статусов, чтобы загрузка списка чатов не завалила плагин событиями.
- `app.focus` → `{ active: boolean }` — Окно клиента стало активным или неактивным.
- `compose.changed` → `{ chat: Peer | null; text: string }` — Текст в поле ввода изменился. Не чаще раза в ~300 мс.
- `plugins.changed` → `{ plugins: InstalledPlugin[] }` — Список плагинов изменился: установили, удалили, включили, выключили, обновили версию. Изменения только текста ошибки не считаются.

## Типы данных

Объекты, которые API принимает и возвращает. Одноимённые интерфейсы из разных файлов слиты, как это делает TypeScript.

### `Peer`

- `id: PeerId` — Внутренний id DamZaGram (с битами типа). Используй его во всех вызовах API.
- `tgId: number` — Голый Telegram id (как в ботах и t.me/c/...).
- `name: string` — Имя пользователя или название чата.
- `username: string` — Юзернейм без @ или ''.
- `type: 'user' | 'bot' | 'group' | 'channel' | 'unknown'` — Тип чата.
- `isSelf: boolean` — Это ты сам («Избранное»).

### `Message`

- `id: number` — id сообщения в чате.
- `peerId: PeerId` — id чата, в котором сообщение.
- `chat: Peer` — Чат сообщения.
- `fromId: PeerId` — id отправителя (в каналах — сам канал).
- `fromName: string` — Имя отправителя.
- `text: string` — Текст или подпись к медиа, без форматирования.
- `date: number` — Unix time, секунды.
- `out: boolean` — Исходящее (отправил ты).
- `service: boolean` — Сервисное сообщение («X вступил в группу»).
- `post: boolean` — Пост в канале.
- `pinned: boolean` — Закреплено.
- `deleted: boolean` — true, если сообщение удалено на сервере, но сохранено AyuGram.
- `replyToId: number` — 0, если не ответ.
- `views: number` — Просмотры (для постов каналов).
- `media?: MediaType` — Тип вложения, если есть.
- `selfId: PeerId` — Аккаунт, которому пришло сообщение: id его Peer. События сообщений приходят от всех аккаунтов, а API работает от активного — прежде чем действовать, сравни с dzg.users.self().id.
- `entities: MessageEntity[]` — Форматирование текста.
- `editDate: number` — Дата последнего изменения, 0 — не редактировалось.
- `groupedId: string` — id альбома, '' — не в альбоме.
- `forwardFrom?: MessageForward` — Есть у пересланных сообщений.
- `mediaInfo?: MessageMediaInfo` — Подробности о файле или фото (если есть media).
- `buttons?: MessageButton[][]` — Кнопки бота: строки, в строке — кнопки слева направо.
- `reactions?: MessageReaction[]` — Реакции под сообщением.
- `topicId?: number` — id темы форума (только в форумах).

### `SendContext`

- `text: string` — Текст, который сейчас уйдёт (уже с правками предыдущих хуков).
- `peerId: PeerId` — id чата.
- `chat: Peer` — Чат, в который отправляют.
- `replyTo: number` — id сообщения, на которое отвечают, или 0.

### `CommandContext`

- `args: string[]` — Аргументы, разбитые по пробелам.
- `rest: string` — Всё после имени команды одной строкой.
- `text: string` — Весь текст сообщения с командой.
- `chat: Peer` — Чат, в котором набрали команду.
- `replyTo: number` — id сообщения, на которое отвечали, или 0.

### `CommandSpec`

- `description?: string` — Описание для командной палитры и dzg.commands.all().
- `run(ctx: CommandContext): void | Promise<void>` — Вызывается вместо отправки сообщения.

### `MenuItem<T>`

- `title: string | ((target: T) => string)` — Текст пункта или функция, которая строит его для конкретной цели.
- `when?: (target: T) => boolean` — Показывать пункт, только если вернула true.
- `onClick(target: T): void` — Клик по пункту.

### `BoxSpec`

- `title?: string` — Заголовок окна.
- `text?: string` — Текст под заголовком.
- `inputs?: { id: string; placeholder?: string; value?: string; multiline?: boolean }[]` — Поля ввода; значения придут в values[id].
- `toggles?: { id: string; text: string; checked?: boolean }[]` — Чекбоксы; значения придут в values[id].
- `buttons?: { id: string; text: string; danger?: boolean }[]` — По умолчанию одна кнопка OK.

### `BoxResult`

- `button: string` — id нажатой кнопки или 'cancel', если окно закрыли.
- `values: Record<string, string | boolean>` — Значения полей и чекбоксов по их id.

### `Rect`

- `x: number` — Левый край.
- `y: number` — Верхний край.
- `width: number` — Ширина.
- `height: number` — Высота.

### `WidgetDump`

- `className: string` — C++ имя класса.
- `name: string` — objectName виджета.
- `visible: boolean` — Виден ли виджет.
- `geometry: Rect` — Положение в родителе.
- `children?: WidgetDump[]` — Дочерние виджеты (до заданной глубины).

### `Widget`

Слабая ссылка на любой виджет клиента. Если виджет уже удалён, методы ничего не делают, valid() вернёт false.

- `readonly id: number` — Стабильный id виджета в рамках плагина.
- `on(event: string, handler: (widget: Widget, value?: unknown) => void): Widget` — Подписаться на событие. Подписаться на событие созданного виджета: click, change, submit, destroyed.
- `valid(): boolean` — Виджет ещё существует.
- `className(): string` — Настоящее C++ имя класса, например "HistoryView::TopBarWidget".
- `classChain(): string[]` — Имя класса и цепочка Qt-родителей.
- `name(): string` — objectName виджета.
- `setName(name: string): void` — Задать objectName (удобно для поиска через find).
- `geometry(): Rect` — Положение и размер в координатах родителя.
- `globalGeometry(): Rect` — Положение и размер в координатах экрана.
- `setGeometry(x: number, y: number, width: number, height: number): void` — Переместить и изменить размер.
- `move(x: number, y: number): void` — Переместить.
- `resize(width: number, height: number): void` — Изменить размер.
- `visible(): boolean` — Виден ли виджет.
- `setVisible(visible: boolean): void` — Показать или спрятать.
- `show(): void` — Показать.
- `hide(): void` — Спрятать.
- `raise(): void` — Поднять над соседями.
- `lower(): void` — Опустить под соседей.
- `update(): void` — Перерисовать.
- `setOpacity(opacity: number): void` — 0..1, 1 снимает эффект.
- `setEnabled(enabled: boolean): void` — Включить или выключить (серый, не реагирует на клики).
- `setFocus(): void` — Поставить фокус клавиатуры.
- `destroy(): void` — Удалить виджет. Чужие виджеты клиента так удалять опасно.
- `parentWidget(): Widget | null` — Родитель или null.
- `window(): Widget | null` — Окно верхнего уровня, в котором лежит виджет.
- `childWidgets(): Widget[]` — Прямые потомки.
- `find(className?: string, name?: string): Widget[]` — Найти потомков. Поиск среди всех потомков. className сравнивается с полным или коротким именем.
- `childAt(x: number, y: number): Widget | null` — Потомок в точке (локальные координаты).
- `prop(name: string): unknown` — Значение Qt-свойства (Q_PROPERTY) по имени.
- `setProp(name: string, value: unknown): boolean` — Записать Qt-свойство. false, если не вышло.
- `propNames(): string[]` — Имена Qt-свойств виджета.
- `click(x: number, y: number): void` — Синтетический клик левой кнопкой в локальных координатах.
- `typeText(text: string): void` — Напечатать текст, как с клавиатуры.
- `grab(path: string): boolean` — Сохранить скриншот виджета в PNG.
- `text(): string` — Для полей ввода, лейблов и кнопок.
- `setText(text: string): void` — Задать текст полю, лейблу или кнопке.
- `dump(depth: number): WidgetDump` — Дерево виджета до заданной глубины — для изучения интерфейса.

### `CreateProps`

- `x?: number` — Положение в родителе.
- `y?: number` — Положение в родителе.
- `width?: number` — Ширина.
- `height?: number` — Высота.
- `text?: string` — Текст лейбла, кнопки, поля или чекбокса.
- `name?: string` — objectName.
- `visible?: boolean` — По умолчанию true.
- `style?: 'light'` — button: 'light' — вторичная кнопка.
- `placeholder?: string` — input
- `multiline?: boolean` — input: многострочное поле.
- `checked?: boolean` — checkbox
- `color?: string` — panel: цвет #RRGGBB или #AARRGGBB и скругление.
- `radius?: number` — panel: скругление углов, px.
- `onClick?: (widget: Widget) => void` — button, panel: клик.
- `onChange?: (widget: Widget, value: string | boolean) => void` — input: текст; checkbox: состояние.
- `onSubmit?: (widget: Widget, value: string) => void` — input: Enter (Ctrl+Enter в многострочном поле).
- `on?: { click?: (widget: Widget) => void; change?: (widget: Widget, value: string | boolean) => void; submit?: (widget: Widget, value: string) => void }` — Те же обработчики одним объектом.

### `FetchOptions`

- `method?: string` — GET (по умолчанию), POST, PUT, DELETE, PATCH...
- `headers?: Record<string, string>` — Заголовки запроса.
- `body?: string` — Тело запроса (строка, уйдёт как UTF-8).
- `json?: unknown` — Отправить JSON: выставит Content-Type и сериализует объект.
- `timeout?: number` — Таймаут в мс, по умолчанию 30000.

### `FetchResponse`

- `status: number` — HTTP-код ответа.
- `ok: boolean` — status от 200 до 299.
- `url: string` — Итоговый адрес (после редиректов).
- `headers: Record<string, string>` — Имена заголовков в нижнем регистре.
- `text(): Promise<string>` — Тело ответа строкой (UTF-8).
- `json<T = any>(): Promise<T>` — Тело ответа, разобранное как JSON.

### `AppLanguage`

Язык интерфейса клиента.

- `id: string` — Код языка: 'ru', 'en', 'de'... Для своих языковых пакетов — их id.
- `baseId: string` — Базовый язык пакета ('' если нет): для 'ru-custom' — 'ru'.
- `name: string` — Название языка на нём самом: 'Русский', 'English'.

### `MessageEntity`

Кусок форматирования. offset и length — в UTF-16 (как String.length в JS).

- `type: MessageEntityType | string` — Тип сущности.
- `offset: number` — Начало (UTF-16).
- `length: number` — Длина (UTF-16).
- `url?: string` — textLink: адрес ссылки.
- `userId?: PeerId` — textMention: id пользователя (PeerId).
- `language?: string` — pre: язык блока кода.
- `documentId?: string` — customEmoji: id документа премиум-эмодзи.
- `collapsed?: boolean` — blockquote: свёрнутая (раскрывающаяся) цитата.

### `FormattedText`

Текст и форматирование — результат parse(), вход format().

- `text: string` — Текст без разметки.
- `entities: MessageEntity[]` — Форматирование.

### `MessageButton`

- `text: string` — Надпись на кнопке.
- `type: 'url' | 'callback' | 'other'` — Грубый тип из контракта API. Точный — в kind.
- `kind: MessageButtonKind` — Точный вид кнопки.
- `url?: string` — url, webView, simpleWebView, auth.
- `data?: string` — callback: данные кнопки (как строка UTF-8).
- `copyText?: string` — copyText: текст, который кнопка копирует.

### `MessageReaction`

- `emoji: string` — Эмодзи ('👍'), id премиум-эмодзи ('5368324170671202286') или 'paid' для звёзд.
- `count: number` — Сколько раз поставили.
- `chosen: boolean` — Эту реакцию поставил ты.
- `customEmojiId?: string` — id премиум-эмодзи.
- `paid?: boolean` — Платная реакция звёздами.

### `MessageMediaInfo`

- `fileName?: string` — Имя файла.
- `mime?: string` — MIME-тип.
- `size?: number` — Байты.
- `duration?: number` — Секунды (аудио, видео, голосовые, кружки).
- `width?: number` — Ширина (фото, видео).
- `height?: number` — Высота (фото, видео).
- `documentId?: string` — id документа (файлы, видео, стикеры...).
- `photoId?: string` — id фото.
- `spoiler?: boolean` — Медиа скрыто под спойлером.

### `MessageForward`

- `name: string` — Имя автора оригинала (или подпись скрытого отправителя).
- `peerId?: PeerId` — Есть, если автор оригинала известен клиенту.
- `date: number` — Дата оригинала, unix time.
- `messageId?: number` — id оригинала в канале-источнике.
- `postAuthor?: string` — Подпись автора поста в канале.
- `imported?: boolean` — Сообщение импортировано из другого мессенджера.

### `MessagesSendOptions`

- `replyTo?: number | Message` — Ответ на сообщение: id или сам Message.
- `topicId?: number` — Тема форума, в которую отправить.
- `silent?: boolean` — Без звука у получателей.
- `runHooks?: boolean` — Пропустить сообщение через onBeforeSend и команды плагинов. По умолчанию false.
- `parse?: TextParseMode` — Разметка текста. По умолчанию 'none' — текст как есть.
- `scheduleDate?: number | Date` — Отложенная отправка: unix time (секунды), миллисекунды или Date.
- `noWebpage?: boolean` — Не показывать превью ссылок.
- `entities?: MessageEntity[]` — Готовое форматирование. Добавляется к тому, что дал parse.

### `MessagesEditOptions`

- `parse?: TextParseMode` — Разметка текста.
- `noWebpage?: boolean` — Без превью ссылок.
- `entities?: MessageEntity[]` — Готовое форматирование.

### `MessagesHistoryOptions`

- `limit?: number` — Сколько сообщений, по умолчанию 50. Больше 100 — догрузит страницами.
- `offsetId?: number | Message` — Брать сообщения старше этого id (0 — с самого нового).
- `addOffset?: number` — Сдвиг от offsetId. Отрицательный — взять более новые (тогда не больше 100).
- `offsetDate?: number | Date` — Брать сообщения старше этой даты.
- `minId?: number` — Только id больше minId.
- `maxId?: number` — Только id меньше maxId.

### `MessagesIterateOptions`

- `limit?: number` — Всего сообщений, по умолчанию — вся история.
- `batch?: number` — Размер страницы, до 100.
- `offsetId?: number | Message` — Начать со старших, чем этот id.

### `MessagesSearchOptions`

- `limit?: number` — По умолчанию 50. Больше 100 — догрузит страницами.
- `from?: PeerId | Peer` — Только сообщения этого отправителя (в «Избранном» — из этого чата).
- `filter?: MessagesSearchFilter` — Только сообщения с этим типом вложений.
- `topicId?: number` — Тема форума.
- `minDate?: number | Date` — Не раньше этой даты.
- `maxDate?: number | Date` — Не позже этой даты.
- `offsetId?: number` — Искать старше этого id.
- `addOffset?: number` — Сдвиг от offsetId.
- `folder?: number` — Только глобальный поиск: 0 — основной список, 1 — архив.
- `type?: 'channels' | 'groups' | 'users'` — Только глобальный поиск: искать только в каналах, группах или личках.

### `MessagesForwardOptions`

- `silent?: boolean` — Без звука у получателей.
- `dropAuthor?: boolean` — Переслать без автора (как копию).
- `dropCaptions?: boolean` — Без автора и без подписей к медиа.
- `scheduleDate?: number | Date` — Отложенная пересылка.
- `topicId?: number` — Тема форума в чате назначения.

### `MessagesSendFileOptions`

- `caption?: string` — Подпись. Ставится к последнему файлу (к первому, если это альбом фото и видео).
- `parse?: TextParseMode` — Разметка подписи.
- `entities?: MessageEntity[]` — Готовое форматирование подписи.
- `asDocument?: boolean` — Отправить файлом, без сжатия.
- `album?: boolean` — Несколько файлов — альбомом (по умолчанию true).
- `spoiler?: boolean` — Скрыть фото и видео под спойлер.
- `replyTo?: number | Message` — Ответ на сообщение.
- `topicId?: number` — Тема форума.
- `silent?: boolean` — Без звука.
- `scheduleDate?: number | Date` — Отложенная отправка.

### `MessagesButtonAnswer`

- `answer?: string` — Ответ бота (всплывающий текст).
- `url?: string` — Ссылка: у url-кнопки или из ответа бота.
- `alert?: boolean` — Бот просил показать ответ окном, а не всплывашкой.

### `ChatListEntry`

Чат в списке чатов: Peer плюс счётчики и состояние. Расширяет `Peer`.

- `unread: number` — Непрочитанные сообщения.
- `unreadMark: boolean` — Чат вручную помечен непрочитанным.
- `mentions: number` — Непрочитанные упоминания.
- `reactions: number` — Непрочитанные реакции на твои сообщения.
- `pinned: boolean` — Закреплён в той папке, из которой взят список (для entry() — в основном списке).
- `muted: boolean` — Уведомления выключены.
- `archived: boolean` — Чат в архиве.
- `lastMessage: Message | null` — Последнее сообщение, если клиент его знает.

### `ChatFolder`

Папка чатов (фильтр). Папка «Все чаты» сюда не входит: это folder 0.

- `id: number` — id папки для dzg.chats.list({ folder }).
- `title: string` — Название папки.
- `emoji: string` — Эмодзи-иконка папки или ''.
- `chatlist: boolean` — Папка добавлена по ссылке-приглашению (chatlist).

### `ChatListOptions`

- `folder?: number | 'archive'` — id папки из folders(), 'archive' — архив, 0 или не задано — основной список.
- `limit?: number` — Сколько чатов вернуть, по умолчанию 100. 0 — все, что загружены.
- `unreadOnly?: boolean` — Только чаты с непрочитанными или с пометкой «непрочитано».
- `type?: Peer['type'] | Peer['type'][]` — Только чаты этого типа (или типов).

### `ChatMember`

Участник группы или канала. Расширяет `Peer`.

- `role: ChatMemberRole` — Роль в чате.
- `rank?: string` — Должность (подпись админа или участника), если задана.

### `ChatMembersOptions`

- `limit?: number` — Сколько участников вернуть, по умолчанию 200, максимум 10000.
- `query?: string` — Поиск по имени и юзернейму.

### `ChatInfo`

Полная информация о чате, свежая с сервера.

- `peer: Peer` — Сам чат.
- `about: string` — Описание группы или канала, для пользователя — био.
- `membersCount: number` — 0 для личных чатов.
- `onlineCount: number` — Сколько участников сейчас в сети (по данным сервера).
- `inviteLink: string` — Основная ссылка-приглашение, если она тебе доступна, иначе ''.
- `linkedChat: Peer | null` — Связанный чат: группа обсуждения канала или канал группы.
- `slowmodeSeconds: number` — Медленный режим, секунды. 0 — выключен.
- `amIn: boolean` — Ты состоишь в чате. Для личных чатов всегда true.
- `isCreator: boolean` — Ты создатель.
- `isAdmin: boolean` — Ты админ.
- `isForum: boolean` — Группа с темами.
- `isVerified: boolean` — Галочка верификации.
- `isScam: boolean` — Помечен как скам.
- `isFake: boolean` — Помечен как фейк.

### `ChatCreateOptions`

- `title: string` — Название.
- `type?: 'group' | 'supergroup' | 'channel'` — 'group' (по умолчанию) — обычная группа, 'supergroup' — супергруппа, 'channel' — канал.
- `about?: string` — Описание.
- `users?: (PeerId | Peer)[]` — Кого сразу пригласить.
- `forum?: boolean` — Супергруппа с темами.

### `InviteLinkOptions`

- `expireDate?: number` — Unix time, когда ссылка перестанет работать.
- `usageLimit?: number` — Сколько раз по ней можно вступить.
- `title?: string` — Название ссылки (видно только админам).
- `requestNeeded?: boolean` — Вступление по заявке.

### `UserStatus`

- `online: boolean` — Сейчас в сети.
- `lastSeen: number` — Unix time последнего визита; когда в сети — текущее время; 0, если скрыто.
- `kind: UserStatusKind` — Вид статуса.
- `text: string` — Готовая строка на языке клиента: «в сети», «был(а) недавно»...

### `UserInfo`

- `peer: Peer` — Сам пользователь.
- `bio: string` — Био ('' если нет).
- `phone: string` — Телефон без '+', если виден.
- `firstName: string` — Имя.
- `lastName: string` — Фамилия.
- `isContact: boolean` — В твоих контактах.
- `isPremium: boolean` — Есть Telegram Premium.
- `isBot: boolean` — Это бот.
- `isVerified: boolean` — Галочка верификации.
- `isScam: boolean` — Помечен как скам.
- `isFake: boolean` — Помечен как фейк.
- `isBlocked: boolean` — Ты заблокировал этого пользователя.
- `commonChats: number` — Сколько у вас общих групп.
- `status: UserStatus` — Статус в сети.
- `birthday: { day: number; month: number; year: number } | null` — День рождения, если пользователь его показывает. year = 0, если год скрыт.

### `AccountEntry`

- `index: number` — Номер аккаунта в клиенте. Не обязательно идёт подряд.
- `self: Peer` — Пользователь аккаунта.
- `active: boolean` — Аккаунт активен сейчас.
- `premium: boolean` — У аккаунта есть Premium.

### `TopBarButtonSpec`

Кнопка в шапке чата (справа, рядом с поиском и звонком).

- `icon?: DzgIconName | string` — Иконка. Нужна иконка или текст.
- `text?: string` — Короткий текст вместо иконки, например 'Аа' или 'GPT'.
- `tooltip?: string` — Подсказка при наведении.
- `when?(chat: Peer): boolean` — Показывать кнопку в этом чате? Вызывается синхронно при каждом обновлении шапки (смена чата, выделение...), поэтому должна быть быстрой. Нет функции — кнопка видна во всех чатах.
- `onClick(chat: Peer): void` — Клик по кнопке.

### `ComposeButtonSpec`

Кнопка у поля ввода (справа, рядом с эмодзи и «запланированными»).

- `icon?: DzgIconName | string` — Иконка (или text).
- `text?: string` — Короткий текст вместо иконки.
- `tooltip?: string` — Подсказка при наведении.
- `onClick(chat: Peer | null): void` — chat — чат, в котором открыто поле, или null.

### `MainMenuItemSpec`

- `title: string` — Текст пункта.
- `icon?: DzgIconName | string` — По умолчанию 'customize'.
- `onClick(): void` — Меню закрывается перед вызовом.

### `NotifySpec`

- `title?: string` — Заголовок.
- `text: string` — Текст.
- `onClick?(): void` — Клик по уведомлению или по кнопке «Открыть». Окно клиента активируется.
- `silent?: boolean` — Без звука и без мигания значка на панели задач.

### `ComposeState`

- `chat: Peer | null` — Чат, к которому относится поле, или null (например, черновик без чата).
- `text: string` — Текст в поле.

### `PanelLabelOptions`

- `bold?: boolean` — Заголовок раздела (как в настройках).
- `muted?: boolean` — Серый поясняющий текст.

### `PanelButtonOptions`

- `style?: 'light' | 'row'` — По умолчанию — заполненная кнопка на всю ширину. 'light' — светлая кнопка; 'row' — строка как в настройках. Если задан icon, кнопка всегда строка с иконкой.
- `icon?: DzgIconName | string` — Иконка: кнопка станет строкой с иконкой.

### `PanelInputOptions`

- `placeholder?: string` — Подсказка в пустом поле.
- `value?: string` — Начальный текст.
- `multiline?: boolean` — Многострочное поле: отправка по Ctrl+Enter.
- `onSubmit?(text: string, widget: Widget): void` — Enter в однострочном поле, Ctrl+Enter в многострочном.
- `onChange?(text: string, widget: Widget): void` — Каждое изменение текста.

### `Panel`

Боковая панель справа в главном окне — как третья колонка. У плагина одна панель: повторный dzg.ui.panel() возвращает её же. Панели разных плагинов становятся вкладками; показана последняя открытая.

- `readonly id: number` — id панели.
- `readonly widget: Widget` — Вся панель (заголовок + содержимое).
- `readonly content: Widget` — Прокручиваемая колонка с элементами.
- `setTitle(title: string): Panel` — Поменять заголовок.
- `setWidth(width: number): Panel` — 240..720 px.
- `show(): Panel` — Сделать вкладку панели активной.
- `scrollToBottom(): Panel` — Прокрутить содержимое вниз.
- `clear(): Panel` — Удалить все элементы. Обработчики старых элементов отключаются.
- `addLabel(text: string, options?: PanelLabelOptions): Widget | null` — Текст можно менять: label.setText('...').
- `addButton(text: string, onClick: (widget: Widget) => void, options?: PanelButtonOptions): Widget | null` — Кнопка на всю ширину.
- `addInput(options?: PanelInputOptions): Widget | null` — Текущий текст: widget.text().
- `addToggle(text: string, checked: boolean, onChange: (checked: boolean, widget: Widget) => void): Widget | null` — Переключатель как в настройках.
- `addSeparator(): void` — Разделитель.
- `addSkip(px?: number): void` — Пустое место, по умолчанию стандартный отступ.
- `close(): void` — Закрыть панель. Срабатывает on('close').
- `on(event: 'close', handler: (panel: Panel) => void): Panel` — 'close' — панель закрыта: крестиком, close(), выгрузкой окна.
- `isOpen(): boolean` — Панель ещё открыта.

### `PanelOptions`

- `title?: string` — По умолчанию — имя плагина.
- `width?: number` — По умолчанию 340, допустимо 240..720.

### `TLObject`

TL-объект в JSON. Конструктор пишется в поле `_`, остальные поля называются так же, как в api.tl (snake_case): - `int`, `double` — number; - `long` — строка с десятичным числом (`'-1001234567890'`), можно BigInt; - `string` — строка; - `bytes`, `int128`, `int256` — base64-строка (на вход можно Uint8Array, ArrayBuffer, массив байт или `{ text: 'строка' }` — она уйдёт как UTF-8); - `Bool` — true / false; - `Vector<T>` — массив; - `flags.N?true` — true, если флаг стоит; отсутствует, если нет; - поля-флаги (`flags:#`, `flags2:#`) считаются сами и в ответах не приходят; - необязательные поля (`flags.N?T`), которых нет, просто отсутствуют.

- `_: string` — Имя конструктора: 'inputPeerSelf', 'message'...
- `[field: string]: any` — Поля как в api.tl (snake_case).

### `ApiCallOptions`

- `timeout?: number` — Через сколько миллисекунд отклонить промис с ошибкой TIMEOUT и отменить запрос. 0 — ждать сколько нужно.
- `floodSleep?: number` — FLOOD_WAIT не дольше стольких секунд клиент переждёт сам и повторит запрос. На более долгий промис отклоняется с ошибкой (error.seconds). По умолчанию 10, 0 — никогда не ждать.
- `applyResult?: boolean` — true (по умолчанию): клиент обработает ответ так, будто запрос сделал он сам — применит Updates (сообщения появятся в чатах, придут события) и запомнит users / chats из ответа (после этого работают inputPeer() и dzg.chats.get()). false: ответ видит только плагин.
- `dc?: number` — Дата-центр, в который уйдёт запрос (для продвинутых случаев: upload.getFile файла с другого DC). Методы upload.get* без dc идут в DC аккаунта через отдельное соединение для загрузок. По умолчанию — основное соединение.

### `ApiError`

Ошибка dzg.api.call. Расширяет `Error`.

- `code?: number` — Код ошибки Telegram: 400, 403, 420... Нет у локальных ошибок (кодирование, TIMEOUT).
- `type?: string` — Тип ошибки: 'PEER_ID_INVALID', 'FLOOD_WAIT_30', 'TIMEOUT'...
- `seconds?: number` — Для FLOOD_WAIT_X / SLOWMODE_WAIT_X: сколько секунд ждать.

### `TLParamInfo`

- `name: string` — Имя поля как в api.tl.
- `type: string` — Тип как в api.tl, вместе с условием: `'flags.0?Vector<InputDocument>'`.
- `optional: boolean` — Поле зависит от флага (flags.N?...).

### `TLSchemaInfo`

- `kind: 'method' | 'constructor'` — Метод или конструктор.
- `name: string` — Имя: 'messages.getHistory', 'inputPeerSelf'.
- `id: number` — Id конструктора (беззнаковое 32-битное число).
- `idHex: string` — То же id в hex, 8 символов: '4423e6c5'.
- `params: TLParamInfo[]` — Поля по порядку схемы.
- `type: string` — Тип результата метода или тип конструктора: 'messages.Messages', 'Update'.

### `ApiUpdateMeta`

Второй аргумент обработчика onUpdate.

- `self: PeerId` — Id аккаунта (PeerId), которому пришло обновление.

### `InstalledPlugin`

Установленный плагин, как его видит менеджер.

- `id: string` — id плагина.
- `name: string` — Название.
- `version: string` — Версия.
- `author: string` — Автор.
- `description: string` — Описание.
- `dir: string` — Папка плагина.
- `type: 'module' | 'script'` — 'module' (ES-модуль, по умолчанию) или 'script'.
- `enabled: boolean` — Пользователь включил плагин.
- `loaded: boolean` — Плагин сейчас работает (включён и загрузился без ошибок).
- `builtin: boolean` — Встроенный: поставляется с клиентом, удалить нельзя — только выключить.
- `hasSettings: boolean` — Есть схема настроек в manifest.json.
- `error: string` — Последняя ошибка ('' если всё хорошо).

### `PluginPackageInfo`

Что лежит по ссылке или пути, без установки.

- `id: string` — id плагина.
- `name: string` — Название.
- `version: string` — Версия.
- `author: string` — Автор.
- `description: string` — Описание.
- `files: number` — Сколько файлов в пакете.
- `size: number` — Суммарный размер в байтах.
- `singleFile: boolean` — Плагин из одного .js: манифест собран из шапки `// @name ...`.
- `source: string` — Итоговая ссылка или имя файла.
- `installed: boolean` — Плагин с таким id уже установлен (установка его обновит).

### `BuiltinPluginInfo`

Встроенный плагин, который клиент поставляет и обновляет сам.

- `id: string` — id плагина.
- `name: string` — Название.
- `version: string` — Версия, вшитая в клиент.
- `installedVersion: string` — Версия в `tdata/plugins/<id>/` ('' если не установлен).
- `enabledByDefault: boolean` — Включается сам при первом запуске (пользователь может выключить).
- `replaced: boolean` — Пользователь заменил плагин своим (в manifest.json нет "builtin": true) — клиент его не трогает.
- `updated: boolean` — Установлен или обновлён при этом запуске.
- `error: string` — Ошибка установки ('' если всё хорошо).

### `PluginCommandInfo`

Команда любого загруженного плагина.

- `name: string` — Имя команды без /.
- `description: string` — Описание команды.
- `plugin: string` — id плагина, который зарегистрировал команду.
- `pluginName: string` — Название плагина.

## Псевдонимы типов

### `PeerId`

Идентификатор чата или пользователя. Строка: 64-битные id не влезают в number без потерь.

```ts
type PeerId = string;
```

### `MediaType`

```ts
type MediaType = | 'photo' | 'video' | 'gif' | 'sticker' | 'voice' | 'videoNote' | 'audio' | 'file' | 'poll' | 'other';
```

### `BeforeSendHook`

Хук перед отправкой: - вернуть строку или { text } — заменить текст; - вернуть false или { cancel: true } — не отправлять; - ничего не вернуть — оставить как есть.

```ts
type BeforeSendHook = (ctx: SendContext) => string | false | { text?: string; cancel?: boolean } | void;
```

### `WidgetType`

```ts
type WidgetType = 'label' | 'button' | 'input' | 'checkbox' | 'panel';
```

### `MessageEntityType`

Тип сущности форматирования. Имена как в Bot API, только camelCase. Ссылки, упоминания, хэштеги и команды клиент находит в тексте сам.

```ts
type MessageEntityType = | 'bold' | 'italic' | 'underline' | 'strikethrough' | 'spoiler' | 'code' | 'pre' | 'blockquote' | 'textLink' | 'textMention' | 'customEmoji' | 'url' | 'email' | 'phone' | 'bankCard' | 'mention' | 'hashtag' | 'cashtag' | 'botCommand' | 'mediaTimestamp' | 'formattedDate' | 'subscript' | 'superscript' | 'marked';
```

### `TextParseMode`

```ts
type TextParseMode = 'markdown' | 'md' | 'html' | 'none';
```

### `MessageButtonKind`

Вид inline-кнопки или кнопки клавиатуры бота.

```ts
type MessageButtonKind = | 'text' | 'url' | 'callback' | 'callbackWithPassword' | 'requestPhone' | 'requestLocation' | 'requestPoll' | 'requestPeer' | 'switchInline' | 'switchInlineSame' | 'game' | 'buy' | 'auth' | 'userProfile' | 'webView' | 'simpleWebView' | 'copyText' | 'disabled' | 'other';
```

### `MessagesSearchFilter`

```ts
type MessagesSearchFilter = | 'photo' | 'video' | 'photoVideo' | 'document' | 'url' | 'gif' | 'voice' | 'music' | 'roundVideo' | 'roundVoice' | 'mentions' | 'pinned' | 'geo' | 'contacts' | 'chatPhotos' | 'poll';
```

### `TypingAction`

Действие «печатает…». В setTyping можно передать любое, кроме 'import_history' (его присылает только сервер, когда кто-то импортирует историю).

```ts
type TypingAction = | 'typing' | 'cancel' | 'record_video' | 'upload_video' | 'record_voice' | 'upload_voice' | 'record_round' | 'upload_round' | 'upload_photo' | 'upload_document' | 'choose_location' | 'choose_contact' | 'choose_sticker' | 'play_game' | 'import_history';
```

### `ChatMemberRole`

```ts
type ChatMemberRole = 'creator' | 'admin' | 'member' | 'restricted' | 'left' | 'banned';
```

### `UserStatusKind`

Вид статуса: - online — в сети; - offline — был(а) в сети в lastSeen; - recently / lastWeek / lastMonth / longAgo — время скрыто, известен только порядок; - hidden — неизвестно.

```ts
type UserStatusKind = | 'online' | 'offline' | 'recently' | 'lastWeek' | 'lastMonth' | 'longAgo' | 'hidden';
```

### `DzgIconName`

Имя иконки из встроенного набора клиента. Полный список — dzg.ui.icons(). Неизвестное имя не ломает кнопку: в лог пишется предупреждение, вместо иконки рисуется 'customize' (если нет текста).

```ts
type DzgIconName = | 'search' | 'settings' | 'customize' | 'edit' | 'delete' | 'clear' | 'copy' | 'link' | 'links' | 'pin' | 'unpin' | 'mute' | 'unmute' | 'sound' | 'soundOff' | 'silent' | 'archive' | 'unarchive' | 'info' | 'help' | 'stats' | 'bot' | 'code' | 'experimental' | 'star' | 'fave' | 'like' | 'premium' | 'gift' | 'bell' | 'lock' | 'user' | 'users' | 'addUser' | 'admin' | 'username' | 'channel' | 'chat' | ...;
```

### `RenderTransform`

Возвращает текст, который показать вместо message.text, или null/undefined, чтобы оставить как есть. Меняется только отображение: сообщение, его копирование и пересылка остаются прежними. Форматирование заменённого текста сбрасывается. Если трансформаций несколько (даже из разных плагинов), они применяются по очереди: следующая получает результат предыдущей в message.text.

```ts
type RenderTransform = (message: Message) => string | null | undefined;
```

### `TLInputPeerLike`

Значение, которое можно передать там, где ждут InputPeer / InputUser / InputChannel.

```ts
type TLInputPeerLike = TLObject | Peer | Message | PeerId | 'me' | 'self';
```
