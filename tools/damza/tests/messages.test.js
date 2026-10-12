// tools/damza/sandbox.py tools/damza/tests/messages.plugin.js tools/damza/tests/messages.test.js
// Covers the JS half of the messages module: text parsing/formatting,
// option normalization, pagination and promise plumbing.
const { eq, assert } = sandbox;

const chat = { id: '-1001', tgId: 1001, name: 'Chat', username: 'chat', type: 'group', isSelf: false };
const msg = (id, date) => ({ id, peerId: '-1001', date: date || id, text: 'm' + id, entities: [] });

let nextId = 5000;
const asyncNatives = [
  'messages_history', 'messages_search', 'messages_fetch', 'messages_forward',
  'messages_react', 'messages_pin', 'messages_download', 'messages_clickButton',
];
for (const name of asyncNatives) {
  sandbox.native[name] = () => ++nextId;
}

const tick = () => new Promise((resolve) => queueMicrotask(resolve));
const flush = async () => {
  for (let i = 0; i < 10; ++i) await tick();
};
const rejects = async (promise, text) => {
  try {
    await promise;
  } catch (e) {
    assert(e instanceof Error, 'rejects with Error');
    if (text) assert(String(e.message).includes(text), 'message ' + e.message);
    return;
  }
  throw new Error('Expected a rejection');
};

// --- parse: markdown (the client's syntax + Bot API extras)
{
  const r = dzg.messages.parse('Hi **bold __both__** `c` [l](https://a.b/x_(y)) ||s|| ~~x~~');
  eq(r.text, 'Hi bold both c l s x');
  eq(r.entities, [
    { type: 'bold', offset: 3, length: 9 },
    { type: 'italic', offset: 8, length: 4 },
    { type: 'code', offset: 13, length: 1 },
    { type: 'textLink', offset: 15, length: 1, url: 'https://a.b/x_(y)' },
    { type: 'spoiler', offset: 17, length: 1 },
    { type: 'strikethrough', offset: 19, length: 1 },
  ]);
}
{
  const r = dzg.messages.parse('```python\nprint(1)\n```\nok');
  eq(r.text, 'print(1)\nok');
  eq(r.entities, [{ type: 'pre', offset: 0, length: 8, language: 'python' }]);
}
{
  const r = dzg.messages.parse('a\n> q **b**\n> c\nz');
  eq(r.text, 'a\nq b\nc\nz');
  eq(r.entities, [{ type: 'blockquote', offset: 2, length: 5 }, { type: 'bold', offset: 4, length: 1 }]);
  const x = dzg.messages.parse('**>hidden\n>more||');
  eq(x.entities, [{ type: 'blockquote', offset: 0, length: 11, collapsed: true }]);
}
{
  const r = dzg.messages.parse('\\*\\*no\\*\\* snake_case 2*3 http://x.com/__init__.py');
  eq(r.text, '**no** snake_case 2*3 http://x.com/__init__.py');
  eq(r.entities, []);
  const m = dzg.messages.parse('[Pavel](tg://user?id=42) ![e](tg://emoji?id=99)');
  eq(m.entities, [
    { type: 'textMention', offset: 0, length: 5, userId: '42' },
    { type: 'customEmoji', offset: 6, length: 1, documentId: '99' },
  ]);
}
eq(dzg.messages.parse('plain **x**', 'none'), { text: 'plain **x**', entities: [] });
let threw = false;
try { dzg.messages.parse('x', 'bbcode'); } catch (e) { threw = e instanceof TypeError; }
assert(threw, 'unknown parse mode throws');

// --- parse: html
{
  const r = dzg.messages.parse('<b>b <i>i</i></b> &lt;3&amp; <a href="https://x.y?a=1&amp;b=2">x</a><br>'
    + '<pre><code class="language-js">1</code></pre><span class="tg-spoiler">s</span>'
    + '<tg-spoiler>t</tg-spoiler><u>u</u><del>d</del><code>c</code>'
    + '<blockquote expandable>q</blockquote><tg-emoji emoji-id="5">e</tg-emoji><a href="tg://user?id=7">m</a><unknown>k</unknown>', 'html');
  eq(r.text, 'b i <3& x\n1stuDcqemk'.replace('D', 'd'));
  eq(r.entities.map((e) => e.type), [
    'bold', 'italic', 'textLink', 'pre', 'spoiler', 'spoiler', 'underline',
    'strikethrough', 'code', 'blockquote', 'customEmoji', 'textMention',
  ]);
  eq(r.entities[2].url, 'https://x.y?a=1&b=2');
  eq(r.entities[3].language, 'js');
  assert(r.entities[9].collapsed === true, 'expandable blockquote');
  eq(r.entities[11].userId, '7');
}

