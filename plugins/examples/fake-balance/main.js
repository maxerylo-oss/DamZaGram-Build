// Подмена баланса Stars и TON — только визуально, в этом клиенте.
// Клиент периодически получает настоящий баланс с сервера; плагин ловит
// событие wallet.changed и снова ставит своё значение.
// Команды: /balance, /balance stars 5000, /balance ton 12.5, /balance on|off.

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return value === undefined || value === null ? fallback : value;
};

const enabled = () => !!setting('enabled', true);

function wanted() {
  const result = {};
  if (setting('applyStars', true)) result.stars = Math.max(0, Number(setting('stars', 0)) || 0);
  if (setting('applyTon', true)) result.ton = Math.max(0, Number(setting('ton', 0)) || 0);
  return result;
}

const same = (a, b) => Math.abs(Number(a) - Number(b)) < 1e-9;

let applying = false;

function apply() {
  if (!enabled() || applying) return;
  const target = wanted();
  const current = dzg.wallet.get();
  if (!current) return; // нет активного аккаунта
  const changes = {};
  if (target.stars !== undefined && !same(current.stars, target.stars)) changes.stars = target.stars;
  if (target.ton !== undefined && !same(current.ton, target.ton)) changes.ton = target.ton;
  if (!Object.keys(changes).length) return;
  applying = true;
  try {
    dzg.wallet.setLocal(changes);
  } finally {
    applying = false;
  }
}

function restore() {
  dzg.wallet.refresh();
}

// --- Фейковая запись в истории Stars ---------------------------------------

function applyHistory() {
  if (!dzg.wallet.setHistory) return; // старый клиент без поддержки
  if (!setting('histEnabled', false) || !String(setting('histTitle', '')).trim()) {
    dzg.wallet.setHistory([]);
    return;
  }
  const build = (peer) => dzg.wallet.setHistory([{
    title: String(setting('histTitle', '')),
    description: String(setting('histDescription', '')),
    amount: Math.max(0, Number(setting('histAmount', 0)) || 0),
    currency: setting('histCurrency', 'stars'),
    peer: peer || undefined,
    incoming: setting('histIncoming', true) !== false,
  }]);
  const uname = String(setting('histUsername', '')).replace(/^@+/, '').trim();
  const self = dzg.users.self();
  if (!uname) { build(self); return; }
  const found = dzg.chats.find(uname);
  if (found) { build(found); return; }
  dzg.chats.resolve(uname).then((r) => build(r || self)).catch(() => build(self));
}

const format = (value) => {
  const number = Number(value) || 0;
  return Number.isInteger(number) ? String(number) : number.toFixed(2).replace(/\.?0+$/, '');
};

function status() {
  const target = wanted();
  const parts = [];
  if (target.stars !== undefined) parts.push('Stars: ' + format(target.stars));
  if (target.ton !== undefined) parts.push('TON: ' + format(target.ton));
  return (enabled() ? 'Подмена включена. ' : 'Подмена выключена. ') + (parts.join(', ') || 'нечего подменять');
}

// --- Панель -----------------------------------------------------------------

let panel = null;

