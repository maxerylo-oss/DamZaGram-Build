// Chat Tools — пример модулей dzg.chats, dzg.users и dzg.accounts.
// Автор: @wetakush (https://t.me/wetakush).

const plural = (n, one, few, many) => {
  const a = n % 10, b = n % 100;
  if (a === 1 && b !== 11) return one;
  if (a >= 2 && a <= 4 && (b < 12 || b > 14)) return few;
  return many;
};

// /unread — сводка непрочитанного по папкам.
dzg.commands.register('unread', {
  description: 'Сколько непрочитанного и где',
  run() {
    const lines = [];
    const add = (title, chats) => {
      const total = chats.reduce((sum, c) => sum + c.unread, 0);
      if (chats.length) lines.push(`${title}: ${chats.length} ${plural(chats.length, 'чат', 'чата', 'чатов')}, ${total} сообщ.`);
    };
    add('Все чаты', dzg.chats.list({ unreadOnly: true, limit: 0 }));
    for (const folder of dzg.chats.folders()) {
      add(`${folder.emoji} ${folder.title}`.trim(), dzg.chats.list({ folder: folder.id, unreadOnly: true, limit: 0 }));
    }
    add('Архив', dzg.chats.list({ folder: 'archive', unreadOnly: true, limit: 0 }));
    dzg.ui.alert(lines.join('\n') || 'Всё прочитано', 'Непрочитанное');
  },
});

// /readall [channels|groups|users] — прочитать всё (или только чаты одного типа).
dzg.commands.register('readall', {
  description: 'Прочитать все чаты: /readall [channels|groups|users]',
  async run({ args }) {
    const types = {
      channels: ['channel'],
      groups: ['group'],
      users: ['user', 'bot'],
    }[args[0]];
    const chats = dzg.chats.list({ unreadOnly: true, limit: 0, type: types });
    if (!chats.length) {
      dzg.ui.toast('Нечего читать');
      return;
    }
    const ok = await dzg.ui.confirm(`Прочитать ${chats.length} ${plural(chats.length, 'чат', 'чата', 'чатов')}?`);
    if (!ok) return;
    chats.forEach((chat) => dzg.chats.markRead(chat));
    dzg.ui.toast(`Прочитано: ${chats.length}`);
  },
});

const describeUser = async (peer) => {
  const u = await dzg.users.info(peer);
  const common = u.commonChats ? `Общих групп: ${u.commonChats}` : '';
  return [
    u.peer.username ? '@' + u.peer.username : '',
    u.bio,
    `Статус: ${u.status.text}`,
    common,
    u.isPremium ? 'Premium' : '',
    u.isContact ? 'В контактах' : '',
    u.isBlocked ? 'Заблокирован' : '',
    u.birthday ? `День рождения: ${u.birthday.day}.${u.birthday.month}${u.birthday.year ? '.' + u.birthday.year : ''}` : '',
    `id: ${u.peer.tgId}`,
  ].filter(Boolean).join('\n');
};

const describeChat = async (peer) => {
  const info = await dzg.chats.info(peer);
  return [
    info.peer.username ? '@' + info.peer.username : '',
    info.about,
    `Участников: ${info.membersCount}${info.onlineCount ? `, в сети: ${info.onlineCount}` : ''}`,
    info.linkedChat ? `Связанный чат: ${info.linkedChat.name}` : '',
    info.slowmodeSeconds ? `Медленный режим: ${info.slowmodeSeconds} с` : '',
    info.inviteLink,
    `id: ${info.peer.tgId}`,
  ].filter(Boolean).join('\n');
};

const showInfo = async (peer) => {
  try {
    const isUser = peer.type === 'user' || peer.type === 'bot';
    const text = isUser ? await describeUser(peer) : await describeChat(peer);
    await dzg.ui.alert(text, peer.name);
  } catch (e) {
    dzg.ui.toast('Не удалось получить информацию: ' + e.message);
  }
};

// /whois @username — профиль с сервера.
dzg.commands.register('whois', {
  description: 'Информация о пользователе или чате: /whois @username',
  async run({ rest, chat }) {
    const peer = rest ? await dzg.chats.resolve(rest) : chat;
    if (!peer) {
      dzg.ui.toast('Никого не нашёл');
      return;
    }
    await showInfo(peer);
  },
});

dzg.ui.addChatMenuItem({
  title: 'Кто это?',
  onClick: showInfo,
});

// /typing [секунды] — «записывает голосовое» в текущем чате.
dzg.commands.register('typing', {
  description: 'Показать «записывает голосовое»: /typing 15',
  run({ args, chat }) {
    const seconds = Math.min(120, Math.max(1, Number(args[0]) || 10));
    dzg.chats.setTyping(chat, 'record_voice');
    const timer = setInterval(() => dzg.chats.setTyping(chat, 'record_voice'), 4000);
    setTimeout(() => {
      clearInterval(timer);
      dzg.chats.setTyping(chat, 'cancel');
    }, seconds * 1000);
  },
});

// Кто из «наблюдаемых» появился в сети.
const watched = () => new Set(String(dzg.settings.get('watch') || '')
  .split(',')
  .map((s) => s.trim().replace(/^@/, '').toLowerCase())
  .filter(Boolean));

dzg.on('user.status', ({ user, online }) => {
  if (online && user.username && watched().has(user.username.toLowerCase())) {
    dzg.ui.toast(`${user.name} в сети`, 'Chat Tools');
  }
});

dzg.on('chat.typing', ({ chat, user, action }) => {
  if (action === 'record_voice' && dzg.settings.get('typingToasts')) {
    dzg.ui.toast(`${user.name} записывает голосовое в «${chat.name}»`);
  }
});

// /accounts — переключиться на следующий аккаунт.
dzg.commands.register('accounts', {
  description: 'Переключиться на следующий аккаунт',
  run() {
    const list = dzg.accounts.list();
    if (list.length < 2) {
      dzg.ui.toast('Аккаунт один');
      return;
    }
    const index = list.findIndex((a) => a.active);
    const next = list[(index + 1) % list.length];
    dzg.ui.toast(`Переключаюсь на ${next.self.name}`);
    dzg.accounts.switchTo(next);
  },
});
