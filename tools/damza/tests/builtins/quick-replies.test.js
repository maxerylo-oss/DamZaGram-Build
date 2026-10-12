import {
  expand, questions, firstWord, cleanShortcut, suggestName, parseNameAnswer, search, resolve,
  parseImport, merge, exportJson, validate, pickIcon, load, edit, DEFAULT_SNIPPETS,
} from '../../../../plugins/builtin/quick-replies/main.js';

const { eq, assert } = sandbox;

// dzg.ui.box is not mocked by _setup.js: answer from a queue.
mock.boxes = [];
mock.boxAnswers = [];
dzg.ui.box = (spec) => {
  mock.boxes.push(spec);
  return Promise.resolve(mock.boxAnswers.length ? mock.boxAnswers.shift() : { button: 'cancel', values: {} });
};

// Controlled clock: the send rate limit counts real time.
const realNow = Date.now;
let offset = 0;
Date.now = () => realNow() + offset;
const later = () => { offset += 20000; };

const lastToast = () => mock.toasts[mock.toasts.length - 1] || '';
const lastSent = () => mock.sent[mock.sent.length - 1];
const snippets = () => sandbox.storage.snippets;
const byName = (name) => snippets().find((s) => s.name === name);
const send = (text, chat, replyTo) => sandbox.dispatch('beforeSend', {
  text, chat: chat || mock.current, peerId: (chat || mock.current).id, replyTo: replyTo || 0,
});
const openPanel = () => {
  mock.composeButtons.filter((b) => !b.removed)[0].onClick(mock.current);
  return mock.lastPanel();
};
const group = mock.chats[1];

// --- Pure helpers ---------------------------------------------------------

const now = new Date(2026, 9, 12, 9, 5);
eq(expand('Привет, {first}! 👋', { first: 'Вася' }), 'Привет, Вася! 👋');
eq(expand('Привет, {first}! 👋', { first: '' }), 'Привет! 👋', 'empty variable takes its comma');
eq(expand('{first}, привет', {}), 'привет');
eq(expand('Ну, {first}, привет', {}), 'Ну, привет');
eq(expand('Привет {first}', {}), 'Привет');
eq(expand('Сегодня {date} в {time}', { now }), 'Сегодня 12.10.2026 в 09:05');
eq(expand('{имя} · {дата} · {время}', { first: 'Аня', now }), 'Аня · 12.10.2026 · 09:05', 'russian aliases');
eq(expand('Чат: {name}, буфер: {clipboard}', { chatName: 'DamZaGram Chat', clipboard: () => 'abc' }), 'Чат: DamZaGram Chat, буфер: abc');
eq(expand('{me} на связи', { me: 'Даня' }), 'Даня на связи');
eq(expand('Неизвестное {foo} остаётся, {NAME} — нет', { chatName: 'X' }), 'Неизвестное {foo} остаётся, X — нет');
eq(expand('Буду через {?Сколько минут?|10} мин', { answers: { 'Сколько минут?': '15' } }), 'Буду через 15 мин');
eq(expand('Адрес', { rest: 'кв. 5' }), 'Адрес кв. 5', 'rest appended to one line');
eq(expand('Строка 1\nСтрока 2', { rest: 'хвост' }), 'Строка 1\nСтрока 2\nхвост', 'rest on a new line');
eq(expand('Итого: {rest}. Спасибо', { rest: '100 ₽' }), 'Итого: 100 ₽. Спасибо');
eq(expand('Итого:{rest}', {}), 'Итого:');
eq(questions('{?A} и {?B|x} и снова {?A}'), [{ question: 'A', fallback: '' }, { question: 'B', fallback: 'x' }]);
eq(questions('без вопросов {name}'), []);
eq(firstWord('🌸 Анна-Мария Петрова'), 'Анна-Мария');
eq(firstWord('Вася Пупкин'), 'Вася');
eq(firstWord('  '), '');
eq(cleanShortcut(';;Addr'), 'addr');
eq(cleanShortcut(''), '');
eq(cleanShortcut('два слова'), null);
eq(cleanShortcut('a!'), null);
eq(suggestName('Мой адрес: ул. Ленина, д. 5'), 'Мой адрес ул Ленина');
eq(suggestName('{first}!'), '{first}!');
eq(parseNameAnswer('Адрес ;;addr'), { name: 'Адрес', shortcut: 'addr' });
eq(parseNameAnswer(' Адрес '), { name: 'Адрес', shortcut: '' });
eq(pickIcon(['search', 'chat', 'reply']), 'chat');
eq(pickIcon(['reply']), 'reply');
eq(pickIcon([]), 'chat');

