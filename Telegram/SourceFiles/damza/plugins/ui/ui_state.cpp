// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/plugins/ui/ui_state.h"

#include "base/qt_signal_producer.h"
#include "base/timer.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/core_settings.h"
#include "damza/damza_lang.h"
#include "damza/plugins/plugin_bridge.h"
#include "damza/plugins/plugin_data.h"
#include "damza/plugins/plugin_instance.h"
#include "damza/plugins/plugin_manager.h"
#include "damza/plugins/ui/ui_hooks.h"
#include "damza/plugins/ui/ui_widgets.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_top_bar_widget.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "settings/settings_common.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"
#include "styles/style_window_main_menu.h"
#include "ui/basic_click_handlers.h"
#include "ui/text/text_entity.h"
#include "ui/ui_utility.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "window/notifications_manager.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QKeySequence>
#include <QtWidgets/QAbstractSpinBox>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QTextEdit>

namespace Damza::Plugins::UiExt {
namespace {

constexpr auto kComposeChangedDelay = crl::time(300);
constexpr auto kRenderCacheLimit = 20000;
constexpr auto kMinPanelWidth = 240;
constexpr auto kDefaultPanelWidth = 340;

enum class Kind {
	TopBar,
	Compose,
	MainMenu,
	Shortcut,
};

struct Entry {
	Kind kind = Kind::TopBar;
	int id = 0;
	QString pluginId;
	ButtonSpec button;
	MenuItemSpec menu;
	Callback shortcut;
	QKeyCombination keys;
};

struct TopBarButton {
	int id = 0;
	Callback when;
	QPointer<PluginButton> button;
};

struct TopBarState {
	std::vector<TopBarButton> buttons;
	PeerData *peer = nullptr;
	bool built = false;
};

struct ComposeField {
	QPointer<Ui::InputField> field;
	QPointer<QWidget> owner;
	Fn<PeerData*()> peer;
	crl::time focused = 0;
};

struct ComposeHost {
	QPointer<ButtonsRow> row;
	Fn<PeerData*()> peer;
	Fn<void()> relayout;
};

struct PanelEntry {
	QString pluginId;
	int id = 0;
	QPointer<SidePanel> panel;
};

struct RenderCached {
	int generation = 0;
	QString original;
	std::optional<QString> result;
};

class ShortcutFilter final : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

};

struct State {
	std::vector<Entry> entries;
	int nextId = 0;
	base::flat_map<QWidget*, TopBarState> topBars;
	std::vector<ComposeField> fields;
	base::flat_map<QWidget*, ComposeHost> composeHosts;
	base::flat_map<QWidget*, QPointer<SidePanelHost>> panelHosts;
	std::vector<PanelEntry> panels;
	std::vector<QString> renderPlugins;
	std::map<std::pair<const void*, FullMsgId>, RenderCached> renderCache;
	int renderGeneration = 0;
	bool rendering = false;
	bool updatingTopBar = false;
	bool topBarRefreshQueued = false;
	bool composeRefreshQueued = false;
	bool mainMenuRefreshQueued = false;
	bool renderRefreshQueued = false;
	bool appActive = false;
	QPointer<ShortcutFilter> shortcutFilter;
	rpl::event_stream<> mainMenuChanges;
};

[[nodiscard]] State &S() {
	static auto result = State();
	return result;
}

[[nodiscard]] bool Alive(const QString &pluginId) {
	return Manager::Exists() && (Manager::Get().instance(pluginId) != nullptr);
}

[[nodiscard]] bool HasKind(Kind kind) {
	return ranges::any_of(S().entries, [&](const Entry &entry) {
		return (entry.kind == kind);
	});
}

void PruneDead() {
	auto &entries = S().entries;
	entries.erase(ranges::remove_if(entries, [](const Entry &entry) {
		return !Alive(entry.pluginId);
	}), end(entries));
}

[[nodiscard]] std::vector<Entry> EntriesOf(Kind kind) {
	PruneDead();
	auto result = std::vector<Entry>();
	for (const auto &entry : S().entries) {
		if (entry.kind == kind) {
			result.push_back(entry);
		}
	}
	return result;
}

[[nodiscard]] QVariant PeerValue(PeerData *peer) {
	return peer ? QVariant(SerializePeer(peer)) : QVariant();
}

void Drop(const Callback &callback) {
	if (!callback) {
		return;
	}
	crl::on_main([=] {
		if (!Manager::Exists()) {
			return;
		}
		if (const auto instance = Manager::Get().instance(callback.pluginId)) {
			instance->dispatch(u"ui:drop"_q, QVariantMap{
				{ u"ids"_q, QVariantList{ callback.id } },
			});
		}
	});
}

template <typename Callback>
void ForEachSession(Callback &&callback) {
	if (!Core::IsAppLaunched() || !Core::App().domain().started()) {
		return;
	}
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (account->sessionExists()) {
			callback(&account->session());
		}
	}
}

