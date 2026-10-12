// Экспорт чата: превращение сообщений в JSON, Markdown и HTML.
// Здесь только форматирование — без запросов к серверу и без интерфейса.
// Писатели (writers) получают сообщения от новых к старым, как их отдаёт
// dzg.messages.iterate, а в finish() собирают документ в хронологическом
// порядке: от старых к новым.
// Автор: @wetakush

export const ACCENT = '#C8F135';

// Метка ответа: заменяется цитатой, когда загружено всё (оригинал ответа
// обычно старше и приходит позже самого ответа).
const REPLY_TOKEN = '\u0000reply\u0000';
// Разделитель имени и текста в кратком пересказе сообщения для цитат.
const SNIPPET_SEP = '\u0001';

// --- Даты, числа, размеры -------------------------------------------------

export const pad = (n) => String(n).padStart(2, '0');

const MONTHS = ['января', 'февраля', 'марта', 'апреля', 'мая', 'июня', 'июля',
  'августа', 'сентября', 'октября', 'ноября', 'декабря'];
const WEEKDAYS = ['воскресенье', 'понедельник', 'вторник', 'среда', 'четверг', 'пятница', 'суббота'];

const toDate = (unix) => new Date((Number(unix) || 0) * 1000);

/** 'YYYY-MM-DD' в местном времени. */
export function dayKey(unix) {
  const d = toDate(unix);
  return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate());
}

/** 'Понедельник, 5 октября 2026'. */
export function formatDay(unix) {
  const d = toDate(unix);
  const weekday = WEEKDAYS[d.getDay()];
  return weekday[0].toUpperCase() + weekday.slice(1) + ', ' + d.getDate() + ' ' + MONTHS[d.getMonth()] + ' ' + d.getFullYear();
}

export function formatTime(unix) {
  const d = toDate(unix);
  return pad(d.getHours()) + ':' + pad(d.getMinutes());
}

export function formatDate(unix) {
  const d = toDate(unix);
  return pad(d.getDate()) + '.' + pad(d.getMonth() + 1) + '.' + d.getFullYear();
}

export function formatDateTime(unix) {
  return formatDate(unix) + ' ' + formatTime(unix);
}

/** 1234567 → '1 234 567' (неразрывные пробелы). */
export function formatNumber(n) {
  return String(Math.round(Number(n) || 0)).replace(/\B(?=(\d{3})+(?!\d))/g, ' ');
}

export function plural(n, one, few, many) {
  const a = Math.abs(n) % 10;
  const b = Math.abs(n) % 100;
  if (a === 1 && b !== 11) return one;
  if (a >= 2 && a <= 4 && (b < 12 || b > 14)) return few;
  return many;
}

export const messagesWord = (n) => plural(n, 'сообщение', 'сообщения', 'сообщений');

export function formatSize(bytes) {
  const n = Number(bytes) || 0;
  if (n < 1024) return n + ' Б';
  if (n < 1024 * 1024) return Math.round(n / 1024) + ' КБ';
  const mb = n / (1024 * 1024);
  if (mb < 1024) return (mb < 10 ? mb.toFixed(1) : String(Math.round(mb))).replace('.', ',') + ' МБ';
  return (mb / 1024).toFixed(1).replace('.', ',') + ' ГБ';
}

/** Секунды → '0:42', '12:05', '1:02:03'. */
export function formatDuration(seconds) {
  const s = Math.max(0, Math.round(Number(seconds) || 0));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const rest = pad(s % 60);
  return h ? h + ':' + pad(m) + ':' + rest : m + ':' + rest;
}

// --- Сообщение: автор, медиа, краткий текст -------------------------------

export const MEDIA = {
  photo: ['📷', 'Фото'],
  video: ['🎬', 'Видео'],
  gif: ['🎞', 'GIF'],
  sticker: ['🧩', 'Стикер'],
  voice: ['🎤', 'Голосовое сообщение'],
  videoNote: ['⏺', 'Видеосообщение'],
  audio: ['🎵', 'Аудио'],
  file: ['📎', 'Файл'],
  poll: ['📊', 'Опрос'],
  other: ['📦', 'Вложение'],
};

