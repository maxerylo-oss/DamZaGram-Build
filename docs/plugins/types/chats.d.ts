// Типы API плагинов DamZaGram — модуль chats (apiVersion 2).
// Дополняет DzgChats из core.d.ts: список чатов, папки, вход и выход,
// уведомления, архив, закрепление, «печатает…», участники и полная информация.
// Автор модуля: @wetakush (https://t.me/wetakush).

/**
 * Действие «печатает…».
 * В setTyping можно передать любое, кроме 'import_history'
 * (его присылает только сервер, когда кто-то импортирует историю).
 */
type TypingAction =
  | 'typing' | 'cancel'
  | 'record_video' | 'upload_video'
  | 'record_voice' | 'upload_voice'
  | 'record_round' | 'upload_round'
  | 'upload_photo' | 'upload_document'
  | 'choose_location' | 'choose_contact' | 'choose_sticker'
  | 'play_game' | 'import_history';

/** Чат в списке чатов: Peer плюс счётчики и состояние. */
interface ChatListEntry extends Peer {
  /** Непрочитанные сообщения. */
  unread: number;
  /** Чат вручную помечен непрочитанным. */
  unreadMark: boolean;
  /** Непрочитанные упоминания. */
  mentions: number;
  /** Непрочитанные реакции на твои сообщения. */
  reactions: number;
  /** Закреплён в той папке, из которой взят список (для entry() — в основном списке). */
  pinned: boolean;
  /** Уведомления выключены. */
  muted: boolean;
  /** Чат в архиве. */
  archived: boolean;
  /** Последнее сообщение, если клиент его знает. */
  lastMessage: Message | null;
}

/** Папка чатов (фильтр). Папка «Все чаты» сюда не входит: это folder 0. */
interface ChatFolder {
  id: number;
  title: string;
  /** Эмодзи-иконка папки или ''. */
  emoji: string;
  /** Папка добавлена по ссылке-приглашению (chatlist). */
  chatlist: boolean;
}

interface ChatListOptions {
  /** id папки из folders(), 'archive' — архив, 0 или не задано — основной список. */
  folder?: number | 'archive';
  /** Сколько чатов вернуть, по умолчанию 100. 0 — все, что загружены. */
  limit?: number;
  /** Только чаты с непрочитанными или с пометкой «непрочитано». */
  unreadOnly?: boolean;
  /** Только чаты этого типа (или типов). */
  type?: Peer['type'] | Peer['type'][];
}

type ChatMemberRole = 'creator' | 'admin' | 'member' | 'restricted' | 'left' | 'banned';

/** Участник группы или канала. */
interface ChatMember extends Peer {
  role: ChatMemberRole;
  /** Должность (подпись админа или участника), если задана. */
  rank?: string;
}

interface ChatMembersOptions {
  /** Сколько участников вернуть, по умолчанию 200, максимум 10000. */
  limit?: number;
  /** Поиск по имени и юзернейму. */
  query?: string;
}

/** Полная информация о чате, свежая с сервера. */
interface ChatInfo {
  peer: Peer;
  /** Описание группы или канала, для пользователя — био. */
  about: string;
  /** 0 для личных чатов. */
  membersCount: number;
  /** Сколько участников сейчас в сети (по данным сервера). */
  onlineCount: number;
  /** Основная ссылка-приглашение, если она тебе доступна, иначе ''. */
  inviteLink: string;
  /** Связанный чат: группа обсуждения канала или канал группы. */
  linkedChat: Peer | null;
  /** Медленный режим, секунды. 0 — выключен. */
  slowmodeSeconds: number;
  /** Ты состоишь в чате. Для личных чатов всегда true. */
  amIn: boolean;
  isCreator: boolean;
  isAdmin: boolean;
  /** Группа с темами. */
  isForum: boolean;
  isVerified: boolean;
  isScam: boolean;
  isFake: boolean;
}

interface ChatCreateOptions {
  title: string;
  /** 'group' (по умолчанию) — обычная группа, 'supergroup' — супергруппа, 'channel' — канал. */
  type?: 'group' | 'supergroup' | 'channel';
  /** Описание. */
  about?: string;
  /** Кого сразу пригласить. */
  users?: (PeerId | Peer)[];
  /** Супергруппа с темами. */
  forum?: boolean;
}

interface InviteLinkOptions {
  /** Unix time, когда ссылка перестанет работать. */
  expireDate?: number;
  /** Сколько раз по ней можно вступить. */
  usageLimit?: number;
  /** Название ссылки (видно только админам). */
  title?: string;
  /** Вступление по заявке. */
  requestNeeded?: boolean;
}

interface DzgChats {
  /** Все действия, которые принимает setTyping. */
  readonly typingActions: readonly TypingAction[];

  /**
   * Чаты, которые клиент уже загрузил, в том порядке, как в списке слева
   * (закреплённые сверху). Запросов к серверу нет.
   */
  list(options?: ChatListOptions): ChatListEntry[];
  /** Состояние одного чата: счётчики, mute, архив. null, если чат неизвестен. */
  entry(peer: PeerId | Peer): ChatListEntry | null;
  /** Папки чатов пользователя (без «Все чаты»). */
  folders(): ChatFolder[];

