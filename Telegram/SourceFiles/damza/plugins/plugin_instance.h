// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

#include "damza/plugins/plugin_manager.h"

namespace Damza::Plugins {

namespace Js {
class Runtime;
} // namespace Js

class Bridge;

// One loaded plugin: its own QuickJS runtime, globals and storage.
// Everything runs on the main thread.
class Instance final {
public:
	explicit Instance(Manifest manifest);
	~Instance();

	[[nodiscard]] bool load();
	[[nodiscard]] const QString &error() const;
	[[nodiscard]] const Manifest &manifest() const;
	[[nodiscard]] not_null<Bridge*> bridge() const;

	// Calls the prelude dispatcher. Exceptions are caught and logged.
	QVariant dispatch(const QString &type, const QVariant &payload);

	// Runs code in the plugin's global scope (dev console). Returns the
	// result converted to text, or the exception text in *error.
	[[nodiscard]] QString evaluate(const QString &code, QString *error);

	// True while JS of this plugin is on the stack.
	[[nodiscard]] bool busy() const;

	void setHasFilter(bool value);
	[[nodiscard]] bool hasFilter() const;
	void setHasSendHook(bool value);
	[[nodiscard]] bool hasSendHook() const;

	[[nodiscard]] QJsonObject &storage();
	void saveStorage();

	void log(const QString &level, const QString &text);
	[[nodiscard]] const QStringList &logLines() const;

private:
	[[nodiscard]] QString storagePath() const;

	Manifest _manifest;
	std::unique_ptr<Bridge> _bridge;
	std::unique_ptr<Js::Runtime> _runtime;
	QString _error;
	QJsonObject _storage;
	QStringList _log;
	bool _hasFilter = false;
	bool _hasSendHook = false;
	bool _unloading = false;
	bool _loaded = false;
	bool _closed = false;

};

} // namespace Damza::Plugins
