// Sandbox test for the main.js that "Create plugin" scaffolds
// (template in damza/plugins/plugin_install.cpp). Run it through
// tools/damza/tests/scaffold_test.py, which extracts the template.
const { eq, assert } = sandbox;
sandbox.settings.greeting = 'Привет';
sandbox.settings.mode = 'loud';
sandbox.settings.notify = true;
sandbox.settings.keyword = 'damza';

sandbox.event('ready');
const toast = sandbox.last('toast');
assert(toast && toast.args[0] === 'ПРИВЕТ!', 'ready toast: ' + JSON.stringify(toast && toast.args));

const result = sandbox.dispatch('beforeSend', {
  text: '/my_plugin мир',
  chat: { id: 'u1', name: 'X' },
  peerId: 'u1',
  replyTo: 0,
});
eq(result, { cancel: true }, 'the command is swallowed');
const sent = sandbox.last('messages_send') || sandbox.last('sendMessage');
assert(sent && sent.args[1] === 'ПРИВЕТ! мир', 'command sends: ' + JSON.stringify(sent && sent.args));

sandbox.reset();
sandbox.event('message.new', { out: false, text: 'hello DAMZA', fromName: 'A', chat: { name: 'C' } });
assert(sandbox.last('toast'), 'keyword toast');
sandbox.reset();
sandbox.event('message.new', { out: true, text: 'damza', fromName: 'Me', chat: { name: 'C' } });
assert(!sandbox.last('toast'), 'no toast for outgoing');

const items = sandbox.dispatch('menu:collect', { kind: 'message', target: { text: 'a\nb' } });
eq(items.length, 1, 'one message menu item');
sandbox.dispatch('menu:click', { kind: 'message', id: items[0].id, target: { text: 'a\nb' } });
const copied = sandbox.last('setClipboardText') || sandbox.last('clipboard_set');
assert(copied && copied.args[0] === '> a\n> b', 'quote copied: ' + JSON.stringify(copied && copied.args));
print('scaffold.test.js: all good');
