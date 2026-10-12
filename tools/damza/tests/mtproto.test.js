// tools/damza/sandbox.py tools/damza/tests/mtproto.plugin.js tools/damza/tests/mtproto.test.js
// Covers the JS half of the raw MTProto module (dzg.api), natives mocked.
const { eq, assert } = sandbox;

let nextId = 5000;
sandbox.native.api_call = () => ++nextId;

const rejects = async (promise, check) => {
  try {
    await promise;
  } catch (e) {
    assert(e instanceof Error, 'rejects with Error');
    if (check) check(e);
    return;
  }
  throw new Error('Expected a rejection');
};

// --- shape
for (const name of ['call', 'inputPeer', 'inputUser', 'inputChannel', 'peerId', 'onUpdate', 'schema', 'constructors', 'search', 'encode', 'decode', 'toBase64', 'fromBase64']) {
  eq(typeof dzg.api[name], 'function', name);
}
sandbox.returns.api_layer = 229;
eq(dzg.api.layer, 229);
assert(Object.keys(dzg.api).includes('layer'), 'layer is enumerable');

// --- call: params are JSON, BigInt and binary data are converted
{
  const promise = dzg.api.call('messages.getHistory', {
    peer: { _: 'inputPeerChannel', channel_id: 1234567890123n, access_hash: '-5' },
    offset_id: 0, limit: 20,
    data: new Uint8Array([1, 2, 3, 255]),
    buffer: new Uint8Array([104, 105]).buffer,
  });
  const call = sandbox.last('api_call');
  eq(call.args[0], 'messages.getHistory');
  eq(JSON.parse(call.args[1]), {
    peer: { _: 'inputPeerChannel', channel_id: '1234567890123', access_hash: '-5' },
    offset_id: 0, limit: 20, data: 'AQID/w==', buffer: 'aGk=',
  });
  eq(call.args[2], { timeout: 0, floodSleep: 10, applyResult: true, dc: 0 });
  sandbox.settle(nextId, true, '{"_":"messages.messages","messages":[],"count":"5"}');
  eq(await promise, { _: 'messages.messages', messages: [], count: '5' });
}

// --- call options and default params
{
  const promise = dzg.api.call('help.getConfig', undefined, { timeout: 1500, floodSleep: 0, applyResult: false, dc: 2 });
  const call = sandbox.last('api_call');
  eq(call.args[1], '{}');
  eq(call.args[2], { timeout: 1500, floodSleep: 0, applyResult: false, dc: 2 });
  sandbox.settle(nextId, true, 'true');
  eq(await promise, true);
}

// --- options reach C++ as ints: no NaN / Infinity / negative / fractions
{
  const promise = dzg.api.call('help.getConfig', {}, { timeout: Infinity, floodSleep: -5, dc: 'x' });
  eq(sandbox.last('api_call').args[2], { timeout: 0x7FFFFFFF, floodSleep: 0, applyResult: true, dc: 0 });
  sandbox.settle(nextId, true, 'true');
  await promise;
  const small = dzg.api.call('help.getConfig', {}, { timeout: 0.5, floodSleep: NaN, dc: 2.0 });
  eq(sandbox.last('api_call').args[2], { timeout: 1, floodSleep: 0, applyResult: true, dc: 2 });
  sandbox.settle(nextId, true, 'true');
  await small;
}

// --- RPC errors become Error objects with code / type / seconds
await rejects((() => {
  const p = dzg.api.call('messages.sendReaction', { peer: 'me', msg_id: 1 });
  sandbox.settle(nextId, false, '400: PEER_ID_INVALID');
  return p;
})(), (e) => {
  eq(e.message, '400: PEER_ID_INVALID');
  eq(e.code, 400);
  eq(e.type, 'PEER_ID_INVALID');
  eq(e.seconds, undefined);
});
await rejects((() => {
  const p = dzg.api.call('contacts.resolveUsername', { username: 'durov' });
  sandbox.settle(nextId, false, '420: FLOOD_WAIT_31');
  return p;
})(), (e) => {
  eq(e.code, 420);
  eq(e.type, 'FLOOD_WAIT_31');
  eq(e.seconds, 31);
});
await rejects((() => {
  const p = dzg.api.call('help.getConfig', {}, { timeout: 10 });
  sandbox.settle(nextId, false, 'TIMEOUT');
  return p;
})(), (e) => eq(e.type, 'TIMEOUT'));

