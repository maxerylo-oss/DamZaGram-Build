// Тема DamZaGram: фирменные акценты поверх текущей темы клиента.
// Меняет только «акцентные» цвета палитры (кнопки, ссылки, выбранный чат,
// исходящие сообщения), фон и текст остаются от темы пользователя.
// Отдельные оттенки для ночного и дневного режима.
// Автор: @wetakush

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return (value === undefined || value === null || value === '') ? fallback : value;
};

// Базовые цвета пресета. Цвета с альфой в формате #AARRGGBB.
//   accent       заливка кнопок, бейджей, переключателей
//   accentHover  та же заливка под мышью
//   ripple       волна при нажатии
//   onAccent     текст и иконки на заливке
//   onAccentSoft вторичный текст на заливке (счётчик на кнопке)
//   text         акцент как цвет текста на фоне окна (ссылки, «в сети»)
//   active       фон выбранного чата в списке (текст на нём белый)
//   activeRipple волна на выбранном чате
//   out          фон исходящего сообщения, outSelected — выделенного
//   outMeta      время и статус в исходящем, outService — имя/пересылка
//   outIcon      галочки, полоска ответа, кружок файла, волна голосового
//   outLink      ссылки в исходящем, outWave — непроигранная часть волны
export const PRESETS = {
  Monolith: {
    title: 'Monolith',
    about: 'лаймовый акцент #C8F135, строгий тёмный',
    night: {
      accent: '#C8F135', accentHover: '#D4F55C', ripple: '#B4DC22', onAccent: '#12160A', onAccentSoft: '#3B4A12',
      text: '#C8F135', active: '#3A4A14', activeRipple: '#4A5D1A',
      out: '#2E3A16', outSelected: '#3D4D1D', outMeta: '#A3B87A', outService: '#C8F135', outIcon: '#C8F135',
      outLink: '#DCF77A', outWave: '#5B6B2C', outShadow: '#26000000',
    },
    day: {
      accent: '#8DC21F', accentHover: '#9ACD2E', ripple: '#7DB017', onAccent: '#10160A', onAccentSoft: '#2F3E0E',
      text: '#4E7A00', active: '#5E8A0C', activeRipple: '#527A08',
      out: '#EAF7C9', outSelected: '#D6EEA0', outMeta: '#6B8F2A', outService: '#4E7A00', outIcon: '#6FA318',
      outLink: '#3F6600', outWave: '#C9E39A', outShadow: '#296FA318',
    },
  },
  Glass: {
    title: 'Glass',
    about: 'тёплый коралл #FF8A5B, мягкий',
    night: {
      accent: '#FF8A5B', accentHover: '#FF9D75', ripple: '#E8764A', onAccent: '#1E0F08', onAccentSoft: '#5A2A16',
      text: '#FF9E7A', active: '#5A3021', activeRipple: '#6B3A28',
      out: '#3D2A22', outSelected: '#4F3427', outMeta: '#D4A08B', outService: '#FF9E7A', outIcon: '#FF8A5B',
      outLink: '#FFB89C', outWave: '#7A4B39', outShadow: '#26000000',
    },
    day: {
      accent: '#F2703F', accentHover: '#F5814F', ripple: '#DB5E2F', onAccent: '#2B1409', onAccentSoft: '#6B2E15',
      text: '#C4501E', active: '#D95B2B', activeRipple: '#C44E22',
      out: '#FFEDE3', outSelected: '#FFD8C4', outMeta: '#AD5F3B', outService: '#AD4A1C', outIcon: '#F2703F',
      outLink: '#A63F15', outWave: '#F6C4AC', outShadow: '#29F2703F',
    },
  },
  Rail: {
    title: 'Rail',
    about: 'оранжевый #C2410C, контрастный',
    night: {
      accent: '#C2410C', accentHover: '#CB4612', ripple: '#A93709', onAccent: '#FFFFFF', onAccentSoft: '#FFD7C2',
      text: '#F27A3D', active: '#7C2D0E', activeRipple: '#8E3511',
      out: '#3A2418', outSelected: '#4C2E1D', outMeta: '#C99474', outService: '#F28A50', outIcon: '#F27A3D',
      outLink: '#FFAA78', outWave: '#7A4428', outShadow: '#26000000',
    },
    day: {
      accent: '#C2410C', accentHover: '#CF4A12', ripple: '#A93709', onAccent: '#FFFFFF', onAccentSoft: '#FFD7C2',
      text: '#C2410C', active: '#C2410C', activeRipple: '#A93709',
      out: '#FDEBDD', outSelected: '#F8D3B8', outMeta: '#B26A3E', outService: '#B23C0A', outIcon: '#D9531A',
      outLink: '#9A330A', outWave: '#F0C3A3', outShadow: '#29C2410C',
    },
  },
};

