// tools/damza/sandbox.py plugins/examples/chat-tools/main.js tools/damza/tests/chat-tools.test.js
const { eq, assert } = sandbox;

const chat = { id: '1', tgId: 1, name: 'Chat', username: '', type: 'group', isSelf: false };
const user = { id: '2', tgId: 2, name: 'Pavel', username: 'Durov', type: 'user', isSelf: false };
const entry = (peer, unread) => Object.assign({ unread, unreadMark: false, mentions: 0, reactions: 0, pinned: false, muted: false, archived: false, lastMessage: null }, peer);

const names = dzg.commands.list().map((c) => c.name).sort();
eq(names, ['accounts', 'readall', 'typing', 'unread', 'whois']);

// /unread builds an alert from list() for every folder.
sandbox.native.chats_list = (options) => (options.folder === 0 ? [entry(chat, 3), entry(user, 2)] : []);
sandbox.returns.chats_folders = [{ id: 5, title: 'Work', emoji: '', chatlist: false }];
eq(sandbox.dispatch('beforeSend', { text: '/unread', chat, peerId: chat.id, replyTo: 0 }), { cancel: true });
const box = sandbox.last('showBox').args[0];
assert(box.text.includes('Все чаты: 2 чата, 5'), box.text);
eq(sandbox.called('chats_list').map((c) => c.args[0].folder), [0, 5, 'archive']);

// /readall channels asks first, then marks every chat read.
sandbox.reset();
sandbox.native.chats_list = () => [entry(chat, 1), entry(user, 4)];
sandbox.native.showBox = () => 9001;
sandbox.dispatch('beforeSend', { text: '/readall channels', chat, peerId: chat.id, replyTo: 0 });
eq(sandbox.last('chats_list').args[0].types, ['channel']);
eq(sandbox.called('showBox').length, 1);
eq(sandbox.called('chats_markUnread').length, 0, 'waits for confirmation');
sandbox.settle(9001, true, { button: 'ok', values: {} });
for (let i = 0; i < 5; ++i) await Promise.resolve();
eq(sandbox.called('chats_markUnread').map((c) => c.args), [['1', false], ['2', false]]);
delete sandbox.native.showBox;

// /typing starts and later cancels the status.
sandbox.reset();
sandbox.dispatch('beforeSend', { text: '/typing 5', chat, peerId: chat.id, replyTo: 0 });
eq(sandbox.last('chats_setTyping').args, ['1', 'record_voice', 0]);

// user.status toast for watched usernames only.
sandbox.settings.watch = '@durov, someone';
sandbox.reset();
sandbox.event('user.status', { user, online: true, lastSeen: 0, kind: 'online' });
eq(sandbox.called('toast').length, 1);
sandbox.event('user.status', { user: Object.assign({}, user, { username: 'other' }), online: true, lastSeen: 0, kind: 'online' });
eq(sandbox.called('toast').length, 1);

// /accounts switches to the next one.
sandbox.returns.accounts_list = [{ index: 0, self: user, active: true, premium: false }, { index: 3, self: chat, active: false, premium: false }];
sandbox.dispatch('beforeSend', { text: '/accounts', chat, peerId: chat.id, replyTo: 0 });
eq(sandbox.last('accounts_switchTo').args, [3]);

print('chat-tools.test.js: all checks passed');
