// Standalone tests of the TL codec used by dzg.api (no client needed):
//
//   tools/damza/tests/tl_codec_test.sh
#include "damza/plugins/tl/tl_codec.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

#include <cstdio>
#include <cstring>

using namespace Damza::Tl;

namespace {

int Failures = 0;
int Checks = 0;

#define CHECK(cond) do { \
	++Checks; \
	if (!(cond)) { \
		++Failures; \
		std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (false)

#define CHECK_EQ(a, b) do { \
	++Checks; \
	const auto x_ = (a); \
	const auto y_ = (b); \
	if (!(x_ == y_)) { \
		++Failures; \
		std::printf("FAIL %s:%d: %s == %s\n  got:      %s\n  expected: %s\n", \
			__FILE__, __LINE__, #a, #b, \
			Show(x_).toUtf8().constData(), Show(y_).toUtf8().constData()); \
	} \
} while (false)

QString Show(const QString &value) { return value; }
QString Show(const QByteArray &value) { return QString::fromUtf8(value); }
QString Show(qint64 value) { return QString::number(value); }
QString Show(int value) { return QString::number(value); }
QString Show(quint32 value) { return IdToHex(value); }
QString Show(const Buffer &value) {
	auto result = QStringList();
	for (const auto prime : value) {
		result.push_back(IdToHex(quint32(prime)));
	}
	return result.join(' ');
}
QString Show(const QJsonValue &value) {
	return QString::fromUtf8(QJsonDocument(QJsonArray{ value }).toJson(QJsonDocument::Compact));
}

QJsonObject Json(const char *text) {
	return QJsonDocument::fromJson(QByteArray(text)).object();
}

Buffer Primes(std::initializer_list<quint32> list) {
	auto result = Buffer();
	for (const auto value : list) {
		result.push_back(qint32(value));
	}
	return result;
}

void AppendLong(Buffer &buffer, qint64 value) {
	buffer.push_back(qint32(quint64(value) & 0xFFFFFFFFULL));
	buffer.push_back(qint32(quint64(value) >> 32));
}

void TestSchema(const Schema &schema) {
	CHECK_EQ(schema.layer(), 229);
	CHECK_EQ(schema.layer(), EmbeddedLayer());
	CHECK(schema.constructorsCount() > 1500);
	CHECK(schema.methodsCount() > 600);

	const auto history = schema.method(QStringLiteral("messages.getHistory"));
	CHECK(history != nullptr);
	if (history) {
		CHECK_EQ(history->id, quint32(0x4423e6c5));
		CHECK_EQ(int(history->params.size()), 8);
		CHECK_EQ(history->params[0].name, QStringLiteral("peer"));
		CHECK(history->params[0].type.kind == TypeKind::Boxed);
		CHECK_EQ(history->params[7].typeText, QStringLiteral("long"));
		CHECK_EQ(history->typeText, QStringLiteral("messages.Messages"));
		CHECK(!history->params[0].optional());
	}
	const auto message = schema.constructor(QStringLiteral("message"));
	CHECK(message != nullptr);
	if (message) {
		CHECK_EQ(message->id, quint32(0x7600b9d3));
		CHECK(schema.constructor(quint32(0x7600b9d3)) == message);
		auto flags2 = -1;
		for (auto i = 0; i != int(message->params.size()); ++i) {
			if (message->params[i].name == QStringLiteral("flags2")) {
				flags2 = i;
			}
		}
		CHECK(flags2 > 0);
		for (const auto &param : message->params) {
			if (param.name == QStringLiteral("offline")) {
				CHECK_EQ(param.flagsIndex, flags2);
				CHECK_EQ(param.bit, 1);
				CHECK(param.type.kind == TypeKind::True);
			} else if (param.name == QStringLiteral("entities")) {
				CHECK(param.type.kind == TypeKind::Vector);
				CHECK(param.type.inner && param.type.inner->kind == TypeKind::Boxed);
				CHECK_EQ(param.type.text(), QStringLiteral("Vector<MessageEntity>"));
			}
		}
	}
	const auto invoke = schema.method(QStringLiteral("invokeWithLayer"));
	CHECK(invoke != nullptr);
	if (invoke) {
		CHECK_EQ(invoke->generic, QStringLiteral("X"));
		CHECK(invoke->type.kind == TypeKind::Generic);
		CHECK(invoke->params[1].type.kind == TypeKind::Query);
	}
	CHECK(schema.constructor(QStringLiteral("rpc_error")) != nullptr);
	CHECK(schema.method(QStringLiteral("req_pq")) == nullptr);
	CHECK(schema.constructor(QStringLiteral("vector")) == nullptr);
	CHECK_EQ(int(schema.constructorsOf(QStringLiteral("Bool")).size()), 2);
	const auto found = schema.search(QStringLiteral("gethistory"), 50);
	CHECK(found.contains(QStringLiteral("messages.getHistory")));
	CHECK_EQ(schema.search(QStringLiteral("messages.getHistory"), 5).value(0), QStringLiteral("messages.getHistory"));
}

void TestGetHistory(const Schema &schema) {
	const auto encoded = EncodeMethod(schema, QStringLiteral("messages.getHistory"), Json(R"({
		"peer": { "_": "inputPeerChannel", "channel_id": "1234567890123", "access_hash": "-7766554433221100" },
		"offset_id": 10, "offset_date": 0, "add_offset": -5, "limit": 20, "max_id": 0, "min_id": 0
	})"));
	CHECK_EQ(encoded.error, QString());
	auto expected = Primes({ 0x4423e6c5, 0x27bcbbfc });
	AppendLong(expected, 1234567890123LL);
	AppendLong(expected, -7766554433221100LL);
	expected += Primes({ 10, 0, quint32(-5), 20, 0, 0 });
	AppendLong(expected, 0);
	CHECK_EQ(encoded.data, expected);
	CHECK_EQ(encoded.result.text(), QStringLiteral("messages.Messages"));

	const auto missing = EncodeMethod(schema, QStringLiteral("messages.getHistory"), Json(R"({
		"peer": { "_": "inputPeerSelf" }, "limit": 20
	})"));
	CHECK(missing.error.contains(QStringLiteral("offset_id")));

	const auto camel = EncodeMethod(schema, QStringLiteral("messages.getHistory"), Json(R"({
		"peer": { "_": "inputPeerSelf" }, "offsetId": 1
	})"));
	CHECK(camel.error.contains(QStringLiteral("did you mean 'offset_id'")));

	const auto wrongType = EncodeMethod(schema, QStringLiteral("messages.getHistory"), Json(R"({
		"peer": { "_": "inputUserSelf" }, "offset_id": 0, "offset_date": 0, "add_offset": 0, "limit": 1, "max_id": 0, "min_id": 0
	})"));
	CHECK(wrongType.error.startsWith(QStringLiteral("peer: 'inputUserSelf' is InputUser, but InputPeer expected")));

	const auto unknown = EncodeMethod(schema, QStringLiteral("messages.getHistori"), {});
	CHECK(unknown.error.contains(QStringLiteral("unknown method")));
	const auto notMethod = EncodeMethod(schema, QStringLiteral("message"), {});
	CHECK(notMethod.error.contains(QStringLiteral("is a constructor")));
}

void TestResolve(const Schema &schema) {
	auto options = EncodeOptions();
	options.resolve = [](const QString &type, const QJsonValue &value) -> QJsonValue {
		if (type == QStringLiteral("InputPeer") && value.isString()) {
			if (value.toString() == QStringLiteral("missing")) {
				return QStringLiteral("peer not found");
			}
			return QJsonObject{
				{ "_", "inputPeerUser" },
				{ "user_id", value.toString() },
				{ "access_hash", "42" },
			};
		}
		return QJsonValue(QJsonValue::Undefined);
	};
	const auto encoded = EncodeMethod(schema, QStringLiteral("messages.sendReaction"), Json(R"({
		"peer": "777", "msg_id": 5, "big": true,
		"reaction": [ { "_": "reactionEmoji", "emoticon": "❤" } ]
	})"), options);
	CHECK_EQ(encoded.error, QString());
	auto expected = Primes({ 0xd30d78d4, (1u << 0) | (1u << 1), 0xdde8a54c });
	AppendLong(expected, 777);
	AppendLong(expected, 42);
	expected += Primes({ 5, 0x1cb5c415, 1, 0x1b2286b8, 0xa49de203 });
	CHECK_EQ(encoded.data, expected);

	const auto failed = EncodeMethod(schema, QStringLiteral("messages.sendReaction"), Json(R"({
		"peer": "missing", "msg_id": 5
	})"), options);
	CHECK_EQ(failed.error, QStringLiteral("peer: peer not found"));

	const auto decoded = Decode(schema, encoded.data, ParseType(QStringLiteral("Object")));
	CHECK_EQ(decoded.error, QString());
	CHECK_EQ(decoded.json, QByteArray(R"({"_":"messages.sendReaction","big":true,"peer":{"_":"inputPeerUser","user_id":"777","access_hash":"42"},"msg_id":5,"reaction":[{"_":"reactionEmoji","emoticon":"❤"}]})").replace("\\u2764", "\xe2\x9d\xa4"));
}

void TestMessageRoundTrip(const Schema &schema) {
	auto longText = QString();
	for (auto i = 0; i != 300; ++i) {
		longText += QChar(u'a' + (i % 26));
	}
	longText += QStringLiteral(" \"quoted\" \\ \n tab\t юникод \U0001F600");
	auto object = Json(R"({
		"_": "message",
		"out": true,
		"pinned": true,
		"offline": true,
		"id": 4242,
		"from_id": { "_": "peerUser", "user_id": "100500" },
		"peer_id": { "_": "peerChannel", "channel_id": "2000000000123" },
		"reply_to": { "_": "messageReplyHeader", "quote": true, "reply_to_msg_id": 41, "quote_text": "hi" },
		"date": 1700000000,
		"media": { "_": "messageMediaEmpty" },
		"entities": [ { "_": "messageEntityBold", "offset": 0, "length": 5 } ],
		"views": 10,
		"forwards": 2,
		"grouped_id": "-9223372036854775808",
		"effect": "18446744073709551615"
	})");
	object.insert(QStringLiteral("message"), longText);
	const auto encoded = EncodeValue(schema, object, ParseType(QStringLiteral("Message")));
	CHECK_EQ(encoded.error, QString());
	CHECK_EQ(quint32(encoded.data.value(0)), 0x7600b9d3u);
	const auto flags = quint32(encoded.data.value(1));
	CHECK_EQ(flags, (1u << 1) | (1u << 24) | (1u << 8) | (1u << 3) | (1u << 9) | (1u << 7) | (1u << 10) | (1u << 17));
	const auto flags2 = quint32(encoded.data.value(2));
	CHECK_EQ(flags2, (1u << 1) | (1u << 2));

