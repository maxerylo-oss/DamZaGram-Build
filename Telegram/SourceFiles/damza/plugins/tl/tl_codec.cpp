// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/plugins/tl/tl_codec.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QRandomGenerator>
#include <QtCore/QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Damza::Tl {
namespace {

// Real TL data nests a few dozen levels at most (Instant View pages). An
// Encoder level takes ~3 KB of stack, and the main thread has only 1 MB on
// Windows, shared with the JS engine that called us.
constexpr auto kMaxDepth = 100;
constexpr auto kMaxSafeInteger = 9007199254740991.;
constexpr auto kMaxBytesLength = 0xFFFFFF;
// MTProto never nests gzip_packed, the limits stop gzip "quines" and bombs
// in dzg.api.decode() input from recursing or allocating without bound.
constexpr auto kMaxGzipNesting = 2;
constexpr auto kMaxUnpackedTotal = qint64(64 * 1024 * 1024);

[[nodiscard]] QString JoinPath(const QString &path, const QString &key) {
	return path.isEmpty() ? key : (path + QLatin1Char('.') + key);
}

[[nodiscard]] QString IndexPath(const QString &path, int index) {
	return path + QLatin1Char('[') + QString::number(index) + QLatin1Char(']');
}

[[nodiscard]] QString Describe(const QJsonValue &value) {
	switch (value.type()) {
	case QJsonValue::Null: return QStringLiteral("null");
	case QJsonValue::Bool: return QStringLiteral("a boolean");
	case QJsonValue::Double: return QStringLiteral("a number");
	case QJsonValue::String: return QStringLiteral("a string");
	case QJsonValue::Array: return QStringLiteral("an array");
	case QJsonValue::Object: {
		const auto name = value.toObject().value(QLatin1String("_"));
		return name.isString()
			? (QStringLiteral("{_: '") + name.toString() + QStringLiteral("'}"))
			: QStringLiteral("an object without '_'");
	}
	default: return QStringLiteral("undefined");
	}
}

[[nodiscard]] QString SnakeCase(const QString &name) {
	auto result = QString();
	for (const auto ch : name) {
		if (ch.isUpper()) {
			result += QLatin1Char('_');
			result += ch.toLower();
		} else {
			result += ch;
		}
	}
	return result;
}

[[nodiscard]] bool Present(const QJsonValue &value, const TypeRef &type) {
	if (value.isUndefined() || value.isNull()) {
		return false;
	} else if (type.kind == TypeKind::True) {
		return value.isBool()
			? value.toBool()
			: value.isDouble()
			? (value.toDouble() != 0.)
			: true;
	}
	return true;
}

class Encoder final {
public:
	Encoder(const Schema &schema, const EncodeOptions &options)
	: _schema(schema)
	, _options(options) {
	}

	bool value(const QJsonValue &value, const TypeRef &type, const QString &path);
	bool fields(
		const Combinator &combinator,
		const QJsonObject &object,
		const QString &path);

	[[nodiscard]] Buffer take() {
		auto result = Buffer(int(_bytes.size() / 4));
		if (!result.isEmpty()) {
			std::memcpy(result.data(), _bytes.constData(), _bytes.size());
		}
		return result;
	}

	void putId(quint32 id) {
		putInt(qint32(id));
	}

	QString error;
	// Result type of the innermost encoded !X query, for {X:Type} methods.
	TypeRef queryResult;

private:
	bool fail(const QString &path, const QString &text) {
		if (error.isEmpty()) {
			error = path.isEmpty() ? text : (path + QStringLiteral(": ") + text);
		}
		return false;
	}

