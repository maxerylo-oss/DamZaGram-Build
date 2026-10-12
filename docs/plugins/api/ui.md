# `dzg.ui` — кнопки, меню, горячие клавиши, панели

Базовый `dzg.ui` умеет тосты, диалоги, пункты контекстного меню и доступ к
любому виджету. Этот модуль добавляет то, из чего собирается настоящий
интерфейс плагина. Плагин может:

- повесить свою кнопку в **шапку чата**, к **полю ввода** и в **главное меню**;
- назначить **горячую клавишу** на всё приложение;
- показать **уведомление**, по клику на которое что-то произойдёт;
- открыть **ссылку** так, как это сделал бы сам клиент (`t.me`, `tg://`);
- читать и менять **текст в поле ввода**;
- открыть **боковую панель** справа, как третью колонку;
- **подменять текст сообщений** на экране, не трогая сами сообщения.

Типы: [`types/ui.d.ts`](../types/ui.d.ts). Исходники:
`Telegram/SourceFiles/damza/plugins/api/api_ui.cpp` и
`Telegram/SourceFiles/damza/plugins/ui/`. Живой пример, в котором есть всё
сразу: [`plugins/examples/ui-showcase`](../../../plugins/examples/ui-showcase).
Автор модуля — [@wetakush](https://t.me/wetakush).

Общие правила:

- Все `add*` возвращают функцию, которая убирает добавленное. Вызывать её
  можно сколько угодно раз. При выгрузке плагина всё убирается само.
- Колбэки кликов вызываются из цикла событий, а не внутри обработчика нажатия.
  Поэтому из `onClick` можно смело удалять ту же кнопку, закрывать панель,
  открывать окна.
- Исключение внутри колбэка не роняет клиент: оно попадает в лог плагина и
  показывается тостом в режиме разработчика.
- Иконки задаются по имени из `dzg.ui.icons()`. Это иконки самого клиента, и
  они перекрашиваются вместе с темой.

## Шпаргалка

| Метод | Что делает |
|---|---|
| `icons()` | Имена иконок для опции `icon` |
| `addTopBarButton({ icon, text, tooltip, when, onClick })` | Кнопка в шапке чата |
| `addMainMenuItem({ title, icon, onClick })` | Пункт в главном меню (☰) |
| `addShortcut(keys, fn)` | Горячая клавиша, например `'Ctrl+Shift+K'` |
| `notify({ title, text, onClick, silent })` | Уведомление с действием |
| `openLink(url)` | Открыть ссылку через клиент |
| `refreshButtons()` | Пересчитать `when()` у кнопок в шапке |
| `compose.get()` / `set(text)` / `insert(text)` / `focus()` | Поле ввода |
| `compose.addButton({ icon, text, tooltip, onClick })` | Кнопка у поля ввода |
| `panel({ title, width })` | Боковая панель справа |
| `dzg.messages.addRenderTransform(fn)` | Подмена текста при отрисовке |
| `dzg.messages.refreshRender()` | Перерисовать с новыми правилами |

События: `app.focus`, `compose.changed` (см. [ниже](#события)).

## Иконки

```js
console.log(dzg.ui.icons());
// ['search', 'settings', 'customize', 'edit', 'delete', 'copy', 'link', 'pin',
//  'mute', 'unmute', 'archive', 'info', 'stats', 'bot', 'star', 'bell', 'lock',
//  'user', 'users', 'channel', 'photo', 'video', 'file', 'download', 'upload',
//  'reply', 'forward', 'translate', 'palette', 'code', 'plus', 'close', 'check',
//  'timer', 'location', 'poll', 'sticker', 'emoji', 'gift', 'ghost', ... ]
```

Всего около сотни иконок, полный список есть в `DzgIconName` в
[`ui.d.ts`](../types/ui.d.ts). Две из них DamZaGram берёт у AyuGram:
`ghost` (режим призрака) и `streamer` (режим стримера). Если указать
неизвестное имя, в лог уйдёт предупреждение. Кнопка без текста тогда получит
иконку `customize`.

## Кнопка в шапке чата

```js
const off = dzg.ui.addTopBarButton({
  icon: 'translate',
  tooltip: 'Перевести последние сообщения',
  // Только в группах и каналах:
  when: (chat) => chat.type === 'group' || chat.type === 'channel',
  async onClick(chat) {
    const last = dzg.messages.loaded(chat, 5).map((m) => m.text).join('\n');
    dzg.ui.toast(`Переводим ${last.length} символов из «${chat.name}»…`);
  },
});
```

Кнопки встают справа, между кнопкой поиска и кнопками AyuGram для админов,
и выглядят как родные: такой же размер, ripple при нажатии, цвет иконки из
темы. Вместо иконки можно дать короткий `text`, например `'Аа'`, `'GPT'`
или `'×2'`. Текстовая кнопка расширяется под надпись.

`when(chat)` вызывается **синхронно** при каждом обновлении шапки: при смене
чата, выделении сообщений, изменении размера окна. Делай его быстрым и без
`await`. Если условие зависит от твоего состояния (например, включили
настройку), вызови `dzg.ui.refreshButtons()`, и шапка пересчитается.

Кнопки не показываются в списке тем форума, в режиме «пожаловаться» и
вне чатов. Пока выделены сообщения, они уезжают вместе с остальными.

## Пункт главного меню

```js
dzg.ui.addMainMenuItem({
  title: 'Мой дашборд',
  icon: 'stats',
  onClick: () => openDashboard(),
});
```

Пункты плагинов идут в конце меню, после разделителя. Перед вызовом
`onClick` меню закрывается. Если меню открыто, а плагин добавил или убрал
пункт, список обновится сразу.

## Горячие клавиши

```js
// Ctrl+Shift+T — открыть «Избранное»
dzg.ui.addShortcut('Ctrl+Shift+T', () => {
  const me = dzg.users ? dzg.users.self() : null;
  if (me) dzg.chats.open(me);
});

// F8 — проверить орфографию в поле ввода
dzg.ui.addShortcut('F8', () => {
  const draft = dzg.ui.compose.get();
  if (draft) dzg.ui.compose.set(fixTypos(draft.text));
});
```

- Синтаксис как в Qt: `Ctrl`, `Shift`, `Alt`, `Meta` и клавиша через `+`.
  Примеры: `'Ctrl+Alt+P'`, `'Shift+F2'`, `'Ctrl+/'`.
- Работает в любом окне клиента. Сочетание плагина важнее встроенных
  сочетаний клиента, так что выбирай аккуратно.
- Аккорды вида `'Ctrl+Shift+K, Ctrl+C'` не поддерживаются. На них, как и на
  нераспознанную строку, бросается `Error('Invalid key sequence…')`.
- Если фокус в текстовом поле, сочетания **без** `Ctrl`/`Alt`/`Meta`
  (например, `'Shift+A'` или просто `'Q'`) не срабатывают. F-клавиши
  срабатывают всегда. Так плагин не мешает набирать текст.
- Если клавишу зажать, автоповтор не вызывает функцию много раз.

## Уведомления

```js
dzg.on('message.new', (m) => {
  if (!m.out && /срочно|urgent/i.test(m.text)) {
    dzg.ui.notify({
      title: `🔥 ${m.chat.name}`,
      text: `${m.fromName}: ${m.text}`,
      onClick: () => dzg.chats.open(m.chat, m.id),   // прыгнуть к сообщению
    });
  }
});

dzg.ui.notify('Готово!');                                   // просто текст
dzg.ui.notify({ text: 'Синхронизация завершена', silent: true });
```

Уведомление появляется поверх окна клиента и держится 5 секунд. Если есть
`onClick`, в уведомлении появляется кнопка «Открыть». Клик по ней или по
самому уведомлению активирует окно и вызывает `onClick`. Правый клик
закрывает уведомление.

Без `silent` проигрывается стандартный звук уведомлений (если звук включён в
настройках клиента). Если окно не в фокусе, ещё и мигает значок на панели
задач.

> Почему не системное уведомление ОС? Механизм уведомлений клиента
> рассчитан только на сообщения: ему нужен конкретный чат и сообщение, а
> клик по нему всегда открывает чат. Поэтому плагины используют свой
> канал, и `onClick` работает одинаково на всех системах.

## Ссылки

```js
dzg.ui.openLink('https://t.me/wetakush');      // откроет канал внутри клиента
dzg.ui.openLink('tg://settings');              // настройки
dzg.ui.openLink('tg://resolve?domain=durov');  // профиль
dzg.ui.openLink('https://github.com');         // обычная ссылка — в браузере
```

Ссылка обрабатывается так же, как клик по ней в сообщении. Ссылки на
Telegram открываются внутри клиента, остальные уходят в браузер. Чем
отличается от `dzg.openUrl`: тот всегда открывает браузер ОС.

## Поле ввода

```js
const draft = dzg.ui.compose.get();
// → { chat: { id: '…', name: 'Мама', … }, text: 'привет, как д' } или null

dzg.ui.compose.insert(' 🙂');           // вставить в позицию курсора
dzg.ui.compose.set('Новый текст');     // заменить всё, курсор в конец
dzg.ui.compose.focus();                // активировать окно и поле
```

«Текущее поле» — это поле активного окна, в котором сейчас фокус. Если
фокуса нет, берётся поле, где фокус был последним. Работает в обычных чатах,
в темах форумов, в ответах и в отложенных сообщениях. Если поля ввода нет
(не открыт чат или писать нельзя), `get()` вернёт `null`, а `set()` и
`insert()` вернут `false`.

### Кнопка у поля ввода

```js
// «ghbdtn» → «привет»: исправить текст, набранный не в той раскладке
dzg.ui.compose.addButton({
  icon: 'font',
  tooltip: 'Исправить раскладку',
  onClick() {
    const draft = dzg.ui.compose.get();
    if (draft) dzg.ui.compose.set(switchLayout(draft.text));
  },
});
```

Кнопка встаёт справа от поля, рядом с кнопками «запланированные» и эмодзи,
и поле ввода сужается под неё. Она видна, пока поле ввода показано. Если
писать в чат нельзя (канал, бан), кнопка прячется вместе с полем.

## Боковая панель

Это колонка справа в главном окне. Окно не перекрывается: чат сужается,
как при открытом профиле. У панели свой заголовок с крестиком и
прокручиваемое содержимое из родных элементов клиента, как в настройках.

```js
const panel = dzg.ui.panel({ title: 'Заметки', width: 360 });

panel.addLabel('Быстрые заметки', { bold: true });
const list = panel.addLabel(dzg.storage.get('notes', '') || 'Пока пусто', { muted: true });

const input = panel.addInput({
  placeholder: 'Новая заметка (Enter — сохранить)',
  onSubmit(text) {
    const notes = (dzg.storage.get('notes', '') + '\n• ' + text).trim();
    dzg.storage.set('notes', notes);
    list.setText(notes);
    input.setText('');
  },
});

panel.addToggle('Показывать при запуске', dzg.storage.get('autoOpen', false), (on) => {
  dzg.storage.set('autoOpen', on);
});

panel.addSeparator();
panel.addButton('Вставить все заметки в поле', () => {
  dzg.ui.compose.insert(dzg.storage.get('notes', ''));
});
panel.addButton('Очистить', () => {
  dzg.storage.remove('notes');
  list.setText('Пока пусто');
}, { style: 'light' });
panel.addButton('Открыть канал автора', () => dzg.ui.openLink('https://t.me/wetakush'), { icon: 'link' });

panel.on('close', () => console.log('панель закрыли'));
```

| Метод | Элемент |
|---|---|
| `addLabel(text, { bold, muted })` | Текст. `bold` — заголовок раздела, `muted` — серый. Текст можно менять: `label.setText()` |
| `addButton(text, onClick, { style, icon })` | Кнопка на всю ширину. `style: 'light'` — светлая, `'row'` или `icon` — строка как в настройках |
| `addInput({ placeholder, value, multiline, onSubmit, onChange })` | Поле ввода. Текст: `input.text()`, `input.setText()` |
| `addToggle(text, checked, onChange)` | Переключатель как в настройках |
| `addSeparator()` | Разделитель между разделами |
| `addSkip(px?)` | Пустое место |
| `clear()` | Удалить всё (заголовок остаётся) |
| `setTitle(text)`, `setWidth(px)` | Заголовок и ширина (240..720) |
| `show()` | Сделать вкладку активной |
| `scrollToBottom()` | Прокрутить вниз, например для лога |
| `close()`, `isOpen()`, `on('close', fn)` | Закрытие |

Все `add*` возвращают обычный `Widget` (см. core), кроме разделителя и
отступа. С ним работают и общие методы: `hide()`, `show()`,
`setEnabled(false)`, `geometry()`.

Как ведут себя панели:

- **Одна панель на плагин.** Повторный `dzg.ui.panel()` возвращает ту же
  открытую панель и обновляет заголовок и ширину. Поэтому переключать панель
  удобно так: `if (p && p.isOpen()) p.close(); else p = build();`.
- **Панели разных плагинов становятся вкладками.** Над ними появляется
  полоска вкладок с заголовками. Активна последняя открытая панель, и
  крестик закрывает только её.
- Если окно слишком узкое (чату остаётся меньше 380 px, а панели меньше
  240 px), панель временно прячется и вернётся, когда окно станет шире.
- Пока клиент заблокирован код-паролем, панель скрыта.
- `on('close')` срабатывает при любом закрытии: крестиком, через `close()`,
  при закрытии окна. Когда плагин выгружается, панель закрывается молча.

## Подмена текста при отрисовке

```js
// Спрятать спойлеры к сериалу во всех чатах
const words = /финал|концовк\p{L}*|убил\p{L}*/giu;
const off = dzg.messages.addRenderTransform((m) => {
  const text = m.text.replace(words, (w) => '█'.repeat(w.length));
  return text === m.text ? null : text;
});

// Показывать время отправки прямо в тексте (только входящие)
dzg.messages.addRenderTransform((m) => {
  if (m.out || !m.text) return null;
  const d = new Date(m.date * 1000);
  const time = `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
  return `[${time}] ${m.text}`;
});
```

Функция получает `Message` и возвращает строку, которая будет показана
вместо `message.text`. Вернуть `null` или `undefined` — значит оставить как
есть. Важно:

- Меняется **только отображение**. Само сообщение, его копирование,
  пересылка, поиск и `dzg.messages.get()` видят оригинал.
- Заменённый текст показывается без форматирования: жирный, ссылки и
  кастомные эмодзи оригинала пропадают. Если вернуть тот же текст,
  форматирование сохранится.
- Результат кешируется для каждого сообщения. Функция вызывается снова, если
  текст сообщения изменился (его отредактировали) или если любой плагин
  добавил или убрал трансформацию. Если правила зависят от настроек, после их
  смены вызови `dzg.messages.refreshRender()`.
- Трансформации из всех плагинов применяются по очереди: следующая получает
  в `message.text` результат предыдущей.
- Функция должна быть быстрой и синхронной: она вызывается, когда сообщение
  впервые появляется на экране. Пока трансформаций нет, клиент вообще не
  обращается к JS.
- Сервисные сообщения («X вступил в группу») не трогаются.

## События

```js
dzg.on('app.focus', ({ active }) => {
  if (active) dzg.ui.toast('С возвращением!');
});

// Живой счётчик символов в панели
dzg.on('compose.changed', ({ chat, text }) => {
  counter.setText(`${text.length} / 4096 в «${chat ? chat.name : '—'}»`);
});
```

| Событие | Данные | Когда |
|---|---|---|
| `app.focus` | `{ active: boolean }` | Окно клиента стало активным или неактивным |
| `compose.changed` | `{ chat: Peer \| null, text }` | Изменился текст в любом поле ввода сообщений. Приходит не чаще раза в ~300 мс, с последним текстом |

## Для разработчиков клиента

Состояние модуля лежит в `damza/plugins/ui/ui_state.*`, виджеты (кнопка,
ряд кнопок, панель, хост панелей, тост с действием) — в
`damza/plugins/ui/ui_widgets.*`. Точки входа для кода клиента описаны в
`damza/plugins/ui/ui_hooks.h`. Каждый хук в клиенте занимает одну строку
и помечен комментарием `// DamZaGram`:

| Файл | Хук |
|---|---|
| `history/view/history_view_top_bar_widget.cpp` `updateControlsVisibility` / `updateControlsGeometry` | `UpdateTopBarButtons`, `PlaceTopBarButtons` |
| `window/window_main_menu.cpp` `MainMenu::setupMenu` | `FillMainMenu` |
| `history/history_widget.cpp` конструктор, `updateFieldSize`, `moveFieldControls` | `RegisterComposeField`, `ComposeButtonsWidth`, `PlaceComposeButtons` |
| `history/view/controls/history_view_compose_controls.cpp` `init`, `updateControlsGeometry` | то же для тем, ответов и отложенных |
| `mainwindow.cpp` `MainWindow::updateControlsGeometry` | `PlaceSidePanels` |
| `history/view/history_view_element.cpp` `Element::validateText` | `DisplayText` |

Горячие клавиши работают через фильтр событий на `QApplication`. Он ставится
только после первой `addShortcut` и принимает `ShortcutOverride`, чтобы
сочетание не перехватил `QShortcut` клиента. JS-колбэки хранятся на стороне
JS под числовыми id. C++ вызывает их через диспатч `ui:call`, а id забытых
колбэков уведомлений возвращает через `ui:drop`.