	const auto decoded = Decode(schema, encoded.data, ParseType(QStringLiteral("Message")));
	CHECK_EQ(decoded.error, QString());
	CHECK_EQ(decoded.size, int(encoded.data.size()));
	CHECK(decoded.json.startsWith("{\"_\":\"message\",\"out\":true,\"pinned\":true,\"offline\":true,\"id\":4242,"));
	auto back = ParseJson(decoded.json).toObject();
	auto expected = object;
	expected.insert(QStringLiteral("effect"), QStringLiteral("-1"));
	CHECK_EQ(QJsonValue(back), QJsonValue(expected));

	const auto again = EncodeValue(schema, back, ParseType(QStringLiteral("Message")));
	CHECK_EQ(again.data, encoded.data);

	auto broken = object;
	broken.insert(QStringLiteral("forwards"), QJsonValue());
	broken.remove(QStringLiteral("views"));
	const auto ok = EncodeValue(schema, broken, ParseType(QStringLiteral("Message")));
	CHECK_EQ(ok.error, QString());
	broken.insert(QStringLiteral("forwards"), 3);
	const auto bad = EncodeValue(schema, broken, ParseType(QStringLiteral("Message")));
	CHECK(bad.error.startsWith(QStringLiteral("views: required because bit 10 of 'flags' is set")));