export const PRESET_NAMES = ['Monolith', 'Glass', 'Rail', 'None'];

const alpha = (hex, a) => '#' + a + hex.replace('#', '').slice(-6);

// Относительная яркость по WCAG, '#RRGGBB' или '#AARRGGBB' (альфа не важна).
function luminance(hex) {
  const v = String(hex).replace('#', '').slice(-6).match(/../g)
    .map((x) => parseInt(x, 16) / 255)
    .map((c) => (c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)));
  return 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2];
}

export function contrast(a, b) {
  const [x, y] = [luminance(a), luminance(b)].sort((p, q) => q - p);
  return (x + 0.05) / (y + 0.05);
}

const isHex = (value) => typeof value === 'string' && /^#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$/.test(value);

/** Ключи палитры Telegram для пресета (см. Telegram/lib_ui/ui/colors.palette). */
export function paletteFor(name, night, options) {
  const preset = PRESETS[name];
  if (!preset) return {};
  const c = night ? preset.night : preset.day;
  const o = Object.assign({ bubbles: true, chatList: true }, options || {});
  // Боковая панель папок тёмная и в дневной теме по умолчанию: цвет текста
  // на ней — тот из двух вариантов пресета, что читается на её фоне.
  const sideBarText = isHex(o.sideBarBg)
    ? [c.text, (night ? preset.day : preset.night).text]
      .sort((x, y) => contrast(y, o.sideBarBg) - contrast(x, o.sideBarBg))[0]
    : c.text;
  const result = {
    windowBgActive: c.accent,
    windowFgActive: c.onAccent,
    windowActiveTextFg: c.text,
    activeButtonBg: c.accent,
    activeButtonBgOver: c.accentHover,
    activeButtonBgRipple: c.ripple,
    activeButtonFg: c.onAccent,
    activeButtonFgOver: c.onAccent,
    activeButtonSecondaryFg: c.onAccentSoft,
    activeButtonSecondaryFgOver: c.onAccentSoft,
    activeLineFg: c.text,
    lightButtonFg: c.text,
    lightButtonFgOver: c.text,
    lightButtonBgOver: alpha(c.text, '1F'),
    lightButtonBgRipple: alpha(c.text, '33'),
    sliderBgActive: c.text,
    filterInputBorderFg: c.text,
    historySendIconFg: c.text,
    historyUnreadBarFg: c.text,
    overviewCheckBgActive: c.accent,
    // Галочка рисуется на заливке акцентом (чекбоксы, выбор медиа).
    overviewCheckFgActive: c.onAccent,
    dialogsUnreadBg: c.accent,
    dialogsUnreadFg: c.onAccent,
    dialogsSentIconFg: c.text,
    sideBarBadgeBg: c.accent,
    sideBarBadgeFg: c.onAccent,
    sideBarTextFgActive: sideBarText,
    sideBarIconFgActive: sideBarText,
  };
  if (o.chatList) {
    Object.assign(result, {
      dialogsBgActive: c.active,
      dialogsRippleBgActive: c.activeRipple,
      dialogsUnreadFgActive: c.active,
      mainMenuCoverBg: c.active,
    });
  }
  if (o.bubbles) {
    Object.assign(result, {
      msgOutBg: c.out,
      msgOutBgSelected: c.outSelected,
      msgOutShadow: c.outShadow,
      msgOutDateFg: c.outMeta,
      msgOutDateFgSelected: c.outMeta,
      mediaOutFg: c.outMeta,
      historySendingOutIconFg: c.outMeta,
      msgOutServiceFg: c.outService,
      msgOutServiceFgSelected: c.outService,
      msgFileThumbLinkOutFg: c.outService,
      historyOutIconFg: c.outIcon,
      historyOutIconFgSelected: c.outIcon,
      msgOutReplyBarColor: c.outIcon,
      msgOutReplyBarSelColor: c.outIcon,
      msgFileOutBg: c.outIcon,
      msgFileOutBgSelected: c.outIcon,
      msgWaveformOutActive: c.outIcon,
      msgWaveformOutInactive: c.outWave,
      historyFileOutIconFg: c.out,
      historyFileOutIconFgSelected: c.outSelected,
      historyFileOutRadialFg: c.out,
      historyLinkOutFg: c.outLink,
      msgOutMonoFg: c.outLink,
    });
  }
  return result;
}

// --- Применение -----------------------------------------------------------

let applied = null; // подпись применённого набора или null

function presetName() {
  const value = String(setting('preset', 'Monolith')).trim();
  const found = PRESET_NAMES.find((n) => n.toLowerCase() === value.toLowerCase());
  return found || 'Monolith';
}

function options() {
  return { bubbles: !!setting('bubbles', true), chatList: !!setting('chatList', true) };
}

function isNight() {
  return dzg.app.isNightMode ? dzg.app.isNightMode() : true;
}

