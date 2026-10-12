import { parseWhen, formatIn } from '../../../../plugins/builtin/reminders/main.js';

const { eq, assert } = sandbox;

const now = new Date(2026, 9, 12, 12, 0, 0, 0).getTime();
const at = (y, mo, d, h, mi) => new Date(y, mo, d, h, mi, 0, 0).getTime();

eq(parseWhen('10m купить хлеб', now), { at: now + 600000, rest: 'купить хлеб' });
eq(parseWhen('1h30m созвон', now).at, now + 5400000);
eq(parseWhen('1 ч 30 мин созвон', now), { at: now + 5400000, rest: 'созвон' });
eq(parseWhen('через 2 часа позвонить маме', now), { at: now + 7200000, rest: 'позвонить маме' });
eq(parseWhen('2д', now).at, now + 2 * 86400000);
eq(parseWhen('1.5h x', now).at, now + 5400000);
eq(parseWhen('18:30 созвон', now), { at: at(2026, 9, 12, 18, 30), rest: 'созвон' });
eq(parseWhen('в 18:30 созвон', now).at, at(2026, 9, 12, 18, 30));
eq(parseWhen('11:00 x', now).at, at(2026, 9, 13, 11, 0), 'past time -> tomorrow');
eq(parseWhen('завтра 9:00 отчёт', now), { at: at(2026, 9, 13, 9, 0), rest: 'отчёт' });
eq(parseWhen('завтра', now), { at: at(2026, 9, 13, 9, 0), rest: '' });
eq(parseWhen('25.12 10:00 подарки', now), { at: at(2026, 11, 25, 10, 0), rest: 'подарки' });
eq(parseWhen('15 чай', now).at, now + 15 * 60000, 'bare number = minutes');
eq(parseWhen('бла-бла', now), null);
eq(parseWhen('9'.repeat(400) + 'm x', now), null, 'infinite duration');
eq(parseWhen('99999999999w x', now), null, 'out of Date range');
eq(parseWhen('9'.repeat(400) + ' x', now), null, 'infinite bare minutes');
eq(formatIn(5400000), '1 ч 30 мин');

// Command adds a persisted reminder.
mock.command('/remind 10m купить хлеб');
eq(sandbox.storage.items.length, 1);
const item = sandbox.storage.items[0];
eq(item.text, 'купить хлеб');
eq(item.peerId, '100');
assert(mock.toasts[0].includes('Напомню'), mock.toasts[0]);

// Not due yet: the timer re-arms without firing.
sandbox.runTimers();
eq(mock.notifications.length, 0);

// Time passes: it fires with a jump to the chat and is removed.
const realNow = Date.now;
Date.now = () => realNow() + 11 * 60000;
sandbox.runTimers();
eq(mock.notifications.length, 1);
assert(mock.notifications[0].text.includes('купить хлеб'));
assert(!mock.toasts.some((t) => t.startsWith('⏰ Напоминание')), 'no duplicate toast next to the notification');
mock.notifications[0].onClick();
eq(mock.opened[0].peerId, '100');
eq(sandbox.storage.items.length, 0);
Date.now = realNow;

// Message menu: asks for the delay, keeps the message link.
const message = mock.message({ id: 77, text: 'созвон в пятницу', fromName: 'Вася' });
mock.promptAnswers.push('30m');
mock.menuClick('message', message, 'Напомнить через');
await mock.tick();
eq(sandbox.storage.items.length, 1);
eq(sandbox.storage.items[0].msgId, 77);
assert(sandbox.storage.items[0].text.includes('созвон в пятницу'));

// Panel: list, snooze, delete.
mock.command('/reminders');
const panel = mock.lastPanel();
assert(mock.panelText(panel).includes('созвон в пятницу'));
const before = sandbox.storage.items[0].at;
mock.click(panel, '+1 час');
eq(sandbox.storage.items[0].at, before + 3600000);
mock.click(panel, 'удалить');
eq(sandbox.storage.items.length, 0);

// Missed while the client was closed: fires on start with a note.
sandbox.storage.items = [{ id: 9, at: Date.now() - 3600000, text: 'старое', peerId: '200', chatName: 'DamZaGram Chat', msgId: 5, created: 0 }];
sandbox.event('ready');
sandbox.runTimers();
const last = mock.notifications[mock.notifications.length - 1];
assert(last.text.includes('старое') && last.text.includes('пропущено'), last.text);
eq(sandbox.storage.items.length, 0);

// Bad input explains the format.
mock.command('/remind когда-нибудь');
assert(mock.toasts[mock.toasts.length - 1].includes('Не понял'));

// A reminder that throws while firing is dropped and the others still fire
// and re-arm; one without a numeric time doesn't spin a zero-delay timer.
const realNotify = dzg.ui.notify;
dzg.ui.notify = (spec) => {
  if (String(spec.text).includes('сломанное')) throw new Error('boom');
  realNotify(spec);
};
const soon = Date.now() - 1000;
sandbox.storage.items = [
  { id: 20, at: soon, text: 'сломанное', peerId: '', chatName: '', msgId: 0, created: 0 },
  { id: 21, at: soon, text: 'целое', peerId: '', chatName: '', msgId: 0, created: 0 },
  { id: 22, text: 'без времени', peerId: '', chatName: '', msgId: 0, created: 0 },
  { id: 23, at: Date.now() + 3600000, text: 'через час', peerId: '', chatName: '', msgId: 0, created: 0 },
];
const count = mock.notifications.length;
sandbox.event('ready');
sandbox.runTimers();
eq(sandbox.storage.items.map((i) => i.id), [23], 'due ones removed, future one kept');
eq(mock.notifications.slice(count).map((n) => n.text.split('\n')[0]), ['целое', 'без времени']);
sandbox.reset();
sandbox.runTimers();
const rearmed = sandbox.called('startTimer');
eq(rearmed.length, 1, 'timer re-armed once');
eq(rearmed[0].args[0], 60000, 'next step is the capped delay, not 0');
dzg.ui.notify = realNotify;
