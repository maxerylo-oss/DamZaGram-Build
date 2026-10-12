// Plugin used by mtproto.test.js: a tiny real-world use of dzg.api.
//   tools/damza/sandbox.py tools/damza/tests/mtproto.plugin.js tools/damza/tests/mtproto.test.js

export async function lastMessages(peer, limit = 10) {
  const result = await dzg.api.call('messages.getHistory', {
    peer,
    offset_id: 0,
    offset_date: 0,
    add_offset: 0,
    limit,
    max_id: 0,
    min_id: 0,
  });
  return result.messages.filter((m) => m._ === 'message').map((m) => m.message);
}

export function watchTyping(onTyping) {
  return dzg.api.onUpdate(['updateUserTyping', 'updateChatUserTyping'], (update, meta) => {
    onTyping({ type: update._, action: update.action._, self: meta.self });
  });
}

globalThis.mtprotoPlugin = { lastMessages, watchTyping };