function themeColor(name) {
  try {
    return dzg.theme.get ? dzg.theme.get(name) : null;
  } catch (e) {
    return null;
  }
}

// force: тема сменилась. Клиент сам кладёт наши цвета поверх новой темы,
// но они подобраны под прежний режим, — перекрашиваем (или снимаем) их.
export function apply(force) {
  const name = presetName();
  const night = isNight();
  const wanted = (name === 'None' || (!night && !setting('applyDay', true)))
    ? null
    : JSON.stringify([name, night, options()]);
  if (wanted === applied && !force) return;
  // Пресет без отключённых групп ключей можно накрыть поверх старого,
  // иначе сначала возвращаем исходную палитру.
  const narrowed = applied && wanted && !sameKeys(applied, wanted);
  if (applied && (!wanted || narrowed)) dzg.theme.reset();
  if (wanted) {
    const palette = paletteFor(name, night, Object.assign(options(), {
      sideBarBg: themeColor('sideBarBgActive'),
    }));
    if (!dzg.theme.setMany(palette)) {
      console.warn('Часть цветов не применилась: палитра этой версии клиента отличается');
    }
  }
  applied = wanted;
}

function sameKeys(a, b) {
  const x = JSON.parse(a)[2];
  const y = JSON.parse(b)[2];
  return (!x.bubbles || y.bubbles) && (!x.chatList || y.chatList);
}

export function setPreset(name) {
  const found = PRESET_NAMES.find((n) => n.toLowerCase() === String(name).toLowerCase());
  if (!found) return false;
  dzg.settings.set('preset', found);
  apply();
  if (panel && panel.isOpen()) render();
  return true;
}

// Тема переключилась (ночь/день): клиент загрузил свежую палитру и
// вернул поверх неё наши прежние цвета. Они для другого режима: когда тема
// дорисуется, ставим цвета нового режима, а если в нём пресет выключен
// (applyDay), снимаем их, иначе ночные пузыри остались бы на дневной теме.
dzg.on('theme.changed', () => {
  setTimeout(() => apply(true), 50);
});

dzg.on('settings.changed', ({ key }) => {
  if (['preset', 'applyDay', 'bubbles', 'chatList'].includes(key)) {
    apply();
    if (panel && panel.isOpen()) render();
  }
});

// --- Панель выбора --------------------------------------------------------

let panel = null;

function render() {
  if (!panel) return;
  panel.clear();
  const current = presetName();
  panel.addLabel('Акценты поверх текущей темы. ' + (isNight() ? 'Сейчас ночной режим.' : 'Сейчас дневной режим.'), { muted: true });
  for (const name of PRESET_NAMES) {
    const about = PRESETS[name] ? PRESETS[name].about : 'стандартные цвета темы';
    panel.addButton((name === current ? '● ' : '○ ') + name + ' — ' + about, () => setPreset(name), name === current ? undefined : { style: 'light' });
  }
  panel.addSeparator();
  panel.addToggle('Исходящие сообщения', !!setting('bubbles', true), (v) => dzg.settings.set('bubbles', !!v));
  panel.addToggle('Выбранный чат в списке', !!setting('chatList', true), (v) => dzg.settings.set('chatList', !!v));
  panel.addToggle('В дневном режиме тоже', !!setting('applyDay', true), (v) => dzg.settings.set('applyDay', !!v));
  panel.addButton(isNight() ? 'Переключить на дневной режим' : 'Переключить на ночной режим', () => dzg.app.setNightMode(!isNight()), { style: 'light' });
  panel.addLabel('DamZaGram · @wetakush', { muted: true });
}

export function show() {
  if (!dzg.ui.panel) {
    dzg.ui.toast('Текущий пресет: ' + presetName() + '. Сменить: /theme monolith|glass|rail|none');
    return;
  }
  if (!panel || !panel.isOpen()) {
    panel = dzg.ui.panel({ title: 'Тема DamZaGram', width: 360 });
    panel.on('close', () => { panel = null; });
  }
  render();
}

dzg.commands.register('theme', {
  description: 'Тема DamZaGram: /theme monolith | glass | rail | none',
  run: ({ args }) => {
    if (!args[0]) {
      show();
      return;
    }
    if (setPreset(args[0])) {
      dzg.ui.toast('Пресет: ' + presetName(), 'Тема DamZaGram');
    } else {
      dzg.ui.toast('Нет такого пресета. Есть: ' + PRESET_NAMES.join(', '));
    }
  },
});

if (dzg.ui.addMainMenuItem) {
  dzg.ui.addMainMenuItem({ title: 'Тема DamZaGram', icon: 'palette', onClick: show });
}

apply();

dzg.on('unload', () => {
  if (panel) panel.close();
  // Палитру вернёт клиент: он сам откатывает цвета выгружаемого плагина.
});
