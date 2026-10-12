// Типы API плагинов DamZaGram — модуль api: сырой MTProto (apiVersion 2).
// Автор модуля: @wetakush (https://t.me/wetakush).
// Подробности и примеры: docs/plugins/api/mtproto.md.

/**
 * TL-объект в JSON. Конструктор пишется в поле `_`, остальные поля называются
 * так же, как в api.tl (snake_case):
 *
 * - `int`, `double` — number;
 * - `long` — строка с десятичным числом (`'-1001234567890'`), можно BigInt;
 * - `string` — строка;
 * - `bytes`, `int128`, `int256` — base64-строка (на вход можно Uint8Array,
 *   ArrayBuffer, массив байт или `{ text: 'строка' }` — она уйдёт как UTF-8);
 * - `Bool` — true / false;
 * - `Vector<T>` — массив;
 * - `flags.N?true` — true, если флаг стоит; отсутствует, если нет;
 * - поля-флаги (`flags:#`, `flags2:#`) считаются сами и в ответах не приходят;
 * - необязательные поля (`flags.N?T`), которых нет, просто отсутствуют.
 */
interface TLObject {
  _: string;
  [field: string]: any;
}

/** Значение, которое можно передать там, где ждут InputPeer / InputUser / InputChannel. */
type TLInputPeerLike = TLObject | Peer | Message | PeerId | 'me' | 'self';

interface ApiCallOptions {
  /** Через сколько миллисекунд отклонить промис с ошибкой TIMEOUT и отменить запрос. 0 — ждать сколько нужно. */
  timeout?: number;
  /**
   * FLOOD_WAIT не дольше стольких секунд клиент переждёт сам и повторит запрос.
   * На более долгий промис отклоняется с ошибкой (error.seconds). По умолчанию 10, 0 — никогда не ждать.
   */
  floodSleep?: number;
  /**
   * true (по умолчанию): клиент обработает ответ так, будто запрос сделал он сам —
   * применит Updates (сообщения появятся в чатах, придут события) и запомнит
   * users / chats из ответа (после этого работают inputPeer() и dzg.chats.get()).
   * false: ответ видит только плагин.
   */
  applyResult?: boolean;
  /**
   * Дата-центр, в который уйдёт запрос (для продвинутых случаев: upload.getFile
   * файла с другого DC). Методы upload.get* без dc идут в DC аккаунта через
   * отдельное соединение для загрузок. По умолчанию — основное соединение.
   */
  dc?: number;
}

/** Ошибка dzg.api.call. */
interface ApiError extends Error {
  /** Код ошибки Telegram: 400, 403, 420... Нет у локальных ошибок (кодирование, TIMEOUT). */
  code?: number;
  /** Тип ошибки: 'PEER_ID_INVALID', 'FLOOD_WAIT_30', 'TIMEOUT'... */
  type?: string;
  /** Для FLOOD_WAIT_X / SLOWMODE_WAIT_X: сколько секунд ждать. */
  seconds?: number;
}

interface TLParamInfo {
  name: string;
  /** Тип как в api.tl, вместе с условием: 'flags.0?Vector<InputDocument>'. */
  type: string;
  /** Поле зависит от флага (flags.N?...). */
  optional: boolean;
}

interface TLSchemaInfo {
  kind: 'method' | 'constructor';
  name: string;
  /** Id конструктора (беззнаковое 32-битное число). */
  id: number;
  /** То же id в hex, 8 символов: '4423e6c5'. */
  idHex: string;
  params: TLParamInfo[];
  /** Тип результата метода или тип конструктора: 'messages.Messages', 'Update'. */
  type: string;
}

/** Второй аргумент обработчика onUpdate. */
interface ApiUpdateMeta {
  /** Id аккаунта (PeerId), которому пришло обновление. */
  self: PeerId;
}

interface DzgApi {
  /**
   * Вызывает любой метод Telegram API: `dzg.api.call('messages.getHistory', {...})`.
   * Поля — как в api.tl; `random_id` и `hash` можно не передавать — подставятся сами
   * (случайный id и 0). Там, где ждут InputPeer / InputUser / InputChannel, можно
   * передать Peer, Message, id чата или 'me'.
   * Промис отклоняется с ApiError ('400: PEER_ID_INVALID').
   */
  call<T = any>(method: string, params?: Record<string, any>, options?: ApiCallOptions): Promise<T>;

  /** InputPeer для чата по известному клиенту access_hash; null, если клиент его не знает. */
  inputPeer(peer: TLInputPeerLike): TLObject | null;
  /** InputUser; null, если это не пользователь или он неизвестен. 'me' -> inputUserSelf. */
  inputUser(peer: TLInputPeerLike): TLObject | null;
  /** InputChannel; null, если это не канал / супергруппа. */
  inputChannel(peer: TLInputPeerLike): TLObject | null;

  /**
   * TL-peer -> id чата DamZaGram (PeerId) для dzg.chats.get / dzg.messages и т. п.
   * Понимает peerUser / peerChat / peerChannel, inputPeer*, inputUser, inputChannel,
   * а также объекты user / chat / channel из ответов. null для остального.
   */
  peerId(peer: TLObject | null | undefined): PeerId | null;

  /**
   * Сырые обновления от сервера (объекты типа Update): 'updateUserTyping',
   * 'updateNewMessage'... '*' — все. Можно передать массив типов.
   * Бросает TypeError для неизвестного типа или не-Update (например 'updateShortMessage').
   * Возвращает функцию отписки.
   */
  onUpdate(type: string | string[] | '*', fn: (update: TLObject, meta: ApiUpdateMeta) => void): () => void;

  /** Описание метода или конструктора из схемы; принимает и id ('0x4423e6c5'). null, если не найден. */
  schema(name: string): TLSchemaInfo | null;
  /** Имена конструкторов типа: constructors('InputPeer') -> ['inputPeerEmpty', 'inputPeerSelf', ...]. */
  constructors(type: string): string[];
  /** Поиск по именам методов и конструкторов (без учёта регистра). */
  search(query: string, limit?: number): string[];

  /** TL-объект -> base64 его бинарного представления. type по умолчанию 'Object' (любой конструктор). Бросает Error. */
  encode(value: any, type?: string): string;
  /** base64 / байты -> TL-объект. type: 'Object', 'Vector<long>', 'Message'... Бросает Error. */
  decode(data: string | Uint8Array | ArrayBuffer | number[], type?: string): any;

  /** Байты или строка (как UTF-8) -> base64. */
  toBase64(data: string | Uint8Array | ArrayBuffer | number[]): string;
  /** base64 (и base64url) -> байты, или строка при as = 'text'. */
  fromBase64(base64: string): Uint8Array;
  fromBase64(base64: string, as: 'text'): string;

  /** Слой (layer) схемы, на котором работает клиент. */
  readonly layer: number;
}

interface Dzg {
  api: DzgApi;
}
