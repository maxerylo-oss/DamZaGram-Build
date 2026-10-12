// Кнопка в шапке чата и пункт главного меню показывают окно с текстом из настроек.
// Текст и заголовок меняются в Настройки → Плагины → «Окно с текстом» → Settings.

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return value === undefined || value === null || value === '' ? fallback : String(value);
};

function showPopup() {
  dzg.ui.alert(setting('text', 'Привет!'), setting('title', 'DamZaGram'));
}

let removeButton = null;

function addButton() {
  if (removeButton) removeButton();
  removeButton = dzg.ui.addTopBarButton({
    text: setting('buttonText', 'Текст'),
    tooltip: 'Показать окно с текстом',
    onClick: showPopup,
  });
}

addButton();
dzg.ui.addMainMenuItem({ title: 'Окно с текстом', icon: 'info', onClick: showPopup });
dzg.commands.register('popup', { description: 'Показать окно с текстом', run: showPopup });

dzg.on('settings.changed', ({ key }) => {
  if (key === 'buttonText') addButton();
});