	const auto truncated = Decode(schema, encoded.data.mid(0, encoded.data.size() - 3), ParseType(QStringLiteral("Message")));
	CHECK(!truncated.ok());
}

void TestScalars(const Schema &schema) {
	const auto vectorLong = EncodeValue(schema, QJsonArray{ "1", -2, "18446744073709551615" }, ParseType(QStringLiteral("Vector<long>")));
	CHECK_EQ(vectorLong.error, QString());
	auto expected = Primes({ 0x1cb5c415, 3 });
	AppendLong(expected, 1);
	AppendLong(expected, -2);
	AppendLong(expected, -1);
	CHECK_EQ(vectorLong.data, expected);
	CHECK_EQ(Decode(schema, vectorLong.data, ParseType(QStringLiteral("Vector<long>"))).json, QByteArray(R"(["1","-2","-1"])"));

	const auto imprecise = EncodeValue(schema, QJsonValue(9007199254740993.), ParseType(QStringLiteral("long")));
	CHECK(imprecise.error.contains(QStringLiteral("decimal string")));

	const auto boolTrue = EncodeValue(schema, true, ParseType(QStringLiteral("Bool")));
	CHECK_EQ(boolTrue.data, Primes({ 0x997275b5 }));
	CHECK_EQ(Decode(schema, Primes({ 0xbc799737 })).json, QByteArray("false"));
	CHECK_EQ(Decode(schema, Primes({ 0x1cb5c415, 2, 0x997275b5, 0xbc799737 }), ParseType(QStringLiteral("Vector<Bool>"))).json, QByteArray("[true,false]"));

	const auto bytes = EncodeValue(schema, QStringLiteral("AQID/w=="), ParseType(QStringLiteral("bytes")));
	CHECK_EQ(bytes.data, Primes({ 0x03020104, 0x000000ff }));
	CHECK_EQ(Decode(schema, bytes.data, ParseType(QStringLiteral("bytes"))).json, QByteArray("\"AQID/w==\""));
	const auto arrayBytes = EncodeValue(schema, QJsonArray{ 1, 2, 3, 255 }, ParseType(QStringLiteral("bytes")));
	CHECK_EQ(arrayBytes.data, bytes.data);
	const auto textBytes = EncodeValue(schema, QJsonObject{ { "text", "hi" } }, ParseType(QStringLiteral("bytes")));
	CHECK_EQ(textBytes.data, Buffer{ qint32(0x00696802) });

	auto big = QByteArray(1000, 'x');
	const auto bigString = EncodeValue(schema, QString::fromLatin1(big), ParseType(QStringLiteral("string")));
	CHECK_EQ(bigString.data.size(), 251);
	CHECK_EQ(quint32(bigString.data[0]), 0x0003e8feu);
	CHECK_EQ(Decode(schema, bigString.data, ParseType(QStringLiteral("string"))).json, '"' + big + '"');

	const auto dbl = EncodeValue(schema, 0.5, ParseType(QStringLiteral("double")));
	CHECK_EQ(Decode(schema, dbl.data, ParseType(QStringLiteral("double"))).json, QByteArray("0.5"));

	const auto key = QByteArray(32, '\x07').toBase64();
	const auto int256 = EncodeValue(schema, QString::fromLatin1(key), ParseType(QStringLiteral("int256")));
	CHECK_EQ(int256.data.size(), 8);
	CHECK_EQ(Decode(schema, int256.data, ParseType(QStringLiteral("int256"))).json, '"' + key + '"');
	CHECK(!EncodeValue(schema, QStringLiteral("AQID"), ParseType(QStringLiteral("int128"))).ok());

	const auto single = EncodeValue(schema, Json(R"({"data": "{}"})"), ParseType(QStringLiteral("DataJSON")));
	CHECK_EQ(single.error, QString());
	CHECK_EQ(Decode(schema, single.data).json, QByteArray(R"({"_":"dataJSON","data":"{}"})"));
	CHECK(!EncodeValue(schema, Json(R"({"user_id": "1"})"), ParseType(QStringLiteral("InputPeer"))).ok());

	CHECK(!Decode(schema, Primes({ 0x12345678 })).ok());
	CHECK(Decode(schema, Primes({ 0x12345678 })).error.contains(QStringLiteral("unknown constructor 0x12345678")));
}

