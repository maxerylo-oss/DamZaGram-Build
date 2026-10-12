// tools/damza/sandbox.py plugins/examples/hello/main.js tools/damza/tests/core-timers.test.js
// Timer delays reach the native side as ints: Infinity or huge values must not overflow.
const { eq, assert } = sandbox;

sandbox.reset();
setTimeout(() => {}, Infinity);
eq(sandbox.last('startTimer').args, [2147483647, false], 'Infinity clamps');
setInterval(() => {}, 1e12);
eq(sandbox.last('startTimer').args, [2147483647, true], 'huge delay clamps');
setTimeout(() => {}, -5);
eq(sandbox.last('startTimer').args, [0, false], 'negative delay is 0');
setTimeout(() => {}, 'abc');
eq(sandbox.last('startTimer').args, [0, false], 'NaN delay is 0');
let fired = 0;
const id = setTimeout(() => { ++fired; }, 10);
assert(typeof id === 'number', 'timer id');
sandbox.runTimers();
eq(fired, 1, 'timer fires');
print('core-timers.test.js: all checks passed');
