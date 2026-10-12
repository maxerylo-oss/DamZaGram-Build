# `dzg.api` — сырой MTProto

`dzg.api` даёт плагину весь Telegram API: любой метод из
[api.tl](../../../Telegram/SourceFiles/mtproto/scheme/api.tl) по имени,
с параметрами в виде обычных JS-объектов, плюс поток сырых обновлений от
сервера. Это «аварийный люк»: если в `dzg.messages` / `dzg.chats` нужной
функции нет — её можно сделать через `dzg.api.call`.

Типы: [`types/api.d.ts`](../types/api.d.ts). Исходники:
`Telegram/SourceFiles/damza/plugins/api/api_mtproto.cpp` (модуль) и
`Telegram/SourceFiles/damza/plugins/tl/` (схема и кодек TL).
Автор модуля — [@wetakush](https://t.me/wetakush).

| Что | Описание |
|---|---|
| `call(method, params?, options?)` | Вызвать метод API → `Promise<объект>` |
| `inputPeer(peer)` / `inputUser(peer)` / `inputChannel(peer)` | `InputPeer` и т. п. по известным клиенту `access_hash` |
| `peerId(tlPeer)` | TL-peer (`peerChannel`, `user`...) → id чата DamZaGram |
| `onUpdate(type, fn)` | Сырые обновления (`Update`) от сервера |
| `schema(name)` | Описание метода или конструктора из схемы |
| `constructors(type)` / `search(query)` | Навигация по схеме |
| `encode(value, type?)` / `decode(data, type?)` | TL-объект ⇄ base64 бинарного вида |
| `toBase64(data)` / `fromBase64(b64, 'text'?)` | Помощники для `bytes` |
| `layer` | Слой схемы клиента (число, например `229`) |

## Быстрый старт

```js
const config = await dzg.api.call('help.getConfig');
console.log('DC:', config.this_dc, 'лимит чата:', config.chat_size_max);

const me = await dzg.api.call('users.getUsers', { id: [{ _: 'inputUserSelf' }] });
console.log(me[0].first_name, me[0].id);   // id — строка: '123456789'
```

## Как TL выглядит в JS

Имена методов, конструкторов и полей — **ровно как в api.tl** (`offset_id`,
а не `offsetId`; ошибка в имени поля подскажет правильное).

| TL | JS | Пример |
|---|---|---|
| конструктор | объект с полем `_` | `{ _: 'inputPeerSelf' }` |
| `int`, `double` | `number` | `limit: 20` |
| `long` | десятичная **строка** (можно `BigInt`, или number до 2^53) | `user_id: '777000'` |
| `string` | `string` | `message: 'Привет'` |
| `bytes`, `int128`, `int256` | base64-строка; на вход ещё `Uint8Array`, `ArrayBuffer`, массив байт или `{ text: '...' }` | `data: 'AQID'` |
| `Bool` | `true` / `false` | `increment: true` |
| `Vector<T>` | массив | `id: [1, 2, 3]` |
| `flags.N?true` | `true`, если флаг стоит; в ответе поля нет, если не стоит | `silent: true` |
| `flags.N?T` | поле есть или его нет | `reply_to: {...}` |
| `flags:#`, `flags2:#` | **не пишутся**: считаются сами, в ответах их нет | — |
| `!X` (в `invokeWithLayer` и т. п.) | объект вызова метода | `query: { _: 'help.getConfig' }` |

Удобства при вызове:

- **Peer вместо InputPeer.** Где схема ждёт `InputPeer`, `InputUser` или
  `InputChannel`, можно передать `Peer` (`dzg.chats.current()`), сообщение
  (`Message` — возьмётся `peerId`), строковый id чата или `'me'`. Клиент сам
  подставит `access_hash`.
- **`random_id` и `hash` можно не писать.** Пропущенный `random_id` станет
  случайным числом (для `Vector<long>` — по числу элементов в `id`),
  пропущенный `hash` — нулём.
- Если у типа ровно один конструктор, `_` можно не писать:
  `{ data: '{}' }` для `DataJSON`.
- Лишнее или неизвестное поле — ошибка, а не тихий пропуск.

Результат приходит в том же виде: объект с `_`, `long` — строками, `bytes` —
base64. Поля идут в порядке схемы, `_` — первым.

## `dzg.api.call(method, params?, options?)`

```ts
call(method: string, params?: object, options?: {
  timeout?: number,       // мс; 0 — ждать сколько нужно (по умолчанию)
  floodSleep?: number,    // FLOOD_WAIT до стольких секунд клиент переждёт сам (по умолчанию 10)
  applyResult?: boolean,  // отдать ответ клиенту (по умолчанию true)
  dc?: number,            // DC для запроса (продвинутое, см. ниже)
}): Promise<any>
```

Запрос уходит от активного аккаунта. Ошибки сервера отклоняют промис с
`Error`, у которого есть:

- `message` — `'400: PEER_ID_INVALID'`;
- `code` — `400`, `type` — `'PEER_ID_INVALID'`;
- `seconds` — для `FLOOD_WAIT_X`, `SLOWMODE_WAIT_X`.

Ошибки 5xx и обрывы связи клиент повторяет сам, как для своих запросов.
Локальные ошибки (нет метода, неверное поле, неизвестный peer) приходят тем
же отклонённым промисом, но без `code`. По `timeout` промис отклоняется с
`type === 'TIMEOUT'`, а запрос отменяется.

**`applyResult`.** По умолчанию клиент «видит» ответ, как будто сделал запрос
сам: применяет `Updates` (отправленное сообщение появится в чате, придут
события `message.new` и `onUpdate`) и запоминает `users` / `chats` из ответа —
после `contacts.resolveUsername` можно сразу звать `dzg.chats.get(id)` и
`dzg.api.inputPeer(id)`. `applyResult: false` — ответ видит только плагин.

**Методы, которые недоступны.** `auth.sendCode`, `auth.signIn`, `auth.signUp`,
`auth.import*Authorization`, `auth.importLoginToken`, `auth.bindTempAuthKey`:
на основном соединении они переключили бы аккаунт на другой дата-центр.

**`dc` и файлы.** `upload.get*` (например `upload.getFile`) всегда идут через
отдельное соединение для загрузок. Если файл лежит в другом DC (его
`dc_id` есть у `photo` / `document`), передай `{ dc: document.dc_id }` —
авторизацию в чужом DC клиент сделает сам. Для обычного скачивания проще
`dzg.messages.download`.

### Пример: история чата (`messages.getHistory`)

```js
async function lastTexts(peer, limit = 20) {
  const res = await dzg.api.call('messages.getHistory', {
    peer,                 // Peer, id или InputPeer
    offset_id: 0,
    offset_date: 0,
    add_offset: 0,
    limit,
    max_id: 0,
    min_id: 0,            // hash можно не передавать
  });
  // res._: 'messages.messages' | 'messages.messagesSlice' | 'messages.channelMessages'
  const users = new Map(res.users.map((u) => [u.id, u]));
  return res.messages
    .filter((m) => m._ === 'message')
    .map((m) => {
      const from = m.from_id && m.from_id._ === 'peerUser' ? users.get(m.from_id.user_id) : null;
      return `${from ? from.first_name : '?'}: ${m.message}`;
    });
}

dzg.commands.register('last', {
  description: 'Последние сообщения этого чата через сырой API',
  async run(ctx) {
    const lines = await lastTexts(ctx.chat, Number(ctx.args[0]) || 5);
    dzg.ui.alert(lines.reverse().join('\n'), 'История');
  },
});
```

Листать дальше в прошлое — передай `offset_id` = id самого старого
полученного сообщения.

### Пример: реакция (`messages.sendReaction`)

```js
// Поставить 🔥 на сообщение
await dzg.api.call('messages.sendReaction', {
  peer: dzg.chats.current(),
  msg_id: 12345,
  add_to_recent: true,
  reaction: [{ _: 'reactionEmoji', emoticon: '🔥' }],
});

// Кастомный эмодзи (Premium): document_id — строка
await dzg.api.call('messages.sendReaction', {
  peer: '777000', msg_id: 1,
  reaction: [{ _: 'reactionCustomEmoji', document_id: '5368324170671202286' }],
});

// Убрать свою реакцию: пустой список (или без поля reaction)
await dzg.api.call('messages.sendReaction', { peer: '777000', msg_id: 1, reaction: [] });
```

Флаг `flags.0` для `reaction` проставится сам, флаги `big` /
`add_to_recent` — по наличию `true`.

### Пример: полный профиль (`users.getFullUser`)

```js
async function profile(peer) {
  const res = await dzg.api.call('users.getFullUser', { id: peer });  // InputUser из Peer
  const user = res.users.find((u) => u.id === res.full_user.id);
  return {
    name: [user.first_name, user.last_name].filter(Boolean).join(' '),
    username: user.username || null,
    about: res.full_user.about || '',
    commonChats: res.full_user.common_chats_count,
    premium: !!user.premium,
    birthday: res.full_user.birthday || null,   // { _: 'birthday', day, month, year? }
  };
}

dzg.ui.addChatMenuItem({
  title: 'Профиль (raw API)',
  when: (peer) => peer.type === 'user',
  async onClick(peer) {
    try {
      const p = await profile(peer);
      dzg.ui.alert(`${p.name}\n@${p.username}\n\n${p.about}\nОбщих чатов: ${p.commonChats}`);
    } catch (e) {
      dzg.ui.toast(e.type === 'USER_ID_INVALID' ? 'Пользователь недоступен' : e.message);
    }
  },
});
```

### Пример: найти по юзернейму и написать

```js
const resolved = await dzg.api.call('contacts.resolveUsername', { username: 'durov' });
// resolved.peer: { _: 'peerUser', user_id: '...' }; users/chats уже попали в клиент
const user = resolved.users[0];

await dzg.api.call('messages.sendMessage', {
  peer: { _: 'inputPeerUser', user_id: user.id, access_hash: user.access_hash },
  message: 'Привет из DamZaGram!',
  entities: [{ _: 'messageEntityBold', offset: 0, length: 6 }],
  silent: true,
  // random_id подставится сам
});
```

### Ошибки и флуд

```js
async function safeCall(method, params) {
  for (let attempt = 0; attempt < 3; ++attempt) {
    try {
      return await dzg.api.call(method, params, { timeout: 15000, floodSleep: 0 });
    } catch (e) {
      if (e.seconds && e.seconds < 120) {
        await new Promise((r) => setTimeout(r, e.seconds * 1000));
        continue;
      }
      throw e;   // e.code, e.type, e.message === '400: MESSAGE_ID_INVALID'
    }
  }
  throw new Error('Слишком много попыток');
}
```

## `dzg.api.inputPeer` / `inputUser` / `inputChannel`

```js
dzg.api.inputPeer(dzg.chats.current());
// { _: 'inputPeerChannel', channel_id: '1234567890', access_hash: '-5462...' }
dzg.api.inputUser('me');        // { _: 'inputUserSelf' }
dzg.api.inputChannel(someGroup); // null, если это не канал / супергруппа
```

Возвращают `null`, если клиент не знает этот чат (не видел его или нет
`access_hash`). Если клиент знает чат только по сообщению, вернётся
`inputPeerUserFromMessage` / `inputChannelFromMessage` — сервер их принимает.

Обратное направление — `dzg.api.peerId(tlPeer)`: из `peerUser` /
`peerChat` / `peerChannel`, `inputPeer*` или объектов `user` / `chat` /
`channel` из ответа получается id чата DamZaGram, который понимают
`dzg.chats.get`, `dzg.messages.*` и сам `dzg.api.call`:

```js
dzg.api.onUpdate('updateNewChannelMessage', (u) => {
  const chat = dzg.chats.get(dzg.api.peerId(u.message.peer_id));   // Peer | null
  if (chat) console.log(`${chat.name}: ${u.message.message}`);
});
```

## `dzg.api.onUpdate(type, fn)` — сырые обновления

```ts
onUpdate(type: string | string[] | '*', fn: (update, meta: { self: PeerId }) => void): () => void
```

Обработчик получает объекты типа `Update` — ровно то, что прислал сервер:
`updateUserTyping`, `updateNewChannelMessage`, `updateMessageReactions`...
`meta.self` — id аккаунта, которому пришло обновление (если аккаунтов
несколько). Возвращает функцию отписки; при выгрузке плагина всё снимается
само.

- Короткие формы `updateShortMessage` / `updateShortChatMessage` (так
  приходят личные сообщения и сообщения обычных групп) превращаются в
  `updateNewMessage` с полноценным `message`, чтобы ловить всё одним типом.
- Тип проверяется по схеме: опечатка или не-`Update` (например
  `'updateShortMessage'`) — сразу `TypeError`.
- Пока никто не подписан, клиент не тратит на это ничего; подписка на
  конкретные типы дешевле, чем `'*'`.
- Обработчики вызываются асинхронно, сразу после того как клиент принял
  обновление, в порядке прихода.

### Пример: кто печатает

```js
const off = dzg.api.onUpdate(
  ['updateUserTyping', 'updateChatUserTyping', 'updateChannelUserTyping'],
  (u) => {
    const who = u.user_id || (u.from_id && (u.from_id.user_id || u.from_id.channel_id));
    const where = u.chat_id || u.channel_id || u.user_id;
    if (u.action._ === 'sendMessageTypingAction') {
      console.log(`${who} печатает в ${where}`);
    } else if (u.action._ === 'sendMessageRecordAudioAction') {
      console.log(`${who} записывает голосовое`);
    }
  });
dzg.on('unload', off);   // необязательно: отписка при выгрузке и так автоматическая
```

### Пример: все новые сообщения и правки

```js
dzg.api.onUpdate(['updateNewMessage', 'updateNewChannelMessage', 'updateEditMessage', 'updateEditChannelMessage'], (u) => {
  const m = u.message;
  if (m._ !== 'message') return;            // messageService, messageEmpty
  const chat = dzg.api.peerId(m.peer_id);
  const kind = u._.includes('Edit') ? 'правка' : 'новое';
  console.log(`[${kind}] ${chat}#${m.id}: ${m.message}`, m.media ? m.media._ : '');
});
```

### Пример: удаления и реакции

```js
dzg.api.onUpdate('updateDeleteChannelMessages', (u) => {
  console.warn(`В канале ${u.channel_id} удалены: ${u.messages.join(', ')}`);
});

