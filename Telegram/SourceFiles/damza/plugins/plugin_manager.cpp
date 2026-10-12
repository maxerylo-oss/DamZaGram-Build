// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/plugins/plugin_manager.h"

#include "api/api_common.h"
#include "core/application.h"
#include "damza/damza_lang.h"
#include "damza/plugins/plugin_api.h"
#include "damza/plugins/plugin_data.h"
#include "damza/plugins/plugin_instance.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings.h"
#include "styles/style_menu_icons.h"
#include "ui/toast/toast.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>

namespace Damza::Plugins {
namespace {

constexpr auto kReloadDelay = 300;
constexpr auto kErrorToastDelay = crl::time(3000);

std::unique_ptr<Manager> Global;
base::flat_map<QString, crl::time> LastErrorToast;

[[nodiscard]] bool IsMainThread() {
	return QThread::currentThread() == QCoreApplication::instance()->thread();
}

[[nodiscard]] QVariantMap Event(const QString &name, const QVariant &data) {
	return { { u"name"_q, name }, { u"data"_q, data } };
}

} // namespace

Manager &Manager::Get() {
	Expects(Global != nullptr);

	return *Global;
}

bool Manager::Exists() {
	return Global != nullptr;
}

Manager::Manager() = default;

Manager::~Manager() {
	_lifetime.destroy();
	_accountLifetimes.clear();
	_instances.clear();
}

QString Manager::pluginsPath() const {
	return cWorkingDir() + u"tdata/plugins/"_q;
}

QString Manager::dataPath() const {
	return cWorkingDir() + u"tdata/plugins_data/"_q;
}

void Manager::start() {
	if (_started) {
		return;
	}
	_started = true;
	QDir().mkpath(pluginsPath());
	QDir().mkpath(dataPath());
	loadState();
	for (const auto &module : ApiModules()) {
		if (module.appStarted) {
			module.appStarted();
		}
	}
	rescan();
	trackAccounts();

	if (const auto window = Core::App().activePrimaryWindow()) {
		window->sessionControllerValue(
		) | rpl::map([](Window::SessionController *controller) {
			return controller
				? rpl::producer<Dialogs::Key>(
					controller->activeChatChanges())
				: rpl::producer<Dialogs::Key>(rpl::never<Dialogs::Key>());
		}) | rpl::flatten_latest(
		) | rpl::on_next([=](Dialogs::Key key) {
			if (const auto peer = key.peer()) {
				emitEvent(u"chat.opened"_q, SerializePeer(peer));
			}
		}, _lifetime);
	}
}

void Manager::shutdown() {
	saveState();
	_watcher = nullptr;
	while (!_instances.empty()) {
		unloadPlugin(_instances.back()->manifest().id);
	}
	_lifetime.destroy();
}

void Manager::loadState() {
	auto file = QFile(dataPath() + u"_state.json"_q);
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto root = QJsonDocument::fromJson(file.readAll()).object();
	const auto enabled = root.value(u"enabled"_q).toObject();
	for (auto i = enabled.begin(); i != enabled.end(); ++i) {
		_enabled[i.key()] = i.value().toBool();
	}
	const auto settings = root.value(u"settings"_q).toObject();
	for (auto i = settings.begin(); i != settings.end(); ++i) {
		_settings[i.key()] = i.value().toObject();
	}
	_devMode = root.value(u"devMode"_q).toBool(true);
}

void Manager::saveState() const {
	auto enabled = QJsonObject();
	for (const auto &[id, value] : _enabled) {
		enabled.insert(id, value);
	}
	auto settings = QJsonObject();
	for (const auto &[id, values] : _settings) {
		settings.insert(id, values);
	}
	const auto root = QJsonObject{
		{ u"enabled"_q, enabled },
		{ u"settings"_q, settings },
		{ u"devMode"_q, _devMode },
	};
	QDir().mkpath(dataPath());
	auto file = QFile(dataPath() + u"_state.json"_q);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	}
}

std::optional<Manifest> Manager::readManifest(
		const QString &dir,
		QString *error) const {
	auto file = QFile(QDir(dir).filePath(u"manifest.json"_q));
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	auto parseError = QJsonParseError();
	const auto root = QJsonDocument::fromJson(
		file.readAll(),
		&parseError).object();
	if (parseError.error != QJsonParseError::NoError) {
		*error = u"manifest.json: "_q + parseError.errorString();
	}
	auto result = Manifest();
	result.dir = dir;
	result.id = root.value(u"id"_q).toString(QFileInfo(dir).fileName());
	result.name = root.value(u"name"_q).toString(result.id);
	result.version = root.value(u"version"_q).toString(u"0.0.0"_q);
	result.author = root.value(u"author"_q).toString();
	result.description = root.value(u"description"_q).toString();
	result.main = root.value(u"main"_q).toString(u"main.js"_q);
	result.type = root.value(u"type"_q).toString(u"module"_q);
	result.settings = root.value(u"settings"_q).toArray();
	return result;
}

