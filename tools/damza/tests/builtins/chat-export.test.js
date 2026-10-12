import {
  parseCount, parseDateInput, parseCommand, resolveOptions, retryPlan, errorText,
  fileUrl, exportDir, progressInfo, progressBar, busy, describe,
} from '../../../../plugins/builtin/chat-export/main.js';
import {
  toCommonMark, safeLinks, mediaLabel, safeFileName, formatDay, messageHtml, messageMarkdown, dayKey,
} from '../../../../plugins/builtin/chat-export/lib/render.js';

const { eq, assert } = sandbox;
const day = (y, m, d) => Math.floor(new Date(y, m, d).getTime() / 1000);

// --- Разбор ввода ---------------------------------------------------------

eq(parseCount('1000'), 1000);
eq(parseCount('10k'), 10000);
eq(parseCount('1,5к'), 1500);
eq(parseCount('10 000'), 10000);
eq(parseCount('все'), 0);
eq(parseCount('all'), 0);
eq(parseCount(''), null);
assert(Number.isNaN(parseCount('много')));
assert(Number.isNaN(parseCount('-5')));

eq(parseDateInput('2026-10-05'), day(2026, 9, 5));
eq(parseDateInput('2026-10-05', true), day(2026, 9, 6), 'end of day = next midnight');
eq(parseDateInput('05.10.2026'), day(2026, 9, 5));
eq(parseDateInput(''), 0);
assert(Number.isNaN(parseDateInput('2026-02-30')), 'no such date');
assert(Number.isNaN(parseDateInput('вчера')));

let parsed = parseCommand(['md', 'все']);
eq(parsed.formats, ['md']);
eq(parsed.count, 0);
parsed = parseCommand(['JSON', 'html', '500', '2026-10-01', '2026-10-03', 'служебные']);
eq(parsed.formats, ['json', 'html']);
eq([parsed.count, parsed.from, parsed.to, parsed.service], [500, '2026-10-01', '2026-10-03', true]);
eq(parseCommand(['stop']).action, 'cancel');
eq(parseCommand(['list']).action, 'list');
assert(parseCommand(['что-то']).error.includes('что-то'));
assert(parseCommand(['2026-01-01', '2026-01-02', '2026-01-03']).error, 'three dates');

let opts = resolveOptions({ formats: [], count: null });
eq([opts.formats, opts.limit, opts.all, opts.service], [['html'], 1000, false, false], 'defaults from settings');
opts = resolveOptions({ formats: ['json', 'html'], count: 0 });
eq([opts.formats, opts.all, opts.limit], [['html', 'json'], true, 100000], 'all = capped by maxMessages');
assert(resolveOptions({ count: NaN }).error);
assert(resolveOptions({ count: 10, from: '2026-10-07', to: '2026-10-06' }).error.includes('позже'));
assert(resolveOptions({ count: 10, from: '31.31.2026' }).error.includes('Не понял дату'));
eq(resolveOptions({ count: null, from: '2026-10-01' }).all, true, 'a period without a count means the whole period');
opts = resolveOptions({ formats: ['md'], count: 100, from: '2026-10-01', to: '2026-10-03', service: true });
eq(describe(opts), 'Markdown · последние 100 · 01.10.2026 — 03.10.2026 · со служебными');

eq(retryPlan(new Error('FLOOD_WAIT_7'), 1), { ms: 7500, flood: true });
eq(retryPlan(new Error('TIMEOUT'), 1), { ms: 2000, flood: false });
eq(retryPlan(new Error('FLOOD_WAIT_100000'), 1), null, 'too long to wait');
eq(retryPlan(new Error('CHANNEL_PRIVATE'), 1), null);
eq(retryPlan(new Error('TIMEOUT'), 99), null, 'retries are limited');
assert(errorText(new Error('FLOOD_WAIT_3600')).includes('1 ч'));
eq(errorText(new Error('CHANNEL_PRIVATE')), 'Нет доступа к истории этого чата.');

eq(exportDir(), '/tmp/sandbox-data/chat-export/exports/');
eq(fileUrl('/tmp/a b/Тест#1/x.html'), 'file:///tmp/a%20b/%D0%A2%D0%B5%D1%81%D1%82%231/x.html');
eq(fileUrl('C:\\Users\\me\\x.json'), 'file:///C:/Users/me/x.json');

