// Типы API плагинов DamZaGram — модуль builtin (apiVersion 2).
// Встроенные плагины и команды всех плагинов сразу.
// Исходник: Telegram/SourceFiles/damza/plugins/api/api_builtin.cpp.
// Автор модуля: @wetakush (https://t.me/wetakush).

/** Встроенный плагин, который клиент поставляет и обновляет сам. */
interface BuiltinPluginInfo {
  id: string;
  name: string;
  /** Версия, вшитая в клиент. */
  version: string;
  /** Версия в tdata/plugins/<id>/ ('' если не установлен). */
  installedVersion: string;
  /** Включается сам при первом запуске (пользователь может выключить). */
  enabledByDefault: boolean;
  /** Пользователь заменил плагин своим (в manifest.json нет "builtin": true) — клиент его не трогает. */
  replaced: boolean;
  /** Установлен или обновлён при этом запуске. */
  updated: boolean;
  /** Ошибка установки ('' если всё хорошо). */
  error: string;
}

/** Команда любого загруженного плагина. */
interface PluginCommandInfo {
  name: string;
  description: string;
  /** id плагина, который зарегистрировал команду. */
  plugin: string;
  pluginName: string;
}

interface DzgCommands {
  /** Команды всех загруженных плагинов, включая свои. */
  all(): PluginCommandInfo[];
  /**
   * Выполнить команду любого плагина так, будто пользователь отправил её в чат.
   * text: '/digest 50' или 'digest 50'. chat: чат-контекст, по умолчанию текущий.
   * false — такой команды нет или её плагин сейчас занят.
   */
  run(text: string, chat?: Peer | PeerId): boolean;
}

interface DzgBuiltin {
  /** Встроенные плагины и их состояние после установки на старте. */
  list(): BuiltinPluginInfo[];
}

interface Dzg {
  builtin: DzgBuiltin;
}