void Manager::rescan() {
	while (!_instances.empty()) {
		unloadPlugin(_instances.back()->manifest().id);
	}
	_plugins.clear();

	const auto root = QDir(pluginsPath());
	const auto dirs = root.entryList(
		QDir::Dirs | QDir::NoDotAndDotDot,
		QDir::Name);
	for (const auto &name : dirs) {
		auto error = QString();
		const auto manifest = readManifest(root.filePath(name), &error);
		if (!manifest) {
			continue;
		}
		const auto duplicate = ranges::any_of(_plugins, [&](const auto &p) {
			return p.manifest.id == manifest->id;
		});
		if (duplicate) {
			continue;
		}
		auto state = PluginState{ .manifest = *manifest, .error = error };
		const auto i = _enabled.find(manifest->id);
		state.enabled = (i != end(_enabled)) && i->second;
		_plugins.push_back(std::move(state));
	}
	for (auto &state : _plugins) {
		if (state.enabled && state.error.isEmpty()) {
			loadPlugin(state);
		}
	}
	invalidateFilters();
	watchFiles();
	notifyChanged();
}

void Manager::loadPlugin(PluginState &state) {
	unloadPlugin(state.manifest.id);
	state.error = QString();
	auto instance = std::make_unique<Instance>(state.manifest);
	const auto raw = instance.get();
	_instances.push_back(std::move(instance));
	if (!raw->load()) {
		state.error = raw->error();
		state.loaded = false;
		_instances.pop_back();
		return;
	}
	state.loaded = true;
	raw->dispatch(u"event"_q, QVariant(Event(u"ready"_q, QVariant())));
}

void Manager::unloadPlugin(const QString &id) {
	const auto i = ranges::find(_instances, id, [](const auto &instance) {
		return instance->manifest().id;
	});
	if (i == end(_instances)) {
		return;
	}
	const auto hadFilter = (*i)->hasFilter();
	auto removed = std::move(*i);
	_instances.erase(i);
	for (const auto &module : ApiModules()) {
		if (module.pluginUnloaded) {
			module.pluginUnloaded(id);
		}
	}
	removed = nullptr;
	for (auto &state : _plugins) {
		if (state.manifest.id == id) {
			state.loaded = false;
		}
	}
	if (hadFilter) {
		invalidateFilters();
	}
}

const std::vector<PluginState> &Manager::plugins() const {
	return _plugins;
}

rpl::producer<> Manager::pluginsChanges() const {
	return _pluginsChanges.events();
}

void Manager::notifyChanged() {
	_pluginsChanges.fire({});
}

void Manager::setEnabled(const QString &id, bool enabled) {
	_enabled[id] = enabled;
	saveState();
	for (auto &state : _plugins) {
		if (state.manifest.id != id) {
			continue;
		}
		state.enabled = enabled;
		if (enabled) {
			loadPlugin(state);
		} else {
			unloadPlugin(id);
			state.error = QString();
		}
	}
	notifyChanged();
}

void Manager::enableByDefault(const QString &id) {
	if (_enabled.find(id) == end(_enabled)) {
		_enabled[id] = true;
		saveState();
	}
}

void Manager::reload(const QString &id) {
	for (auto &state : _plugins) {
		if (state.manifest.id != id) {
			continue;
		}
		auto error = QString();
		if (const auto manifest = readManifest(state.manifest.dir, &error)) {
			state.manifest = *manifest;
		}
		if (state.enabled) {
			loadPlugin(state);
		}
	}
	watchFiles();
	notifyChanged();
}

void Manager::reloadAll() {
	rescan();
}

void Manager::setDevMode(bool enabled) {
	_devMode = enabled;
	saveState();
	watchFiles();
	notifyChanged();
}

bool Manager::devMode() const {
	return _devMode;
}

std::vector<QString> Manager::loadedIds() const {
	return ranges::views::all(
		_instances
	) | ranges::views::transform([](const auto &instance) {
		return instance->manifest().id;
	}) | ranges::to_vector;
}

Instance *Manager::instance(const QString &id) const {
	const auto i = ranges::find(_instances, id, [](const auto &instance) {
		return instance->manifest().id;
	});
	return (i != end(_instances)) ? i->get() : nullptr;
}