// --- First run: defaults, button, menu, command ---------------------------

eq(snippets().map((s) => s.name), DEFAULT_SNIPPETS.map((d) => d.name), 'four defaults seeded');
eq(sandbox.storage.seeded, true);
eq(mock.composeButtons.length, 1, 'compose button');
assert(dzg.ui.icons().includes(mock.composeButtons[0].icon), 'icon from dzg.ui.icons()');
assert(mock.mainMenu.some((m) => m.title === 'Быстрые ответы'), 'main menu item');
assert(dzg.commands.list().some((c) => c.name === 'q'), '/q registered');

const list = load();
eq(search(list, 'спас').map((s) => s.name), ['Спасибо']);
eq(search(list, ';;b').map((s) => s.name), ['Занят']);
eq(search(list, 'освобожусь').map((s) => s.name), ['Занят'], 'search in text');
eq(search(list, '').length, 4);
eq(resolve(list, 'спасибо Петя').snippet.name, 'Спасибо');
eq(resolve(list, 'спасибо Петя').rest, 'Петя');
eq(resolve(list, 'hi').snippet.name, 'Приветствие', 'by shortcut');
eq(resolve(list, ';;late 5').rest, '5');
eq(resolve(list, 'опазд').snippet.name, 'Опаздываю', 'unique prefix');
eq(resolve(list, 'xyz').snippet, null);

// --- Panel: search, Enter inserts, the panel closes -----------------------

let panel = openPanel();
assert(panel.open, 'panel opened');
let text = mock.panelText(panel);
for (const d of DEFAULT_SNIPPETS) assert(text.includes(d.name), d.name + ' listed:\n' + text);
assert(text.includes('Управление'), 'management section');
assert(text.includes(';;hi'), 'shortcut shown');

mock.type(panel, 'спас');
text = mock.panelText(panel);
assert(text.includes('Спасибо') && !text.includes('Опаздываю'), 'filtered:\n' + text);
assert(!text.includes('Управление'), 'management hidden while searching');
mock.submit(panel);
await mock.tick();
eq(mock.compose.text, 'Спасибо, Вася! Очень помогло 🙏');
assert(mock.composeFocused, 'compose focused after insert');
assert(!panel.open, 'closed after insert');
eq(byName('Спасибо').uses, 1, 'usage counted');

// The compose button toggles the panel.
panel = openPanel();
assert(panel.open);
openPanel();
assert(!panel.open, 'second click closes');

// Nothing found offers to create one.
panel = openPanel();
mock.type(panel, 'реквизиты');
assert(mock.panelText(panel).includes('Ничего не нашлось'));
assert(mock.findItem(panel, 'button', 'Создать «реквизиты»'));
panel.api.close();

// --- «Отправить сразу» and the double-click guard -------------------------

mock.compose = null;
panel = openPanel();
mock.type(panel, 'hi');
mock.click(panel, 'Отправить сразу');
await mock.tick();
eq(mock.sent.length, 1);
eq(lastSent(), { peerId: '100', text: 'Привет, Вася! 👋', options: {} });
assert(!panel.open, 'closed after send');
mock.click(panel, 'Отправить сразу');
await mock.tick();
eq(mock.sent.length, 1, 'double click does not send twice');
assert(lastToast().includes('только что'), lastToast());

// --- Ask-variables {?…} ---------------------------------------------------

later();
panel = openPanel();
mock.type(panel, 'опазд');
mock.promptAnswers.push('15');
mock.click(panel, 'Отправить сразу');
await mock.tick();
let prompt = mock.prompts[mock.prompts.length - 1];
eq(prompt.title, 'Через сколько минут?');
eq(prompt.options.value, '10', 'default from {?…|10}');
eq(lastSent().text, 'Немного задерживаюсь, буду через 15 мин. Извини!');
eq(sandbox.storage.answers['Через сколько минут?'], '15', 'answer remembered');

// Cancelled question sends nothing; the remembered answer is suggested.
later();
panel = openPanel();
mock.type(panel, 'опазд');
mock.click(panel, 'Отправить сразу');
await mock.tick();
prompt = mock.prompts[mock.prompts.length - 1];
eq(prompt.options.value, '15', 'last answer suggested');
eq(mock.sent.length, 2, 'cancel sends nothing');
assert(panel.open, 'stays open after cancel');
panel.api.close();

