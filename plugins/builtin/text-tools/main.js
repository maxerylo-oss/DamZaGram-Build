// Текстовые инструменты DamZaGram: калькулятор, раскладка, регистры,
// base64 и автозамена смайлов перед отправкой.
// Автор: @wetakush

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return (value === undefined || value === null || value === '') ? fallback : value;
};

// --- Раскладка ------------------------------------------------------------

const EN = "`qwertyuiop[]asdfghjkl;'zxcvbnm,./~QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>?@#$^&";
const RU = 'ёйцукенгшщзхъфывапролджэячсмитьбю.ЁЙЦУКЕНГШЩЗХЪФЫВАПРОЛДЖЭЯЧСМИТЬБЮ,"№;:?';
const EN_TO_RU = new Map();
const RU_TO_EN = new Map();
for (let i = 0; i < EN.length; ++i) {
  EN_TO_RU.set(EN[i], RU[i]);
  RU_TO_EN.set(RU[i], EN[i]);
}

/** Переключает раскладку: латиница → кириллица или наоборот, по преобладающим буквам. */
export function fixLayout(text) {
  const latin = (text.match(/[a-z]/gi) || []).length;
  const cyrillic = (text.match(/[а-яё]/gi) || []).length;
  const map = latin >= cyrillic ? EN_TO_RU : RU_TO_EN;
  return Array.from(text, (ch) => map.get(ch) || ch).join('');
}

const vowelRatio = (text, vowels) => {
  const letters = text.match(/\p{L}/gu) || [];
  if (!letters.length) return 0;
  return letters.filter((ch) => vowels.includes(ch.toLowerCase())).length / letters.length;
};

/** Похоже ли, что русский текст набран в английской раскладке. */
export function looksMistyped(text) {
  const value = String(text || '').trim();
  if (!value || /[а-яё]/i.test(value) || /https?:|www\.|@\w|\d{3,}/i.test(value)) return false;
  const letters = (value.match(/[a-z]/gi) || []).length;
  if (letters < 5) return false;
  const fixed = fixLayout(value);
  return vowelRatio(value, 'aeiouy') < 0.2 && vowelRatio(fixed, 'аеёиоуыэюя') >= 0.28;
}

// --- Калькулятор: свой разбор выражений, без eval -------------------------

const FUNCTIONS = {
  sqrt: Math.sqrt, abs: Math.abs, round: Math.round, floor: Math.floor, ceil: Math.ceil,
  sin: Math.sin, cos: Math.cos, tan: Math.tan, asin: Math.asin, acos: Math.acos, atan: Math.atan,
  log: Math.log10, lg: Math.log10, ln: Math.log, log2: Math.log2, exp: Math.exp,
  min: Math.min, max: Math.max, pow: Math.pow,
};
const CONSTANTS = { pi: Math.PI, 'π': Math.PI, e: Math.E };

