// Reactions Total — суммарное число реакций в строке под постом,
// рядом с просмотрами и пересылками. Пассивный плагин: просто включает
// нативную отрисовку и выключает её при выгрузке.

dzg.reactions.showTotal(true);

dzg.on('unload', () => {
  dzg.reactions.showTotal(false);
});