	void putInt(qint32 value) {
		const auto le = qToLittleEndian(value);
		_bytes.append(reinterpret_cast<const char*>(&le), 4);
	}
	void putLong(qint64 value) {
		const auto le = qToLittleEndian(value);
		_bytes.append(reinterpret_cast<const char*>(&le), 8);
	}
	void putDouble(double value) {
		auto bits = quint64();
		std::memcpy(&bits, &value, 8);
		putLong(qint64(bits));
	}
	bool putBytes(const QByteArray &data, const QString &path) {
		const auto size = int(data.size());
		if (size > kMaxBytesLength) {
			return fail(path, QStringLiteral("%1 bytes is too long for TL").arg(size));
		} else if (size < 254) {
			_bytes.append(char(size));
			_bytes.append(data);
			pad(size + 1);
		} else {
			_bytes.append(char(254));
			_bytes.append(char(size & 0xFF));
			_bytes.append(char((size >> 8) & 0xFF));
			_bytes.append(char((size >> 16) & 0xFF));
			_bytes.append(data);
			pad(size + 4);
		}
		return true;
	}
	void pad(int written) {
		while (written % 4) {
			_bytes.append(char(0));
			++written;
		}
	}

	bool toInt(const QJsonValue &value, qint32 &out, const QString &path);
	bool toLong(const QJsonValue &value, qint64 &out, const QString &path);
	bool toBytes(const QJsonValue &value, QByteArray &out, const QString &path);
	bool boxed(const QJsonValue &value, const TypeRef &type, const QString &path);
	bool bare(const QJsonValue &value, const TypeRef &type, const QString &path);
	bool query(const QJsonValue &value, const QString &path);
	bool fillDefault(const Param &param, const QJsonObject &object);

