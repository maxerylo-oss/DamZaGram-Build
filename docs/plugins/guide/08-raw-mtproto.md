# 8. Сырой MTProto

`dzg.messages`, `dzg.chats` и `dzg.users` покрывают то, что нужно чаще
всего. Всё остальное в Telegram — опросы, истории, профиль, папки,
статистика каналов, черновики, звёзды — доступно через `dzg.api`: любой
метод Telegram API по имени, с параметрами в виде обычных объектов, и поток
сырых обновлений сервера. Полный справочник модуля —
[api/mtproto.md](../api/mtproto.md).

## Первый вызов

```js
const config = await dzg.api.call('help.getConfig');
console.log('Мой DC:', config.this_dc);

const [me] = await dzg.api.call('users.getUsers', { id: [{ _: 'inputUserSelf' }] });
console.log(me.first_name, me.id);   // id — строка
```

Запрос уходит от активного аккаунта через то же соединение, что и запросы
самого клиента, — с повторами, переносом между дата-центрами и обработкой
`FLOOD_WAIT` до 10 секунд.

## Как найти нужный метод

Схема Telegram — файл
[`api.tl`](../../../Telegram/SourceFiles/mtproto/scheme/api.tl) (слой
`dzg.api.layer`). Искать удобно прямо в клиенте, в DevTools плагина:

```js
dzg.api.search('poll', 10);
// ['inputMediaPoll', 'messages.getPollResults', 'messages.getPollVotes', 'messages.sendVote', …]

dzg.api.schema('messages.getPollVotes');
// { kind: 'method', params: [{ name: 'peer', type: 'InputPeer', optional: false }, …], type: 'messages.VotesList' }

dzg.api.constructors('InputPeer');
// ['inputPeerEmpty', 'inputPeerSelf', 'inputPeerChat', 'inputPeerUser', …]
```