/** '📷 Фото · 1280×720 · 245 КБ', '' — если вложения нет. */
export function mediaLabel(m) {
  if (!m || !m.media) return '';
  const [icon, name] = MEDIA[m.media] || MEDIA.other;
  const info = m.mediaInfo || {};
  const parts = [];
  if ((m.media === 'file' || m.media === 'audio') && info.fileName) parts.push(info.fileName);
  if (info.duration) parts.push(formatDuration(info.duration));
  if ((m.media === 'photo' || m.media === 'video' || m.media === 'gif') && info.width && info.height) {
    parts.push(info.width + '×' + info.height);
  }
  if (info.size) parts.push(formatSize(info.size));
  if (info.spoiler) parts.push('под спойлером');
  return icon + ' ' + name + (parts.length ? ' · ' + parts.join(' · ') : '');
}

export function senderName(m) {
  return String(m.fromName || (m.out ? 'Я' : (m.chat && m.chat.name) || 'Без имени'));
}

export function serviceText(m) {
  return String(m.text || '').trim() || 'Служебное сообщение';
}

/** Текст в одну строку для цитаты ответа. */
export function snippet(m, max) {
  const limit = max || 90;
  const text = String(m.text || '').replace(/\s+/g, ' ').trim();
  if (text) return text.length > limit ? text.slice(0, limit - 1) + '…' : text;
  const media = mediaLabel(m);
  return media ? media.split(' · ')[0] : 'сообщение';
}

/** Запись для карты ответов: id → «имя␁текст». */
export function replyEntry(m) {
  return senderName(m) + SNIPPET_SEP + snippet(m);
}

function splitEntry(entry) {
  if (!entry) return null;
  const at = entry.indexOf(SNIPPET_SEP);
  return at < 0 ? { name: '', text: entry } : { name: entry.slice(0, at), text: entry.slice(at + 1) };
}

export function sameSender(a, b) {
  if (!a || !b) return false;
  if (a.out || b.out) return !!(a.out && b.out);
  return !!a.fromId && a.fromId === b.fromId;
}

export const CHAT_TYPES = {
  user: 'личный чат',
  bot: 'бот',
  group: 'группа',
  channel: 'канал',
  unknown: 'чат',
};

export function chatTypeTitle(chat) {
  if (chat && chat.isSelf) return 'Избранное';
  return CHAT_TYPES[(chat && chat.type) || 'unknown'] || 'чат';
}

/** Безопасное имя файла из названия чата. */
export function safeFileName(name, fallback) {
  const clean = String(name || '')
    .replace(/[^\p{L}\p{N}._-]+/gu, '_')
    .replace(/_+/g, '_')
    .replace(/^[_.-]+|[_.-]+$/g, '')
    .slice(0, 60)
    .replace(/[_.-]+$/, '');
  return clean || fallback || 'chat';
}

// --- Текст сообщения ------------------------------------------------------

const SAFE_URL = /^(https?:\/\/|tg:\/\/|mailto:|tel:)/i;

/**
 * Простые ссылки, почта, @упоминания и телефоны, которые клиент нашёл в
 * тексте, превращаются в textLink — format() сделает из них <a href>.
 */
export function linkifyEntities(text, entities) {
  const list = Array.isArray(entities) ? entities : [];
  return list.map((e) => {
    if (!e || !e.length) return e;
    const value = text.slice(e.offset, e.offset + e.length);
    switch (e.type) {
      case 'url':
        return { type: 'textLink', offset: e.offset, length: e.length, url: /^[a-z][a-z0-9+.-]*:\/\//i.test(value) ? value : 'https://' + value };
      case 'email':
        return { type: 'textLink', offset: e.offset, length: e.length, url: 'mailto:' + value };
      case 'mention':
        return { type: 'textLink', offset: e.offset, length: e.length, url: 'https://t.me/' + value.replace(/^@/, '') };
      case 'phone':
        return { type: 'textLink', offset: e.offset, length: e.length, url: 'tel:' + value.replace(/[^\d+]/g, '') };
      default:
        return e;
    }
  });
}

export const escapeHtml = (s) => String(s ?? '')
  .replace(/&/g, '&amp;')
  .replace(/</g, '&lt;')
  .replace(/>/g, '&gt;')
  .replace(/"/g, '&quot;');

/** Оставляет только http(s), tg, mailto и tel — никаких javascript:. */
export function safeLinks(html) {
  return String(html).replace(/<a href="([^"]*)">/g, (all, href) => (SAFE_URL.test(href)
    ? '<a href="' + href + '" target="_blank" rel="noopener noreferrer">'
    : '<a>'));
}