// A second request while a question is open is refused (no re-entrancy).
later();
const realPrompt = dzg.ui.prompt;
let answerPrompt = null;
dzg.ui.prompt = () => new Promise((resolve) => { answerPrompt = resolve; });
mock.command('/q опаздываю');
await mock.tick();
mock.command('/q hi');
await mock.tick();
assert(lastToast().includes('Сначала закончи'), lastToast());
eq(mock.sent.length, 2);
answerPrompt('5');
await mock.tick();
eq(lastSent().text, 'Немного задерживаюсь, буду через 5 мин. Извини!');
eq(mock.sent.length, 3);
dzg.ui.prompt = realPrompt;

// --- /q -------------------------------------------------------------------

later();
mock.command('/q hi');
await mock.tick();
eq(lastSent(), { peerId: '100', text: 'Привет, Вася! 👋', options: {} });

// In a group as a reply: {first} is the author of the replied message.
dzg.messages.get = (p, id) => (id === 55 ? mock.message({ id: 55, peerId: '200', chat: group, fromName: 'Петя Иванов' }) : null);
sandbox.dispatch('beforeSend', { text: '/q ty', chat: group, peerId: '200', replyTo: 55 });
await mock.tick();
eq(lastSent(), { peerId: '200', text: 'Спасибо, Петя! Очень помогло 🙏', options: { replyTo: 55 } });

// Editor: invalid shortcut reopens the box with the typed text, then saves.
panel = openPanel();
mock.boxAnswers.push(
  { button: 'ok', values: { name: 'Счёт', shortcut: 'два слова', text: 'Итого к оплате: {rest}. Реквизиты в закрепе.' } },
  { button: 'ok', values: { name: 'Счёт', shortcut: ';;bill', text: 'Итого к оплате: {rest}. Реквизиты в закрепе.' } });
mock.click(panel, 'Новый быстрый ответ');
await mock.tick();
eq(mock.boxes.length, 2, 'box reopened after an error');
assert(mock.toasts.some((t) => t.includes('одно слово')), 'shortcut error shown');
eq(mock.boxes[1].inputs[2].value, 'Итого к оплате: {rest}. Реквизиты в закрепе.', 'typed text kept');
eq(byName('Счёт').shortcut, 'bill');
assert(mock.panelText(panel).includes('Счёт'), 'panel re-rendered');
panel.api.close();
eq(validate({ name: 'счёт', shortcut: '', text: 'x' }, null, load()).error, '«Счёт» уже есть — выбери другое название');
eq(validate({ name: 'Другое', shortcut: 'BILL', text: 'x' }, null, load()).error, 'Сокращение ;;bill уже у «Счёт»');
eq(validate({ name: '', shortcut: '', text: '  ' }, null, load()).error, 'Напиши текст ответа');
eq(validate({ name: '', shortcut: '', text: 'Новый текст тут' }, null, load()).value.name, 'Новый текст тут');

later();
mock.command('/q счет 1500 ₽');
await mock.tick();
eq(lastSent().text, 'Итого к оплате: 1500 ₽. Реквизиты в закрепе.', '{rest} and ё = е in names');

// Unknown name: toast and the full list; /q alone opens the list.
const panelsBefore = mock.panels.length;
mock.command('/q абракадабра');
await mock.tick();
assert(lastToast().includes('нет'), lastToast());
eq(mock.panels.length, panelsBefore + 1, 'list opened');
mock.lastPanel().api.close();
mock.command('/q');
assert(mock.lastPanel().open, '/q opens the panel');
mock.lastPanel().api.close();

// Without a chat (palette, dzg.commands.run) it goes to the compose field.
mock.compose = null;
const sentBefore = mock.sent.length;
sandbox.dispatch('builtin:run', { text: '/q busy', chat: null, peerId: '', replyTo: 0 });
await mock.tick();
eq(mock.sent.length, sentBefore, 'nothing sent without a chat');
eq(mock.compose.text, 'Сейчас не могу ответить — напишу, как освобожусь 🙌');

// --- ;;shortcut before sending ---------------------------------------------

later();
eq(send(';;hi'), { text: 'Привет, Вася! 👋' });
eq(send(' ;;HI '), { text: 'Привет, Вася! 👋' }, 'case and spaces');
eq(send(';;hi', group), { text: 'Привет! 👋' }, '{first} is empty in a group');
eq(send('смотри ;;hi'), { text: 'смотри ;;hi' }, 'only the whole message');
eq(send(';;)'), { text: ';;)' }, 'not a shortcut');

// Unknown shortcut: not sent, text back in the field; the second time it goes as is.
mock.compose = { chat: mock.current, text: '' };
eq(send(';;nope'), { cancel: true });
assert(lastToast().includes(';;nope'), lastToast());
sandbox.runTimers();
eq(mock.compose.text, ';;nope', 'text restored');
eq(send(';;nope'), { text: ';;nope' }, 'second send passes');

