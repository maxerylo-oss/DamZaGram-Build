// Setup for running UI plugins in the sandbox: registration natives of the
// `ui` module return fresh ids, like the client does.
//   tools/damza/sandbox.py plugins/examples/ui-showcase/main.js \
//     tools/damza/tests/ui-showcase.test.js --setup=tools/damza/tests/ui.setup.js
(function () {
  let nextUiId = 500;
  for (const name of ['ui_addTopBarButton', 'ui_addComposeButton', 'ui_addMainMenuItem', 'ui_addShortcut']) {
    sandbox.native[name] = () => ++nextUiId;
  }
  // Panel items: remember what was added under which widget id.
  sandbox.panelItems = [];
  sandbox.native.ui_panelAdd = (panelId, type, props) => {
    const id = ++nextUiId;
    sandbox.panelItems.push({ id, panelId, type, props });
    return id;
  };
  sandbox.native.ui_panelOpen = () => ({ id: ++nextUiId, content: ++nextUiId, reused: false });
  // The same names as damza/plugins/ui/ui_widgets.cpp.
  sandbox.native.ui_icons = () => [
    'search', 'settings', 'customize', 'edit', 'delete', 'clear', 'copy', 'link', 'links', 'pin',
    'unpin', 'mute', 'unmute', 'sound', 'soundOff', 'silent', 'archive', 'unarchive', 'info',
    'help', 'stats', 'bot', 'code', 'experimental', 'star', 'fave', 'like', 'premium', 'gift',
    'bell', 'lock', 'user', 'users', 'addUser', 'admin', 'username', 'channel', 'chat', 'chats',
    'topics', 'discussion', 'saved', 'folder', 'addToFolder', 'photo', 'video', 'gif', 'file',
    'download', 'upload', 'reply', 'forward', 'send', 'share', 'translate', 'palette', 'night',
    'font', 'draw', 'spoiler', 'plus', 'close', 'check', 'markRead', 'markUnread', 'timer',
    'schedule', 'hourglass', 'location', 'poll', 'todo', 'sticker', 'emoji', 'reactions', 'phone',
    'qr', 'block', 'report', 'restore', 'leave', 'network', 'devices', 'permissions', 'manage',
    'shortcut', 'newWindow', 'expand', 'collapse', 'zoomIn', 'zoomOut', 'reorder', 'storage',
    'power', 'tag', 'shop', 'ghost', 'streamer'
  ];
  sandbox.native.ui_composeGet = () => ({ chat: { id: '42', name: 'Test', type: 'user' }, text: 'ghbdtn' });
})();