const hasFormat = () => typeof dzg !== 'undefined' && dzg.messages && typeof dzg.messages.format === 'function';

/** Текст сообщения в HTML с жирным, курсивом, кодом, ссылками и спойлерами. */
export function messageHtml(m) {
  const text = String(m.text || '');
  if (!text) return '';
  if (!hasFormat()) return escapeHtml(text);
  try {
    return safeLinks(dzg.messages.format(text, 'html', linkifyEntities(text, m.entities)));
  } catch (e) {
    return escapeHtml(text);
  }
}

const URL_AT = /(?:[a-z][a-z0-9+.-]*:\/\/|www\.)[^\s<>\[\]`\\]+/iy;
// Всё, что может потребовать замены: обычный текст между ними копируется
// целиком, без посимвольного обхода.
const MD_SPECIAL = /[\\`\]_|\n<]|(?<![\p{L}\p{N}])(?:[a-z][a-z0-9+.-]*:\/\/|www\.)/giu;

/**
 * Markdown в стиле Telegram (результат dzg.messages.format) → обычный
 * CommonMark, который понимают GitHub, Obsidian и любые просмотрщики:
 * __курсив__ → *курсив*, ||спойлер|| → текст, переносы строк сохраняются.
 */
export function toCommonMark(source) {
  const s = String(source || '');
  let out = '';
  let i = 0;
  // Начало строки: свёрнутая цитата «**>» и «#», который стал бы заголовком.
  const lineStart = () => {
    if (s.startsWith('**>', i)) {
      out += '>';
      i += 3;
    } else if (s[i] === '#') {
      out += '\\#';
      i += 1;
    }
  };
  lineStart();
  while (i < s.length) {
    MD_SPECIAL.lastIndex = i;
    const found = MD_SPECIAL.exec(s);
    if (!found) {
      out += s.slice(i);
      break;
    }
    if (found.index > i) {
      out += s.slice(i, found.index);
      i = found.index;
    }
    const c = s[i];
    if (c === '\\') {
      out += s.slice(i, i + 2);
      i += 2;
    } else if (c === '`') {
      const fence = s.startsWith('```', i) ? s.indexOf('```', i + 3) : -1;
      const end = (fence >= 0) ? fence + 3 : s.indexOf('`', i + 1) + 1;
      if (end > i + 1) {
        out += s.slice(i, end);
        i = end;
      } else {
        out += c;
        i += 1;
      }
    } else if (c === ']') {
      let j = i + 1;
      if (s[j] === '(') {
        // Адрес ссылки копируется как есть: в нём «__» — не курсив.
        j += 1;
        while (j < s.length && s[j] !== ')') j += (s[j] === '\\') ? 2 : 1;
        j += 1;
      }
      out += s.slice(i, j);
      i = j;
    } else if (c === '_' || c === '|') {
      if (s[i + 1] === c) {
        if (c === '_') out += '*';
        i += 2;
      } else {
        out += c;
        i += 1;
      }
    } else if (c === '\n') {
      out += '  \n';
      i += 1;
      lineStart();
    } else if (c === '<') {
      out += '\\<';
      i += 1;
    } else {
      URL_AT.lastIndex = i;
      const url = URL_AT.exec(s);
      const raw = url ? url[0].replace(/[*_~|]+$/, '') : '';
      const length = (raw && (raw.includes('/') || raw.includes('.'))) ? raw.length : 1;
      out += s.slice(i, i + length);
      i += length;
    }
  }
  return out;
}