void TestDefaultsAndGenerics(const Schema &schema) {
	const auto send = EncodeMethod(schema, QStringLiteral("messages.sendMessage"), Json(R"({
		"peer": { "_": "inputPeerSelf" }, "message": "hello", "silent": true,
		"reply_to": { "_": "inputReplyToMessage", "reply_to_msg_id": 7 }
	})"));
	CHECK_EQ(send.error, QString());
	CHECK_EQ(quint32(send.data.value(1)), (1u << 5) | (1u << 0));
	CHECK_EQ(send.data.size(), 1 + 1 + 1 + 3 + 2 + 2);

	auto noDefaults = EncodeOptions();
	noDefaults.defaults = false;
	const auto strict = EncodeMethod(schema, QStringLiteral("messages.sendMessage"), Json(R"({
		"peer": { "_": "inputPeerSelf" }, "message": "hello"
	})"), noDefaults);
	CHECK(strict.error.contains(QStringLiteral("random_id")));

	const auto forward = EncodeMethod(schema, QStringLiteral("messages.forwardMessages"), Json(R"({
		"from_peer": { "_": "inputPeerSelf" }, "id": [1, 2, 3], "to_peer": { "_": "inputPeerSelf" }
	})"));
	CHECK_EQ(forward.error, QString());
	CHECK_EQ(forward.data.size(), 1 + 1 + 1 + (2 + 3) + (2 + 6) + 1);

	const auto layered = EncodeMethod(schema, QStringLiteral("invokeWithLayer"), Json(R"({
		"layer": 229, "query": { "_": "invokeWithoutUpdates", "query": { "_": "help.getConfig" } }
	})"));
	CHECK_EQ(layered.error, QString());
	CHECK_EQ(layered.data, Primes({ 0xda9b0d0d, 229, 0xbf9459b7, 0xc4f9186b }));
	CHECK_EQ(layered.result.text(), QStringLiteral("Config"));
	const auto badQuery = EncodeMethod(schema, QStringLiteral("invokeWithLayer"), Json(R"({
		"layer": 229, "query": { "_": "inputPeerSelf" }
	})"));
	CHECK(!badQuery.ok());

	const auto history = EncodeMethod(schema, QStringLiteral("messages.getHistory"), Json(R"({
		"peer": { "_": "inputPeerSelf" }, "offset_id": 0, "offset_date": 0, "add_offset": 0, "limit": 1, "max_id": 0, "min_id": 0
	})"));
	CHECK_EQ(history.error, QString());
}