// Top bar.

void BuildTopBar(not_null<QWidget*> bar, TopBarState &state) {
	for (const auto &button : base::take(state.buttons)) {
		delete button.button.data();
	}
	const auto raw = bar.get();
	for (const auto &entry : EntriesOf(Kind::TopBar)) {
		const auto &spec = entry.button;
		const auto button = Ui::CreateChild<PluginButton>(
			raw,
			st::topBarSearch,
			LookupIcon(spec.icon),
			spec.text);
		if (!spec.tooltip.isEmpty()) {
			button->setToolTip(spec.tooltip);
			button->setAccessibleName(spec.tooltip);
		}
		const auto click = spec.click;
		button->setClickedCallback([=] {
			const auto &bars = S().topBars;
			const auto i = bars.find(raw);
			const auto peer = (i != end(bars)) ? i->second.peer : nullptr;
			CallLater(click, { PeerValue(peer) });
		});
		button->hide();
		state.buttons.push_back({ entry.id, spec.when, button });
	}
	state.built = true;
}

[[nodiscard]] TopBarState *TopBarStateFor(not_null<QWidget*> bar, bool create) {
	auto &bars = S().topBars;
	auto i = bars.find(bar.get());
	if (i == end(bars)) {
		if (!create) {
			return nullptr;
		}
		const auto raw = bar.get();
		i = bars.emplace(raw, TopBarState()).first;
		QObject::connect(raw, &QObject::destroyed, [=] {
			S().topBars.remove(raw);
		});
	}
	return &i->second;
}

void RefreshTopBars(bool rebuild) {
	auto bars = std::vector<QPointer<HistoryView::TopBarWidget>>();
	const auto any = HasKind(Kind::TopBar);
	for (const auto widget : QApplication::allWidgets()) {
		if (const auto bar = dynamic_cast<HistoryView::TopBarWidget*>(widget)) {
			bars.push_back(bar);
		}
	}
	for (const auto &bar : bars) {
		if (!bar) {
			continue;
		}
		if (const auto state = TopBarStateFor(bar.data(), any)) {
			if (rebuild) {
				BuildTopBar(bar.data(), *state);
			}
		}
		if (bar) {
			bar->updateControlsVisibility();
		}
	}
}

void ScheduleTopBarRefresh() {
	auto &state = S();
	if (state.topBarRefreshQueued) {
		return;
	}
	state.topBarRefreshQueued = true;
	crl::on_main([] {
		S().topBarRefreshQueued = false;
		RefreshTopBars(true);
	});
}

// Compose area.

void BuildCompose(not_null<QWidget*> owner) {
	auto &hosts = S().composeHosts;
	const auto i = hosts.find(owner.get());
	if (i == end(hosts)) {
		return;
	}
	auto &host = i->second;
	if (!host.row) {
		host.row = Ui::CreateChild<ButtonsRow>(owner.get());
		host.row->hide();
		host.row->lower();
	}
	const auto row = host.row.data();
	const auto peer = host.peer;
	auto buttons = std::vector<not_null<Ui::RpWidget*>>();
	for (const auto &entry : EntriesOf(Kind::Compose)) {
		const auto &spec = entry.button;
		const auto button = Ui::CreateChild<PluginButton>(
			row,
			st::historyAttach,
			LookupIcon(spec.icon),
			spec.text);
		if (!spec.tooltip.isEmpty()) {
			button->setToolTip(spec.tooltip);
			button->setAccessibleName(spec.tooltip);
		}
		const auto click = spec.click;
		button->setClickedCallback([=] {
			CallLater(click, { PeerValue(peer ? peer() : nullptr) });
		});
		buttons.push_back(button);
	}
	row->setButtons(std::move(buttons));
	if (const auto relayout = host.relayout) {
		relayout();
	}
}

void ScheduleComposeRefresh() {
	auto &state = S();
	if (state.composeRefreshQueued) {
		return;
	}
	state.composeRefreshQueued = true;
	crl::on_main([] {
		S().composeRefreshQueued = false;
		auto owners = std::vector<QWidget*>();
		for (const auto &[owner, host] : S().composeHosts) {
			owners.push_back(owner);
		}
		for (const auto owner : owners) {
			BuildCompose(owner);
		}
	});
}

