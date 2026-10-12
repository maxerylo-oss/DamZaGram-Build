// Типы API плагинов DamZaGram — расширения модуля ui (apiVersion 2).
// Дополняет DzgUi и DzgMessages из core.d.ts: кнопки в шапке чата и у поля
// ввода, пункты главного меню, горячие клавиши, уведомления, ссылки,
// поле ввода, боковые панели и подмена текста сообщений при отрисовке.
// Автор модуля: @wetakush (https://t.me/wetakush).

/**
 * Имя иконки из встроенного набора клиента. Полный список — dzg.ui.icons().
 * Неизвестное имя не ломает кнопку: в лог пишется предупреждение,
 * вместо иконки рисуется 'customize' (если нет текста).
 */
type DzgIconName =
  | 'search' | 'settings' | 'customize' | 'edit' | 'delete' | 'clear' | 'copy'
  | 'link' | 'links' | 'pin' | 'unpin' | 'mute' | 'unmute' | 'sound' | 'soundOff'
  | 'silent' | 'archive' | 'unarchive' | 'info' | 'help' | 'stats' | 'bot' | 'code'
  | 'experimental' | 'star' | 'fave' | 'like' | 'premium' | 'gift' | 'bell' | 'lock'
  | 'user' | 'users' | 'addUser' | 'admin' | 'username' | 'channel' | 'chat' | 'chats'
  | 'topics' | 'discussion' | 'saved' | 'folder' | 'addToFolder' | 'photo' | 'video'
  | 'gif' | 'file' | 'download' | 'upload' | 'reply' | 'forward' | 'send' | 'share'
  | 'translate' | 'palette' | 'night' | 'font' | 'draw' | 'spoiler' | 'plus' | 'close'
  | 'check' | 'markRead' | 'markUnread' | 'timer' | 'schedule' | 'hourglass'
  | 'location' | 'poll' | 'todo' | 'sticker' | 'emoji' | 'reactions' | 'phone' | 'qr'
  | 'block' | 'report' | 'restore' | 'leave' | 'network' | 'devices' | 'permissions'
  | 'manage' | 'shortcut' | 'newWindow' | 'expand' | 'collapse' | 'zoomIn' | 'zoomOut'
  | 'reorder' | 'storage' | 'power' | 'tag' | 'shop' | 'ghost' | 'streamer';

/** Кнопка в шапке чата (справа, рядом с поиском и звонком). */
interface TopBarButtonSpec {
  /** Иконка. Нужна иконка или текст. */
  icon?: DzgIconName | string;
  /** Короткий текст вместо иконки, например 'Аа' или 'GPT'. */
  text?: string;
  /** Подсказка при наведении. */
  tooltip?: string;
  /**
   * Показывать кнопку в этом чате? Вызывается синхронно при каждом
   * обновлении шапки (смена чата, выделение...), поэтому должна быть быстрой.
   * Нет функции — кнопка видна во всех чатах.
   */
  when?(chat: Peer): boolean;
  onClick(chat: Peer): void;
}

/** Кнопка у поля ввода (справа, рядом с эмодзи и «запланированными»). */
interface ComposeButtonSpec {
  icon?: DzgIconName | string;
  text?: string;
  tooltip?: string;
  /** chat — чат, в котором открыто поле, или null. */
  onClick(chat: Peer | null): void;
}

interface MainMenuItemSpec {
  title: string;
  /** По умолчанию 'customize'. */
  icon?: DzgIconName | string;
  /** Меню закрывается перед вызовом. */
  onClick(): void;
}

interface NotifySpec {
  title?: string;
  text: string;
  /** Клик по уведомлению или по кнопке «Открыть». Окно клиента активируется. */
  onClick?(): void;
  /** Без звука и без мигания значка на панели задач. */
  silent?: boolean;
}

interface ComposeState {
  /** Чат, к которому относится поле, или null (например, черновик без чата). */
  chat: Peer | null;
  text: string;
}

interface DzgCompose {
  /** Текущее поле ввода: в фокусе, иначе последнее, где был фокус, в активном окне. null — поля нет. */
  get(): ComposeState | null;
  /** Заменить весь текст поля. Курсор — в конец. false — поля нет. */
  set(text: string): boolean;
  /** Вставить текст в позицию курсора (заменяет выделение). */
  insert(text: string): boolean;
  /** Активировать окно и поставить фокус в поле. */
  focus(): void;
  /** Кнопка рядом с полем ввода во всех чатах, темах и ответах. */
  addButton(spec: ComposeButtonSpec): () => void;
}

interface PanelLabelOptions {
  /** Заголовок раздела (как в настройках). */
  bold?: boolean;
  /** Серый поясняющий текст. */
  muted?: boolean;
}

