// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/plugins/plugin_data.h"

#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_message_reaction_id.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_reply_markup.h"
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

[[nodiscard]] QString ButtonTypeName(HistoryMessageMarkupButton::Type type) {
	using Type = HistoryMessageMarkupButton::Type;
	switch (type) {
	case Type::Default: return u"text"_q;
	case Type::Url: return u"url"_q;
	case Type::Callback: return u"callback"_q;
	case Type::CallbackWithPassword: return u"callbackWithPassword"_q;
	case Type::RequestPhone: return u"requestPhone"_q;
	case Type::RequestLocation: return u"requestLocation"_q;
	case Type::RequestPoll: return u"requestPoll"_q;
	case Type::RequestPeer: return u"requestPeer"_q;
	case Type::SwitchInline: return u"switchInline"_q;
	case Type::SwitchInlineSame: return u"switchInlineSame"_q;
	case Type::Game: return u"game"_q;
	case Type::Buy: return u"buy"_q;
	case Type::Auth: return u"auth"_q;
	case Type::UserProfile: return u"userProfile"_q;
	case Type::WebView: return u"webView"_q;
	case Type::SimpleWebView: return u"simpleWebView"_q;
	case Type::CopyText: return u"copyText"_q;
	case Type::Disabled: return u"disabled"_q;
	default: break;
	}
	return u"other"_q;
}

[[nodiscard]] QVariantList SerializeButtons(not_null<HistoryItem*> item) {
	using Type = HistoryMessageMarkupButton::Type;
	auto result = QVariantList();
	const auto markup = item->Get<HistoryMessageReplyMarkup>();
	if (!markup) {
		return result;
	}
	for (const auto &row : markup->data.rows) {
		auto list = QVariantList();
		for (const auto &button : row) {
			const auto type = button.type;
			auto entry = QVariantMap{
				{ u"text"_q, button.text },
				{ u"type"_q, (type == Type::Url)
					? u"url"_q
					: (type == Type::Callback)
					? u"callback"_q
					: u"other"_q },
				{ u"kind"_q, ButtonTypeName(type) },
			};
			if (type == Type::Url
				|| type == Type::WebView
				|| type == Type::SimpleWebView
				|| type == Type::Auth) {
				entry.insert(u"url"_q, QString::fromUtf8(button.data));
			} else if (type == Type::Callback) {
				entry.insert(u"data"_q, QString::fromUtf8(button.data));
			} else if (type == Type::CopyText) {
				entry.insert(u"copyText"_q, QString::fromUtf8(button.data));
			}
			list.push_back(entry);
		}
		result.push_back(list);
	}
	return result;
}

[[nodiscard]] QVariantMap SerializeMediaInfo(not_null<Data::Media*> media) {
	auto result = QVariantMap();
	if (const auto document = media->document()) {
		const auto name = document->filename();
		if (!name.isEmpty()) {
			result.insert(u"fileName"_q, name);
		}
		const auto mime = document->mimeString();
		if (!mime.isEmpty()) {
			result.insert(u"mime"_q, mime);
		}
		if (document->size > 0) {
			result.insert(u"size"_q, double(document->size));
		}
		if (document->hasDuration()) {
			result.insert(u"duration"_q, document->duration() / 1000.);
		}
		const auto size = document->dimensions;
		if (size.width() > 0 && size.height() > 0) {
			result.insert(u"width"_q, size.width());
			result.insert(u"height"_q, size.height());
		}
		result.insert(u"documentId"_q, QString::number(document->id));
	} else if (const auto photo = media->photo()) {
		result.insert(u"mime"_q, u"image/jpeg"_q);
		if (photo->width() > 0 && photo->height() > 0) {
			result.insert(u"width"_q, photo->width());
			result.insert(u"height"_q, photo->height());
		}
		const auto bytes = photo->imageByteSize(Data::PhotoSize::Large);
		if (bytes > 0) {
			result.insert(u"size"_q, double(bytes));
		}
		result.insert(u"photoId"_q, QString::number(photo->id));
	}
	if (media->hasSpoiler()) {
		result.insert(u"spoiler"_q, true);
	}
	return result;
}

[[nodiscard]] QVariant SerializeForward(not_null<HistoryItem*> item) {
	const auto forwarded = item->Get<HistoryMessageForwarded>();
	if (!forwarded) {
		return QVariant();
	}
	auto result = QVariantMap{
		{ u"date"_q, double(forwarded->originalDate) },
	};
	if (const auto sender = forwarded->originalSender) {
		result.insert(u"name"_q, sender->name());
		result.insert(u"peerId"_q, PeerIdToString(sender->id));
	} else if (const auto &hidden = forwarded->originalHiddenSenderInfo) {
		result.insert(u"name"_q, hidden->name);
	} else {
		result.insert(u"name"_q, QString());
	}
	if (forwarded->originalId) {
		result.insert(u"messageId"_q, double(forwarded->originalId.bare));
	}
	if (!forwarded->originalPostAuthor.isEmpty()) {
		result.insert(u"postAuthor"_q, forwarded->originalPostAuthor);
	}
	if (forwarded->imported) {
		result.insert(u"imported"_q, true);
	}
	return result;
}

