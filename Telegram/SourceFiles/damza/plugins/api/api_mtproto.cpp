// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/plugins/api/api_mtproto.h"

#include "api/api_updates.h"
#include "apiwrap.h"
#include "damza/plugins/plugin_api.h"
#include "damza/plugins/plugin_bridge.h"
#include "damza/plugins/plugin_data.h"
#include "damza/plugins/plugin_instance.h"
#include "damza/plugins/plugin_manager.h"
#include "damza/plugins/tl/tl_codec.h"
#include "damza/plugins/tl/tl_schema_layer.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "mtproto/details/mtproto_serialized_request.h"
#include "mtproto/facade.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_dc_options.h"
#include "mtproto/mtproto_response.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QTimer>

#include <zlib.h>

namespace Damza::Plugins {
namespace details {

bool RawUpdatesWanted = false;

} // namespace details
namespace {

static_assert(
	Tl::kEmbeddedLayer == MTP::details::kCurrentLayer,
	"Embedded TL schema is stale, run tools/damza/gen_tl.py");

constexpr auto kDefaultFloodSleep = 10;
constexpr auto kMaxUnpackedSize = 64 * 1024 * 1024;
// One dispatch runs under one watchdog deadline, keep the batches small.
constexpr auto kMaxUpdatesPerDispatch = 100;

struct PendingCall {
	QPointer<Bridge> bridge;
	QString pluginId;
	int jsId = 0;
	QPointer<MTP::Instance> mtp;
	mtpRequestId requestId = 0;
};

struct UpdateListener {
	base::flat_set<mtpTypeId> ids;
	bool all = false;
};

struct QueuedUpdate {
	PeerId self = 0;
	mtpTypeId type = 0;
	mtpBuffer data;
	bool shortMessage = false;
};

struct State {
	std::map<int, PendingCall> calls;
	int callSeq = 0;
	base::flat_map<QString, UpdateListener> listeners;
	base::flat_set<mtpTypeId> wanted;
	bool wantAll = false;
	std::vector<QueuedUpdate> queue;
	bool flushScheduled = false;
};

[[nodiscard]] State &GetState() {
	static auto state = State();
	return state;
}

[[nodiscard]] const Tl::Schema &Schema() {
	return Tl::Schema::Embedded();
}

[[nodiscard]] QByteArray Ungzip(const QByteArray &packed) {
	auto stream = z_stream();
	if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
		return {};
	}
	constexpr auto kChunk = 64 * 1024;
	auto result = QByteArray();
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(packed.data()));
	stream.avail_in = uInt(packed.size());
	auto status = Z_OK;
	while (status != Z_STREAM_END) {
		const auto was = int(result.size());
		if (was > kMaxUnpackedSize) {
			inflateEnd(&stream);
			return {};
		}
		result.resize(was + kChunk);
		stream.next_out = reinterpret_cast<Bytef*>(result.data() + was);
		stream.avail_out = kChunk;
		status = inflate(&stream, Z_NO_FLUSH);
		result.resize(was + kChunk - int(stream.avail_out));
		if (status != Z_OK && status != Z_STREAM_END) {
			inflateEnd(&stream);
			return {};
		} else if (status == Z_OK && !stream.avail_in && stream.avail_out) {
			inflateEnd(&stream);
			return {};
		}
	}
	inflateEnd(&stream);
	return result;
}

[[nodiscard]] Tl::DecodeOptions BaseDecodeOptions() {
	auto result = Tl::DecodeOptions();
	result.ungzip = Ungzip;
	return result;
}

template <typename Value>
[[nodiscard]] QByteArray ToJson(const Value &value, const QString &type) {
	auto buffer = mtpBuffer();
	value.write(buffer);
	const auto decoded = Tl::Decode(
		Schema(),
		buffer,
		Tl::ParseType(type),
		BaseDecodeOptions());
	return decoded.ok() ? decoded.json : QByteArray();
}

enum class InputKind {
	Peer,
	User,
	Channel,
};

[[nodiscard]] std::optional<InputKind> InputKindFromType(const QString &type) {
	if (type == u"InputPeer"_q) {
		return InputKind::Peer;
	} else if (type == u"InputUser"_q) {
		return InputKind::User;
	} else if (type == u"InputChannel"_q) {
		return InputKind::Channel;
	}
	return std::nullopt;
}

[[nodiscard]] QByteArray InputJson(not_null<PeerData*> peer, InputKind kind) {
	switch (kind) {
	case InputKind::Peer:
		return ToJson(peer->input(), u"InputPeer"_q);
	case InputKind::User:
		if (const auto user = peer->asUser()) {
			return ToJson(user->inputUser(), u"InputUser"_q);
		}
		return {};
	case InputKind::Channel:
		if (const auto channel = peer->asChannel()) {
			return ToJson(channel->inputChannel(), u"InputChannel"_q);
		}
		return {};
	}
	return {};
}

[[nodiscard]] bool IsSelfAlias(const QString &id) {
	return (id == u"me"_q) || (id == u"self"_q);
}