// With a question: cancelled, asked, then sent as a reply.
later();
mock.promptAnswers.push('20');
eq(send(';;late', mock.current, 7), { cancel: true });
sandbox.runTimers();
await mock.tick();
eq(lastSent(), { peerId: '100', text: 'Немного задерживаюсь, буду через 20 мин. Извини!', options: { replyTo: 7 } });

// Markdown: the client would drop it from replaced text, so the plugin sends.
mock.boxAnswers.push({ button: 'ok', values: { name: 'Важно', shortcut: 'imp', text: '**Важно:** прочитай закреп' } });
await edit(null);
later();
eq(send(';;imp', mock.current, 3), { cancel: true });
sandbox.runTimers();
eq(lastSent(), { peerId: '100', text: '**Важно:** прочитай закреп', options: { replyTo: 3, parse: 'markdown' } });
sandbox.settings.markdown = false;
eq(send(';;imp'), { text: '**Важно:** прочитай закреп' }, 'markdown off: plain replacement');
sandbox.settings.markdown = true;

// Turned off in settings.
sandbox.settings.shortcuts = false;
eq(send(';;hi'), { text: ';;hi' });
sandbox.settings.shortcuts = true;

// --- Send rate limit -------------------------------------------------------

later();
const rateStart = mock.sent.length;
for (const chat of [mock.chats[0], mock.chats[1], mock.chats[2]]) {
  sandbox.dispatch('beforeSend', { text: '/q hi', chat, peerId: chat.id, replyTo: 0 });
  await mock.tick();
  sandbox.dispatch('beforeSend', { text: '/q busy', chat, peerId: chat.id, replyTo: 0 });
  await mock.tick();
}
eq(mock.sent.length - rateStart, 5, 'no more than 5 sends in 10 seconds');
assert(mock.toasts.some((t) => t.includes('Слишком часто')), 'rate limit explained');

// --- Message menu ------------------------------------------------------------

const address = mock.message({ id: 42, text: 'Мой адрес: ул. Ленина, д. 5', fromName: 'Вася' });
assert(mock.menu('message', address).some((i) => i.title.includes('Сохранить как быстрый ответ')));
assert(!mock.menu('message', mock.message({ text: '' })).some((i) => i.title.includes('быстрый ответ')), 'hidden without text');
assert(!mock.menu('message', mock.message({ text: 'x', service: true })).some((i) => i.title.includes('быстрый ответ')), 'hidden for service');
mock.promptAnswers.push('Адрес ;;addr');
mock.menuClick('message', address, 'Сохранить как быстрый ответ');
await mock.tick();
eq(mock.prompts[mock.prompts.length - 1].options.value, 'Мой адрес ул Ленина', 'name suggested');
eq(byName('Адрес').text, 'Мой адрес: ул. Ленина, д. 5');
eq(byName('Адрес').shortcut, 'addr');
later();
eq(send(';;addr'), { text: 'Мой адрес: ул. Ленина, д. 5' });

// Same name: asks before replacing; the shortcut stays.
mock.promptAnswers.push('адрес');
mock.confirmAnswer = false;
mock.menuClick('message', mock.message({ text: 'Новый адрес' }), 'Сохранить как быстрый ответ');
await mock.tick();
eq(byName('Адрес').text, 'Мой адрес: ул. Ленина, д. 5', 'kept when declined');
mock.confirmAnswer = true;
mock.promptAnswers.push('адрес');
mock.menuClick('message', mock.message({ text: 'Новый адрес' }), 'Сохранить как быстрый ответ');
await mock.tick();
eq(byName('Адрес').text, 'Новый адрес');
eq(byName('Адрес').shortcut, 'addr');

// Formatting is kept as Markdown.
mock.promptAnswers.push('Срочно');
mock.menuClick('message', mock.message({ text: 'Важно: завтра в 10', entities: [{ type: 'bold', offset: 0, length: 5 }] }), 'Сохранить как быстрый ответ');
await mock.tick();
eq(byName('Срочно').text, '**Важно**: завтра в 10');

// A taken shortcut is dropped with a note.
mock.promptAnswers.push('Ещё привет ;;hi');
mock.menuClick('message', mock.message({ text: 'Здравствуйте!' }), 'Сохранить как быстрый ответ');
await mock.tick();
eq(byName('Ещё привет').shortcut, '');
assert(lastToast().includes('уже у «Приветствие»'), lastToast());

// --- Export and import ------------------------------------------------------

