import { isAd, activeIn, compileWords } from '../../../../plugins/builtin/anti-ads/main.js';

const { eq, assert } = sandbox;

const channel = mock.chats[2];
const group = mock.chats[1];
const filter = (m) => sandbox.dispatch('filter', m);
const post = (id, text, chat) => mock.message({ id, text, chat: chat || channel, peerId: (chat || channel).id, post: true });

// Built-in markers.
assert(isAd('Реклама. ООО «Ромашка», ИНН 7700000000. erid: 2SDnjcR1abc'));
assert(isAd('Лучший курс по Python #реклама'));
assert(isAd('Материал опубликован на правах рекламы'));
assert(!isAd('Сегодня обсуждали рекламу в Telegram'), 'plain mention is fine');
assert(!isAd('heroid: hello'), 'erid inside a word');
eq(compileWords('промокод\n/скидк[аи]/\n/[/').length, 2);

// Only channels by default.
assert(activeIn(channel));
assert(!activeIn(group));
eq(filter(post(1, 'Купите слона. erid: 2VtzqwXyz12')), true);
eq(filter(post(2, 'Обычный пост про релиз')), false);
eq(filter(post(3, '#реклама в группе', group)), false, 'groups untouched');
eq(filter(mock.message({ id: 4, out: true, chat: channel, peerId: channel.id, text: '#реклама' })), false, 'own posts untouched');

// Counter: unique messages only, persisted with a delay.
filter(post(1, 'Купите слона. erid: 2VtzqwXyz12'));
sandbox.runTimers();
eq(sandbox.storage.stats.total, 1);
eq(sandbox.storage.stats.byChat['300'].count, 1);

// User words and strict mode via settings.
eq(filter(post(5, 'Скидка 50% только сегодня')), false);
sandbox.settings.strict = true;
sandbox.event('settings.changed', { key: 'strict', value: true });
eq(filter(post(5, 'Скидка 50% только сегодня')), true);
sandbox.settings.words = 'казино';
sandbox.event('settings.changed', { key: 'words', value: 'казино' });
eq(filter(post(6, 'Лучшее КАЗИНО')), true);
assert(sandbox.called('refilter').length >= 2, 'refilter on settings change');

// Per-chat toggle from the chat menu.
const menu = mock.menu('chat', channel);
assert(menu[0].title.includes('выключить здесь'), menu[0].title);
mock.menuClick('chat', channel, 'выключить здесь');
eq(sandbox.storage.overrides['300'], false);
eq(filter(post(7, '#реклама')), false, 'disabled in this channel');
mock.menuClick('chat', group, 'включить здесь');
eq(filter(post(8, '#реклама', group)), true, 'enabled in this group');
assert(mock.menu('chat', mock.current).length === 0, 'no item for private chats');

// Panel: counters, pause.
mock.command('/antiads');
const panel = mock.lastPanel();
assert(mock.panelText(panel).includes('Скрыто за всё время'), mock.panelText(panel));
mock.click(panel, 'Показать скрытое');
eq(filter(post(9, '#реклама', group)), false, 'paused');
mock.click(panel, 'Снова скрывать');
eq(filter(post(9, '#реклама', group)), true);
mock.click(panel, 'Сбросить исключения');
eq(Object.keys(sandbox.storage.overrides).length, 0);