interface PanelButtonOptions {
  /**
   * По умолчанию — заполненная кнопка на всю ширину.
   * 'light' — светлая кнопка; 'row' — строка как в настройках.
   * Если задан icon, кнопка всегда строка с иконкой.
   */
  style?: 'light' | 'row';
  icon?: DzgIconName | string;
}

interface PanelInputOptions {
  placeholder?: string;
  value?: string;
  /** Многострочное поле: отправка по Ctrl+Enter. */
  multiline?: boolean;
  /** Enter в однострочном поле, Ctrl+Enter в многострочном. */
  onSubmit?(text: string, widget: Widget): void;
  /** Каждое изменение текста. */
  onChange?(text: string, widget: Widget): void;
}

/**
 * Боковая панель справа в главном окне — как третья колонка.
 * У плагина одна панель: повторный dzg.ui.panel() возвращает её же.
 * Панели разных плагинов становятся вкладками; показана последняя открытая.
 */
interface Panel {
  readonly id: number;
  /** Вся панель (заголовок + содержимое). */
  readonly widget: Widget;
  /** Прокручиваемая колонка с элементами. */
  readonly content: Widget;
  setTitle(title: string): Panel;
  /** 240..720 px. */
  setWidth(width: number): Panel;
  /** Сделать вкладку панели активной. */
  show(): Panel;
  scrollToBottom(): Panel;
  /** Удалить все элементы. Обработчики старых элементов отключаются. */
  clear(): Panel;
  /** Текст можно менять: label.setText('...'). */
  addLabel(text: string, options?: PanelLabelOptions): Widget | null;
  addButton(text: string, onClick: (widget: Widget) => void, options?: PanelButtonOptions): Widget | null;
  /** Текущий текст: widget.text(). */
  addInput(options?: PanelInputOptions): Widget | null;
  /** Переключатель как в настройках. */
  addToggle(text: string, checked: boolean, onChange: (checked: boolean, widget: Widget) => void): Widget | null;
  addSeparator(): void;
  /** Пустое место, по умолчанию стандартный отступ. */
  addSkip(px?: number): void;
  /** Закрыть панель. Срабатывает on('close'). */
  close(): void;
  /** 'close' — панель закрыта: крестиком, close(), выгрузкой окна. */
  on(event: 'close', handler: (panel: Panel) => void): Panel;
  isOpen(): boolean;
}

interface PanelOptions {
  /** По умолчанию — имя плагина. */
  title?: string;
  /** По умолчанию 340, допустимо 240..720. */
  width?: number;
}

interface DzgUi {
  /** Имена иконок для опции icon. */
  icons(): DzgIconName[];
  addTopBarButton(spec: TopBarButtonSpec): () => void;
  addMainMenuItem(spec: MainMenuItemSpec): () => void;
  /**
   * Горячая клавиша на всё приложение, синтаксис Qt: 'Ctrl+Shift+K', 'Alt+F1', 'F8'.
   * Только одно сочетание (без аккордов 'Ctrl+K, Ctrl+C') — иначе исключение.
   * Без Ctrl/Alt/Meta и не F-клавиша — не срабатывает, пока фокус в текстовом поле.
   * Сочетание плагина важнее сочетаний клиента.
   */
  addShortcut(keys: string, handler: () => void): () => void;
  /** Уведомление поверх окна клиента. Строка — то же, что { text }. */
  notify(spec: NotifySpec | string): void;
  /** Ссылка через клиент: t.me/..., tg://..., обычные — в браузере. */
  openLink(url: string): void;
  /** Пересчитать when() у кнопок в шапке, если условие изменилось. */
  refreshButtons(): void;
  compose: DzgCompose;
  /** Открыть (или вернуть уже открытую) боковую панель плагина. */
  panel(options?: PanelOptions): Panel;
}

/**
 * Возвращает текст, который показать вместо message.text, или null/undefined,
 * чтобы оставить как есть. Меняется только отображение: сообщение, его
 * копирование и пересылка остаются прежними. Форматирование заменённого
 * текста сбрасывается. Если трансформаций несколько (даже из разных
 * плагинов), они применяются по очереди: следующая получает результат
 * предыдущей в message.text.
 */
type RenderTransform = (message: Message) => string | null | undefined;

interface DzgMessages {
  /**
   * Подменить текст сообщений при отрисовке. Результат кешируется для
   * каждого сообщения, функция вызывается снова, когда текст меняется
   * или когда трансформации добавляются/убираются.
   */
  addRenderTransform(transform: RenderTransform): () => void;
  /** Сбросить кеш и перерисовать: например, после смены настроек плагина. */
  refreshRender(): void;
}

interface EventMap {
  /** Окно клиента стало активным или неактивным. */
  'app.focus': { active: boolean };
  /** Текст в поле ввода изменился. Не чаще раза в ~300 мс. */
  'compose.changed': { chat: Peer | null; text: string };
}
