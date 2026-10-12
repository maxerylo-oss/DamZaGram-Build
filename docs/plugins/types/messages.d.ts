// Типы API плагинов DamZaGram — модуль messages (apiVersion 2).
// Дополняет Message и DzgMessages из core.d.ts: форматированная отправка
// (Markdown / HTML), история и поиск на сервере, пересылка, реакции,
// закрепление, файлы, ссылки на сообщения и inline-кнопки ботов.
// Автор модуля: @wetakush (https://t.me/wetakush).

/**
 * Тип сущности форматирования. Имена как в Bot API, только camelCase.
 * Ссылки, упоминания, хэштеги и команды клиент находит в тексте сам.
 */
type MessageEntityType =
  | 'bold' | 'italic' | 'underline' | 'strikethrough' | 'spoiler'
  | 'code' | 'pre' | 'blockquote'
  | 'textLink' | 'textMention' | 'customEmoji'
  | 'url' | 'email' | 'phone' | 'bankCard'
  | 'mention' | 'hashtag' | 'cashtag' | 'botCommand'
  | 'mediaTimestamp' | 'formattedDate'
  | 'subscript' | 'superscript' | 'marked';

/** Кусок форматирования. offset и length — в UTF-16 (как String.length в JS). */
interface MessageEntity {
  type: MessageEntityType | string;
  offset: number;
  length: number;
  /** textLink: адрес ссылки. */
  url?: string;
  /** textMention: id пользователя (PeerId). */
  userId?: PeerId;
  /** pre: язык блока кода. */
  language?: string;
  /** customEmoji: id документа премиум-эмодзи. */
  documentId?: string;
  /** blockquote: свёрнутая (раскрывающаяся) цитата. */
  collapsed?: boolean;
}

/** Текст и форматирование — результат parse(), вход format(). */
interface FormattedText {
  text: string;
  entities: MessageEntity[];
}

type TextParseMode = 'markdown' | 'md' | 'html' | 'none';

/** Вид inline-кнопки или кнопки клавиатуры бота. */
type MessageButtonKind =
  | 'text' | 'url' | 'callback' | 'callbackWithPassword'
  | 'requestPhone' | 'requestLocation' | 'requestPoll' | 'requestPeer'
  | 'switchInline' | 'switchInlineSame' | 'game' | 'buy' | 'auth'
  | 'userProfile' | 'webView' | 'simpleWebView' | 'copyText' | 'disabled'
  | 'other';

interface MessageButton {
  text: string;
  /** Грубый тип из контракта API. Точный — в kind. */
  type: 'url' | 'callback' | 'other';
  kind: MessageButtonKind;
  /** url, webView, simpleWebView, auth. */
  url?: string;
  /** callback: данные кнопки (как строка UTF-8). */
  data?: string;
  /** copyText: текст, который кнопка копирует. */
  copyText?: string;
}

interface MessageReaction {
  /** Эмодзи ('👍'), id премиум-эмодзи ('5368324170671202286') или 'paid' для звёзд. */
  emoji: string;
  count: number;
  /** Эту реакцию поставил ты. */
  chosen: boolean;
  customEmojiId?: string;
  paid?: boolean;
}

interface MessageMediaInfo {
  fileName?: string;
  mime?: string;
  /** Байты. */
  size?: number;
  /** Секунды (аудио, видео, голосовые, кружки). */
  duration?: number;
  width?: number;
  height?: number;
  /** id документа (файлы, видео, стикеры...). */
  documentId?: string;
  /** id фото. */
  photoId?: string;
  /** Медиа скрыто под спойлером. */
  spoiler?: boolean;
}

interface MessageForward {
  /** Имя автора оригинала (или подпись скрытого отправителя). */
  name: string;
  /** Есть, если автор оригинала известен клиенту. */
  peerId?: PeerId;
  /** Дата оригинала, unix time. */
  date: number;
  /** id оригинала в канале-источнике. */
  messageId?: number;
  /** Подпись автора поста в канале. */
  postAuthor?: string;
  /** Сообщение импортировано из другого мессенджера. */
  imported?: boolean;
}