QJsonObject Manager::settingsValues(const QString &id) const {
	auto result = QJsonObject();
	for (const auto &state : _plugins) {
		if (state.manifest.id != id) {
			continue;
		}
		for (const auto &entry : state.manifest.settings) {
			const auto object = entry.toObject();
			const auto key = object.value(u"key"_q).toString();
			if (!key.isEmpty()) {
				result.insert(key, object.value(u"default"_q));
			}
		}
	}
	const auto i = _settings.find(id);
	if (i != end(_settings)) {
		for (auto j = i->second.begin(); j != i->second.end(); ++j) {
			result.insert(j.key(), j.value());
		}
	}
	return result;
}

void Manager::setSettingValue(
		const QString &id,
		const QString &key,
		const QJsonValue &value) {
	_settings[id].insert(key, value);
	saveState();
	if (const auto plugin = instance(id)) {
		plugin->dispatch(u"event"_q, QVariant(Event(
			u"settings.changed"_q,
			QVariantMap{
				{ u"key"_q, key },
				{ u"value"_q, value.toVariant() },
			})));
	}
}

void Manager::watchFiles() {
	_watcher = nullptr;
	if (!_devMode) {
		return;
	}
	_watcher = std::make_unique<QFileSystemWatcher>();
	_watcher->addPath(pluginsPath());
	for (const auto &state : _plugins) {
		const auto dir = QDir(state.manifest.dir);
		_watcher->addPath(dir.absolutePath());
		const auto files = dir.entryList(QDir::Files);
		for (const auto &file : files) {
			_watcher->addPath(dir.filePath(file));
		}
	}
	const auto schedule = [=](const QString &path) {
		if (_reloadScheduled) {
			return;
		}
		_reloadScheduled = true;
		const auto root = QDir(pluginsPath()).absolutePath();
		auto id = QString();
		for (const auto &state : _plugins) {
			const auto dir = QDir(state.manifest.dir).absolutePath();
			if (path.startsWith(dir)) {
				id = state.manifest.id;
			}
		}
		if (QDir(path).absolutePath() == root) {
			id = QString();
		}
		QTimer::singleShot(kReloadDelay, [=] {
			if (!Exists()) {
				return;
			}
			auto &manager = Get();
			manager._reloadScheduled = false;
			if (id.isEmpty()) {
				LOG(("Plugins: rescanning after a change in %1").arg(path));
				manager.rescan();
			} else {
				LOG(("Plugins: hot reload of %1").arg(id));
				manager.reload(id);
				if (const auto plugin = manager.instance(id)) {
					Ui::Toast::Show(Damza::Text(
						"Plugin reloaded",
						"Плагин перезагружен") + u": "_q + plugin->manifest().name);
				}
			}
		});
	};
	QObject::connect(
		_watcher.get(),
		&QFileSystemWatcher::fileChanged,
		schedule);
	QObject::connect(
		_watcher.get(),
		&QFileSystemWatcher::directoryChanged,
		schedule);
}

void Manager::trackAccounts() {
	const auto refresh = [=] {
		auto &domain = Core::App().domain();
		auto alive = base::flat_set<not_null<Main::Account*>>();
		for (const auto &[index, account] : domain.accounts()) {
			const auto raw = account.get();
			alive.emplace(raw);
			if (_accountLifetimes.find(raw) != end(_accountLifetimes)) {
				continue;
			}
			raw->sessionValue(
			) | rpl::filter([](Main::Session *session) {
				return session != nullptr;
			}) | rpl::on_next([=](Main::Session *session) {
				trackSession(session);
			}, _accountLifetimes[raw]);
		}
		for (auto i = begin(_accountLifetimes); i != end(_accountLifetimes);) {
			if (alive.find(i->first) != end(alive)) {
				++i;
			} else {
				i = _accountLifetimes.erase(i);
			}
		}
	};
	Core::App().domain().accountsChanges(
	) | rpl::on_next(refresh, _lifetime);
	refresh();
}

void Manager::trackSession(not_null<Main::Session*> session) {
	session->data().newItemAdded(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		if (Exists() && !Get()._instances.empty()) {
			Get().emitEvent(u"message.new"_q, SerializeMessage(item));
		}
	}, session->lifetime());

	session->changes().messageUpdates(
		Data::MessageUpdate::Flag::Edited
	) | rpl::on_next([=](const Data::MessageUpdate &update) {
		if (!Exists()) {
			return;
		}
		auto &manager = Get();
		manager._filterCache.erase(update.item->fullId());
		if (!manager._instances.empty()) {
			manager.emitEvent(
				u"message.edited"_q,
				SerializeMessage(update.item));
		}
	}, session->lifetime());

	for (const auto &module : ApiModules()) {
		if (module.sessionStarted) {
			module.sessionStarted(session);
		}
	}
	emitEvent(u"session.ready"_q, SerializePeer(session->user()));
}

