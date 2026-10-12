// Test plugin for chats.test.js: records chats/users events.
globalThis.chatsEvents = [];
for (const name of ['chat.typing', 'chat.unread', 'user.status']) {
  dzg.on(name, (data) => globalThis.chatsEvents.push({ name, data }));
}