interface Message {
  /** Форматирование текста. */
  entities: MessageEntity[];
  /** Дата последнего изменения, 0 — не редактировалось. */
  editDate: number;
  /** id альбома, '' — не в альбоме. */
  groupedId: string;
  /** Есть у пересланных сообщений. */
  forwardFrom?: MessageForward;
  /** Подробности о файле или фото (если есть media). */
  mediaInfo?: MessageMediaInfo;
  /** Кнопки бота: строки, в строке — кнопки слева направо. */
  buttons?: MessageButton[][];
  /** Реакции под сообщением. */
  reactions?: MessageReaction[];
  /** id темы форума (только в форумах). */
  topicId?: number;
}

interface MessagesSendOptions {
  /** Ответ на сообщение: id или сам Message. */
  replyTo?: number | Message;
  /** Тема форума, в которую отправить. */
  topicId?: number;
  /** Без звука у получателей. */
  silent?: boolean;
  /** Пропустить сообщение через onBeforeSend и команды плагинов. По умолчанию false. */
  runHooks?: boolean;
  /** Разметка текста. По умолчанию 'none' — текст как есть. */
  parse?: TextParseMode;
  /** Отложенная отправка: unix time (секунды), миллисекунды или Date. */
  scheduleDate?: number | Date;
  /** Не показывать превью ссылок. */
  noWebpage?: boolean;
  /** Готовое форматирование. Добавляется к тому, что дал parse. */
  entities?: MessageEntity[];
}

interface MessagesEditOptions {
  parse?: TextParseMode;
  noWebpage?: boolean;
  entities?: MessageEntity[];
}

interface MessagesHistoryOptions {
  /** Сколько сообщений, по умолчанию 50. Больше 100 — догрузит страницами. */
  limit?: number;
  /** Брать сообщения старше этого id (0 — с самого нового). */
  offsetId?: number | Message;
  /** Сдвиг от offsetId. Отрицательный — взять более новые (тогда не больше 100). */
  addOffset?: number;
  /** Брать сообщения старше этой даты. */
  offsetDate?: number | Date;
  /** Только id больше minId. */
  minId?: number;
  /** Только id меньше maxId. */
  maxId?: number;
}

interface MessagesIterateOptions {
  /** Всего сообщений, по умолчанию — вся история. */
  limit?: number;
  /** Размер страницы, до 100. */
  batch?: number;
  /** Начать со старших, чем этот id. */
  offsetId?: number | Message;
}

type MessagesSearchFilter =
  | 'photo' | 'video' | 'photoVideo' | 'document' | 'url' | 'gif'
  | 'voice' | 'music' | 'roundVideo' | 'roundVoice' | 'mentions'
  | 'pinned' | 'geo' | 'contacts' | 'chatPhotos' | 'poll';

interface MessagesSearchOptions {
  /** По умолчанию 50. Больше 100 — догрузит страницами. */
  limit?: number;
  /** Только сообщения этого отправителя (в «Избранном» — из этого чата). */
  from?: PeerId | Peer;
  filter?: MessagesSearchFilter;
  /** Тема форума. */
  topicId?: number;
  minDate?: number | Date;
  maxDate?: number | Date;
  offsetId?: number;
  addOffset?: number;
  /** Только глобальный поиск: 0 — основной список, 1 — архив. */
  folder?: number;
  /** Только глобальный поиск: искать только в каналах, группах или личках. */
  type?: 'channels' | 'groups' | 'users';
}

interface MessagesForwardOptions {
  silent?: boolean;
  /** Переслать без автора (как копию). */
  dropAuthor?: boolean;
  /** Без автора и без подписей к медиа. */
  dropCaptions?: boolean;
  scheduleDate?: number | Date;
  /** Тема форума в чате назначения. */
  topicId?: number;
}

interface MessagesSendFileOptions {
  /** Подпись. Ставится к последнему файлу (к первому, если это альбом фото и видео). */
  caption?: string;
  /** Разметка подписи. */
  parse?: TextParseMode;
  entities?: MessageEntity[];
  /** Отправить файлом, без сжатия. */
  asDocument?: boolean;
  /** Несколько файлов — альбомом (по умолчанию true). */
  album?: boolean;
  /** Скрыть фото и видео под спойлер. */
  spoiler?: boolean;
  replyTo?: number | Message;
  topicId?: number;
  silent?: boolean;
  scheduleDate?: number | Date;
}

