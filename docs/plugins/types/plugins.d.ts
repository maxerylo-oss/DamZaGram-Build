// Типы API плагинов DamZaGram — модуль plugins (apiVersion 2).
// Управление другими плагинами из JS: список, включение, перезагрузка,
// установка по ссылке или пути, удаление, выполнение кода в другом плагине.
// Автор модуля: @wetakush (https://t.me/wetakush).

/** Установленный плагин, как его видит менеджер. */
interface InstalledPlugin {
  id: string;
  name: string;
  version: string;
  author: string;
  description: string;
  /** Папка плагина. */
  dir: string;
  /** 'module' (ES-модуль, по умолчанию) или 'script'. */
  type: 'module' | 'script';
  /** Пользователь включил плагин. */
  enabled: boolean;
  /** Плагин сейчас работает (включён и загрузился без ошибок). */
  loaded: boolean;
  /** Встроенный: поставляется с клиентом, удалить нельзя — только выключить. */
  builtin: boolean;
  /** Есть схема настроек в manifest.json. */
  hasSettings: boolean;
  /** Последняя ошибка ('' если всё хорошо). */
  error: string;
}

/** Что лежит по ссылке или пути, без установки. */
interface PluginPackageInfo {
  id: string;
  name: string;
  version: string;
  author: string;
  description: string;
  /** Сколько файлов в пакете. */
  files: number;
  /** Суммарный размер в байтах. */
  size: number;
  /** Плагин из одного .js: манифест собран из шапки `// @name ...`. */
  singleFile: boolean;
  /** Итоговая ссылка или имя файла. */
  source: string;
  /** Плагин с таким id уже установлен (установка его обновит). */
  installed: boolean;
}

interface DzgPlugins {
  /** Папка плагинов: `<папка данных>/tdata/plugins/`. */
  readonly path: string;
  /** Все установленные плагины, включая выключенные. */
  list(): InstalledPlugin[];
  /** Плагин по id (по умолчанию — текущий) или null. */
  get(id?: string): InstalledPlugin | null;
  /**
   * Включить или выключить плагин. Применяется сразу после текущего
   * обработчика (не синхронно). false — нет такого плагина.
   */
  setEnabled(id: string, enabled: boolean): boolean;
  enable(id: string): boolean;
  disable(id: string): boolean;
  /** Перезагрузить плагин (по умолчанию — текущий) после текущего обработчика. */
  reload(id?: string): boolean;
  /** Перечитать папку плагинов и перезапустить все плагины. */
  reloadAll(): boolean;
  /** Последние строки лога плагина (по умолчанию 100). Пусто, если не загружен. */
  log(id?: string, limit?: number): string[];
  /** Текущие значения настроек плагина (с учётом значений по умолчанию). */
  settings(id?: string): Record<string, unknown>;
  /** Поменять настройку другого плагина; он получит событие settings.changed. */
  setSetting(id: string, key: string, value: unknown): boolean;
  /**
   * Выполнить код в глобальной области другого загруженного плагина.
   * Возвращает результат текстом (JSON, если получается), бросает Error.
   */
  evaluate(id: string, code: string): string;
  /**
   * Прочитать пакет по ссылке или пути без установки.
   * Принимает https-ссылки на .zip/.js, страницы файлов и репозитории GitHub,
   * file:// и абсолютные пути (папка, .zip, .js).
   */
  inspect(source: string): Promise<PluginPackageInfo>;
  /**
   * Тихо установить или обновить плагин (без окна подтверждения) и вернуть
   * его id. Внимание: после установки нового плагина менеджер перечитывает
   * папку и перезапускает все плагины, включая вызвавший, — промис
   * может не дождаться ответа. Для магазинов плагинов удобнее promptInstall.
   */
  install(source: string, options?: { enable?: boolean }): Promise<string>;
  /**
   * Скачать пакет и показать пользователю окно установки (манифест,
   * предупреждение о полном доступе). Промис выполняется, когда окно показано.
   */
  promptInstall(source: string): Promise<void>;
  /** Удалить плагин. Встроенные только выключаются. */
  uninstall(id: string, options?: { removeData?: boolean }): Promise<void>;
  /** Открыть раздел «Плагины» в настройках. */
  openManager(): void;
  /** Открыть окно DevTools (консоль + живой лог) для плагина. */
  openDevTools(id?: string): void;
}

interface EventMap {
  /**
   * Список плагинов изменился: установили, удалили, включили, выключили,
   * обновили версию. Изменения только текста ошибки не считаются.
   */
  'plugins.changed': { plugins: InstalledPlugin[] };
}

interface Dzg {
  plugins: DzgPlugins;
}
