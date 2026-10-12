// Самый простой плагин DamZaGram.
// Файл выполняется один раз при загрузке плагина.

console.log('hello загружен, версия клиента', dzg.app.version);

dzg.on('ready', () => {
  dzg.ui.toast(dzg.settings.get('greeting'), dzg.plugin.name);
});

dzg.on('chat.opened', (chat) => {
  if (dzg.settings.get('toastOnOpen')) {
    dzg.ui.toast('Открыт чат: ' + chat.name);
  }
});

// Команда: напиши в любом чате "/hello Вася" — вместо отправки
// плагин сам отправит приветствие.
dzg.commands.register('hello', {
  description: 'Поприветствовать',
  run({ rest, chat }) {
    const name = rest || 'мир';
    dzg.messages.send(chat.id, `${dzg.settings.get('greeting')} Привет, ${name}!`);
  },
});

// Пункт в контекстном меню сообщения.
dzg.ui.addMessageMenuItem({
  title: 'Hello: инфо о сообщении',
  onClick(message) {
    dzg.ui.alert(
      `id: ${message.id}\nчат: ${message.chat.name}\nот: ${message.fromName}\n` +
      `текст: ${message.text || '(нет текста)'}`,
      'Сообщение'
    );
  },
});

dzg.on('unload', () => console.log('hello выгружен'));