eq(progressBar(0.5), '▰▰▰▰▰▰▰▱▱▱▱▱▱▱');
const progress = progressInfo({ started: 0, opts: { all: false, limit: 1000 }, exported: 250, scanned: 250, total: 0, lastDate: day(2026, 9, 5) }, 1000);
assert(progress.bar.endsWith('25%'), progress.bar);
assert(progress.status.includes('250 из 1\u00a0000') && progress.status.includes('250 сообщ./с') && progress.status.includes('осталось ≈ 3 с'), progress.status);
assert(progress.where.includes('05.10.2026'), progress.where);
const unknown = progressInfo({ started: 0, opts: { all: true, limit: 100000 }, exported: 40, scanned: 50, total: 0 }, 1000);
assert(unknown.fraction < 0 && unknown.bar.includes('50 просмотрено'), unknown.bar);

// --- Форматирование -------------------------------------------------------

eq(toCommonMark('__курсив__ и **жирный**'), '*курсив* и **жирный**');
eq(toCommonMark('http://x.com/__init__.py'), 'http://x.com/__init__.py', 'bare links untouched');
eq(toCommonMark('`__x__`'), '`__x__`', 'code untouched');
eq(toCommonMark('[__a__](https://x.com/__b__)'), '[*a*](https://x.com/__b__)');
eq(toCommonMark('a\nb'), 'a  \nb', 'hard line breaks');
eq(toCommonMark('||спойлер||'), 'спойлер');
eq(toCommonMark('# не заголовок <b>'), '\\# не заголовок \\<b>');
eq(toCommonMark('\\_\\_'), '\\_\\_', 'escapes kept');

eq(safeLinks('<a href="javascript:alert(1)">x</a><a href="https://ok.dev">y</a>'),
  '<a>x</a><a href="https://ok.dev" target="_blank" rel="noopener noreferrer">y</a>');
eq(mediaLabel({ media: 'photo', mediaInfo: { width: 1280, height: 720, size: 250000 } }), '📷 Фото · 1280×720 · 244 КБ');
eq(mediaLabel({ media: 'voice', mediaInfo: { duration: 42 } }), '🎤 Голосовое сообщение · 0:42');
eq(mediaLabel({ media: 'file', mediaInfo: { fileName: 'отчёт.pdf', size: 3 * 1024 * 1024 } }), '📎 Файл · отчёт.pdf · 3,0 МБ');
eq(mediaLabel({}), '');
eq(safeFileName('DamZaGram Chat'), 'DamZaGram_Chat');
eq(safeFileName('Чат: «Ромашка» 🌼'), 'Чат_Ромашка');
eq(safeFileName('///', 'chat_5'), 'chat_5');
eq(formatDay(day(2026, 9, 5)), 'Понедельник, 5 октября 2026');

const rich = {
  text: 'Жирный текст, сайт damza.dev, @wetakush и тайная ссылка',
  entities: [
    { type: 'bold', offset: 0, length: 6 },
    { type: 'url', offset: 19, length: 9 },
    { type: 'mention', offset: 30, length: 9 },
    { type: 'textLink', offset: 49, length: 6, url: 'javascript:alert(1)' },
    { type: 'italic', offset: 7, length: 5 },
  ],
};
const richHtml = messageHtml(rich);
assert(richHtml.includes('<b>Жирный</b>'), richHtml);
assert(richHtml.includes('<i>текст</i>'), richHtml);
assert(richHtml.includes('<a href="https://damza.dev" target="_blank" rel="noopener noreferrer">damza.dev</a>'), richHtml);
assert(richHtml.includes('<a href="https://t.me/wetakush"'), richHtml);
assert(!richHtml.includes('javascript'), 'unsafe links removed: ' + richHtml);
const richMd = messageMarkdown(rich);
assert(richMd.startsWith('**Жирный** *текст*'), richMd);
eq(messageHtml({ text: '<script>alert(1)</script>', entities: [] }), '&lt;script&gt;alert(1)&lt;/script&gt;');

// --- Моки клиента ---------------------------------------------------------

const chat = mock.chats[1]; // группа «DamZaGram Chat»
const authors = [{ fromId: '7', fromName: 'Петя' }, { fromId: '8', fromName: 'Маша' }];
const start = new Date(2026, 9, 5, 9, 0, 0).getTime() / 1000; // пн, 5 октября, 9:00
const history = [];
for (let i = 1; i <= 250; ++i) {
  const over = Object.assign({ id: i, peerId: chat.id, chat, date: start + (i - 1) * 1200, text: 'сообщение №' + i }, authors[i % 2]);
  if (i === 1) Object.assign(over, { service: true, text: 'Вася создал группу' });
  if (i === 2) Object.assign(over, rich, authors[0]);
  if (i === 3) Object.assign(over, { replyToId: 2, text: 'Отвечаю на жирный' });
  if (i === 4) Object.assign(over, { text: '', media: 'photo', mediaInfo: { width: 1280, height: 720, size: 250000 } });
  if (i === 5) Object.assign(over, { out: true, fromId: '1', fromName: 'Я', text: 'Моё сообщение' });
  if (i === 6) Object.assign(over, { forwardFrom: { name: 'Новости', date: start - 86400 } });
  if (i === 7) Object.assign(over, { reactions: [{ emoji: '👍', count: 3, chosen: true }] });
  if (i === 8) Object.assign(over, { text: '', media: 'voice', mediaInfo: { duration: 42 } });
  if (i === 9) Object.assign(over, { replyToId: 1000000, text: 'Ответ на что-то старое' });
  history.push(mock.message(over));
}
mock.history[chat.id] = history;
const days = new Set(history.map((m) => dayKey(m.date)));

