// UI Showcase — витрина UI-расширений DamZaGram.
// Автор: @wetakush (https://t.me/wetakush)
//
// Что здесь есть:
//   • кнопка в шапке чата (dzg.ui.addTopBarButton) — открывает панель;
//   • кнопка «Аа» в шапке только в личных чатах (when);
//   • пункт главного меню (dzg.ui.addMainMenuItem);
//   • горячая клавиша Ctrl+Shift+U (dzg.ui.addShortcut);
//   • кнопка у поля ввода «исправить раскладку» (dzg.ui.compose.addButton);
//   • боковая панель со всеми видами элементов (dzg.ui.panel);
//   • уведомления с действием по клику (dzg.ui.notify);
//   • ссылки t.me и tg:// (dzg.ui.openLink);
//   • подмена текста сообщений при отрисовке (dzg.messages.addRenderTransform);
//   • события app.focus и compose.changed.

const state = {
  panel: null,
  counter: null,
  focusLabel: null,
  focusSwitches: 0,
  censorOff: null,
  capsOff: null,
  shortcutOff: null,
};

// ---------- Раскладка: "ghbdtn" -> "привет" и обратно ----------

const EN = "`qwertyuiop[]asdfghjkl;'zxcvbnm,./~QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>?@#$^&";
const RU = 'ёйцукенгшщзхъфывапролджэячсмитьбю.ЁЙЦУКЕНГШЩЗХЪФЫВАПРОЛДЖЭЯЧСМИТЬБЮ,"№;:?';

function switchLayout(text) {
  const latin = (text.match(/[a-z]/gi) || []).length;
  const cyrillic = (text.match(/[а-яё]/gi) || []).length;
  const [from, to] = latin >= cyrillic ? [EN, RU] : [RU, EN];
  return Array.from(text, (ch) => {
    const index = from.indexOf(ch);
    return index >= 0 ? to[index] : ch;
  }).join('');
}

// ---------- Подмена текста при отрисовке ----------

function censorWords() {
  return String(dzg.settings.get('censor') || '')
    .split(',')
    .map((w) => w.trim())
    .filter(Boolean);
}

