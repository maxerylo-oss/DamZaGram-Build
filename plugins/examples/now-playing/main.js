// .музыка -> красивая карточка-картинка с тем, что играет, в чат.
// Источник — локальный плеер через MPRIS (Spotify и любой другой).
// Карточка рисуется нативно (dzg.media.renderCard) и шлётся как фото.
// Вручную: «.музыка Radiohead - Creep».
// Точку-префикс и кириллицу слеш-команды не ловят — перехват через onBeforeSend.

const setting = (key, fallback) => {
  const v = dzg.settings.get(key);
  return (v === undefined || v === null || v === '') ? fallback : String(v);
};

const esc = (s) => String(s || '')
  .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

// Подпись под картинкой: исполнитель — трек + кнопка Слушать.
function caption(track) {
  const parts = [];
  const line = [esc(track.artist), esc(track.title)].filter(Boolean).join(' — ');
  if (line) parts.push(`🎧 ${line}`);
  if (track.url && /^https?:\/\//i.test(track.url)) {
    parts.push(`<a href="${esc(track.url)}">▶ Слушать</a>`);
  }
  return parts.join('\n');
}

// Текстовый фолбэк, если картинка не отрисовалась.
function textCard(track) {
  const lines = [`🎧 <b>${esc(setting('label', 'Сейчас играет'))}</b>`, ''];
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

function parseManual(text) {
  const m = text.split(/\s+[-–—]\s+/);
  if (m.length >= 2) return { artist: m[0].trim(), title: m.slice(1).join(' - ').trim() };
  return { artist: '', title: text.trim() };
}

async function run(ctx, rest) {
  const chat = ctx.chat || ctx.peerId;
  let track;
  if (rest) {
    track = parseManual(rest);
  } else {
    try {
      track = dzg.media.nowPlaying({ player: setting('player', 'spotify') });
    } catch (e) {
      dzg.ui.toast('Не прочитать плеер: ' + e, 'Сейчас играет');
      return;
    }
  }
  if (!track || !track.title) {
    dzg.ui.alert(
      'Не вижу, что играет. Запусти Spotify и включи трек — читаю через MPRIS. '
      + 'Либо вручную: ' + setting('trigger', '.музыка') + ' Исполнитель - Трек.',
      'Сейчас играет');
    return;
  }
  track.label = setting('label', 'Сейчас играет');
  try {
    const path = await dzg.media.renderCard(track);
    dzg.messages.sendFile(chat, path, { caption: caption(track), parse: 'html', album: false });
  } catch (e) {
    dzg.messages.send(chat, textCard(track), { parse: 'html' });
  }
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
