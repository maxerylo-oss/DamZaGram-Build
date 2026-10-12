import { PRESETS, PRESET_NAMES, paletteFor, contrast as pluginContrast } from '../../../../plugins/builtin/damza-theme/main.js';

const { eq, assert } = sandbox;

// Every key exists in the client palette.
const root = mock.pluginDir.replace(/\/plugins\/builtin\/damza-theme\/?$/, '');
const palette = std.loadFile(root + '/Telegram/lib_ui/ui/colors.palette');
assert(palette, 'colors.palette readable');
const known = new Set(Array.from(palette.matchAll(/^(\w+):/gm), (m) => m[1]));
for (const name of Object.keys(PRESETS)) {
  for (const night of [true, false]) {
    const map = paletteFor(name, night, { bubbles: true, chatList: true });
    for (const [key, color] of Object.entries(map)) {
      assert(known.has(key), 'unknown palette key ' + key);
      assert(/^#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$/.test(color), key + ' = ' + color);
    }
  }
}

// Readability: WCAG contrast of the important pairs.
const lum = (hex) => {
  const v = hex.replace('#', '').slice(-6).match(/../g).map((x) => parseInt(x, 16) / 255)
    .map((c) => (c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)));
  return 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2];
};
const contrast = (a, b) => {
  const [x, y] = [lum(a), lum(b)].sort((p, q) => q - p);
  return (x + 0.05) / (y + 0.05);
};
for (const [name, preset] of Object.entries(PRESETS)) {
  for (const [mode, c, bg, fg] of [['night', preset.night, '#17212b', '#e4ecf2'], ['day', preset.day, '#ffffff', '#000000']]) {
    const where = name + '/' + mode;
    assert(contrast(c.onAccent, c.accent) >= 4.5, where + ': text on accent ' + contrast(c.onAccent, c.accent).toFixed(2));
    assert(contrast(c.onAccent, c.accentHover) >= 4.5, where + ': text on hovered accent ' + contrast(c.onAccent, c.accentHover).toFixed(2));
    // lightButtonFg: text of the box buttons ("OK", "Cancel") on the window.
    assert(contrast(c.text, bg) >= 4.5, where + ': accent text on window ' + contrast(c.text, bg).toFixed(2));
    assert(contrast(c.outLink, c.outSelected) >= 4.5, where + ': link in selected bubble ' + contrast(c.outLink, c.outSelected).toFixed(2));
    assert(contrast(c.outService, c.out) >= 4.5, where + ': names in outgoing bubble ' + contrast(c.outService, c.out).toFixed(2));
    assert(contrast('#ffffff', c.active) >= 3.5, where + ': white on selected chat ' + contrast('#ffffff', c.active).toFixed(2));
    assert(contrast(fg, c.out) >= 7, where + ': text in outgoing bubble ' + contrast(fg, c.out).toFixed(2));
    assert(contrast(c.outLink, c.out) >= 4.5, where + ': link in outgoing bubble ' + contrast(c.outLink, c.out).toFixed(2));
    assert(contrast(c.outMeta, c.out) >= 3, where + ': time in outgoing bubble ' + contrast(c.outMeta, c.out).toFixed(2));
  }
}

// The plugin's own contrast() agrees with the test's.
assert(Math.abs(pluginContrast('#000000', '#ffffff') - 21) < 1e-9);

// Check marks are drawn on the accent fill with the on-accent color.
for (const name of Object.keys(PRESETS)) {
  for (const night of [true, false]) {
    const map = paletteFor(name, night);
    eq(map.overviewCheckFgActive, map.windowFgActive, name + ' check mark');
  }
}

// The folders side bar is dark in the default day theme too: its active
// text is picked by the real background, not by the mode.
for (const name of Object.keys(PRESETS)) {
  for (const night of [true, false]) {
    for (const sideBarBg of ['#17212b', '#293a4c', '#ffffff', '#f1f1f1']) {
      const map = paletteFor(name, night, { sideBarBg });
      const value = contrast(map.sideBarTextFgActive, sideBarBg);
      const best = Math.max(contrast(PRESETS[name].night.text, sideBarBg), contrast(PRESETS[name].day.text, sideBarBg));
      eq(value, best, name + ' picks the more readable side bar color');
      assert(value >= 4, name + (night ? '/night' : '/day') + ' side bar on ' + sideBarBg + ': ' + value.toFixed(2));
    }
  }
}
eq(paletteFor('Monolith', false).sideBarTextFgActive, PRESETS.Monolith.day.text, 'no background known: mode color');

// Options drop key groups.
const slim = paletteFor('Monolith', true, { bubbles: false, chatList: false });
assert(!('msgOutBg' in slim) && !('dialogsBgActive' in slim) && 'windowBgActive' in slim);
eq(paletteFor('None', true), {});
eq(PRESET_NAMES, ['Monolith', 'Glass', 'Rail', 'None']);

// Default: Monolith applied at load, night colors.
eq(mock.theme.windowBgActive, '#C8F135');
eq(mock.theme.msgOutBg, PRESETS.Monolith.night.out);

// Day mode switch: re-applied with day colors after the theme reloads.
mock.night = false;
mock.theme = {};
sandbox.event('theme.changed', { night: false });
sandbox.runTimers();
eq(mock.theme.windowBgActive, PRESETS.Monolith.day.accent);
eq(mock.themeResets, 0, 'no reset after a theme reload');

// The client puts the old (night) colors back on top of a new theme by
// itself. With applyDay off they must be taken down, not left on the day
// theme (dark bubbles under black text).
mock.night = true;
sandbox.event('theme.changed', { night: true });
sandbox.runTimers();
eq(mock.theme.msgOutBg, PRESETS.Monolith.night.out);
sandbox.settings.applyDay = false;
mock.night = false;
const resetsBefore = mock.themeResets;
sandbox.event('theme.changed', { night: false });
sandbox.runTimers();
eq(mock.themeResets, resetsBefore + 1, 'night colors taken down in day mode');
eq(mock.theme, {});
sandbox.settings.applyDay = true;
sandbox.event('settings.changed', { key: 'applyDay', value: true });
eq(mock.theme.windowBgActive, PRESETS.Monolith.day.accent);
mock.themeResets = 0;

// Switching presets with the same options paints over without reset.
mock.command('/theme glass');
eq(sandbox.settings.preset, 'Glass');
eq(mock.theme.windowBgActive, PRESETS.Glass.day.accent);
eq(mock.themeResets, 0);

// Turning bubbles off resets first, then applies the rest.
sandbox.settings.bubbles = false;
sandbox.event('settings.changed', { key: 'bubbles', value: false });
eq(mock.themeResets, 1);
assert(!('msgOutBg' in mock.theme) && mock.theme.windowBgActive === PRESETS.Glass.day.accent);

// applyDay off in day mode: back to the theme's colors.
sandbox.settings.applyDay = false;
sandbox.event('settings.changed', { key: 'applyDay', value: false });
eq(mock.themeResets, 2);
eq(mock.theme, {});

// None.
sandbox.settings.applyDay = true;
sandbox.event('settings.changed', { key: 'applyDay', value: true });
mock.command('/theme none');
eq(mock.theme, {});

// Panel: preset buttons.
mock.command('/theme');
const panel = mock.lastPanel();
assert(mock.panelText(panel).includes('● None'));
mock.click(panel, 'Rail');
eq(sandbox.settings.preset, 'Rail');
eq(mock.theme.windowBgActive, '#C2410C');
assert(mock.mainMenu.some((i) => i.title === 'Тема DamZaGram'));