interface MessagesButtonAnswer {
  /** Ответ бота (всплывающий текст). */
  answer?: string;
  /** Ссылка: у url-кнопки или из ответа бота. */
  url?: string;
  /** Бот просил показать ответ окном, а не всплывашкой. */
  alert?: boolean;
}

interface DzgMessages {
  send(peer: PeerId | Peer, text: string, options?: MessagesSendOptions): boolean;
  /** Изменить текст. Возвращает false, если сообщение не найдено, ещё не отправлено или это предложенный пост, ждущий ответа. */
  edit(peer: PeerId | Peer, messageId: number, text: string, options?: MessagesEditOptions): boolean;

  /** Разобрать Markdown (по умолчанию) или HTML в текст и форматирование. */
  parse(text: string, mode?: TextParseMode): FormattedText;
  /** Обратно: текст и форматирование в HTML (по умолчанию) или Markdown. */
  format(message: FormattedText | Message, mode?: 'html' | 'markdown' | 'md'): string;
  format(text: string, mode: 'html' | 'markdown' | 'md', entities: MessageEntity[]): string;

  /** История с сервера, от старых к новым. */
  history(peer: PeerId | Peer, options?: MessagesHistoryOptions): Promise<Message[]>;
  /** Вся история страницами, от новых к старым: for await (const m of dzg.messages.iterate(chat)). */
  iterate(peer: PeerId | Peer, options?: MessagesIterateOptions): AsyncGenerator<Message, void, void>;
  /** Поиск в чате или по всем чатам (peer = null). Результат от старых к новым. */
  search(peer: PeerId | Peer | null, query: string, options?: MessagesSearchOptions): Promise<Message[]>;
  /** Сообщение с сервера, даже если клиент его не загружал. */
  fetch(peer: PeerId | Peer, messageId: number): Promise<Message | null>;
  fetch(peer: PeerId | Peer, messageIds: number[]): Promise<Message[]>;

  /** Переслать одно или несколько сообщений в другой чат. Служебные (и звонки) и неотправленные пропускаются; если переслать нечего — MESSAGE_NOT_FORWARDABLE. */
  forward(fromPeer: PeerId | Peer, ids: number | Message | (number | Message)[], toPeer: PeerId | Peer, options?: MessagesForwardOptions): Promise<void>;
  /** Поставить реакцию. null — убрать свои реакции. Премиум-эмодзи — строкой с id. */
  react(peer: PeerId | Peer, messageId: number, emoji: string | null, options?: { addToRecent?: boolean }): Promise<void>;
  /** Реакции, которые можно поставить на сообщение (если оно загружено). */
  reactions(peer: PeerId | Peer, messageId: number): string[];
  pin(peer: PeerId | Peer, messageId: number, options?: { silent?: boolean; unpin?: boolean; oneSide?: boolean }): Promise<void>;
  unpin(peer: PeerId | Peer, messageId: number): Promise<void>;
  /** Прочитать чат (до конца или до tillId). Учитывает режим призрака AyuGram. */
  markRead(peer: PeerId | Peer, options?: { tillId?: number }): boolean;

  /** Отправить файл или несколько файлов (пути относительно папки плагина или абсолютные). */
  sendFile(peer: PeerId | Peer, path: string | string[], options?: MessagesSendFileOptions): boolean;
  /** Скачать файл или фото из сообщения. Без path — в папку загрузок клиента. Можно передать папку. */
  download(peer: PeerId | Peer, messageId: number, path?: string): Promise<string>;
  /** Ссылка на сообщение (t.me/...). null для личных чатов и обычных групп. */
  link(peer: PeerId | Peer, messageId: number, options?: { thread?: boolean; private?: boolean }): string | null;
  /** Нажать кнопку бота по позиции (строка, столбец) или по тексту. */
  clickButton(peer: PeerId | Peer, messageId: number, row: number, column: number, options?: { open?: boolean }): Promise<MessagesButtonAnswer>;
  clickButton(peer: PeerId | Peer, messageId: number, text: string, options?: { open?: boolean }): Promise<MessagesButtonAnswer>;
}

interface EventMap {
  /** Кто-то поставил реакцию на твоё сообщение (новая непрочитанная реакция). */
  'message.reaction': Message;
}
