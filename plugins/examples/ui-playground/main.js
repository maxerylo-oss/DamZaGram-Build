// Плагины видят ВЕСЬ интерфейс как дерево виджетов Qt.
// dzg.ui.mainWindow() — главное окно, .find(className) — поиск внутри.

let panel = null;

function togglePanel() {
  if (panel && panel.valid()) {
    panel.destroy();
    panel = null;
    return;
  }
  const win = dzg.ui.mainWindow();
  const g = win.geometry();
  panel = dzg.ui.create('panel', win, {
    x: g.width - 300, y: 80, width: 280, height: 168,
    color: '#E6202428', radius: 14, name: 'uiPlaygroundPanel',
  });
  dzg.ui.create('label', panel, { x: 16, y: 14, width: 248, height: 24, text: 'Панель из плагина' });
  const input = dzg.ui.create('input', panel, {
    x: 16, y: 44, width: 248, height: 44, placeholder: 'Цвет акцента, напр. #ff7a45',
    on: { submit: (h, value) => applyAccent(value) },
  });
  dzg.ui.create('button', panel, {
    x: 16, y: 108, width: 120, height: 40, text: 'Применить',
    onClick: () => applyAccent(input.text()),
  });
  dzg.ui.create('button', panel, {
    x: 144, y: 108, width: 120, height: 40, text: 'Сброс', style: 'light',
    onClick: () => dzg.theme.reset(),
  });
}

function applyAccent(color) {
  const ok = dzg.theme.setMany({
    windowBgActive: color,
    activeButtonBg: color,
    dialogsBgActive: color,
    msgOutBg: color,
  });
  dzg.ui.toast(ok ? 'Цвета применены' : 'Не все цвета применились');
}

dzg.commands.register('panel', () => togglePanel());

// /tree — сохранить дерево виджетов главного окна в файл и показать путь.
dzg.commands.register('tree', () => {
  const dump = dzg.ui.mainWindow().dump(6);
  const path = dzg.plugin.dataDir + 'ui-tree.json';
  dzg.fs.write(path, JSON.stringify(dump, null, 2));
  dzg.ui.toast('Дерево виджетов: ' + path);
});

// /ip — пример HTTP-запроса.
// async/await движок не поддерживает, поэтому цепочка .then().
dzg.commands.register('ip', () => {
  dzg.http.fetch('https://api.ipify.org?format=json')
    .then((res) => res.json())
    .then((data) => dzg.ui.alert('Твой внешний IP: ' + data.ip))
    .catch((e) => dzg.ui.toast('Ошибка: ' + e));
});

dzg.ui.addChatMenuItem({
  title: 'UI Playground: панель',
  onClick: () => togglePanel(),
});

dzg.on('unload', () => {
  if (panel && panel.valid()) panel.destroy();
  dzg.theme.reset();
});
