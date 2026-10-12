// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
//
// JSON <-> TL binary, the JSON shape is described in docs/plugins/api/mtproto.md.
#pragma once

#include "damza/plugins/tl/tl_schema.h"

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QVector>

#include <functional>

namespace Damza::Tl {

using Prime = qint32;
using Buffer = QVector<Prime>;

struct EncodeOptions {
	// Returns the TL object, an error text as a string, or undefined.
	std::function<QJsonValue(const QString &type, const QJsonValue &value)> resolve;
	bool defaults = true;
};

struct DecodeOptions {
	std::function<QByteArray(const QByteArray &packed)> ungzip;
	std::function<void(
		const Combinator &owner,
		const Param &param,
		const Prime *from,
		const Prime *till)> topLevelField;
};

struct EncodeResult {
	Buffer data;
	QString error;
	const Combinator *method = nullptr;
	TypeRef result;

	[[nodiscard]] bool ok() const {
		return error.isEmpty();
	}
};

struct DecodeResult {
	QByteArray json;
	QString error;
	int size = 0;

	[[nodiscard]] bool ok() const {
		return error.isEmpty();
	}
};

[[nodiscard]] EncodeResult EncodeMethod(
	const Schema &schema,
	const QString &method,
	const QJsonObject &params,
	const EncodeOptions &options = {});

[[nodiscard]] EncodeResult EncodeValue(
	const Schema &schema,
	const QJsonValue &value,
	const TypeRef &type = TypeRef(),
	const EncodeOptions &options = {});

[[nodiscard]] DecodeResult Decode(
	const Schema &schema,
	const Prime *from,
	const Prime *till,
	const TypeRef &type = TypeRef(),
	const DecodeOptions &options = {});

[[nodiscard]] inline DecodeResult Decode(
		const Schema &schema,
		const Buffer &buffer,
		const TypeRef &type = TypeRef(),
		const DecodeOptions &options = {}) {
	return Decode(
		schema,
		buffer.constData(),
		buffer.constData() + buffer.size(),
		type,
		options);
}

[[nodiscard]] QJsonValue ParseJson(const QByteArray &json);

[[nodiscard]] QString IdToHex(quint32 id);

} // namespace Damza::Tl