[[nodiscard]] QByteArray SelfJson(InputKind kind) {
	switch (kind) {
	case InputKind::Peer: return R"({"_":"inputPeerSelf"})";
	case InputKind::User: return R"({"_":"inputUserSelf"})";
	case InputKind::Channel: return {};
	}
	return {};
}

[[nodiscard]] QJsonValue ResolveInput(
		Main::Session *session,
		const QString &type,
		const QJsonValue &value) {
	const auto kind = InputKindFromType(type);
	if (!kind) {
		return QJsonValue(QJsonValue::Undefined);
	}
	auto id = QString();
	if (value.isString()) {
		id = value.toString();
	} else if (value.isObject()) {
		const auto object = value.toObject();
		const auto peerId = object.value(u"peerId"_q);
		id = peerId.isString()
			? peerId.toString()
			: object.value(u"id"_q).toString();
	} else if (value.isDouble()) {
		return u"peer ids are strings, got a number"_q;
	} else {
		return QJsonValue(QJsonValue::Undefined);
	}
	if (id.isEmpty()) {
		return QJsonValue(QJsonValue::Undefined);
	}
	auto json = QByteArray();
	if (IsSelfAlias(id)) {
		json = SelfJson(*kind);
	} else if (const auto peer = ResolvePeer(session, id)) {
		json = InputJson(peer, *kind);
	} else {
		return u"peer '%1' is unknown to the client"_q.arg(id);
	}
	if (json.isEmpty()) {
		return u"peer '%1' can't be used as %2"_q.arg(id, type);
	}
	return Tl::ParseJson(json);
}

[[nodiscard]] bool IsUpdatesConstructor(mtpTypeId id) {
	switch (id) {
	case mtpc_updates:
	case mtpc_updatesCombined:
	case mtpc_updateShort:
	case mtpc_updateShortMessage:
	case mtpc_updateShortChatMessage:
	case mtpc_updateShortSentMessage:
	case mtpc_updatesTooLong:
		return true;
	}
	return false;
}

// WHY: *_MIGRATE_X on the main DC makes the client switch the account's
// main DC, so these methods are refused or sent to a download DC.
[[nodiscard]] bool IsRefusedMethod(const QString &method) {
	static const auto list = base::flat_set<QString>{
		u"auth.sendCode"_q,
		u"auth.signIn"_q,
		u"auth.signUp"_q,
		u"auth.importAuthorization"_q,
		u"auth.importBotAuthorization"_q,
		u"auth.importLoginToken"_q,
		u"auth.importWebTokenAuthorization"_q,
		u"auth.bindTempAuthKey"_q,
	};
	return list.contains(method);
}

// Called for the method and for every method wrapped in it with !X
// (invokeWithLayer, invokeWithoutUpdates...): a wrapper must not let
// a refused method through.
[[nodiscard]] QString RefuseReason(
		const Tl::Combinator &method,
		const QJsonObject &params) {
	if (IsRefusedMethod(method.name)) {
		return u"%1 is not available to plugins"_q.arg(method.name);
	} else if (method.name == u"initConnection"_q) {
		// The client initializes its connections itself.
		return u"%1 is not available to plugins"_q.arg(method.name);
	} else if (method.name == u"invokeWithLayer"_q) {
		// Another layer would stay on the session, and the client could
		// not parse the updates and responses that the server sends in it.
		const auto layer = params.value(u"layer"_q).toVariant().toLongLong();
		if (layer != MTP::details::kCurrentLayer) {
			return u"invokeWithLayer: only layer %1 is supported"_q.arg(
				MTP::details::kCurrentLayer);
		}
	}
	return QString();
}

[[nodiscard]] bool IsFileMethod(const QString &method) {
	return method.startsWith(u"upload.get"_q)
		|| (method == u"upload.reuploadCdnFile"_q);
}

// A dc from the config, or a CDN dc the client has keys for (the target
// of upload.getCdnFile). For others MTP starts a session that never connects.
[[nodiscard]] bool IsKnownDc(not_null<Main::Session*> session, int dc) {
	const auto &options = session->mtp().dcOptions();
	const auto id = MTP::DcId(dc);
	if (ranges::contains(options.configEnumDcIds(), id)) {
		return true;
	}
	return (id > 0)
		&& (MTP::BareDcId(MTP::ShiftedDcId(id)) == id)
		&& (options.dcType(MTP::ShiftedDcId(id)) == MTP::DcType::Cdn)
		&& options.hasCDNKeysForDc(id);
}

[[nodiscard]] int FloodSeconds(const QString &type) {
	static const auto regexp = QRegularExpression(
		u"^FLOOD_(?:PREMIUM_)?WAIT_(\\d+)$"_q);
	const auto match = regexp.match(type);
	return match.hasMatch() ? match.captured(1).toInt() : -1;
}