	const Schema &_schema;
	const EncodeOptions &_options;
	QByteArray _bytes;
	int _depth = 0;

};

bool Encoder::toInt(const QJsonValue &value, qint32 &out, const QString &path) {
	auto number = 0.;
	if (value.isDouble()) {
		number = value.toDouble();
	} else if (value.isString()) {
		auto ok = false;
		number = double(value.toString().trimmed().toLongLong(&ok));
		if (!ok) {
			return fail(path, QStringLiteral("int expected, got '%1'").arg(value.toString()));
		}
	} else if (value.isBool()) {
		number = value.toBool() ? 1. : 0.;
	} else {
		return fail(path, QStringLiteral("int expected, got ") + Describe(value));
	}
	if (std::floor(number) != number
		|| number < -2147483648.
		|| number > 4294967295.) {
		return fail(path, QStringLiteral("%1 is not a 32-bit integer").arg(number));
	}
	out = qint32(quint32(qint64(number)));
	return true;
}

bool Encoder::toLong(const QJsonValue &value, qint64 &out, const QString &path) {
	if (value.isString()) {
		const auto text = value.toString().trimmed();
		auto ok = false;
		out = text.toLongLong(&ok);
		if (!ok) {
			const auto unsignedValue = text.toULongLong(&ok);
			out = qint64(unsignedValue);
		}
		if (!ok) {
			return fail(path, QStringLiteral("long expected as a decimal string, got '%1'").arg(text));
		}
		return true;
	} else if (value.isDouble()) {
		const auto number = value.toDouble();
		if (std::floor(number) != number || std::abs(number) > kMaxSafeInteger) {
			return fail(path, QStringLiteral("long must be a decimal string: %1 is not a safe integer").arg(number));
		}
		out = qint64(number);
		return true;
	}
	return fail(path, QStringLiteral("long expected as a decimal string, got ") + Describe(value));
}

bool Encoder::toBytes(const QJsonValue &value, QByteArray &out, const QString &path) {
	if (value.isString()) {
		const auto text = value.toString();
		const auto latin = text.toLatin1();
		if (QString::fromLatin1(latin) != text) {
			return fail(path, QStringLiteral("bytes expected as base64"));
		}
		auto decoded = QByteArray::fromBase64Encoding(
			latin,
			QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
		if (!decoded) {
			decoded = QByteArray::fromBase64Encoding(
				latin,
				QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
		}
		if (!decoded) {
			return fail(path, QStringLiteral("bytes expected as base64, got invalid base64"));
		}
		out = *decoded;
		return true;
	} else if (value.isArray()) {
		const auto list = value.toArray();
		out.clear();
		out.reserve(list.size());
		for (auto i = 0; i != list.size(); ++i) {
			const auto byte = list[i].toDouble(-1.);
			if (!list[i].isDouble() || byte < 0. || byte > 255. || std::floor(byte) != byte) {
				return fail(IndexPath(path, i), QStringLiteral("byte (0..255) expected"));
			}
			out.append(char(int(byte)));
		}
		return true;
	} else if (value.isObject()) {
		const auto text = value.toObject().value(QLatin1String("text"));
		if (text.isString()) {
			out = text.toString().toUtf8();
			return true;
		}
	}
	return fail(path, QStringLiteral("bytes expected as base64, got ") + Describe(value));
}

bool Encoder::value(
		const QJsonValue &value,
		const TypeRef &type,
		const QString &path) {
	if (_depth > kMaxDepth) {
		return fail(path, QStringLiteral("too deep"));
	}
	switch (type.kind) {
	case TypeKind::Int:
	case TypeKind::Flags: {
		auto result = qint32();
		if (!toInt(value, result, path)) {
			return false;
		}
		putInt(result);
		return true;
	}
	case TypeKind::Long: {
		auto result = qint64();
		if (!toLong(value, result, path)) {
			return false;
		}
		putLong(result);
		return true;
	}
	case TypeKind::Double: {
		auto ok = value.isDouble();
		auto result = value.toDouble();
		if (value.isString()) {
			result = value.toString().toDouble(&ok);
		}
		if (!ok) {
			return fail(path, QStringLiteral("double expected, got ") + Describe(value));
		}
		putDouble(result);
		return true;
	}
	case TypeKind::String: {
		if (value.isString()) {
			return putBytes(value.toString().toUtf8(), path);
		} else if (value.isDouble()) {
			return putBytes(
				QString::number(value.toDouble(), 'g', 17).toUtf8(),
				path);
		}
		return fail(path, QStringLiteral("string expected, got ") + Describe(value));
	}
	case TypeKind::Bytes: {
		auto result = QByteArray();
		if (!toBytes(value, result, path)) {
			return false;
		}
		return putBytes(result, path);
	}
	case TypeKind::Int128:
	case TypeKind::Int256: {
		const auto size = (type.kind == TypeKind::Int128) ? 16 : 32;
		auto result = QByteArray();
		if (!toBytes(value, result, path)) {
			return false;
		} else if (result.size() != size) {
			return fail(path, QStringLiteral("%1 bytes expected, got %2").arg(size).arg(result.size()));
		}
		_bytes.append(result);
		return true;
	}
	case TypeKind::True:
		return true;
	case TypeKind::Bool: {
		auto flag = false;
		if (value.isBool()) {
			flag = value.toBool();
		} else if (value.isDouble()) {
			flag = (value.toDouble() != 0.);
		} else if (value.isObject()) {
			const auto name = value.toObject().value(QLatin1String("_")).toString();
			if (name == QLatin1String("boolTrue")) {
				flag = true;
			} else if (name != QLatin1String("boolFalse")) {
				return fail(path, QStringLiteral("Bool expected, got ") + Describe(value));
			}
		} else {
			return fail(path, QStringLiteral("Bool expected, got ") + Describe(value));
		}
		putInt(qint32(flag ? kBoolTrueId : kBoolFalseId));
		return true;
	}
	case TypeKind::Vector:
	case TypeKind::BareVector: {
		if (!value.isArray()) {
			return fail(path, type.text() + QStringLiteral(" expected as an array, got ") + Describe(value));
		}
		const auto list = value.toArray();
		if (type.kind == TypeKind::Vector) {
			putInt(qint32(kVectorId));
		}
		putInt(qint32(list.size()));
		const auto inner = type.inner ? *type.inner : TypeRef();
		++_depth;
		for (auto i = 0; i != list.size(); ++i) {
			if (!this->value(list[i], inner, IndexPath(path, i))) {
				return false;
			}
		}
		--_depth;
		return true;
	}
	case TypeKind::Boxed:
	case TypeKind::Generic:
	case TypeKind::Object:
		return boxed(value, type, path);
	case TypeKind::Bare:
		return bare(value, type, path);
	case TypeKind::Query:
		return query(value, path);
	}
	return fail(path, QStringLiteral("unsupported type ") + type.text());
}

bool Encoder::boxed(
		const QJsonValue &value,
		const TypeRef &type,
		const QString &path) {
	const auto typed = (type.kind == TypeKind::Boxed);
	const auto named = [](const QJsonValue &value) {
		return value.isObject()
			&& value.toObject().value(QLatin1String("_")).isString();
	};
	if (!named(value) && typed && _options.resolve) {
		const auto resolved = _options.resolve(type.name, value);
		if (resolved.isString()) {
			return fail(path, resolved.toString());
		} else if (named(resolved)) {
			return boxed(resolved, type, path);
		}
	}
	const auto object = value.toObject();
	const auto hasName = named(value);
	auto combinator = (const Combinator*)nullptr;
	if (hasName) {
		const auto name = object.value(QLatin1String("_")).toString();
		combinator = _schema.constructor(name);
		if (!combinator) {
			return fail(path, _schema.method(name)
				? (QStringLiteral("'") + name + QStringLiteral("' is a method, a constructor expected"))
				: (QStringLiteral("unknown constructor '") + name + QLatin1Char('\'')));
		}
	} else if (typed && value.isObject()) {
		const auto &list = _schema.constructorsOf(type.name);
		if (list.size() == 1) {
			combinator = list.front();
		}
	}
	if (!combinator) {
		return fail(path, (typed ? type.name : QStringLiteral("TL object"))
			+ QStringLiteral(" expected like {_: '")
			+ (typed && !_schema.constructorsOf(type.name).empty()
				? _schema.constructorsOf(type.name).front()->name
				: QStringLiteral("constructorName"))
			+ QStringLiteral("', ...}, got ")
			+ Describe(value));
	}
	if (typed && combinator->typeText != type.name) {
		return fail(path, QStringLiteral("'%1' is %2, but %3 expected").arg(
			combinator->name,
			combinator->typeText,
			type.name));
	}
	putInt(qint32(combinator->id));
	++_depth;
	const auto result = fields(*combinator, object, path);
	--_depth;
	return result;
}

bool Encoder::bare(
		const QJsonValue &value,
		const TypeRef &type,
		const QString &path) {
	auto combinator = (const Combinator*)nullptr;
	if (type.name.startsWith(QLatin1Char('%'))) {
		const auto &list = _schema.constructorsOf(type.name.mid(1));
		if (list.size() == 1) {
			combinator = list.front();
		}
	} else {
		combinator = _schema.constructor(type.name);
	}
	if (!combinator) {
		return fail(path, QStringLiteral("unknown bare type ") + type.name);
	} else if (!value.isObject()) {
		return fail(path, combinator->name + QStringLiteral(" object expected, got ") + Describe(value));
	}
	++_depth;
	const auto result = fields(*combinator, value.toObject(), path);
	--_depth;
	return result;
}

bool Encoder::query(const QJsonValue &value, const QString &path) {
	const auto object = value.toObject();
	const auto name = object.value(QLatin1String("_")).toString();
	const auto method = _schema.method(name);
	if (!method) {
		return fail(path, QStringLiteral("method call expected like {_: 'help.getConfig'}, got ") + Describe(value));
	} else if (_options.check) {
		const auto refused = _options.check(*method, object);
		if (!refused.isEmpty()) {
			return fail(path, refused);
		}
	}
	putInt(qint32(method->id));
	++_depth;
	if (!fields(*method, object, path)) {
		return false;
	}
	--_depth;
	if (method->type.kind != TypeKind::Generic) {
		queryResult = method->type;
	}
	return true;
}

bool Encoder::fillDefault(const Param &param, const QJsonObject &object) {
	if (!_options.defaults) {
		return false;
	}
	auto random = QRandomGenerator::global();
	if (param.name == QLatin1String("random_id")) {
		if (param.type.kind == TypeKind::Long) {
			putLong(qint64(random->generate64()));
			return true;
		} else if (param.type.kind == TypeKind::Int) {
			putInt(qint32(random->generate()));
			return true;
		} else if (param.type.kind == TypeKind::Vector
			&& param.type.inner
			&& param.type.inner->kind == TypeKind::Long) {
			const auto ids = object.value(QLatin1String("id"));
			if (!ids.isArray()) {
				return false;
			}
			const auto count = ids.toArray().size();
			putInt(qint32(kVectorId));
			putInt(qint32(count));
			for (auto i = 0; i != count; ++i) {
				putLong(qint64(random->generate64()));
			}
			return true;
		}
	} else if (param.name == QLatin1String("hash")) {
		if (param.type.kind == TypeKind::Long) {
			putLong(0);
			return true;
		} else if (param.type.kind == TypeKind::Int) {
			putInt(0);
			return true;
		}
	}
	return false;
}

bool Encoder::fields(
		const Combinator &combinator,
		const QJsonObject &object,
		const QString &path) {
	const auto &params = combinator.params;
	for (auto i = object.begin(); i != object.end(); ++i) {
		const auto &key = i.key();
		if (key == QLatin1String("_")) {
			continue;
		}
		const auto known = std::any_of(params.begin(), params.end(), [&](
				const Param &param) {
			return param.name == key;
		});
		if (!known) {
			const auto snake = SnakeCase(key);
			const auto hint = std::any_of(params.begin(), params.end(), [&](
					const Param &param) {
				return param.name == snake;
			});
			return fail(path, QStringLiteral("unknown field '%1' in %2%3").arg(
				key,
				combinator.name,
				hint
					? (QStringLiteral(" (did you mean '") + snake + QStringLiteral("'?)"))
					: QString()));
		}
	}
	auto flags = std::vector<quint32>(params.size(), 0);
	for (auto i = 0; i != int(params.size()); ++i) {
		if (params[i].type.kind == TypeKind::Flags) {
			const auto given = object.value(params[i].name);
			const auto number = given.toDouble(-1.);
			if (given.isDouble()
				&& number >= -2147483648.
				&& number <= 4294967295.) {
				flags[i] = quint32(qint64(number));
			}
		}
	}
	for (const auto &param : params) {
		if (param.optional() && Present(object.value(param.name), param.type)) {
			flags[param.flagsIndex] |= (quint32(1) << param.bit);
		}
	}
	for (auto i = 0; i != int(params.size()); ++i) {
		const auto &param = params[i];
		const auto fieldPath = JoinPath(path, param.name);
		if (param.type.kind == TypeKind::Flags) {
			putInt(qint32(flags[i]));
			continue;
		}
		const auto field = object.value(param.name);
		if (param.optional()) {
			if (!(flags[param.flagsIndex] & (quint32(1) << param.bit))
				|| param.type.kind == TypeKind::True) {
				continue;
			} else if (!Present(field, param.type)) {
				return fail(fieldPath, QStringLiteral("required because bit %1 of '%2' is set").arg(
					QString::number(param.bit),
					params[param.flagsIndex].name));
			}
		} else if (!Present(field, param.type)) {
			if (param.type.kind == TypeKind::True) {
				continue;
			} else if (fillDefault(param, object)) {
				continue;
			}
			return fail(fieldPath, QStringLiteral("missing %1 field in %2").arg(
				param.typeText,
				combinator.name));
		}
		if (!value(field, param.type, fieldPath)) {
			return false;
		}
	}
	return true;
}

class JsonWriter final {
public:
	void raw(const char *text) {
		_out.append(text);
	}
	void raw(const QByteArray &text) {
		_out.append(text);
	}
	void string(const QString &text) {
		_out.append('"');
		const auto plain = std::none_of(text.begin(), text.end(), [](QChar ch) {
			return ch.unicode() < 0x20 || ch == QLatin1Char('"') || ch == QLatin1Char('\\');
		});
		if (plain) {
			_out.append(text.toUtf8());
			_out.append('"');
			return;
		}
		auto chunk = QString();
		for (const auto ch : text) {
			const auto code = ch.unicode();
			if (code == '"') {
				chunk += QLatin1String("\\\"");
			} else if (code == '\\') {
				chunk += QLatin1String("\\\\");
			} else if (code == '\n') {
				chunk += QLatin1String("\\n");
			} else if (code == '\r') {
				chunk += QLatin1String("\\r");
			} else if (code == '\t') {
				chunk += QLatin1String("\\t");
			} else if (code < 0x20) {
				chunk += QStringLiteral("\\u%1").arg(int(code), 4, 16, QLatin1Char('0'));
			} else {
				chunk += ch;
			}
		}
		_out.append(chunk.toUtf8());
		_out.append('"');
	}
	void key(const QString &name) {
		string(name);
		_out.append(':');
	}
	void comma() {
		_out.append(',');
	}
	[[nodiscard]] QByteArray take() {
		return std::move(_out);
	}

private:
	QByteArray _out;

};

class Decoder final {
public:
	Decoder(
		const Schema &schema,
		const DecodeOptions &options,
		const Prime *from,
		const Prime *till,
		JsonWriter &out,
		qint64 &unpacked,
		int gzipNesting = 0)
	: _schema(schema)
	, _options(options)
	, _from(from)
	, _position(from)
	, _till(till)
	, _out(out)
	, _unpacked(unpacked)
	, _gzipNesting(gzipNesting) {
	}

	bool value(const TypeRef &type, int depth);

	[[nodiscard]] int consumed() const {
		return int(_position - _from);
	}

	QString error;

private:
	bool fail(const QString &text) {
		if (error.isEmpty()) {
			error = text + QStringLiteral(" (at prime %1)").arg(consumed());
		}
		return false;
	}
	bool has(qint64 primes) const {
		return primes >= 0 && (_till - _position) >= primes;
	}
	bool readInt(qint32 &out) {
		if (!has(1)) {
			return fail(QStringLiteral("unexpected end of data"));
		}
		out = qFromLittleEndian(*_position++);
		return true;
	}
	bool readLong(qint64 &out) {
		if (!has(2)) {
			return fail(QStringLiteral("unexpected end of data"));
		}
		auto raw = qint64();
		std::memcpy(&raw, _position, 8);
		out = qFromLittleEndian(raw);
		_position += 2;
		return true;
	}
	bool readBytes(QByteArray &out);
	bool boxed(const TypeRef &type, int depth);
	bool fields(const Combinator &combinator, int depth);
	bool gzipped(const TypeRef &type, int depth);

	const Schema &_schema;
	const DecodeOptions &_options;
	const Prime *_from = nullptr;
	const Prime *_position = nullptr;
	const Prime *_till = nullptr;
	JsonWriter &_out;
	qint64 &_unpacked;
	int _gzipNesting = 0;

};

bool Decoder::readBytes(QByteArray &out) {
	if (!has(1)) {
		return fail(QStringLiteral("unexpected end of data"));
	}
	const auto bytes = reinterpret_cast<const uchar*>(_position);
	const auto available = qint64(_till - _position) * 4;
	auto length = qint64(bytes[0]);
	auto offset = 1;
	if (length == 254) {
		length = qint64(bytes[1])
			| (qint64(bytes[2]) << 8)
			| (qint64(bytes[3]) << 16);
		offset = 4;
	} else if (length == 255) {
		return fail(QStringLiteral("bad string length"));
	}
	const auto total = ((offset + length + 3) / 4) * 4;
	if (total > available) {
		return fail(QStringLiteral("string is longer than the data"));
	}
	out = QByteArray(reinterpret_cast<const char*>(bytes + offset), int(length));
	_position += total / 4;
	return true;
}

bool Decoder::gzipped(const TypeRef &type, int depth) {
	auto packed = QByteArray();
	if (!readBytes(packed)) {
		return false;
	} else if (!_options.ungzip) {
		return fail(QStringLiteral("gzip_packed is not supported here"));
	} else if (_gzipNesting >= kMaxGzipNesting) {
		return fail(QStringLiteral("gzip_packed is nested too deep"));
	}
	const auto unpacked = _options.ungzip(packed);
	if (unpacked.isEmpty() || (unpacked.size() % 4)) {
		return fail(QStringLiteral("bad gzip_packed data"));
	}
	_unpacked += unpacked.size();
	if (_unpacked > kMaxUnpackedTotal) {
		return fail(QStringLiteral("gzip_packed data is too large"));
	}
	auto buffer = Buffer(int(unpacked.size() / 4));
	std::memcpy(buffer.data(), unpacked.constData(), unpacked.size());
	auto inner = Decoder(
		_schema,
		_options,
		buffer.constData(),
		buffer.constData() + buffer.size(),
		_out,
		_unpacked,
		_gzipNesting + 1);
	if (!inner.value(type, depth)) {
		return fail(QStringLiteral("in gzip_packed: ") + inner.error);
	}
	return true;
}

bool Decoder::value(const TypeRef &type, int depth) {
	if (depth > kMaxDepth) {
		return fail(QStringLiteral("too deep"));
	}
	switch (type.kind) {
	case TypeKind::Int:
	case TypeKind::Flags: {
		auto result = qint32();
		if (!readInt(result)) {
			return false;
		}
		_out.raw(QByteArray::number(result));
		return true;
	}
	case TypeKind::Long: {
		auto result = qint64();
		if (!readLong(result)) {
			return false;
		}
		_out.raw("\"");
		_out.raw(QByteArray::number(result));
		_out.raw("\"");
		return true;
	}
	case TypeKind::Double: {
		auto result = qint64();
		if (!readLong(result)) {
			return false;
		}
		auto number = 0.;
		std::memcpy(&number, &result, 8);
		_out.raw(std::isfinite(number)
			? QByteArray::number(number, 'g', 17)
			: QByteArray("null"));
		return true;
	}
	case TypeKind::String: {
		auto result = QByteArray();
		if (!readBytes(result)) {
			return false;
		}
		_out.string(QString::fromUtf8(result));
		return true;
	}
	case TypeKind::Bytes: {
		auto result = QByteArray();
		if (!readBytes(result)) {
			return false;
		}
		_out.string(QString::fromLatin1(result.toBase64()));
		return true;
	}
	case TypeKind::Int128:
	case TypeKind::Int256: {
		const auto primes = (type.kind == TypeKind::Int128) ? 4 : 8;
		if (!has(primes)) {
			return fail(QStringLiteral("unexpected end of data"));
		}
		const auto data = QByteArray(
			reinterpret_cast<const char*>(_position),
			primes * 4);
		_position += primes;
		_out.string(QString::fromLatin1(data.toBase64()));
		return true;
	}
	case TypeKind::True:
		_out.raw("true");
		return true;
	case TypeKind::Vector:
	case TypeKind::BareVector: {
		if (type.kind == TypeKind::Vector) {
			auto id = qint32();
			if (!readInt(id)) {
				return false;
			} else if (quint32(id) == kGzipPackedId) {
				return gzipped(type, depth);
			} else if (quint32(id) != kVectorId) {
				return fail(QStringLiteral("vector expected, got 0x") + IdToHex(quint32(id)));
			}
		}
		auto count = qint32();
		if (!readInt(count)) {
			return false;
		} else if (count < 0 || !has(count)) {
			return fail(QStringLiteral("bad vector size %1").arg(count));
		}
		const auto inner = type.inner ? *type.inner : TypeRef();
		_out.raw("[");
		for (auto i = 0; i != count; ++i) {
			if (i) {
				_out.comma();
			}
			if (!value(inner, depth + 1)) {
				return false;
			}
		}
		_out.raw("]");
		return true;
	}
	case TypeKind::Bare: {
		auto combinator = (const Combinator*)nullptr;
		if (type.name.startsWith(QLatin1Char('%'))) {
			const auto &list = _schema.constructorsOf(type.name.mid(1));
			if (list.size() == 1) {
				combinator = list.front();
			}
		} else {
			combinator = _schema.constructor(type.name);
		}
		if (!combinator) {
			return fail(QStringLiteral("unknown bare type ") + type.name);
		}
		return fields(*combinator, depth);
	}
	case TypeKind::Bool:
	case TypeKind::Boxed:
	case TypeKind::Generic:
	case TypeKind::Object:
	case TypeKind::Query:
		return boxed(type, depth);
	}
	return fail(QStringLiteral("unsupported type ") + type.text());
}

bool Decoder::boxed(const TypeRef &type, int depth) {
	auto raw = qint32();
	if (!readInt(raw)) {
		return false;
	}
	const auto id = quint32(raw);
	if (id == kBoolTrueId) {
		_out.raw("true");
		return true;
	} else if (id == kBoolFalseId) {
		_out.raw("false");
		return true;
	} else if (id == kGzipPackedId) {
		return gzipped(type, depth);
	} else if (id == kVectorId) {
		return fail(QStringLiteral("a vector of unknown type"));
	}
	auto combinator = _schema.constructor(id);
	if (!combinator && type.kind != TypeKind::Boxed) {
		combinator = _schema.method(id);
	}
	if (!combinator) {
		return fail(QStringLiteral("unknown constructor 0x") + IdToHex(id));
	}
	return fields(*combinator, depth);
}

bool Decoder::fields(const Combinator &combinator, int depth) {
	auto flags = std::vector<quint32>(combinator.params.size(), 0);
	_out.raw("{\"_\":");
	_out.string(combinator.name);
	const auto notify = (depth == 0) && _options.topLevelField;
	for (auto i = 0; i != int(combinator.params.size()); ++i) {
		const auto &param = combinator.params[i];
		if (param.type.kind == TypeKind::Flags) {
			auto value = qint32();
			if (!readInt(value)) {
				return false;
			}
			flags[i] = quint32(value);
			continue;
		} else if (param.optional()
			&& !(flags[param.flagsIndex] & (quint32(1) << param.bit))) {
			continue;
		}
		_out.comma();
		_out.key(param.name);
		const auto start = _position;
		if (param.type.kind == TypeKind::True) {
			_out.raw("true");
		} else if (!value(param.type, depth + 1)) {
			return false;
		}
		if (notify) {
			_options.topLevelField(combinator, param, start, _position);
		}
	}
	_out.raw("}");
	return true;
}

} // namespace

EncodeResult EncodeMethod(
		const Schema &schema,
		const QString &method,
		const QJsonObject &params,
		const EncodeOptions &options) {
	auto result = EncodeResult();
	const auto combinator = schema.method(method);
	if (!combinator) {
		result.error = schema.constructor(method)
			? (QStringLiteral("'") + method + QStringLiteral("' is a constructor, not a method"))
			: (QStringLiteral("unknown method '") + method + QLatin1Char('\''));
		return result;
	}
	auto object = params;
	object.remove(QStringLiteral("_"));
	if (options.check) {
		result.error = options.check(*combinator, object);
		if (!result.error.isEmpty()) {
			return result;
		}
	}
	auto encoder = Encoder(schema, options);
	encoder.putId(combinator->id);
	if (!encoder.fields(*combinator, object, QString())) {
		result.error = encoder.error;
		return result;
	}
	result.data = encoder.take();
	result.method = combinator;
	result.result = (combinator->type.kind == TypeKind::Generic)
		? encoder.queryResult
		: combinator->type;
	return result;
}

EncodeResult EncodeValue(
		const Schema &schema,
		const QJsonValue &value,
		const TypeRef &type,
		const EncodeOptions &options) {
	auto result = EncodeResult();
	auto encoder = Encoder(schema, options);
	if (!encoder.value(value, type, QString())) {
		result.error = encoder.error;
		return result;
	}
	result.data = encoder.take();
	result.result = type;
	return result;
}

DecodeResult Decode(
		const Schema &schema,
		const Prime *from,
		const Prime *till,
		const TypeRef &type,
		const DecodeOptions &options) {
	auto writer = JsonWriter();
	auto unpacked = qint64();
	auto decoder = Decoder(schema, options, from, till, writer, unpacked);
	auto result = DecodeResult();
	if (!decoder.value(type, 0)) {
		result.error = decoder.error;
		return result;
	}
	result.json = writer.take();
	result.size = decoder.consumed();
	return result;
}

QJsonValue ParseJson(const QByteArray &json) {
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(
		'[' + json + ']',
		&error);
	if (error.error != QJsonParseError::NoError || !document.isArray()) {
		return QJsonValue(QJsonValue::Undefined);
	}
	return document.array().at(0);
}

QString IdToHex(quint32 id) {
	return QStringLiteral("%1").arg(id, 8, 16, QLatin1Char('0'));
}

} // namespace Damza::Tl
