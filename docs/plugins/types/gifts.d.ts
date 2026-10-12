// Типы API плагинов DamZaGram — модуль gifts (apiVersion 2).
// Перехват действия «Предложить оффер» у уникальных (NFT) подарков.
// Исходник: Telegram/SourceFiles/damza/plugins/api/api_gifts.cpp.
// Автор: @wetakush (https://t.me/wetakush).

/** Оффер на покупку NFT-подарка: что выбрал пользователь в окне. */
interface GiftOffer {
  /** slug подарка (часть ссылки t.me/nft/<slug>). */
  slug: string;
  /** Ссылка на подарок: 'https://t.me/nft/<slug>'. */
  link: string;
  /** Название подарка, например 'Plush Pepe #123'. */
  title: string;
  /** Сумма из окна десятичной строкой: '100' (Stars) или '2.5' (TON). */
  amount: string;
  /** Та же сумма числом. */
  amountNumber: number;
  /** Валюта из окна: 'ton' (бот ждёт 'gram') или 'stars'. */
  currency: 'ton' | 'stars';
  /** Срок действия оффера в секундах (0 — без срока). */
  duration: number;
  /** id владельца подарка (Peer id). */
  ownerId: PeerId;
  /** @username владельца без @ ('' если нет). */
  ownerUsername: string;
  /** Имя владельца. */
  ownerName: string;
}

interface DzgGifts {
  /**
   * Перехватить «Предложить оффер» у NFT-подарков. Пока есть хотя бы один
   * обработчик, клиент показывает то же окно (сумма и валюта), а на
   * подтверждении вызывает обработчик вместо отправки реального оффера.
   * Возвращает функцию отписки.
   */
  onOffer(handler: (offer: GiftOffer) => void): () => void;
  /** Закрыть окна подарка и профиля, вернуться к чату. */
  close(): void;
}

interface Dzg {
  /** NFT-подарки: перехват «Предложить оффер». */
  gifts: DzgGifts;
}
