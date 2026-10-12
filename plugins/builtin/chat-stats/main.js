// Статистика чата по последним N сообщениям (dzg.messages.history).
// Пункт меню «Статистика чата» и /stats [N].
// Автор: @wetakush

const MAX_COUNT = 5000;

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return (value === undefined || value === null || value === '') ? fallback : value;
};

const STOP = new Set((
  'и в во не что он на я с со как а то все она так его но да ты к у же вы за бы по только ее мне было вот от меня еще нет о из ему ' +
  'теперь когда даже ну вдруг ли если уже или ни быть был него до вас нибудь опять уж вам ведь там потом себя ничего ей может они тут где ' +
  'есть надо ней для мы тебя их чем была сам чтоб без будто чего раз тоже себе под будет ж тогда кто этот того потому этого какой совсем ним ' +
  'здесь этом один почти мой тем чтобы нее сейчас были куда зачем всех никогда можно при наконец два об другой хоть после над больше тот ' +
  'через эти нас про всего них какая много разве три эту моя впрочем хорошо свою этой перед иногда лучше чуть том нельзя такой им более ' +
  'всегда конечно всю между это вообще просто тебе типа короче щас сегодня завтра очень вроде кстати может ещё всё её какие какое ' +
  'the a an and or but if then of to in on at for with is are was were be been it this that you he she they we i me my your our not no yes ' +
  'do does did have has had will would can could just so as by from up out about what when who how all there here'
).split(/\s+/));

const DAYS = ['Вс', 'Пн', 'Вт', 'Ср', 'Чт', 'Пт', 'Сб'];
const MEDIA = {
  photo: 'фото', video: 'видео', gif: 'GIF', sticker: 'стикеры', voice: 'голосовые', videoNote: 'кружки',
  audio: 'аудио', file: 'файлы', poll: 'опросы', other: 'другое',
};

const pad = (n) => String(n).padStart(2, '0');

export function bar(value, max, width) {
  if (!max || !value) return '';
  const blocks = ['', '▏', '▎', '▍', '▌', '▋', '▊', '▉'];
  const exact = (value / max) * (width || 16);
  const full = Math.floor(exact);
  const part = blocks[Math.round((exact - full) * 7)];
  return '█'.repeat(full) + part || '▏';
}

function plural(n, one, few, many) {
  const a = n % 10;
  const b = n % 100;
  if (a === 1 && b !== 11) return one;
  if (a >= 2 && a <= 4 && (b < 12 || b > 14)) return few;
  return many;
}

// --- Подсчёт --------------------------------------------------------------