void TestUpdatesAndGzip(const Schema &schema) {
	const auto typing = Json(R"({
		"_": "updateShort",
		"update": { "_": "updateUserTyping", "user_id": "123", "action": { "_": "sendMessageTypingAction" } },
		"date": 1700000000
	})");
	const auto encoded = EncodeValue(schema, typing, ParseType(QStringLiteral("Updates")));
	CHECK_EQ(encoded.error, QString());
	auto expected = Primes({ 0x78d4dec1, 0x2a17bf5c, 0 });
	AppendLong(expected, 123);
	expected += Primes({ 0x16bf744e, 1700000000 });
	CHECK_EQ(encoded.data, expected);

	auto fields = QStringList();
	auto options = DecodeOptions();
	options.topLevelField = [&](const Combinator &owner, const Param &param, const Prime *from, const Prime *till) {
		fields.push_back(owner.name + ':' + param.name + ':' + QString::number(till - from));
	};
	const auto decoded = Decode(schema, encoded.data, ParseType(QStringLiteral("Updates")), options);
	CHECK_EQ(decoded.json, QByteArray(R"({"_":"updateShort","update":{"_":"updateUserTyping","user_id":"123","action":{"_":"sendMessageTypingAction"}},"date":1700000000})"));
	CHECK_EQ(fields.join(','), QStringLiteral("updateShort:update:5,updateShort:date:1"));

	auto inner = QByteArray(reinterpret_cast<const char*>(encoded.data.constData()), encoded.data.size() * 4);
	const auto packed = qCompress(inner);
	const auto wrapped = EncodeValue(schema, QString::fromLatin1(packed.toBase64()), ParseType(QStringLiteral("bytes")));
	auto gz = Primes({ kGzipPackedId }) + wrapped.data;
	options.topLevelField = nullptr;
	CHECK(!Decode(schema, gz, ParseType(QStringLiteral("Updates"))).ok());
	options.ungzip = [](const QByteArray &data) { return qUncompress(data); };
	const auto unzipped = Decode(schema, gz, ParseType(QStringLiteral("Updates")), options);
	CHECK_EQ(unzipped.error, QString());
	CHECK_EQ(unzipped.json, decoded.json);
	CHECK_EQ(unzipped.size, int(gz.size()));

	auto vector = Primes({ 0x1cb5c415, 1 }) + encoded.data.mid(1, 5);
	const auto updates = Decode(schema, vector, ParseType(QStringLiteral("Vector<Update>")));
	CHECK_EQ(updates.json, QByteArray(R"([{"_":"updateUserTyping","user_id":"123","action":{"_":"sendMessageTypingAction"}}])"));

	const auto error = Primes({ kRpcErrorId, 400 }) + EncodeValue(schema, QStringLiteral("PEER_ID_INVALID"), ParseType(QStringLiteral("string"))).data;
	CHECK_EQ(Decode(schema, error).json, QByteArray(R"({"_":"rpc_error","error_code":400,"error_message":"PEER_ID_INVALID"})"));
}