void FinishCall(int key, bool ok, const QVariant &value) {
	auto &state = GetState();
	const auto i = state.calls.find(key);
	if (i == end(state.calls)) {
		return;
	}
	const auto bridge = i->second.bridge;
	const auto jsId = i->second.jsId;
	state.calls.erase(i);
	if (bridge) {
		bridge->settle(jsId, ok, value);
	}
}

// Held by the response handlers. MTP may destroy them without a call: when
// its instance dies (the account logs out) or when it drops a request after
// a temporary error. Then the call is settled from here instead of staying
// pending forever. After a normal finish this is a no-op.
class CallGuard final {
public:
	explicit CallGuard(int key) : _key(key) {
	}
	~CallGuard() {
		crl::on_main([key = _key] {
			FinishCall(
				key,
				false,
				u"Request was dropped by the client (logged out?)"_q);
		});
	}

private:
	int _key = 0;

};

void FeedPeerVector(
		not_null<Main::Session*> session,
		const Tl::TypeRef &type,
		const mtpBuffer &reply) {
	if (type.kind != Tl::TypeKind::Vector || !type.inner) {
		return;
	}
	auto from = reply.constData();
	const auto till = from + reply.size();
	if (type.inner->name == u"User"_q) {
		auto users = MTPVector<MTPUser>();
		if (users.read(from, till)) {
			session->data().processUsers(users);
		}
	} else if (type.inner->name == u"Chat"_q) {
		auto chats = MTPVector<MTPChat>();
		if (chats.read(from, till)) {
			session->data().processChats(chats);
		}
	}
}

void FeedPeers(
		base::weak_ptr<Main::Session> weak,
		const Tl::Param &param,
		const Tl::Prime *from,
		const Tl::Prime *till) {
	const auto session = weak.get();
	if (!session) {
		return;
	} else if (param.name == u"users"_q
		&& param.typeText == u"Vector<User>"_q) {
		auto users = MTPVector<MTPUser>();
		if (users.read(from, till)) {
			session->data().processUsers(users);
		}
	} else if (param.name == u"chats"_q
		&& param.typeText == u"Vector<Chat>"_q) {
		auto chats = MTPVector<MTPChat>();
		if (chats.read(from, till)) {
			session->data().processChats(chats);
		}
	}
}

[[nodiscard]] QVariant StartCall(
		not_null<Bridge*> bridge,
		not_null<Main::Session*> session,
		const Tl::EncodeResult &encoded,
		const QVariantMap &options,
		MTP::ShiftedDcId dcId) {
	auto &state = GetState();
	const auto key = ++state.callSeq;
	const auto jsId = bridge->nextRequestId();
	const auto mtp = &session->mtp();
	const auto requestId = MTP::details::GetNextRequestId();
	const auto resultType = encoded.result;
	const auto apply = options.value(u"applyResult"_q, true).toBool();
	const auto floodSleep = options.contains(u"floodSleep"_q)
		? options.value(u"floodSleep"_q).toInt()
		: kDefaultFloodSleep;
	const auto timeout = options.value(u"timeout"_q).toInt();
	const auto weakSession = base::make_weak(session.get());

	state.calls.emplace(key, PendingCall{
		.bridge = QPointer<Bridge>(bridge.get()),
		.pluginId = bridge->instance()->manifest().id,
		.jsId = jsId,
		.mtp = QPointer<MTP::Instance>(mtp),
		.requestId = requestId,
	});
	const auto guard = std::make_shared<CallGuard>(key);

	auto done = [=, guard = guard](const MTP::Response &response) {
		const auto &reply = response.reply;
		auto decodeOptions = BaseDecodeOptions();
		if (apply) {
			decodeOptions.topLevelField = [=](
					const Tl::Combinator &,
					const Tl::Param &param,
					const Tl::Prime *from,
					const Tl::Prime *till) {
				FeedPeers(weakSession, param, from, till);
			};
		}
		const auto decoded = Tl::Decode(
			Schema(),
			reply.constData(),
			reply.constData() + reply.size(),
			resultType,
			decodeOptions);
		if (!decoded.ok()) {
			FinishCall(key, false, u"RESPONSE_PARSE_FAILED: "_q + decoded.error);
			return true;
		}
		const auto strong = weakSession.get();
		const auto type = mtpTypeId(reply[0]);
		if (apply && strong) {
			// Vector<User> results like users.getUsers, the users / chats
			// fields of an object result were fed by topLevelField.
			FeedPeerVector(strong, resultType, reply);
		}
		if (apply && strong && IsUpdatesConstructor(type)) {
			if (type == mtpc_updateShortSentMessage) {
				// The client has no local copy of a message sent this way.
				strong->api().updates().getDifference();
			} else {
				auto updates = MTPUpdates();
				auto from = reply.constData();
				if (updates.read(from, from + reply.size())) {
					strong->api().applyUpdates(updates);
				}
			}
		}
		FinishCall(key, true, QString::fromUtf8(decoded.json));
		return true;
	};
	auto fail = [=, guard = guard](
			const MTP::Error &error,
			const MTP::Response &) {
		const auto code = error.code();
		if (code < 0 || code >= 500) {
			return false;
		}
		const auto seconds = FloodSeconds(error.type());
		if (seconds >= 0 && seconds <= floodSleep) {
			return false;
		}
		FinishCall(
			key,
			false,
			QString::number(code) + u": "_q + error.type());
		return true;
	};

	using Serialized = MTP::details::SerializedRequest;
	const auto size = uint32(encoded.data.size());
	auto request = Serialized::Prepare(size);
	request->resize(Serialized::kMessageBodyPosition + size);
	memcpy(
		request->data() + Serialized::kMessageBodyPosition,
		encoded.data.constData(),
		size * sizeof(mtpPrime));
	mtp->sendSerialized(
		requestId,
		std::move(request),
		MTP::ResponseHandler{ std::move(done), std::move(fail) },
		dcId,
		0,
		0);

	if (timeout > 0) {
		QTimer::singleShot(timeout, bridge.get(), [=] {
			auto &state = GetState();
			const auto i = state.calls.find(key);
			if (i == end(state.calls)) {
				return;
			} else if (const auto instance = i->second.mtp.data()) {
				instance->cancel(i->second.requestId);
			}
			FinishCall(key, false, u"TIMEOUT"_q);
		});
	}
	return jsId;
}