[[nodiscard]] ComposeField *CurrentField() {
	auto &fields = S().fields;
	const auto window = Manager::Exists()
		? Manager::Get().activeWindow()
		: nullptr;
	const auto top = window ? window->widget()->window() : nullptr;
	auto best = (ComposeField*)nullptr;
	for (auto &entry : fields) {
		const auto field = entry.field.data();
		if (!field || !field->isVisible()) {
			continue;
		} else if (top && field->window() != top) {
			continue;
		} else if (Ui::InFocusChain(field)) {
			return &entry;
		} else if (!best || entry.focused > best->focused) {
			best = &entry;
		}
	}
	return best;
}

// Main menu.

void ScheduleMainMenuRefresh() {
	auto &state = S();
	if (state.mainMenuRefreshQueued) {
		return;
	}
	state.mainMenuRefreshQueued = true;
	crl::on_main([] {
		S().mainMenuRefreshQueued = false;
		S().mainMenuChanges.fire({});
	});
}

// Shortcuts.

[[nodiscard]] bool TextInputFocused() {
	const auto focused = QApplication::focusWidget();
	return focused
		&& (qobject_cast<QTextEdit*>(focused)
			|| qobject_cast<QPlainTextEdit*>(focused)
			|| qobject_cast<QLineEdit*>(focused)
			|| qobject_cast<QAbstractSpinBox*>(focused));
}

[[nodiscard]] bool IsModifierKey(int key) {
	return (key == Qt::Key_Shift)
		|| (key == Qt::Key_Control)
		|| (key == Qt::Key_Alt)
		|| (key == Qt::Key_Meta)
		|| (key == Qt::Key_AltGr)
		|| (key == Qt::Key_CapsLock)
		|| (key == 0)
		|| (key == Qt::Key_unknown);
}