void TestHostileInput(const Schema &schema) {
	// Type texts come from plugins: deep nesting must not recurse unbounded.
	auto deepType = QString();
	for (auto i = 0; i != 100000; ++i) {
		deepType += QStringLiteral("Vector<");
	}
	deepType += QStringLiteral("int");
	for (auto i = 0; i != 100000; ++i) {
		deepType += QLatin1Char('>');
	}
	const auto deep = ParseType(deepType);
	CHECK(deep.kind == TypeKind::Vector);
	CHECK(Decode(schema, Primes({ 0x1cb5c415, 0 }), deep).ok());
	CHECK(EncodeValue(schema, QJsonArray{ QJsonArray{} }, deep).ok());
	auto nested = Primes({});
	for (auto i = 0; i != 20; ++i) {
		nested += Primes({ 0x1cb5c415, 1 });
	}
	CHECK(!Decode(schema, nested, deep).ok());
	CHECK_EQ(ParseType(QStringLiteral("Vector<Vector<long>>")).text(), QStringLiteral("Vector<Vector<long>>"));

	// Deep but valid data is refused cleanly instead of overflowing the stack.
	const auto jsonArray = quint32(0xf7444763);
	auto deepData = Primes({});
	for (auto i = 0; i != 5000; ++i) {
		deepData += Primes({ jsonArray, 0x1cb5c415, 1 });
	}
	deepData += Primes({ 0x3f6d7b68 }); // jsonNull
	const auto tooDeep = Decode(schema, deepData, ParseType(QStringLiteral("JSONValue")));
	CHECK(tooDeep.error.startsWith(QStringLiteral("too deep")));
	auto fineData = Primes({});
	for (auto i = 0; i != 30; ++i) {
		fineData += Primes({ jsonArray, 0x1cb5c415, 1 });
	}
	fineData += Primes({ 0x3f6d7b68 });
	const auto fine = Decode(schema, fineData, ParseType(QStringLiteral("JSONValue")));
	CHECK_EQ(fine.error, QString());
	auto deepJson = QJsonValue(QJsonObject{ { "_", "jsonNull" } });
	for (auto i = 0; i != 300; ++i) {
		deepJson = QJsonObject{ { "_", "jsonArray" }, { "value", QJsonArray{ deepJson } } };
	}
	const auto tooDeepEncode = EncodeValue(schema, deepJson, ParseType(QStringLiteral("JSONValue")));
	CHECK(tooDeepEncode.error.contains(QStringLiteral("too deep")));
	const auto back = ParseJson(fine.json);
	CHECK_EQ(EncodeValue(schema, back, ParseType(QStringLiteral("JSONValue"))).data, fineData);

	// gzip_packed that unpacks to gzip_packed again (a "quine") must stop.
	auto options = DecodeOptions();
	auto unpacks = 0;
	auto self = Buffer();
	options.ungzip = [&](const QByteArray &) {
		++unpacks;
		return QByteArray(
			reinterpret_cast<const char*>(self.constData()),
			self.size() * 4);
	};
	self = Primes({ kGzipPackedId, 0x00000000 });
	const auto quine = Decode(schema, self, ParseType(QStringLiteral("Updates")), options);
	CHECK(!quine.ok());
	CHECK(quine.error.contains(QStringLiteral("nested too deep")));
	CHECK(unpacks <= 3);

	// A vector of gzip_packed bombs is limited in total.
	unpacks = 0;
	options.ungzip = [&](const QByteArray &) {
		++unpacks;
		auto result = QByteArray(16 * 1024 * 1024, '\0');
		const auto empty = Primes({ 0x1cb5c415, 0 });
		std::memcpy(result.data(), empty.constData(), 8);
		return result;
	};
	auto bombs = Primes({ 0x1cb5c415, 100 });
	for (auto i = 0; i != 100; ++i) {
		bombs += Primes({ kGzipPackedId, 0x00000000 });
	}
	const auto bombed = Decode(schema, bombs, ParseType(QStringLiteral("Vector<Vector<int>>")), options);
	CHECK(bombed.error.contains(QStringLiteral("too large")));
	CHECK_EQ(unpacks, 5);

	// bytes longer than the 24-bit TL length can't be encoded.
	const auto huge = QString(0x1000000, QLatin1Char('a'));
	CHECK(EncodeValue(schema, huge, ParseType(QStringLiteral("string"))).error.contains(QStringLiteral("too long")));
	CHECK(EncodeValue(schema, QString(0xFFFFFF, QLatin1Char('a')), ParseType(QStringLiteral("string"))).ok());

	// Explicit out of range flags are ignored instead of being converted.
	const auto flags = EncodeValue(schema, Json(R"({"_": "inputReplyToMessage", "flags": 1e300, "reply_to_msg_id": 1})"), ParseType(QStringLiteral("InputReplyTo")));
	CHECK_EQ(flags.error, QString());
	CHECK_EQ(quint32(flags.data.value(1)), 0u);
}