export function calc(input) {
  let source = String(input || '').trim().replace(/×/g, '*').replace(/÷/g, '/').replace(/−/g, '-');
  if (!/[a-z]\s*\(/i.test(source)) source = source.replace(/(\d),(\d)/g, '$1.$2');
  const tokens = source.match(/\d+(?:\.\d+)?(?:e[+-]?\d+)?|[a-zπ]\w*|\*\*|[-+*/%^(),!]|\S/gi) || [];
  let pos = 0;
  const peek = () => tokens[pos];
  const next = () => tokens[pos++];
  const expect = (t) => {
    if (next() !== t) throw new Error('ожидалось «' + t + '»');
  };
  const parseExpression = () => {
    let value = parseTerm();
    while (peek() === '+' || peek() === '-') {
      const op = next();
      const right = parseTerm();
      value = op === '+' ? value + right : value - right;
    }
    return value;
  };
  const parseTerm = () => {
    let value = parseUnary();
    while (peek() === '*' || peek() === '/' || peek() === '%') {
      const op = next();
      const right = parseUnary();
      if (op === '*') value *= right;
      else if (op === '/') value /= right;
      else value %= right;
    }
    return value;
  };
  const parseUnary = () => {
    if (peek() === '-') { next(); return -parseUnary(); }
    if (peek() === '+') { next(); return parseUnary(); }
    return parsePower();
  };
  const parsePower = () => {
    const base = parsePostfix();
    if (peek() === '^' || peek() === '**') {
      next();
      return Math.pow(base, parseUnary());
    }
    return base;
  };
  const factorial = (n) => {
    if (n < 0 || !Number.isInteger(n) || n > 170) throw new Error('факториал только для целых 0..170');
    let r = 1;
    for (let i = 2; i <= n; ++i) r *= i;
    return r;
  };
  const parsePostfix = () => {
    let value = parsePrimary();
    while (peek() === '!') { next(); value = factorial(value); }
    return value;
  };
  const parsePrimary = () => {
    const token = next();
    if (token === undefined) throw new Error('выражение оборвалось');
    if (/^\d/.test(token)) return Number(token);
    if (token === '(') {
      const value = parseExpression();
      expect(')');
      return value;
    }
    const name = token.toLowerCase();
    if (Object.prototype.hasOwnProperty.call(FUNCTIONS, name)) {
      expect('(');
      const args = [];
      if (peek() !== ')') {
        args.push(parseExpression());
        while (peek() === ',') { next(); args.push(parseExpression()); }
      }
      expect(')');
      return FUNCTIONS[name](...args);
    }
    if (Object.prototype.hasOwnProperty.call(CONSTANTS, name)) return CONSTANTS[name];
    throw new Error('непонятно: «' + token + '»');
  };
  if (!tokens.length) throw new Error('пустое выражение');
  const value = parseExpression();
  if (pos < tokens.length) throw new Error('лишнее: «' + tokens[pos] + '»');
  if (typeof value !== 'number' || Number.isNaN(value)) throw new Error('не число');
  return value;
}

export function formatNumber(value) {
  if (!Number.isFinite(value)) return value > 0 ? '∞' : (value < 0 ? '-∞' : 'NaN');
  if (Number.isInteger(value) && Math.abs(value) < 1e21) return String(value);
  return String(Number(value.toPrecision(12)));
}

// --- Base64 с UTF-8 -------------------------------------------------------

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

function utf8Encode(text) {
  const bytes = [];
  for (const ch of text) {
    let c = ch.codePointAt(0);
    if (c < 0x80) bytes.push(c);
    else if (c < 0x800) bytes.push(0xc0 | (c >> 6), 0x80 | (c & 63));
    else if (c < 0x10000) bytes.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
    else bytes.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
  }
  return bytes;
}

function utf8Decode(bytes) {
  let out = '';
  for (let i = 0; i < bytes.length;) {
    const b = bytes[i];
    let c;
    let n;
    if (b < 0x80) { c = b; n = 1; }
    else if ((b & 0xe0) === 0xc0) { c = b & 0x1f; n = 2; }
    else if ((b & 0xf0) === 0xe0) { c = b & 0x0f; n = 3; }
    else if ((b & 0xf8) === 0xf0) { c = b & 0x07; n = 4; }
    else throw new Error('не UTF-8');
    if (i + n > bytes.length) throw new Error('не UTF-8');
    for (let j = 1; j < n; ++j) {
      if ((bytes[i + j] & 0xc0) !== 0x80) throw new Error('не UTF-8');
      c = (c << 6) | (bytes[i + j] & 0x3f);
    }
    out += String.fromCodePoint(c);
    i += n;
  }
  return out;
}

export function base64Encode(text) {
  const bytes = utf8Encode(String(text));
  let out = '';
  for (let i = 0; i < bytes.length; i += 3) {
    const n = (bytes[i] << 16) | ((bytes[i + 1] || 0) << 8) | (bytes[i + 2] || 0);
    out += B64[(n >> 18) & 63] + B64[(n >> 12) & 63] +
      (i + 1 < bytes.length ? B64[(n >> 6) & 63] : '=') +
      (i + 2 < bytes.length ? B64[n & 63] : '=');
  }
  return out;
}

export function base64Decode(text) {
  const clean = String(text).replace(/\s+/g, '').replace(/-/g, '+').replace(/_/g, '/').replace(/=+$/, '');
  if (!/^[A-Za-z0-9+/]*$/.test(clean) || clean.length % 4 === 1) throw new Error('это не base64');
  const bytes = [];
  for (let i = 0; i < clean.length; i += 4) {
    const chunk = clean.slice(i, i + 4);
    const n = Array.from(chunk).reduce((acc, ch, k) => acc | (B64.indexOf(ch) << (18 - 6 * k)), 0);
    bytes.push((n >> 16) & 255);
    if (chunk.length > 2) bytes.push((n >> 8) & 255);
    if (chunk.length > 3) bytes.push(n & 255);
  }
  return utf8Decode(bytes);
}

// --- Преобразования -------------------------------------------------------

export function mock(text) {
  let upper = false;
  return Array.from(text, (ch) => {
    if (!/\p{L}/u.test(ch)) return ch;
    upper = !upper;
    return upper ? ch.toLowerCase() : ch.toUpperCase();
  }).join('');
}

const UPSIDE = {
  a: 'ɐ', b: 'q', c: 'ɔ', d: 'p', e: 'ǝ', f: 'ɟ', g: 'ƃ', h: 'ɥ', i: 'ᴉ', j: 'ɾ', k: 'ʞ', l: 'l', m: 'ɯ', n: 'u',
  o: 'o', p: 'd', q: 'b', r: 'ɹ', s: 's', t: 'ʇ', u: 'n', v: 'ʌ', w: 'ʍ', x: 'x', y: 'ʎ', z: 'z',
  '.': '˙', ',': "'", '?': '¿', '!': '¡', '(': ')', ')': '(', '[': ']', ']': '[', '<': '>', '>': '<', '_': '‾',
  'а': 'ɐ', 'б': 'ƍ', 'в': 'ʚ', 'г': 'ɹ', 'д': 'ɓ', 'е': 'ǝ', 'ё': 'ǝ', 'ж': 'ж', 'з': 'ε', 'и': 'и', 'й': 'ņ',
  'к': 'ʞ', 'л': 'v', 'м': 'w', 'н': 'н', 'о': 'о', 'п': 'u', 'р': 'd', 'с': 'ɔ', 'т': 'ɯ', 'у': 'ʎ', 'ф': 'ф',
  'х': 'х', 'ц': 'ǹ', 'ч': 'һ', 'ш': 'm', 'щ': 'm', 'ъ': 'q', 'ы': 'ıq', 'ь': 'q', 'э': 'є', 'ю': 'oı', 'я': 'ʁ',
};

export function upsideDown(text) {
  return Array.from(text.toLowerCase(), (ch) => UPSIDE[ch] || ch).reverse().join('');
}

const REPLACEMENTS = {
  ':shrug:': '¯\\_(ツ)_/¯',
  ':lenny:': '( ͡° ͜ʖ ͡°)',
  ':flip:': '(╯°□°)╯︵ ┻━┻',
  ':unflip:': '┬─┬ノ( º _ ºノ)',
  ':cool:': '(⌐■_■)',
  ':why:': 'ლ(ಠ益ಠლ)',
  ':hug:': '(っ˘̩╭╮˘̩)っ',
  ':dance:': '└(・。・)┘ ┌(・。・)┐',
};

export function applyReplacements(text) {
  let result = text;
  for (const [from, to] of Object.entries(REPLACEMENTS)) result = result.split(from).join(to);
  return result;
}

// --- Вывод результата -----------------------------------------------------

function output(chat, text, replyTo) {
  if (setting('toCompose', false) && dzg.ui.compose && dzg.ui.compose.set && dzg.ui.compose.set(text)) {
    dzg.ui.compose.focus();
    return;
  }
  // Команду можно запустить и без открытого чата (палитра, dzg.commands.run).
  if (!chat) {
    dzg.clipboard.set(text);
    dzg.ui.toast(text, 'Чат не открыт — скопировано');
    return;
  }
  dzg.messages.send(chat.id, text, replyTo ? { replyTo } : undefined);
}

function repliedText(chat, replyTo) {
  if (!replyTo || !chat) return '';
  const message = dzg.messages.get(chat.id, replyTo);
  return message && message.text ? message.text : '';
}

// Команда-преобразование: текст из аргумента уходит в чат, а текст из
// сообщения, на которое отвечаешь, показывается и копируется.
function transform(name, description, fn) {
  dzg.commands.register(name, {
    description,
    run: ({ rest, chat, replyTo }) => {
      if (rest) {
        output(chat, fn(rest), replyTo);
        return;
      }
      const source = repliedText(chat, replyTo);
      if (!source) {
        dzg.ui.toast('Напиши текст после /' + name + ' или ответь командой на сообщение');
        return;
      }
      const result = fn(source);
      dzg.clipboard.set(result);
      dzg.ui.toast(result, '/' + name + ' — скопировано');
    },
  });
}

transform('upper', 'В ВЕРХНИЙ РЕГИСТР', (t) => t.toUpperCase());
transform('lower', 'в нижний регистр', (t) => t.toLowerCase());
transform('mock', 'сПоНжБоБ-рЕгИсТр', mock);
transform('reverse', 'Текст задом наперёд', (t) => Array.from(t).reverse().join(''));
transform('upside', 'ʎнɯоʚǝdǝu ɯɔʞǝɯ', upsideDown);
transform('tr', 'Исправить раскладку: ghbdtn → привет', fixLayout);

dzg.commands.register('b64', {
  description: 'Base64: /b64 текст · /b64 -d строка',
  run: ({ args, rest, chat, replyTo }) => {
    const decode = args[0] === '-d' || args[0] === 'decode' || args[0] === 'd';
    const encode = args[0] === '-e' || args[0] === 'encode' || args[0] === 'e';
    const value = (decode || encode) ? rest.slice(args[0].length).trim() : rest;
    const source = value || repliedText(chat, replyTo);
    if (!source) {
      dzg.ui.toast('/b64 текст — закодировать, /b64 -d строка — раскодировать');
      return;
    }
    try {
      const result = decode ? base64Decode(source) : base64Encode(source);
      if (value) {
        output(chat, result, replyTo);
      } else {
        dzg.clipboard.set(result);
        dzg.ui.toast(result, 'base64 — скопировано');
      }
    } catch (e) {
      dzg.ui.toast('Не получилось: ' + e.message);
    }
  },
});

dzg.commands.register('calc', {
  description: 'Калькулятор: /calc 2*(3+4)^2, sqrt(2), 10!',
  run: ({ rest, chat, replyTo }) => {
    if (!rest) {
      dzg.ui.toast('Пример: /calc (2+3)*4^2 / sqrt(16)');
      return;
    }
    try {
      output(chat, rest + ' = ' + formatNumber(calc(rest)), replyTo);
    } catch (e) {
      dzg.ui.toast('Не посчитать: ' + e.message, '/calc');
    }
  },
});

dzg.commands.register('shrug', {
  description: '¯\\_(ツ)_/¯',
  run: ({ rest, chat, replyTo }) => output(chat, (rest ? rest + ' ' : '') + '¯\\_(ツ)_/¯', replyTo),
});

dzg.commands.register('len', {
  description: 'Сколько символов и слов в тексте',
  run: ({ rest, chat, replyTo }) => {
    const text = rest || repliedText(chat, replyTo);
    if (!text) {
      dzg.ui.toast('/len текст — или ответь командой на сообщение');
      return;
    }
    const words = (text.match(/\S+/g) || []).length;
    dzg.ui.toast(Array.from(text).length + ' симв. · ' + words + ' сл. · ' + text.split('\n').length + ' стр.', '/len');
  },
});

dzg.commands.register('rand', {
  description: 'Случайное число: /rand [от] [до], /rand a b c — выбрать из вариантов',
  run: ({ args, chat, replyTo }) => {
    let text;
    const numbers = args.map(Number);
    if (args.length >= 2 && numbers.every((n) => Number.isFinite(n)) && args.length <= 2) {
      const [a, b] = numbers[0] <= numbers[1] ? numbers : [numbers[1], numbers[0]];
      text = '🎲 ' + (Math.floor(Math.random() * (Math.floor(b) - Math.ceil(a) + 1)) + Math.ceil(a));
    } else if (args.length >= 2) {
      text = '🎲 ' + args[Math.floor(Math.random() * args.length)];
    } else {
      const max = Number.isFinite(numbers[0]) && numbers[0] > 0 ? Math.floor(numbers[0]) : 100;
      text = '🎲 ' + (1 + Math.floor(Math.random() * max));
    }
    output(chat, text, replyTo);
  },
});

// --- Перед отправкой ------------------------------------------------------

dzg.messages.onBeforeSend(({ text }) => {
  let result = text;
  if (setting('replace', true)) result = applyReplacements(result);
  if (setting('autoLayout', false) && looksMistyped(result)) result = fixLayout(result);
  return result === text ? undefined : result;
});

// --- Меню сообщения -------------------------------------------------------

dzg.ui.addMessageMenuItem({
  title: (m) => (looksMistyped(m.text) ? '⌨️ Исправить раскладку: «' + fixLayout(m.text).slice(0, 24) + '»' : '⌨️ Исправить раскладку'),
  when: (m) => !!m.text && /[a-zа-яё]/i.test(m.text),
  onClick: (m) => {
    const fixed = fixLayout(m.text);
    if (m.out && dzg.messages.edit(m.peerId, m.id, fixed)) {
      dzg.ui.toast('Сообщение исправлено');
      return;
    }
    dzg.clipboard.set(fixed);
    dzg.ui.alert(fixed, 'Раскладка исправлена (скопировано)');
  },
});

dzg.ui.addMessageMenuItem({
  title: 'Скопировать как цитату',
  when: (m) => !!m.text,
  onClick: (m) => {
    dzg.clipboard.set(m.text.split('\n').map((l) => '> ' + l).join('\n') + '\n— ' + (m.out ? 'я' : m.fromName));
    dzg.ui.toast('Скопировано');
  },
});

dzg.ui.addMessageMenuItem({
  title: 'Раскодировать base64',
  when: (m) => {
    const t = String(m.text || '').trim();
    if (t.length < 8 || !/^[A-Za-z0-9+/_-]+=*$/.test(t)) return false;
    try { return /\p{L}/u.test(base64Decode(t)); } catch (e) { return false; }
  },
  onClick: (m) => {
    const decoded = base64Decode(m.text.trim());
    dzg.clipboard.set(decoded);
    dzg.ui.alert(decoded, 'base64 (скопировано)');
  },
});
