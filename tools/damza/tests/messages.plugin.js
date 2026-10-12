// Test plugin for messages.test.js: records message.reaction events.
globalThis.messagesEvents = [];
dzg.on('message.reaction', (data) => globalThis.messagesEvents.push(data));
