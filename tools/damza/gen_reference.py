#!/usr/bin/env python3
"""Generate docs/plugins/reference.md from docs/plugins/types/*.d.ts.

  tools/damza/gen_reference.py           write reference.md
  tools/damza/gen_reference.py --check   exit 1 if reference.md is stale
  tools/damza/gen_reference.py --missing list members without a description

Interfaces with the same name are merged across files (core.d.ts first, then
the files listed in types/index.d.ts), exactly like TypeScript does. Every
`dzg.*` namespace, data type, event and global gets a line with its
signature and the JSDoc text from the .d.ts. Members without JSDoc take their
description from EXTRA below.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TYPES = os.path.join(ROOT, 'docs', 'plugins', 'types')
OUT = os.path.join(ROOT, 'docs', 'plugins', 'reference.md')

# Descriptions for members that have no JSDoc in the .d.ts files.
EXTRA = {
    'Peer.name': 'Имя пользователя или название чата.',
    'Peer.username': 'Юзернейм без @ или \'\'.',
    'Peer.type': 'Тип чата.',
    'Peer.isSelf': 'Это ты сам («Избранное»).',
    'Message.id': 'id сообщения в чате.',
    'Message.peerId': 'id чата, в котором сообщение.',
    'Message.chat': 'Чат сообщения.',
    'Message.fromId': 'id отправителя (в каналах — сам канал).',
    'Message.fromName': 'Имя отправителя.',
    'Message.text': 'Текст или подпись к медиа, без форматирования.',
    'Message.out': 'Исходящее (отправил ты).',
    'Message.service': 'Сервисное сообщение («X вступил в группу»).',
    'Message.post': 'Пост в канале.',
    'Message.pinned': 'Закреплено.',
    'Message.views': 'Просмотры (для постов каналов).',
    'Message.media': 'Тип вложения, если есть.',
    'PluginInfo.id': 'id плагина из manifest.json.',
    'PluginInfo.name': 'Название плагина.',
    'PluginInfo.version': 'Версия плагина.',
    'PluginInfo.author': 'Автор плагина.',
    'PluginInfo.description': 'Описание плагина.',
    'AppInfo.name': 'Всегда \'DamZaGram\'.',
    'AppInfo.version': 'Версия клиента, например \'5.16.4\'.',
    'AppInfo.apiVersion': 'Версия API плагинов (2).',
    'AppInfo.platform': 'Платформа Qt: \'windows\', \'macos\', \'arch\', \'ubuntu\'...',
    'AppInfo.os': 'Название ОС для людей.',
    'AppInfo.pluginsPath': 'Папка плагинов, с завершающим слешем.',
    'AppInfo.devMode': 'Включена горячая перезагрузка (режим разработчика): ошибки плагинов показываются тостом.',
    'SendContext.text': 'Текст, который сейчас уйдёт (уже с правками предыдущих хуков).',
    'SendContext.peerId': 'id чата.',
    'SendContext.chat': 'Чат, в который отправляют.',
    'CommandContext.text': 'Весь текст сообщения с командой.',
    'CommandContext.chat': 'Чат, в котором набрали команду.',
    'CommandContext.replyTo': 'id сообщения, на которое отвечали, или 0.',
    'CommandSpec.description': 'Описание команды, его отдаёт dzg.commands.all().',
    'CommandSpec.run': 'Вызывается вместо отправки сообщения.',
    'MenuItem.onClick': 'Клик по пункту.',
    'BoxSpec.title': 'Заголовок окна.',
    'BoxSpec.text': 'Текст под заголовком.',
    'BoxSpec.inputs': 'Поля ввода; значения придут в values[id].',
    'BoxSpec.toggles': 'Чекбоксы; значения придут в values[id].',
    'BoxResult.values': 'Значения полей и чекбоксов по их id.',
    'Rect.x': 'Левый край.', 'Rect.y': 'Верхний край.', 'Rect.width': 'Ширина.', 'Rect.height': 'Высота.',
    'WidgetDump.className': 'C++ имя класса.',
    'WidgetDump.name': 'objectName виджета.',
    'WidgetDump.visible': 'Виден ли виджет.',
    'WidgetDump.geometry': 'Положение в родителе.',
    'WidgetDump.children': 'Дочерние виджеты (до заданной глубины).',
    'Widget.valid': 'Виджет ещё существует.',
    'Widget.name': 'objectName виджета.',
    'Widget.setName': 'Задать objectName (удобно для поиска через find).',
    'Widget.geometry': 'Положение и размер в координатах родителя.',
    'Widget.globalGeometry': 'Положение и размер в координатах экрана.',
    'Widget.setGeometry': 'Переместить и изменить размер.',
    'Widget.move': 'Переместить.',
    'Widget.resize': 'Изменить размер.',
    'Widget.visible': 'Виден ли виджет.',
    'Widget.setVisible': 'Показать или спрятать.',
    'Widget.show': 'Показать.',
    'Widget.hide': 'Спрятать.',
    'Widget.raise': 'Поднять над соседями.',
    'Widget.lower': 'Опустить под соседей.',
    'Widget.update': 'Перерисовать.',
    'Widget.setEnabled': 'Включить или выключить (серый, не реагирует на клики).',
    'Widget.setFocus': 'Поставить фокус клавиатуры.',
    'Widget.parentWidget': 'Родитель или null.',
    'Widget.window': 'Окно верхнего уровня, в котором лежит виджет.',
    'Widget.childWidgets': 'Прямые потомки.',
    'Widget.childAt': 'Потомок в точке (локальные координаты).',
    'Widget.prop': 'Значение Qt-свойства (Q_PROPERTY) по имени.',
    'Widget.setProp': 'Записать Qt-свойство. false, если не вышло.',
    'Widget.propNames': 'Имена Qt-свойств виджета.',
    'Widget.typeText': 'Напечатать текст, как с клавиатуры.',
    'Widget.dump': 'Дерево виджета до заданной глубины — для изучения интерфейса.',
    'CreateProps.text': 'Текст лейбла, кнопки, поля или чекбокса.',
    'CreateProps.name': 'objectName.',
    'CreateProps.visible': 'По умолчанию true.',
    'CreateProps.onClick': 'button, panel: клик.',
    'CreateProps.onChange': 'input: текст; checkbox: состояние.',
    'CreateProps.on': 'Те же обработчики одним объектом.',
    'FetchOptions.method': 'GET (по умолчанию), POST, PUT, DELETE, PATCH...',
    'FetchOptions.headers': 'Заголовки запроса.',
    'FetchOptions.body': 'Тело запроса (строка, уйдёт как UTF-8).',
    'FetchResponse.status': 'HTTP-код ответа.',
    'FetchResponse.ok': 'status от 200 до 299.',
    'FetchResponse.url': 'Итоговый адрес (после редиректов).',
    'FetchResponse.text': 'Тело ответа строкой (UTF-8).',
    'FetchResponse.json': 'Тело ответа, разобранное как JSON.',
    'DzgStorage.get': 'Значение по ключу или fallback.',
    'DzgStorage.remove': 'Удалить ключ.',
    'DzgStorage.keys': 'Все ключи.',
    'DzgSettings.get': 'Значение настройки (или default из схемы).',
    'DzgSettings.all': 'Все настройки объектом.',
    'DzgSettings.set': 'Записать настройку (сохраняется, приходит settings.changed).',
    'DzgUi.toast': 'Короткое всплывающее сообщение внизу окна.',
    'DzgUi.box': 'Окно с полями, чекбоксами и кнопками.',
    'DzgUi.alert': 'Окно с текстом и кнопкой OK.',
    'DzgUi.addChatMenuItem': 'Пункт в контекстном меню чата (список чатов и шапка). Возвращает функцию, которая убирает пункт.',
    'DzgUi.mainWindow': 'Корневой виджет главного окна.',
    'DzgUi.focused': 'Виджет с фокусом клавиатуры.',
    'DzgUi.repaint': 'Перерисовать все окна клиента.',
    'DzgChats.current': 'Открытый сейчас чат или null.',
    'DzgChats.get': 'Peer по id или null.',
    'DzgChats.open': 'Открыть чат (и прокрутить к сообщению).',
    'DzgMessages.delete': 'Удалить сообщения. revoke — у всех.',
    'DzgMessages.get': 'Сообщение из памяти клиента или null.',
    'DzgMessages.onBeforeSend': 'Хук перед отправкой: переписать или отменить исходящее сообщение. Возвращает функцию отписки.',
    'DzgCommands.list': 'Команды этого плагина.',
    'DzgHttp.fetch': 'HTTP-запрос. Промис отклоняется только при сетевой ошибке; 404 и 500 — обычный ответ с ok = false.',
    'DzgFs.read': 'Прочитать текстовый файл (UTF-8). Относительный путь — от папки плагина. null, если файла нет.',
    'DzgFs.write': 'Записать текстовый файл, создав папки. Относительный путь — от папки плагина.',
    'DzgFs.list': 'Имена файлов и папок внутри папки.',
    'DzgFs.exists': 'Есть ли файл или папка.',
    'DzgClipboard.get': 'Текст из буфера обмена.',
    'DzgClipboard.set': 'Положить текст в буфер обмена.',
    'Dzg.version': 'Версия API плагинов (2).',
    'Dzg.plugin': 'Информация о плагине.',
    'Dzg.app': 'Информация о клиенте и модуль app.',
    'Dzg.on': 'Подписаться на событие. Возвращает функцию отписки.',
    'Dzg.off': 'Отписать обработчик.',
    'Dzg.once': 'Подписаться на одно срабатывание.',
    'Dzg.log': 'То же, что console.log.',
    'Dzg.ui': 'Интерфейс: тосты, окна, меню, кнопки, панели, виджеты.',
    'Dzg.theme': 'Цвета палитры на лету.',
    'Dzg.chats': 'Чаты, папки, участники.',
    'Dzg.messages': 'Сообщения: отправка, история, поиск, фильтры, хуки.',
    'Dzg.commands': 'Слеш-команды.',
    'Dzg.http': 'HTTP-запросы.',
    'Dzg.fs': 'Файлы.',
    'Dzg.clipboard': 'Буфер обмена.',
    'Dzg.openUrl': 'Открыть ссылку в браузере ОС.',
    'Dzg.users': 'Пользователи и контакты.',
    'Dzg.accounts': 'Аккаунты, в которые выполнен вход.',
    'Dzg.api': 'Сырой MTProto: любой метод Telegram API.',
    'Dzg.plugins': 'Управление плагинами.',
    'Dzg.builtin': 'Встроенные плагины.',
    'DzgUi.compose': 'Поле ввода сообщения.',
    'ChatFolder.id': 'id папки для dzg.chats.list({ folder }).',
    'ChatFolder.title': 'Название папки.',
    'ChatInfo.peer': 'Сам чат.',
    'ChatCreateOptions.title': 'Название.',
    'ChatInfo.isCreator': 'Ты создатель.',
    'ChatInfo.isAdmin': 'Ты админ.',
    'ChatInfo.isVerified': 'Галочка верификации.',
    'ChatInfo.isScam': 'Помечен как скам.',
    'ChatInfo.isFake': 'Помечен как фейк.',
    'UserStatus.online': 'Сейчас в сети.',
    'UserInfo.peer': 'Сам пользователь.',
    'UserInfo.firstName': 'Имя.', 'UserInfo.lastName': 'Фамилия.',
    'UserInfo.isContact': 'В твоих контактах.', 'UserInfo.isPremium': 'Есть Telegram Premium.',
    'UserInfo.isBot': 'Это бот.', 'UserInfo.isVerified': 'Галочка верификации.',
    'UserInfo.isScam': 'Помечен как скам.', 'UserInfo.isFake': 'Помечен как фейк.',
    'AccountEntry.self': 'Пользователь аккаунта.', 'AccountEntry.active': 'Аккаунт активен сейчас.',
    'AccountEntry.premium': 'У аккаунта есть Premium.',
    'MessageButton.text': 'Надпись на кнопке.',
    'MessageReaction.customEmojiId': 'id премиум-эмодзи.', 'MessageReaction.paid': 'Платная реакция звёздами.',
    'MessageReaction.count': 'Сколько раз поставили.',
    'MessageEntity.type': 'Тип сущности.', 'MessageEntity.offset': 'Начало (UTF-16).', 'MessageEntity.length': 'Длина (UTF-16).',
    'FormattedText.text': 'Текст без разметки.', 'FormattedText.entities': 'Форматирование.',
    'MessagesEditOptions.parse': 'Разметка текста.', 'MessagesEditOptions.noWebpage': 'Без превью ссылок.',
    'MessagesEditOptions.entities': 'Готовое форматирование.',
    'MessagesSearchOptions.minDate': 'Не раньше этой даты.', 'MessagesSearchOptions.maxDate': 'Не позже этой даты.',
    'MessagesSearchOptions.offsetId': 'Искать старше этого id.', 'MessagesSearchOptions.addOffset': 'Сдвиг от offsetId.',
    'MessagesForwardOptions.silent': 'Без звука у получателей.', 'MessagesForwardOptions.scheduleDate': 'Отложенная пересылка.',
    'MessagesSendFileOptions.replyTo': 'Ответ на сообщение.', 'MessagesSendFileOptions.topicId': 'Тема форума.',
    'MessagesSendFileOptions.silent': 'Без звука.', 'MessagesSendFileOptions.scheduleDate': 'Отложенная отправка.',
    'MessagesSendFileOptions.entities': 'Готовое форматирование подписи.',
    'DzgMessages.unpin': 'Открепить сообщение.',
    'TopBarButtonSpec.onClick': 'Клик по кнопке.',
    'ComposeButtonSpec.icon': 'Иконка (или text).', 'ComposeButtonSpec.text': 'Короткий текст вместо иконки.',
    'ComposeButtonSpec.tooltip': 'Подсказка при наведении.',
    'MainMenuItemSpec.title': 'Текст пункта.',
    'NotifySpec.title': 'Заголовок.', 'NotifySpec.text': 'Текст.',
    'ComposeState.text': 'Текст в поле.',
    'PanelInputOptions.placeholder': 'Подсказка в пустом поле.', 'PanelInputOptions.value': 'Начальный текст.',
    'Panel.id': 'id панели.',
    'Panel.addSeparator': 'Разделитель.',
    'Panel.isOpen': 'Панель ещё открыта.',
    'DzgUi.icons': 'Имена иконок для опции icon.',
    'DzgUi.addTopBarButton': 'Кнопка в шапке чата. Возвращает функцию, которая её убирает.',
    'DzgUi.addMainMenuItem': 'Пункт главного меню (☰). Возвращает функцию, которая его убирает.',
    'TLParamInfo.name': 'Имя поля как в api.tl.',
    'DzgPlugins.enable': 'То же, что setEnabled(id, true).', 'DzgPlugins.disable': 'То же, что setEnabled(id, false).',
    'DzgPlugins.list': 'Все установленные плагины, включая выключенные.',
    'InstalledPlugin.id': 'id плагина.', 'InstalledPlugin.name': 'Название.', 'InstalledPlugin.version': 'Версия.',
    'InstalledPlugin.author': 'Автор.', 'InstalledPlugin.description': 'Описание.',
    'PluginPackageInfo.id': 'id плагина.', 'PluginPackageInfo.name': 'Название.', 'PluginPackageInfo.version': 'Версия.',
    'PluginPackageInfo.author': 'Автор.', 'PluginPackageInfo.description': 'Описание.',
    'BuiltinPluginInfo.id': 'id плагина.', 'BuiltinPluginInfo.name': 'Название.',
    'PluginCommandInfo.name': 'Имя команды без /.', 'PluginCommandInfo.description': 'Описание команды.',
    'PluginCommandInfo.pluginName': 'Название плагина.',
    'TLObject._': 'Имя конструктора: \'inputPeerSelf\', \'message\'...',
    'TLObject.[field: string]': 'Поля как в api.tl (snake_case).',
    'console.log': 'Строка в лог плагина (уровень info).', 'console.info': 'То же, что log.',
    'console.debug': 'Отладочная строка.', 'console.warn': 'Предупреждение.',
    'console.error': 'Ошибка: в режиме разработчика ещё и тост.',
    'DzgTheme.setMany': 'Поменять несколько цветов сразу: { windowBg: \'#101010\', ... }.',
    'DzgMessages.send': 'Отправить текст (с Markdown/HTML, ответом, отложенно...). true — сообщение принято к отправке.',
    'DzgMessages.edit': 'Изменить текст своего сообщения. false — сообщение не найдено.',
    'DzgMessages.format': 'Собрать HTML (по умолчанию) или Markdown из текста и форматирования.',
    'DzgMessages.fetch': 'Сообщения с сервера по id, даже если клиент их не загружал.',
    'DzgMessages.forward': 'Переслать одно или несколько сообщений в другой чат.',
    'DzgMessages.pin': 'Закрепить сообщение ({ unpin: true } — открепить).',
    'DzgMessages.clickButton': 'Нажать inline-кнопку бота по позиции или по тексту.',
    'DzgApi.fromBase64': 'base64 (и base64url) -> байты, или строка при as = \'text\'.',
    "EventMap.message.edited": 'Сообщение изменили (текст, реакции, кнопки...).',
    "EventMap.chat.opened": 'Пользователь открыл чат.',
    'Widget.setText': 'Задать текст полю, лейблу или кнопке.',
    'CreateProps.x': 'Положение в родителе.', 'CreateProps.y': 'Положение в родителе.',
    'CreateProps.width': 'Ширина.', 'CreateProps.height': 'Высота.',
    'CreateProps.multiline': 'input: многострочное поле.',
    'CreateProps.radius': 'panel: скругление углов, px.',
    'MessageButton.kind': 'Точный вид кнопки.',
    'MessageMediaInfo.fileName': 'Имя файла.', 'MessageMediaInfo.mime': 'MIME-тип.',
    'MessageMediaInfo.width': 'Ширина (фото, видео).', 'MessageMediaInfo.height': 'Высота (фото, видео).',
    'MessagesSearchOptions.filter': 'Только сообщения с этим типом вложений.',
    'ChatMember.role': 'Роль в чате.',
    'UserStatus.kind': 'Вид статуса.',
    'UserInfo.status': 'Статус в сети.',
    'PanelButtonOptions.icon': 'Иконка: кнопка станет строкой с иконкой.',
    'Panel.setTitle': 'Поменять заголовок.',
    'Panel.scrollToBottom': 'Прокрутить содержимое вниз.',
    'Panel.addButton': 'Кнопка на всю ширину.',
    'TLSchemaInfo.kind': 'Метод или конструктор.',
    'TLSchemaInfo.name': 'Имя: \'messages.getHistory\', \'inputPeerSelf\'.',
    'TLSchemaInfo.params': 'Поля по порядку схемы.',
}

# Short leads put before a JSDoc text that only adds remarks.
LEAD = {
    'DzgUi.addMessageMenuItem': 'Пункт в контекстном меню сообщения.',
    'DzgUi.find': 'Найти виджеты по C++ классу и objectName.',
    'DzgUi.create': 'Создать свой виджет: label, button, input, checkbox, panel.',
    'DzgUi.prompt': 'Окно с полем ввода.',
    'DzgStorage.set': 'Записать значение, сразу сохраняется на диск.',
    'DzgChats.find': 'Чат по юзернейму.',
    'DzgMessages.addFilter': 'Скрыть сообщения из ленты.',
    'DzgMessages.loaded': 'Сообщения из памяти клиента.',
    'DzgCommands.register': 'Зарегистрировать слеш-команду.',
    'DzgUi.confirm': 'Окно «Да / Отмена».',
    'Widget.find': 'Найти потомков.',
    'Widget.on': 'Подписаться на событие.',
}

GLOBAL_DOCS = {
    'setTimeout': 'Вызвать fn через ms миллисекунд. Возвращает id таймера.',
    'setInterval': 'Вызывать fn каждые ms миллисекунд.',
    'clearTimeout': 'Отменить setTimeout.',
    'clearInterval': 'Отменить setInterval.',
    'queueMicrotask': 'Выполнить fn сразу после текущего кода (микрозадача).',
}

# Where each namespace is explained in depth.
LINKS = {
    'dzg': ('guide/01-first-plugin.md', None),
    'dzg.plugin': ('README.md#анатомия-плагина', None),
    'dzg.app': ('guide/05-ui.md#тема-и-ночной-режим', None),
    'dzg.storage': ('guide/07-network-and-storage.md#хранилище-dzgstorage', None),
    'dzg.settings': ('guide/07-network-and-storage.md#настройки-dzgsettings', None),
    'dzg.ui': ('guide/05-ui.md', 'api/ui.md'),
    'dzg.ui.compose': ('guide/05-ui.md#поле-ввода', 'api/ui.md#поле-ввода'),
    'dzg.theme': ('guide/05-ui.md#тема-и-ночной-режим', None),
    'dzg.chats': ('guide/04-chats-and-users.md', 'api/chats.md'),
    'dzg.messages': ('guide/03-messages.md', 'api/messages.md'),
    'dzg.commands': ('guide/06-commands-and-hooks.md', 'api/builtin.md'),
    'dzg.http': ('guide/07-network-and-storage.md#http', None),
    'dzg.fs': ('guide/07-network-and-storage.md#файлы-dzgfs', None),
    'dzg.clipboard': ('guide/07-network-and-storage.md#буфер-обмена-и-ссылки', None),
    'dzg.users': ('guide/04-chats-and-users.md#пользователи', 'api/users.md'),
    'dzg.accounts': ('guide/04-chats-and-users.md#несколько-аккаунтов', 'api/users.md#dzgaccounts'),
    'dzg.api': ('guide/08-raw-mtproto.md', 'api/mtproto.md'),
    'dzg.plugins': ('guide/10-publishing.md', 'api/plugins.md'),
    'dzg.builtin': (None, 'api/builtin.md'),
    'dzg.wallet': (None, None),
    'dzg.gifts': (None, None),
}

ORDER = ['core.d.ts', 'app.d.ts', 'messages.d.ts', 'chats.d.ts', 'users.d.ts',
         'ui.d.ts', 'api.d.ts', 'plugins.d.ts', 'builtin.d.ts', 'wallet.d.ts', 'gifts.d.ts']

NAMESPACE_TYPES = {'AppInfo', 'PluginInfo'}


def clean_doc(raw):
    text = raw.strip()
    text = re.sub(r'^/\*\*', '', text)
    text = re.sub(r'\*/$', '', text)
    lines = []
    for line in text.split('\n'):
        line = re.sub(r'^\s*\* ?', '', line).rstrip()
        lines.append(line)
    text = ' '.join(l.strip() for l in lines if l.strip())
    text = re.sub(r'\s+-\s+(?=\S)', ' — ', text) if text.startswith('-') else text
    text = re.sub(r':\s+—\s+', ': ', text)
    return text.strip()


def split_items(body):
    """Split a block of TS into (doc, statement) pairs at depth 0."""
    items = []
    i, n = 0, len(body)
    doc = None
    current = ''
    depth = 0
    while i < n:
        if body.startswith('/*', i):
            end = body.index('*/', i) + 2
            if depth == 0 and not current.strip():
                if body.startswith('/**', i):
                    doc = body[i:end]
            else:
                current += body[i:end]
            i = end
            continue
        if body.startswith('//', i):
            end = body.find('\n', i)
            end = n if end < 0 else end
            if depth > 0:
                current += body[i:end]
            i = end
            continue
        c = body[i]
        if c == "'" or c == '"' or c == '`':
            end = i + 1
            while end < n and body[end] != c:
                end += 2 if body[end] == '\\' else 1
            current += body[i:end + 1]
            i = end + 1
            continue
        if c in '({[':
            depth += 1
        elif c in ')}]':
            depth -= 1
        current += c
        i += 1
        if depth == 0 and (c == ';' or (c == '}' and re.match(r'\s*(interface|declare\s+global)\b', current))):
            stmt = current.strip()
            if stmt and stmt != ';':
                items.append((doc, stmt))
            current = ''
            doc = None
    if current.strip():
        items.append((doc, current.strip()))
    return items


def norm(sig):
    sig = re.sub(r'\s+', ' ', sig).strip()
    sig = re.sub(r';\s*$', '', sig)
    sig = sig.replace('{ ', '{ ').replace(' }', ' }')
    sig = re.sub(r';\s*}', ' }', sig)
    return sig


def parse_member(doc, stmt):
    m = re.match(r"(readonly\s+)?('[^']+'|\[[^\]]+\]|[A-Za-z_$][\w$]*)(\?)?\s*(<[^(]*?>)?\s*([(:])", stmt)
    if not m:
        return None
    name = m.group(2)
    kind = 'method' if m.group(5) == '(' else 'prop'
    rest = stmt[m.end(2):]
    if kind == 'prop':
        typ = norm(stmt[m.end(5):])
        return {'name': name, 'kind': kind, 'optional': bool(m.group(3)),
                'readonly': bool(m.group(1)), 'type': typ, 'doc': clean_doc(doc) if doc else '',
                'sig': norm(stmt)}
    return {'name': name, 'kind': kind, 'type': '', 'doc': clean_doc(doc) if doc else '',
            'sig': norm(stmt), 'optional': bool(m.group(3)), 'readonly': False}


class Model:
    def __init__(self):
        self.interfaces = {}   # name -> {'doc', 'extends', 'members': [..], 'file'}
        self.order = []
        self.aliases = []      # (name, doc, text, file)
        self.declares = []     # (doc, stmt)

    def add_file(self, name, text):
        for doc, stmt in split_items(text):
            if stmt.startswith('interface '):
                m = re.match(r'interface\s+(\w+)(<[^>]*>)?\s*(extends\s+([^{]+))?\{', stmt)
                iname = m.group(1)
                body = stmt[m.end():stmt.rindex('}')]
                entry = self.interfaces.get(iname)
                if not entry:
                    entry = {'doc': '', 'extends': '', 'members': [], 'file': name,
                             'generic': m.group(2) or ''}
                    self.interfaces[iname] = entry
                    self.order.append(iname)
                if doc and not entry['doc']:
                    entry['doc'] = clean_doc(doc)
                if m.group(4):
                    entry['extends'] = m.group(4).strip()
                for mdoc, mstmt in split_items(body):
                    member = parse_member(mdoc, mstmt)
                    if member:
                        member['file'] = name
                        entry['members'].append(member)
            elif stmt.startswith('type '):
                m = re.match(r'type\s+(\w+)(<[^>]*>)?\s*=\s*(.*)$', stmt, re.S)
                self.aliases.append((m.group(1), clean_doc(doc) if doc else '', norm(m.group(3)), name))
            elif stmt.startswith('declare '):
                self.declares.append((doc, stmt))


def describe(iname, member):
    key = '%s.%s' % (iname, member['name'].strip("'"))
    text = member['doc'] or EXTRA.get(key, '')
    if member['doc'] and key in LEAD and member.get('first', True):
        lead = LEAD[key]
        first = text[0].upper() if re.match(r'[а-яё]', text[0]) else text[0]
        text = lead + ' ' + first + text[1:]
    return codeify(text)


def codeify(text):
    """Put `<...>` fragments (paths, generics) outside code into backticks,
    so Markdown renderers don't take them for HTML tags."""
    parts = re.split(r'(`[^`]*`)', text)
    for i in range(0, len(parts), 2):
        parts[i] = re.sub(r"([^\s`]*<[^>\s]+>[^\s`,;)]*?)([.,;]?)(?=\s|$|\))", r'`\1`\2', parts[i])
    return ''.join(parts)


