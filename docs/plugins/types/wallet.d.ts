// Типы API плагинов DamZaGram — модуль wallet (apiVersion 2).
// Балансы Telegram Stars и TON активного аккаунта.
// Исходник: Telegram/SourceFiles/damza/plugins/api/api_wallet.cpp.
// Автор: @wetakush (https://t.me/wetakush).

/** Балансы активного аккаунта, как их сейчас показывает клиент. */
interface WalletBalances {
  /** Звёзды Telegram Stars (дробная часть — нано-звёзды). */
  stars: number;
  /** TON. */
  ton: number;
  /** Клиент уже получил баланс Stars с сервера. */
  starsLoaded: boolean;
  /** Клиент уже получил баланс TON с сервера. */
  tonLoaded: boolean;
}

/** Фейковая запись для истории транзакций Stars (только отображение). */
interface WalletHistoryEntry {
  /** Название записи (имя сверху). */
  title: string;
  /** Описание под названием. */
  description?: string;
  /** Сумма. */
  amount: number;
  /** Валюта, по умолчанию 'stars'. */
  currency?: 'ton' | 'stars';
  /** Чей аватар показать (Peer или id); по умолчанию твой. */
  peer?: Peer | PeerId;
  /** true (+, по умолчанию) — поступление, false (−) — списание. */
  incoming?: boolean;
  /** Unix-время записи в секундах; по умолчанию сейчас. */
  date?: number;
  /** Свой id записи (иначе сгенерируется). */
  id?: string;
}

interface DzgWallet {
  /** Текущие балансы; null — нет активного аккаунта. */
  get(): WalletBalances | null;
  /**
   * Поменять баланс только в этом клиенте (визуально). На сервере ничего не
   * меняется: следующее обновление с сервера или refresh() вернёт настоящий
   * баланс, после него придёт 'wallet.changed'. Отрицательные и нечисловые
   * значения — RangeError.
   */
  setLocal(values: { stars?: number; ton?: number }): boolean;
  /** Перезапросить настоящие балансы с сервера. */
  refresh(): boolean;
  /**
   * Добавить фейковые записи в начало списка транзакций Stars (Настройки →
   * Звёзды). Только отображение у тебя; на сервере ничего нет. Пустой
   * массив убирает записи. Возвращает число записей.
   */
  setHistory(entries: WalletHistoryEntry[]): number;
}

interface Dzg {
  /** Балансы Stars и TON. */
  wallet: DzgWallet;
}

interface EventMap {
  /**
   * Баланс Stars или TON активного аккаунта изменился (с сервера или через
   * setLocal). Приходит на следующей итерации цикла событий.
   */
  'wallet.changed': WalletBalances;
}
