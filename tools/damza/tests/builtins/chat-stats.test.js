import { analyze, sections, bar, reportText } from '../../../../plugins/builtin/chat-stats/main.js';

const { eq, assert } = sandbox;

eq(bar(0, 10, 10), '');
eq(bar(10, 10, 10), '██████████');
eq(bar(5, 10, 10), '█████');

const chat = mock.chats[1];
const base = new Date(2026, 9, 5, 9, 0, 0).getTime() / 1000; // понедельник, 9:00
const history = [];
for (let i = 0; i < 300; ++i) {
  const author = i % 3 === 0 ? { fromId: '7', fromName: 'Петя' } : { fromId: '8', fromName: 'Маша' };
  history.push(mock.message(Object.assign({
    id: i + 1,
    peerId: chat.id,
    chat,
    date: base + i * 1800,
    text: i % 10 === 0 ? 'релиз DamZaGram 🚀 https://t.me/wetakush' : 'обсуждаем релиз и плагины',
    media: i % 25 === 0 ? 'photo' : undefined,
    replyToId: i % 7 === 0 ? i : 0,
  }, author)));
}
history.unshift(mock.message({ id: 0, peerId: chat.id, chat, service: true, text: '', date: base }));

const stats = analyze(history, {});
eq(stats.total, 300, 'service messages skipped');
eq(stats.authors[0].name, 'Маша');
eq(stats.authors[0].count, 200);
eq(stats.authors[1].count, 100);
eq(stats.hours.reduce((a, b) => a + b, 0), 300);
eq(stats.words[0][0], 'релиз');
eq(stats.words[0][1], 300);
assert(!stats.words.some(([w]) => w === 'и'), 'stop words removed');
eq(stats.emoji[0], ['🚀', 30]);
eq(stats.links, 30);
eq(stats.media[0], ['photo', 12]);
assert(stats.replies > 0);
const stopped = analyze(history, { stopWords: 'релиз' });
assert(stopped.words[0][0] !== 'релиз', 'custom stop words');

const secs = sections(stats, { topAuthors: 5, topWords: 5 });
const titles = secs.map((s) => s.title);
assert(titles.includes('Кто пишет больше всех') && titles.includes('Активность по часам') && titles.includes('Эмодзи'), titles.join());
eq(secs.find((s) => s.title === 'Активность по часам').lines.length, 24);
assert(reportText(chat, stats, {}).startsWith('📊 Статистика «DamZaGram Chat»'));

// /stats loads history page by page and renders the panel.
mock.history[chat.id] = history;
mock.command('/stats 250', chat);
await mock.tick(80);
const panel = mock.lastPanel();
const text = mock.panelText(panel);
assert(text.includes('250 сообщений'), text);
assert(text.includes('Маша'), 'authors listed');
mock.click(panel, 'Скопировать отчёт');
assert(mock.clipboard.includes('Активность по часам'));
mock.click(panel, 'Отправить в этот чат');
await mock.tick();
eq(mock.sent[mock.sent.length - 1].peerId, chat.id);

// Chat menu item.
assert(mock.menu('chat', chat).some((i) => i.title.includes('Статистика')));