/** Экранирование имён и подписей для Markdown. */
export const mdEscape = (s) => String(s ?? '').replace(/[\\`*_[\]<>#|~]/g, '\\$&').replace(/\n/g, ' ');

/** Текст сообщения в Markdown (CommonMark). */
export function messageMarkdown(m) {
  const text = String(m.text || '');
  if (!text) return '';
  if (!hasFormat()) return toCommonMark(text.replace(/[\\`*_[\]~|]/g, '\\$&'));
  try {
    return toCommonMark(dzg.messages.format(text, 'markdown', m.entities || []));
  } catch (e) {
    return toCommonMark(text.replace(/[\\`*_[\]~|]/g, '\\$&'));
  }
}

function reactionsLine(m) {
  return (Array.isArray(m.reactions) ? m.reactions : [])
    .filter((r) => r && r.count)
    .map((r) => ((r.paid ? '⭐' : (r.customEmojiId || /^\d+$/.test(String(r.emoji)) ? '✨' : r.emoji)) + ' ' + r.count))
    .join(' · ');
}

// --- Писатели -------------------------------------------------------------

class Writer {
  /**
   * ctx: { chat, link(id) → url | null, replies: Map<id, string>, theme }
   */
  constructor(ctx) {
    this.ctx = ctx;
    this.chunks = [];
    this.patches = [];
    this.chars = 0;
  }

  push(text) {
    this.chunks.push(text);
    this.chars += text.length;
    return this.chunks.length - 1;
  }

  /** Подставить цитаты ответов вместо меток и развернуть в хронологию. */
  ordered(renderReply) {
    for (const patch of this.patches) {
      this.chunks[patch.index] = this.chunks[patch.index].replace(REPLY_TOKEN, renderReply(patch.id));
    }
    this.patches = [];
    return this.chunks.reverse();
  }
}

/** JSON: шапка с информацией о чате и массив объектов Message. */
export class JsonWriter extends Writer {
  add(m) {
    const { chat, ...rest } = m;
    this.push(JSON.stringify(rest));
  }

  finish(meta) {
    const lines = this.ordered(() => '');
    const chat = meta.chat || {};
    const info = meta.info || null;
    const header = {
      exportedBy: 'DamZaGram',
      plugin: 'chat-export',
      exportedAt: new Date(meta.exportedAt || Date.now()).toISOString(),
      chat: {
        id: chat.id,
        tgId: chat.tgId,
        name: chat.name,
        username: chat.username || '',
        type: chat.type,
        isSelf: !!chat.isSelf,
      },
      filter: meta.filter || {},
      count: lines.length,
      first: meta.first || 0,
      last: meta.last || 0,
      complete: !meta.truncated,
    };
    if (info) {
      header.chat.about = info.about || '';
      header.chat.membersCount = info.membersCount || 0;
      if (info.linkedChat) header.chat.linkedChat = info.linkedChat;
    }
    if (meta.truncated) header.truncated = meta.truncated;
    const doc = JSON.stringify(Object.assign(header, { messages: [] }), null, 2);
    const marker = '"messages": []';
    const at = doc.lastIndexOf(marker);
    const body = lines.length ? '"messages": [\n    ' + lines.join(',\n    ') + '\n  ]' : marker;
    return doc.slice(0, at) + body + doc.slice(at + marker.length) + '\n';
  }
}

/** Markdown: читаемая расшифровка с датами, авторами, ответами и медиа. */
export class MarkdownWriter extends Writer {
  add(m, prev) {
    const ctx = this.ctx;
    const time = formatTime(m.date);
    if (m.service) {
      this.push('*— ' + mdEscape(serviceText(m)) + ' · ' + time + ' —*\n');
    } else {
      const link = ctx.link(m.id);
      const flags = [];
      if (m.editDate) flags.push('✏️ изменено');
      if (m.deleted) flags.push('🗑 удалено');
      if (m.pinned) flags.push('📌');
      if (m.views) flags.push('👁 ' + formatNumber(m.views));
      let head = '**' + mdEscape(senderName(m)) + '** · ' + (link ? '[' + time + '](' + link + ')' : time);
      if (flags.length) head += ' · ' + flags.join(' · ');
      if (m.forwardFrom) head += '  \n↪ Переслано от **' + mdEscape(m.forwardFrom.name || 'скрытого автора') + '**';
      const blocks = [head];
      if (m.replyToId) {
        blocks.push(REPLY_TOKEN);
      }
      const text = messageMarkdown(m);
      if (text) blocks.push(text);
      const extra = [];
      const media = mediaLabel(m);
      if (media) extra.push('*' + mdEscape(media) + '*');
      const reactions = reactionsLine(m);
      if (reactions) extra.push(reactions);
      const buttons = (m.buttons || []).flat().filter((b) => b && b.text);
      if (buttons.length) {
        extra.push(buttons.map((b) => (b.url && SAFE_URL.test(b.url)
          ? '[' + mdEscape(b.text) + '](' + b.url.replace(/[()\\]/g, '\\$&') + ')'
          : '`' + String(b.text).replace(/`/g, "'") + '`')).join(' · '));
      }
      if (extra.length) blocks.push(extra.join('  \n'));
      const index = this.push(blocks.join('\n\n') + '\n');
      if (m.replyToId) this.patches.push({ index, id: m.replyToId });
    }
    if (!prev || dayKey(prev.date) !== dayKey(m.date)) this.push('## ' + formatDay(m.date) + '\n');
  }

  reply(id) {
    const entry = splitEntry(this.ctx.replies.get(id));
    if (!entry) {
      const link = this.ctx.link(id);
      return '> ↩ ' + (link ? '[ответ на сообщение](' + link + ')' : 'ответ на сообщение #' + id);
    }
    return '> ↩ **' + mdEscape(entry.name) + ':** ' + mdEscape(entry.text);
  }

  finish(meta) {
    const lines = this.ordered((id) => this.reply(id));
    const chat = meta.chat || {};
    const info = meta.info || null;
    const head = ['# ' + mdEscape(chat.name || 'Чат'), ''];
    const sub = [];
    if (chat.username) sub.push('@' + mdEscape(chat.username));
    sub.push(chatTypeTitle(chat));
    if (info && info.membersCount) sub.push(formatNumber(info.membersCount) + ' ' + plural(info.membersCount, 'участник', 'участника', 'участников'));
    head.push(sub.join(' · '), '');
    if (info && info.about) head.push('> ' + mdEscape(info.about), '');
    head.push('- Сообщений: **' + formatNumber(meta.count || 0) + '**');
    if (meta.first) head.push('- Период: ' + formatDate(meta.first) + ' — ' + formatDate(meta.last));
    head.push('- Экспортировано DamZaGram ' + formatDateTime((meta.exportedAt || Date.now()) / 1000));
    if (meta.truncated) head.push('- ⚠️ ' + mdEscape(meta.truncated));
    head.push('', '---', '');
    const empty = lines.length ? [] : ['*Нет сообщений для экспорта.*', ''];
    const foot = ['---', '', '*Экспортировано DamZaGram · плагин «Экспорт чата»*', ''];
    return head.join('\n') + '\n' + empty.join('\n') + lines.join('\n') + (lines.length ? '\n' : '') + foot.join('\n');
  }
}