export function analyze(messages, options) {
  const o = options || {};
  const extraStop = new Set(String(o.stopWords || '').toLowerCase().split(/[\s,]+/).filter(Boolean));
  const list = messages.filter((m) => !m.service);
  const authors = new Map();
  const hours = new Array(24).fill(0);
  const weekdays = new Array(7).fill(0);
  const days = new Map();
  const words = new Map();
  const emoji = new Map();
  const media = new Map();
  let links = 0;
  let forwards = 0;
  let replies = 0;
  let chars = 0;
  let texts = 0;
  let edited = 0;

  for (const m of list) {
    const key = m.out ? 'self' : (m.fromId || m.fromName || '?');
    const author = authors.get(key) || { name: m.out ? 'Я' : (m.fromName || '?'), count: 0, chars: 0 };
    author.count++;
    const d = new Date(m.date * 1000);
    hours[d.getHours()]++;
    weekdays[d.getDay()]++;
    const day = d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate());
    days.set(day, (days.get(day) || 0) + 1);
    if (m.media) media.set(m.media, (media.get(m.media) || 0) + 1);
    if (m.forwardFrom) forwards++;
    if (m.replyToId) replies++;
    if (m.editDate) edited++;
    const text = String(m.text || '');
    if (text) {
      texts++;
      chars += text.length;
      author.chars += text.length;
      const urls = (m.entities || []).filter((e) => e.type === 'url' || e.type === 'textUrl' || e.type === 'text_url').length;
      links += urls || (text.match(/https?:\/\/\S+/g) || []).length;
      for (const e of text.match(/\p{Extended_Pictographic}/gu) || []) {
        emoji.set(e, (emoji.get(e) || 0) + 1);
      }
      for (const raw of text.toLowerCase().replace(/https?:\/\/\S+/g, ' ').match(/[\p{L}][\p{L}\p{N}'’-]{2,}/gu) || []) {
        const word = raw.replace(/ё/g, 'е').replace(/['’-]+$/, '');
        if (word.length < 3 || STOP.has(word) || extraStop.has(word)) continue;
        words.set(word, (words.get(word) || 0) + 1);
      }
    }
    authors.set(key, author);
  }

  const sortDesc = (map) => Array.from(map.entries()).sort((a, b) => b[1] - a[1]);
  const first = list.length ? list[0].date : 0;
  const last = list.length ? list[list.length - 1].date : 0;
  const spanDays = Math.max(1, Math.ceil((last - first) / 86400));
  const busiest = sortDesc(days)[0] || null;
  return {
    total: list.length,
    first,
    last,
    spanDays,
    perDay: list.length / spanDays,
    authors: Array.from(authors.values()).sort((a, b) => b.count - a.count),
    hours,
    weekdays,
    busiestDay: busiest ? { day: busiest[0], count: busiest[1] } : null,
    words: sortDesc(words),
    emoji: sortDesc(emoji),
    media: sortDesc(media),
    links,
    forwards,
    replies,
    edited,
    avgLength: texts ? Math.round(chars / texts) : 0,
  };
}

// --- Отчёт ----------------------------------------------------------------

function formatDate(unix) {
  const d = new Date(unix * 1000);
  return pad(d.getDate()) + '.' + pad(d.getMonth() + 1) + '.' + d.getFullYear();
}

export function sections(stats, options) {
  const o = options || {};
  const topAuthors = Math.max(1, Number(o.topAuthors) || 8);
  const topWords = Math.max(1, Number(o.topWords) || 12);
  const out = [];
  out.push({
    title: 'Обзор',
    lines: [
      stats.total + ' ' + plural(stats.total, 'сообщение', 'сообщения', 'сообщений') +
        ' за ' + stats.spanDays + ' ' + plural(stats.spanDays, 'день', 'дня', 'дней') +
        ' (' + formatDate(stats.first) + ' — ' + formatDate(stats.last) + ')',
      '≈ ' + stats.perDay.toFixed(stats.perDay < 10 ? 1 : 0) + ' в день, средняя длина ' + stats.avgLength + ' симв.',
      stats.busiestDay ? 'Самый активный день: ' + stats.busiestDay.day.split('-').reverse().join('.') + ' — ' + stats.busiestDay.count : '',
      'Ответов: ' + stats.replies + ' · пересылок: ' + stats.forwards + ' · ссылок: ' + stats.links + ' · правок: ' + stats.edited,
    ].filter(Boolean),
  });
  if (stats.authors.length > 1 || (stats.authors[0] && stats.authors[0].name !== 'Я')) {
    const max = stats.authors[0] ? stats.authors[0].count : 0;
    out.push({
      title: 'Кто пишет больше всех',
      lines: stats.authors.slice(0, topAuthors).map((a, i) =>
        (i + 1) + '. ' + a.name + ' — ' + a.count + ' (' + Math.round((a.count / stats.total) * 100) + '%) ' + bar(a.count, max, 10)),
    });
  }
  const maxHour = Math.max(...stats.hours);
  out.push({
    title: 'Активность по часам',
    mono: true,
    lines: stats.hours.map((count, hour) => pad(hour) + ' ' + (bar(count, maxHour, 16) || '·') + (count ? ' ' + count : '')),
  });
  const maxDay = Math.max(...stats.weekdays);
  out.push({
    title: 'По дням недели',
    mono: true,
    lines: [1, 2, 3, 4, 5, 6, 0].map((d) => DAYS[d] + ' ' + (bar(stats.weekdays[d], maxDay, 16) || '·') + (stats.weekdays[d] ? ' ' + stats.weekdays[d] : '')),
  });
  if (stats.words.length) {
    out.push({
      title: 'Частые слова',
      lines: [stats.words.slice(0, topWords).map(([w, c]) => w + ' ×' + c).join(', ')],
    });
  }
  if (stats.emoji.length) {
    out.push({
      title: 'Эмодзи',
      lines: [stats.emoji.slice(0, 10).map(([e, c]) => e + ' ' + c).join('   ')],
    });
  }
  if (stats.media.length) {
    out.push({
      title: 'Медиа',
      lines: [stats.media.map(([type, c]) => (MEDIA[type] || type) + ': ' + c).join(' · ')],
    });
  }
  return out;
}

export function reportText(chat, stats, options) {
  const parts = ['Статистика «' + chat.name + '»'];
  for (const section of sections(stats, options)) {
    parts.push('', section.title + ':');
    parts.push(...section.lines);
  }
  parts.push('', 'DamZaGram · chat-stats');
  return parts.join('\n');
}

// --- Загрузка и панель ----------------------------------------------------

async function load(chat, count, progress) {
  let result = [];
  let offsetId = 0;
  while (result.length < count) {
    const limit = Math.min(100, count - result.length);
    const batch = await dzg.messages.history(chat.id, { limit, offsetId });
    if (!batch || !batch.length) break;
    result = batch.concat(result);
    if (offsetId && batch[0].id >= offsetId) break;
    offsetId = batch[0].id;
    if (progress) progress(result.length);
    if (batch.length < limit) break;
  }
  return result;
}

let panel = null;
let loading = false;

function openPanel(title) {
  if (panel && panel.isOpen()) {
    panel.clear();
    panel.setTitle(title);
    return panel;
  }
  panel = dzg.ui.panel({ title, width: 380 });
  panel.on('close', () => { panel = null; });
  return panel;
}

export async function showStats(chat, count) {
  chat = chat || dzg.chats.current();
  if (!chat) {
    dzg.ui.toast('Открой чат для статистики');
    return;
  }
  if (!dzg.ui.panel || !dzg.messages.history) {
    dzg.ui.toast('Этой версии DamZaGram не хватает API для статистики');
    return;
  }
  if (loading) {
    dzg.ui.toast('Статистика уже считается…');
    return;
  }
  const wanted = Math.max(10, Math.min(MAX_COUNT, Number(count) || Number(setting('count', 1000)) || 1000));
  const options = {
    topAuthors: setting('topAuthors', 8),
    topWords: setting('topWords', 12),
    stopWords: setting('stopWords', ''),
  };
  loading = true;
  const p = openPanel('' + chat.name);
  const status = p.addLabel('Загружаю сообщения… 0 из ' + wanted, { muted: true });
  try {
    const messages = await load(chat, wanted, (n) => status.setText('Загружаю сообщения… ' + n + ' из ' + wanted));
    if (!panel) return;
    const stats = analyze(messages, options);
    panel.clear();
    if (!stats.total) {
      panel.addLabel('В чате нет сообщений для статистики.');
      return;
    }
    for (const section of sections(stats, options)) {
      panel.addLabel(section.title, { bold: true });
      panel.addLabel(section.lines.join('\n'));
      panel.addSkip(6);
    }
    panel.addSeparator();
    const text = reportText(chat, stats, options);
    panel.addButton('Скопировать отчёт', () => {
      dzg.clipboard.set(text);
      dzg.ui.toast('Отчёт скопирован');
    }, { style: 'light' });
    panel.addButton('Отправить в этот чат', async () => {
      if (await dzg.ui.confirm('Отправить статистику в «' + chat.name + '»?', { confirmText: 'Отправить', cancelText: 'Отмена' })) {
        dzg.messages.send(chat.id, text);
      }
    }, { style: 'light' });
    panel.addButton('Посчитать по ' + Math.min(MAX_COUNT, wanted * 2) + ' сообщениям', () => showStats(chat, wanted * 2), { style: 'light' });
  } catch (e) {
    if (panel) {
      panel.clear();
      panel.addLabel('Не удалось загрузить историю: ' + (e && e.message ? e.message : e));
    }
  } finally {
    loading = false;
  }
}

dzg.commands.register('stats', {
  description: 'Статистика чата: /stats [сколько сообщений]',
  run: ({ args, chat }) => showStats(chat, Number(args[0]) || 0),
});

dzg.ui.addChatMenuItem({
  title: 'Статистика чата',
  onClick: (chat) => showStats(chat),
});

dzg.on('unload', () => {
  if (panel) panel.close();
});
