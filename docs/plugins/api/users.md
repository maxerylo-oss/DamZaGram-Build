# `dzg.users` и `dzg.accounts` — люди и аккаунты

`dzg.users` отвечает за пользователей: кто ты, профиль собеседника,
статус «в сети», контакты, блокировка. `dzg.accounts` — за аккаунты,
в которые выполнен вход, и переключение между ними.

Типы: [`types/users.d.ts`](../types/users.d.ts). Исходник модуля:
`Telegram/SourceFiles/damza/plugins/api/api_users.cpp`.
Автор модуля — [@wetakush](https://t.me/wetakush).

Правила те же, что и в [`dzg.chats`](chats.md): вместо пользователя можно
передать `Peer` или его `id`; синхронные методы не бросают исключений,
асинхронные возвращают `Promise`, который отклоняется с `Error` (в
`message` — код ошибки Telegram).

## `dzg.users`

| Метод | Что делает | Результат |
|---|---|---|
| `self()` | Ты сам (активный аккаунт) | `Peer \| null` |
| `get(peer)` | `Peer` по id (как `dzg.chats.get`) | `Peer \| null` |
| `status(peer)` | Статус «в сети» из памяти | `UserStatus \| null` |
| `isBlocked(peer)` | Заблокирован ли | `boolean` |
| `info(peer)` | Полный профиль с сервера | `Promise<UserInfo>` |
| `contacts()` | Контакты | `Peer[]` |
| `block(peer, blocked = true)` / `unblock(peer)` | Блокировка | `Promise<void>` |
| `commonChats(peer, { limit })` | Общие группы | `Promise<Peer[]>` |
| `addContact(peer, { firstName, lastName, phone, sharePhone })` | Добавить в контакты | `Promise<void>` |
| `deleteContact(peer)` | Удалить из контактов | `Promise<void>` |

Событие: `user.status` (см. [ниже](#событие-userstatus)).

### `dzg.users.self()`

```js
const me = dzg.users.self();
dzg.log(`Я — ${me.name} (@${me.username || 'без юзернейма'}), id ${me.id}`);
```

### `dzg.users.status(peer)`

Статус из памяти клиента, без запросов:

```ts
{
  online: false,
  lastSeen: 1760248800,     // unix time; когда в сети — текущее время; 0 если скрыто
  kind: 'offline',          // online | offline | recently | lastWeek | lastMonth | longAgo | hidden
  text: 'был(а) 5 минут назад'  // готовая строка на языке клиента
}
```

`null`, если это не пользователь или он неизвестен клиенту.

### `dzg.users.info(peer)`

Запрашивает полный профиль (`users.getFullUser`) и обновляет его в клиенте.

| Поле | Описание |
|---|---|
| `peer` | `Peer` |
| `bio` | «О себе» |
| `phone` | телефон без `+`, если виден, иначе `''` |
| `firstName`, `lastName` | имя и фамилия |
| `isContact`, `isPremium`, `isBot`, `isVerified`, `isScam`, `isFake` | флаги |
| `isBlocked` | ты его заблокировал |
| `commonChats` | число общих групп |
| `status` | `UserStatus`, как у `status()` |
| `birthday` | `{ day, month, year }` или `null`; `year` = 0, если год скрыт |

```js
dzg.ui.addChatMenuItem({
  title: 'Кто это?',
  when: (peer) => peer.type === 'user',
  async onClick(peer) {
    const u = await dzg.users.info(peer);
    const lines = [
      u.bio || '(без био)',
      `Статус: ${u.status.text}`,
      `Общих групп: ${u.commonChats}`,
      u.isPremium ? 'Premium' : '',
      u.birthday ? `ДР: ${u.birthday.day}.${u.birthday.month}` : '',
    ].filter(Boolean);
    dzg.ui.alert(lines.join('\n'), u.peer.name);
  },
});
```

### `dzg.users.contacts()`

Контакты, которые загрузил клиент (`Peer[]`, без запросов).

```js
const online = dzg.users.contacts().filter((c) => dzg.users.status(c)?.online);
dzg.ui.toast(`Контактов в сети: ${online.length}`);
```

### `dzg.users.block(peer, blocked = true)` / `dzg.users.unblock(peer)`

Блокирует или разблокирует пользователя (или канал/бота). Промис
выполняется, когда сервер подтвердил действие, после этого
`isBlocked(peer)` возвращает новое значение.

```js
await dzg.users.block(spammer);
dzg.ui.toast(`${spammer.name} заблокирован`);
```

### `dzg.users.commonChats(peer, { limit = 100 }?)`

Общие группы и каналы с пользователем (до 100), с сервера.

```js
const common = await dzg.users.commonChats(peer);
dzg.ui.alert(common.map((c) => c.name).join('\n') || 'Нет общих чатов');
```

### `dzg.users.addContact(peer, options?)` / `dzg.users.deleteContact(peer)`

Добавляет пользователя в контакты (или переименовывает контакт) и удаляет
из них. Пустые `firstName`, `lastName`, `phone` берутся из профиля.
`sharePhone: true` покажет этому человеку твой номер.

```js
await dzg.users.addContact(peer, { firstName: 'Паша', lastName: '(работа)' });
```

### Событие `user.status`

```ts
{ user: Peer; online: boolean; lastSeen: number; kind: UserStatusKind }
```

Пользователь появился в сети или вышел из неё. Повторные одинаковые
статусы отбрасываются, боты не учитываются, событие приходит только для
активного аккаунта. Сервер присылает статусы контактов и собеседников
из открытых чатов.

Первые 15 секунд после входа в аккаунт клиент загружает список чатов и
узнаёт статусы сотен людей сразу. В это время событие приходит только
для тех, чей статус уже был известен и изменился, — плагин не получит
лавину событий на старте.

```js
// Уведомлять, когда избранные люди появляются в сети
const watch = new Set(['123456789']);
dzg.on('user.status', ({ user, online }) => {
  if (online && watch.has(user.id)) dzg.ui.toast(`${user.name} в сети`);
});
```

## `dzg.accounts`

| Метод | Что делает | Результат |
|---|---|---|
| `list()` | Аккаунты, в которые выполнен вход | `{ index, self, active, premium }[]` |
| `current()` | Активный аккаунт | `AccountEntry \| null` |
| `switchTo(index)` | Переключиться | `boolean` |

`index` — внутренний номер аккаунта в клиенте, он не обязательно идёт
подряд (0, 2, 3...). `switchTo` принимает и номер, и объект из `list()`.
Переключение происходит сразу после текущего вызова. После него все методы
API работают с новым аккаунтом; если аккаунт открывается впервые за сессию,
придёт событие `session.ready`.

```js
// Команда /acc: показать аккаунты и переключиться на следующий
dzg.commands.register('acc', () => {
  const list = dzg.accounts.list();
  const i = list.findIndex((a) => a.active);
  const next = list[(i + 1) % list.length];
  if (next && !next.active) {
    dzg.ui.toast(`Переключаюсь на ${next.self.name}`);
    dzg.accounts.switchTo(next);
  }
});
```

События `user.status`, `chat.typing` и `chat.unread` приходят только для
активного аккаунта.