void RecomputeWanted() {
	auto &state = GetState();
	state.wanted.clear();
	state.wantAll = false;
	for (const auto &[id, listener] : state.listeners) {
		state.wantAll |= listener.all;
		for (const auto type : listener.ids) {
			state.wanted.emplace(type);
		}
	}
	details::RawUpdatesWanted = state.wantAll || !state.wanted.empty();
	if (!details::RawUpdatesWanted) {
		state.queue.clear();
	}
}

[[nodiscard]] QByteArray ShortToNewMessage(
		const QByteArray &json,
		PeerId self) {
	const auto object = Tl::ParseJson(json).toObject();
	const auto chat = (object.value(u"_"_q).toString()
		== u"updateShortChatMessage"_q);
	auto message = QJsonObject{ { u"_"_q, u"message"_q } };
	const auto copied = {
		"out", "mentioned", "media_unread", "silent", "id", "fwd_from",
		"via_bot_id", "reply_to", "date", "message", "entities", "ttl_period",
	};
	for (const auto key : copied) {
		const auto name = QString::fromLatin1(key);
		if (object.contains(name)) {
			message.insert(name, object.value(name));
		}
	}
	const auto peerUser = [](const QJsonValue &id) {
		return QJsonObject{ { u"_"_q, u"peerUser"_q }, { u"user_id"_q, id } };
	};
	if (chat) {
		message.insert(u"from_id"_q, peerUser(object.value(u"from_id"_q)));
		message.insert(u"peer_id"_q, QJsonObject{
			{ u"_"_q, u"peerChat"_q },
			{ u"chat_id"_q, object.value(u"chat_id"_q) },
		});
	} else {
		const auto user = object.value(u"user_id"_q);
		const auto out = object.value(u"out"_q).toBool();
		message.insert(u"from_id"_q, out
			? peerUser(QString::number(peerToUser(self).bare))
			: peerUser(user));
		message.insert(u"peer_id"_q, peerUser(user));
	}
	message.insert(u"media"_q, QJsonObject{
		{ u"_"_q, u"messageMediaEmpty"_q },
	});
	return QJsonDocument(QJsonObject{
		{ u"_"_q, u"updateNewMessage"_q },
		{ u"message"_q, message },
		{ u"pts"_q, object.value(u"pts"_q) },
		{ u"pts_count"_q, object.value(u"pts_count"_q) },
	}).toJson(QJsonDocument::Compact);
}

void FlushUpdates() {
	auto &state = GetState();
	state.flushScheduled = false;
	auto queue = base::take(state.queue);
	if (queue.empty() || !Manager::Exists() || state.listeners.empty()) {
		return;
	}
	auto decoded = std::vector<QByteArray>(queue.size());
	const auto json = [&](int index) -> const QByteArray & {
		auto &result = decoded[index];
		if (result.isEmpty()) {
			const auto &entry = queue[index];
			const auto parsed = Tl::Decode(
				Schema(),
				entry.data,
				Tl::ParseType(entry.shortMessage ? u"Updates"_q : u"Update"_q),
				BaseDecodeOptions());
			if (parsed.ok()) {
				result = entry.shortMessage
					? ShortToNewMessage(parsed.json, entry.self)
					: parsed.json;
			} else {
				LOG(("DamZaGram Plugins: could not decode update 0x%1: %2"
					).arg(Tl::IdToHex(queue[index].type), parsed.error));
				result = "null";
			}
		}
		return result;
	};
	const auto listeners = state.listeners;
	for (const auto &[pluginId, listener] : listeners) {
		auto batch = QByteArray();
		auto count = 0;
		auto self = PeerId();
		const auto send = [&] {
			if (batch.isEmpty()) {
				return;
			}
			batch.append(']');
			if (const auto instance = Manager::Get().instance(pluginId)) {
				instance->dispatch(u"api:updates"_q, QVariantMap{
					{ u"json"_q, QString::fromUtf8(batch) },
					{ u"self"_q, PeerIdToString(self) },
				});
			}
			batch.clear();
			count = 0;
		};
		for (auto i = 0; i != int(queue.size()); ++i) {
			const auto &entry = queue[i];
			if (!listener.all && !listener.ids.contains(entry.type)) {
				continue;
			} else if (entry.self != self) {
				send();
				self = entry.self;
			}
			const auto &text = json(i);
			if (text == "null") {
				continue;
			}
			batch.append(batch.isEmpty() ? '[' : ',');
			batch.append(text);
			if (++count >= kMaxUpdatesPerDispatch) {
				send();
			}
		}
		send();
	}
}