dzg.api.onUpdate('updateMessageReactions', (u) => {
  const results = (u.reactions.results || []).map((r) =>
    `${r.reaction.emoticon || r.reaction.document_id}×${r.count}`);
  console.log(`Реакции на ${u.msg_id}: ${results.join(' ')}`);
});
```

### Пример: отладочный «сниффер»

```js
const counts = new Map();
dzg.api.onUpdate('*', (u) => counts.set(u._, (counts.get(u._) || 0) + 1));
dzg.commands.register('updates', () => {
  const top = [...counts].sort((a, b) => b[1] - a[1]).slice(0, 15);
  dzg.ui.alert(top.map(([t, n]) => `${n}\t${t}`).join('\n'), 'Обновления');
});
```

Чего `onUpdate` не видит: сообщения, которые клиент догружает через
`updates.getDifference` после разрыва связи, приходят не обновлениями, а
списком — для них есть событие `message.new`.

## Схема: `schema`, `constructors`, `search`

```js
dzg.api.schema('messages.getHistory');
// {
//   kind: 'method', name: 'messages.getHistory', id: 1143203525, idHex: '4423e6c5',
//   params: [{ name: 'peer', type: 'InputPeer', optional: false }, ...],
//   type: 'messages.Messages'
// }
dzg.api.schema('0x4423e6c5');            // то же по id
dzg.api.constructors('InputPeer');       // ['inputPeerEmpty', 'inputPeerSelf', 'inputPeerChat', ...]
dzg.api.search('reaction', 10);          // методы и конструкторы с 'reaction' в имени
dzg.api.layer;                           // 229
```

Удобно для дев-консоли плагина: посмотреть поля метода прямо в клиенте.

## `encode` / `decode` и байты

```js
const b64 = dzg.api.encode({ _: 'inputPeerSelf' });   // 'yX6gfQ==' (id 0x7da07ec9)
dzg.api.decode(b64);                                   // { _: 'inputPeerSelf' }
dzg.api.decode('AgAAAAAAAAA=', 'long');                // '2'
dzg.api.decode(bytes, 'Vector<int>');                  // bytes: Uint8Array