void Manager::emitEvent(const QString &event, const QVariant &payload) {
	if (_instances.empty()) {
		return;
	}
	const auto data = QVariant(Event(event, payload));
	// Handlers may reload plugins, so iterate over a snapshot of ids.
	for (const auto &id : loadedIds()) {
		if (const auto plugin = instance(id)) {
			plugin->dispatch(u"event"_q, data);
		}
	}
}

void Manager::invalidateFilters() {
	auto refresh = std::vector<FullMsgId>();
	refresh.reserve(_filterCache.size());
	for (const auto &[id, hidden] : _filterCache) {
		refresh.push_back(id);
	}
	_filterCache.clear();

	const auto session = activeSession();
	if (!session) {
		return;
	}
	auto &data = session->data();
	for (const auto &id : refresh) {
		if (const auto item = data.message(id)) {
			data.requestItemResize(item);
		}
	}
	if (const auto controller = activeController()) {
		if (const auto history = controller->activeChatCurrent().history()) {
			for (const auto &block : history->blocks) {
				for (const auto &view : block->messages) {
					data.requestItemResize(view->data());
				}
			}
		}
	}
}

void Manager::reportError(const QString &id, const QString &error) {
	for (auto &state : _plugins) {
		if (state.manifest.id == id) {
			state.error = error.section('\n', 0, 0);
		}
	}
	notifyChanged();
	if (!_devMode) {
		return;
	}
	const auto now = crl::now();
	auto &last = LastErrorToast[id];
	if (last && now - last < kErrorToastDelay) {
		return;
	}
	last = now;
	Ui::Toast::Show(Ui::Toast::Config{
		.title = Damza::Text("Plugin error", "Ошибка плагина")
			+ u": "_q + id,
		.text = { error.section('\n', 0, 0) },
	});
}

QNetworkAccessManager *Manager::network() {
	if (!_network) {
		_network = std::make_unique<QNetworkAccessManager>();
	}
	return _network.get();
}

Window::Controller *Manager::activeWindow() const {
	if (const auto window = Core::App().activePrimaryWindow()) {
		return window;
	}
	return Core::App().activeWindow();
}

Window::SessionController *Manager::activeController() const {
	const auto window = activeWindow();
	return window ? window->sessionController() : nullptr;
}

Main::Session *Manager::activeSession() const {
	auto &domain = Core::App().domain();
	if (!domain.started()) {
		return nullptr;
	}
	auto &account = domain.active();
	return account.sessionExists() ? &account.session() : nullptr;
}

rpl::lifetime &Manager::lifetime() {
	return _lifetime;
}

bool Manager::sendingFromPlugin() const {
	return _sendingFromPlugin;
}

void Manager::setSendingFromPlugin(bool value) {
	_sendingFromPlugin = value;
}

bool Manager::filtered(not_null<HistoryItem*> item) {
	const auto id = item->fullId();
	if (const auto i = _filterCache.find(id); i != end(_filterCache)) {
		return i->second;
	}
	if (!IsMainThread()) {
		return false;
	}
	const auto any = ranges::any_of(_instances, [](const auto &instance) {
		return instance->hasFilter();
	});
	if (!any) {
		return false;
	}
	const auto payload = QVariant(SerializeMessage(item));
	auto hidden = false;
	for (const auto &instance : _instances) {
		if (instance->hasFilter()
			&& instance->dispatch(u"filter"_q, payload).toBool()) {
			hidden = true;
			break;
		}
	}
	_filterCache.emplace(id, hidden);
	return hidden;
}