[[nodiscard]] QVariantList SerializeReactions(not_null<HistoryItem*> item) {
	auto result = QVariantList();
	for (const auto &reaction : item->reactions()) {
		auto entry = QVariantMap{
			{ u"emoji"_q, SerializeReactionId(reaction.id) },
			{ u"count"_q, reaction.count },
			{ u"chosen"_q, reaction.my },
		};
		if (const auto custom = reaction.id.custom()) {
			entry.insert(u"customEmojiId"_q, QString::number(custom));
		} else if (reaction.id.paid()) {
			entry.insert(u"paid"_q, true);
		}
		result.push_back(entry);
	}
	return result;
}

} // namespace

QString EntityTypeName(EntityType type) {
	switch (type) {
	case EntityType::Url: return u"url"_q;
	case EntityType::CustomUrl: return u"textLink"_q;
	case EntityType::Email: return u"email"_q;
	case EntityType::Hashtag: return u"hashtag"_q;
	case EntityType::Cashtag: return u"cashtag"_q;
	case EntityType::Mention: return u"mention"_q;
	case EntityType::MentionName: return u"textMention"_q;
	case EntityType::CustomEmoji: return u"customEmoji"_q;
	case EntityType::BotCommand: return u"botCommand"_q;
	case EntityType::MediaTimestamp: return u"mediaTimestamp"_q;
	case EntityType::Phone: return u"phone"_q;
	case EntityType::BankCard: return u"bankCard"_q;
	case EntityType::Bold:
	case EntityType::Semibold: return u"bold"_q;
	case EntityType::Italic: return u"italic"_q;
	case EntityType::Underline: return u"underline"_q;
	case EntityType::StrikeOut: return u"strikethrough"_q;
	case EntityType::Code: return u"code"_q;
	case EntityType::Pre: return u"pre"_q;
	case EntityType::Blockquote: return u"blockquote"_q;
	case EntityType::Spoiler: return u"spoiler"_q;
	case EntityType::Subscript: return u"subscript"_q;
	case EntityType::Superscript: return u"superscript"_q;
	case EntityType::Marked: return u"marked"_q;
	case EntityType::FormattedDate: return u"formattedDate"_q;
	case EntityType::Colorized:
	case EntityType::Invalid: break;
	}
	return QString();
}

QVariantList SerializeEntities(const EntitiesInText &entities) {
	auto result = QVariantList();
	result.reserve(entities.size());
	for (const auto &entity : entities) {
		const auto type = EntityTypeName(entity.type());
		if (type.isEmpty() || entity.length() <= 0) {
			continue;
		}
		auto map = QVariantMap{
			{ u"type"_q, type },
			{ u"offset"_q, entity.offset() },
			{ u"length"_q, entity.length() },
		};
		const auto data = entity.data();
		switch (entity.type()) {
		case EntityType::CustomUrl:
			map.insert(u"url"_q, data);
			break;
		case EntityType::MentionName: {
			const auto fields = TextUtilities::MentionNameDataToFields(data);
			if (fields.userId) {
				map.insert(
					u"userId"_q,
					PeerIdToString(peerFromUser(UserId(fields.userId))));
			}
		} break;
		case EntityType::Pre:
			if (!data.isEmpty()) {
				map.insert(u"language"_q, data);
			}
			break;
		case EntityType::CustomEmoji:
			map.insert(u"documentId"_q, data);
			break;
		case EntityType::Blockquote:
			if (!data.isEmpty()) {
				map.insert(u"collapsed"_q, true);
			}
			break;
		default:
			break;
		}
		result.push_back(map);
	}
	return result;
}

QString SerializeReactionId(const Data::ReactionId &id) {
	if (const auto custom = id.custom()) {
		return QString::number(custom);
	} else if (id.paid()) {
		return u"paid"_q;
	}
	return id.emoji();
}

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
	if (!id || !(std::abs(msgId) < 9e18)) { // NaN and huge ids from JS
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
	const auto &text = item->originalText();
	const auto edited = item->Get<HistoryMessageEdited>();
	const auto groupId = item->groupId();
	auto result = QVariantMap{
		{ u"id"_q, double(item->id.bare) },
		{ u"peerId"_q, PeerIdToString(peer->id) },
		{ u"chat"_q, SerializePeer(peer) },
		{ u"fromId"_q, PeerIdToString(from->id) },
		{ u"fromName"_q, from->name() },
		{ u"text"_q, text.text },
		{ u"entities"_q, SerializeEntities(text.entities) },
		{ u"editDate"_q, double(edited ? edited->date : TimeId(0)) },
		{ u"groupedId"_q, groupId.value
			? QString::number(groupId.value)
			: QString() },
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
		if (auto info = SerializeMediaInfo(item->media()); !info.isEmpty()) {
			result.insert(u"mediaInfo"_q, std::move(info));
		}
	}
	if (auto forward = SerializeForward(item); forward.isValid()) {
		result.insert(u"forwardFrom"_q, std::move(forward));
	}
	if (auto buttons = SerializeButtons(item); !buttons.isEmpty()) {
		result.insert(u"buttons"_q, std::move(buttons));
	}
	if (auto reactions = SerializeReactions(item); !reactions.isEmpty()) {
		result.insert(u"reactions"_q, std::move(reactions));
	}
	if (item->history()->isForum()) {
		result.insert(u"topicId"_q, double(item->topicRootId().bare));
	}
	return result;
}

} // namespace Damza::Plugins
