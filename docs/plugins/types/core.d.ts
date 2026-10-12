// Типы API плагинов DamZaGram — ядро (apiVersion 2).
// Модули API дописывают свои методы в отдельных файлах этой папки через
// слияние интерфейсов: `interface DzgMessages { history(...): ... }`.
// Подключи папку через jsconfig.json — VS Code начнёт подсказывать `dzg`.

/** Идентификатор чата или пользователя. Строка: 64-битные id не влезают в number без потерь. */
type PeerId = string;

interface Peer {
  /** Внутренний id DamZaGram (с битами типа). Используй его во всех вызовах API. */
  id: PeerId;
  /** Голый Telegram id (как в ботах и t.me/c/...). */
  tgId: number;
  name: string;
  username: string;
  type: 'user' | 'bot' | 'group' | 'channel' | 'unknown';
  isSelf: boolean;
}

type MediaType =
  | 'photo' | 'video' | 'gif' | 'sticker' | 'voice' | 'videoNote'
  | 'audio' | 'file' | 'poll' | 'other';

interface Message {
  id: number;
  peerId: PeerId;
  chat: Peer;
  fromId: PeerId;
  fromName: string;
  text: string;
  /** Unix time, секунды. */
  date: number;
  out: boolean;
  service: boolean;
  post: boolean;
  pinned: boolean;
  /** true, если сообщение удалено на сервере, но сохранено AyuGram. */
  deleted: boolean;
  /** 0, если не ответ. */
  replyToId: number;
  views: number;
  media?: MediaType;
}

interface PluginInfo {
  id: string;
  name: string;
  version: string;
  author: string;
  description: string;
  /** Папка плагина (с завершающим слешем не гарантируется). */
  dir: string;
  /** Папка для данных плагинов, с завершающим слешем. */
  dataDir: string;
}

interface AppInfo {
  name: 'DamZaGram';
  version: string;
  apiVersion: number;
  platform: string;
  os: string;
  pluginsPath: string;
  devMode: boolean;
}

interface EventMap {
  /** Плагин загружен и готов. Срабатывает и после горячей перезагрузки. */
  'ready': void;
  /** Плагин выгружается: выключен, перезагружен или клиент закрывается. */
  'unload': void;
  /** Аккаунт авторизован и его данные доступны. */
  'session.ready': Peer;
  /** Новое сообщение (входящее или исходящее) появилось в любом чате. */
  'message.new': Message;
  'message.edited': Message;
  /** Сервер сообщил об удалении. Сообщение ещё доступно в этот момент. */
  'message.deleted': Message;
  'chat.opened': Peer;
  /** Пользователь поменял настройку плагина в окне настроек. */
  'settings.changed': { key: string; value: unknown };
}

interface SendContext {
  text: string;
  peerId: PeerId;
  chat: Peer;
  /** id сообщения, на которое отвечают, или 0. */
  replyTo: number;
}

/**
 * Хук перед отправкой:
 * - вернуть строку или { text } — заменить текст;
 * - вернуть false или { cancel: true } — не отправлять;
 * - ничего не вернуть — оставить как есть.
 */
type BeforeSendHook = (ctx: SendContext) =>
  string | false | { text?: string; cancel?: boolean } | void;

interface CommandContext {
  /** Аргументы, разбитые по пробелам. */
  args: string[];
  /** Всё после имени команды одной строкой. */
  rest: string;
  text: string;
  chat: Peer;
  replyTo: number;
}

interface CommandSpec {
  description?: string;
  run(ctx: CommandContext): void | Promise<void>;
}

interface MenuItem<T> {
  /** Текст пункта или функция, которая строит его для конкретной цели. */
  title: string | ((target: T) => string);
  /** Показывать пункт, только если вернула true. */
  when?: (target: T) => boolean;
  onClick(target: T): void;
}

interface BoxSpec {
  title?: string;
  text?: string;
  inputs?: { id: string; placeholder?: string; value?: string; multiline?: boolean }[];
  toggles?: { id: string; text: string; checked?: boolean }[];
  /** По умолчанию одна кнопка OK. */
  buttons?: { id: string; text: string; danger?: boolean }[];
}

interface BoxResult {
  /** id нажатой кнопки или 'cancel', если окно закрыли. */
  button: string;
  values: Record<string, string | boolean>;
}

