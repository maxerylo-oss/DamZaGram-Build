# 4. Чаты и пользователи

`dzg.chats` — всё про чаты: список слева, папки, поиск по юзернейму,
вступление и выход, уведомления, архив, участники и полная информация.
`dzg.users` — профили, статусы, контакты и блокировка. `dzg.accounts` —
аккаунты, в которые выполнен вход. Полные описания — на страницах
[api/chats.md](../api/chats.md) и [api/users.md](../api/users.md).

## `Peer` — чат или человек

```js
const peer = {
  id: '…',          // строка: используй её во всех вызовах API
  tgId: 1234567890, // «голый» Telegram id, как в Bot API и t.me/c/…
  name: 'DamZaGram chat',
  username: 'damzagram',
  type: 'group',    // 'user' | 'bot' | 'group' | 'channel' | 'unknown'
  isSelf: false,    // это ты сам («Избранное»)
};
```

Получить `Peer`:

```js
dzg.chats.current();                    // открытый сейчас чат или null
dzg.chats.get(id);                      // по id — только известные клиенту
dzg.chats.find('durov');                // по юзернейму среди загруженных
await dzg.chats.resolve('t.me/durov');  // спросить сервер (и номер телефона тоже)
dzg.users.self();                       // ты
```

`find` ищет только среди того, что клиент уже знает, и работает синхронно.
`resolve` спрашивает сервер и понимает `durov`, `@durov`, `t.me/durov`,
`tg://resolve?domain=durov` и номера `+79991234567`.

## Список чатов и папки

```js
const all = dzg.chats.list({ limit: 0 });                      // все загруженные
const unread = dzg.chats.list({ unreadOnly: true });           // с непрочитанными
const groups = dzg.chats.list({ type: ['group', 'channel'] }); // только группы и каналы
const archived = dzg.chats.list({ folder: 'archive' });

for (const folder of dzg.chats.folders()) {
  const chats = dzg.chats.list({ folder: folder.id, limit: 0 });
  console.log(`${folder.emoji} ${folder.title}: ${chats.length}`);
}
```

Каждый элемент — `Peer` плюс состояние: `unread`, `unreadMark`, `mentions`,
`reactions`, `pinned`, `muted`, `archived`, `lastMessage`. Порядок — как в
списке слева. Список содержит только то, что клиент уже загрузил (при
прокрутке списка он догружает остальное). Состояние одного чата —
`dzg.chats.entry(chat)`.

## Действия с чатом

```js
dzg.chats.open(chat);                       // открыть
dzg.chats.open(chat, 4512);                 // и прокрутить к сообщению
dzg.chats.mute(chat, 8 * 3600);             // выключить уведомления на 8 часов
dzg.chats.mute(chat);                       // навсегда
dzg.chats.unmute(chat);
dzg.chats.archive(chat);                    // в архив; archive(chat, false) — обратно
dzg.chats.pin(chat);                        // закрепить; pin(chat, false) — открепить
dzg.chats.markRead(chat);                   // прочитать всё
dzg.chats.markUnread(chat);                 // пометить непрочитанным
dzg.chats.setTyping(chat, 'typing');        // показать «печатает…» ~5 секунд
dzg.chats.clearHistory(chat);               // очистить историю у себя
```

Эти методы синхронные и возвращают `true`, если действие принято.

`setTyping` держит статус около 5 секунд — для долгого статуса повторяй раз в
4–5 секунд, `'cancel'` снимает. Доступные действия — в
`dzg.chats.typingActions`. Режим призрака AyuGram уважается.

## Вступить, выйти, создать

```js
const chat = await dzg.chats.join('https://t.me/+AbCdEfGh123');   // по приглашению
await dzg.chats.join('@damzagram');                                // публичный
await dzg.chats.leave(chat);

const group = await dzg.chats.create({
  title: 'Тестовая группа',
  type: 'supergroup',
  about: 'Создана плагином',
  users: [friend],
});
await dzg.chats.addMembers(group, [user1, user2]);
const link = await dzg.chats.createInviteLink(group, { usageLimit: 10 });
await dzg.chats.setTitle(group, 'Новое имя');
await dzg.chats.kick(group, user1, { ban: true });
```

Ошибки — коды сервера: `INVITE_HASH_EXPIRED`, `INVITE_REQUEST_SENT`,
`CHANNELS_TOO_MUCH`, `CHAT_ADMIN_REQUIRED`…

## Участники и информация

```js
const members = await dzg.chats.members(chat, { limit: 1000 });
const admins = members.filter((m) => m.role === 'admin' || m.role === 'creator');

const info = await dzg.chats.info(chat);
console.log(info.about, info.membersCount, info.onlineCount, info.inviteLink);
```

`members` возвращает `Peer` с `role` (`creator`, `admin`, `member`,
`restricted`, `left`, `banned`) и `rank`. Для каналов нужны права админа.
`info` спрашивает сервер: описание, число участников и онлайн, ссылку,
связанный чат, медленный режим, твои права.

## Пользователи

```js
const me = dzg.users.self();
const status = dzg.users.status(user);   // из памяти: { online, lastSeen, kind, text }
const full = await dzg.users.info(user); // с сервера: bio, phone, birthday, commonChats…
const contacts = dzg.users.contacts();

await dzg.users.block(spammer);
await dzg.users.unblock(spammer);
const common = await dzg.users.commonChats(user);
await dzg.users.addContact(user, { firstName: 'Паша', lastName: 'Дуров' });
```

`status.text` — готовая строка на языке клиента («в сети», «был(а)
недавно»). `kind` — `online`, `offline`, `recently`, `lastWeek`,
`lastMonth`, `longAgo` или `hidden`. Изменения статуса приходят событием
[`user.status`](02-events.md#userstatus).

## Несколько аккаунтов

```js
for (const account of dzg.accounts.list()) {
  console.log(account.index, account.self.name, account.active ? '(активный)' : '');
}
dzg.accounts.switchTo(1);
```

Весь API работает с **активным** аккаунтом. После `switchTo` переключение
происходит чуть позже; если аккаунт открывается впервые за запуск, придёт
`session.ready`.

## Пример: отчёт по непрочитанным

```js
dzg.commands.register('inbox', ({ chat }) => {
  const lines = dzg.chats.list({ unreadOnly: true, limit: 0 })
    .filter((c) => !c.muted)
    .sort((a, b) => b.unread - a.unread)
    .slice(0, 15)
    .map((c) => `${c.mentions ? '@ ' : ''}${c.name} — ${c.unread}`);
  dzg.messages.send(dzg.users.self(), lines.length
    ? '**Непрочитанное:**\n' + lines.join('\n')
    : 'Всё прочитано', { parse: 'markdown' });
  dzg.chats.open(dzg.users.self());
});
```

## Что дальше

- [Интерфейс](05-ui.md) — пункты меню чата, кнопки в шапке.
- [api/chats.md](../api/chats.md) — пример «тихий час» и все детали.
- [api/users.md](../api/users.md) — статусы, блокировка, аккаунты.