// --- format: round trips
{
  const source = { text: 'Hi bold & <tag> a**b > x\nq1\nq2', entities: [
    { type: 'bold', offset: 3, length: 4 },
    { type: 'italic', offset: 5, length: 2 },
    { type: 'textLink', offset: 8, length: 1, url: 'https://e.com/(1)' },
    { type: 'blockquote', offset: 25, length: 5 },
    { type: 'hashtag', offset: 0, length: 2 },
  ] };
  const html = dzg.messages.format(source, 'html');
  eq(html, 'Hi <b>bo<i>ld</i></b> <a href="https://e.com/(1)">&amp;</a> &lt;tag&gt; a**b &gt; x\n<blockquote>q1\nq2</blockquote>');
  const back = dzg.messages.parse(html, 'html');
  eq(back.text, source.text);
  eq(back.entities.length, 4);
  const md = dzg.messages.format(source, 'markdown');
  const mdBack = dzg.messages.parse(md);
  eq(mdBack.text, source.text);
  eq(mdBack.entities, back.entities);
  // Overlapping entities are split, not broken.
  eq(dzg.messages.format('abcdef', 'html', [
    { type: 'bold', offset: 0, length: 4 },
    { type: 'italic', offset: 2, length: 4 },
  ]), '<b>ab<i>cd</i></b><i>ef</i>');
  eq(dzg.messages.format('x', 'md', []), 'x');
}

// --- send: parse modes and options
sandbox.returns.messages_send = true;
assert(dzg.messages.send(chat, 'Hello **world**', { parse: 'markdown', replyTo: { id: 12 }, silent: 1 }) === true);
{
  const [peerId, text, entities, options] = sandbox.last('messages_send').args;
  eq(peerId, '-1001');
  eq(text, 'Hello world');
  eq(entities, [{ type: 'bold', offset: 6, length: 5 }]);
  eq(options, { replyTo: 12, topicId: 0, silent: true, runHooks: false, scheduleDate: 0, noWebpage: false });
}
dzg.messages.send('-1001', '<i>x</i>', { parse: 'html', scheduleDate: new Date(2000000000 * 1000), noWebpage: true, runHooks: true });
{
  const [, text, entities, options] = sandbox.last('messages_send').args;
  eq(text, 'x');
  eq(entities, [{ type: 'italic', offset: 0, length: 1 }]);
  eq(options.scheduleDate, 2000000000);
  assert(options.noWebpage && options.runHooks, 'flags passed');
}
dzg.messages.send('-1001', 'raw **x**', { scheduleDate: 2000000000123, entities: [{ type: 'bold', offset: 0, length: 3 }] });
eq(sandbox.last('messages_send').args[1], 'raw **x**');
eq(sandbox.last('messages_send').args[2], [{ type: 'bold', offset: 0, length: 3 }]);
eq(sandbox.last('messages_send').args[3].scheduleDate, 2000000000);
sandbox.returns.messages_send = null;
eq(dzg.messages.send(chat, 'x'), false);

// --- edit keeps the boolean result, gains parse
sandbox.returns.messages_edit = true;
eq(dzg.messages.edit(chat, 5, '__it__', { parse: 'md', noWebpage: true }), true);
eq(sandbox.last('messages_edit').args, ['-1001', 5, 'it', [{ type: 'italic', offset: 0, length: 2 }], { noWebpage: true }]);