// --- HTML -----------------------------------------------------------------

const NAME_COLORS = 7;

function colorIndex(key) {
  let h = 0;
  const s = String(key || '');
  for (let i = 0; i < s.length; ++i) h = (h * 31 + s.charCodeAt(i)) | 0;
  return Math.abs(h) % NAME_COLORS;
}

function initials(name) {
  const words = String(name || '?').trim().split(/\s+/).filter(Boolean);
  const letters = words.slice(0, 2).map((w) => Array.from(w)[0] || '').join('');
  return (letters || '?').toUpperCase();
}

const LIGHT = '--bg:#f3f4ef;--surface:#ffffff;--bubble:#ffffff;--bubble-out:#eef8cf;--text:#16181b;--muted:#6b7178;'
  + '--line:#e1e4dc;--soft:#f0f2ea;--link:#3f6a00;--accent-ink:#4c6d00;--shadow:0 1px 2px rgba(20,24,16,.07);'
  + '--n0:#c0392b;--n1:#c25400;--n2:#8e44ad;--n3:#1f6fa8;--n4:#11806b;--n5:#9a7d00;--n6:#c2185b;color-scheme:light;';
const DARK = '--bg:#0e0f11;--surface:#16181b;--bubble:#1b1e22;--bubble-out:#283114;--text:#e9ebe6;--muted:#8e949b;'
  + '--line:#272a2f;--soft:#23262b;--link:#d4f55a;--accent-ink:#C8F135;--shadow:0 1px 2px rgba(0,0,0,.35);'
  + '--n0:#ff7b72;--n1:#ffa657;--n2:#d2a8ff;--n3:#79c0ff;--n4:#56d4bc;--n5:#e3c75a;--n6:#ff8fc8;color-scheme:dark;';

