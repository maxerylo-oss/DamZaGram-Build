import { parseDigest, formatTranscript, collect } from '../../../../plugins/builtin/ai-digest/main.js';

const { eq, assert } = sandbox;

// Parsing of message references.
const parsed = parseDigest('Суть.\n• Тема один [#5]\n• Две ссылки [#7, #9]\n\n**Тебе:**');
eq(parsed.length, 4);
eq(parsed[1], { text: '• Тема один', refs: [5] });
eq(parsed[2].refs, [7, 9]);
eq(parsed[3].text, 'Тебе:');

// History: 250 messages, paged by 100 from the server.
const chat = mock.current;
mock.history[chat.id] = [];
for (let i = 1; i <= 250; ++i) {
  mock.history[chat.id].push(mock.message({ id: i, text: 'сообщение ' + i, fromName: i % 2 ? 'Вася' : 'Петя', date: 1700000000 + i * 60 }));
}
mock.history[chat.id][9].media = 'photo';
const got = await collect(chat, 120);
eq(got.length, 120);
eq(got[0].id, 131);
eq(got[119].id, 250);
const transcript = formatTranscript(mock.history[chat.id].slice(0, 12));
assert(transcript.includes('[#10 ') && transcript.includes('[фото]'), transcript);

// Registration.
assert(dzg.commands.list().some((c) => c.name === 'digest'));
assert(mock.topBar.length === 1 && mock.topBar[0].tooltip.includes('пропустил'), 'top bar button');
assert(mock.menu('chat', chat).some((i) => i.title.includes('Что я пропустил')), 'chat menu item');

// No key: explains how to get one, saving the key starts the digest.
let requests = [];
mock.httpHandler = (url, options) => {
  const body = JSON.parse(options.body);
  requests.push({ url, headers: options.headers, body });
  return {
    status: 200,
    body: {
      model: body.model,
      stop_reason: 'end_turn',
      content: [
        { type: 'thinking', thinking: '' },
        { type: 'text', text: 'Обсуждали релиз.\n• Решили выпустить в пятницу [#240]\nТебе:\n• Петя спросил про билд [#248]' },
      ],
    },
  };
};
mock.command('/digest');
let panel = mock.lastPanel();
assert(mock.panelText(panel).includes('console.anthropic.com'), 'key instructions');
mock.submit(panel, 'sk-ant-test');
await mock.tick(60);
eq(sandbox.settings.apiKey, 'sk-ant-test');
eq(requests.length, 1);
const request = requests[0];
eq(request.url, 'https://api.anthropic.com/v1/messages');
eq(request.headers['x-api-key'], 'sk-ant-test');
eq(request.headers['anthropic-version'], '2023-06-01');
eq(request.headers['content-type'], 'application/json');
eq(request.body.model, 'claude-sonnet-5-5');
eq(request.body.fallbacks, 'default');
eq(request.headers['anthropic-beta'], 'server-side-fallback-2026-07-01');
assert(request.body.messages[0].content.includes('<transcript>'), 'transcript wrapped');
// 3 unread in the mock chat list -> at least 30 messages are read.
assert(request.body.messages[0].content.includes('[#250 '), 'latest message included');
assert(!request.body.messages[0].content.includes('[#200 '), 'smart unread limits the range');

panel = mock.lastPanel();
let text = mock.panelText(panel);
assert(text.includes('Решили выпустить в пятницу') && !text.includes('[#240]'), text);
mock.click(panel, 'к сообщению');
eq(mock.opened[mock.opened.length - 1], { peerId: '100', messageId: 240 });

// /digest 50 reads exactly 50.
requests = [];
mock.command('/digest 50');
await mock.tick(60);
assert(requests[0].body.messages[0].content.includes('[#201 '), 'explicit count');
assert(!requests[0].body.messages[0].content.includes('[#200 '), 'explicit count upper bound');

// A proxy that rejects new fields: retried without them.
requests = [];
mock.httpHandler = (url, options) => {
  const body = JSON.parse(options.body);
  requests.push({ body });
  if (body.fallbacks) return { status: 400, body: { error: { message: 'Unknown field: fallbacks' } } };
  return { status: 200, body: { model: body.model, stop_reason: 'end_turn', content: [{ type: 'text', text: 'Ок' }] } };
};
mock.command('/digest 10');
await mock.tick(60);
eq(requests.length, 2);
assert(!requests[1].body.fallbacks && !requests[1].body.output_config, 'retry without extras');
assert(mock.panelText(mock.lastPanel()).includes('Ок'));

// Errors are explained.
mock.httpHandler = () => ({ status: 401, body: { error: { message: 'invalid x-api-key' } } });
mock.command('/digest 10');
await mock.tick(60);
assert(mock.panelText(mock.lastPanel()).includes('Ключ не подошёл'), mock.panelText(mock.lastPanel()));

// Refusal is reported, not shown as a digest.
mock.httpHandler = () => ({ status: 200, body: { stop_reason: 'refusal', content: [] } });
mock.command('/digest 10');
await mock.tick(60);
assert(mock.panelText(mock.lastPanel()).includes('отказалась'));

// The key never shows up in an error, even if a proxy echoes it back.
const key = String(sandbox.settings.apiKey);
assert(key.length >= 8, 'test key set');
mock.httpHandler = () => ({ status: 500, body: { error: { message: 'bad header x-api-key: ' + key } } });
mock.command('/digest 10');
await mock.tick(60);
const errorText = mock.panelText(mock.lastPanel());
assert(errorText.includes('Ошибка API 500') && !errorText.includes(key), errorText);
assert(!sandbox.calls.some((c) => c.name === 'log' && JSON.stringify(c.args).includes(key)), 'not logged');