bool Manager::beforeSend(Api::MessageToSend &message) {
	if (_sendingFromPlugin) {
		return true;
	}
	const auto any = ranges::any_of(_instances, [](const auto &instance) {
		return instance->hasSendHook();
	});
	if (!any) {
		return true;
	}
	const auto peer = message.action.history->peer;
	const auto original = message.textWithTags.text;
	auto text = original;
	const auto ids = ranges::views::all(
		_instances
	) | ranges::views::filter([](const auto &instance) {
		return instance->hasSendHook();
	}) | ranges::views::transform([](const auto &instance) {
		return instance->manifest().id;
	}) | ranges::to_vector;
	for (const auto &id : ids) {
		const auto plugin = instance(id);
		if (!plugin) {
			continue;
		}
		const auto result = plugin->dispatch(u"beforeSend"_q, QVariant(
			QVariantMap{
				{ u"text"_q, text },
				{ u"peerId"_q, PeerIdToString(peer->id) },
				{ u"chat"_q, SerializePeer(peer) },
				{ u"replyTo"_q, double(
					message.action.replyTo.messageId.msg.bare) },
			}));
		const auto map = result.toMap();
		if (map.value(u"cancel"_q).toBool()) {
			return false;
		}
		const auto changed = map.value(u"text"_q);
		if (changed.typeId() == QMetaType::QString) {
			text = changed.toString();
		}
	}
	if (text != original) {
		if (text.trimmed().isEmpty()) {
			return false;
		}
		message.textWithTags = TextWithTags{ text };
	}
	return true;
}

void Manager::messageDeleted(not_null<HistoryItem*> item) {
	if (!_instances.empty()) {
		emitEvent(u"message.deleted"_q, SerializeMessage(item));
	}
}

void Manager::fillMessageMenu(
		not_null<Ui::PopupMenu*> menu,
		HistoryItem *item) {
	if (!item || _instances.empty()) {
		return;
	}
	const auto target = QVariant(SerializeMessage(item));
	const auto payload = QVariant(QVariantMap{
		{ u"kind"_q, u"message"_q },
		{ u"target"_q, target },
	});
	for (const auto &instance : _instances) {
		const auto list = instance->dispatch(u"menu:collect"_q, payload).toList();
		const auto pluginId = instance->manifest().id;
		for (const auto &value : list) {
			const auto entry = value.toMap();
			const auto entryId = entry.value(u"id"_q).toString();
			menu->addAction(entry.value(u"title"_q).toString(), [=] {
				if (!Exists()) {
					return;
				}
				if (const auto plugin = Get().instance(pluginId)) {
					plugin->dispatch(u"menu:click"_q, QVariant(QVariantMap{
						{ u"kind"_q, u"message"_q },
						{ u"id"_q, entryId },
						{ u"target"_q, target },
					}));
				}
			}, &st::menuIconBotCommands);
		}
	}
}

void Manager::fillPeerMenu(
		PeerData *peer,
		const Ui::Menu::MenuCallback &add) {
	if (!peer || _instances.empty()) {
		return;
	}
	const auto target = QVariant(SerializePeer(peer));
	const auto payload = QVariant(QVariantMap{
		{ u"kind"_q, u"chat"_q },
		{ u"target"_q, target },
	});
	for (const auto &instance : _instances) {
		const auto list = instance->dispatch(u"menu:collect"_q, payload).toList();
		const auto pluginId = instance->manifest().id;
		for (const auto &value : list) {
			const auto entry = value.toMap();
			const auto entryId = entry.value(u"id"_q).toString();
			add(entry.value(u"title"_q).toString(), [=] {
				if (!Exists()) {
					return;
				}
				if (const auto plugin = Get().instance(pluginId)) {
					plugin->dispatch(u"menu:click"_q, QVariant(QVariantMap{
						{ u"kind"_q, u"chat"_q },
						{ u"id"_q, entryId },
						{ u"target"_q, target },
					}));
				}
			}, &st::menuIconBotCommands);
		}
	}
}

// Free entry points.

void Start() {
	if (Global) {
		return;
	}
	Global = std::unique_ptr<Manager>(new Manager());
	QObject::connect(
		QCoreApplication::instance(),
		&QCoreApplication::aboutToQuit,
		[] { Shutdown(); });
	Global->start();
}

void Shutdown() {
	if (Global) {
		Global->shutdown();
		Global = nullptr;
	}
}

bool Filtered(not_null<HistoryItem*> item) {
	return Global && Global->filtered(item);
}

bool BeforeSend(Api::MessageToSend &message) {
	return !Global || Global->beforeSend(message);
}

void MessageDeleted(not_null<HistoryItem*> item) {
	if (Global) {
		Global->messageDeleted(item);
	}
}

void FillMessageMenu(not_null<Ui::PopupMenu*> menu, HistoryItem *item) {
	if (Global) {
		Global->fillMessageMenu(menu, item);
	}
}

void FillPeerMenu(PeerData *peer, const Ui::Menu::MenuCallback &addAction) {
	if (Global) {
		Global->fillPeerMenu(peer, addAction);
	}
}

} // namespace Damza::Plugins