bool ShortcutFilter::eventFilter(QObject *object, QEvent *event) {
	const auto type = event->type();
	if (type != QEvent::KeyPress && type != QEvent::ShortcutOverride) {
		return false;
	}
	const auto e = static_cast<QKeyEvent*>(event);
	const auto key = e->key();
	if (IsModifierKey(key)) {
		return false;
	}
	const auto modifiers = e->modifiers() & (Qt::ShiftModifier
		| Qt::ControlModifier
		| Qt::AltModifier
		| Qt::MetaModifier);
	const auto combination = QKeyCombination(modifiers, Qt::Key(key));
	auto matched = Callback();
	for (const auto &entry : S().entries) {
		if (entry.kind == Kind::Shortcut
			&& entry.keys == combination
			&& Alive(entry.pluginId)) {
			matched = entry.shortcut;
			break;
		}
	}
	if (!matched) {
		return false;
	}
	const auto strong = (modifiers
		& (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
	const auto functionKey = (key >= Qt::Key_F1 && key <= Qt::Key_F35);
	if (!strong && !functionKey && TextInputFocused()) {
		return false;
	}
	if (type == QEvent::ShortcutOverride) {
		e->accept();
		return true;
	}
	if (!e->isAutoRepeat()) {
		CallLater(matched);
	}
	return true;
}

void EnsureShortcutFilter() {
	auto &state = S();
	if (state.shortcutFilter || !qApp) {
		return;
	}
	state.shortcutFilter = new ShortcutFilter(qApp);
	qApp->installEventFilter(state.shortcutFilter.data());
}

// Panels.

[[nodiscard]] SidePanelHost *EnsurePanelHost(
		not_null<Window::MainWindow*> window) {
	auto &hosts = S().panelHosts;
	const auto raw = window.get();
	const auto i = hosts.find(raw);
	if (i != end(hosts) && i->second) {
		return i->second.data();
	}
	const auto host = Ui::CreateChild<SidePanelHost>(window->bodyWidget());
	hosts[raw] = host;
	const auto weak = QPointer<Window::MainWindow>(raw);
	host->layoutRequests() | rpl::on_next([=] {
		if (weak) {
			weak->updateControlsGeometry();
		}
	}, host->lifetime());
	QObject::connect(raw, &QObject::destroyed, [=] {
		S().panelHosts.remove(raw);
	});
	return host;
}

[[nodiscard]] SidePanel *PanelById(not_null<Bridge*> bridge, int id) {
	return dynamic_cast<SidePanel*>(bridge->widgetById(id));
}

// Render transforms.

void RefreshRenderedViews() {
	ForEachSession([](not_null<Main::Session*> session) {
		auto &data = session->data();
		const auto refresh = [&](not_null<PeerData*> peer) {
			const auto history = data.historyLoaded(peer);
			if (!history) {
				return;
			}
			auto items = std::vector<not_null<HistoryItem*>>();
			for (const auto &block : history->blocks) {
				for (const auto &view : block->messages) {
					items.push_back(view->data());
				}
			}
			for (const auto item : items) {
				data.requestItemTextRefresh(item);
			}
		};
		data.enumerateUsers([&](not_null<UserData*> user) {
			refresh(user);
		});
		data.enumerateGroups(refresh);
		data.enumerateBroadcasts([&](not_null<ChannelData*> channel) {
			refresh(channel);
		});
	});
}

} // namespace

QVariant Call(const Callback &callback, const QVariantList &args) {
	if (!callback || !Manager::Exists()) {
		return QVariant();
	}
	const auto instance = Manager::Get().instance(callback.pluginId);
	if (!instance) {
		return QVariant();
	}
	return instance->dispatch(u"ui:call"_q, QVariantMap{
		{ u"id"_q, callback.id },
		{ u"args"_q, args },
	});
}

void CallLater(const Callback &callback, QVariantList args) {
	if (!callback) {
		return;
	}
	crl::on_main([=] {
		Call(callback, args);
	});
}

int AddTopBarButton(ButtonSpec spec) {
	if (!spec.click || !Alive(spec.click.pluginId)) {
		return 0;
	}
	if (!LookupIcon(spec.icon) && spec.text.isEmpty()) {
		spec.icon = u"customize"_q;
	}
	auto &state = S();
	const auto id = ++state.nextId;
	state.entries.push_back({
		.kind = Kind::TopBar,
		.id = id,
		.pluginId = spec.click.pluginId,
		.button = std::move(spec),
	});
	ScheduleTopBarRefresh();
	return id;
}

int AddComposeButton(ButtonSpec spec) {
	if (!spec.click || !Alive(spec.click.pluginId)) {
		return 0;
	}
	if (!LookupIcon(spec.icon) && spec.text.isEmpty()) {
		spec.icon = u"customize"_q;
	}
	auto &state = S();
	const auto id = ++state.nextId;
	state.entries.push_back({
		.kind = Kind::Compose,
		.id = id,
		.pluginId = spec.click.pluginId,
		.button = std::move(spec),
	});
	ScheduleComposeRefresh();
	return id;
}

int AddMainMenuItem(MenuItemSpec spec) {
	if (!spec.click || !Alive(spec.click.pluginId) || spec.title.isEmpty()) {
		return 0;
	}
	auto &state = S();
	const auto id = ++state.nextId;
	state.entries.push_back({
		.kind = Kind::MainMenu,
		.id = id,
		.pluginId = spec.click.pluginId,
		.menu = std::move(spec),
	});
	ScheduleMainMenuRefresh();
	return id;
}

int AddShortcut(Callback callback, const QString &keys) {
	if (!callback || !Alive(callback.pluginId)) {
		return 0;
	}
	const auto sequence = QKeySequence::fromString(
		keys.trimmed(),
		QKeySequence::PortableText);
	if (sequence.count() != 1
		|| IsModifierKey(sequence[0].key())) {
		return 0;
	}
	auto &state = S();
	const auto id = ++state.nextId;
	state.entries.push_back({
		.kind = Kind::Shortcut,
		.id = id,
		.pluginId = callback.pluginId,
		.shortcut = callback,
		.keys = sequence[0],
	});
	EnsureShortcutFilter();
	return id;
}

bool Remove(const QString &pluginId, int id) {
	auto &entries = S().entries;
	const auto i = ranges::find_if(entries, [&](const Entry &entry) {
		return (entry.id == id) && (entry.pluginId == pluginId);
	});
	if (i == end(entries)) {
		return false;
	}
	const auto kind = i->kind;
	entries.erase(i);
	switch (kind) {
	case Kind::TopBar: ScheduleTopBarRefresh(); break;
	case Kind::Compose: ScheduleComposeRefresh(); break;
	case Kind::MainMenu: ScheduleMainMenuRefresh(); break;
	case Kind::Shortcut: break;
	}
	return true;
}

void RefreshButtons() {
	crl::on_main([] {
		RefreshTopBars(false);
	});
}

void Notify(const QString &pluginId, const QVariantMap &spec) {
	const auto title = spec.value(u"title"_q).toString();
	const auto text = spec.value(u"text"_q).toString();
	const auto click = Callback{
		.pluginId = pluginId,
		.id = spec.value(u"click"_q).toInt(),
	};
	const auto silent = spec.value(u"silent"_q).toBool();
	const auto window = Manager::Exists()
		? Manager::Get().activeWindow()
		: nullptr;
	const auto weakWindow = window
		? QPointer<QWidget>(window->widget().get())
		: QPointer<QWidget>();
	auto activate = Fn<void()>();
	if (click) {
		activate = [=] {
			if (Manager::Exists()) {
				if (const auto window = Manager::Get().activeWindow()) {
					window->activate();
				}
			}
			CallLater(click);
		};
	}
	ShowActionToast(
		title,
		text,
		Damza::Text("Open", "Открыть"),
		std::move(activate),
		[=] { Drop(click); });
	if (silent) {
		return;
	}
	if (Core::App().settings().soundNotify()) {
		if (const auto session = Manager::Get().activeSession()) {
			Core::App().notifications().playSound(session, DocumentId());
		}
	}
	if (QGuiApplication::applicationState() != Qt::ApplicationActive
		&& weakWindow) {
		QApplication::alert(weakWindow->window());
	}
}

void OpenLink(const QString &url) {
	const auto trimmed = url.trimmed();
	if (trimmed.isEmpty()) {
		return;
	}
	const auto controller = Manager::Exists()
		? Manager::Get().activeController()
		: nullptr;
	auto context = ClickHandlerContext();
	if (controller) {
		context.sessionWindow = base::make_weak(controller);
	}
	crl::on_main([=] {
		UrlClickHandler::Open(trimmed, QVariant::fromValue(context));
	});
}

QVariant ComposeGet() {
	const auto entry = CurrentField();
	if (!entry) {
		return QVariant();
	}
	const auto peer = entry->peer ? entry->peer() : nullptr;
	return QVariantMap{
		{ u"chat"_q, PeerValue(peer) },
		{ u"text"_q, entry->field->getLastText() },
	};
}

bool ComposeSet(const QString &text) {
	const auto entry = CurrentField();
	if (!entry) {
		return false;
	}
	const auto field = entry->field.data();
	field->setTextWithTags({ text, {} });
	field->setCursorPosition(text.size());
	return true;
}

bool ComposeInsert(const QString &text) {
	const auto entry = CurrentField();
	if (!entry) {
		return false;
	}
	const auto field = entry->field.data();
	auto cursor = field->textCursor();
	cursor.insertText(text);
	field->setTextCursor(cursor);
	return true;
}

void ComposeFocus() {
	const auto entry = CurrentField();
	if (!entry) {
		return;
	}
	const auto field = entry->field.data();
	if (Manager::Exists()) {
		if (const auto window = Manager::Get().activeWindow()) {
			window->activate();
		}
	}
	field->setFocusFast();
}

QVariant PanelOpen(not_null<Bridge*> bridge, const QVariantMap &spec) {
	const auto pluginId = bridge->instance()->manifest().id;
	const auto title = spec.value(u"title"_q).toString();
	const auto width = spec.value(u"width"_q).toInt();
	const auto desired = (width > 0) ? width : kDefaultPanelWidth;
	auto &panels = S().panels;
	for (const auto &entry : panels) {
		if (entry.pluginId != pluginId || !entry.panel) {
			continue;
		}
		const auto panel = entry.panel.data();
		if (!title.isEmpty()) {
			panel->setTitle(title);
		}
		panel->setDesiredWidth(desired);
		if (const auto host = dynamic_cast<SidePanelHost*>(
				panel->parentWidget())) {
			host->activate(panel);
		}
		return QVariantMap{
			{ u"id"_q, entry.id },
			{ u"content"_q, bridge->registerWidget(panel->content()) },
			{ u"reused"_q, true },
		};
	}
	const auto window = Manager::Get().activeWindow();
	if (!window || !window->sessionController()) {
		return QVariant();
	}
	const auto mainWindow = not_null<Window::MainWindow*>(
		window->widget().get());
	const auto host = EnsurePanelHost(mainWindow);
	const auto panel = Ui::CreateChild<SidePanel>(
		host,
		pluginId,
		title,
		desired);
	bridge->adoptWidget(panel);
	const auto id = bridge->registerWidget(panel);
	const auto content = bridge->registerWidget(panel->content());
	panels.push_back({ pluginId, id, panel });
	const auto weak = QPointer<SidePanel>(panel);
	panel->closeRequests() | rpl::on_next([=] {
		crl::on_main([=] {
			delete weak.data();
		});
	}, panel->lifetime());
	QObject::connect(panel, &QObject::destroyed, [=] {
		auto &list = S().panels;
		list.erase(ranges::remove_if(list, [&](const PanelEntry &entry) {
			return !entry.panel
				|| (entry.pluginId == pluginId && entry.id == id);
		}), end(list));
		if (!Manager::Exists()) {
			return;
		}
		if (const auto instance = Manager::Get().instance(pluginId)) {
			instance->dispatch(u"ui:panelClosed"_q, QVariantMap{
				{ u"id"_q, id },
			});
		}
	});
	host->add(panel);
	return QVariantMap{
		{ u"id"_q, id },
		{ u"content"_q, content },
		{ u"reused"_q, false },
	};
}

QVariant PanelAdd(
		not_null<Bridge*> bridge,
		int panelId,
		const QString &type,
		const QVariantMap &props) {
	const auto panel = PanelById(bridge, panelId);
	if (!panel) {
		return QVariant();
	}
	const auto weak = QPointer<Bridge>(bridge.get());
	const auto id = std::make_shared<int>(0);
	const auto widget = panel->addItem(type, props, [=](
			QString event,
			QVariant value) {
		crl::on_main([=] {
			if (weak && *id) {
				weak->widgetEvent(*id, event, value);
			}
		});
	});
	if (!widget) {
		return QVariant();
	}
	*id = bridge->registerWidget(widget);
	return *id;
}

bool PanelSet(
		not_null<Bridge*> bridge,
		int panelId,
		const QVariantMap &props) {
	const auto panel = PanelById(bridge, panelId);
	if (!panel) {
		return false;
	}
	if (props.contains(u"title"_q)) {
		panel->setTitle(props.value(u"title"_q).toString());
	}
	if (props.contains(u"width"_q)) {
		panel->setDesiredWidth(props.value(u"width"_q).toInt());
	}
	if (props.value(u"activate"_q).toBool()) {
		if (const auto host = dynamic_cast<SidePanelHost*>(
				panel->parentWidget())) {
			host->activate(panel);
		}
	}
	if (props.value(u"scrollToBottom"_q).toBool()) {
		const auto weak = QPointer<SidePanel>(panel);
		crl::on_main([=] {
			if (weak) {
				weak->scrollToBottom();
			}
		});
	}
	return true;
}

bool PanelClear(not_null<Bridge*> bridge, int panelId) {
	const auto panel = PanelById(bridge, panelId);
	if (!panel) {
		return false;
	}
	panel->content()->clear();
	return true;
}

bool PanelClose(not_null<Bridge*> bridge, int panelId) {
	const auto panel = PanelById(bridge, panelId);
	if (!panel) {
		return false;
	}
	delete panel;
	return true;
}

bool PanelIsOpen(not_null<Bridge*> bridge, int panelId) {
	return PanelById(bridge, panelId) != nullptr;
}

void SetRenderTransforms(const QString &pluginId, bool has) {
	auto &list = S().renderPlugins;
	const auto i = ranges::find(list, pluginId);
	if (has && i == end(list)) {
		list.push_back(pluginId);
	} else if (!has && i != end(list)) {
		list.erase(i);
	} else if (!has) {
		return;
	}
	RefreshRender();
}

void RefreshRender() {
	auto &state = S();
	++state.renderGeneration;
	state.renderCache.clear();
	if (state.renderRefreshQueued) {
		return;
	}
	state.renderRefreshQueued = true;
	crl::on_main([] {
		S().renderRefreshQueued = false;
		RefreshRenderedViews();
	});
}

void AppStarted() {
	S().appActive = (QGuiApplication::applicationState()
		== Qt::ApplicationActive);
	base::qt_signal_producer(
		static_cast<QGuiApplication*>(qApp),
		&QGuiApplication::applicationStateChanged
	) | rpl::on_next([](Qt::ApplicationState state) {
		const auto active = (state == Qt::ApplicationActive);
		if (S().appActive == active || !Manager::Exists()) {
			return;
		}
		S().appActive = active;
		Manager::Get().emitEvent(
			u"app.focus"_q,
			QVariantMap{ { u"active"_q, active } });
	}, Manager::Get().lifetime());
}

void PluginUnloaded(const QString &pluginId) {
	auto &state = S();
	auto kinds = base::flat_set<Kind>();
	state.entries.erase(ranges::remove_if(state.entries, [&](
			const Entry &entry) {
		if (entry.pluginId == pluginId) {
			kinds.emplace(entry.kind);
			return true;
		}
		return false;
	}), end(state.entries));
	if (kinds.contains(Kind::TopBar)) {
		ScheduleTopBarRefresh();
	}
	if (kinds.contains(Kind::Compose)) {
		ScheduleComposeRefresh();
	}
	if (kinds.contains(Kind::MainMenu)) {
		ScheduleMainMenuRefresh();
	}
	auto panels = std::vector<QPointer<SidePanel>>();
	for (const auto &entry : state.panels) {
		if (entry.pluginId == pluginId) {
			panels.push_back(entry.panel);
		}
	}
	for (const auto &panel : panels) {
		delete panel.data();
	}
	SetRenderTransforms(pluginId, false);
}

} // namespace Damza::Plugins::UiExt

