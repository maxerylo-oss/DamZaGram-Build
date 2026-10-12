// Фильтр сообщений: вернуть true — сообщение будет скрыто.

const BUILTIN = [/\berid\b/i, /#реклама/i, /на правах рекламы/i, /\bреклама\.?\s*(ООО|ИП)\b/i];

let patterns = [];
let hidden = 0;

function compile() {
  const extra = String(dzg.settings.get('words') || '')
    .split('\n')
    .map((s) => s.trim())
    .filter(Boolean)
    .map((s) => {
      const m = /^\/(.+)\/([a-z]*)$/.exec(s);
      if (m) {
        try { return new RegExp(m[1], m[2] || 'i'); } catch (e) { console.warn('плохая регулярка', s); return null; }
      }
      return new RegExp(s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'i');
    })
    .filter(Boolean);
  patterns = BUILTIN.concat(extra);
}

compile();

dzg.messages.addFilter((message) => {
  if (message.out || message.service || !message.text) return false;
  if (dzg.settings.get('onlyChannels') && message.chat.type !== 'channel') return false;
  const ad = patterns.some((re) => re.test(message.text));
  if (ad) hidden++;
  return ad;
});

dzg.on('settings.changed', () => {
  compile();
  dzg.messages.refilter();
});

dzg.ui.addChatMenuItem({
  title: () => `Антиреклама: скрыто ${hidden}`,
  when: (chat) => chat.type === 'channel',
  onClick() { dzg.ui.toast(`За эту сессию скрыто постов: ${hidden}`); },
});
