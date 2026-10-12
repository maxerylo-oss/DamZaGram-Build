// Sandbox test for the "plugins" API module (api_plugins.cpp).
const { eq, assert } = sandbox;

assert(typeof dzg.plugins === 'object', 'dzg.plugins exists');

sandbox.returns.plugins_path = '/tmp/plugins/';
eq(dzg.plugins.path, '/tmp/plugins/');

const entry = { id: 'hello', name: 'Hello', version: '1.0.0', enabled: true, loaded: true, builtin: false, error: '' };
sandbox.returns.plugins_list = [entry];
eq(dzg.plugins.list(), [entry]);
sandbox.returns.plugins_list = null;
eq(dzg.plugins.list(), [], 'list() never returns null');

sandbox.returns.plugins_get = entry;
eq(dzg.plugins.get('hello'), entry);
eq(sandbox.last('plugins_get').args, ['hello']);
dzg.plugins.get();
eq(sandbox.last('plugins_get').args, ['sandbox'], 'defaults to own id');
dzg.plugins.get({ id: 'other' });
eq(sandbox.last('plugins_get').args, ['other'], 'accepts objects');
sandbox.returns.plugins_get = null;
eq(dzg.plugins.get('missing'), null);

sandbox.returns.plugins_setEnabled = true;
eq(dzg.plugins.enable('hello'), true);
eq(sandbox.last('plugins_setEnabled').args, ['hello', true]);
dzg.plugins.disable('hello');
eq(sandbox.last('plugins_setEnabled').args, ['hello', false]);
dzg.plugins.setEnabled('hello', 0);
eq(sandbox.last('plugins_setEnabled').args, ['hello', false]);

sandbox.returns.plugins_reload = true;
eq(dzg.plugins.reload(), true);
eq(sandbox.last('plugins_reload').args, ['sandbox']);
dzg.plugins.reloadAll();
eq(sandbox.last('plugins_reload').args, ['']);

sandbox.returns.plugins_log = ['[info] a', '[error] b'];
eq(dzg.plugins.log('hello', 2), ['[info] a', '[error] b']);
eq(sandbox.last('plugins_log').args, ['hello', 2]);
dzg.plugins.log('hello');
eq(sandbox.last('plugins_log').args, ['hello', 100]);

sandbox.returns.plugins_settings = { greeting: 'hi' };
eq(dzg.plugins.settings('hello'), { greeting: 'hi' });
sandbox.returns.plugins_setSetting = true;
eq(dzg.plugins.setSetting('hello', 'greeting', 'yo'), true);
eq(sandbox.last('plugins_setSetting').args, ['hello', 'greeting', 'yo']);

sandbox.native.plugins_eval = (id, code) => (code === 'boom'
  ? { ok: false, error: 'ReferenceError: boom is not defined' }
  : { ok: true, result: '42' });
eq(dzg.plugins.evaluate('hello', '6 * 7'), '42');
let thrown = null;
try { dzg.plugins.evaluate('hello', 'boom'); } catch (e) { thrown = e; }
assert(thrown instanceof Error && /boom/.test(thrown.message), 'evaluate throws on error');

let installed = null;
let failed = null;
sandbox.native.plugins_install = () => 501;
dzg.plugins.install('https://example.com/x.dzg.zip').then((id) => { installed = id; });
eq(sandbox.last('plugins_install').args, ['https://example.com/x.dzg.zip', true]);
sandbox.settle(501, true, 'x');
dzg.plugins.install('/tmp/y.js', { enable: false });
eq(sandbox.last('plugins_install').args, ['/tmp/y.js', false]);

sandbox.native.plugins_uninstall = () => 502;
dzg.plugins.uninstall('x', { removeData: true }).catch((e) => { failed = e; });
eq(sandbox.last('plugins_uninstall').args, ['x', true]);
sandbox.settle(502, false, 'Plugin not found: x');

let inspected = null;
sandbox.native.plugins_inspect = () => 503;
dzg.plugins.inspect('https://github.com/a/b').then((info) => { inspected = info; });
sandbox.settle(503, true, { id: 'b', name: 'B', files: 2 });

dzg.plugins.openManager();
assert(sandbox.last('plugins_openManager'), 'openManager calls native');
dzg.plugins.openDevTools('hello');
eq(sandbox.last('plugins_openDevTools').args, ['hello']);
dzg.plugins.openDevTools();
eq(sandbox.last('plugins_openDevTools').args, ['']);

sandbox.event('plugins.changed', { plugins: [entry] });
eq(globalThis.pluginsEvents, [{ plugins: [entry] }]);

await Promise.resolve();
await Promise.resolve();
eq(installed, 'x', 'install resolves with the id');
assert(failed instanceof Error && failed.message === 'Plugin not found: x', 'uninstall rejects');
eq(inspected, { id: 'b', name: 'B', files: 2 });
print('plugins.test.js: all good');
