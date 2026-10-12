// Minimal Manager for install_test.sh: a plugins folder in a temp dir.
#pragma once
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

class QNetworkAccessManager;

namespace Damza::Plugins {

class Instance;

struct Manifest {
	QString id;
	QString name;
	QString version;
	QString author;
	QString description;
	QString main = u"main.js"_q;
	QString type = u"module"_q;
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
	static Manager &Get() { static Manager m; return m; }
	static bool Exists() { return true; }
	QString root;
	std::vector<PluginState> list;
	std::map<QString, bool> enabled;
	int rescans = 0;
	int reloads = 0;

	QString pluginsPath() const { return root + u"/plugins/"_q; }
	QString dataPath() const { return root + u"/data/"_q; }
	const std::vector<PluginState> &plugins() const { return list; }
	QNetworkAccessManager *network() { return nullptr; }
	Instance *instance(const QString &) const { return nullptr; }
	void rescan() {
		++rescans;
		list.clear();
		const auto dir = QDir(pluginsPath());
		for (const auto &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
			auto file = QFile(dir.filePath(name) + u"/manifest.json"_q);
			if (!file.open(QIODevice::ReadOnly)) {
				continue;
			}
			const auto object = QJsonDocument::fromJson(file.readAll()).object();
			auto state = PluginState();
			state.manifest.id = object.value(u"id"_q).toString(name);
			state.manifest.name = object.value(u"name"_q).toString();
			state.manifest.version = object.value(u"version"_q).toString();
			state.manifest.dir = dir.filePath(name);
			state.enabled = enabled[state.manifest.id];
			list.push_back(state);
		}
	}
	void refresh() { rescan(); }
	void forgetPlugin(const QString &) {}
	void reload(const QString &) { ++reloads; rescan(); }
	void setEnabled(const QString &id, bool value) {
		enabled[id] = value;
		for (auto &state : list) {
			if (state.manifest.id == id) {
				state.enabled = value;
			}
		}
	}
};

} // namespace Damza::Plugins
