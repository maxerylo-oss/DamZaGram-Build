// tools/damza/sandbox.py tools/damza/tests/chats.plugin.js tools/damza/tests/chats.test.js
// Covers the JS half of the chats / users / accounts modules.
const { eq, assert } = sandbox;

const peer = { id: '777', tgId: 777, name: 'Pavel', username: 'durov', type: 'user', isSelf: false };
const group = { id: '-100', tgId: 100, name: 'Group', username: '', type: 'group', isSelf: false };

let nextId = 1000;
const asyncNative = (name) => {
  sandbox.native[name] = () => ++nextId;
};
for (const name of ['chats_resolve', 'chats_join', 'chats_leave', 'chats_members', 'chats_info', 'users_info', 'users_block']) {
  asyncNative(name);
}

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

// --- dzg.chats.list / folders / entry
sandbox.returns.chats_list = [Object.assign({ unread: 2, mentions: 0, pinned: true, muted: false, archived: false, lastMessage: null }, peer)];
const list = dzg.chats.list();
eq(list.length, 1);
eq(sandbox.last('chats_list').args[0], { folder: 0, limit: 100, unreadOnly: false, types: [] });
dzg.chats.list({ folder: 'archive', limit: 5, unreadOnly: 1, type: 'group' });
eq(sandbox.last('chats_list').args[0], { folder: 'archive', limit: 5, unreadOnly: true, types: ['group'] });
dzg.chats.list({ folder: 3, limit: 0, type: ['user', 'bot'] });
eq(sandbox.last('chats_list').args[0], { folder: 3, limit: 0, unreadOnly: false, types: ['user', 'bot'] });
sandbox.returns.chats_list = null;
eq(dzg.chats.list(), []);

sandbox.returns.chats_folders = [{ id: 2, title: 'Work', emoji: '', chatlist: false }];
eq(dzg.chats.folders()[0].title, 'Work');
sandbox.returns.chats_folders = null;
eq(dzg.chats.folders(), []);

eq(dzg.chats.entry(peer), null);
eq(sandbox.last('chats_entry').args, ['777']);

// --- mute / archive / pin / markUnread
sandbox.returns.chats_mute = true;
const muteCases = [[undefined, 0], [true, 0], [0, 0], [false, -1], [3600, 3600], [1.4, 1], [-5, 0], ['60', 60]];
for (const [input, expected] of muteCases) {
  assert(dzg.chats.mute(peer, input) === true, 'mute returns boolean');
  eq(sandbox.last('chats_mute').args, ['777', expected], 'mute ' + String(input));
}
dzg.chats.unmute('777');
eq(sandbox.last('chats_mute').args, ['777', -1]);

sandbox.returns.chats_archive = null;
eq(dzg.chats.archive(peer), false);
eq(sandbox.last('chats_archive').args, ['777', true]);
dzg.chats.archive(peer, false);
eq(sandbox.last('chats_archive').args, ['777', false]);

dzg.chats.pin(peer, true);
eq(sandbox.last('chats_pin').args, ['777', true, 0]);
dzg.chats.pin(peer, false, { folder: 4 });
eq(sandbox.last('chats_pin').args, ['777', false, 4]);

dzg.chats.markUnread(peer, true);
eq(sandbox.last('chats_markUnread').args, ['777', true]);
dzg.chats.markRead(peer);
eq(sandbox.last('chats_markUnread').args, ['777', false]);

// --- typing
dzg.chats.setTyping(peer);
eq(sandbox.last('chats_setTyping').args, ['777', 'typing', 0]);
dzg.chats.setTyping(group, 'record_voice', { topicId: 12 });
eq(sandbox.last('chats_setTyping').args, ['-100', 'record_voice', 12]);
dzg.chats.setTyping(group, 'cancel');
eq(sandbox.last('chats_setTyping').args, ['-100', 'cancel', 0]);
let threw = false;
try {
  dzg.chats.setTyping(peer, 'dancing');
} catch (e) {
  threw = e instanceof TypeError;
}
assert(threw, 'unknown typing action throws TypeError');
assert(dzg.chats.typingActions.includes('choose_sticker'), 'typingActions');