export const HTML_STYLE = ':root{--accent:' + ACCENT + ';' + LIGHT + '}'
  + '@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){' + DARK + '}}'
  + ':root[data-theme="dark"]{' + DARK + '}'
  + '*{box-sizing:border-box}'
  + 'html{-webkit-text-size-adjust:100%}'
  + 'body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif}'
  + '.wrap{max-width:780px;margin:0 auto;padding:20px 16px 40px}'
  + '.brand{display:flex;align-items:center;gap:8px;font-size:11px;font-weight:700;letter-spacing:.12em;text-transform:uppercase;color:var(--muted);margin:0 0 14px}'
  + '.brand i{width:10px;height:10px;border-radius:3px;background:var(--accent);display:inline-block}'
  + '.head{display:flex;gap:14px;align-items:center;background:var(--surface);border:1px solid var(--line);border-radius:18px;padding:16px;box-shadow:var(--shadow)}'
  + '.ava{width:54px;height:54px;border-radius:50%;flex:none;display:grid;place-items:center;font-weight:800;font-size:19px;color:#121410;background:var(--accent)}'
  + '.head h1{margin:0;font-size:20px;line-height:1.25;overflow-wrap:anywhere}'
  + '.sub{color:var(--muted);font-size:13px;margin-top:3px}'
  + '.about{margin:12px 2px 0;white-space:pre-wrap;font-size:14px;color:var(--muted);overflow-wrap:anywhere}'
  + '.chips{display:flex;flex-wrap:wrap;gap:6px;margin:12px 0 4px}'
  + '.chips span{background:var(--surface);border:1px solid var(--line);border-radius:999px;padding:3px 10px;font-size:12px;color:var(--muted)}'
  + '.chips .warn{border-color:var(--accent);color:var(--text)}'
  + '.day{position:sticky;top:8px;z-index:2;text-align:center;margin:20px 0 10px}'
  + '.day span{display:inline-block;background:var(--surface);border:1px solid var(--line);border-radius:999px;padding:3px 12px;font-size:12px;font-weight:600;color:var(--muted);box-shadow:var(--shadow)}'
  + '.msg{display:flex;margin:3px 0}'
  + '.msg.first{margin-top:12px}'
  + '.msg.out{justify-content:flex-end}'
  + '.bubble{max-width:min(580px,86%);min-width:72px;background:var(--bubble);border:1px solid var(--line);border-radius:16px;padding:7px 12px 6px;box-shadow:var(--shadow);overflow-wrap:anywhere}'
  + '.out .bubble{background:var(--bubble-out);border-color:transparent}'
  + '.post .bubble{max-width:100%;width:100%}'
  + '.deleted .bubble{border-style:dashed;opacity:.8}'
  + '.msg:target .bubble{outline:2px solid var(--accent);outline-offset:2px}'
  + '.from{font-weight:650;font-size:13.5px;margin-bottom:2px}'
  + '.from.me{color:var(--accent-ink)}'
  + '.c0{color:var(--n0)}.c1{color:var(--n1)}.c2{color:var(--n2)}.c3{color:var(--n3)}.c4{color:var(--n4)}.c5{color:var(--n5)}.c6{color:var(--n6)}'
  + '.fwd{font-size:12.5px;color:var(--muted);margin-bottom:3px}'
  + '.fwd b{color:var(--text)}'
  + '.reply{display:block;border-left:3px solid var(--accent);background:var(--soft);border-radius:6px;padding:3px 8px;margin:2px 0 6px;font-size:13px;color:inherit;text-decoration:none;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}'
  + '.reply b{display:block;font-size:12.5px;color:var(--accent-ink)}'
  + '.media{display:inline-block;background:var(--soft);border-radius:9px;padding:5px 10px;font-size:13px;margin:2px 0 4px}'
  + '.text{white-space:pre-wrap}'
  + '.text a,.btns a{color:var(--link)}'
  + '.meta{display:flex;justify-content:flex-end;flex-wrap:wrap;gap:8px;font-size:11.5px;color:var(--muted);margin-top:2px}'
  + '.meta a{color:inherit;text-decoration:none}'
  + '.meta a:hover{text-decoration:underline}'
  + '.reacts{display:flex;flex-wrap:wrap;gap:4px;margin-top:6px}'
  + '.reacts span{background:var(--soft);border-radius:999px;padding:1px 8px;font-size:12.5px}'
  + '.reacts .me{box-shadow:inset 0 0 0 1.5px var(--accent)}'
  + '.btns{display:grid;gap:4px;margin-top:6px}'
  + '.btns div{display:flex;gap:4px}'
  + '.btns a,.btns span{flex:1;text-align:center;background:var(--soft);border-radius:8px;padding:5px 8px;font-size:13px;text-decoration:none}'
  + '.service{text-align:center;margin:12px 0}'
  + '.service span{display:inline-block;max-width:90%;background:var(--surface);border:1px solid var(--line);border-radius:999px;padding:3px 12px;font-size:12.5px;color:var(--muted)}'
  + 'code,pre{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:13px;background:var(--soft);border-radius:5px}'
  + 'code{padding:1px 4px}'
  + 'pre{display:block;padding:8px 10px;margin:4px 0;white-space:pre-wrap;overflow-x:auto}'
  + 'pre code{padding:0;background:none}'
  + 'blockquote{margin:4px 0;padding:2px 10px;border-left:3px solid var(--accent);background:var(--soft);border-radius:4px}'
  + 'tg-spoiler{background:var(--muted);color:transparent;border-radius:4px;cursor:pointer;transition:background .2s,color .2s}'
  + 'tg-spoiler:hover,tg-spoiler:focus{background:transparent;color:inherit}'
  + '.empty{text-align:center;color:var(--muted);margin:40px 0}'
  + 'footer{margin-top:32px;padding-top:16px;border-top:1px solid var(--line);text-align:center;color:var(--muted);font-size:12px}'
  + 'footer b{color:var(--text)}'
  + 'footer i{display:inline-block;width:8px;height:8px;border-radius:2px;background:var(--accent);margin-right:6px;vertical-align:middle}'
  + '@media (max-width:540px){.bubble{max-width:94%}.wrap{padding:14px 10px 32px}}'
  + '@media print{.day{position:static}.bubble{box-shadow:none}}';

