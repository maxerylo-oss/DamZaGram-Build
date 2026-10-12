#!/usr/bin/env python3
"""Check the code in docs/plugins/**/*.md.

  tools/damza/check_docs.py [--tsc=path/to/tsc]

1. Every ```js block in every Markdown file must parse as an ES module
   (node --check; snippets may use top-level await).
2. Every ```js block in docs/plugins/cookbook.md is a complete plugin: it is
   loaded in the sandbox (tools/damza/sandbox.py, real prelude and API
   modules) and receives a round of events and its own commands. Any
   "[error]" line in the plugin log fails the check.
3. With TypeScript available (--tsc, $TSC or `tsc` in PATH), cookbook
   recipes are type-checked with --checkJs against docs/plugins/types.
Exit code is non-zero on any failure.
"""
import glob, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOCS = os.path.join(ROOT, 'docs', 'plugins')
SANDBOX = os.path.join(ROOT, 'tools', 'damza', 'sandbox.py')
UI_SETUP = os.path.join(ROOT, 'tools', 'damza', 'tests', 'ui.setup.js')

BLOCK = re.compile(r'^```(js|javascript)\s*\n(.*?)^```', re.S | re.M)

SETUP = r"""
const peer = (id, name, type) => ({ id, tgId: Number(id), name, username: name.toLowerCase(), type, isSelf: id === '1' });
sandbox.me = peer('1', 'Me', 'user');
sandbox.chat = peer('42', 'Chat', 'group');
sandbox.returns.users_self = sandbox.me;
sandbox.returns.chats_list = [];
sandbox.returns.chats_entry = null;
sandbox.returns.app_isNightMode = false;
sandbox.returns.messages_reactions = ['👍', '❤'];
sandbox.returns.messages_link = 'https://t.me/c/42/7';
sandbox.returns.clipboardText = '';
sandbox.returns.readFile = null;
sandbox.returns.writeFile = true;
sandbox.returns.messages_send = true;
sandbox.returns.messages_sendFile = true;
sandbox.returns.setThemeColor = true;
let requestId = 9000;
for (const name of ['messages_history', 'messages_search', 'messages_fetch', 'messages_download',
  'messages_forward', 'messages_react', 'messages_pin', 'messages_clickButton', 'api_call',
  'chats_resolve', 'chats_info', 'chats_members', 'users_info']) {
  sandbox.native[name] = () => ++requestId;
}
"""

SMOKE = r"""
const { me, chat } = sandbox;
const message = (extra) => Object.assign({
  id: 7, peerId: chat.id, chat, fromId: '2', fromName: 'Pavel', text: 'спасибо, @me вакансия',
  date: Math.floor(Date.now() / 1000), out: false, service: false, post: false, pinned: false,
  deleted: false, replyToId: 0, views: 0, entities: [], editDate: 0, groupedId: '',
}, extra || {});
sandbox.event('ready');
sandbox.event('session.ready', me);
sandbox.event('message.new', message());
sandbox.event('message.new', message({ post: true, chat: Object.assign({}, chat, { type: 'channel' }) }));
sandbox.event('message.edited', message({ text: 'edited' }));
sandbox.event('message.deleted', message());
sandbox.event('message.reaction', message({ out: true, reactions: [{ emoji: '👍', count: 1, chosen: false }] }));
sandbox.event('chat.opened', chat);
sandbox.event('chat.typing', { chat, user: me, action: 'typing', topicId: 0 });
sandbox.event('chat.unread', { chat, unread: 3, unreadMark: false, mentions: 1 });
sandbox.event('user.status', { user: me, online: true, lastSeen: 0, kind: 'online' });
sandbox.event('settings.changed', { key: 'words', value: 'x' });
sandbox.event('theme.changed', { night: true });
sandbox.event('app.focus', { active: true });
sandbox.event('compose.changed', { chat, text: 'x'.repeat(5000) });
sandbox.dispatch('filter', message());
sandbox.dispatch('menu:collect', { kind: 'message', target: message() });
sandbox.dispatch('menu:collect', { kind: 'chat', target: chat });
for (const command of dzg.commands.list()) {
  sandbox.dispatch('beforeSend', { text: '/' + command.name + ' 10m test | a | b', chat, peerId: chat.id, replyTo: 0 });
}
sandbox.dispatch('beforeSend', { text: 'обычный текст', chat, peerId: chat.id, replyTo: 0 });
sandbox.runTimers();
for (let i = 0; i < 10; ++i) await Promise.resolve();
"""


def blocks(path):
    text = open(path, encoding='utf-8').read()
    return [m.group(2) for m in BLOCK.finditer(text)]


def find_tsc():
    for arg in sys.argv[1:]:
        if arg.startswith('--tsc='):
            return arg.split('=', 1)[1]
    return os.environ.get('TSC') or shutil.which('tsc')


def main():
    failures = []
    files = sorted(glob.glob(os.path.join(DOCS, '**', '*.md'), recursive=True))
    node = shutil.which('node')
    with tempfile.TemporaryDirectory() as tmp:
        count = 0
        for path in files:
            for i, code in enumerate(blocks(path)):
                count += 1
                if not node:
                    continue
                file = os.path.join(tmp, 'parse_%d.mjs' % count)
                open(file, 'w', encoding='utf-8').write(code)
                result = subprocess.run([node, '--check', file], capture_output=True, text=True)
                if result.returncode:
                    lines = [l for l in result.stderr.splitlines() if 'Error' in l][:1]
                    failures.append('%s block %d: %s' % (os.path.relpath(path, ROOT), i + 1, ' '.join(lines)))
        print('parsed %d js blocks in %d files%s' % (count, len(files), '' if node else ' (node missing: skipped)'))

        recipes = blocks(os.path.join(DOCS, 'cookbook.md'))
        setup = os.path.join(tmp, 'setup.js')
        open(setup, 'w', encoding='utf-8').write(SETUP)
        smoke = os.path.join(tmp, 'smoke.js')
        open(smoke, 'w', encoding='utf-8').write(SMOKE)
        recipe_files = []
        for i, code in enumerate(recipes):
            file = os.path.join(tmp, 'recipe_%02d.js' % (i + 1))
            open(file, 'w', encoding='utf-8').write(code + '\nexport {};\n')
            recipe_files.append(file)
            result = subprocess.run(
                [sys.executable, SANDBOX, file, smoke, '--setup=' + UI_SETUP, '--setup=' + setup],
                capture_output=True, text=True)
            output = result.stdout + result.stderr
            errors = [l for l in output.splitlines() if l.startswith('[error]')]
            if result.returncode or errors or 'SANDBOX OK' not in output:
                failures.append('cookbook recipe %d: %s' % (i + 1, (errors or output.strip().splitlines()[-3:])))
        print('ran %d cookbook recipes in the sandbox' % len(recipes))

        tsc = find_tsc()
        if tsc:
            cmd = (['node', tsc] if tsc.endswith('.js') or os.path.basename(tsc) == 'tsc' and 'node_modules' in tsc else [tsc])
            cmd += ['--noEmit', '--allowJs', '--checkJs', '--strict', 'false',
                    '--target', 'ES2023', '--lib', 'ES2023', '--module', 'ES2022', '--moduleResolution', 'bundler',
                    os.path.join(DOCS, 'types', 'index.d.ts')] + recipe_files
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode:
                failures.append('tsc:\n' + (result.stdout + result.stderr).strip())
            print('type-checked cookbook recipes with %s' % tsc)
        else:
            print('tsc not found: type check skipped')

    if failures:
        print('\nFAILED:')
        for failure in failures:
            print(' -', failure)
        return 1
    print('docs OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
