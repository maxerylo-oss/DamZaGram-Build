// api_builtin.cpp JS fragment: dzg.builtin and cross-plugin commands.
//   tools/damza/sandbox.py plugins/examples/hello/main.js tools/damza/tests/builtin-module.test.js
const { eq, assert } = sandbox;

const chat = { id: '100', tgId: 100, name: 'Вася', username: '', type: 'user', isSelf: false };
// dzg.messages.send goes to the core native or to the messages module's one.
const lastSend = () => sandbox.last('messages_send') || sandbox.last('sendMessage');

sandbox.returns.builtin_list = [{ id: 'reminders', name: 'Напоминания', version: '1.0.0', installedVersion: '1.0.0', enabledByDefault: true, replaced: false, updated: true, error: '' }];
eq(dzg.builtin.list()[0].id, 'reminders');

// Own and foreign commands.
sandbox.returns.builtin_commands = [{ plugin: 'ai-digest', pluginName: 'ИИ-дайджест', name: 'digest', description: 'пересказ' }];
const all = dzg.commands.all();
const own = all.find((c) => c.name === 'hello');
eq(own.plugin, 'sandbox');
eq(own.pluginName, 'Sandbox');
assert(all.some((c) => c.name === 'digest' && c.plugin === 'ai-digest'), 'foreign commands');

// Running an own command does not go through the native side.
sandbox.reset();
assert(dzg.commands.run('/hello Петя', chat), 'own command ran');
assert(sandbox.called('builtin_runCommand').length === 0);
const sent = lastSend();
assert(sent && sent.args[0] === '100' && sent.args[1].includes('Петя'), JSON.stringify(sent));

// Foreign commands are dispatched to their plugin.
sandbox.returns.builtin_runCommand = true;
assert(dzg.commands.run('digest 50', chat), 'foreign command ran');
eq(sandbox.last('builtin_runCommand').args, ['ai-digest', '/digest 50', '100']);
assert(!dzg.commands.run('/nope', chat), 'unknown command');

// What other plugins call on this one.
eq(sandbox.dispatch('builtin:commands').map((c) => c.name), ['hello']);
sandbox.reset();
eq(sandbox.dispatch('builtin:run', { text: '/hello Маша', chat, replyTo: 0 }), true);
assert(lastSend().args[1].includes('Маша'));
eq(sandbox.dispatch('builtin:run', { text: '/other', chat }), false);