/** HTML: одна самодостаточная страница со стилями внутри. */
export class HtmlWriter extends Writer {
  add(m, prev) {
    const ctx = this.ctx;
    const newDay = !prev || dayKey(prev.date) !== dayKey(m.date);
    if (m.service) {
      this.push('<div class="service" id="m' + m.id + '"><span>' + escapeHtml(serviceText(m)) + ' · ' + formatTime(m.date) + '</span></div>');
    } else {
      const grouped = !newDay && prev && !prev.service && !m.forwardFrom
        && sameSender(prev, m) && m.date - prev.date < 300;
      const classes = ['msg', m.out ? 'out' : 'in'];
      if (!grouped) classes.push('first');
      if (m.post && !m.out) classes.push('post');
      if (m.deleted) classes.push('deleted');
      let html = '<div class="' + classes.join(' ') + '" id="m' + m.id + '"><div class="bubble">';
      if (!grouped) {
        const name = senderName(m);
        html += '<div class="from ' + (m.out ? 'me' : 'c' + colorIndex(m.fromId || name)) + '">' + escapeHtml(name) + '</div>';
      }
      if (m.forwardFrom) {
        html += '<div class="fwd">↪ Переслано от <b>' + escapeHtml(m.forwardFrom.name || 'скрытого автора') + '</b>'
          + (m.forwardFrom.date ? ' · ' + formatDateTime(m.forwardFrom.date) : '') + '</div>';
      }
      if (m.replyToId) html += REPLY_TOKEN;
      const media = mediaLabel(m);
      if (media) html += '<div class="media">' + escapeHtml(media) + '</div>';
      const text = messageHtml(m);
      if (text) html += '<div class="text">' + text + '</div>';
      const buttons = Array.isArray(m.buttons) ? m.buttons.filter((row) => Array.isArray(row) && row.length) : [];
      if (buttons.length) {
        html += '<div class="btns">' + buttons.map((row) => '<div>' + row.map((b) => (b.url && SAFE_URL.test(b.url)
          ? '<a href="' + escapeHtml(b.url) + '" target="_blank" rel="noopener noreferrer">' + escapeHtml(b.text) + '</a>'
          : '<span>' + escapeHtml(b.text) + '</span>')).join('') + '</div>').join('') + '</div>';
      }
      const reactions = (Array.isArray(m.reactions) ? m.reactions : []).filter((r) => r && r.count);
      if (reactions.length) {
        html += '<div class="reacts">' + reactions.map((r) => '<span' + (r.chosen ? ' class="me"' : '') + '>'
          + escapeHtml(r.paid ? '⭐' : (r.customEmojiId || /^\d+$/.test(String(r.emoji)) ? '✨' : r.emoji)) + ' ' + r.count + '</span>').join('') + '</div>';
      }
      const meta = [];
      if (m.pinned) meta.push('<span title="Закреплено">📌</span>');
      if (m.deleted) meta.push('<span>удалено</span>');
      if (m.editDate) meta.push('<span title="' + escapeHtml(formatDateTime(m.editDate)) + '">изменено</span>');
      if (m.views) meta.push('<span>👁 ' + formatNumber(m.views) + '</span>');
      const link = ctx.link(m.id);
      const stamp = '<span title="' + escapeHtml(formatDateTime(m.date)) + '">' + formatTime(m.date) + '</span>';
      meta.push(link ? '<a href="' + escapeHtml(link) + '" target="_blank" rel="noopener noreferrer">' + stamp + '</a>' : stamp);
      html += '<div class="meta">' + meta.join('') + '</div></div></div>';
      const index = this.push(html);
      if (m.replyToId) this.patches.push({ index, id: m.replyToId });
    }
    if (newDay) this.push('<div class="day"><span>' + escapeHtml(formatDay(m.date)) + '</span></div>');
  }

