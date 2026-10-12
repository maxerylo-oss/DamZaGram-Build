// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/plugins/plugin_data.h"

#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace Damza::Plugins {
namespace {

[[nodiscard]] QString PeerType(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return user->isBot() ? u"bot"_q : u"user"_q;
	} else if (peer->isChat() || peer->isMegagroup()) {
		return u"group"_q;
	} else if (peer->isChannel()) {
		return u"channel"_q;
	}
	return u"unknown"_q;
}

[[nodiscard]] QString MediaType(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return QString();
	} else if (media->photo()) {
		return u"photo"_q;
	} else if (media->poll()) {
		return u"poll"_q;
	} else if (const auto document = media->document()) {
		if (document->sticker()) {
			return u"sticker"_q;
		} else if (document->isVoiceMessage()) {
			return u"voice"_q;
		} else if (document->isVideoMessage()) {
			return u"videoNote"_q;
		} else if (document->isAnimation()) {
			return u"gif"_q;
		} else if (document->isVideoFile()) {
			return u"video"_q;
		} else if (document->isAudioFile()) {
			return u"audio"_q;
		}
		return u"file"_q;
	}
	return u"other"_q;
}

} // namespace

QString PeerIdToString(PeerId id) {
	return QString::number(id.value);
}

PeerId PeerIdFromString(const QString &value) {
	auto ok = false;
	const auto parsed = value.toULongLong(&ok);
	return ok ? PeerId(PeerIdHelper(parsed)) : PeerId();
}

PeerData *ResolvePeer(Main::Session *session, const QString &peerId) {
	if (!session) {
		return nullptr;
	}
	const auto id = PeerIdFromString(peerId);
	return id ? session->data().peerLoaded(id) : nullptr;
}

HistoryItem *ResolveMessage(
		Main::Session *session,
		const QString &peerId,
		double msgId) {
	if (!session) {
		return nullptr;
	}
	const auto id = PeerIdFromString(peerId);
	if (!id) {
		return nullptr;
	}
	return session->data().message(id, MsgId(int64(msgId)));
}

QVariantMap SerializePeer(not_null<PeerData*> peer) {
	auto result = QVariantMap{
		{ u"id"_q, PeerIdToString(peer->id) },
		{ u"tgId"_q, double(peer->id.value & PeerId::kChatTypeMask) },
		{ u"name"_q, peer->name() },
		{ u"username"_q, peer->username() },
		{ u"type"_q, PeerType(peer) },
		{ u"isSelf"_q, peer->isSelf() },
	};
	return result;
}

QVariantMap SerializeMessage(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	const auto from = item->from();
	auto result = QVariantMap{
		{ u"id"_q, double(item->id.bare) },
		{ u"peerId"_q, PeerIdToString(peer->id) },
		{ u"chat"_q, SerializePeer(peer) },
		{ u"fromId"_q, PeerIdToString(from->id) },
		{ u"fromName"_q, from->name() },
		{ u"text"_q, item->originalText().text },
		{ u"date"_q, double(item->date()) },
		{ u"out"_q, item->out() },
		{ u"service"_q, item->isService() },
		{ u"post"_q, item->isPost() },
		{ u"pinned"_q, item->isPinned() },
		{ u"deleted"_q, item->isDeleted() },
		{ u"replyToId"_q, double(item->replyToId().bare) },
		{ u"views"_q, item->viewsCount() },
	};
	const auto media = MediaType(item);
	if (!media.isEmpty()) {
		result.insert(u"media"_q, media);
	}
	return result;
}

} // namespace Damza::Plugins