// --- synchronous failures (bad method, encoding errors) reject too
sandbox.native.api_call = () => "messages.getHistory: unknown field 'offsetId' in messages.getHistory (did you mean 'offset_id'?)";
await rejects(dzg.api.call('messages.getHistory', { offsetId: 1 }), (e) => {
  assert(e.message.includes("did you mean 'offset_id'"), e.message);
});
sandbox.native.api_call = () => null;
await rejects(dzg.api.call('x'), (e) => eq(e.message, 'Request was not started'));
sandbox.native.api_call = () => ++nextId;

// --- input helpers take Peer objects, messages or ids
{
  sandbox.returns.api_input = '{"_":"inputPeerUser","user_id":"777","access_hash":"-42"}';
  eq(dzg.api.inputPeer({ id: '777', name: 'Pavel' }), { _: 'inputPeerUser', user_id: '777', access_hash: '-42' });
  eq(sandbox.last('api_input').args, ['InputPeer', '777']);
  dzg.api.inputUser('777');
  eq(sandbox.last('api_input').args, ['InputUser', '777']);
  dzg.api.inputChannel({ peerId: '-1001', id: 5 });
  eq(sandbox.last('api_input').args, ['InputChannel', '-1001']);
  sandbox.returns.api_input = null;
  eq(dzg.api.inputPeer('404'), null);
}

// --- TL peers -> DamZaGram peer ids
{
  eq(dzg.api.peerId({ _: 'peerUser', user_id: '777000' }), '777000');
  eq(dzg.api.peerId({ _: 'peerChat', chat_id: '5' }), String((1n << 48n) | 5n));
  eq(dzg.api.peerId({ _: 'peerChannel', channel_id: '1234567890' }), String((2n << 48n) | 1234567890n));
  eq(dzg.api.peerId({ _: 'channel', id: '1', title: 'x' }), String((2n << 48n) | 1n));
  eq(dzg.api.peerId({ _: 'inputPeerUser', user_id: '9', access_hash: '1' }), '9');
  eq(dzg.api.peerId({ _: 'inputPeerSelf' }), null);
  eq(dzg.api.peerId({ _: 'peerUser', user_id: 'oops' }), null);
  eq(dzg.api.peerId(null), null);
}

// --- schema helpers
{
  const info = { kind: 'method', name: 'messages.getHistory', id: 1143203525, idHex: '4423e6c5', params: [{ name: 'peer', type: 'InputPeer', optional: false }], type: 'messages.Messages' };
  sandbox.returns.api_schema = info;
  eq(dzg.api.schema('messages.getHistory'), info);
  sandbox.returns.api_schema = null;
  eq(dzg.api.schema('nope'), null);
  sandbox.returns.api_constructors = ['inputPeerEmpty', 'inputPeerSelf'];
  eq(dzg.api.constructors('InputPeer'), ['inputPeerEmpty', 'inputPeerSelf']);
  sandbox.returns.api_search = ['messages.getHistory'];
  eq(dzg.api.search('gethistory'), ['messages.getHistory']);
  eq(sandbox.last('api_search').args, ['gethistory', 50]);
}

// --- encode / decode
{
  sandbox.returns.api_encode = { data: 'c+d3fQ==' };
  eq(dzg.api.encode({ _: 'inputPeerSelf' }), 'c+d3fQ==');
  eq(sandbox.last('api_encode').args, ['{"_":"inputPeerSelf"}', 'Object']);
  sandbox.returns.api_encode = { error: "unknown constructor 'x'" };
  let thrown = null;
  try { dzg.api.encode({ _: 'x' }, 'InputPeer'); } catch (e) { thrown = e; }
  eq(thrown && thrown.message, "unknown constructor 'x'");

  sandbox.returns.api_decode = { json: '{"_":"inputPeerSelf"}', size: 4 };
  eq(dzg.api.decode('c+d3fQ=='), { _: 'inputPeerSelf' });
  dzg.api.decode(new Uint8Array([0x73, 0xe7, 0x77, 0x7d]), 'InputPeer');
  eq(sandbox.last('api_decode').args, ['c+d3fQ==', 'InputPeer']);
  sandbox.returns.api_decode = { error: 'unexpected end of data' };
  thrown = null;
  try { dzg.api.decode('AA=='); } catch (e) { thrown = e; }
  eq(thrown && thrown.message, 'unexpected end of data');
}

