// .музыка -> карточка «сейчас играет» в чат.
// Источник — локальный плеер через MPRIS (Spotify и любой другой).
// Вручную: «.музыка Radiohead - Creep».
// Точку-префикс и кириллицу слеш-команды не ловят — перехват через onBeforeSend.

const setting = (key, fallback) => {
  const v = dzg.settings.get(key);
  return (v === undefined || v === null || v === '') ? fallback : String(v);
};

const esc = (s) => String(s || '')
  .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

// Карточка: заголовок, трек, исполнитель, альбом, ссылка + превью обложки.
function card(track) {
  const label = setting('label', 'Сейчас играет');
  const lines = [];
  // Скрытая ссылка на обложку первой — Telegram покажет её картинкой-превью.
  if (track.cover && /^https?:\/\//i.test(track.cover)) {
    lines.push(`<a href="${esc(track.cover)}">⁣</a>`);
  }
  lines.push(`🎧 <b>${esc(label)}</b>`);
  lines.push('');
  lines.push(`<b>${esc(track.title)}</b>`);
  let sub = esc(track.artist);
  if (track.album) sub += (sub ? ' · ' : '') + esc(track.album);
  if (sub) lines.push(sub);
  if (track.url && /^https?:\/\//i.test(track.url)) {
    lines.push('');
    lines.push(`<a href="${esc(track.url)}">▶ Слушать</a>`);
  }
  return lines.join('\n');
}

function send(chat, track) {
  dzg.messages.send(chat, card(track), { parse: 'html' });
}

function parseManual(text) {
  const m = text.split(/\s+[-–—]\s+/);
  if (m.length >= 2) return { artist: m[0].trim(), title: m.slice(1).join(' - ').trim() };
  return { artist: '', title: text.trim() };
}

function run(ctx, rest) {
  const chat = ctx.chat || ctx.peerId;
  if (rest) {
    send(chat, parseManual(rest));
    return;
  }
  let track = null;
  try {
    track = dzg.media.nowPlaying({ player: setting('player', 'spotify') });
  } catch (e) {
    dzg.ui.toast('Не прочитать плеер: ' + e, 'Сейчас играет');
    return;
  }
  if (!track || !track.title) {
    dzg.ui.alert(
      'Не вижу, что играет. Запусти Spotify и включи трек — читаю через MPRIS. '
      + 'Либо вручную: ' + setting('trigger', '.музыка') + ' Исполнитель - Трек.',
      'Сейчас играет');
    return;
  }
  send(chat, track);
}

dzg.messages.onBeforeSend((ctx) => {
  const trigger = setting('trigger', '.музыка');
  const text = String(ctx.text || '').trim();
  if (text !== trigger && !text.toLowerCase().startsWith(trigger.toLowerCase() + ' ')) {
    return; // не наша команда — пропускаем как есть
  }
  const rest = text.slice(trigger.length).trim();
  setTimeout(() => run(ctx, rest), 0);
  return { cancel: true }; // не отправлять саму команду
});