void TestCheck(const Schema &schema) {
	auto seen = QStringList();
	auto options = EncodeOptions();
	options.check = [&](const Combinator &method, const QJsonObject &params) {
		seen.push_back(method.name);
		if (method.name == QStringLiteral("auth.signIn")) {
			return QStringLiteral("refused");
		} else if (method.name == QStringLiteral("invokeWithLayer")
			&& params.value(QStringLiteral("layer")).toInt() != 229) {
			return QStringLiteral("bad layer");
		}
		return QString();
	};
	const auto wrapped = EncodeMethod(schema, QStringLiteral("invokeWithLayer"), Json(R"({
		"layer": 229, "query": { "_": "invokeWithoutUpdates", "query": { "_": "auth.signIn", "phone_number": "1", "phone_code_hash": "x" } }
	})"), options);
	CHECK_EQ(wrapped.error, QStringLiteral("query.query: refused"));
	CHECK_EQ(seen.join(','), QStringLiteral("invokeWithLayer,invokeWithoutUpdates,auth.signIn"));

	const auto layer = EncodeMethod(schema, QStringLiteral("invokeWithLayer"), Json(R"({
		"layer": 1, "query": { "_": "help.getConfig" }
	})"), options);
	CHECK_EQ(layer.error, QStringLiteral("bad layer"));

	seen.clear();
	const auto allowed = EncodeMethod(schema, QStringLiteral("invokeWithoutUpdates"), Json(R"({
		"query": { "_": "upload.getFile", "location": { "_": "inputPeerPhotoFileLocation", "peer": { "_": "inputPeerSelf" }, "photo_id": "1" }, "offset": "0", "limit": 1024 }
	})"), options);
	CHECK_EQ(allowed.error, QString());
	CHECK_EQ(seen.join(','), QStringLiteral("invokeWithoutUpdates,upload.getFile"));
	CHECK_EQ(allowed.result.text(), QStringLiteral("upload.File"));
}

QJsonValue Sample(const Schema &schema, const TypeRef &type, int depth);

