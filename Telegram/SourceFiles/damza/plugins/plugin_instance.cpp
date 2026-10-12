// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/plugins/plugin_instance.h"

#include "damza/plugins/plugin_api.h"
#include "damza/plugins/plugin_bridge.h"
#include "damza/plugins/plugin_js.h"
#include "damza/plugins/plugin_prelude.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>

namespace Damza::Plugins {
namespace {

constexpr auto kMaxLogLines = 500;
constexpr auto kLoadTimeLimit = crl::time(10000);
constexpr auto kCallTimeLimit = crl::time(3000);

} // namespace

Instance::Instance(Manifest manifest)
: _manifest(std::move(manifest)) {
	auto file = QFile(storagePath());
	if (file.open(QIODevice::ReadOnly)) {
		_storage = QJsonDocument::fromJson(file.readAll()).object();
	}
}

Instance::~Instance() {
	if (_loaded) {
		_unloading = true;
		dispatch(u"unload"_q, QVariant());
	}
	// Deleting widgets below fires their events: no JS after "unload".
	_closed = true;
	// Bridge owns widgets and timers that reference the runtime.
	_bridge = nullptr;
	_runtime = nullptr;
	saveStorage();
}

bool Instance::load() {
	_bridge = std::make_unique<Bridge>(this);
	_runtime = std::make_unique<Js::Runtime>(
		_manifest.dir,
		[=](const QString &level, const QString &text) { log(level, text); });
	_runtime->setTimeLimit(kLoadTimeLimit);

	auto natives = _bridge->methods();
	for (const auto &module : ApiModules()) {
		if (module.natives) {
			module.natives(_bridge.get(), natives);
		}
	}
	_runtime->installObject("__native", std::move(natives));

	auto error = QString();
	if (!_runtime->evalScript(Prelude(), u"dzg:prelude.js"_q, &error)) {
		_error = error;
		log(u"error"_q, _error);
		return false;
	}
	for (const auto &module : ApiModules()) {
		if (!module.prelude) {
			continue;
		}
		const auto code = u"(function (native, dzg, internal) {\n"_q
			+ module.prelude()
			+ u"\n})(globalThis.__native, globalThis.dzg, globalThis.__dzgInternal);"_q;
		const auto name = u"dzg:"_q + QString::fromLatin1(module.name) + u".js"_q;
		if (!_runtime->evalScript(code, name, &error)) {
			_error = error;
			log(u"error"_q, _error);
			return false;
		}
	}
	_runtime->deleteGlobal("__native");
	_runtime->deleteGlobal("__dzgInternal");
	if (!_runtime->bindGlobalFunction("__dzgDispatch")) {
		_error = u"Prelude did not define a dispatcher."_q;
		log(u"error"_q, _error);
		return false;
	}

	const auto path = QDir(_manifest.dir).filePath(_manifest.main);
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		_error = u"Can't open %1"_q.arg(path);
		log(u"error"_q, _error);
		return false;
	}
	const auto code = QString::fromUtf8(file.readAll());
	const auto ok = (_manifest.type == u"script"_q)
		? _runtime->evalScript(code, path, &error)
		: _runtime->evalModule(code, path, &error);
	_runtime->setTimeLimit(kCallTimeLimit);
	if (!ok) {
		_error = error;
		log(u"error"_q, _error);
		return false;
	}
	_loaded = true;
	return true;
}

const QString &Instance::error() const {
	return _error;
}

const Manifest &Instance::manifest() const {
	return _manifest;
}

not_null<Bridge*> Instance::bridge() const {
	return _bridge.get();
}

QString Instance::evaluate(const QString &code, QString *error) {
	if (!_runtime || _closed) {
		*error = u"Plugin is not loaded."_q;
		return QString();
	}
	return _runtime->evaluateToString(code, error);
}

bool Instance::busy() const {
	return _runtime && _runtime->busy();
}

QVariant Instance::dispatch(const QString &type, const QVariant &payload) {
	if (!_runtime || _closed) {
		return QVariant();
	}
	auto error = QString();
	const auto result = _runtime->callBound({ type, payload }, &error);
	if (!error.isEmpty()) {
		log(u"error"_q, error);
	}
	return result;
}

void Instance::setListens(const QString &event, bool listens) {
	if (listens) {
		_listens.emplace(event);
	} else {
		_listens.remove(event);
	}
}

bool Instance::listens(const QString &event) const {
	return _listens.contains(event);
}

void Instance::setHasFilter(bool value) {
	_hasFilter = value;
}

bool Instance::hasFilter() const {
	return _hasFilter;
}

void Instance::setHasSendHook(bool value) {
	_hasSendHook = value;
}

bool Instance::hasSendHook() const {
	return _hasSendHook;
}

QJsonObject &Instance::storage() {
	return _storage;
}

QString Instance::storagePath() const {
	return Manager::Get().dataPath() + _manifest.id + u".json"_q;
}

void Instance::saveStorage() {
	QDir().mkpath(Manager::Get().dataPath());
	auto file = QFile(storagePath());
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(QJsonDocument(_storage).toJson(QJsonDocument::Indented));
	}
}

void Instance::log(const QString &level, const QString &text) {
	constexpr auto kMaxLine = 16 * 1024;
	auto line = u"[%1] %2"_q.arg(level, text);
	if (line.size() > kMaxLine) {
		line = line.left(kMaxLine) + u"… (%1 chars cut)"_q.arg(line.size() - kMaxLine);
	}
	_log.push_back(line);
	while (_log.size() > kMaxLogLines) {
		_log.pop_front();
	}
	LOG(("Plugin %1: %2").arg(_manifest.id, line));
	if (level == u"error"_q && !_unloading) {
		Manager::Get().reportError(_manifest.id, text);
	}
}

const QStringList &Instance::logLines() const {
	return _log;
}

} // namespace Damza::Plugins