mock.iterateCalls = [];
mock.failAt = 0;
mock.failError = '';
let yielded = 0;
dzg.messages.iterate = async function* (peer, options) {
  const o = options || {};
  mock.iterateCalls.push(Object.assign({}, o));
  const all = (mock.history[String((peer && peer.id) || peer)] || []).slice().sort((a, b) => a.id - b.id);
  const list = o.offsetId ? all.filter((m) => m.id < o.offsetId) : all;
  for (let i = list.length - 1; i >= 0; --i) {
    if (mock.failAt && yielded >= mock.failAt) {
      mock.failAt = 0;
      throw new Error(mock.failError || 'FLOOD_WAIT_2');
    }
    if (yielded % 100 === 0) await null;
    yielded++;
    yield list[i];
  }
};
const plainHistory = dzg.messages.history;
dzg.messages.history = (p, o) => {
  if (o && o.offsetDate) {
    const older = (mock.history[String((p && p.id) || p)] || []).filter((m) => m.date < o.offsetDate);
    return Promise.resolve(older.slice(-(o.limit || 50)));
  }
  return plainHistory(p, o);
};
mock.boxes = [];
mock.boxAnswers = [];
dzg.ui.box = (spec) => {
  mock.boxes.push(spec);
  return Promise.resolve(mock.boxAnswers.length ? mock.boxAnswers.shift() : { button: 'cancel', values: {} });
};
mock.sentFiles = [];
dzg.messages.sendFile = (p, paths, options) => {
  mock.sentFiles.push({ peerId: String((p && p.id) || p), paths, options });
  return true;
};

async function drain() {
  for (let i = 0; i < 5000; ++i) {
    await mock.tick(20);
    if (!busy()) {
      await mock.tick(20);
      return;
    }
    sandbox.runTimers();
  }
  throw new Error('export did not finish');
}

const dir = exportDir();
const today = dayKey(Date.now() / 1000);
const fileName = (suffix, ext) => dir + 'DamZaGram_Chat-' + today + suffix + '.' + ext;
const lastToast = () => mock.toasts[mock.toasts.length - 1] || '';
const exportedIds = (json) => json.messages.map((m) => m.id);

// --- /export html все: вся история в HTML ---------------------------------

mock.command('/export html все', chat);
assert(busy(), 'export started');
await drain();
const html = mock.files[fileName('', 'html')];
assert(html, 'html written: ' + Object.keys(mock.files).join(', '));
assert(html.startsWith('<!doctype html>'));
assert(html.includes('#C8F135') && html.includes('prefers-color-scheme:dark') && html.includes(':root[data-theme="dark"]'), 'theme tokens');
assert(html.includes('Экспортировано <b>DamZaGram</b>'), 'author line');
assert(html.includes('<b>Жирный</b>') && html.includes('href="https://damza.dev"'), 'entities rendered');
assert(!html.includes('javascript:'), 'no unsafe links');
assert(!html.includes('Вася создал группу'), 'service messages skipped by default');
assert(html.includes('<a class="reply" href="#m2"><b>Петя</b>Жирный текст'), 'reply resolved to the quote');
assert(html.includes('↩ Ответ на сообщение вне экспорта'), 'reply outside the export links to t.me');
assert(html.includes('📷 Фото · 1280×720 · 244 КБ') && html.includes('🎤 Голосовое сообщение · 0:42'), 'media placeholders');
assert(html.includes('↪ Переслано от <b>Новости</b>'), 'forward');
assert(html.includes('<span class="me">👍 3</span>'), 'reactions');
assert(html.includes('class="msg out first" id="m5"'), 'outgoing bubble');
assert(html.includes('https://t.me/c/200/250'), 'message links in groups');
assert(html.indexOf('id="m2"') < html.indexOf('id="m100"') && html.indexOf('id="m100"') < html.indexOf('id="m250"'), 'chronological order');
eq(html.split('<div class="day">').length - 1, days.size, 'one separator per day');
assert(html.indexOf('Понедельник, 5 октября 2026') < html.indexOf('id="m2"'), 'day header before its messages');
assert(html.includes('249 сообщений'), 'count chip');

