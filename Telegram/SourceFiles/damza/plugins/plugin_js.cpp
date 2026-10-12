// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/plugins/plugin_js.h"

#include <quickjs.h>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace Damza::Plugins::Js {
namespace {

constexpr auto kMemoryLimit = size_t(256) * 1024 * 1024;
constexpr auto kStackLimit = size_t(1024) * 1024;
constexpr auto kMaxDepth = 32;

[[nodiscard]] Runtime *FromContext(JSContext *ctx) {
	return static_cast<Runtime*>(JS_GetContextOpaque(ctx));
}

[[nodiscard]] QString ToQString(JSContext *ctx, JSValueConst value) {
	auto length = size_t();
	const auto data = JS_ToCStringLen(ctx, &length, value);
	if (!data) {
		JS_FreeValue(ctx, JS_GetException(ctx));
		return QString();
	}
	const auto result = QString::fromUtf8(data, qsizetype(length));
	JS_FreeCString(ctx, data);
	return result;
}

[[nodiscard]] JSValue FromQString(JSContext *ctx, const QString &value) {
	const auto utf8 = value.toUtf8();
	return JS_NewStringLen(ctx, utf8.constData(), size_t(utf8.size()));
}

[[nodiscard]] QVariant ToVariant(JSContext *ctx, JSValueConst value, int depth) {
	if (depth > kMaxDepth) {
		return QVariant();
	} else if (JS_IsUndefined(value) || JS_IsNull(value)) {
		return QVariant();
	} else if (JS_IsBool(value)) {
		return QVariant(JS_ToBool(ctx, value) > 0);
	} else if (JS_VALUE_GET_TAG(value) == JS_TAG_INT) {
		return QVariant(int(JS_VALUE_GET_INT(value)));
	} else if (JS_IsNumber(value)) {
		auto result = 0.;
		JS_ToFloat64(ctx, &result, value);
		return QVariant(result);
	} else if (JS_IsString(value)) {
		return QVariant(ToQString(ctx, value));
	} else if (JS_IsFunction(ctx, value) || !JS_IsObject(value)) {
		return QVariant();
	} else if (JS_IsArray(value)) {
		auto length = int64_t();
		if (JS_GetLength(ctx, value, &length) < 0) {
			JS_FreeValue(ctx, JS_GetException(ctx));
			return QVariantList();
		}
		auto result = QVariantList();
		result.reserve(int(length));
		for (auto i = int64_t(); i != length; ++i) {
			const auto item = JS_GetPropertyUint32(ctx, value, uint32_t(i));
			result.push_back(ToVariant(ctx, item, depth + 1));
			JS_FreeValue(ctx, item);
		}
		return result;
	}
	auto properties = (JSPropertyEnum*)nullptr;
	auto count = uint32_t();
	const auto flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
	if (JS_GetOwnPropertyNames(ctx, &properties, &count, value, flags) < 0) {
		JS_FreeValue(ctx, JS_GetException(ctx));
		return QVariantMap();
	}
	auto result = QVariantMap();
	for (auto i = uint32_t(); i != count; ++i) {
		const auto key = JS_AtomToCString(ctx, properties[i].atom);
		const auto item = JS_GetProperty(ctx, value, properties[i].atom);
		if (key && !JS_IsFunction(ctx, item) && !JS_IsUndefined(item)) {
			result.insert(
				QString::fromUtf8(key),
				ToVariant(ctx, item, depth + 1));
		}
		JS_FreeValue(ctx, item);
		if (key) {
			JS_FreeCString(ctx, key);
		}
	}
	JS_FreePropertyEnum(ctx, properties, count);
	return result;
}

[[nodiscard]] JSValue FromVariant(
		JSContext *ctx,
		const QVariant &value,
		int depth) {
	if (depth > kMaxDepth || !value.isValid() || value.isNull()) {
		return JS_NULL;
	}
	switch (value.typeId()) {
	case QMetaType::Bool:
		return JS_NewBool(ctx, value.toBool());
	case QMetaType::Int:
	case QMetaType::UInt:
	case QMetaType::Short:
	case QMetaType::UShort:
		return JS_NewInt32(ctx, value.toInt());
	case QMetaType::LongLong:
	case QMetaType::ULongLong:
	case QMetaType::Long:
	case QMetaType::ULong:
	case QMetaType::Double:
	case QMetaType::Float:
		return JS_NewNumber(ctx, value.toDouble());
	case QMetaType::QString:
		return FromQString(ctx, value.toString());
	case QMetaType::QByteArray:
		return FromQString(ctx, QString::fromUtf8(value.toByteArray()));
	case QMetaType::QStringList:
	case QMetaType::QVariantList: {
		const auto list = value.toList();
		const auto result = JS_NewArray(ctx);
		for (auto i = 0; i != list.size(); ++i) {
			JS_SetPropertyUint32(
				ctx,
				result,
				uint32_t(i),
				FromVariant(ctx, list[i], depth + 1));
		}
		return result;
	}
	case QMetaType::QVariantMap: {
		const auto map = value.toMap();
		const auto result = JS_NewObject(ctx);
		for (auto i = map.begin(); i != map.end(); ++i) {
			const auto key = i.key().toUtf8();
			JS_SetPropertyStr(
				ctx,
				result,
				key.constData(),
				FromVariant(ctx, i.value(), depth + 1));
		}
		return result;
	}
	}
	if (value.canConvert<QString>()) {
		return FromQString(ctx, value.toString());
	}
	return JS_NULL;
}

void FinalizeNative(void *opaque) {
	delete static_cast<Native*>(opaque);
}

JSValue CallNative(
		JSContext *ctx,
		JSValueConst thisValue,
		int argc,
		JSValueConst *argv,
		int magic,
		void *opaque) {
	auto args = QVariantList();
	args.reserve(argc);
	for (auto i = 0; i != argc; ++i) {
		args.push_back(ToVariant(ctx, argv[i], 0));
	}
	const auto &method = *static_cast<Native*>(opaque);
	return FromVariant(ctx, method(args), 0);
}

int Interrupt(JSRuntime *rt, void *opaque) {
	return static_cast<Runtime*>(opaque)->shouldInterrupt() ? 1 : 0;
}

void TrackRejection(
		JSContext *ctx,
		JSValueConst promise,
		JSValueConst reason,
		bool isHandled,
		void *opaque) {
	if (isHandled) {
		return;
	}
	auto text = ToQString(ctx, reason);
	if (JS_IsError(reason)) {
		const auto stack = JS_GetPropertyStr(ctx, reason, "stack");
		if (JS_IsString(stack)) {
			text += '\n' + ToQString(ctx, stack);
		}
		JS_FreeValue(ctx, stack);
	}
	static_cast<Runtime*>(opaque)->reportRejection(text);
}

char *NormalizeModule(
		JSContext *ctx,
		const char *baseName,
		const char *name,
		void *opaque) {
	const auto runtime = static_cast<Runtime*>(opaque);
	const auto requested = QString::fromUtf8(name);
	auto path = QString();
	if (QDir::isAbsolutePath(requested)) {
		path = requested;
	} else {
		const auto base = QFileInfo(QString::fromUtf8(baseName)).absolutePath();
		path = QDir(base.isEmpty() ? runtime->moduleRoot() : base)
			.filePath(requested);
	}
	path = QDir::cleanPath(path);
	if (QFileInfo(path).suffix().isEmpty()
		&& QFileInfo::exists(path + u".js"_q)) {
		path += u".js"_q;
	}
	const auto utf8 = path.toUtf8();
	const auto result = static_cast<char*>(js_malloc(ctx, utf8.size() + 1));
	if (result) {
		memcpy(result, utf8.constData(), utf8.size() + 1);
	}
	return result;
}

JSModuleDef *LoadModule(JSContext *ctx, const char *name, void *opaque) {
	auto file = QFile(QString::fromUtf8(name));
	if (!file.open(QIODevice::ReadOnly)) {
		JS_ThrowReferenceError(ctx, "could not load module '%s'", name);
		return nullptr;
	}
	const auto code = file.readAll();
	const auto compiled = JS_Eval(
		ctx,
		code.constData(),
		size_t(code.size()),
		name,
		JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
	if (JS_IsException(compiled)) {
		return nullptr;
	}
	const auto result = static_cast<JSModuleDef*>(
		JS_VALUE_GET_PTR(compiled));
	JS_FreeValue(ctx, compiled);
	return result;
}

} // namespace

// Guards one entry from C++ into JS: arms the watchdog for the outermost
// call and runs promise jobs once the stack unwinds back to C++.
class Runtime::Entry final {
public:
	explicit Entry(not_null<Runtime*> runtime) : _runtime(runtime) {
		if (!_runtime->_depth++) {
			_runtime->_deadline = crl::now() + _runtime->_timeLimit;
			_runtime->_interrupted = false;
		}
	}
	~Entry() {
		if (_runtime->_depth == 1) {
			_runtime->runJobs();
		}
		--_runtime->_depth;
	}

private:
	const not_null<Runtime*> _runtime;

};

Runtime::Runtime(QString moduleRoot, LogCallback log)
: _moduleRoot(std::move(moduleRoot))
, _log(std::move(log))
, _runtime(JS_NewRuntime())
, _context(JS_NewContext(_runtime)) {
	JS_SetMemoryLimit(_runtime, kMemoryLimit);
	JS_SetMaxStackSize(_runtime, kStackLimit);
	JS_SetContextOpaque(_context, this);
	JS_SetInterruptHandler(_runtime, Interrupt, this);
	JS_SetHostPromiseRejectionTracker(_runtime, TrackRejection, this);
	JS_SetModuleLoaderFunc(_runtime, NormalizeModule, LoadModule, this);
}

Runtime::~Runtime() {
	if (_bound) {
		const auto bound = static_cast<JSValue*>(_bound);
		JS_FreeValue(_context, *bound);
		delete bound;
	}
	JS_FreeContext(_context);
	JS_FreeRuntime(_runtime);
}

void Runtime::installObject(
		const char *name,
		std::vector<NativeMethod> methods) {
	const auto object = JS_NewObject(_context);
	for (auto &entry : methods) {
		const auto function = JS_NewCClosure(
			_context,
			CallNative,
			entry.name,
			FinalizeNative,
			0,
			0,
			new Native(std::move(entry.method)));
		JS_SetPropertyStr(_context, object, entry.name, function);
	}
	const auto global = JS_GetGlobalObject(_context);
	JS_SetPropertyStr(_context, global, name, object);
	JS_FreeValue(_context, global);
}

QString Runtime::exceptionText() {
	const auto exception = JS_GetException(_context);
	auto result = ToQString(_context, exception);
	if (JS_IsError(exception)) {
		const auto stack = JS_GetPropertyStr(_context, exception, "stack");
		if (JS_IsString(stack)) {
			const auto trace = ToQString(_context, stack).trimmed();
			if (!trace.isEmpty()) {
				result += '\n' + trace;
			}
		}
		JS_FreeValue(_context, stack);
	}
	JS_FreeValue(_context, exception);
	if (_interrupted) {
		result = u"Script took longer than %1 ms and was stopped. "_q.arg(
			_timeLimit) + result;
	}
	return result;
}

bool Runtime::evalScript(
		const QString &code,
		const QString &fileName,
		QString *error) {
	const auto entry = Entry(this);
	const auto utf8 = code.toUtf8();
	const auto name = fileName.toUtf8();
	const auto result = JS_Eval(
		_context,
		utf8.constData(),
		size_t(utf8.size()),
		name.constData(),
		JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(result)) {
		*error = exceptionText();
		return false;
	}
	JS_FreeValue(_context, result);
	return true;
}

bool Runtime::evalModule(
		const QString &code,
		const QString &fileName,
		QString *error) {
	auto promise = JS_UNDEFINED;
	{
		const auto entry = Entry(this);
		const auto utf8 = code.toUtf8();
		const auto name = fileName.toUtf8();
		promise = JS_Eval(
			_context,
			utf8.constData(),
			size_t(utf8.size()),
			name.constData(),
			JS_EVAL_TYPE_MODULE);
		if (JS_IsException(promise)) {
			*error = exceptionText();
			return false;
		}
	}
	// Jobs ran in ~Entry, so a module without pending top-level awaits
	// is settled by now.
	auto ok = true;
	if (JS_PromiseState(_context, promise) == JS_PROMISE_REJECTED) {
		const auto reason = JS_PromiseResult(_context, promise);
		JS_Throw(_context, reason);
		*error = exceptionText();
		ok = false;
	}
	JS_FreeValue(_context, promise);
	return ok;
}

QString Runtime::evaluateToString(const QString &code, QString *error) {
	const auto entry = Entry(this);
	const auto utf8 = code.toUtf8();
	const auto result = JS_Eval(
		_context,
		utf8.constData(),
		size_t(utf8.size()),
		"<console>",
		JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(result)) {
		*error = exceptionText();
		return QString();
	}
	auto text = QString();
	if (JS_IsUndefined(result)) {
		text = u"undefined"_q;
	} else if (JS_IsFunction(_context, result) || JS_IsError(result)) {
		text = ToQString(_context, result);
	} else {
		const auto json = JS_JSONStringify(_context, result, JS_UNDEFINED, JS_NewInt32(_context, 2));
		if (JS_IsException(json) || JS_IsUndefined(json)) {
			JS_FreeValue(_context, JS_GetException(_context));
			text = ToQString(_context, result);
		} else {
			text = ToQString(_context, json);
		}
		JS_FreeValue(_context, json);
	}
	JS_FreeValue(_context, result);
	return text;
}

bool Runtime::bindGlobalFunction(const char *name) {
	const auto global = JS_GetGlobalObject(_context);
	const auto function = JS_GetPropertyStr(_context, global, name);
	JS_FreeValue(_context, global);
	if (!JS_IsFunction(_context, function)) {
		JS_FreeValue(_context, function);
		return false;
	}
	if (_bound) {
		JS_FreeValue(_context, *static_cast<JSValue*>(_bound));
		*static_cast<JSValue*>(_bound) = function;
	} else {
		_bound = new JSValue(function);
	}
	return true;
}

QVariant Runtime::callBound(const QVariantList &args, QString *error) {
	if (!_bound) {
		return QVariant();
	}
	const auto entry = Entry(this);
	auto argv = std::vector<JSValue>();
	argv.reserve(args.size());
	for (const auto &arg : args) {
		argv.push_back(FromVariant(_context, arg, 0));
	}
	const auto result = JS_Call(
		_context,
		*static_cast<JSValue*>(_bound),
		JS_UNDEFINED,
		int(argv.size()),
		argv.data());
	for (const auto &value : argv) {
		JS_FreeValue(_context, value);
	}
	if (JS_IsException(result)) {
		*error = exceptionText();
		return QVariant();
	}
	auto converted = ToVariant(_context, result, 0);
	JS_FreeValue(_context, result);
	return converted;
}

void Runtime::deleteGlobal(const char *name) {
	const auto global = JS_GetGlobalObject(_context);
	const auto atom = JS_NewAtom(_context, name);
	JS_DeleteProperty(_context, global, atom, 0);
	JS_FreeAtom(_context, atom);
	JS_FreeValue(_context, global);
}

void Runtime::setTimeLimit(crl::time ms) {
	_timeLimit = ms;
}

bool Runtime::busy() const {
	return _depth > 0;
}

bool Runtime::shouldInterrupt() const {
	if (_depth > 0 && _deadline && crl::now() > _deadline) {
		const_cast<Runtime*>(this)->_interrupted = true;
		return true;
	}
	return false;
}

void Runtime::reportRejection(const QString &reason) {
	_log(u"error"_q, u"Unhandled promise rejection: "_q + reason);
}

const QString &Runtime::moduleRoot() const {
	return _moduleRoot;
}

void Runtime::runJobs() {
	auto context = (JSContext*)nullptr;
	while (true) {
		const auto result = JS_ExecutePendingJob(_runtime, &context);
		if (result == 0) {
			break;
		} else if (result < 0) {
			_log(u"error"_q, exceptionText());
		}
	}
}

} // namespace Damza::Plugins::Js
