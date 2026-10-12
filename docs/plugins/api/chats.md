# `dzg.chats` — чаты, папки, участники

Модуль расширяет базовые `dzg.chats.current / get / find / open` всем, что
можно сделать с чатом из списка слева: прочитать счётчики, выключить
уведомления, убрать в архив, закрепить, вступить и выйти, показать
«печатает…», получить участников и полную информацию с сервера.

Типы: [`types/chats.d.ts`](../types/chats.d.ts). Исходник модуля:
`Telegram/SourceFiles/damza/plugins/api/api_chats.cpp`.
Автор модуля — [@wetakush](https://t.me/wetakush).
Готовый пример со всеми возможностями модуля —
[`plugins/examples/chat-tools`](../../../plugins/examples/chat-tools/main.js).

Общие правила:

- Везде, где нужен чат, можно передать объект `Peer` или его `id` (строку).
- Синхронные методы не бросают исключений, если чат не найден: они
  возвращают `null`, `false` или `[]`.
- Асинхронные методы возвращают `Promise`. При ошибке он отклоняется с
  `Error`, в `message` — код ошибки Telegram (`FLOOD_WAIT_10`,
  `CHANNEL_PRIVATE`, `INVITE_HASH_EXPIRED`...) или понятное описание.
- Всё работает с **активным** аккаунтом (см. [`dzg.accounts`](users.md#dzgaccounts)).

## Шпаргалка

| Метод | Что делает | Результат |
|---|---|---|
| `list({ folder, limit, unreadOnly, type })` | Чаты из списка слева | `ChatListEntry[]` |
| `entry(peer)` | Счётчики и состояние одного чата | `ChatListEntry \| null` |
| `folders()` | Папки чатов | `{ id, title, emoji, chatlist }[]` |
| `resolve(username)` | Найти чат или человека на сервере | `Promise<Peer \| null>` |
| `join(usernameOrLink)` | Вступить по юзернейму или приглашению | `Promise<Peer>` |
| `leave(peer)` | Выйти из группы или канала | `Promise<void>` |
| `mute(peer, seconds?)` / `unmute(peer)` | Уведомления | `boolean` |
| `archive(peer, archived = true)` | Архив | `boolean` |
| `pin(peer, pinned = true, { folder })` | Закрепление | `boolean` |
| `markUnread(peer, unread = true)` / `markRead(peer)` | Прочитано / непрочитано | `boolean` |
| `setTyping(peer, action = 'typing', { topicId })` | Статус «печатает…» | `boolean` |
| `members(peer, { limit, query })` | Участники | `Promise<ChatMember[]>` |
| `info(peer)` | Описание, число участников, ссылка... | `Promise<ChatInfo>` |
| `create({ title, type, about, users, forum })` | Создать группу или канал | `Promise<Peer>` |
| `addMembers(peer, users)` | Пригласить людей | `Promise<{ added, missing }>` |
| `kick(peer, user, { ban })` | Удалить участника | `Promise<void>` |
| `setTitle(peer, title)` / `setAbout(peer, about)` | Название и описание | `Promise<void>` |
| `createInviteLink(peer, options)` | Новая ссылка-приглашение | `Promise<string>` |
| `clearHistory(peer, { revoke })` | Очистить историю | `boolean` |

События: `chat.typing`, `chat.unread` (см. [ниже](#события)).

## Список чатов

### `dzg.chats.list(options?)`

Возвращает чаты, которые клиент уже загрузил, в том же порядке, что и в
списке слева: закреплённые сверху, дальше по дате. К серверу не ходит, поэтому
вызов быстрый, но в списке будут только загруженные чаты (листание списка
в клиенте подгружает следующие).

| Опция | По умолчанию | Описание |
|---|---|---|
| `folder` | `0` | `0` — основной список, `'archive'` — архив, число — id папки из `folders()` |
| `limit` | `100` | Сколько вернуть. `0` — все загруженные |
| `unreadOnly` | `false` | Только чаты с непрочитанными или пометкой «непрочитано» |
| `type` | — | `'user'`, `'bot'`, `'group'`, `'channel'` или массив таких строк |

Каждый элемент — это `Peer` с дополнительными полями:

```ts
{
  id, tgId, name, username, type, isSelf,   // как у Peer
  unread: 3,            // непрочитанные сообщения
  unreadMark: false,    // помечен «непрочитанным» вручную
  mentions: 1,          // непрочитанные упоминания
  reactions: 0,         // непрочитанные реакции
  pinned: true,         // закреплён в этой папке
  muted: false,
  archived: false,
  lastMessage: { ... }  // Message или null
}
```

```js
// Сводка непрочитанного в группах
const groups = dzg.chats.list({ unreadOnly: true, type: 'group', limit: 0 });
const total = groups.reduce((sum, c) => sum + c.unread, 0);
dzg.ui.toast(`${groups.length} групп, ${total} непрочитанных`);
```

### `dzg.chats.entry(peer)`

То же самое для одного чата: удобно узнать, выключены ли уведомления или
в архиве ли чат, не перебирая весь список. `pinned` здесь относится к
основному списку.

```js
const chat = dzg.chats.current();
const state = chat && dzg.chats.entry(chat);
if (state && state.muted) dzg.log(`${chat.name}: уведомления выключены`);
```

### `dzg.chats.folders()`

Папки пользователя: `{ id, title, emoji, chatlist }`. Папка «Все чаты» в
список не входит — это `folder: 0`.

```js
for (const folder of dzg.chats.folders()) {
  const unread = dzg.chats.list({ folder: folder.id, unreadOnly: true, limit: 0 });
  dzg.log(`${folder.emoji} ${folder.title}: ${unread.length}`);
}
```

## Поиск, вступление, выход

### `dzg.chats.resolve(username)`

Спрашивает сервер, кому принадлежит юзернейм. Понимает разные формы записи:
`durov`, `@durov`, `https://t.me/durov`, `tg://resolve?domain=durov`, а также
номер телефона (`+7 999 123-45-67`, работает, если номер разрешено искать).
Возвращает `Peer` или `null`, если никого нет.

```js
const peer = await dzg.chats.resolve('@telegram');
if (peer) dzg.chats.open(peer);
```

В отличие от `dzg.chats.find`, который ищет только среди уже загруженных
чатов, `resolve` всегда делает запрос. Не зови его в цикле: сервер быстро
ответит `FLOOD_WAIT_N`.

### `dzg.chats.join(usernameOrInviteLink)`

Вступает в публичную группу или канал по юзернейму / ссылке или в любой чат
по приглашению:

- `durov`, `@channel`, `https://t.me/channel`;
- `https://t.me/+AbCdEf`, `https://t.me/joinchat/AbCdEf`, `tg://join?invite=AbCdEf`, `+AbCdEf`;
- объект `Peer` уже известного канала.

Возвращает чат, в который вступили. Если ты уже участник — просто вернёт его.
Для пользователей и ботов ничего не делает и возвращает их `Peer`.

Частые ошибки: `INVITE_HASH_EXPIRED` (ссылка устарела), `INVITE_REQUEST_SENT`
(в чат нужно одобрение — заявка отправлена), `CHANNELS_TOO_MUCH` (лимит
каналов), `JOIN_VERIFICATION_REQUIRED` (чат требует проверку ботом — клиент
открыл окно проверки, после неё пользователь окажется в чате).

```js
try {
  const chat = await dzg.chats.join('https://t.me/+AbCdEf123456');
  dzg.ui.toast(`Вступил в «${chat.name}»`);
} catch (e) {
  if (e.message === 'INVITE_REQUEST_SENT') dzg.ui.toast('Заявка отправлена');
  else dzg.ui.toast('Не вышло: ' + e.message);
}
```

### `dzg.chats.leave(peer)`

Выходит из группы или канала. История чата остаётся, удалить её можно
вручную. Для личных чатов промис отклоняется.

## Уведомления, архив, закрепление, прочитанность

Эти методы синхронные: они сразу меняют состояние в клиенте, а запрос к
серверу уходит в фоне. Возвращают `true`, если действие принято.

### `dzg.chats.mute(peer, seconds?)`

| `seconds` | Что будет |
|---|---|
| не задан, `true`, `0` | выключить навсегда |
| число > 0 | выключить на столько секунд |
| `false` | включить обратно (то же, что `unmute(peer)`) |

```js
dzg.chats.mute(chat, 8 * 3600);  // на 8 часов
dzg.chats.unmute(chat);
```

### `dzg.chats.archive(peer, archived = true)`

Убирает чат в архив или возвращает из него. Возвращает `false`, если чат
нельзя архивировать (например, «Избранное» или служебные уведомления).

### `dzg.chats.pin(peer, pinned = true, { folder }?)`

Закрепляет или открепляет чат в основном списке или в папке `folder`.
Работает через тот же код, что и пункт меню «Закрепить», поэтому при
превышении лимита клиент сам покажет окно про лимит. Возвращает `false`,
если у активного аккаунта нет открытого окна.

### `dzg.chats.markUnread(peer, unread = true)` / `dzg.chats.markRead(peer)`

`markUnread(peer)` ставит пометку «непрочитано» (как в контекстном меню).
`markRead(peer)` или `markUnread(peer, false)` читает все сообщения и снимает
пометку. Режим призрака AyuGram действует как обычно.

```js
// Прочитать все каналы одной командой
dzg.commands.register('readchannels', () => {
  const channels = dzg.chats.list({ type: 'channel', unreadOnly: true, limit: 0 });
  channels.forEach((c) => dzg.chats.markRead(c));
  dzg.ui.toast(`Прочитано каналов: ${channels.length}`);
});
```

## «Печатает…»

### `dzg.chats.setTyping(peer, action = 'typing', { topicId }?)`

Показывает собеседникам статус. Действия (`dzg.chats.typingActions`):

| Действие | Статус |
|---|---|
| `typing` | печатает |
| `record_voice` / `upload_voice` | записывает / отправляет голосовое |
| `record_video` / `upload_video` | записывает / отправляет видео |
| `record_round` / `upload_round` | записывает / отправляет кружок |
| `upload_photo` / `upload_document` | отправляет фото / файл |
| `choose_sticker` | выбирает стикер |
| `choose_location` / `choose_contact` | выбирает место / контакт |
| `play_game` | играет |
| `cancel` | снять статус |

Один вызов держит статус примерно 5 секунд. Повторные вызовы чаще раза в
5 секунд клиент не отправляет, так что вызывать можно из таймера.
Неизвестное действие бросает `TypeError`. Если в настройках призрака AyuGram
выключена отправка статусов, ничего не уходит. В каналах статусы не
показываются, в «Избранном» — тоже.

```js
// «Записывает голосовое» 20 секунд
const chat = dzg.chats.current();
const timer = setInterval(() => dzg.chats.setTyping(chat, 'record_voice'), 4000);
dzg.chats.setTyping(chat, 'record_voice');
setTimeout(() => { clearInterval(timer); dzg.chats.setTyping(chat, 'cancel'); }, 20000);
```

## Участники и информация

### `dzg.chats.members(peer, { limit = 200, query }?)`

Загружает участников с сервера. В супергруппах и каналах — постранично по
200, пока не наберётся `limit` (максимум 10000). Каждый участник — `Peer` с
полями `role` (`creator`, `admin`, `member`, `restricted`, `left`, `banned`)
и `rank` (должность, если есть). `query` ищет по имени и юзернейму.

В каналах список участников видят только админы: иначе промис отклонится
с `CHAT_ADMIN_REQUIRED`. Для личных чатов — ошибка.

```js
const admins = (await dzg.chats.members(chat, { limit: 1000 }))
  .filter((m) => m.role === 'admin' || m.role === 'creator');
dzg.ui.alert(admins.map((a) => `${a.name}${a.rank ? ` (${a.rank})` : ''}`).join('\n'), 'Админы');
```

### `dzg.chats.info(peer)`

Запрашивает полную информацию (как при открытии профиля) и обновляет её в
клиенте:

| Поле | Описание |
|---|---|
| `peer` | `Peer` |
| `about` | описание, для пользователя — био |
| `membersCount` | участников (0 у личных чатов) |
| `onlineCount` | сейчас в сети |
| `inviteLink` | основная ссылка-приглашение или `''` |
| `linkedChat` | группа обсуждения канала / канал группы или `null` |
| `slowmodeSeconds` | медленный режим, 0 — выключен |
| `amIn`, `isCreator`, `isAdmin` | твоё положение в чате |
| `isForum`, `isVerified`, `isScam`, `isFake` | флаги чата |

```js
const info = await dzg.chats.info(dzg.chats.current());
dzg.ui.alert(
  `${info.peer.name}\n${info.about}\n\n` +
  `Участников: ${info.membersCount}, в сети: ${info.onlineCount}` +
  (info.linkedChat ? `\nОбсуждение: ${info.linkedChat.name}` : ''),
  'О чате');
```

## Управление группами и каналами

Для этих методов нужны соответствующие права админа, иначе промис
отклонится с кодом вроде `CHAT_ADMIN_REQUIRED`.

### `dzg.chats.create(options)`

| Опция | По умолчанию | Описание |
|---|---|---|
| `title` | — | Название, обязательно |
| `type` | `'group'` | `'group'`, `'supergroup'` или `'channel'` |
| `about` | `''` | Описание |
| `users` | `[]` | Кого пригласить сразу (`Peer` или id) |
| `forum` | `false` | Супергруппа с темами |

Вместо объекта можно передать строку — название обычной группы.
Возвращает `Peer` созданного чата.

```js
const team = await dzg.chats.create({
  title: 'Команда',
  type: 'supergroup',
  about: 'Создано плагином',
  users: dzg.users.contacts().slice(0, 3),
});
dzg.chats.open(team);
```

### `dzg.chats.addMembers(peer, users)`

Приглашает одного или нескольких пользователей. Результат —
`{ added, missing }`, где `missing` — id тех, кого добавить не получилось
(например, они запретили себя приглашать).

### `dzg.chats.kick(peer, user, { ban }?)`

Удаляет участника. В супергруппах и каналах без `ban` человек сможет
вернуться по ссылке, с `ban: true` — попадёт в чёрный список чата.

### `dzg.chats.setTitle(peer, title)` / `dzg.chats.setAbout(peer, about)`

Переименовывают группу или канал и меняют описание. Если значение не
изменилось, промис всё равно выполняется.

### `dzg.chats.createInviteLink(peer, options?)`

Создаёт новую ссылку-приглашение и возвращает её текст.
Опции: `expireDate` (unix time), `usageLimit`, `title`, `requestNeeded`
(вступление по заявке).

```js
const link = await dzg.chats.createInviteLink(chat, {
  expireDate: Math.floor(Date.now() / 1000) + 24 * 3600,
  usageLimit: 10,
  title: 'На сутки',
});
dzg.clipboard.set(link);
```

### `dzg.chats.clearHistory(peer, { revoke }?)`

Очищает историю чата, как пункт меню «Очистить историю». `revoke: true`
удаляет сообщения и у собеседника, где это возможно. Сообщения,
сохранённые AyuGram, ведут себя как при ручной очистке. Очистка начинается
не внутри вызова, а сразу после того, как твой код вернёт управление
клиенту, поэтому её можно вызывать и из обработчика `message.new`. Из-за
этого сообщение, отправленное тем же кодом сразу после `clearHistory`, уйдёт
раньше очистки, и она уберёт его из чата у тебя (на сервере оно останется).
Такое сообщение отправляй с небольшой задержкой, например через `setTimeout`.

## События

### `chat.typing`

```ts
{ chat: Peer; user: Peer; action: TypingAction; topicId: number }
```

Кто-то в чате `chat` печатает (или записывает, выбирает стикер...).
Повторы одного и того же действия приходят не чаще раза в 4 секунды. Когда
действие закончилось — явно или по таймауту (~6.5 с без обновлений), —
приходит то же событие с `action: 'cancel'`. `topicId` — id темы в форуме
или 0. Приходит только для активного аккаунта, твои собственные действия
не приходят. Значение `import_history` означает импорт истории чата.

```js
dzg.on('chat.typing', ({ chat, user, action }) => {
  if (action === 'record_voice') dzg.ui.toast(`${user.name} записывает голосовое в «${chat.name}»`);
});
```

### `chat.unread`

```ts
{ chat: Peer; unread: number; unreadMark: boolean; mentions: number }
```

Число непрочитанных в чате изменилось (пришло сообщение, ты прочитал чат,
поставил пометку). Изменения собираются пачкой за 300 мс и повторно не
присылаются, если значения не поменялись. Чаты с нулём непрочитанных при
первой загрузке не присылаются.

```js
// Счётчик непрочитанного в заголовке своей панели
dzg.on('chat.unread', ({ chat, unread }) => {
  if (unread > 50) dzg.log(`${chat.name}: уже ${unread} непрочитанных`);
});
```

## Полный пример: «тихий час»

Плагин на ночь выключает уведомления у всех групп с непрочитанным, а утром
включает обратно.

```js
const muted = new Set(dzg.storage.get('muted', []));

dzg.commands.register('quiet', {
  description: 'Выключить уведомления групп на 8 часов',
  run() {
    for (const chat of dzg.chats.list({ type: 'group', limit: 0 })) {
      if (!chat.muted && dzg.chats.mute(chat, 8 * 3600)) muted.add(chat.id);
    }
    dzg.storage.set('muted', [...muted]);
    dzg.ui.toast(`Тихий час: ${muted.size} групп`);
  },
});

dzg.commands.register('loud', () => {
  muted.forEach((id) => dzg.chats.unmute(id));
  muted.clear();
  dzg.storage.set('muted', []);
});
```