panel = openPanel();
mock.click(panel, 'Экспорт');
const exported = JSON.parse(mock.clipboard);
eq(exported.type, 'damzagram.quick-replies');
eq(exported.snippets.length, snippets().length);
eq(exported.snippets.find((s) => s.name === 'Адрес'), { name: 'Адрес', shortcut: 'addr', text: 'Новый адрес' });
eq(parseImport(mock.clipboard).items.length, snippets().length, 'round trip');

mock.clipboard = JSON.stringify([
  { name: 'Спасибо', text: 'Благодарю!' },
  { name: 'Новый', text: 'Новый текст', shortcut: 'hi' },
  { name: 'Пустой', text: '' },
]);
mock.click(panel, 'Импорт');
await mock.tick();
eq(byName('Спасибо').text, 'Благодарю!', 'updated by name');
eq(byName('Спасибо').shortcut, 'ty', 'shortcut kept');
eq(byName('Новый').shortcut, '', 'taken shortcut dropped');
assert(lastToast().includes('добавлено 1') && lastToast().includes('обновлено 1') && lastToast().includes('пропущено 1'), lastToast());

mock.clipboard = 'не json';
const count = snippets().length;
mock.click(panel, 'Импорт');
await mock.tick();
assert(lastToast().includes('Импорт не удался'), lastToast());
eq(snippets().length, count);
panel.api.close();

eq(parseImport('{"Адрес": "ул. Ленина", "Почта": "a@b.c"}').items.map((i) => i.name), ['Адрес', 'Почта'], 'object form');
eq(parseImport('["Просто текст"]').items[0], { name: 'Просто текст', text: 'Просто текст', shortcut: '' }, 'strings');
let failed = '';
try { parseImport('[]'); } catch (e) { failed = e.message; }
assert(failed.includes('не нашлось'), failed);
const merged = merge([], [{ name: 'A', text: '1', shortcut: 'a' }, { name: 'B', text: '2', shortcut: 'a' }]);
eq(merged.list.map((s) => s.shortcut), ['a', '']);
eq([merged.added, merged.conflicts], [2, 1]);
eq(JSON.parse(exportJson([{ name: 'X', text: 'y', shortcut: '' }])).snippets, [{ name: 'X', text: 'y' }]);

// --- Edit mode: delete, editor delete, restore defaults ---------------------

panel = openPanel();
mock.visible(panel).find((i) => i.type === 'toggle').onChange(true);
mock.type(panel, 'занят');
assert(mock.panelText(panel).includes('✎ Занят'), mock.panelText(panel));
mock.click(panel, 'Удалить');
await mock.tick();
eq(byName('Занят'), undefined, 'deleted after confirm');

mock.type(panel, 'счёт');
mock.boxAnswers.push({ button: 'delete', values: {} });
mock.click(panel, '✎ Счёт');
await mock.tick();
eq(byName('Счёт'), undefined, 'deleted from the editor');
eq(mock.boxes[mock.boxes.length - 1].buttons.map((b) => b.id), ['ok', 'delete', 'cancel']);

mock.type(panel, '');
mock.click(panel, 'Вернуть стандартные (1)');
assert(byName('Занят'), 'default restored');
eq(byName('Занят').shortcut, 'busy');
assert(!mock.findItem(panel, 'button', 'Вернуть стандартные'), 'button gone when nothing is missing');

// Editing keeps the id and usage.
const thanks = byName('Спасибо');
mock.type(panel, 'спасибо');
mock.boxAnswers.push({ button: 'ok', values: { name: 'Спасибо', shortcut: 'ty', text: 'Спасибо огромное, {first}!' } });
mock.click(panel, '✎ Спасибо');
await mock.tick();
eq(byName('Спасибо').id, thanks.id);
eq(byName('Спасибо').uses, thanks.uses);
eq(byName('Спасибо').text, 'Спасибо огромное, {first}!');
panel.api.close();

// --- Settings and unload -----------------------------------------------------

sandbox.settings.composeButton = false;
sandbox.event('settings.changed', { key: 'composeButton', value: false });
assert(mock.composeButtons[0].removed, 'button removed');
sandbox.settings.composeButton = true;
sandbox.event('settings.changed', { key: 'composeButton', value: true });
eq(mock.composeButtons.filter((b) => !b.removed).length, 1, 'button back');

// Defaults are not seeded twice: the flag is kept.
eq(sandbox.storage.seeded, true);

panel = openPanel();
assert(panel.open);
sandbox.dispatch('unload');
assert(!panel.open, 'panel closed on unload');
assert(mock.composeButtons.every((b) => b.removed), 'compose button removed on unload');

Date.now = realNow;
