// .музыка -> карточка «сейчас играет» в чат.
// Источник — Last.fm (ник + бесплатный API-ключ в настройках).
// Вручную: «.музыка Radiohead - Creep».

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
  if (track.cover) lines.push(`<a href="${esc(track.cover)}">⁣</a>`);
  lines.push(`🎧 <b>${esc(label)}</b>`);
  lines.push('');
  lines.push(`<b>${esc(track.title)}</b>`);
  let sub = esc(track.artist);
  if (track.album) sub += (sub ? ' · ' : '') + esc(track.album);
  if (sub) lines.push(sub);
  if (track.url) { lines.push(''); lines.push(`<a href="${esc(track.url)}">▶ Слушать</a>`); }
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

async function lastfmNowPlaying() {
  const user = setting('lastfmUser', '');
  const key = setting('lastfmKey', '');
  if (!user || !key) return null;
  const url = 'https://ws.audioscrobbler.com/2.0/?method=user.getrecenttracks'
    + '&user=' + encodeURIComponent(user)
    + '&api_key=' + encodeURIComponent(key)
    + '&format=json&limit=1&extended=1';
  const res = await dzg.http.fetch(url);
  const data = await res.json();
  const t = data && data.recenttracks && data.recenttracks.track;
  const track = Array.isArray(t) ? t[0] : t;
  if (!track) return null;
  const playing = track['@attr'] && track['@attr'].nowplaying === 'true';
  const images = track.image || [];
  const cover = (images[images.length - 1] || {})['#text'] || '';
  return {
    title: track.name || '',
    artist: (track.artist && (track.artist.name || track.artist['#text'])) || '',
    album: (track.album && track.album['#text']) || '',
    url: track.url || '',
    cover,
    playing,
  };
}

async function run(ctx, rest) {
  const chat = ctx.chat || ctx.peerId;
  if (rest) {
    send(chat, parseManual(rest));
    return;
  }
  let track;
  try {
    track = await lastfmNowPlaying();
  } catch (e) {
    dzg.ui.toast('Last.fm недоступен: ' + e, 'Сейчас играет');
    return;
  }
  if (!track) {
    dzg.ui.alert(
      'Укажи ник и API-ключ Last.fm в настройках плагина (ключ бесплатно на last.fm/api), '
      + 'или пиши вручную: ' + setting('trigger', '.музыка') + ' Исполнитель - Трек.',
      'Сейчас играет');
    return;
  }
  if (!track.playing && setting('label', 'Сейчас играет') === 'Сейчас играет') {
    track = Object.assign({}, track);
    // ничего не меняем в тексте — просто отправим последнее прослушанное
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