// --- async: resolve / join / leave / members / info
{
  const promise = dzg.chats.resolve('@durov');
  eq(sandbox.last('chats_resolve').args, ['@durov']);
  sandbox.settle(nextId, true, peer);
  eq((await promise).id, '777');
}
{
  const promise = dzg.chats.resolve('nobody_here');
  sandbox.settle(nextId, true, null);
  eq(await promise, null);
}
{
  const promise = dzg.chats.resolve('x');
  sandbox.settle(nextId, false, 'FLOOD_WAIT_10');
  await rejects(promise, 'FLOOD_WAIT_10');
}
{
  const promise = dzg.chats.join('https://t.me/+AbCdEf123');
  eq(sandbox.last('chats_join').args, ['https://t.me/+AbCdEf123']);
  sandbox.settle(nextId, true, group);
  eq((await promise).id, '-100');
  dzg.chats.join(group);
  eq(sandbox.last('chats_join').args, ['-100']);
}
{
  const promise = dzg.chats.leave(group);
  eq(sandbox.last('chats_leave').args, ['-100']);
  sandbox.settle(nextId, true, null);
  eq(await promise, undefined);
}
{
  const promise = dzg.chats.members(group);
  eq(sandbox.last('chats_members').args, ['-100', { limit: 200, query: '' }]);
  sandbox.settle(nextId, true, [Object.assign({ role: 'creator' }, peer)]);
  eq((await promise)[0].role, 'creator');
  dzg.chats.members(group, { limit: 10, query: 'pav' });
  eq(sandbox.last('chats_members').args, ['-100', { limit: 10, query: 'pav' }]);
}
{
  const promise = dzg.chats.info(group);
  sandbox.settle(nextId, true, { peer: group, about: 'hi', membersCount: 3, onlineCount: 1, inviteLink: '', linkedChat: null, slowmodeSeconds: 0 });
  eq((await promise).membersCount, 3);
}
{
  sandbox.native.chats_info = () => null;
  await rejects(dzg.chats.info(group), 'not started');
  asyncNative('chats_info');
}

// --- extras: create / addMembers / kick / setTitle / setAbout / invite link / clearHistory
for (const name of ['chats_create', 'chats_addMembers', 'chats_kick', 'chats_setTitle', 'chats_setAbout', 'chats_createInviteLink', 'users_commonChats', 'users_addContact', 'users_deleteContact']) {
  asyncNative(name);
}
{
  const promise = dzg.chats.create({ title: 'Team', type: 'supergroup', users: [peer, '888'], forum: true });
  eq(sandbox.last('chats_create').args[0], { title: 'Team', type: 'supergroup', about: '', users: ['777', '888'], forum: true });
  sandbox.settle(nextId, true, group);
  eq((await promise).id, '-100');
  dzg.chats.create('Just a title');
  eq(sandbox.last('chats_create').args[0], { title: 'Just a title', type: 'group', about: '', users: [], forum: false });
}
{
  const promise = dzg.chats.addMembers(group, [peer, '888', null]);
  eq(sandbox.last('chats_addMembers').args, ['-100', ['777', '888']]);
  sandbox.settle(nextId, true, { added: 1, missing: ['888'] });
  eq((await promise).missing, ['888']);
  dzg.chats.addMembers(group, peer);
  eq(sandbox.last('chats_addMembers').args, ['-100', ['777']]);
}
{
  const promise = dzg.chats.kick(group, peer);
  eq(sandbox.last('chats_kick').args, ['-100', '777', false]);
  sandbox.settle(nextId, true, null);
  eq(await promise, undefined);
  dzg.chats.kick(group, peer, { ban: true });
  eq(sandbox.last('chats_kick').args, ['-100', '777', true]);
}
{
  const promise = dzg.chats.setTitle(group, 'New');
  eq(sandbox.last('chats_setTitle').args, ['-100', 'New']);
  sandbox.settle(nextId, false, 'CHAT_ADMIN_REQUIRED');
  await rejects(promise, 'CHAT_ADMIN_REQUIRED');
  dzg.chats.setAbout(group, null);
  eq(sandbox.last('chats_setAbout').args, ['-100', '']);
}
{
  const promise = dzg.chats.createInviteLink(group, { usageLimit: 5.7, title: 'promo', requestNeeded: 1 });
  eq(sandbox.last('chats_createInviteLink').args, ['-100', { expireDate: 0, usageLimit: 5, title: 'promo', requestNeeded: true }]);
  sandbox.settle(nextId, true, 'https://t.me/+xyz');
  eq(await promise, 'https://t.me/+xyz');
}
sandbox.returns.chats_clearHistory = true;
eq(dzg.chats.clearHistory(group), true);
eq(sandbox.last('chats_clearHistory').args, ['-100', false]);
dzg.chats.clearHistory(group, { revoke: true });
eq(sandbox.last('chats_clearHistory').args, ['-100', true]);

