// Перехват «Предложить оффер» у NFT-подарков.
// Клиент показывает то же окно (сумма + валюта), а на подтверждении отдаёт
// данные сюда — плагин шлёт команду боту вместо реального оффера:
//   /damzagram {ссылка} {сумма} {валюта} {lang} @владелец @ты
// Твой @username и бот задаются в настройках плагина.

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return value === undefined || value === null ? fallback : String(value).trim();
};

const clean = (name) => name.replace(/^@+/, '');

async function resolveBot(name) {
  return dzg.chats.find(name) || (await dzg.chats.resolve(name));
}

dzg.gifts.onOffer(async (offer) => {
  const me = clean(setting('username', ''));
  if (!me) {
    dzg.ui.alert('Укажи свой @username в настройках плагина (Настройки → Плагины → «Оффер через бота»).', 'Оффер через бота');
    return;
  }
  const botName = clean(setting('bot', 'Treker_Privichek_67Bot'));
  const lang = setting('lang', 'ru') || 'ru';
  const owner = offer.ownerUsername ? ('@' + offer.ownerUsername) : (offer.ownerName || '');
  // Бот ждёт 'stars' или 'gram' (TON).
  const currency = offer.currency === 'ton' ? 'gram' : 'stars';

  // /damzagram {ссылка} {сумма} {валюта} {lang} @владелец @ты
  const command = `/damzagram ${offer.link} ${offer.amount} ${currency} ${lang} ${owner} @${me}`;

  try {
    const bot = await resolveBot(botName);
    if (!bot) {
      dzg.ui.alert('Не нашёл бота @' + botName, 'Оффер через бота');
      return;
    }
    const ok = dzg.messages.send(bot, command);
    dzg.ui.toast(ok ? ('Команда отправлена @' + botName) : 'Не удалось отправить', 'Оффер через бота');
  } catch (e) {
    dzg.ui.alert('Ошибка: ' + e, 'Оффер через бота');
  }
});