// --- sync helpers
sandbox.returns.messages_link = 'https://t.me/chat/5';
eq(dzg.messages.link(chat, 5), 'https://t.me/chat/5');
eq(sandbox.last('messages_link').args, ['-1001', 5, { thread: false, private: false }]);
sandbox.returns.messages_link = null;
eq(dzg.messages.link(chat, 5, { private: true }), null);
sandbox.returns.messages_markRead = true;
eq(dzg.messages.markRead(chat), true);
eq(sandbox.last('messages_markRead').args, ['-1001', { tillId: 0 }]);
sandbox.returns.messages_availableReactions = ['👍', '❤'];
eq(dzg.messages.reactions(chat, 1), ['👍', '❤']);
sandbox.returns.messages_availableReactions = null;
eq(dzg.messages.reactions(chat, 1), []);

sandbox.returns.messages_sendFile = true;
eq(dzg.messages.sendFile(chat, 'a.png', { caption: '**hi**', parse: 'markdown', asDocument: true }), true);
{
  const [peerId, paths, options] = sandbox.last('messages_sendFile').args;
  eq(peerId, '-1001');
  eq(paths, ['a.png']);
  eq(options.caption, 'hi');
  eq(options.captionEntities, [{ type: 'bold', offset: 0, length: 2 }]);
  assert(options.asDocument === true && options.album === true && options.spoiler === false, 'file flags');
}
dzg.messages.sendFile(chat, ['a.png', 'b.jpg', ''], { album: false, spoiler: true });
eq(sandbox.last('messages_sendFile').args[1], ['a.png', 'b.jpg']);
assert(sandbox.last('messages_sendFile').args[2].album === false, 'album off');