// --- users
{
  const promise = dzg.users.commonChats(peer);
  eq(sandbox.last('users_commonChats').args, ['777', 100]);
  sandbox.settle(nextId, true, [group]);
  eq((await promise)[0].id, '-100');
  dzg.users.commonChats(peer, { limit: 500 });
  eq(sandbox.last('users_commonChats').args, ['777', 100]);
}
{
  const promise = dzg.users.addContact(peer, { firstName: 'Pavel' });
  eq(sandbox.last('users_addContact').args, ['777', { firstName: 'Pavel', lastName: '', phone: '', sharePhone: false }]);
  sandbox.settle(nextId, true, null);
  eq(await promise, undefined);
  const removed = dzg.users.deleteContact('777');
  eq(sandbox.last('users_deleteContact').args, ['777']);
  sandbox.settle(nextId, true, null);
  eq(await removed, undefined);
}

sandbox.returns.users_self = Object.assign({}, peer, { isSelf: true });
eq(dzg.users.self().isSelf, true);
sandbox.returns.users_self = null;
eq(dzg.users.self(), null);

sandbox.returns.getPeer = peer;
eq(dzg.users.get('777').name, 'Pavel');

sandbox.returns.users_status = { online: true, lastSeen: 1, kind: 'online', text: 'online' };
eq(dzg.users.status(peer).kind, 'online');
eq(sandbox.last('users_status').args, ['777']);

sandbox.returns.users_isBlocked = 1;
eq(dzg.users.isBlocked(peer), true);

sandbox.returns.users_contacts = [peer];
eq(dzg.users.contacts().length, 1);
sandbox.returns.users_contacts = null;
eq(dzg.users.contacts(), []);
{
  const promise = dzg.users.info(peer);
  eq(sandbox.last('users_info').args, ['777']);
  sandbox.settle(nextId, true, { peer, bio: 'bio', phone: '', isContact: false, isPremium: true, isBot: false, isVerified: true, commonChats: 2, status: { online: false, lastSeen: 5 } });
  const info = await promise;
  eq(info.bio, 'bio');
  eq(info.status.lastSeen, 5);
}
{
  const promise = dzg.users.block(peer);
  eq(sandbox.last('users_block').args, ['777', true]);
  sandbox.settle(nextId, true, null);
  eq(await promise, undefined);
  const second = dzg.users.unblock('777');
  eq(sandbox.last('users_block').args, ['777', false]);
  sandbox.settle(nextId, false, 'Could not unblock');
  await rejects(second, 'Could not unblock');
}

// --- accounts
sandbox.returns.accounts_list = [
  { index: 0, self: peer, active: false },
  { index: 2, self: group, active: true },
];
eq(dzg.accounts.list().length, 2);
eq(dzg.accounts.current().index, 2);
sandbox.returns.accounts_switchTo = true;
eq(dzg.accounts.switchTo(0), true);
eq(sandbox.last('accounts_switchTo').args, [0]);
dzg.accounts.switchTo({ index: 2 });
eq(sandbox.last('accounts_switchTo').args, [2]);
sandbox.returns.accounts_list = null;
eq(dzg.accounts.list(), []);
eq(dzg.accounts.current(), null);

// --- events reach dzg.on
sandbox.event('chat.typing', { chat: group, user: peer, action: 'typing', topicId: 0 });
sandbox.event('chat.unread', { chat: group, unread: 4, unreadMark: false, mentions: 1 });
sandbox.event('user.status', { user: peer, online: true, lastSeen: 100, kind: 'online' });
eq(globalThis.chatsEvents.map((e) => e.name), ['chat.typing', 'chat.unread', 'user.status']);
eq(globalThis.chatsEvents[1].data.unread, 4);

print('chats.test.js: all checks passed');