interface Rect { x: number; y: number; width: number; height: number }

interface WidgetDump {
  className: string;
  name: string;
  visible: boolean;
  geometry: Rect;
  children?: WidgetDump[];
}

/**
 * Слабая ссылка на любой виджет клиента.
 * Если виджет уже удалён, методы ничего не делают, valid() вернёт false.
 */
interface Widget {
  /** Стабильный id виджета в рамках плагина. */
  readonly id: number;
  /** Подписаться на событие созданного виджета: click, change, submit, destroyed. */
  on(event: string, handler: (widget: Widget, value?: unknown) => void): Widget;
  valid(): boolean;
  /** Настоящее C++ имя класса, например "HistoryView::TopBarWidget". */
  className(): string;
  /** Имя класса и цепочка Qt-родителей. */
  classChain(): string[];
  name(): string;
  setName(name: string): void;

  geometry(): Rect;
  globalGeometry(): Rect;
  setGeometry(x: number, y: number, width: number, height: number): void;
  move(x: number, y: number): void;
  resize(width: number, height: number): void;
  visible(): boolean;
  setVisible(visible: boolean): void;
  show(): void;
  hide(): void;
  raise(): void;
  lower(): void;
  update(): void;
  /** 0..1, 1 снимает эффект. */
  setOpacity(opacity: number): void;
  setEnabled(enabled: boolean): void;
  setFocus(): void;
  /** Удалить виджет. Чужие виджеты клиента так удалять опасно. */
  destroy(): void;

  parentWidget(): Widget | null;
  window(): Widget | null;
  childWidgets(): Widget[];
  /** Поиск среди всех потомков. className сравнивается с полным или коротким именем. */
  find(className?: string, name?: string): Widget[];
  childAt(x: number, y: number): Widget | null;

  prop(name: string): unknown;
  setProp(name: string, value: unknown): boolean;
  propNames(): string[];

  /** Синтетический клик левой кнопкой в локальных координатах. */
  click(x: number, y: number): void;
  typeText(text: string): void;
  /** Сохранить скриншот виджета в PNG. */
  grab(path: string): boolean;

  /** Для полей ввода, лейблов и кнопок. */
  text(): string;
  setText(text: string): void;

  dump(depth: number): WidgetDump;
}

type WidgetType = 'label' | 'button' | 'input' | 'checkbox' | 'panel';

interface CreateProps {
  x?: number; y?: number; width?: number; height?: number;
  text?: string;
  name?: string;
  visible?: boolean;
  /** button: 'light' — вторичная кнопка. */
  style?: 'light';
  /** input */
  placeholder?: string;
  multiline?: boolean;
  /** checkbox */
  checked?: boolean;
  /** panel: цвет #RRGGBB или #AARRGGBB и скругление. */
  color?: string;
  radius?: number;
  onClick?: (widget: Widget) => void;
  onChange?: (widget: Widget, value: string | boolean) => void;
  on?: {
    click?: (widget: Widget) => void;
    change?: (widget: Widget, value: string | boolean) => void;
    submit?: (widget: Widget, value: string) => void;
  };
}

interface FetchOptions {
  method?: string;
  headers?: Record<string, string>;
  body?: string;
  /** Отправить JSON: выставит Content-Type и сериализует объект. */
  json?: unknown;
  /** Таймаут в мс, по умолчанию 30000. */
  timeout?: number;
}

interface FetchResponse {
  status: number;
  ok: boolean;
  url: string;
  /** Имена заголовков в нижнем регистре. */
  headers: Record<string, string>;
  text(): Promise<string>;
  json<T = any>(): Promise<T>;
}

interface DzgStorage {
  get<T = unknown>(key: string, fallback?: T): T;
  /** Значение должно сериализоваться в JSON. */
  set(key: string, value: unknown): void;
  remove(key: string): void;
  keys(): string[];
}

interface DzgSettings {
  get<T = unknown>(key: string): T;
  all(): Record<string, unknown>;
  set(key: string, value: unknown): void;
}