dzg.api.toBase64('привет');                // base64 UTF-8
dzg.api.fromBase64(b64);                   // Uint8Array
dzg.api.fromBase64(btnData, 'text');       // строка из UTF-8
```

Пример с `bytes`: нажать inline-кнопку бота.

```js
const msg = (await dzg.api.call('messages.getMessages', { id: [{ _: 'inputMessageID', id: 42 }] })).messages[0];
// replyInlineMarkup -> keyboardInlineButtonRow -> keyboardInlineButton { text, type }
const button = msg.reply_markup.rows[0].buttons[0];
if (button.type._ === 'inlineButtonTypeCallback') {
  console.log(button.text, 'data:', dzg.api.fromBase64(button.type.data, 'text'));
  const answer = await dzg.api.call('messages.getBotCallbackAnswer', {
    peer: dzg.api.peerId(msg.peer_id), msg_id: msg.id, data: button.type.data,  // base64 как есть
  });
  if (answer.message) dzg.ui.toast(answer.message);
}
```

## Как это устроено

- Схема `api.tl` (+ типы `mtproto.tl`) встроена в клиент и разбирается при
  первом обращении. Генератор: `tools/damza/gen_tl.py` — его нужно
  перезапускать после каждого обновления `api.tl` (сборка упадёт со
  `static_assert`, если слой встроенной схемы отстал от клиентского).
- Кодек TL (`damza/plugins/tl/tl_codec.*`) превращает JSON в бинарный TL и
  обратно, сам считает `flags`, распаковывает `gzip_packed`. Он не зависит
  от клиента; тесты: `tools/damza/tests/tl_codec_test.sh` (в том числе
  кодирование-декодирование всех ~2500 конструкторов и методов схемы).
- Запрос уходит готовыми байтами через `MTP::Instance::sendSerialized`, с теми
  же повторами, переносом между DC и `initConnection`, что и у клиента.
- Обновления: хуки в `Api::Updates::feedUpdate` и `Api::Updates::applyUpdates`
  (`api/api_updates.cpp`); без подписчиков это одна проверка флага.
- JS-часть модуля проверяется в песочнице:
  `tools/damza/sandbox.py tools/damza/tests/mtproto.plugin.js tools/damza/tests/mtproto.test.js`.