let note = mock.notifications[mock.notifications.length - 1];
assert(note.title.includes('Экспорт готов') && note.text.includes('249 сообщений') && note.text.includes('.html'), note.text);
note.onClick();
eq(mock.links[mock.links.length - 1], 'file:///tmp/sandbox-data/chat-export/exports/');
let panel = mock.lastPanel();
assert(mock.panelText(panel).includes('✅ Готово'), mock.panelText(panel));
mock.click(panel, 'Открыть HTML');
eq(mock.links[mock.links.length - 1], fileUrl(fileName('', 'html')));
mock.click(panel, 'Отправить в Избранное');
mock.click(panel, 'Отправить в Избранное');
eq(mock.sentFiles.length, 1, 'no double send');
eq(mock.sentFiles[0].peerId, '1');
eq(mock.sentFiles[0].paths, [fileName('', 'html')]);
mock.click(panel, 'Скопировать путь');
eq(mock.clipboard, fileName('', 'html'));
eq(sandbox.storage.history.length, 1);

// --- /export json 100: последние 100 сообщений -----------------------------

mock.command('/export json 100', chat);
await drain();
let json = JSON.parse(mock.files[fileName('', 'json')]);
eq(json.messages.length, 100);
eq(json.count, 100);
eq(json.exportedBy, 'DamZaGram');
eq(json.chat.name, 'DamZaGram Chat');
eq(json.messages[0].id, 151);
eq(json.messages[99].id, 250, 'oldest first, newest last');
assert(!('chat' in json.messages[0]), 'chat info is in the header only');
eq(Object.keys(json.messages[0]).filter((k) => ['id', 'fromName', 'text', 'date', 'entities', 'editDate'].includes(k)).length, 6,'full Message objects');
eq(json.filter.count, 100);

// --- Меню чата → окно → Markdown со служебными ----------------------------

assert(mock.menu('chat', chat).some((i) => i.title.includes('Экспорт чата…')), 'chat menu item');
mock.boxAnswers.push({ button: 'ok', values: { count: 'все', from: '', to: '', html: false, md: true, json: false, service: true } });
mock.menuClick('chat', chat, 'Экспорт чата');
await drain();
const box = mock.boxes[mock.boxes.length - 1];
eq(box.inputs.map((i) => i.id), ['count', 'from', 'to']);
eq(box.toggles.map((t) => t.id), ['html', 'md', 'json', 'service']);
const md = mock.files[fileName('', 'md')];
assert(md, 'markdown written');
assert(md.startsWith('# DamZaGram Chat\n'), md.slice(0, 80));
assert(md.includes('## Понедельник, 5 октября 2026'), 'day header');
assert(md.includes('*— Вася создал группу · 09:00 —*'), 'service message included');
assert(md.includes('**Петя** · [09:20](https://t.me/c/200/2)'), 'author, time and link');
assert(md.includes('> ↩ **Петя:** Жирный текст'), 'reply quote');
assert(md.includes('**Жирный** *текст*'), 'formatting in CommonMark');
assert(md.includes('*📷 Фото · 1280×720 · 244 КБ*'), 'media placeholder');
assert(md.includes('↪ Переслано от **Новости**'), 'forward');
assert(md.includes('👍 3'), 'reactions');
assert(md.includes('Экспортировано DamZaGram'), 'author line');
assert(md.indexOf('сообщение №10\n') < md.indexOf('сообщение №250'), 'chronological');
eq(sandbox.storage.last.formats, ['md'], 'options remembered');

// --- Период по датам ------------------------------------------------------

mock.command('/export json 2026-10-06 2026-10-06', chat); // период без числа = весь период
await drain();
json = JSON.parse(mock.files[fileName('-2', 'json')]);
const expected = history.filter((m) => !m.service && dayKey(m.date) === '2026-10-06').map((m) => m.id);
eq(exportedIds(json), expected, 'only that day');
eq([json.filter.from, json.filter.to], ['2026-10-06', '2026-10-06']);
assert(mock.iterateCalls[mock.iterateCalls.length - 1].offsetId === expected[expected.length - 1] + 1, 'jumped straight to the date');

// Пустой период: файлы не пишутся.
let count = Object.keys(mock.files).length;
mock.command('/export json 2020-01-01 2020-01-02', chat);
await drain();
eq(Object.keys(mock.files).length, count);
assert(mock.panelText(mock.lastPanel()).includes('Нет сообщений') || lastToast().includes('Нет сообщений'));