Описание методов словами — на [core.telegram.org/methods](https://core.telegram.org/methods).

## Как TL выглядит в JS

| TL | JS |
|---|---|
| Конструктор | Объект с полем `_`: `{ _: 'inputPeerSelf' }` |
| `int`, `double` | `number` |
| `long` | **строка** с числом: `'1234567890123'` (на вход можно и `BigInt`) |
| `string` | `string` |
| `bytes` | base64-строка (на вход ещё `Uint8Array` или `{ text: 'строка' }`) |
| `Bool` | `true` / `false` |
| `Vector<T>` | массив |
| `flags.N?true` | `true` или поле отсутствует |
| `flags.N?T` | поле есть или его нет |
| `flags:#` | не пишутся: считаются сами |

Имена полей — **ровно как в api.tl**, в snake_case: `offset_id`, а не
`offsetId`. Ошибка в имени поля подскажет правильное («did you mean
'offset_id'?»). Если у типа один конструктор, `_` можно не писать.

Удобства:

- Где ждут `InputPeer`, `InputUser`, `InputChannel` — передавай `Peer`,
  `Message`, строку id или `'me'`. `access_hash` клиент подставит сам.
- `random_id` и `hash` можно не писать — подставятся сами. Остальные
  обязательные поля (без `flags.N?`) нужно передать все, даже нули:
  `offset_id: 0`.
- Ответ применяется к клиенту: отправленные сообщения появятся в чате,
  `users`/`chats` из ответа станут известны `dzg.chats.get`. Если не нужно,
  `{ applyResult: false }`.

## Чтение ответа

Ответы — тоже TL-объекты. Почти все методы со списками возвращают рядом
массивы `users` и `chats` — по ним находят имена:

```js
const res = await dzg.api.call('messages.getHistory', {
  peer: chat, offset_id: 0, offset_date: 0, add_offset: 0, limit: 20, max_id: 0, min_id: 0,
});
const users = new Map(res.users.map((u) => [u.id, u]));
for (const m of res.messages) {
  if (m._ !== 'message') continue;                       // messageService, messageEmpty
  const fromId = m.from_id?._ === 'peerUser' ? m.from_id.user_id : null;
  const author = fromId ? users.get(fromId)?.first_name : '—';
  console.log(`${author}: ${m.message}`);
}
```

Перевести TL-peer в id для остального API — `dzg.api.peerId(m.peer_id)`.

## Примеры

### Статус «не в сети»

```js
await dzg.api.call('account.updateStatus', { offline: true });
```

### Сменить био

```js
await dzg.api.call('account.updateProfile', { about: 'DamZaGram by @wetakush' });
```

### Сколько участников онлайн

```js
const { onlines } = await dzg.api.call('messages.getOnlines', { peer: dzg.chats.current() });
dzg.ui.toast(`Сейчас в сети: ${onlines}`);
```

### Отложенные сообщения чата

```js
const res = await dzg.api.call('messages.getScheduledHistory', { peer: chat });
const lines = res.messages
  .filter((m) => m._ === 'message')
  .map((m) => `${new Date(m.date * 1000).toISOString()} ${m.message}`);
```

### Опрос

```js
await dzg.api.call('messages.sendMedia', {
  peer: dzg.chats.current(),
  message: '',
  media: {
    _: 'inputMediaPoll',
    poll: {
      _: 'poll',
      id: '0',
      question: { text: 'Обед?', entities: [] },
      answers: ['Пицца', 'Суши', 'Шаурма'].map((text, i) => ({
        _: 'pollAnswer',
        text: { text, entities: [] },
        option: dzg.api.toBase64(String(i)),
      })),
    },
  },
});
```

## Ошибки и флуд

Ошибка сервера отклоняет промис с `Error`, у которого есть `code` (`400`),
`type` (`'PEER_ID_INVALID'`), а для `FLOOD_WAIT_X` ещё и `seconds`:

```js
try {
  await dzg.api.call('channels.joinChannel', { channel: chat });
} catch (e) {
  if (e.type === 'CHANNELS_TOO_MUCH') dzg.ui.toast('Слишком много каналов');
  else if (e.seconds) dzg.ui.toast(`Подожди ${e.seconds} с`);
  else throw e;
}
```

| Опция `call` | По умолчанию | Описание |
|---|---|---|
| `timeout` | `0` | Через сколько мс отменить запрос (ошибка `TIMEOUT`) |
| `floodSleep` | `10` | `FLOOD_WAIT` до стольких секунд клиент переждёт и повторит сам |
| `applyResult` | `true` | Отдать ответ клиенту |
| `dc` | — | Отправить в конкретный дата-центр (для `upload.getFile`) |

Методы входа (`auth.sendCode`, `auth.signIn` и подобные) запрещены: они
могли бы переключить аккаунт на другой дата-центр.

## Сырые обновления

```js
const off = dzg.api.onUpdate('updateMessagePollVote', (update, meta) => {
  console.log('Голос в опросе', update.poll_id, 'от', dzg.api.peerId(update.peer), 'аккаунт', meta.self);
});

dzg.api.onUpdate(['updateNewMessage', 'updateNewChannelMessage'], (u) => {
  if (u.message._ === 'message' && u.message.media?._ === 'messageMediaDice') {
    console.log('Кто-то бросил кубик:', u.message.media.value);
  }
});
```

- Тип проверяется по схеме: опечатка — сразу `TypeError`.
- `'*'` — все обновления (удобно для изучения, но дороже).
- Личные сообщения в коротком виде (`updateShortMessage`) приходят как
  `updateNewMessage` — ловить нужно один тип.
- Приходят обновления всех аккаунтов; `meta.self` — id аккаунта.
- Сообщения, которые клиент догружает после разрыва связи, приходят не
  обновлениями — для них есть `message.new`.

## Байты

```js
dzg.api.toBase64('привет');               // строка -> base64 (UTF-8)
dzg.api.fromBase64(b64);                   // -> Uint8Array
dzg.api.fromBase64(b64, 'text');           // -> строка
dzg.api.encode({ _: 'inputPeerSelf' });    // TL -> base64 бинарного вида
dzg.api.decode(b64, 'Object');             // обратно
```

## Что дальше

- [api/mtproto.md](../api/mtproto.md) — все подробности, кнопки ботов через
  `bytes`, сниффер обновлений.
- [Отладка](09-debugging.md) — DevTools удобен, чтобы пробовать методы
  вживую.