QJsonObject SampleObject(const Schema &schema, const Combinator &combinator, int depth) {
	auto result = QJsonObject{ { "_", combinator.name } };
	for (const auto &param : combinator.params) {
		if (param.type.kind == TypeKind::Flags || (param.optional() && depth > 3)) {
			continue;
		}
		const auto value = Sample(schema, param.type, depth + 1);
		if (value.isUndefined()) {
			return {};
		}
		result.insert(param.name, value);
	}
	return result;
}

QJsonValue Sample(const Schema &schema, const TypeRef &type, int depth) {
	switch (type.kind) {
	case TypeKind::Int: return 7;
	case TypeKind::Long: return "-1234567890123";
	case TypeKind::Double: return 1.5;
	case TypeKind::String: return QString::fromUtf8("s\xc3\xa9");
	case TypeKind::Bytes: return "AQI=";
	case TypeKind::Int128: return QString::fromLatin1(QByteArray(16, 'a').toBase64());
	case TypeKind::Int256: return QString::fromLatin1(QByteArray(32, 'b').toBase64());
	case TypeKind::True: return true;
	case TypeKind::Bool: return false;
	case TypeKind::Vector:
	case TypeKind::BareVector: {
		const auto inner = Sample(schema, *type.inner, depth);
		return inner.isUndefined() ? inner : QJsonArray{ inner };
	}
	case TypeKind::Boxed: {
		const auto &list = schema.constructorsOf(type.name);
		if (list.empty() || depth > 12) {
			return QJsonValue(QJsonValue::Undefined);
		}
		auto chosen = list.front();
		if (depth > 2) {
			for (const auto c : list) {
				if (c->params.size() < chosen->params.size()) {
					chosen = c;
				}
			}
		}
		const auto object = SampleObject(schema, *chosen, depth);
		return object.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(object);
	}
	case TypeKind::Query: return QJsonObject{ { "_", "help.getConfig" } };
	default: return QJsonValue(QJsonValue::Undefined);
	}
}

void TestWholeSchema(const Schema &schema) {
	auto tested = 0;
	auto skipped = QStringList();
	for (const auto &name : schema.search(QString(), 0)) {
		const auto constructor = schema.constructor(name);
		const auto method = constructor ? nullptr : schema.method(name);
		const auto combinator = constructor ? constructor : method;
		if (!combinator || (constructor && constructor->typeText == QStringLiteral("Bool"))) {
			continue;
		}
		const auto object = SampleObject(schema, *combinator, 0);
		if (object.isEmpty()) {
			skipped.push_back(name);
			continue;
		}
		const auto encoded = method
			? EncodeMethod(schema, name, object)
			: EncodeValue(schema, object, ParseType(QStringLiteral("Object")));
		if (!encoded.ok()) {
			++Failures;
			std::printf("FAIL encode %s: %s\n", name.toUtf8().constData(), encoded.error.toUtf8().constData());
			continue;
		}
		const auto decoded = Decode(schema, encoded.data, ParseType(QStringLiteral("Object")));
		const auto back = ParseJson(decoded.json);
		++Checks;
		if (!decoded.ok() || back != QJsonValue(object) || decoded.size != encoded.data.size()) {
			++Failures;
			std::printf("FAIL round trip %s: %s\n  %s\n  %s\n",
				name.toUtf8().constData(),
				decoded.error.toUtf8().constData(),
				Show(QJsonValue(object)).toUtf8().constData(),
				decoded.json.constData());
		}
		++tested;
	}
	std::printf("whole schema: %d combinators round-tripped, %d skipped\n", tested, int(skipped.size()));
	if (!skipped.isEmpty()) {
		std::printf("  skipped: %s\n", skipped.mid(0, 20).join(", ").toUtf8().constData());
	}
}

} // namespace

int main() {
	const auto &schema = Schema::Embedded();
	TestSchema(schema);
	TestGetHistory(schema);
	TestResolve(schema);
	TestMessageRoundTrip(schema);
	TestScalars(schema);
	TestDefaultsAndGenerics(schema);
	TestUpdatesAndGzip(schema);
	TestHostileInput(schema);
	TestCheck(schema);
	TestWholeSchema(schema);
	std::printf("%d checks, %d failures\n", Checks, Failures);
	return Failures ? 1 : 0;
}