// --- FLOOD_WAIT посреди экспорта: ждём и продолжаем без дублей -------------

mock.failAt = yielded + 120;
mock.failError = 'FLOOD_WAIT_2';
mock.command('/export json все', chat);
await drain();
json = JSON.parse(mock.files[fileName('-3', 'json')]);
eq(json.messages.length, 249, 'everything exported after the wait');
eq(new Set(exportedIds(json)).size, 249, 'no duplicates after resume');
const resumed = mock.iterateCalls[mock.iterateCalls.length - 1];
eq(resumed.offsetId, 250 - 119, 'resumed from the last seen message');

// --- Отмена ---------------------------------------------------------------

count = Object.keys(mock.files).length;
mock.command('/export html все', chat);
await mock.tick(200);
assert(busy());
panel = mock.lastPanel();
assert(mock.panelText(panel).includes('Выгружено'), mock.panelText(panel));
mock.click(panel, 'Отменить экспорт');
await drain();
eq(Object.keys(mock.files).length, count, 'nothing written');
assert(mock.panelText(mock.lastPanel()).includes('Экспорт отменён'));

// /export stop тоже отменяет.
mock.command('/export md все', chat);
await mock.tick(200);
mock.command('/export stop', chat);
await drain();
eq(Object.keys(mock.files).length, count);

// --- Остановить и сохранить загруженное -----------------------------------

mock.command('/export md все', chat);
await mock.tick(200);
mock.click(mock.lastPanel(), 'Остановить и сохранить');
await drain();
const partial = mock.files[fileName('-2', 'md')];
assert(partial && partial.includes('Экспорт остановлен вручную'), 'partial export saved');
assert(partial.includes('сообщение №250') && !partial.includes('сообщение №10\n'), 'newest part kept');

// --- Повторный запуск во время экспорта -----------------------------------

count = Object.keys(mock.files).length;
mock.command('/export json все', chat);
await mock.tick(200);
mock.command('/export html 100', chat);
assert(lastToast().includes('Уже идёт экспорт'), lastToast());
mock.menuClick('chat', chat, 'Экспорт чата');
assert(lastToast().includes('Уже идёт экспорт'), 'menu is guarded too');
await drain();
eq(Object.keys(mock.files).length, count + 1, 'only one export ran');

// --- Сменился аккаунт: остановка и «Сохранить загруженное» ----------------

const realSelf = mock.self;
mock.command('/export json все', chat);
await mock.tick(200);
mock.self = mock.peer(42, 'Другой', 'user', { isSelf: true });
await drain();
mock.self = realSelf;
panel = mock.lastPanel();
assert(mock.panelText(panel).includes('Сменился активный аккаунт'), mock.panelText(panel));
count = Object.keys(mock.files).length;
mock.click(panel, 'Сохранить загруженное');
await drain();
eq(Object.keys(mock.files).length, count + 1, 'loaded part saved on request');

// Ошибка доступа без загруженных сообщений.
mock.failAt = yielded;
mock.failError = 'CHANNEL_PRIVATE';
mock.command('/export json все', chat);
await drain();
assert(mock.panelText(mock.lastPanel()).includes('Нет доступа к истории'), mock.panelText(mock.lastPanel()));

// --- Окно: ошибка в дате открывает окно снова -----------------------------

const boxes = mock.boxes.length;
mock.boxAnswers.push({ button: 'ok', values: { count: '100', from: '2026-13-01', to: '', html: true, md: false, json: false, service: false } });
mock.command('/export', chat);
await mock.tick(50);
assert(lastToast().includes('Не понял дату'), lastToast());
eq(mock.boxes.length, boxes + 2, 'box reopened');
eq(mock.boxes[mock.boxes.length - 1].inputs[1].value, '2026-13-01', 'entered values kept');
assert(!busy());

// --- Список экспортов -----------------------------------------------------

mock.command('/export list', chat);
panel = mock.lastPanel();
assert(mock.panelText(panel).includes('«DamZaGram Chat»'), mock.panelText(panel));
mock.click(panel, 'Открыть папку с экспортами');
eq(mock.links[mock.links.length - 1], fileUrl(dir));
mock.click(panel, 'Очистить список');
eq(sandbox.storage.history.length, 0);

// --- Выгрузка плагина посреди экспорта ------------------------------------

count = Object.keys(mock.files).length;
mock.command('/export html все', chat);
await mock.tick(200);
sandbox.event('unload');
eq(mock.openPanels().length, 0, 'panel closed');
await drain();
eq(Object.keys(mock.files).length, count, 'nothing written after unload');