  /**
   * Найти чат или пользователя на сервере.
   * Понимает 'durov', '@durov', 't.me/durov', 'tg://resolve?domain=durov'
   * и номера телефонов ('+79991234567'). null, если никого нет.
   */
  resolve(username: string): Promise<Peer | null>;
  /**
   * Вступить в группу или канал по юзернейму, ссылке t.me/... или
   * приглашению (t.me/+hash, t.me/joinchat/hash, tg://join?invite=hash).
   * Можно передать и Peer уже известного канала.
   * Если ты уже участник — просто вернёт чат.
   * Ошибки — коды сервера: 'INVITE_HASH_EXPIRED', 'INVITE_REQUEST_SENT',
   * 'CHANNELS_TOO_MUCH', 'JOIN_VERIFICATION_REQUIRED' и т.п.
   */
  join(usernameOrInviteLink: string | Peer): Promise<Peer>;
  /** Выйти из группы или канала. История чата не удаляется. */
  leave(peer: PeerId | Peer): Promise<void>;

  /**
   * Выключить уведомления.
   * seconds: число секунд; true, 0 или не задано — навсегда; false — включить обратно.
   */
  mute(peer: PeerId | Peer, seconds?: number | boolean): boolean;
  /** То же, что mute(peer, false). */
  unmute(peer: PeerId | Peer): boolean;
  /** Переместить в архив (по умолчанию) или вернуть из него. */
  archive(peer: PeerId | Peer, archived?: boolean): boolean;
  /**
   * Закрепить или открепить чат. По умолчанию в основном списке,
   * { folder } — в папке. При превышении лимита клиент покажет своё окно.
   * false, если нет открытого окна аккаунта.
   */
  pin(peer: PeerId | Peer, pinned?: boolean, options?: { folder?: number }): boolean;
  /** true — пометить непрочитанным, false — прочитать всё. */
  markUnread(peer: PeerId | Peer, unread?: boolean): boolean;
  /** Прочитать все сообщения чата (то же, что markUnread(peer, false)). */
  markRead(peer: PeerId | Peer): boolean;
  /**
   * Показать собеседникам статус «печатает…» и т.п. Один вызов держит статус
   * около 5 секунд, для долгого статуса повторяй раз в 4–5 секунд.
   * 'cancel' снимает статус. Уважает режим призрака AyuGram.
   * Неизвестное действие бросает TypeError.
   */
  setTyping(peer: PeerId | Peer, action?: TypingAction, options?: { topicId?: number }): boolean;

  /** Участники группы или канала с сервера (в канале нужны права админа). */
  members(peer: PeerId | Peer, options?: ChatMembersOptions): Promise<ChatMember[]>;
  /** Полная информация о чате, канале или пользователе с сервера. */
  info(peer: PeerId | Peer): Promise<ChatInfo>;

  /** Создать группу, супергруппу или канал. Строка — название обычной группы. */
  create(options: ChatCreateOptions | string): Promise<Peer>;
  /**
   * Пригласить пользователей. missing — id тех, кого пригласить не вышло
   * (запретили приглашать себя в настройках приватности и т.п.).
   */
  addMembers(peer: PeerId | Peer, users: PeerId | Peer | (PeerId | Peer)[]): Promise<{ added: number; missing: PeerId[] }>;
  /** Удалить участника. { ban: true } — ещё и заблокировать в чате (только супергруппы и каналы). */
  kick(peer: PeerId | Peer, user: PeerId | Peer, options?: { ban?: boolean }): Promise<void>;
  /** Переименовать группу или канал. */
  setTitle(peer: PeerId | Peer, title: string): Promise<void>;
  /** Поменять описание группы или канала. */
  setAbout(peer: PeerId | Peer, about: string): Promise<void>;
  /** Создать новую ссылку-приглашение. Возвращает ссылку. */
  createInviteLink(peer: PeerId | Peer, options?: InviteLinkOptions): Promise<string>;
  /** Очистить историю. { revoke: true } — удалить и у собеседника (где это возможно). */
  clearHistory(peer: PeerId | Peer, options?: { revoke?: boolean }): boolean;
}

interface EventMap {
  /**
   * Кто-то печатает, записывает голосовое, выбирает стикер...
   * Повторы одного действия приходят не чаще раза в 4 секунды.
   * Когда действие закончилось (явно или по таймауту ~6.5 с), приходит action 'cancel'.
   * Только для активного аккаунта.
   */
  'chat.typing': { chat: Peer; user: Peer; action: TypingAction; topicId: number };
  /**
   * Изменилось число непрочитанных в чате или пометка «непрочитано».
   * Изменения собираются пачкой за 300 мс, одинаковые значения не повторяются.
   */
  'chat.unread': { chat: Peer; unread: number; unreadMark: boolean; mentions: number };
}
