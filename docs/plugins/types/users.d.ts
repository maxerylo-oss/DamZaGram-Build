// Типы API плагинов DamZaGram — модули users и accounts (apiVersion 2).
// Автор модуля: @wetakush (https://t.me/wetakush).

/**
 * Вид статуса:
 * - online — в сети;
 * - offline — был(а) в сети в lastSeen;
 * - recently / lastWeek / lastMonth / longAgo — время скрыто, известен только порядок;
 * - hidden — неизвестно.
 */
type UserStatusKind =
  | 'online' | 'offline' | 'recently' | 'lastWeek' | 'lastMonth' | 'longAgo' | 'hidden';

interface UserStatus {
  online: boolean;
  /** Unix time последнего визита; когда в сети — текущее время; 0, если скрыто. */
  lastSeen: number;
  kind: UserStatusKind;
  /** Готовая строка на языке клиента: «в сети», «был(а) недавно»... */
  text: string;
}

interface UserInfo {
  peer: Peer;
  /** Био ('' если нет). */
  bio: string;
  /** Телефон без '+', если виден. */
  phone: string;
  firstName: string;
  lastName: string;
  isContact: boolean;
  isPremium: boolean;
  isBot: boolean;
  isVerified: boolean;
  isScam: boolean;
  isFake: boolean;
  /** Ты заблокировал этого пользователя. */
  isBlocked: boolean;
  /** Сколько у вас общих групп. */
  commonChats: number;
  status: UserStatus;
  /** День рождения, если пользователь его показывает. year = 0, если год скрыт. */
  birthday: { day: number; month: number; year: number } | null;
}

interface DzgUsers {
  /** Ты сам (активный аккаунт). null до входа в аккаунт. */
  self(): Peer | null;
  /** Peer по id. То же, что dzg.chats.get. */
  get(peer: PeerId | Peer): Peer | null;
  /** Текущий статус из памяти клиента, без запроса. null, если это не пользователь. */
  status(peer: PeerId | Peer): UserStatus | null;
  /** Заблокирован ли пользователь (по данным клиента). */
  isBlocked(peer: PeerId | Peer): boolean;
  /** Полная информация с сервера (users.getFullUser). */
  info(peer: PeerId | Peer): Promise<UserInfo>;
  /** Контакты, которые загрузил клиент. */
  contacts(): Peer[];
  /** Заблокировать (по умолчанию) или разблокировать. */
  block(peer: PeerId | Peer, blocked?: boolean): Promise<void>;
  /** То же, что block(peer, false). */
  unblock(peer: PeerId | Peer): Promise<void>;
  /** Общие группы и каналы, до 100. */
  commonChats(peer: PeerId | Peer, options?: { limit?: number }): Promise<Peer[]>;
  /**
   * Добавить в контакты или переименовать контакт. Пустые поля берутся из профиля.
   * sharePhone — показать этому человеку свой номер.
   */
  addContact(peer: PeerId | Peer, options?: { firstName?: string; lastName?: string; phone?: string; sharePhone?: boolean }): Promise<void>;
  /** Удалить из контактов. */
  deleteContact(peer: PeerId | Peer): Promise<void>;
}

interface AccountEntry {
  /** Номер аккаунта в клиенте. Не обязательно идёт подряд. */
  index: number;
  self: Peer;
  active: boolean;
  premium: boolean;
}

interface DzgAccounts {
  /** Аккаунты, в которые выполнен вход. */
  list(): AccountEntry[];
  /** Активный аккаунт. */
  current(): AccountEntry | null;
  /**
   * Переключиться на аккаунт. Переключение происходит чуть позже,
   * после него придёт событие 'session.ready' (если аккаунт открыт впервые)
   * и API начнёт работать с новым аккаунтом.
   * false, если аккаунта с таким index нет.
   */
  switchTo(index: number | AccountEntry): boolean;
}

interface Dzg {
  users: DzgUsers;
  accounts: DzgAccounts;
}

interface EventMap {
  /**
   * Пользователь появился в сети или вышел из неё (только активный аккаунт).
   * Повторные одинаковые статусы отбрасываются. Первые 15 секунд после
   * входа в аккаунт приходят только изменения уже известных статусов,
   * чтобы загрузка списка чатов не завалила плагин событиями.
   */
  'user.status': { user: Peer; online: boolean; lastSeen: number; kind: UserStatusKind };
}