  reply(id) {
    const entry = splitEntry(this.ctx.replies.get(id));
    if (entry) {
      return '<a class="reply" href="#m' + id + '"><b>' + escapeHtml(entry.name) + '</b>' + escapeHtml(entry.text) + '</a>';
    }
    const link = this.ctx.link(id);
    return link
      ? '<a class="reply" href="' + escapeHtml(link) + '" target="_blank" rel="noopener noreferrer">↩ Ответ на сообщение вне экспорта</a>'
      : '<span class="reply">↩ Ответ на сообщение #' + id + '</span>';
  }

  finish(meta) {
    const lines = this.ordered((id) => this.reply(id));
    const chat = meta.chat || {};
    const info = meta.info || null;
    const theme = (meta.theme === 'light' || meta.theme === 'dark') ? ' data-theme="' + meta.theme + '"' : '';
    const sub = [];
    if (chat.username) sub.push('@' + chat.username);
    sub.push(chatTypeTitle(chat));
    if (info && info.membersCount) sub.push(formatNumber(info.membersCount) + ' ' + plural(info.membersCount, 'участник', 'участника', 'участников'));
    const chips = ['<span>' + formatNumber(meta.count || 0) + ' ' + messagesWord(meta.count || 0) + '</span>'];
    if (meta.first) chips.push('<span>' + formatDate(meta.first) + ' — ' + formatDate(meta.last) + '</span>');
    if (meta.truncated) chips.push('<span class="warn">⚠️ ' + escapeHtml(meta.truncated) + '</span>');
    const exported = formatDateTime((meta.exportedAt || Date.now()) / 1000);
    return '<!doctype html>\n<html lang="ru"' + theme + '>\n<head>\n<meta charset="utf-8">\n'
      + '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
      + '<meta name="generator" content="DamZaGram chat-export">\n'
      + '<title>' + escapeHtml(chat.name || 'Чат') + ' — экспорт DamZaGram</title>\n'
      + '<style>' + HTML_STYLE + '</style>\n</head>\n<body>\n<div class="wrap">\n'
      + '<div class="brand"><i></i>DamZaGram · экспорт чата</div>\n'
      + '<header class="head"><div class="ava">' + escapeHtml(initials(chat.name)) + '</div><div>'
      + '<h1>' + escapeHtml(chat.name || 'Чат') + '</h1><div class="sub">' + escapeHtml(sub.join(' · ')) + '</div></div></header>\n'
      + (info && info.about ? '<div class="about">' + escapeHtml(info.about) + '</div>\n' : '')
      + '<div class="chips">' + chips.join('') + '</div>\n<main>\n'
      + (lines.length ? lines.join('\n') : '<div class="empty">Нет сообщений для экспорта.</div>')
      + '\n</main>\n<footer><i></i>Экспортировано <b>DamZaGram</b> · ' + escapeHtml(exported) + ' · плагин «Экспорт чата»</footer>\n'
      + '</div>\n</body>\n</html>\n';
  }
}

export const WRITERS = { html: HtmlWriter, md: MarkdownWriter, json: JsonWriter };