interface DzgUi {
  toast(text: string, title?: string): void;
  box(spec: BoxSpec): Promise<BoxResult>;
  alert(text: string, title?: string): Promise<void>;
  confirm(text: string, options?: { title?: string; confirmText?: string; cancelText?: string }): Promise<boolean>;
  /** null, если пользователь отменил. */
  prompt(title: string, options?: {
    text?: string; placeholder?: string; value?: string; multiline?: boolean;
    confirmText?: string; cancelText?: string;
  }): Promise<string | null>;
  /** Возвращает функцию, которая убирает пункт. */
  addMessageMenuItem(item: MenuItem<Message>): () => void;
  addChatMenuItem(item: MenuItem<Peer>): () => void;
  mainWindow(): Widget | null;
  focused(): Widget | null;
  /** Поиск по всем окнам приложения. */
  find(className?: string, name?: string): Widget[];
  /** parent: null — главное окно. */
  create(type: WidgetType, parent: Widget | null, props?: CreateProps): Widget | null;
  repaint(): void;
}

interface DzgTheme {
  /** Имя цвета палитры Telegram (windowBg, msgOutBg, ...). Цвет: #RRGGBB или #AARRGGBB. */
  set(name: string, color: string): boolean;
  setMany(colors: Record<string, string>): boolean;
  /** Вернуть палитру, какой она была до первого изменения плагинами. */
  reset(): void;
}

interface DzgChats {
  current(): Peer | null;
  get(peerId: PeerId | Peer): Peer | null;
  /** Ищет только среди уже загруженных клиентом чатов. */
  find(username: string): Peer | null;
  open(peerId: PeerId | Peer, messageId?: number): boolean;
}

interface DzgMessages {
  send(peerId: PeerId | Peer, text: string, options?: {
    replyTo?: number;
    silent?: boolean;
    /** Пропустить сообщение через onBeforeSend других плагинов. По умолчанию false. */
    runHooks?: boolean;
  }): boolean;
  edit(peerId: PeerId | Peer, messageId: number, text: string): boolean;
  delete(peerId: PeerId | Peer, ids: number | number[], options?: { revoke?: boolean }): boolean;
  get(peerId: PeerId | Peer, messageId: number): Message | null;
  /** Последние сообщения, которые клиент уже загрузил в память. Запросов к серверу нет. */
  loaded(peerId: PeerId | Peer, limit?: number): Message[];
  /** Вернуть true — скрыть сообщение. Вызывается часто: фильтр должен быть быстрым. */
  addFilter(filter: (message: Message) => boolean): () => void;
  onBeforeSend(hook: BeforeSendHook): () => void;
  /** Пересчитать фильтры, например после смены настроек. */
  refilter(): void;
}

interface DzgCommands {
  /** Команда срабатывает, когда пользователь отправляет "/name ...". Сообщение не уходит. */
  register(name: string, spec: CommandSpec | CommandSpec['run']): () => void;
  list(): { name: string; description: string }[];
}

interface DzgHttp {
  fetch(url: string, options?: FetchOptions): Promise<FetchResponse>;
}

interface DzgFs {
  /** null, если файла нет. */
  read(path: string): string | null;
  write(path: string, text: string): boolean;
  list(path: string): string[];
  exists(path: string): boolean;
}

interface DzgClipboard {
  get(): string;
  set(text: string): void;
}

interface Dzg {
  readonly version: 2;
  readonly plugin: PluginInfo;
  readonly app: AppInfo;

  on<K extends keyof EventMap>(event: K, handler: (data: EventMap[K]) => void): () => void;
  off<K extends keyof EventMap>(event: K, handler: (data: EventMap[K]) => void): void;
  once<K extends keyof EventMap>(event: K, handler: (data: EventMap[K]) => void): () => void;

  log(...args: unknown[]): void;
  settings: DzgSettings;

  ui: DzgUi;

  theme: DzgTheme;

  chats: DzgChats;

  messages: DzgMessages;

  commands: DzgCommands;

  http: DzgHttp;

  fs: DzgFs;

  clipboard: DzgClipboard;

  openUrl(url: string): void;
}

declare const dzg: Dzg;

declare const console: {
  log(...args: unknown[]): void;
  info(...args: unknown[]): void;
  debug(...args: unknown[]): void;
  warn(...args: unknown[]): void;
  error(...args: unknown[]): void;
};

declare function setTimeout(fn: (...args: any[]) => void, ms?: number, ...args: any[]): number;
declare function setInterval(fn: (...args: any[]) => void, ms?: number, ...args: any[]): number;
declare function clearTimeout(id: number): void;
declare function clearInterval(id: number): void;
declare function queueMicrotask(fn: () => void): void;
