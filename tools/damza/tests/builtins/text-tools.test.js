import {
  fixLayout, looksMistyped, calc, formatNumber, base64Encode, base64Decode, mock as spongebob, upsideDown, applyReplacements,
} from '../../../../plugins/builtin/text-tools/main.js';

const { eq, assert } = sandbox;

// Layout.
eq(fixLayout('ghbdtn'), 'привет');
eq(fixLayout('Ghbdtn? rfr ltkf&'), 'Привет, как дела?');
eq(fixLayout('руддщ'), 'hello');
assert(looksMistyped('ghbdtn rfr ltkf'));
assert(!looksMistyped('hello world'));
assert(!looksMistyped('привет'));
assert(!looksMistyped('https://t.me/wetakush'));

// Calculator.
eq(calc('2*(3+4)'), 14);
eq(calc('2^3^2'), 512);
eq(calc('-2^2'), -4);
eq(calc('sqrt(16) + abs(-3)'), 7);
eq(calc('10 % 4'), 2);
eq(calc('5!'), 120);
eq(calc('2,5*2'), 5);
eq(calc('max(1, 7, 3)'), 7);
eq(formatNumber(calc('0.1+0.2')), '0.3');
eq(formatNumber(calc('1/3')), '0.333333333333');
eq(formatNumber(1 / 0), '∞');
let failed = false;
try { calc('2 + alert(1)'); } catch (e) { failed = true; }
assert(failed, 'unknown identifiers rejected');
failed = false;
try { calc('(1+2'); } catch (e) { failed = true; }
assert(failed, 'unbalanced');

// Base64 round trip with UTF-8.
eq(base64Encode('Привет, DamZaGram! 🚀'), '0J/RgNC40LLQtdGCLCBEYW1aYUdyYW0hIPCfmoA=');
eq(base64Decode('0J/RgNC40LLQtdGCLCBEYW1aYUdyYW0hIPCfmoA='), 'Привет, DamZaGram! 🚀');
eq(base64Decode(base64Encode('a')), 'a');
eq(base64Decode(base64Encode('ab')), 'ab');

eq(spongebob('привет мир'), 'пРиВеТ мИр');
eq(upsideDown('hello'), 'oʃʃǝɥ'.replace(/ʃ/g, 'l'));
eq(applyReplacements('ну :shrug:'), 'ну ¯\\_(ツ)_/¯');

// Commands send the result.
mock.command('/calc 2+2*2');
eq(mock.sent[0].text, '2+2*2 = 6');
mock.command('/tr ghbdtn');
eq(mock.sent[1].text, 'привет');
mock.command('/upper тест');
eq(mock.sent[2].text, 'ТЕСТ');
mock.command('/b64 -d 0J/RgNC40LLQtdGC');
eq(mock.sent[3].text, 'Привет');
mock.command('/mock abc');
eq(mock.sent[4].text, 'aBc');
mock.command('/calc 2+');
eq(mock.sent.length, 5, 'error is a toast');
assert(mock.toasts[mock.toasts.length - 1].includes('Не посчитать'));

// Commands list includes all tools.
const names = dzg.commands.list().map((c) => c.name);
for (const name of ['calc', 'shrug', 'upper', 'lower', 'mock', 'reverse', 'b64', 'tr', 'len', 'rand', 'upside']) {
  assert(names.includes(name), 'missing /' + name);
}

// Result into the compose field instead of sending.
sandbox.settings.toCompose = true;
mock.command('/lower ПРИВЕТ');
eq(mock.compose.text, 'привет');
eq(mock.sent.length, 5);
sandbox.settings.toCompose = false;

// Before-send hook: replacements and the optional layout fix.
let result = sandbox.dispatch('beforeSend', { text: 'ок :flip:', chat: mock.current, peerId: '100', replyTo: 0 });
eq(result.text, 'ок (╯°□°)╯︵ ┻━┻');
sandbox.settings.autoLayout = true;
result = sandbox.dispatch('beforeSend', { text: 'ghbdtn rfr ltkf', chat: mock.current, peerId: '100', replyTo: 0 });
eq(result.text, 'привет как дела');

// Message menu: fixing the layout of an own message edits it.
const own = mock.message({ id: 9, out: true, text: 'ghbdtn' });
const items = mock.menu('message', own);
assert(items[0].title.includes('привет'), items[0].title);
mock.menuClick('message', own, 'Исправить раскладку');
eq(mock.edited[0], { peerId: '100', id: 9, text: 'привет' });
const theirs = mock.message({ id: 10, text: 'rfr ltkf' });
mock.menuClick('message', theirs, 'Исправить раскладку');
eq(mock.clipboard, 'как дела');
assert(mock.menu('message', mock.message({ text: '0J/RgNC40LLQtdGC' })).some((i) => i.title.includes('base64')));
