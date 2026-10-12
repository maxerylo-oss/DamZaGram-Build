// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

#include "base/flat_map.h"
#include "base/flat_set.h"
#include "damza/plugins/plugins.h"
#include "data/data_msg_id.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QFileSystemWatcher>

class QNetworkAccessManager;

namespace Main {
class Account;
class Session;
} // namespace Main

namespace Window {
class Controller;
class SessionController;
} // namespace Window

namespace Damza::Plugins {

class Instance;

struct Manifest {
	QString id;
	QString name;
	QString version;
	QString author;
	QString description;
	QString main = u"main.js"_q;
	// "module" (default): ES module, import/export, top-level await.
	// "script": classic global script.
	QString type = u"module"_q;
	// Shipped with the client (plugins/builtin), see api_builtin.cpp.
	bool builtin = false;
	QJsonArray settings;
	QString dir;
};

struct PluginState {
	Manifest manifest;
	bool enabled = false;
	bool loaded = false;
	QString error;
};

class Manager final {
public:
	Manager();
	~Manager();

	[[nodiscard]] static Manager &Get();
	[[nodiscard]] static bool Exists();

	void start();
	void shutdown();

	[[nodiscard]] QString pluginsPath() const;
	[[nodiscard]] QString dataPath() const;

	// Restarts every plugin.
	void rescan();
	// Picks up added / removed plugin folders, keeps the rest running.
	void refresh();
	[[nodiscard]] const std::vector<PluginState> &plugins() const;
	[[nodiscard]] rpl::producer<> pluginsChanges() const;

	void setEnabled(const QString &id, bool enabled);
	// Marks a plugin enabled unless the user already chose a state for it.
	// Call before plugins are scanned (from ApiModule::appStarted).
	void enableByDefault(const QString &id);
	void reload(const QString &id);
	void reloadAll();
	void setDevMode(bool enabled);
	[[nodiscard]] bool devMode() const;

	[[nodiscard]] Instance *instance(const QString &id) const;
	// Snapshot of loaded plugin ids, safe to iterate while dispatching.
	[[nodiscard]] std::vector<QString> loadedIds() const;
	[[nodiscard]] QJsonObject settingsValues(const QString &id) const;
	void setSettingValue(
		const QString &id,
		const QString &key,
		const QJsonValue &value);

	// Hooks.
	[[nodiscard]] bool filtered(not_null<HistoryItem*> item);
	[[nodiscard]] bool beforeSend(Api::MessageToSend &message);
	void messageDeleted(not_null<HistoryItem*> item);
	void fillMessageMenu(not_null<Ui::PopupMenu*> menu, HistoryItem *item);
	void fillPeerMenu(PeerData *peer, const Ui::Menu::MenuCallback &add);

	// Called by instances.
	void emitEvent(const QString &event, const QVariant &payload);
	void invalidateFilters();
	void reportError(const QString &id, const QString &error);
	[[nodiscard]] QNetworkAccessManager *network();
	[[nodiscard]] Main::Session *activeSession() const;
	[[nodiscard]] Window::SessionController *activeController() const;
	[[nodiscard]] Window::Controller *activeWindow() const;

	// Destroyed with the manager on quit: use it for module subscriptions.
	[[nodiscard]] rpl::lifetime &lifetime();

	// Set while plugin code itself is sending, so hooks are not re-entered.
	[[nodiscard]] bool sendingFromPlugin() const;
	void setSendingFromPlugin(bool value);

private:
	[[nodiscard]] std::vector<PluginState> scanDirectory() const;
	void scheduleReload(const QString &path);
	void loadState();
	void saveState() const;
	[[nodiscard]] std::optional<Manifest> readManifest(
		const QString &dir,
		QString *error) const;
	void loadPlugin(PluginState &state);
	void unloadPlugin(const QString &id);
	void trackAccounts();
	void trackSession(not_null<Main::Session*> session);
	void watchFiles();
	void notifyChanged();

	std::vector<PluginState> _plugins;
	std::vector<std::unique_ptr<Instance>> _instances;
	base::flat_map<QString, bool> _enabled;
	base::flat_map<QString, QJsonObject> _settings;
	std::map<FullMsgId, bool> _filterCache;
	std::map<not_null<Main::Account*>, rpl::lifetime> _accountLifetimes;
	std::unique_ptr<QNetworkAccessManager> _network;
	std::unique_ptr<QFileSystemWatcher> _watcher;
	rpl::event_stream<> _pluginsChanges;
	bool _started = false;
	bool _devMode = true;
	bool _sendingFromPlugin = false;
	bool _reloadScheduled = false;
	bool _rescanScheduled = false;
	base::flat_set<QString> _reloadIds;
	rpl::lifetime _lifetime;

};

} // namespace Damza::Plugins