void SetUpdateTypes(const QString &pluginId, const QVariantList &types) {
	auto listener = UpdateListener();
	for (const auto &type : types) {
		const auto name = type.toString();
		if (name == u"*"_q) {
			listener.all = true;
		} else if (const auto constructor = Schema().constructor(name)) {
			listener.ids.emplace(mtpTypeId(constructor->id));
		}
	}
	auto &state = GetState();
	if (listener.all || !listener.ids.empty()) {
		state.listeners[pluginId] = std::move(listener);
	} else {
		state.listeners.erase(pluginId);
	}
	RecomputeWanted();
}

[[nodiscard]] QVariant DescribeCombinator(const Tl::Combinator &combinator) {
	auto params = QVariantList();
	for (const auto &param : combinator.params) {
		params.push_back(QVariantMap{
			{ u"name"_q, param.name },
			{ u"type"_q, param.typeText },
			{ u"optional"_q, param.optional() },
		});
	}
	return QVariantMap{
		{ u"kind"_q, combinator.method ? u"method"_q : u"constructor"_q },
		{ u"name"_q, combinator.name },
		{ u"id"_q, double(combinator.id) },
		{ u"idHex"_q, Tl::IdToHex(combinator.id) },
		{ u"params"_q, params },
		{ u"type"_q, combinator.typeText },
	};
}

[[nodiscard]] const Tl::Combinator *FindCombinator(const QString &name) {
	const auto &schema = Schema();
	if (const auto method = schema.method(name)) {
		return method;
	} else if (const auto constructor = schema.constructor(name)) {
		return constructor;
	}
	auto ok = false;
	const auto hex = name.startsWith(u"0x"_q, Qt::CaseInsensitive);
	const auto id = hex ? name.mid(2).toUInt(&ok, 16) : name.toUInt(&ok);
	if (!ok) {
		return nullptr;
	} else if (const auto constructor = schema.constructor(quint32(id))) {
		return constructor;
	}
	return schema.method(quint32(id));
}

[[nodiscard]] std::optional<QJsonValue> ParseParams(const QString &json) {
	if (json.isEmpty()) {
		return QJsonValue(QJsonObject());
	}
	const auto value = Tl::ParseJson(json.toUtf8());
	if (value.isUndefined()) {
		return std::nullopt;
	}
	return value;
}

} // namespace

namespace details {

void FeedRawUpdateSlow(
		not_null<Main::Session*> session,
		const MTPUpdate &update) {
	auto &state = GetState();
	const auto type = mtpTypeId(update.type());
	if (!state.wantAll && !state.wanted.contains(type)) {
		return;
	}
	auto buffer = mtpBuffer();
	update.write(buffer);
	state.queue.push_back({
		.self = session->userPeerId(),
		.type = type,
		.data = std::move(buffer),
	});
	if (!state.flushScheduled) {
		state.flushScheduled = true;
		crl::on_main([] { FlushUpdates(); });
	}
}

void FeedRawUpdatesSlow(
		not_null<Main::Session*> session,
		const MTPUpdates &updates) {
	const auto type = mtpTypeId(updates.type());
	if (type != mtpc_updateShortMessage
		&& type != mtpc_updateShortChatMessage) {
		return;
	}
	auto &state = GetState();
	if (!state.wantAll && !state.wanted.contains(mtpc_updateNewMessage)) {
		return;
	}
	auto buffer = mtpBuffer();
	updates.write(buffer);
	state.queue.push_back({
		.self = session->userPeerId(),
		.type = mtpc_updateNewMessage,
		.data = std::move(buffer),
		.shortMessage = true,
	});
	if (!state.flushScheduled) {
		state.flushScheduled = true;
		crl::on_main([] { FlushUpdates(); });
	}
}

} // namespace details