namespace Damza::Plugins {

using namespace UiExt;

void UpdateTopBarButtons(
		not_null<HistoryView::TopBarWidget*> bar,
		PeerData *peer) {
	auto &state = S();
	const auto data = TopBarStateFor(bar.get(), HasKind(Kind::TopBar));
	if (!data) {
		return;
	}
	if (!data->built) {
		BuildTopBar(bar.get(), *data);
	}
	data->peer = peer;
	if (state.updatingTopBar) {
		return;
	}
	state.updatingTopBar = true;
	const auto buttons = data->buttons;
	auto serialized = std::optional<QVariant>();
	for (const auto &button : buttons) {
		if (!button.button) {
			continue;
		}
		auto visible = (peer != nullptr);
		if (visible && button.when) {
			if (!serialized) {
				serialized = PeerValue(peer);
			}
			visible = Call(button.when, { *serialized }).toBool();
		}
		if (button.button) {
			button.button->setVisible(visible);
		}
	}
	state.updatingTopBar = false;
}

int PlaceTopBarButtons(
		not_null<HistoryView::TopBarWidget*> bar,
		int right,
		int top) {
	const auto data = TopBarStateFor(bar.get(), false);
	if (!data) {
		return 0;
	}
	auto taken = 0;
	for (const auto &button : data->buttons) {
		if (!button.button || button.button->isHidden()) {
			continue;
		}
		button.button->moveToRight(right + taken, top);
		taken += button.button->width();
	}
	return taken;
}

void FillMainMenu(
		not_null<Ui::VerticalLayout*> menu,
		not_null<Window::SessionController*> controller) {
	const auto wrap = menu->add(object_ptr<Ui::VerticalLayout>(menu));
	const auto weak = base::make_weak(controller);
	const auto fill = [=] {
		wrap->clear();
		const auto entries = EntriesOf(Kind::MainMenu);
		if (!entries.empty()) {
			wrap->add(
				object_ptr<Ui::PlainShadow>(wrap),
				{ 0, st::mainMenuSkip, 0, st::mainMenuSkip });
		}
		for (const auto &entry : entries) {
			const auto icon = LookupIcon(entry.menu.icon);
			const auto click = entry.menu.click;
			Settings::AddButtonWithIcon(
				wrap,
				rpl::single(entry.menu.title),
				st::mainMenuButton,
				{ icon ? icon : &st::menuIconCustomize }
			)->setClickedCallback([=] {
				if (const auto strong = weak.get()) {
					strong->window().hideSettingsAndLayer();
				}
				CallLater(click);
			});
		}
		wrap->resizeToWidth(menu->width());
	};
	fill();
	S().mainMenuChanges.events(
	) | rpl::on_next(fill, wrap->lifetime());
}

void RegisterComposeField(
		not_null<QWidget*> owner,
		not_null<Ui::InputField*> field,
		Fn<PeerData*()> peer,
		Fn<void()> relayout) {
	auto &state = S();
	const auto raw = field.get();
	state.fields.push_back({
		.field = raw,
		.owner = owner.get(),
		.peer = peer,
	});
	field->focusedChanges(
	) | rpl::filter(rpl::mappers::_1) | rpl::on_next([=] {
		for (auto &entry : S().fields) {
			if (entry.field == raw) {
				entry.focused = crl::now();
			}
		}
	}, field->lifetime());
	const auto timer = field->lifetime().make_state<base::Timer>([=] {
		if (!Manager::Exists()) {
			return;
		}
		Manager::Get().emitEvent(u"compose.changed"_q, QVariantMap{
			{ u"chat"_q, PeerValue(peer ? peer() : nullptr) },
			{ u"text"_q, raw->getLastText() },
		});
	});
	field->changes() | rpl::on_next([=] {
		if (!timer->isActive()) {
			timer->callOnce(kComposeChangedDelay);
		}
	}, field->lifetime());
	QObject::connect(raw, &QObject::destroyed, [=] {
		auto &fields = S().fields;
		fields.erase(ranges::remove_if(fields, [&](const ComposeField &f) {
			return !f.field || (f.field.data() == raw);
		}), end(fields));
	});

	auto &hosts = state.composeHosts;
	if (!hosts.contains(owner.get())) {
		const auto ownerRaw = owner.get();
		hosts.emplace(ownerRaw, ComposeHost{
			.peer = std::move(peer),
			.relayout = std::move(relayout),
		});
		QObject::connect(ownerRaw, &QObject::destroyed, [=] {
			S().composeHosts.remove(ownerRaw);
		});
		if (HasKind(Kind::Compose)) {
			// The owner may still be in its constructor: lay out later.
			const auto weak = QPointer<QWidget>(ownerRaw);
			crl::on_main([=] {
				if (const auto strong = weak.data()) {
					BuildCompose(strong);
				}
			});
		}
	}
}

int ComposeButtonsWidth(not_null<QWidget*> owner, bool shown) {
	if (!shown) {
		return 0;
	}
	const auto &hosts = S().composeHosts;
	const auto i = hosts.find(owner.get());
	return (i != end(hosts) && i->second.row)
		? i->second.row->contentWidth()
		: 0;
}

int PlaceComposeButtons(
		not_null<QWidget*> owner,
		int right,
		int top,
		bool shown) {
	const auto &hosts = S().composeHosts;
	const auto i = hosts.find(owner.get());
	if (i == end(hosts) || !i->second.row) {
		return 0;
	}
	const auto row = i->second.row.data();
	const auto width = row->contentWidth();
	const auto visible = shown && (width > 0);
	row->setVisible(visible);
	if (!visible) {
		return 0;
	}
	row->moveToRight(right, top);
	return width;
}

int PlaceSidePanels(not_null<Window::MainWindow*> window, QRect area) {
	auto &hosts = S().panelHosts;
	const auto i = hosts.find(window.get());
	if (i == end(hosts)) {
		return 0;
	}
	const auto host = i->second.data();
	if (!host) {
		hosts.erase(i);
		return 0;
	}
	const auto controller = window->sessionController();
	if (host->empty()
		|| !controller
		|| window->controller().locked()) {
		host->hide();
		return 0;
	}
	// Dock as a column when the window is wide enough, otherwise float
	// over the right part of the chat instead of disappearing.
	const auto available = area.width() - st::windowMinWidth;
	const auto docked = (available >= kMinPanelWidth);
	const auto width = docked
		? std::min(host->desiredWidth(), available)
		: std::min(host->desiredWidth(), area.width());
	host->setGeometry(
		area.x() + area.width() - width,
		area.y(),
		width,
		area.height());
	if (host->isHidden()) {
		host->show();
	}
	if (docked) {
		host->stackUnder(controller->content());
	} else {
		host->raise();
	}
	return docked ? width : 0;
}

TextWithEntities DisplayText(
		not_null<HistoryItem*> item,
		TextWithEntities &&text) {
	auto &state = S();
	if (state.renderPlugins.empty()
		|| state.rendering
		|| !Manager::Exists()) {
		return std::move(text);
	}
	const auto key = std::make_pair(
		static_cast<const void*>(&item->history()->session()),
		item->fullId());
	auto &cache = state.renderCache;
	const auto i = cache.find(key);
	if (i != end(cache)
		&& i->second.generation == state.renderGeneration
		&& i->second.original == text.text) {
		if (i->second.result) {
			return TextWithEntities{ *i->second.result };
		}
		return std::move(text);
	}
	state.rendering = true;
	auto payload = SerializeMessage(item);
	auto result = std::optional<QString>();
	const auto plugins = state.renderPlugins;
	for (const auto &pluginId : plugins) {
		const auto instance = Manager::Get().instance(pluginId);
		if (!instance) {
			continue;
		}
		payload.insert(u"text"_q, result.value_or(text.text));
		const auto value = instance->dispatch(u"ui:render"_q, payload);
		if (value.typeId() == QMetaType::QString) {
			result = value.toString();
		}
	}
	state.rendering = false;
	if (result && *result == text.text) {
		result = std::nullopt;
	}
	if (cache.size() > kRenderCacheLimit) {
		cache.clear();
	}
	cache[key] = RenderCached{
		.generation = state.renderGeneration,
		.original = text.text,
		.result = result,
	};
	if (result) {
		return TextWithEntities{ *result };
	}
	return std::move(text);
}

} // namespace Damza::Plugins