function render() {
  if (!panel || !panel.isOpen()) return;
  panel.clear();
  panel.addLabel('Меняется только то, что видишь ты в этом клиенте. Настоящий баланс не трогается.', { muted: true });
  panel.addToggle('Подменять баланс', enabled(), (value) => dzg.settings.set('enabled', !!value));
  panel.addSeparator();
  panel.addToggle('Stars', !!setting('applyStars', true), (value) => dzg.settings.set('applyStars', !!value));
  panel.addInput({
    placeholder: 'Сколько звёзд показывать',
    value: format(setting('stars', 0)),
    onSubmit: (text) => saveNumber('stars', text),
  });
  panel.addToggle('TON', !!setting('applyTon', true), (value) => dzg.settings.set('applyTon', !!value));
  panel.addInput({
    placeholder: 'Сколько TON показывать',
    value: format(setting('ton', 0)),
    onSubmit: (text) => saveNumber('ton', text),
  });
  panel.addLabel('Enter в поле — сохранить.', { muted: true });
  panel.addSeparator();
  panel.addLabel('Фейковая запись в истории Stars', { bold: true });
  panel.addToggle('Показывать запись', !!setting('histEnabled', false), (v) => dzg.settings.set('histEnabled', !!v));
  panel.addInput({ placeholder: 'Название', value: String(setting('histTitle', '')), onSubmit: (t) => dzg.settings.set('histTitle', String(t)) });
  panel.addInput({ placeholder: 'Описание', value: String(setting('histDescription', '')), onSubmit: (t) => dzg.settings.set('histDescription', String(t)) });
  panel.addInput({ placeholder: 'Сумма', value: format(setting('histAmount', 0)), onSubmit: (t) => saveNumber('histAmount', t) });
  panel.addInput({ placeholder: '@username для аватарки (необязательно)', value: String(setting('histUsername', '')), onSubmit: (t) => dzg.settings.set('histUsername', String(t)) });
  panel.addToggle('В TON (иначе Stars)', setting('histCurrency', 'stars') === 'ton', (v) => dzg.settings.set('histCurrency', v ? 'ton' : 'stars'));
  panel.addToggle('Входящая (+), иначе исходящая (−)', setting('histIncoming', true) !== false, (v) => dzg.settings.set('histIncoming', !!v));
  panel.addLabel('Открой Настройки → Звёзды, чтобы увидеть запись.', { muted: true });
  panel.addSeparator();
  const real = dzg.wallet.get();
  panel.addButton('Показать настоящий баланс', () => {
    dzg.settings.set('enabled', false);
  }, { style: 'light' });
  if (real) {
    panel.addLabel('Сейчас в клиенте: Stars ' + format(real.stars) + ', TON ' + format(real.ton), { muted: true });
  }
}

function saveNumber(key, text) {
  const value = Number(String(text).replace(',', '.').replace(/\s+/g, ''));
  if (!Number.isFinite(value) || value < 0) {
    dzg.ui.toast('Нужно неотрицательное число', 'Подмена баланса');
    return;
  }
  dzg.settings.set(key, value);
}

function showPanel() {
  panel = dzg.ui.panel({ title: 'Подмена баланса', width: 340 });
  panel.on('close', () => { panel = null; });
  render();
}

// --- События ----------------------------------------------------------------

dzg.on('ready', () => { apply(); applyHistory(); });
dzg.on('session.ready', () => { apply(); applyHistory(); });
dzg.on('wallet.changed', () => {
  if (enabled()) setTimeout(apply, 0);
  render();
});
dzg.on('settings.changed', () => {
  if (enabled()) {
    apply();
applyHistory();
  } else {
    restore();
  }
  applyHistory();
  render();
});

dzg.commands.register('balance', {
  description: 'Подмена баланса: /balance stars 5000 · /balance ton 12.5 · /balance on|off',
  run({ args }) {
    const [what, amount] = args;
    if (!what) {
      showPanel();
      return;
    }
    switch (what.toLowerCase()) {
    case 'on': dzg.settings.set('enabled', true); break;
    case 'off': dzg.settings.set('enabled', false); break;
    case 'stars':
    case 'ton':
      if (amount === undefined) {
        dzg.ui.toast('Укажи число: /balance ' + what + ' 1000', 'Подмена баланса');
        return;
      }
      saveNumber(what.toLowerCase(), amount);
      if (!enabled()) dzg.settings.set('enabled', true);
      break;
    default:
      dzg.ui.toast('Команды: /balance, /balance stars N, /balance ton N, /balance on|off', 'Подмена баланса');
      return;
    }
    dzg.ui.toast(status(), 'Подмена баланса');
  },
});

dzg.ui.addMainMenuItem({ title: 'Подмена баланса', icon: 'settings', onClick: showPanel });

dzg.on('unload', () => {
  if (panel) panel.close();
  if (dzg.wallet.setHistory) dzg.wallet.setHistory([]);
  restore();
});

apply();
applyHistory();