DZG_API_MODULE({
	.name = "api",
	.natives = [](not_null<Bridge*> bridge, std::vector<Js::NativeMethod> &out) {
		using Args = QVariantList;
		out.push_back({ "api_call", [=](const Args &args) -> QVariant {
			const auto method = args.value(0).toString();
			const auto params = ParseParams(args.value(1).toString());
			const auto options = args.value(2).toMap();
			const auto session = Manager::Get().activeSession();
			if (!session) {
				return u"No active account"_q;
			} else if (!params || !params->isObject()) {
				return method + u": params must be an object"_q;
			} else if (IsRefusedMethod(method)) {
				return method + u": not available to plugins"_q;
			}
			const auto dc = options.value(u"dc"_q).toInt();
			if (dc > 0 && !IsKnownDc(session, dc)) {
				return method + u": unknown dc %1"_q.arg(dc);
			}
			auto file = IsFileMethod(method);
			auto encodeOptions = Tl::EncodeOptions();
			encodeOptions.resolve = [=](
					const QString &type,
					const QJsonValue &value) {
				return ResolveInput(session, type, value);
			};
			encodeOptions.check = [&](
					const Tl::Combinator &called,
					const QJsonObject &params) {
				file = file || IsFileMethod(called.name);
				return RefuseReason(called, params);
			};
			const auto encoded = Tl::EncodeMethod(
				Schema(),
				method,
				params->toObject(),
				encodeOptions);
			if (!encoded.ok()) {
				return method + u": "_q + encoded.error;
			}
			const auto dcId = (dc > 0)
				? MTP::downloadDcId(dc, 0)
				: file
				? MTP::downloadDcId(session->mtp().mainDcId(), 0)
				: MTP::ShiftedDcId(0);
			return StartCall(bridge, session, encoded, options, dcId);
		} });
		out.push_back({ "api_input", [](const Args &args) -> QVariant {
			const auto kind = InputKindFromType(args.value(0).toString());
			const auto id = args.value(1).toString();
			if (!kind || id.isEmpty()) {
				return QVariant();
			} else if (IsSelfAlias(id)) {
				const auto json = SelfJson(*kind);
				return json.isEmpty() ? QVariant() : QVariant(QString::fromUtf8(json));
			}
			const auto session = Manager::Get().activeSession();
			const auto peer = ResolvePeer(session, id);
			if (!peer) {
				return QVariant();
			}
			const auto json = InputJson(peer, *kind);
			return json.isEmpty() ? QVariant() : QVariant(QString::fromUtf8(json));
		} });
		out.push_back({ "api_schema", [](const Args &args) -> QVariant {
			const auto combinator = FindCombinator(args.value(0).toString());
			return combinator ? DescribeCombinator(*combinator) : QVariant();
		} });
		out.push_back({ "api_constructors", [](const Args &args) -> QVariant {
			auto result = QStringList();
			for (const auto c : Schema().constructorsOf(args.value(0).toString())) {
				result.push_back(c->name);
			}
			return result;
		} });
		out.push_back({ "api_search", [](const Args &args) -> QVariant {
			return Schema().search(
				args.value(0).toString(),
				std::max(args.value(1).toInt(), 1));
		} });
		out.push_back({ "api_layer", [](const Args &) -> QVariant {
			return int(MTP::details::kCurrentLayer);
		} });
		out.push_back({ "api_encode", [](const Args &args) -> QVariant {
			const auto value = ParseParams(args.value(0).toString());
			if (!value) {
				return QVariantMap{ { u"error"_q, u"bad JSON"_q } };
			}
			const auto type = args.value(1).toString();
			const auto session = Manager::Get().activeSession();
			auto options = Tl::EncodeOptions();
			options.resolve = [=](
					const QString &type,
					const QJsonValue &value) {
				return ResolveInput(session, type, value);
			};
			const auto encoded = Tl::EncodeValue(
				Schema(),
				*value,
				Tl::ParseType(type),
				options);
			if (!encoded.ok()) {
				return QVariantMap{ { u"error"_q, encoded.error } };
			}
			const auto bytes = QByteArray(
				reinterpret_cast<const char*>(encoded.data.constData()),
				encoded.data.size() * sizeof(Tl::Prime));
			return QVariantMap{
				{ u"data"_q, QString::fromLatin1(bytes.toBase64()) },
			};
		} });
		out.push_back({ "api_decode", [](const Args &args) -> QVariant {
			const auto bytes = QByteArray::fromBase64(
				args.value(0).toString().toLatin1());
			if (bytes.isEmpty() || (bytes.size() % 4)) {
				return QVariantMap{
					{ u"error"_q, u"data size must be a multiple of 4"_q },
				};
			}
			auto buffer = Tl::Buffer(int(bytes.size() / 4));
			memcpy(buffer.data(), bytes.constData(), bytes.size());
			const auto decoded = Tl::Decode(
				Schema(),
				buffer,
				Tl::ParseType(args.value(1).toString()),
				BaseDecodeOptions());
			if (!decoded.ok()) {
				return QVariantMap{ { u"error"_q, decoded.error } };
			}
			return QVariantMap{
				{ u"json"_q, QString::fromUtf8(decoded.json) },
				{ u"size"_q, decoded.size * int(sizeof(Tl::Prime)) },
			};
		} });
		out.push_back({ "api_setUpdateTypes", [=](const Args &args) -> QVariant {
			const auto instance = bridge->instance();
			const auto &id = instance->manifest().id;
			// dzg.on('unload', off) runs after pluginUnloaded() dropped the
			// listener: don't bring it back for an unloading plugin.
			if (Manager::Exists()
				&& Manager::Get().instance(id) == instance.get()) {
				SetUpdateTypes(id, args.value(0).toList());
			}
			return QVariant();
		} });
	},
	.prelude = [] {
		return QString::fromUtf8(R"DZG_JS(
	const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
	const utf8 = (text) => {
		const out = [];
		for (const ch of String(text)) {
			const c = ch.codePointAt(0);
			if (c < 0x80) {
				out.push(c);
			} else if (c < 0x800) {
				out.push(0xC0 | (c >> 6), 0x80 | (c & 63));
			} else if (c < 0x10000) {
				out.push(0xE0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
			} else {
				out.push(0xF0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
			}
		}
		return out;
	};
	const fromUtf8 = (bytes) => {
		let result = '';
		for (let i = 0; i < bytes.length;) {
			const a = bytes[i++];
			const extra = (a >= 0xF0) ? 3 : (a >= 0xE0) ? 2 : (a >= 0xC0) ? 1 : 0;
			let c = (a >= 0x80 && !extra) ? 0xFFFD : (a & (0x7F >> extra));
			for (let k = 0; k < extra; ++k) {
				c = (c << 6) | ((i < bytes.length) ? (bytes[i++] & 63) : 0);
			}
			result += String.fromCodePoint(c > 0x10FFFF ? 0xFFFD : c);
		}
		return result;
	};
	const toBytes = (data) => {
		if (typeof data === 'string') return utf8(data);
		if (data instanceof ArrayBuffer) return new Uint8Array(data);
		if (ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
		if (Array.isArray(data)) return data.map((x) => Number(x) & 255);
		throw new TypeError('Expected a string, Uint8Array, ArrayBuffer or array of bytes');
	};
	const encode64 = (bytes) => {
		let s = '';
		for (let i = 0; i < bytes.length; i += 3) {
			const n = (bytes[i] << 16) | ((bytes[i + 1] || 0) << 8) | (bytes[i + 2] || 0);
			s += B64[(n >> 18) & 63] + B64[(n >> 12) & 63]
				+ (i + 1 < bytes.length ? B64[(n >> 6) & 63] : '=')
				+ (i + 2 < bytes.length ? B64[n & 63] : '=');
		}
		return s;
	};
	const decode64 = (text) => {
		const clean = String(text).replace(/-/g, '+').replace(/_/g, '/').replace(/[^A-Za-z0-9+/]/g, '');
		const out = new Uint8Array(Math.floor(clean.length * 3 / 4));
		let bits = 0;
		let value = 0;
		let index = 0;
		for (const ch of clean) {
			value = (value << 6) | B64.indexOf(ch);
			bits += 6;
			if (bits >= 8) {
				bits -= 8;
				out[index++] = (value >> bits) & 255;
			}
		}
		return out.subarray(0, index);
	};
	const replacer = (key, value) => {
		if (typeof value === 'bigint') return value.toString();
		if (value instanceof ArrayBuffer || ArrayBuffer.isView(value)) return encode64(toBytes(value));
		return value;
	};
	const toJson = (value) => JSON.stringify((value === undefined) ? {} : value, replacer);
	// Options go to C++ as int: no NaN, Infinity or out of range values.
	const toInt = (value) => Math.min(Math.max(0, Math.ceil(Number(value) || 0)), 0x7FFFFFFF);

	const makeError = (text) => {
		const error = new Error(String(text));
		const match = /^(-?\d+): ([A-Za-z0-9_]+)$/.exec(String(text));
		if (match) {
			error.code = Number(match[1]);
			error.type = match[2];
			const wait = /_WAIT_(\d+)$/.exec(match[2]);
			if (wait) error.seconds = Number(wait[1]);
		} else if (text === 'TIMEOUT') {
			error.type = 'TIMEOUT';
		}
		return error;
	};

	const peerKinds = {
		peerUser: ['user_id', 0n], peerChat: ['chat_id', 1n], peerChannel: ['channel_id', 2n],
		inputPeerUser: ['user_id', 0n], inputPeerChat: ['chat_id', 1n], inputPeerChannel: ['channel_id', 2n],
		inputUser: ['user_id', 0n], inputChannel: ['channel_id', 2n],
		user: ['id', 0n], userEmpty: ['id', 0n],
		chat: ['id', 1n], chatEmpty: ['id', 1n], chatForbidden: ['id', 1n],
		channel: ['id', 2n], channelForbidden: ['id', 2n],
	};

	const updateHandlers = new Map();
	const syncUpdateTypes = () => native.api_setUpdateTypes(Array.from(updateHandlers.keys()));
	const checkUpdateType = (type) => {
		if (type === '*') return;
		const info = native.api_schema(type);
		if (!info || info.kind !== 'constructor') {
			throw new TypeError('Unknown update type: ' + type);
		} else if (info.type !== 'Update') {
			throw new TypeError(type + ' is ' + info.type + ', not Update; dzg.api.onUpdate gets single Update objects');
		}
	};

	const api = {
		call(method, params, options) {
			const o = options || {};
			let failure = null;
			const promise = internal.request(() => {
				const result = native.api_call(String(method), toJson(params || {}), {
					timeout: toInt(o.timeout),
					floodSleep: (o.floodSleep === undefined) ? 10 : toInt(o.floodSleep),
					applyResult: o.applyResult !== false,
					dc: toInt(o.dc),
				});
				if (typeof result === 'number' && result > 0) return result;
				failure = String(result || 'Request was not started');
				return 0;
			});
			if (failure !== null) {
				promise.catch(() => {});
				return Promise.reject(makeError(failure));
			}
			return promise.then((text) => JSON.parse(text), (e) => {
				throw makeError((e && e.message) ? e.message : e);
			});
		},
		inputPeer(peer) {
			const json = native.api_input('InputPeer', internal.toPeerId(peer));
			return json ? JSON.parse(json) : null;
		},
		inputUser(peer) {
			const json = native.api_input('InputUser', internal.toPeerId(peer));
			return json ? JSON.parse(json) : null;
		},
		inputChannel(peer) {
			const json = native.api_input('InputChannel', internal.toPeerId(peer));
			return json ? JSON.parse(json) : null;
		},
		peerId(peer) {
			const kind = peer && peerKinds[peer._];
			if (!kind) return null;
			try {
				return (BigInt(peer[kind[0]]) | (kind[1] << 48n)).toString();
			} catch (e) {
				return null;
			}
		},
		onUpdate(type, fn) {
			if (typeof fn !== 'function') throw new TypeError('dzg.api.onUpdate: callback must be a function');
			const types = (Array.isArray(type) ? type : [type]).map((t) => String(t || '*'));
			types.forEach(checkUpdateType);
			let changed = false;
			for (const t of types) {
				if (!updateHandlers.has(t)) {
					updateHandlers.set(t, []);
					changed = true;
				}
				updateHandlers.get(t).push(fn);
			}
			if (changed) syncUpdateTypes();
			let active = true;
			return () => {
				if (!active) return;
				active = false;
				let removed = false;
				for (const t of types) {
					const list = updateHandlers.get(t);
					if (!list) continue;
					const index = list.indexOf(fn);
					if (index >= 0) list.splice(index, 1);
					if (!list.length) {
						updateHandlers.delete(t);
						removed = true;
					}
				}
				if (removed) syncUpdateTypes();
			};
		},
		schema(name) {
			return native.api_schema(String(name)) || null;
		},
		constructors(type) {
			return native.api_constructors(String(type)) || [];
		},
		search(query, limit) {
			return native.api_search(String(query || ''), Number(limit) || 50) || [];
		},
		encode(value, type) {
			const r = native.api_encode(toJson(value), String(type || 'Object')) || {};
			if (typeof r.data !== 'string') throw makeError(r.error || 'Encoding failed');
			return r.data;
		},
		decode(data, type) {
			const base64 = (typeof data === 'string') ? data : encode64(toBytes(data));
			const r = native.api_decode(base64, String(type || 'Object')) || {};
			if (typeof r.json !== 'string') throw makeError(r.error || 'Decoding failed');
			return JSON.parse(r.json);
		},
		toBase64(data) {
			return encode64(toBytes(data));
		},
		fromBase64(text, as) {
			const bytes = decode64(text);
			return (as === 'text') ? fromUtf8(bytes) : bytes;
		},
	};
	Object.defineProperty(api, 'layer', {
		enumerable: true,
		get() { return native.api_layer(); },
	});
	dzg.api = api;

	internal.on('api:updates', (payload) => {
		let list;
		try {
			list = JSON.parse(payload.json);
		} catch (e) {
			internal.report('api.onUpdate', e);
			return;
		}
		const meta = { self: payload.self };
		for (const update of list) {
			if (!update) continue;
			const exact = updateHandlers.get(update._);
			if (exact) {
				for (const fn of exact.slice()) internal.safe('api.onUpdate(' + update._ + ')', fn, update, meta);
			}
			const any = updateHandlers.get('*');
			if (any) {
				for (const fn of any.slice()) internal.safe('api.onUpdate(*)', fn, update, meta);
			}
		}
	});
)DZG_JS");
	},
	.pluginUnloaded = [](const QString &pluginId) {
		auto &state = GetState();
		for (auto i = begin(state.calls); i != end(state.calls);) {
			if (i->second.pluginId == pluginId) {
				if (const auto instance = i->second.mtp.data()) {
					instance->cancel(i->second.requestId);
				}
				i = state.calls.erase(i);
			} else {
				++i;
			}
		}
		if (state.listeners.erase(pluginId)) {
			RecomputeWanted();
		}
	},
});

} // namespace Damza::Plugins
