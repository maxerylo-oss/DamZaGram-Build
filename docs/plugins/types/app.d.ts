// Типы API плагинов DamZaGram — модуль app (apiVersion 2).
// Дополняет dzg.app из core.d.ts: язык интерфейса, ночной режим, перезапуск.
// Исходник: Telegram/SourceFiles/damza/plugins/api/api_app.cpp.
// Автор: @wetakush (https://t.me/wetakush).

/** Язык интерфейса клиента. */
interface AppLanguage {
  /** Код языка: 'ru', 'en', 'de'... Для своих языковых пакетов — их id. */
  id: string;
  /** Базовый язык пакета ('' если нет): для 'ru-custom' — 'ru'. */
  baseId: string;
  /** Название языка на нём самом: 'Русский', 'English'. */
  name: string;
}

interface AppInfo {
  /** Текущий язык интерфейса. */
  language(): AppLanguage;
  /** Включён ли ночной режим (тёмная тема). */
  isNightMode(): boolean;
  /**
   * Включить или выключить ночной режим. Переключение происходит чуть
   * позже (после текущего обработчика), затем приходит 'theme.changed'.
   */
  setNightMode(night: boolean): void;
  /** Перезапустить клиент. */
  restart(): void;
}

interface EventMap {
  /** Ночной режим включили или выключили. */
  'theme.changed': { night: boolean };
}
