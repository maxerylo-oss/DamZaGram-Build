// Пример хуков отправки и команд.

const REPLACEMENTS = {
  ':shrug:': '¯\\_(ツ)_/¯',
  ':lenny:': '( ͡° ͜ʖ ͡°)',
  ':flip:': '(╯°□°)╯︵ ┻━┻',
};

// onBeforeSend получает { text, chat, peerId, replyTo }.
// Вернуть строку — заменить текст, false — отменить отправку.
dzg.messages.onBeforeSend(({ text }) => {
  if (!dzg.settings.get('replace')) return;
  let result = text;
  for (const [from, to] of Object.entries(REPLACEMENTS)) {
    result = result.split(from).join(to);
  }
  return result;
});

dzg.commands.register('shrug', ({ rest, chat, replyTo }) => {
  dzg.messages.send(chat.id, (rest ? rest + ' ' : '') + '¯\\_(ツ)_/¯', { replyTo });
});

dzg.commands.register('upper', ({ rest, chat, replyTo }) => {
  if (rest) dzg.messages.send(chat.id, rest.toUpperCase(), { replyTo });
});

dzg.commands.register('calc', {
  description: 'Посчитать выражение: /calc 2*(3+4)',
  run({ rest, chat, replyTo }) {
    if (!/^[\d\s+\-*/().,%^]+$/.test(rest)) {
      dzg.ui.toast('Только числа и + - * / ( ) % ^');
      return;
    }
    let value;
    try {
      value = Function('"use strict"; return (' + rest.replace(/\^/g, '**').replace(/,/g, '.') + ')')();
    } catch (e) {
      dzg.ui.toast('Не получилось посчитать');
      return;
    }
    dzg.messages.send(chat.id, `${rest} = ${value}`, { replyTo });
  },
});

dzg.ui.addMessageMenuItem({
  title: 'Скопировать как цитату',
  when: (m) => !!m.text,
  onClick(m) {
    dzg.clipboard.set(m.text.split('\n').map((l) => '> ' + l).join('\n') + `\n— ${m.fromName}`);
    dzg.ui.toast('Скопировано');
  },
});