function escapeRegExp(text) {
  return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function setCensor(enabled) {
  if (state.censorOff) {
    state.censorOff();
    state.censorOff = null;
  }
  if (!enabled) return;
  const words = censorWords();
  if (!words.length) return;
  const pattern = new RegExp(words.map(escapeRegExp).join('|'), 'gi');
  state.censorOff = dzg.messages.addRenderTransform((message) => {
    if (!message.text || !pattern.test(message.text)) return null;
    pattern.lastIndex = 0;
    return message.text.replace(pattern, (word) => '█'.repeat(word.length));
  });
}

function setCaps(enabled) {
  if (state.capsOff) {
    state.capsOff();
    state.capsOff = null;
  }
  if (!enabled) return;
  state.capsOff = dzg.messages.addRenderTransform((message) =>
    (!message.out && message.text) ? message.text.toUpperCase() : null);
}

// ---------- Панель ----------

function describeCompose(data) {
  if (!data) return 'Поле ввода не найдено — открой любой чат.';
  const where = data.chat ? `«${data.chat.name}»` : 'без чата';
  return `Символов в поле: ${data.text.length} · чат ${where}`;
}

function describeFocus(active) {
  return `Окно ${active ? 'в фокусе' : 'не в фокусе'} · переключений: ${state.focusSwitches}`;
}

function buildPanel(panel) {
  panel.clear();

  panel.addLabel('Поле ввода', { bold: true });
  state.counter = panel.addLabel(describeCompose(dzg.ui.compose.get()), { muted: true });
  const input = panel.addInput({
    placeholder: 'Текст для вставки (Enter — вставить)',
    onSubmit(text) {
      if (text && dzg.ui.compose.insert(text)) dzg.ui.compose.focus();
    },
  });
  panel.addButton('Вставить в поле', () => {
    const text = input.text();
    if (!text) return dzg.ui.toast('Сначала напиши текст выше');
    if (!dzg.ui.compose.insert(text)) dzg.ui.toast('Нет открытого чата');
  });
  panel.addButton('Заменить текст поля', () => {
    dzg.ui.compose.set(input.text() || '');
  }, { style: 'light' });
  panel.addButton('Исправить раскладку в поле', fixLayout, { icon: 'font' });
  panel.addButton('Поставить курсор в поле', () => dzg.ui.compose.focus(), { icon: 'edit' });

  panel.addSeparator();
  panel.addLabel('Как показывать сообщения', { bold: true });
  panel.addToggle('Прятать слова из настроек плагина', !!state.censorOff, (on) => {
    setCensor(on);
    dzg.storage.set('censor', on);
  });
  panel.addToggle('КАПСЛОК для входящих', !!state.capsOff, (on) => {
    setCaps(on);
    dzg.storage.set('caps', on);
  });
  panel.addLabel('Меняется только отображение: сами сообщения остаются прежними.', { muted: true });

  panel.addSeparator();
  panel.addLabel('Уведомления и ссылки', { bold: true });
  panel.addButton('Показать уведомление', () => {
    dzg.ui.notify({
      title: 'UI Showcase',
      text: 'Нажми, чтобы открыть канал автора',
      onClick: () => dzg.ui.openLink('https://t.me/wetakush'),
    });
  }, { icon: 'bell' });
  panel.addButton('Тихое уведомление', () => {
    dzg.ui.notify({ title: 'Тсс', text: 'Без звука и мигания', silent: true });
  }, { icon: 'silent' });
  panel.addButton('Открыть @wetakush', () => dzg.ui.openLink('https://t.me/wetakush'), { icon: 'link' });
  panel.addButton('Открыть настройки (tg://settings)', () => dzg.ui.openLink('tg://settings'), { icon: 'settings' });

  panel.addSeparator();
  panel.addLabel('Разное', { bold: true });
  state.focusLabel = panel.addLabel(describeFocus(true), { muted: true });
  panel.addButton(`Иконки (${dzg.ui.icons().length})`, () => {
    dzg.ui.alert(dzg.ui.icons().join(', '), 'dzg.ui.icons()');
  }, { icon: 'palette' });
  panel.addSkip(12);
  panel.addButton('Закрыть панель', () => panel.close(), { style: 'light' });
}

function togglePanel() {
  if (state.panel && state.panel.isOpen()) {
    state.panel.close();
    return;
  }
  const panel = dzg.ui.panel({ title: 'UI Showcase', width: 360 });
  state.panel = panel;
  buildPanel(panel);
  panel.on('close', () => {
    state.panel = null;
    state.counter = null;
    state.focusLabel = null;
  });
}

// ---------- Поле ввода ----------

function fixLayout() {
  const compose = dzg.ui.compose.get();
  if (!compose || !compose.text) {
    dzg.ui.toast('Поле ввода пустое');
    return;
  }
  dzg.ui.compose.set(switchLayout(compose.text));
  dzg.ui.compose.focus();
}

// ---------- Регистрация ----------

dzg.ui.addTopBarButton({
  icon: 'palette',
  tooltip: 'UI Showcase',
  onClick: () => togglePanel(),
});

dzg.ui.addTopBarButton({
  text: 'Аа',
  tooltip: 'Кто это?',
  when: (chat) => chat.type === 'user' && !chat.isSelf,
  onClick(chat) {
    dzg.ui.notify({
      title: chat.name,
      text: chat.username ? `@${chat.username} · id ${chat.id}` : `id ${chat.id}`,
      silent: true,
    });
  },
});

dzg.ui.addMainMenuItem({
  title: 'UI Showcase',
  icon: 'experimental',
  onClick: () => togglePanel(),
});

dzg.ui.compose.addButton({
  icon: 'font',
  tooltip: 'Исправить раскладку (ghbdtn → привет)',
  onClick: () => fixLayout(),
});

function bindShortcut() {
  if (state.shortcutOff) state.shortcutOff();
  state.shortcutOff = null;
  const keys = String(dzg.settings.get('shortcut') || 'Ctrl+Shift+U');
  try {
    state.shortcutOff = dzg.ui.addShortcut(keys, () => togglePanel());
  } catch (e) {
    dzg.ui.toast(`Не получилось назначить «${keys}»: ${e.message}`, 'UI Showcase');
  }
}
bindShortcut();

setCensor(dzg.storage.get('censor', false));
setCaps(dzg.storage.get('caps', false));

dzg.on('settings.changed', ({ key }) => {
  if (key === 'shortcut') bindShortcut();
  if (key === 'censor' && state.censorOff) setCensor(true);
});

dzg.on('compose.changed', (data) => {
  if (state.counter && state.counter.valid()) {
    state.counter.setText(describeCompose(data));
  }
});

dzg.on('app.focus', ({ active }) => {
  state.focusSwitches += 1;
  if (state.focusLabel && state.focusLabel.valid()) {
    state.focusLabel.setText(describeFocus(active));
  }
});

dzg.on('ready', () => {
  console.log('UI Showcase готов: иконок', dzg.ui.icons().length);
});