// --- async: promise plumbing
(async () => {
  // history: one page, oldest first comes straight from native.
  const p1 = dzg.messages.history(chat, { limit: 3 });
  const h1 = sandbox.last('messages_history');
  eq(h1.args[0], '-1001');
  eq(h1.args[1], { limit: 3, offsetId: 0, addOffset: 0, offsetDate: 0, minId: 0, maxId: 0 });
  await flush();
  sandbox.settle(nextId, true, { messages: [msg(1), msg(2), msg(3)], count: 10, nextOffsetId: 1, nextOffsetPeerId: '-1001' });
  eq((await p1).map((m) => m.id), [1, 2, 3]);

  // history: 150 messages -> two pages, merged oldest first.
  const p2 = dzg.messages.history(chat, { limit: 150 });
  eq(sandbox.last('messages_history').args[1].limit, 100);
  await flush();
  const first = [];
  for (let i = 51; i <= 150; ++i) first.push(msg(i));
  sandbox.settle(nextId, true, { messages: first, count: 1000, nextOffsetId: 51, nextOffsetPeerId: '-1001' });
  await flush();
  const second = sandbox.last('messages_history').args[1];
  eq([second.limit, second.offsetId], [50, 51]);
  const older = [];
  for (let i = 1; i <= 50; ++i) older.push(msg(i));
  sandbox.settle(nextId, true, { messages: older, count: 1000, nextOffsetId: 1, nextOffsetPeerId: '-1001' });
  const all = await p2;
  eq(all.length, 150);
  eq([all[0].id, all[149].id], [1, 150]);

  // history: rejects with the server error.
  const p3 = dzg.messages.history('-1001');
  await flush();
  sandbox.settle(nextId, false, 'CHANNEL_PRIVATE');
  await rejects(p3, 'CHANNEL_PRIVATE');

  // search: per chat and global, filters and paging cursor.
  const s1 = dzg.messages.search(chat, 'hello', { limit: 2, from: { id: '42' }, filter: 'photo' });
  const s1args = sandbox.last('messages_search').args;
  eq([s1args[0], s1args[1], s1args[2].from, s1args[2].filter, s1args[2].limit], ['-1001', 'hello', '42', 'photo', 2]);
  await flush();
  sandbox.settle(nextId, true, { messages: [msg(7), msg(9)], count: 2, nextOffsetId: 7, nextOffsetPeerId: '-1001' });
  eq((await s1).map((m) => m.id), [7, 9]);

  const s2 = dzg.messages.search(null, 'q', { limit: 150, type: 'channels' });
  eq(sandbox.last('messages_search').args[0], '');
  eq(sandbox.last('messages_search').args[2].type, 'channels');
  eq(sandbox.last('messages_search').args[2].folder, -1);
  await flush();
  sandbox.settle(nextId, true, { messages: [msg(20, 200)], count: 0, nextRate: 77, nextOffsetId: 20, nextOffsetPeerId: '-5' });
  await flush();
  const cursor = sandbox.last('messages_search').args[2];
  eq([cursor.offsetRate, cursor.offsetPeerId, cursor.offsetId], [77, '-5', 20]);
  sandbox.settle(nextId, true, { messages: [], count: 0 });
  eq((await s2).length, 1);

  // iterate: newest first, stops at limit.
  const seen = [];
  const iterator = dzg.messages.iterate(chat, { limit: 3, batch: 2 });
  const consume = (async () => {
    for await (const m of iterator) seen.push(m.id);
  })();
  await flush();
  eq(sandbox.last('messages_history').args[1].limit, 2);
  sandbox.settle(nextId, true, { messages: [msg(9), msg(10)], count: 10, nextOffsetId: 9 });
  await flush();
  eq(sandbox.last('messages_history').args[1].offsetId, 9);
  eq(sandbox.last('messages_history').args[1].limit, 1);
  sandbox.settle(nextId, true, { messages: [msg(8)], count: 10, nextOffsetId: 8 });
  await consume;
  eq(seen, [10, 9, 8]);

  // fetch: single id -> message or null, array -> list.
  const f1 = dzg.messages.fetch(chat, 5);
  eq(sandbox.last('messages_fetch').args, ['-1001', [5]]);
  await flush();
  sandbox.settle(nextId, true, []);
  eq(await f1, null);
  const f2 = dzg.messages.fetch(chat, [5, { id: 6 }]);
  eq(sandbox.last('messages_fetch').args[1], [5, 6]);
  await flush();
  sandbox.settle(nextId, true, [msg(5), msg(6)]);
  eq((await f2).length, 2);

  // forward / react / pin resolve with undefined.
  const fw = dzg.messages.forward(chat, [1, msg(2)], '777', { dropAuthor: true, silent: true });
  eq(sandbox.last('messages_forward').args, ['-1001', [1, 2], '777', { silent: true, dropAuthor: true, dropCaptions: false, scheduleDate: 0, topicId: 0 }]);
  await flush();
  sandbox.settle(nextId, true, null);
  eq(await fw, undefined);

  const r1 = dzg.messages.react(chat, 3, '👍');
  eq(sandbox.last('messages_react').args, ['-1001', 3, '👍', { addToRecent: false }]);
  await flush();
  sandbox.settle(nextId, false, 'REACTION_INVALID');
  await rejects(r1, 'REACTION_INVALID');
  const r2 = dzg.messages.react(chat, 3, null);
  eq(sandbox.last('messages_react').args[2], '');
  await flush();
  sandbox.settle(nextId, true, null);
  eq(await r2, undefined);

  const pn = dzg.messages.unpin(chat, 4);
  eq(sandbox.last('messages_pin').args, ['-1001', 4, { silent: false, unpin: true, oneSide: false }]);
  await flush();
  sandbox.settle(nextId, true, null);
  await pn;

  // download resolves with the path.
  const dl = dzg.messages.download(chat, 8);
  eq(sandbox.last('messages_download').args, ['-1001', 8, '']);
  await flush();
  sandbox.settle(nextId, true, '/tmp/file.png');
  eq(await dl, '/tmp/file.png');

  // clickButton: by position and by text.
  const c1 = dzg.messages.clickButton(chat, 9, 1, 0);
  eq(sandbox.last('messages_clickButton').args, ['-1001', 9, 1, 0, { text: '', open: false }]);
  await flush();
  sandbox.settle(nextId, true, { answer: 'Done' });
  eq(await c1, { answer: 'Done' });
  const c2 = dzg.messages.clickButton(chat, 9, 'Buy', { open: true });
  eq(sandbox.last('messages_clickButton').args, ['-1001', 9, -1, -1, { text: 'Buy', open: true }]);
  await flush();
  sandbox.settle(nextId, false, 'BUTTON_NOT_FOUND');
  await rejects(c2, 'BUTTON_NOT_FOUND');

  // message.reaction is a regular dzg.on event.
  sandbox.event('message.reaction', msg(3));
  eq(globalThis.messagesEvents.length, 1);

  print('messages.test.js: all checks passed');
})().catch((e) => {
  print('FAILED: ' + e + '\n' + e.stack);
  throw e;
});