// --- base64 helpers
{
  eq(dzg.api.toBase64('Привет, 😀!'), '0J/RgNC40LLQtdGCLCDwn5iAIQ==');
  eq(dzg.api.fromBase64('0J/RgNC40LLQtdGCLCDwn5iAIQ==', 'text'), 'Привет, 😀!');
  eq(Array.from(dzg.api.fromBase64('AQID/w==')), [1, 2, 3, 255]);
  eq(Array.from(dzg.api.fromBase64('AQID_w')), [1, 2, 3, 255]);
  eq(dzg.api.toBase64([1, 2, 3, 255]), 'AQID/w==');
  eq(dzg.api.toBase64(new Uint8Array([]).buffer), '');
  eq(dzg.api.toBase64('a'), 'YQ==');
}

// --- onUpdate validates types and keeps the native subscription in sync
{
  sandbox.native.api_schema = (name) => {
    if (name === 'updateUserTyping' || name === 'updateChatUserTyping' || name === 'updateNewMessage') {
      return { kind: 'constructor', name, type: 'Update' };
    } else if (name === 'updateShortMessage') {
      return { kind: 'constructor', name, type: 'Updates' };
    }
    return null;
  };
  let thrown = null;
  try { dzg.api.onUpdate('updateUserTypin', () => {}); } catch (e) { thrown = e; }
  assert(thrown instanceof TypeError && thrown.message.includes('Unknown update type'), 'unknown type');
  thrown = null;
  try { dzg.api.onUpdate('updateShortMessage', () => {}); } catch (e) { thrown = e; }
  assert(thrown instanceof TypeError && thrown.message.includes('not Update'), 'Updates is not Update');
  thrown = null;
  try { dzg.api.onUpdate('*'); } catch (e) { thrown = e; }
  assert(thrown instanceof TypeError, 'callback required');

  const typing = [];
  const offTyping = mtprotoPlugin.watchTyping((x) => typing.push(x));
  eq(sandbox.last('api_setUpdateTypes').args[0], ['updateUserTyping', 'updateChatUserTyping']);
  const all = [];
  const offAll = dzg.api.onUpdate('*', (u) => all.push(u._));
  eq(sandbox.last('api_setUpdateTypes').args[0], ['updateUserTyping', 'updateChatUserTyping', '*']);
  const before = sandbox.called('api_setUpdateTypes').length;
  const offSame = dzg.api.onUpdate('updateUserTyping', () => {});
  eq(sandbox.called('api_setUpdateTypes').length, before, 'no resync for a known type');
  offSame();
  eq(sandbox.called('api_setUpdateTypes').length, before, 'still one listener left');

  sandbox.dispatch('api:updates', {
    self: '100500',
    json: JSON.stringify([
      { _: 'updateUserTyping', user_id: '777', action: { _: 'sendMessageTypingAction' } },
      { _: 'updateNewMessage', message: { _: 'message', id: 1 }, pts: 1, pts_count: 1 },
      { _: 'updateChatUserTyping', chat_id: '5', from_id: { _: 'peerUser', user_id: '7' }, action: { _: 'sendMessageCancelAction' } },
    ]),
  });
  eq(typing, [
    { type: 'updateUserTyping', action: 'sendMessageTypingAction', self: '100500' },
    { type: 'updateChatUserTyping', action: 'sendMessageCancelAction', self: '100500' },
  ]);
  eq(all, ['updateUserTyping', 'updateNewMessage', 'updateChatUserTyping']);

  // A throwing handler doesn't break the others.
  const offBad = dzg.api.onUpdate('updateNewMessage', () => { throw new Error('plugin bug'); });
  sandbox.dispatch('api:updates', { self: '1', json: '[{"_":"updateNewMessage","message":{"_":"messageEmpty","id":2},"pts":2,"pts_count":1}]' });
  eq(all.length, 4);
  offBad();

  offTyping();
  offTyping();
  eq(sandbox.last('api_setUpdateTypes').args[0], ['*']);
  offAll();
  eq(sandbox.last('api_setUpdateTypes').args[0], []);
  sandbox.dispatch('api:updates', { self: '1', json: '[{"_":"updateUserTyping","user_id":"1","action":{"_":"sendMessageTypingAction"}}]' });
  eq(typing.length, 2);
  sandbox.dispatch('api:updates', { self: '1', json: 'not json' });
}

// --- the plugin's own helper on top of call()
{
  const promise = mtprotoPlugin.lastMessages({ id: '777' }, 2);
  eq(JSON.parse(sandbox.last('api_call').args[1]).peer, { id: '777' });
  sandbox.settle(nextId, true, JSON.stringify({
    _: 'messages.messages',
    messages: [
      { _: 'message', id: 2, message: 'hello' },
      { _: 'messageService', id: 1 },
    ],
    topics: [], chats: [], users: [],
  }));
  eq(await promise, ['hello']);
}

print('mtproto.test.js: all checks passed');