def grouped(entry):
    """Members in order; a name redeclared in a later file replaces the earlier
    declaration (core.d.ts send -> messages.d.ts send), overloads from one file stay."""
    last_file = {}
    for member in entry['members']:
        last_file[member['name']] = member['file']
    result, seen = [], set()
    for member in entry['members']:
        if member['file'] != last_file[member['name']]:
            continue
        member = dict(member)
        member['first'] = member['name'] not in seen
        seen.add(member['name'])
        result.append(member)
    return result


def bullet(sig, text):
    sig = sig.replace('`', "'")
    return '- `%s`%s' % (sig, (' — ' + text) if text else '')


def namespace_type(member):
    t = member['type']
    if re.fullmatch(r'Dzg\w+', t) or t in NAMESPACE_TYPES:
        return t
    return None


def build(model, missing):
    out = []
    w = out.append
    w('# Справочник API плагинов DamZaGram')
    w('')
    w('Полный список того, что есть у плагина: каждый метод, поле, событие и тип '
      'данных API версии 2. Справочник **собран автоматически** из '
      '[`types/*.d.ts`](types/index.d.ts) скриптом `tools/damza/gen_reference.py`, '
      'поэтому совпадает с подсказками VS Code. Объяснения и примеры — в '
      '[гайдах](README.md#гайды), [рецептах](cookbook.md) и страницах модулей `api/*.md`.')
    w('')
    w('Условные обозначения: `?` — необязательный параметр или поле; `PeerId | Peer` — '
      'можно передать id чата (строку) или сам объект; `Promise<T>` — асинхронный метод, '
      'используй `await`. Автор DamZaGram — [@wetakush](https://t.me/wetakush).')
    w('')

    dzg = model.interfaces['Dzg']
    namespaces = []

    def collect(prefix, iname):
        namespaces.append((prefix, iname))
        for member in model.interfaces[iname]['members']:
            nt = namespace_type(member)
            if nt and nt in model.interfaces:
                collect(prefix + '.' + member['name'], nt)

    namespaces.append(('dzg', 'Dzg'))
    for member in dzg['members']:
        nt = namespace_type(member)
        if nt and nt in model.interfaces:
            collect('dzg.' + member['name'], nt)

    w('## Содержание')
    w('')
    w('- [Глобальные функции](#глобальные-функции)')
    for prefix, iname in namespaces:
        w('- [`%s`](#%s)' % (prefix, slug(prefix)))
    w('- [События](#события)')
    w('- [Типы данных](#типы-данных)')
    w('- [Псевдонимы типов](#псевдонимы-типов)')
    w('')

    w('## Глобальные функции')
    w('')
    w('Есть в любом плагине без импорта, как в браузере.')
    w('')
    for doc, stmt in model.declares:
        m = re.match(r'declare\s+function\s+(\w+)', stmt)
        if m:
            name = m.group(1)
            sig = norm(stmt[len('declare function '):])
            text = (clean_doc(doc) if doc else '') or GLOBAL_DOCS.get(name, '')
            if not text:
                missing.append('global ' + name)
            w(bullet(sig, text))
            continue
        m = re.match(r'declare\s+const\s+console\s*:\s*\{(.*)\}\s*;?$', stmt, re.S)
        if m:
            for mdoc, mstmt in split_items(m.group(1)):
                member = parse_member(mdoc, mstmt)
                text = member['doc'] or EXTRA.get('console.' + member['name'], '')
                if not text:
                    missing.append('console.' + member['name'])
                w(bullet('console.' + member['sig'], text))
    w('- `globalThis`, `Promise`, `Map`, `Set`, `BigInt`, `atob`/`btoa`, `performance.now()` — '
      'стандартная библиотека ES2023 (QuickJS-ng). `Intl`, `TextEncoder`, `fetch`, '
      '`structuredClone` нет — см. [окружение JS](README.md#окружение-javascript).')
    w('')

    for prefix, iname in namespaces:
        entry = model.interfaces[iname]
        w('## `%s`' % prefix)
        w('')
        guide, api = LINKS.get(prefix, (None, None))
        refs = []
        if guide:
            refs.append('гайд: [%s](%s)' % (guide.split('#')[0], guide))
        if api:
            refs.append('подробно: [%s](%s)' % (api.split('#')[0], api))
        intro = 'Тип `%s`.' % iname
        if refs:
            joined = ', '.join(refs)
            intro += ' ' + joined[0].upper() + joined[1:] + '.'
        w(intro)
        w('')
        for member in grouped(entry):
            text = describe(iname, member)
            if not text and member['first']:
                missing.append('%s.%s' % (iname, member['name']))
            if not member['first'] and text == describe(iname, dict(member, doc='')):
                text = 'вариант вызова.' if not member['doc'] else text
            nt = namespace_type(member)
            if nt and nt in model.interfaces and prefix + '.' + member['name'] != prefix:
                sub = prefix + '.' + member['name']
                w('- [`%s`](#%s) — %s' % (sub, slug(sub), text or 'см. раздел'))
                continue
            w(bullet(member['sig'], text))
        w('')

    w('## События')
    w('')
    w('Подписка: `dzg.on(name, (data) => { ... })`, отписка — функция, которую вернул `on`. '
      'Подробно: [гайд по событиям](guide/02-events.md).')
    w('')
    for member in model.interfaces['EventMap']['members']:
        text = describe('EventMap', member)
        if not text:
            missing.append('EventMap.' + member['name'])
        w('- `%s` → `%s`%s' % (member['name'].strip("'"), member['type'].replace('`', "'"),
                               (' — ' + text) if text else ''))
    w('')

    w('## Типы данных')
    w('')
    w('Объекты, которые API принимает и возвращает. Одноимённые интерфейсы из разных '
      'файлов слиты, как это делает TypeScript.')
    w('')
    skip = {'Dzg', 'EventMap'}
    for iname in model.order:
        if iname in skip or re.fullmatch(r'Dzg\w+', iname) or iname in NAMESPACE_TYPES:
            continue
        entry = model.interfaces[iname]
        w('### `%s%s`' % (iname, entry['generic']))
        w('')
        line = []
        if entry['doc']:
            line.append(entry['doc'])
        if entry['extends']:
            line.append('Расширяет `%s`.' % entry['extends'])
        if line:
            w(' '.join(line))
            w('')
        for member in entry['members']:
            text = describe(iname, member)
            if not text:
                missing.append('%s.%s' % (iname, member['name']))
            w(bullet(member['sig'], text))
        w('')

    w('## Псевдонимы типов')
    w('')
    for name, doc, text, _ in model.aliases:
        if len(text) > 400:
            text = text[:400].rsplit('|', 1)[0] + '| ...'
        w('### `%s`' % name)
        w('')
        if doc:
            w(doc)
            w('')
        w('```ts')
        w('type %s = %s;' % (name, text))
        w('```')
        w('')
    return '\n'.join(out).rstrip() + '\n'


def slug(text):
    text = text.strip().lower()
    text = re.sub(r'[^\w\- ]', '', text, flags=re.U)
    return text.replace(' ', '-')


def main():
    model = Model()
    names = [n for n in ORDER if os.path.exists(os.path.join(TYPES, n))]
    extra = sorted(set(f for f in os.listdir(TYPES) if f.endswith('.d.ts')) - set(names) - {'index.d.ts'})
    for name in names + extra:
        model.add_file(name, open(os.path.join(TYPES, name), encoding='utf-8').read())
    missing = []
    text = build(model, missing)
    if '--missing' in sys.argv:
        print('\n'.join(missing) or 'all members described')
        return 0
    if '--check' in sys.argv:
        current = open(OUT, encoding='utf-8').read() if os.path.exists(OUT) else ''
        if current != text:
            print('reference.md is stale: run tools/damza/gen_reference.py')
            return 1
        print('reference.md is up to date')
        return 0
    open(OUT, 'w', encoding='utf-8').write(text)
    print('wrote %s (%d lines, %d undocumented)' % (os.path.relpath(OUT, ROOT), text.count('\n'), len(missing)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
