// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
//
// Module "messages": formatted sending, server history and search,
// forwarding, reactions, pins, files, links and inline buttons.
// See docs/plugins/api/messages.md.
#include "damza/plugins/plugin_api.h"

#include "api/api_bot.h"
#include "api/api_common.h"
#include "api/api_editing.h"
#include "apiwrap.h"
#include "ayu/features/forward/ayu_forward.h"
#include "base/base_file_utilities.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "damza/plugins/plugin_bridge.h"
#include "damza/plugins/plugin_data.h"
#include "damza/plugins/plugin_instance.h"
#include "damza/plugins/plugin_manager.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat_participant_status.h"
#include "data/data_document.h"
#include "data/data_drafts.h"
#include "data/data_file_origin.h"
#include "data/data_histories.h"
#include "data/data_media_types.h"
#include "data/data_message_reaction_id.h"
#include "data/data_message_reactions.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_reply_markup.h"
#include "main/main_session.h"
#include "storage/localimageloader.h"
#include "storage/storage_account.h"
#include "storage/storage_media_prepare.h"
#include "styles/style_boxes.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "ui/item_text_options.h"
#include "ui/basic_click_handlers.h"
#include "window/window_session_controller.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtCore/QTimer>

namespace Damza::Plugins {
namespace {

using Args = QVariantList;

constexpr auto kMaxPage = 100;
constexpr auto kForwardTimeout = 120 * crl::time(1000);

// A promise created in JS with internal.request(). Settles at most once,
// always on a later event loop iteration: JS registers the promise only
// after the native call has returned its id.
class Promise final {
public:
	explicit Promise(not_null<Bridge*> bridge)
	: _bridge(bridge.get())
	, _id(bridge->nextRequestId())
	, _done(std::make_shared<bool>(false)) {
	}

	[[nodiscard]] int id() const {
		return _id;
	}
	[[nodiscard]] bool done() const {
		return *_done || !_bridge;
	}
	void resolve(QVariant value = QVariant()) const {
		finish(true, std::move(value));
	}
	void reject(const QString &error) const {
		finish(false, error);
	}

private:
	void finish(bool ok, QVariant value) const {
		if (*_done || !_bridge) {
			return;
		}
		*_done = true;
		const auto bridge = _bridge;
		const auto id = _id;
		QTimer::singleShot(0, bridge.data(), [=] {
			if (bridge) {
				bridge->settle(id, ok, value);
			}
		});
	}

	QPointer<Bridge> _bridge;
	int _id = 0;
	std::shared_ptr<bool> _done;

};

// Subscriptions that wait for downloads, dropped when the plugin unloads.
struct Wait {
	QString pluginId;
	rpl::lifetime lifetime;
};

[[nodiscard]] std::map<int, Wait> &Waits() {
	static auto result = std::map<int, Wait>();
	return result;
}

[[nodiscard]] int StartWait(not_null<Bridge*> bridge) {
	static auto counter = 0;
	const auto id = ++counter;
	Waits()[id].pluginId = bridge->instance()->manifest().id;
	return id;
}

[[nodiscard]] rpl::lifetime &WaitLifetime(int id) {
	return Waits()[id].lifetime;
}

void FinishWait(int id) {
	// Called from inside the subscription: destroy it a bit later.
	crl::on_main([=] {
		Waits().erase(id);
	});
}

[[nodiscard]] Main::Session *ActiveSession() {
	return Manager::Get().activeSession();
}

[[nodiscard]] Window::SessionController *ControllerFor(
		not_null<Main::Session*> session) {
	const auto controller = Manager::Get().activeController();
	return (controller && &controller->session() == session)
		? controller
		: nullptr;
}

[[nodiscard]] TimeId ScheduleDate(const QVariantMap &options) {
	const auto date = TimeId(options.value(u"scheduleDate"_q).toLongLong());
	return (date > base::unixtime::now()) ? date : TimeId(0);
}

[[nodiscard]] Api::SendAction PrepareAction(
		not_null<History*> history,
		const QVariantMap &options) {
	auto sendOptions = Api::SendOptions();
	sendOptions.silent = options.value(u"silent"_q).toBool();
	sendOptions.scheduled = ScheduleDate(options);
	auto action = Api::SendAction(history, sendOptions);
	action.clearDraft = false;
	const auto peer = history->peer;
	if (const auto replyTo = options.value(u"replyTo"_q).toLongLong()) {
		action.replyTo.messageId = FullMsgId(peer->id, MsgId(replyTo));
	}
	if (const auto topicId = options.value(u"topicId"_q).toLongLong()) {
		action.replyTo.topicRootId = MsgId(topicId);
	}
	return action;
}

[[nodiscard]] QString MentionData(
		not_null<Main::Session*> session,
		const QString &userId) {
	auto ok = false;
	const auto bare = userId.toULongLong(&ok);
	const auto user = ok
		? session->data().userLoaded(UserId(bare))
		: nullptr;
	if (!user) {
		return QString();
	}
	return TextUtilities::MentionNameDataFromFields({
		.selfId = session->userId().bare,
		.userId = peerToUser(user->id).bare,
		.accessHash = user->accessHash(),
	});
}

// Entities from JS ({ type, offset, length, ... }) -> EntitiesInText,
// sorted the way ConvertEntitiesToTextTags expects them.
[[nodiscard]] EntitiesInText EntitiesFromJs(
		not_null<Main::Session*> session,
		int textLength,
		const QVariantList &list) {
	auto result = EntitiesInText();
	static const auto MentionUrl = QRegularExpression(
		u"^tg://user\\?id=(\\d+)$"_q,
		QRegularExpression::CaseInsensitiveOption);
	static const auto Digits = QRegularExpression(u"^\\d+$"_q);
	for (const auto &value : list) {
		const auto map = value.toMap();
		const auto type = map.value(u"type"_q).toString();
		const auto offset = map.value(u"offset"_q).toInt();
		const auto length = map.value(u"length"_q).toInt();
		if (offset < 0 || length <= 0 || int64(offset) + length > textLength) {
			continue;
		}
		const auto add = [&](EntityType type, const QString &data = {}) {
			result.push_back(EntityInText(type, offset, length, data));
		};
		const auto mention = [&](const QString &userId) {
			if (const auto data = MentionData(session, userId)
				; !data.isEmpty()) {
				add(EntityType::MentionName, data);
			}
		};
		if (type == u"bold"_q) {
			add(EntityType::Bold);
		} else if (type == u"italic"_q) {
			add(EntityType::Italic);
		} else if (type == u"underline"_q) {
			add(EntityType::Underline);
		} else if (type == u"strikethrough"_q || type == u"strike"_q) {
			add(EntityType::StrikeOut);
		} else if (type == u"spoiler"_q) {
			add(EntityType::Spoiler);
		} else if (type == u"code"_q) {
			add(EntityType::Code);
		} else if (type == u"pre"_q) {
			add(EntityType::Pre, map.value(u"language"_q).toString());
		} else if (type == u"blockquote"_q) {
			add(
				EntityType::Blockquote,
				map.value(u"collapsed"_q).toBool() ? u"1"_q : QString());
		} else if (type == u"textLink"_q
			|| type == u"textUrl"_q
			|| type == u"url"_q) {
			const auto url = map.value(u"url"_q).toString().trimmed();
			if (const auto match = MentionUrl.match(url); match.hasMatch()) {
				mention(match.captured(1));
			} else if (!url.isEmpty()) {
				add(EntityType::CustomUrl, url);
			}
		} else if (type == u"textMention"_q || type == u"mentionName"_q) {
			mention(map.value(u"userId"_q).toString());
		} else if (type == u"customEmoji"_q) {
			const auto id = map.value(u"documentId"_q).toString();
			if (Digits.match(id).hasMatch()) {
				add(EntityType::CustomEmoji, id);
			}
		}
	}
	std::stable_sort(result.begin(), result.end(), [](
			const EntityInText &a,
			const EntityInText &b) {
		return (a.offset() < b.offset())
			|| (a.offset() == b.offset() && a.length() > b.length());
	});
	return result;
}

[[nodiscard]] TextWithTags TagsFromJs(
		not_null<Main::Session*> session,
		const QString &text,
		const QVariantList &entities) {
	return {
		text,
		TextUtilities::ConvertEntitiesToTextTags(
			EntitiesFromJs(session, text.size(), entities)),
	};
}

// Calls done() with every requested message that exists, loading the
// missing ones from the server first.
void WithMessages(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		std::vector<MsgId> ids,
		Fn<void(std::vector<not_null<HistoryItem*>>)> done) {
	const auto peerId = peer->id;
	const auto collect = [=] {
		auto result = std::vector<not_null<HistoryItem*>>();
		for (const auto id : ids) {
			if (const auto item = session->data().message(peerId, id)) {
				result.push_back(item);
			}
		}
		done(std::move(result));
	};
	auto missing = std::vector<MsgId>();
	for (const auto id : ids) {
		if (id > 0 && !session->data().message(peerId, id)) {
			missing.push_back(id);
		}
	}
	if (missing.empty()) {
		collect();
		return;
	}
	const auto left = std::make_shared<int>(int(missing.size()));
	for (const auto id : missing) {
		session->api().requestMessageData(peer, id, crl::guard(session, [=] {
			if (!--*left) {
				collect();
			}
		}));
	}
}

void WithMessage(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		MsgId id,
		Fn<void(HistoryItem*)> done) {
	WithMessages(session, peer, { id }, [=](
			std::vector<not_null<HistoryItem*>> items) {
		done(items.empty() ? nullptr : items.front().get());
	});
}

// messages.Messages -> { messages (oldest first), count, nextRate,
// nextOffsetId, nextOffsetPeerId }. Items are created in session data
// with the client's own helper, like the search in a chat does.
[[nodiscard]] QVariantMap ProcessMessages(
		not_null<Main::Session*> session,
		ChannelData *channel,
		const MTPmessages_Messages &result) {
	session->data().processExistingMessages(channel, result);
	auto ids = std::vector<FullMsgId>();
	auto count = 0;
	auto nextRate = 0;
	result.match([](const MTPDmessages_messagesNotModified &) {
	}, [&](const auto &data) {
		for (const auto &message : data.vmessages().v) {
			if (DateFromMessage(message)) {
				ids.push_back(FullMsgId(
					PeerFromMessage(message),
					IdFromMessage(message)));
			}
		}
		count = int(data.vmessages().v.size());
	});
	result.match([&](const MTPDmessages_messagesSlice &data) {
		count = data.vcount().v;
		nextRate = data.vnext_rate().value_or_empty();
	}, [&](const MTPDmessages_channelMessages &data) {
		count = data.vcount().v;
	}, [](const auto &) {});

	auto items = std::vector<not_null<HistoryItem*>>();
	for (const auto &id : ids) {
		if (const auto item = session->data().message(id)) {
			items.push_back(item);
		}
	}
	auto info = QVariantMap{
		{ u"count"_q, count },
		{ u"nextRate"_q, nextRate },
	};
	if (!ids.empty()) {
		info.insert(u"nextOffsetId"_q, double(ids.back().msg.bare));
		info.insert(
			u"nextOffsetPeerId"_q,
			PeerIdToString(ids.back().peer));
	}
	std::stable_sort(begin(items), end(items), [](
			not_null<HistoryItem*> a,
			not_null<HistoryItem*> b) {
		return (a->date() < b->date())
			|| (a->date() == b->date() && a->id < b->id);
	});
	auto list = QVariantList();
	list.reserve(items.size());
	for (const auto &item : items) {
		list.push_back(SerializeMessage(item));
	}
	info.insert(u"messages"_q, list);
	return info;
}

[[nodiscard]] std::optional<MTPMessagesFilter> SearchFilter(
		const QString &name) {
	if (name.isEmpty()) {
		return MTP_inputMessagesFilterEmpty();
	} else if (name == u"photo"_q) {
		return MTP_inputMessagesFilterPhotos();
	} else if (name == u"video"_q) {
		return MTP_inputMessagesFilterVideo();
	} else if (name == u"photoVideo"_q) {
		return MTP_inputMessagesFilterPhotoVideo();
	} else if (name == u"document"_q) {
		return MTP_inputMessagesFilterDocument();
	} else if (name == u"url"_q) {
		return MTP_inputMessagesFilterUrl();
	} else if (name == u"gif"_q) {
		return MTP_inputMessagesFilterGif();
	} else if (name == u"voice"_q) {
		return MTP_inputMessagesFilterVoice();
	} else if (name == u"music"_q) {
		return MTP_inputMessagesFilterMusic();
	} else if (name == u"roundVideo"_q) {
		return MTP_inputMessagesFilterRoundVideo();
	} else if (name == u"roundVoice"_q) {
		return MTP_inputMessagesFilterRoundVoice();
	} else if (name == u"mentions"_q) {
		return MTP_inputMessagesFilterMyMentions();
	} else if (name == u"pinned"_q) {
		return MTP_inputMessagesFilterPinned();
	} else if (name == u"geo"_q) {
		return MTP_inputMessagesFilterGeo();
	} else if (name == u"contacts"_q) {
		return MTP_inputMessagesFilterContacts();
	} else if (name == u"chatPhotos"_q) {
		return MTP_inputMessagesFilterChatPhotos();
	} else if (name == u"poll"_q) {
		return MTP_inputMessagesFilterPoll();
	}
	return std::nullopt;
}

[[nodiscard]] Data::ReactionId ReactionFromString(const QString &value) {
	static const auto Digits = QRegularExpression(u"^\\d+$"_q);
	if (Digits.match(value).hasMatch()) {
		return Data::ReactionId{ DocumentId(value.toULongLong()) };
	}
	return Data::ReactionId{ value };
}

[[nodiscard]] QString WithoutVariation(QString emoji) {
	return emoji.remove(QChar(0xFE0F));
}

// Finds the reaction as the client knows it: "❤️" and "❤" are the same.
// Never the paid one ("*"): toggleReaction() asserts on it.
[[nodiscard]] std::optional<Data::ReactionId> AllowedReaction(
		not_null<HistoryItem*> item,
		const Data::ReactionId &id) {
	if (id.paid()) {
		return std::nullopt;
	}
	const auto possible = Data::LookupPossibleReactions(item);
	if (id.custom()) {
		const auto known = ranges::any_of(possible.recent, [&](
				not_null<const Data::Reaction*> reaction) {
			return reaction->id == id;
		});
		return (known || possible.customAllowed)
			? std::make_optional(id)
			: std::nullopt;
	}
	const auto wanted = WithoutVariation(id.emoji());
	for (const auto &reaction : possible.recent) {
		if (reaction->id.paid()) {
			continue;
		} else if (reaction->id == id
			|| (!reaction->id.custom()
				&& WithoutVariation(reaction->id.emoji()) == wanted)) {
			return reaction->id;
		}
	}
	return std::nullopt;
}

[[nodiscard]] QString DownloadFolder(not_null<Main::Session*> session) {
	const auto path = Core::App().settings().downloadPath();
	if (path.isEmpty()) {
		return File::DefaultDownloadPath(session);
	} else if (path == FileDialog::Tmp()) {
		return session->local().tempDirectory();
	}
	return path;
}

[[nodiscard]] QString UniqueFilePath(
		const QString &folder,
		const QString &name) {
	QDir().mkpath(folder);
	auto clean = base::FileNameFromUserString(name);
	if (clean.isEmpty()) {
		clean = u"file"_q;
	}
	const auto dot = clean.lastIndexOf('.');
	const auto stem = (dot > 0) ? clean.mid(0, dot) : clean;
	const auto extension = (dot > 0) ? clean.mid(dot) : QString();
	const auto dir = QDir(folder);
	auto result = dir.absoluteFilePath(clean);
	for (auto i = 2; QFileInfo::exists(result); ++i) {
		result = dir.absoluteFilePath(
			stem + u" (%1)"_q.arg(i) + extension);
	}
	return result;
}

// Empty request: client download folder. A folder (existing or ending
// with a slash): a new file inside it. Anything else: that exact file.
[[nodiscard]] QString TargetPath(
		not_null<Main::Session*> session,
		const QString &requested,
		const QString &name) {
	if (requested.isEmpty()) {
		return UniqueFilePath(DownloadFolder(session), name);
	}
	const auto info = QFileInfo(requested);
	if (requested.endsWith('/')
		|| requested.endsWith('\\')
		|| info.isDir()) {
		return UniqueFilePath(requested, name);
	}
	QDir().mkpath(info.absolutePath());
	return info.absoluteFilePath();
}

[[nodiscard]] bool CanWriteFile(const QString &path) {
	auto file = QFile(path);
	const auto existed = file.exists();
	if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
		return false;
	}
	file.close();
	if (!existed) {
		file.remove();
	}
	return true;
}

[[nodiscard]] QString DocumentName(
		not_null<DocumentData*> document,
		not_null<HistoryItem*> item) {
	const auto name = document->filename();
	if (!name.isEmpty()) {
		return name;
	}
	const auto patterns = Core::MimeTypeForName(
		document->mimeString()).globPatterns();
	auto extension = patterns.isEmpty()
		? QString()
		: QString(patterns.front()).replace('*', QString());
	if (extension.isEmpty() || extension == u"."_q) {
		extension = document->isVoiceMessage() ? u".ogg"_q : u".bin"_q;
	}
	const auto prefix = document->isVoiceMessage()
		? u"voice"_q
		: document->isVideoMessage()
		? u"round"_q
		: document->sticker()
		? u"sticker"_q
		: document->isVideoFile()
		? u"video"_q
		: u"file"_q;
	return prefix + '_' + QString::number(item->id.bare) + extension;
}

void DownloadDocument(
		not_null<Bridge*> bridge,
		Promise promise,
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document,
		const QString &requested) {
	const auto session = &item->history()->session();
	const auto existing = document->filepath(true);
	if (!existing.isEmpty()) {
		if (requested.isEmpty()) {
			promise.resolve(existing);
			return;
		}
		const auto target = TargetPath(
			session,
			requested,
			QFileInfo(existing).fileName());
		if (target == QFileInfo(existing).absoluteFilePath()) {
			promise.resolve(target);
			return;
		}
		QFile::remove(target);
		if (QFile::copy(existing, target)) {
			promise.resolve(target);
		} else {
			promise.reject(u"WRITE_FAILED"_q);
		}
		return;
	}
	const auto target = TargetPath(
		session,
		requested,
		DocumentName(document, item));
	if (!CanWriteFile(target)) {
		// A loader that can't write the file resets the download folder
		// in the client settings and shows an error box.
		promise.reject(u"WRITE_FAILED"_q);
		return;
	}
	document->save(Data::FileOrigin(item->fullId()), target);
	const auto check = [=] {
		if (document->loading()) {
			return false;
		}
		// The file first: save() of an already loaded document writes it
		// right away and keeps the status of an older failed download.
		const auto path = document->filepath(true);
		if (!path.isEmpty()) {
			promise.resolve(path);
		} else if (document->status == FileDownloadFailed) {
			promise.reject(u"DOWNLOAD_FAILED"_q);
		} else if (document->cancelled()) {
			promise.reject(u"DOWNLOAD_CANCELLED"_q);
		} else if (QFileInfo::exists(target)) {
			promise.resolve(target);
		} else {
			promise.reject(u"DOWNLOAD_FAILED"_q);
		}
		return true;
	};
	if (check()) {
		return;
	}
	const auto wait = StartWait(bridge);
	session->data().documentLoadProgress(
	) | rpl::filter([=](not_null<DocumentData*> loaded) {
		return (loaded == document);
	}) | rpl::on_next([=] {
		if (check() || promise.done()) {
			FinishWait(wait);
		}
	}, WaitLifetime(wait));
}

void DownloadPhoto(
		not_null<Bridge*> bridge,
		Promise promise,
		not_null<HistoryItem*> item,
		not_null<PhotoData*> photo,
		const QString &requested) {
	const auto session = &item->history()->session();
	const auto target = TargetPath(
		session,
		requested,
		u"photo_"_q + QString::number(item->id.bare) + u".jpg"_q);
	const auto view = photo->createMediaView();
	const auto check = [=] {
		if (view->loaded()) {
			if (view->saveToFile(target)) {
				promise.resolve(target);
			} else {
				promise.reject(u"WRITE_FAILED"_q);
			}
			return true;
		} else if (photo->failed(Data::PhotoSize::Large)
			|| !photo->loading(Data::PhotoSize::Large)) {
			// Failed, or the load didn't start (no location): no event
			// would come for it.
			promise.reject(u"DOWNLOAD_FAILED"_q);
			return true;
		}
		return false;
	};
	if (!view->loaded()) {
		// Like the download button: an earlier failure makes the loader
		// skip the photo until the flag is cleared.
		photo->clearFailed(Data::PhotoSize::Large);
		view->wanted(
			Data::PhotoSize::Large,
			Data::FileOrigin(item->fullId()));
	}
	if (check()) {
		return;
	}
	const auto wait = StartWait(bridge);
	session->data().photoLoadProgress(
	) | rpl::filter([=](not_null<PhotoData*> loaded) {
		return (loaded == photo);
	}) | rpl::on_next([=] {
		if (check() || promise.done()) {
			FinishWait(wait);
		}
	}, WaitLifetime(wait));
}

} // namespace

namespace {

bool Send(
		const QString &peerId,
		const QString &text,
		const QVariantList &entities,
		const QVariantMap &options) {
	const auto session = ActiveSession();
	const auto peer = ResolvePeer(session, peerId);
	if (!peer || text.trimmed().isEmpty()) {
		return false;
	}
	const auto history = session->data().history(peer);
	auto message = Api::MessageToSend(PrepareAction(history, options));
	message.textWithTags = TagsFromJs(session, text, entities);
	message.webPage.removed = options.value(u"noWebpage"_q).toBool();
	const auto runHooks = options.value(u"runHooks"_q).toBool();
	auto &manager = Manager::Get();
	const auto was = manager.sendingFromPlugin();
	manager.setSendingFromPlugin(!runHooks);
	session->api().sendMessage(std::move(message));
	manager.setSendingFromPlugin(was);
	return true;
}

bool Edit(
		not_null<Bridge*> bridge,
		const QString &peerId,
		MsgId msgId,
		const QString &text,
		const QVariantList &entities,
		const QVariantMap &options) {
	const auto session = ActiveSession();
	const auto item = ResolveMessage(session, peerId, double(msgId.bare));
	if (!item || item->isSending() || item->hasFailed()) {
		// Not on the server yet: editing a scheduled or quick reply one
		// asserts in its lookupId(), a regular one is MESSAGE_ID_INVALID.
		return false;
	} else if (item->computeSuggestionActions()
		== SuggestionActions::AcceptAndDecline) {
		// A suggested post is "edited" by suggesting a new one, which
		// asserts on the suggest options the edit field fills in.
		return false;
	}
	// The same round trip as the edit field: tags, then entities with
	// links, mentions and hashtags found.
	const auto tags = TagsFromJs(session, text, entities);
	auto sending = TextWithEntities{
		tags.text,
		TextUtilities::ConvertTextTagsToEntities(tags.tags),
	};
	TextUtilities::PrepareForSending(
		sending,
		Ui::ItemTextOptions(item->history(), session->user()).flags);
	auto webpage = Data::WebPageDraft();
	webpage.removed = options.value(u"noWebpage"_q).toBool();
	// Like FieldHeader::queryToEdit(): without these the edit flips media
	// shown above the caption and drops the schedule date.
	auto editOptions = Api::SendOptions();
	editOptions.scheduled = item->isScheduled() ? item->date() : 0;
	editOptions.shortcutId = item->shortcutId();
	editOptions.invertCaption = item->invertMedia();
	const auto media = item->media();
	const auto weak = QPointer<Bridge>(bridge.get());
	Api::EditTextMessage(
		item,
		sending,
		webpage,
		editOptions,
		[](mtpRequestId) {},
		[=](const QString &error, mtpRequestId) {
			if (weak) {
				weak->log(u"warn"_q, u"messages.edit failed: "_q + error);
			}
		},
		media && media->hasSpoiler());
	return true;
}

int RequestHistory(
		not_null<Bridge*> bridge,
		const QString &peerId,
		const QVariantMap &o) {
	const auto promise = Promise(bridge);
	const auto session = ActiveSession();
	const auto peer = ResolvePeer(session, peerId);
	if (!peer) {
		promise.reject(u"PEER_NOT_FOUND"_q);
		return promise.id();
	}
	const auto limit = std::clamp(
		o.value(u"limit"_q).toInt(),
		1,
		kMaxPage);
	const auto channel = peer->asChannel();
	session->api().request(MTPmessages_GetHistory(
		peer->input(),
		MTP_int(o.value(u"offsetId"_q).toInt()),
		MTP_int(o.value(u"offsetDate"_q).toInt()),
		MTP_int(o.value(u"addOffset"_q).toInt()),
		MTP_int(limit),
		MTP_int(o.value(u"maxId"_q).toInt()),
		MTP_int(o.value(u"minId"_q).toInt()),
		MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		promise.resolve(ProcessMessages(session, channel, result));
	}).fail([=](const MTP::Error &error) {
		promise.reject(error.type());
	}).send();
	return promise.id();
}

int RequestSearch(
		not_null<Bridge*> bridge,
		const QString &peerId,
		const QString &query,
		const QVariantMap &o) {
	const auto promise = Promise(bridge);
	const auto session = ActiveSession();
	if (!session) {
		promise.reject(u"NO_SESSION"_q);
		return promise.id();
	}
	const auto filter = SearchFilter(o.value(u"filter"_q).toString());
	if (!filter) {
		promise.reject(u"UNKNOWN_FILTER"_q);
		return promise.id();
	}
	const auto limit = std::clamp(
		o.value(u"limit"_q).toInt(),
		1,
		kMaxPage);
	const auto minDate = MTP_int(o.value(u"minDate"_q).toInt());
	const auto maxDate = MTP_int(o.value(u"maxDate"_q).toInt());
	const auto offsetId = MTP_int(o.value(u"offsetId"_q).toInt());
	const auto peer = ResolvePeer(session, peerId);
	const auto channel = peer ? peer->asChannel() : nullptr;
	const auto done = [=](const MTPmessages_Messages &result) {
		promise.resolve(ProcessMessages(session, channel, result));
	};
	const auto fail = [=](const MTP::Error &error) {
		promise.reject(error.type());
	};
	if (peerId.isEmpty()) {
		using Flag = MTPmessages_SearchGlobal::Flag;
		const auto folder = o.value(u"folder"_q).toInt();
		const auto type = o.value(u"type"_q).toString();
		const auto offsetPeer = ResolvePeer(
			session,
			o.value(u"offsetPeerId"_q).toString());
		const auto flags = ((folder >= 0) ? Flag::f_folder_id : Flag())
			| ((type == u"channels"_q) ? Flag::f_broadcasts_only : Flag())
			| ((type == u"groups"_q) ? Flag::f_groups_only : Flag())
			| ((type == u"users"_q) ? Flag::f_users_only : Flag());
		session->api().request(MTPmessages_SearchGlobal(
			MTP_flags(flags),
			MTP_int(std::max(folder, 0)),
			MTPInputChannel(),
			MTP_string(query),
			*filter,
			minDate,
			maxDate,
			MTP_int(o.value(u"offsetRate"_q).toInt()),
			offsetPeer ? offsetPeer->input() : MTP_inputPeerEmpty(),
			offsetId,
			MTP_int(limit)
		)).done(done).fail(fail).send();
		return promise.id();
	}
	if (!peer) {
		promise.reject(u"PEER_NOT_FOUND"_q);
		return promise.id();
	}
	const auto fromId = o.value(u"from"_q).toString();
	const auto from = fromId.isEmpty() ? nullptr : ResolvePeer(session, fromId);
	if (!fromId.isEmpty() && !from) {
		promise.reject(u"FROM_PEER_NOT_FOUND"_q);
		return promise.id();
	}
	// In Saved Messages "from" is the chat the message was saved from.
	const auto fromPeer = peer->isSelf() ? nullptr : from;
	const auto savedPeer = peer->isSelf() ? from : nullptr;
	const auto topicId = o.value(u"topicId"_q).toInt();
	using Flag = MTPmessages_Search::Flag;
	session->api().request(MTPmessages_Search(
		MTP_flags((fromPeer ? Flag::f_from_id : Flag())
			| (savedPeer ? Flag::f_saved_peer_id : Flag())
			| (topicId ? Flag::f_top_msg_id : Flag())),
		peer->input(),
		MTP_string(query),
		fromPeer ? fromPeer->input() : MTP_inputPeerEmpty(),
		savedPeer ? savedPeer->input() : MTP_inputPeerEmpty(),
		MTP_vector<MTPReaction>(),
		MTP_int(topicId),
		*filter,
		minDate,
		maxDate,
		offsetId,
		MTP_int(o.value(u"addOffset"_q).toInt()),
		MTP_int(limit),
		MTP_int(0), // max_id
		MTP_int(0), // min_id
		MTP_long(0) // hash
	)).done(done).fail(fail).send();
	return promise.id();
}

int Fetch(
		not_null<Bridge*> bridge,
		const QString &peerId,
		const QVariantList &ids) {
	const auto promise = Promise(bridge);
	const auto session = ActiveSession();
	const auto peer = ResolvePeer(session, peerId);
	if (!peer) {
		promise.reject(u"PEER_NOT_FOUND"_q);
		return promise.id();
	}
	auto list = std::vector<MsgId>();
	for (const auto &id : ids) {
		if (const auto value = id.toLongLong(); value > 0) {
			list.push_back(MsgId(value));
		}
	}
	WithMessages(session, peer, list, [=](
			std::vector<not_null<HistoryItem*>> items) {
		auto result = QVariantList();
		for (const auto &item : items) {
			result.push_back(SerializeMessage(item));
		}
		promise.resolve(result);
	});
	return promise.id();
}

int Forward(
		not_null<Bridge*> bridge,
		const QString &fromPeerId,
		const QVariantList &ids,
		const QString &toPeerId,
		const QVariantMap &o) {
	const auto promise = Promise(bridge);
	const auto session = ActiveSession();
	const auto from = ResolvePeer(session, fromPeerId);
	const auto to = ResolvePeer(session, toPeerId);
	if (!from || !to) {
		promise.reject(u"PEER_NOT_FOUND"_q);
		return promise.id();
	}
	auto list = std::vector<MsgId>();
	for (const auto &id : ids) {
		if (const auto value = id.toLongLong(); value > 0) {
			list.push_back(MsgId(value));
		}
	}
	if (list.empty()) {
		promise.reject(u"MESSAGE_IDS_EMPTY"_q);
		return promise.id();
	}
	const auto weak = QPointer<Bridge>(bridge.get());
	WithMessages(session, from, list, [=](
			std::vector<not_null<HistoryItem*>> found) {
		if (found.empty()) {
			promise.reject(u"MESSAGE_NOT_FOUND"_q);
			return;
		}
		// HistoryItem::allowsForward() without the content protection,
		// AyuGram forwards that itself. A call is not isService(), but a
		// forwarded copy of it hits Unexpected() in MediaCall::clone().
		auto items = HistoryItemsList();
		for (const auto &item : found) {
			const auto media = item->media();
			if (!item->isService()
				&& (item->isRegular() || item->isEphemeral())
				&& !(media && media->call())) {
				items.push_back(item);
			}
		}
		if (items.empty()) {
			promise.reject(u"MESSAGE_NOT_FORWARDABLE"_q);
			return;
		}
		const auto history = session->data().history(to);
		const auto action = PrepareAction(history, o);
		const auto ayu = AyuForward::isFullAyuForwardNeeded(items.front())
			|| AyuForward::isAyuForwardNeeded(items);
		auto draft = Data::ResolvedForwardDraft{
			.items = std::move(items),
			.options = (o.value(u"dropCaptions"_q).toBool()
				? Data::ForwardOptions::NoNamesAndCaptions
				: o.value(u"dropAuthor"_q).toBool()
				? Data::ForwardOptions::NoSenderNames
				: Data::ForwardOptions::PreserveInfo),
		};
		session->api().forwardMessages(std::move(draft), action, [=] {
			promise.resolve();
		});
		if (ayu) {
			// AyuGram re-sends protected content itself, with no callback.
			promise.resolve();
		} else if (weak) {
			QTimer::singleShot(kForwardTimeout, weak.data(), [=] {
				promise.reject(u"TIMEOUT"_q);
			});
		}
	});
	return promise.id();
}

int React(
		not_null<Bridge*> bridge,
		const QString &peerId,
		MsgId msgId,
		const QString &emoji,
		const QVariantMap &o) {
	const auto promise = Promise(bridge);
	const auto session = ActiveSession();
	const auto peer = ResolvePeer(session, peerId);
	if (!peer) {
		promise.reject(u"PEER_NOT_FOUND"_q);
		return promise.id();
	}
	const auto source = o.value(u"addToRecent"_q).toBool()
		? HistoryReactionSource::Selector
		: HistoryReactionSource::Quick;
	WithMessage(session, peer, msgId, [=](HistoryItem *item) {
		if (!item) {
			promise.reject(u"MESSAGE_NOT_FOUND"_q);
			return;
		}
		const auto chosen = item->chosenReactions();
		if (emoji.isEmpty()) {
			for (const auto &id : chosen) {
				if (!id.paid()) {
					item->toggleReaction(id, source);
				}
			}
			promise.resolve();
			return;
		}
		const auto id = AllowedReaction(item, ReactionFromString(emoji));
		if (!id) {
			promise.reject(u"REACTION_INVALID"_q);
			return;
		} else if (!ranges::contains(chosen, *id)) {
			item->toggleReaction(*id, source);
		}
		if (ranges::contains(item->chosenReactions(), *id)) {
			promise.resolve();
		} else {
			promise.reject(u"REACTION_NOT_ALLOWED"_q);
		}
	});
	return promise.id();
}

QVariant AvailableReactions(const QString &peerId, MsgId msgId) {
	const auto item = ResolveMessage(
		ActiveSession(),
		peerId,
		double(msgId.bare));
	auto result = QVariantList();
	if (!item) {
		return result;
	}
	const auto possible = Data::LookupPossibleReactions(item);
	for (const auto &reaction : possible.recent) {
		if (!reaction->id.paid()) {
			result.push_back(SerializeReactionId(reaction->id));
		}
	}
	return result;
}

int Pin(
		not_null<Bridge*> bridge,
		const QString &peerId,
		MsgId msgId,
		const QVariantMap &o) {
	const auto promise = Promise(bridge);
	const auto session = ActiveSession();
	const auto peer = ResolvePeer(session, peerId);
	if (!peer) {
		promise.reject(u"PEER_NOT_FOUND"_q);
		return promise.id();
	}
	using Flag = MTPmessages_UpdatePinnedMessage::Flag;
	const auto flags = (o.value(u"silent"_q).toBool() ? Flag::f_silent : Flag())
		| (o.value(u"unpin"_q).toBool() ? Flag::f_unpin : Flag())
		| (o.value(u"oneSide"_q).toBool() ? Flag::f_pm_oneside : Flag());
	session->api().request(MTPmessages_UpdatePinnedMessage(
		MTP_flags(flags),
		peer->input(),
		MTP_int(msgId)
	)).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		promise.resolve();
	}).fail([=](const MTP::Error &error) {
		promise.reject(error.type());
	}).send();
	return promise.id();
}

bool MarkRead(const QString &peerId, const QVariantMap &o) {
	const auto session = ActiveSession();
	const auto peer = ResolvePeer(session, peerId);
	if (!peer) {
		return false;
	}
	const auto history = session->data().history(peer);
	auto &histories = session->data().histories();
	if (const auto tillId = MsgId(o.value(u"tillId"_q).toLongLong())
		; tillId > 0) {
		if (!IsServerMsgId(tillId)) {
			return false; // readInboxTill() asserts on it.
		}
		histories.readInboxTill(history, tillId);
	} else {
		histories.readInbox(history);
	}
	return true;
}

bool SendFiles(
		not_null<Bridge*> bridge,
		const QString &peerId,
		const QVariantList &paths,
		const QVariantMap &o) {
	const auto session = ActiveSession();
	const auto peer = ResolvePeer(session, peerId);
	if (!peer
		|| paths.isEmpty()
		|| !Data::CanSendAnyOf(peer, Data::FilesSendRestrictions(), false)) {
		return false;
	}
	auto files = QStringList();
	for (const auto &path : paths) {
		files.push_back(bridge->resolvePath(path.toString()));
	}
	const auto premium = session->user()->isPremium();
	const auto chunk = std::max(Ui::MaxAlbumItems(), 1);
	auto lists = std::vector<Ui::PreparedList>();
	for (auto i = 0; i < files.size(); i += chunk) {
		auto list = Storage::PrepareMediaList(
			files.mid(i, chunk),
			st::sendMediaPreviewSize,
			premium);
		if (list.error != Ui::PreparedList::Error::None) {
			bridge->log(
				u"warn"_q,
				u"messages.sendFile: can't send "_q + list.errorData);
			return false;
		}
		lists.push_back(std::move(list));
	}
	const auto asDocument = o.value(u"asDocument"_q).toBool();
	const auto spoiler = o.value(u"spoiler"_q).toBool() && !asDocument;
	auto way = Ui::SendFilesWay();
	way.setGroupFiles(o.value(u"album"_q).toBool());
	way.setSendImagesAsPhotos(!asDocument);
	const auto type = asDocument ? SendMediaType::File : SendMediaType::Photo;
	const auto history = session->data().history(peer);
	const auto action = PrepareAction(history, o);
	auto caption = TagsFromJs(
		session,
		o.value(u"caption"_q).toString(),
		o.value(u"captionEntities"_q).toList());
	for (auto i = 0; i != int(lists.size()); ++i) {
		auto &list = lists[i];
		for (auto &file : list.files) {
			file.spoiler = spoiler
				&& (file.type == Ui::PreparedFile::Type::Photo
					|| file.type == Ui::PreparedFile::Type::Video);
		}
		auto groups = Ui::DivideByGroups(std::move(list), way, false);
		const auto last = (i + 1 == int(lists.size()));
		if (last && !groups.empty() && !caption.text.isEmpty()) {
			auto &group = groups.back();
			auto &captioned = (group.type == Ui::AlbumType::PhotoVideo)
				? group.list.files.front()
				: group.list.files.back();
			captioned.caption = std::move(caption);
		}
		for (auto &group : groups) {
			const auto album = (group.type != Ui::AlbumType::None)
				? std::make_shared<SendingAlbum>()
				: nullptr;
			session->api().sendFiles(
				std::move(group.list),
				type,
				album,
				action);
		}
	}
	return true;
}

int Download(
		not_null<Bridge*> bridge,
		const QString &peerId,
		MsgId msgId,
		const QString &path) {
	const auto promise = Promise(bridge);
	const auto session = ActiveSession();
	const auto peer = ResolvePeer(session, peerId);
	if (!peer) {
		promise.reject(u"PEER_NOT_FOUND"_q);
		return promise.id();
	}
	const auto folder = path.endsWith('/') || path.endsWith('\\');
	const auto requested = path.isEmpty()
		? QString()
		: (bridge->resolvePath(path) + (folder ? u"/"_q : QString()));
	const auto weak = QPointer<Bridge>(bridge.get());
	WithMessage(session, peer, msgId, [=](HistoryItem *item) {
		const auto media = item ? item->media() : nullptr;
		const auto document = media ? media->document() : nullptr;
		const auto photo = media ? media->photo() : nullptr;
		if (!item) {
			promise.reject(u"MESSAGE_NOT_FOUND"_q);
		} else if (!weak) {
			return;
		} else if (document) {
			DownloadDocument(weak.data(), promise, item, document, requested);
		} else if (photo) {
			DownloadPhoto(weak.data(), promise, item, photo, requested);
		} else {
			promise.reject(u"NO_MEDIA"_q);
		}
	});
	return promise.id();
}

QVariant Link(const QString &peerId, MsgId msgId, const QVariantMap &o) {
	const auto session = ActiveSession();
	const auto peer = ResolvePeer(session, peerId);
	const auto channel = peer ? peer->asChannel() : nullptr;
	if (!channel || msgId <= 0) {
		return QVariant();
	}
	const auto forcePrivate = o.value(u"private"_q).toBool();
	const auto item = session->data().message(peer->id, msgId);
	if (item && item->hasDirectLink()) {
		return session->api().exportDirectMessageLink(
			item,
			o.value(u"thread"_q).toBool(),
			forcePrivate);
	}
	const auto base = (channel->hasUsername() && !forcePrivate)
		? channel->username()
		: (u"c/"_q + QString::number(peerToChannel(channel->id).bare));
	return session->createInternalLinkFull(
		base + '/' + QString::number(msgId.bare));
}

[[nodiscard]] std::optional<std::pair<int, int>> FindButton(
		not_null<HistoryItem*> item,
		const QString &text) {
	const auto markup = item->Get<HistoryMessageReplyMarkup>();
	const auto wanted = text.trimmed();
	if (!markup || wanted.isEmpty()) {
		// Every title contains an empty string.
		return std::nullopt;
	}
	const auto &rows = markup->data.rows;
	for (const auto exact : { true, false }) {
		for (auto row = 0; row != int(rows.size()); ++row) {
			for (auto column = 0; column != int(rows[row].size()); ++column) {
				const auto &title = rows[row][column].text;
				if (exact
					? (title.trimmed() == wanted)
					: title.contains(wanted, Qt::CaseInsensitive)) {
					return std::make_pair(row, column);
				}
			}
		}
	}
	return std::nullopt;
}

void SendCallback(
		Promise promise,
		not_null<HistoryItem*> item,
		int row,
		int column,
		bool open) {
	const auto history = item->history();
	const auto session = &history->session();
	const auto owner = &history->owner();
	const auto fullId = item->fullId();
	const auto lookup = [=] {
		return HistoryMessageMarkupButton::Get(owner, fullId, row, column);
	};
	const auto button = lookup();
	if (!button) {
		promise.reject(u"BUTTON_NOT_FOUND"_q);
		return;
	} else if (button->requestId) {
		promise.reject(u"BUTTON_BUSY"_q);
		return;
	}
	using ButtonType = HistoryMessageMarkupButton::Type;
	using Flag = MTPmessages_GetBotCallbackAnswer::Flag;
	const auto game = (button->type == ButtonType::Game);
	const auto finish = [=] {
		if (const auto button = lookup()) {
			button->requestId = 0;
			if (const auto item = owner->message(fullId)) {
				owner->requestItemRepaint(item);
			}
		}
	};
	button->requestId = session->api().request(MTPmessages_GetBotCallbackAnswer(
		MTP_flags(game ? Flag::f_game : Flag::f_data),
		history->peer->input(),
		MTP_int(item->id),
		MTP_bytes(game ? QByteArray() : button->data),
		MTP_inputCheckPasswordEmpty()
	)).done([=](const MTPmessages_BotCallbackAnswer &result) {
		finish();
		const auto &data = result.data();
		const auto message = data.vmessage()
			? qs(*data.vmessage())
			: QString();
		const auto url = data.vurl() ? qs(*data.vurl()) : QString();
		if (open) {
			if (!message.isEmpty()) {
				if (const auto controller = ControllerFor(session)) {
					controller->showToast(message);
				}
			} else if (!url.isEmpty() && !game) {
				UrlClickHandler::Open(url);
			}
		}
		auto answer = QVariantMap();
		if (!message.isEmpty()) {
			answer.insert(u"answer"_q, message);
		}
		if (!url.isEmpty()) {
			answer.insert(u"url"_q, url);
		}
		if (data.is_alert()) {
			answer.insert(u"alert"_q, true);
		}
		promise.resolve(answer);
	}).fail([=](const MTP::Error &error) {
		finish();
		promise.reject(error.type());
	}).send();
	session->changes().messageUpdated(
		item,
		Data::MessageUpdate::Flag::BotCallbackSent);
}

int ClickButton(
		not_null<Bridge*> bridge,
		const QString &peerId,
		MsgId msgId,
		int row,
		int column,
		const QVariantMap &o) {
	const auto promise = Promise(bridge);
	const auto session = ActiveSession();
	const auto peer = ResolvePeer(session, peerId);
	if (!peer) {
		promise.reject(u"PEER_NOT_FOUND"_q);
		return promise.id();
	}
	const auto text = o.value(u"text"_q).toString();
	const auto open = o.value(u"open"_q).toBool();
	WithMessage(session, peer, msgId, [=](HistoryItem *item) {
		if (!item) {
			promise.reject(u"MESSAGE_NOT_FOUND"_q);
			return;
		}
		auto position = std::make_pair(row, column);
		if (!text.isEmpty()) {
			const auto found = FindButton(item, text);
			if (!found) {
				promise.reject(u"BUTTON_NOT_FOUND"_q);
				return;
			}
			position = *found;
		}
		const auto [r, c] = position;
		const auto button = HistoryMessageMarkupButton::Get(
			&session->data(),
			item->fullId(),
			r,
			c);
		if (!button) {
			promise.reject(u"BUTTON_NOT_FOUND"_q);
			return;
		}
		using Type = HistoryMessageMarkupButton::Type;
		const auto type = button->type;
		if (type == Type::Callback || type == Type::Game) {
			SendCallback(promise, item, r, c, open);
			return;
		} else if (type == Type::Url && !open) {
			promise.resolve(QVariantMap{
				{ u"url"_q, QString::fromUtf8(button->data) },
			});
			return;
		}
		const auto controller = ControllerFor(session);
		if (!controller && type == Type::Default) {
			// A reply keyboard button just sends its text.
			const auto replyTo = item->isRegular()
				? double(item->id.bare)
				: 0.;
			const auto sent = Send(
				PeerIdToString(peer->id),
				button->text,
				{},
				{ { u"replyTo"_q, replyTo } });
			if (sent) {
				promise.resolve(QVariantMap());
			} else {
				promise.reject(u"SEND_FAILED"_q);
			}
			return;
		} else if (!controller) {
			promise.reject(u"NO_WINDOW"_q);
			return;
		}
		auto result = QVariantMap();
		if (type == Type::Url) {
			result.insert(u"url"_q, QString::fromUtf8(button->data));
		}
		Api::ActivateBotCommand({
			.itemId = item->fullId(),
			.sessionWindow = base::make_weak(controller),
		}, r, c);
		promise.resolve(result);
	});
	return promise.id();
}

[[nodiscard]] MsgId ToMsgId(const QVariant &value) {
	return MsgId(int64(value.toDouble()));
}

} // namespace

DZG_API_MODULE({
	.name = "messages",
	.natives = [](not_null<Bridge*> bridge, std::vector<Js::NativeMethod> &out) {
		const auto str = [](const Args &a, int i) { return a.value(i).toString(); };
		const auto map = [](const Args &a, int i) { return a.value(i).toMap(); };
		const auto list = [](const Args &a, int i) { return a.value(i).toList(); };
		out.push_back({ "messages_send", [=](const Args &a) {
			return QVariant(Send(str(a, 0), str(a, 1), list(a, 2), map(a, 3)));
		} });
		out.push_back({ "messages_edit", [=](const Args &a) {
			return QVariant(Edit(
				bridge,
				str(a, 0),
				ToMsgId(a.value(1)),
				str(a, 2),
				list(a, 3),
				map(a, 4)));
		} });
		out.push_back({ "messages_history", [=](const Args &a) {
			return QVariant(RequestHistory(bridge, str(a, 0), map(a, 1)));
		} });
		out.push_back({ "messages_search", [=](const Args &a) {
			return QVariant(
				RequestSearch(bridge, str(a, 0), str(a, 1), map(a, 2)));
		} });
		out.push_back({ "messages_fetch", [=](const Args &a) {
			return QVariant(Fetch(bridge, str(a, 0), list(a, 1)));
		} });
		out.push_back({ "messages_forward", [=](const Args &a) {
			return QVariant(Forward(
				bridge,
				str(a, 0),
				list(a, 1),
				str(a, 2),
				map(a, 3)));
		} });
		out.push_back({ "messages_react", [=](const Args &a) {
			return QVariant(React(
				bridge,
				str(a, 0),
				ToMsgId(a.value(1)),
				str(a, 2),
				map(a, 3)));
		} });
		out.push_back({ "messages_availableReactions", [=](const Args &a) {
			return AvailableReactions(str(a, 0), ToMsgId(a.value(1)));
		} });
		out.push_back({ "messages_pin", [=](const Args &a) {
			return QVariant(
				Pin(bridge, str(a, 0), ToMsgId(a.value(1)), map(a, 2)));
		} });
		out.push_back({ "messages_markRead", [=](const Args &a) {
			return QVariant(MarkRead(str(a, 0), map(a, 1)));
		} });
		out.push_back({ "messages_sendFile", [=](const Args &a) {
			return QVariant(
				SendFiles(bridge, str(a, 0), list(a, 1), map(a, 2)));
		} });
		out.push_back({ "messages_download", [=](const Args &a) {
			return QVariant(Download(
				bridge,
				str(a, 0),
				ToMsgId(a.value(1)),
				str(a, 2)));
		} });
		out.push_back({ "messages_link", [=](const Args &a) {
			return Link(str(a, 0), ToMsgId(a.value(1)), map(a, 2));
		} });
		out.push_back({ "messages_clickButton", [=](const Args &a) {
			return QVariant(ClickButton(
				bridge,
				str(a, 0),
				ToMsgId(a.value(1)),
				a.value(2).toInt(),
				a.value(3).toInt(),
				map(a, 4)));
		} });
	},
	.prelude = [] {
		return QString::fromUtf8(R"DZG_JS(
{
	// Markdown and HTML <-> { text, entities }. Offsets are UTF-16 units,
	// the same as in QString and in Message.entities.
	const PAIRS = [
		['**', 'bold'],
		['__', 'italic'],
		['~~', 'strikethrough'],
		['||', 'spoiler'],
	];
	const ESCAPABLE = '\\`*_~|[]()>!#+-=.{}';
	// Markers inside bare links are kept: http://x.com/__init__.py
	const URL = /(?:[a-z][a-z0-9+.-]*:\/\/|www\.)[^\s<>\[\]`\\]+/iy;
	const matchUrl = (s, i) => {
		if (i && /[\p{L}\p{N}]/u.test(s[i - 1])) return 0;
		URL.lastIndex = i;
		const url = URL.exec(s);
		const raw = url ? url[0].replace(/[*_~|]+$/, '') : '';
		return (raw.includes('/') || raw.includes('.')) ? raw.length : 0;
	};

	const parseMarkdown = (source) => {
		const src = String(source ?? '').replace(/\r\n?/g, '\n');
		let out = '';
		const entities = [];
		const push = (type, offset, extra) => {
			const length = out.length - offset;
			if (length > 0) {
				entities.push(Object.assign({ type, offset, length }, extra || {}));
			}
		};
		const findClose = (s, marker, from) => {
			for (let i = from; i <= s.length - marker.length; ++i) {
				if (s[i] === '\\') {
					++i;
				} else if (s[i] === '`') {
					const end = s.indexOf('`', i + 1);
					if (end > i) i = end;
				} else if (s.startsWith(marker, i)) {
					return i;
				}
			}
			return -1;
		};
		const findBracket = (s, from) => {
			let depth = 1;
			for (let i = from; i < s.length; ++i) {
				const c = s[i];
				if (c === '\\') {
					++i;
				} else if (c === '[') {
					++depth;
				} else if (c === ']' && --depth === 0) {
					return i;
				}
			}
			return -1;
		};
		const findParen = (s, from) => {
			let depth = 1;
			for (let i = from; i < s.length; ++i) {
				const c = s[i];
				if (c === '\\') {
					++i;
				} else if (c === '(') {
					++depth;
				} else if (c === ')' && --depth === 0) {
					return i;
				} else if (c === '\n') {
					return -1;
				}
			}
			return -1;
		};
		const unescapeUrl = (url) => url.replace(/\\([()\\])/g, '$1').trim();
		const inline = (s) => {
			let i = 0;
			while (i < s.length) {
				const c = s[i];
				if (c === '\\' && i + 1 < s.length && ESCAPABLE.includes(s[i + 1])) {
					out += s[i + 1];
					i += 2;
					continue;
				}
				const url = matchUrl(s, i);
				if (url) {
					out += s.slice(i, i + url);
					i += url;
					continue;
				}
				if (s.startsWith('```', i)) {
					const end = s.indexOf('```', i + 3);
					if (end >= 0) {
						let body = s.slice(i + 3, end);
						let language = '';
						const newline = body.indexOf('\n');
						if (newline >= 0) {
							const first = body.slice(0, newline).trim();
							if (!first.length || /^[A-Za-z0-9_+\-#.]{1,32}$/.test(first)) {
								language = first;
								body = body.slice(newline + 1);
							}
						}
						if (body.endsWith('\n')) body = body.slice(0, -1);
						const offset = out.length;
						out += body;
						push('pre', offset, language ? { language } : null);
						i = end + 3;
						continue;
					}
				}
				if (c === '`') {
					const end = s.indexOf('`', i + 1);
					if (end > i + 1) {
						const offset = out.length;
						out += s.slice(i + 1, end);
						push('code', offset);
						i = end + 1;
						continue;
					}
				}
				let matched = false;
				for (const [marker, type] of PAIRS) {
					if (!s.startsWith(marker, i)) continue;
					const from = i + marker.length;
					const end = findClose(s, marker, from);
					if (end > from) {
						const offset = out.length;
						inline(s.slice(from, end));
						push(type, offset);
						i = end + marker.length;
						matched = true;
					}
					break;
				}
				if (matched) continue;
				const emoji = (c === '!' && s[i + 1] === '[');
				if (c === '[' || emoji) {
					const open = i + (emoji ? 2 : 1);
					const close = findBracket(s, open);
					if (close > 0 && s[close + 1] === '(') {
						const end = findParen(s, close + 2);
						if (end > 0) {
							const url = unescapeUrl(s.slice(close + 2, end));
							const custom = /^tg:\/\/emoji\?id=(\d+)$/i.exec(url);
							const mention = /^tg:\/\/user\?id=(\d+)$/i.exec(url);
							if (url.length && (!emoji || custom)) {
								const offset = out.length;
								inline(s.slice(open, close));
								if (custom) {
									push('customEmoji', offset, { documentId: custom[1] });
								} else if (mention) {
									push('textMention', offset, { userId: mention[1] });
								} else {
									push('textLink', offset, { url });
								}
								i = end + 1;
								continue;
							}
						}
					}
				}
				out += c;
				++i;
			}
		};
		// Same rules as inline(): an unclosed ``` is plain text and only
		// ESCAPABLE characters are escaped (never the line break).
		const nextQuote = (from) => {
			for (let i = from; i < src.length; ++i) {
				if (src.startsWith('```', i)) {
					const end = src.indexOf('```', i + 3);
					i = (end < 0) ? (i + 2) : (end + 2);
				} else if (src[i] === '\\' && ESCAPABLE.includes(src[i + 1])) {
					++i;
				} else if (src[i] === '\n' && (src[i + 1] === '>' || src.startsWith('**>', i + 1))) {
					return i + 1;
				}
			}
			return src.length;
		};
		let i = (src[0] === '>' || src.startsWith('**>')) ? 0 : nextQuote(0);
		inline(src.slice(0, i));
		while (i < src.length) {
			const lines = [];
			const expandable = src.startsWith('**>', i);
			let j = i;
			while (j < src.length
				&& (src[j] === '>' || (j === i && expandable))) {
				let end = src.indexOf('\n', j);
				if (end < 0) end = src.length;
				let line = src.slice(j + ((j === i && expandable) ? 3 : 1), end);
				if (line.startsWith(' ')) line = line.slice(1);
				lines.push(line);
				j = end + 1;
			}
			let collapsed = false;
			if (expandable && lines.length) {
				const last = lines[lines.length - 1];
				if (last.endsWith('||') && !last.endsWith('\\||')) {
					lines[lines.length - 1] = last.slice(0, -2);
					collapsed = true;
				}
			}
			const offset = out.length;
			inline(lines.join('\n'));
			push('blockquote', offset, collapsed ? { collapsed: true } : null);
			if (j <= src.length) out += '\n';
			if (j >= src.length) break;
			const next = (src[j] === '>' || src.startsWith('**>', j)) ? j : nextQuote(j);
			inline(src.slice(j, next));
			i = next;
		}
		return { text: out, entities: sortEntities(entities) };
	};

	const HTML_ENTITIES = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", nbsp: '\u00a0' };
	const decodeHtml = (s) => s.replace(/&(#[xX][0-9a-fA-F]+|#\d+|[a-zA-Z]+);/g, (all, code) => {
		if (code[0] === '#') {
			const hex = (code[1] === 'x' || code[1] === 'X');
			const n = parseInt(code.slice(hex ? 2 : 1), hex ? 16 : 10);
			try {
				return String.fromCodePoint(n);
			} catch (e) {
				return all;
			}
		}
		const named = HTML_ENTITIES[code.toLowerCase()];
		return (named === undefined) ? all : named;
	});
	const parseAttributes = (s) => {
		const result = {};
		const re = /([^\s=\/>]+)(?:\s*=\s*("([^"]*)"|'([^']*)'|[^\s>]+))?/g;
		let m;
		while ((m = re.exec(s))) {
			const value = (m[3] !== undefined) ? m[3] : (m[4] !== undefined) ? m[4] : (m[2] || '');
			result[m[1].toLowerCase()] = decodeHtml(value);
		}
		return result;
	};
	const SIMPLE_TAGS = {
		b: 'bold', strong: 'bold',
		i: 'italic', em: 'italic',
		u: 'underline', ins: 'underline',
		s: 'strikethrough', strike: 'strikethrough', del: 'strikethrough',
		'tg-spoiler': 'spoiler',
	};
	const parseHtml = (source) => {
		const src = String(source ?? '').replace(/\r\n?/g, '\n');
		// An unquoted value never starts with a quote: otherwise "x" matches
		// two ways and a long unclosed tag backtracks exponentially.
		const re = /<!--[\s\S]*?-->|<(\/?)([a-zA-Z][a-zA-Z0-9-]*)((?:\s+[^\s=\/>]+(?:\s*=\s*(?:"[^"]*"|'[^']*'|[^\s>"'][^\s>]*))?)*)\s*(\/?)>/g;
		let out = '';
		const entities = [];
		const stack = [];
		const push = (type, offset, extra) => {
			const length = out.length - offset;
			if (length > 0) {
				entities.push(Object.assign({ type, offset, length }, extra || {}));
			}
		};
		const finish = (open) => {
			const { name, offset, attributes } = open;
			if (SIMPLE_TAGS[name]) {
				push(SIMPLE_TAGS[name], offset);
			} else if (name === 'span') {
				if (/(^|\s)tg-spoiler(\s|$)/.test(attributes.class || '')) push('spoiler', offset);
			} else if (name === 'a') {
				const url = (attributes.href || '').trim();
				const mention = /^tg:\/\/user\?id=(\d+)$/i.exec(url);
				if (mention) {
					push('textMention', offset, { userId: mention[1] });
				} else if (url) {
					push('textLink', offset, { url });
				}
			} else if (name === 'code') {
				const language = /(?:^|\s)language-([A-Za-z0-9_+\-#.]+)/.exec(attributes.class || '');
				const pre = stack.length ? stack[stack.length - 1] : null;
				if (pre && pre.name === 'pre') {
					if (language && !pre.language) pre.language = language[1];
				} else {
					push('code', offset);
				}
			} else if (name === 'pre') {
				const language = open.language || attributes.language || '';
				push('pre', offset, language ? { language } : null);
			} else if (name === 'blockquote') {
				push('blockquote', offset, ('expandable' in attributes) ? { collapsed: true } : null);
			} else if (name === 'tg-emoji') {
				const id = attributes['emoji-id'];
				if (/^\d+$/.test(id || '')) push('customEmoji', offset, { documentId: id });
			}
		};
		let last = 0;
		let m;
		while ((m = re.exec(src))) {
			out += decodeHtml(src.slice(last, m.index));
			last = re.lastIndex;
			if (!m[2]) continue;
			const closing = (m[1] === '/');
			const name = m[2].toLowerCase();
			if (name === 'br') {
				out += '\n';
				continue;
			}
			if (!closing) {
				if (m[4] !== '/') {
					stack.push({ name, offset: out.length, attributes: parseAttributes(m[3] || '') });
				}
				continue;
			}
			for (let k = stack.length - 1; k >= 0; --k) {
				if (stack[k].name === name) {
					const open = stack.splice(k, 1)[0];
					finish(open);
					break;
				}
			}
		}
		out += decodeHtml(src.slice(last));
		return { text: out, entities: sortEntities(entities) };
	};

	const sortEntities = (list) => list
		.map((e, index) => ({ e, index }))
		.sort((a, b) => (a.e.offset - b.e.offset)
			|| (b.e.length - a.e.length)
			|| (a.index - b.index))
		.map((x) => x.e);

	const parseText = (text, mode) => {
		const kind = String(mode || 'none').toLowerCase();
		if (kind === 'markdown' || kind === 'md') return parseMarkdown(text);
		if (kind === 'html') return parseHtml(text);
		if (kind === 'none' || kind === 'plain' || kind === 'text') {
			return { text: String(text ?? ''), entities: [] };
		}
		throw new TypeError('Unknown parse mode: ' + mode);
	};

	internal.messagesText = { parseMarkdown, parseHtml, parseText, matchUrl };
}
)DZG_JS") + QString::fromUtf8(R"DZG_JS(
{
	// Message text with entities -> HTML or Markdown.
	const escapeHtml = (s) => s
		.replace(/&/g, '&amp;')
		.replace(/</g, '&lt;')
		.replace(/>/g, '&gt;');
	const escapeAttribute = (s) => escapeHtml(String(s)).replace(/"/g, '&quot;');
	const matchUrl = internal.messagesText.matchUrl;
	// before: the output character in front of s; more: a marker follows
	// s, so its last '*' could pair with the marker's '**'.
	const escapeMarkdown = (s, lineStart, before, more) => {
		let result = '';
		for (let i = 0; i < s.length; ++i) {
			const url = matchUrl(s, i);
			if (url) {
				result += s.slice(i, i + url);
				i += url - 1;
				continue;
			}
			const c = s[i];
			const atLineStart = (i === 0) ? lineStart : (s[i - 1] === '\n');
			const prev = i ? s[i - 1] : before;
			const next = (i + 1 < s.length) ? s[i + 1] : (more ? c : '');
			if (c === '\\' || c === '`' || c === '[' || c === ']'
				|| ((c === '*' || c === '_' || c === '~' || c === '|')
					&& (prev === c || next === c))
				|| (c === '>' && atLineStart)) {
				result += '\\';
			}
			result += c;
		}
		return result;
	};
	const mentionUrl = (e) => 'tg://user?id=' + String(e.userId || '');
	const WRAPPERS = {
		html: {
			bold: () => ['<b>', '</b>'],
			italic: () => ['<i>', '</i>'],
			underline: () => ['<u>', '</u>'],
			strikethrough: () => ['<s>', '</s>'],
			spoiler: () => ['<tg-spoiler>', '</tg-spoiler>'],
			code: () => ['<code>', '</code>'],
			pre: (e) => (e.language
				? ['<pre><code class="language-' + escapeAttribute(e.language) + '">', '</code></pre>']
				: ['<pre>', '</pre>']),
			textLink: (e) => ['<a href="' + escapeAttribute(e.url || '') + '">', '</a>'],
			textMention: (e) => ['<a href="' + escapeAttribute(mentionUrl(e)) + '">', '</a>'],
			customEmoji: (e) => ['<tg-emoji emoji-id="' + escapeAttribute(e.documentId || '') + '">', '</tg-emoji>'],
			blockquote: (e) => [e.collapsed ? '<blockquote expandable>' : '<blockquote>', '</blockquote>'],
		},
		markdown: {
			bold: () => ['**', '**'],
			italic: () => ['__', '__'],
			strikethrough: () => ['~~', '~~'],
			spoiler: () => ['||', '||'],
			code: () => ['`', '`'],
			pre: (e) => ['```' + (e.language || '') + '\n', '\n```'],
			textLink: (e) => ['[', '](' + String(e.url || '').replace(/[()\\]/g, '\\$&') + ')'],
			textMention: (e) => ['[', '](' + mentionUrl(e) + ')'],
			customEmoji: (e) => ['![', '](tg://emoji?id=' + (e.documentId || '') + ')'],
			blockquote: (e) => [e.collapsed ? '**>' : '>', e.collapsed ? '||' : ''],
		},
	};
	const RAW = { code: true, pre: true };

	const formatText = (text, entities, mode) => {
		const source = String(text ?? '');
		const kind = (mode === 'md') ? 'markdown' : mode;
		const wrappers = WRAPPERS[kind];
		if (!wrappers) throw new TypeError('Unknown format: ' + mode);
		const html = (kind === 'html');
		const list = (Array.isArray(entities) ? entities : [])
			.filter((e) => e && wrappers[e.type] && e.length > 0 && e.offset >= 0)
			.map((e, index) => ({
				e,
				start: Math.min(e.offset, source.length),
				end: Math.min(e.offset + e.length, source.length),
				index,
			}))
			.filter((x) => x.end > x.start)
			.sort((a, b) => (a.start - b.start) || (b.end - a.end) || (a.index - b.index));
		const points = new Set([0, source.length]);
		for (const x of list) {
			points.add(x.start);
			points.add(x.end);
		}
		const boundaries = Array.from(points).sort((a, b) => a - b);
		const stack = [];
		let out = '';
		let next = 0;
		let pos = 0;
		const raw = () => stack.some((x) => RAW[x.e.type]);
		const quoted = () => stack.some((x) => x.e.type === 'blockquote');
		const open = (x) => {
			out += wrappers[x.e.type](x.e)[0];
			stack.push(x);
		};
		const close = () => {
			const x = stack.pop();
			out += wrappers[x.e.type](x.e)[1];
		};
		const write = (chunk, start) => {
			if (!chunk.length) return;
			if (html) {
				out += escapeHtml(chunk);
				return;
			}
			// A line start in the text, even after an opening marker:
			// '**' + '>' at a line start would parse as a quote.
			const lineStart = !start || source[start - 1] === '\n';
			const more = (start + chunk.length < source.length) || stack.length > 0;
			let part = raw()
				? chunk
				: escapeMarkdown(chunk, lineStart, out[out.length - 1], more);
			if (quoted()) part = part.replace(/\n/g, '\n>');
			out += part;
		};
		for (const point of boundaries) {
			write(source.slice(pos, point), pos);
			pos = point;
			while (stack.some((x) => x.end <= point)) {
				const reopen = [];
				while (stack.length && stack[stack.length - 1].end > point) {
					reopen.push(stack[stack.length - 1]);
					close();
				}
				close();
				while (reopen.length) open(reopen.pop());
			}
			while (next < list.length && list[next].start === point) {
				open(list[next++]);
			}
		}
		while (stack.length) close();
		return out;
	};

	Object.assign(internal.messagesText, { formatText });
	const parseText = internal.messagesText.parseText;
	Object.assign(dzg.messages, {
		parse(text, mode) {
			return parseText(text, mode === undefined ? 'markdown' : mode);
		},
		format(textOrMessage, mode, entities) {
			if (textOrMessage && typeof textOrMessage === 'object') {
				return formatText(textOrMessage.text, textOrMessage.entities, mode || 'html');
			}
			return formatText(textOrMessage, entities, mode || 'html');
		},
	});
}
)DZG_JS") + QString::fromUtf8(R"DZG_JS(
{
	const text = internal.messagesText;
	const toPeerId = internal.toPeerId;
	const request = internal.request;
	const kPage = 100;

	const toId = (value) => Number((value && typeof value === 'object') ? value.id : value) || 0;
	const toIds = (value) => (Array.isArray(value) ? value : [value])
		.map(toId)
		.filter((id) => Number.isFinite(id) && id !== 0);
	const toTime = (value) => {
		if (value instanceof Date) return Math.floor(value.getTime() / 1000);
		const n = Number(value) || 0;
		return (n > 1e11) ? Math.floor(n / 1000) : Math.floor(n);
	};
	const prepare = (body, o) => {
		const result = text.parseText(body, o.parse || 'none');
		if (Array.isArray(o.entities)) {
			result.entities = result.entities.concat(o.entities);
		}
		return result;
	};
	const sendOptions = (o) => ({
		replyTo: toId(o.replyTo),
		topicId: Number(o.topicId) || 0,
		silent: !!o.silent,
		runHooks: !!o.runHooks,
		scheduleDate: toTime(o.scheduleDate),
		noWebpage: !!o.noWebpage,
	});

	// Loads pages of up to 100 messages until `limit` is reached.
	// Each page is older than the previous one; the result is oldest first.
	const collect = async (load, limit, cursor) => {
		let left = Math.max(1, Math.floor(Number(limit) || 50));
		let result = [];
		let current = Object.assign({}, cursor);
		const seen = new Set();
		while (left > 0) {
			const page = await load(Math.min(kPage, left), current);
			const list = (page && page.messages) || [];
			const key = String(page && page.nextOffsetPeerId) + ':' + String(page && page.nextOffsetId);
			if (seen.has(key)) break; // The same page again: don't repeat it.
			result = list.concat(result);
			left -= list.length;
			if (!list.length || !page.nextOffsetId) break;
			if (page.count > 0 && result.length >= page.count) break;
			seen.add(key);
			// Next pages go by offsetId alone: offsetDate was for the first.
			current = Object.assign({}, current, {
				offsetId: page.nextOffsetId,
				offsetPeerId: page.nextOffsetPeerId || '',
				offsetRate: page.nextRate || 0,
				offsetDate: 0,
				addOffset: 0,
			});
		}
		return result;
	};
	const historyPage = (peerId, o) => (limit, cursor) => request(() => native.messages_history(peerId, {
		limit,
		offsetId: Number(cursor.offsetId) || 0,
		addOffset: Number(cursor.addOffset) || 0,
		offsetDate: toTime((cursor.offsetDate === undefined) ? o.offsetDate : cursor.offsetDate),
		minId: Number(o.minId) || 0,
		maxId: Number(o.maxId) || 0,
	}));
	const searchPage = (peerId, query, o) => (limit, cursor) => request(() => native.messages_search(peerId, String(query ?? ''), {
		limit,
		from: o.from ? toPeerId(o.from) : '',
		filter: o.filter ? String(o.filter) : '',
		topicId: Number(o.topicId) || 0,
		minDate: toTime(o.minDate),
		maxDate: toTime(o.maxDate),
		offsetId: Number(cursor.offsetId) || 0,
		addOffset: Number(cursor.addOffset) || 0,
		offsetRate: Number(cursor.offsetRate) || 0,
		offsetPeerId: cursor.offsetPeerId || '',
		folder: (o.folder === undefined || o.folder === null) ? -1 : Number(o.folder),
		type: o.type ? String(o.type) : '',
	}));

	Object.assign(dzg.messages, {
		send(peer, body, options) {
			const o = options || {};
			const prepared = prepare(body, o);
			return !!native.messages_send(toPeerId(peer), prepared.text, prepared.entities, sendOptions(o));
		},
		edit(peer, id, body, options) {
			const o = options || {};
			const prepared = prepare(body, o);
			return !!native.messages_edit(toPeerId(peer), toId(id), prepared.text, prepared.entities, {
				noWebpage: !!o.noWebpage,
			});
		},
		history(peer, options) {
			const o = options || {};
			const peerId = toPeerId(peer);
			const cursor = { offsetId: toId(o.offsetId), addOffset: Number(o.addOffset) || 0 };
			const limit = Math.max(1, Math.floor(Number(o.limit) || 50));
			if (cursor.addOffset < 0) {
				return historyPage(peerId, o)(Math.min(kPage, limit), cursor).then((page) => page.messages);
			}
			return collect(historyPage(peerId, o), limit, cursor);
		},
		async *iterate(peer, options) {
			const o = options || {};
			const peerId = toPeerId(peer);
			const load = historyPage(peerId, o);
			const batch = Math.min(kPage, Math.max(1, Number(o.batch) || kPage));
			let left = (o.limit === undefined) ? Infinity : Math.max(0, Number(o.limit) || 0);
			let cursor = { offsetId: toId(o.offsetId), addOffset: 0 };
			while (left > 0) {
				const page = await load(Math.min(batch, left), cursor);
				const list = (page && page.messages) || [];
				// No messages, or the same page again: don't repeat it.
				if (!list.length || page.nextOffsetId === cursor.offsetId) return;
				for (let i = list.length - 1; i >= 0 && left > 0; --i, --left) {
					yield list[i];
				}
				if (!page.nextOffsetId) return;
				cursor = { offsetId: page.nextOffsetId, offsetDate: 0, addOffset: 0 };
			}
		},
		search(peer, query, options) {
			const o = options || {};
			const peerId = (peer === null || peer === undefined) ? '' : toPeerId(peer);
			const cursor = { offsetId: toId(o.offsetId), addOffset: Number(o.addOffset) || 0 };
			return collect(searchPage(peerId, query, o), o.limit, cursor);
		},
		fetch(peer, ids) {
			const list = toIds(ids);
			return request(() => native.messages_fetch(toPeerId(peer), list))
				.then((found) => (Array.isArray(ids) ? found : (found[0] || null)));
		},
		forward(fromPeer, ids, toPeer, options) {
			const o = options || {};
			return request(() => native.messages_forward(toPeerId(fromPeer), toIds(ids), toPeerId(toPeer), {
				silent: !!o.silent,
				dropAuthor: !!o.dropAuthor,
				dropCaptions: !!o.dropCaptions,
				scheduleDate: toTime(o.scheduleDate),
				topicId: Number(o.topicId) || 0,
			})).then(() => undefined);
		},
		react(peer, id, emoji, options) {
			const o = options || {};
			const value = (emoji === null || emoji === undefined) ? '' : String(emoji);
			return request(() => native.messages_react(toPeerId(peer), toId(id), value, {
				addToRecent: !!o.addToRecent,
			})).then(() => undefined);
		},
		reactions(peer, id) {
			return native.messages_availableReactions(toPeerId(peer), toId(id)) || [];
		},
		pin(peer, id, options) {
			const o = options || {};
			return request(() => native.messages_pin(toPeerId(peer), toId(id), {
				silent: !!o.silent,
				unpin: !!o.unpin,
				oneSide: !!o.oneSide,
			})).then(() => undefined);
		},
		unpin(peer, id) {
			return dzg.messages.pin(peer, id, { unpin: true });
		},
		markRead(peer, options) {
			const o = options || {};
			return !!native.messages_markRead(toPeerId(peer), { tillId: toId(o.tillId) });
		},
		sendFile(peer, path, options) {
			const o = options || {};
			const paths = (Array.isArray(path) ? path : [path]).map((p) => String(p ?? '')).filter(Boolean);
			const caption = prepare(o.caption ?? '', o);
			return !!native.messages_sendFile(toPeerId(peer), paths, Object.assign(sendOptions(o), {
				caption: caption.text,
				captionEntities: caption.entities,
				asDocument: !!o.asDocument,
				album: (o.album === undefined) ? true : !!o.album,
				spoiler: !!o.spoiler,
			}));
		},
		download(peer, id, path) {
			return request(() => native.messages_download(toPeerId(peer), toId(id), (path === undefined || path === null) ? '' : String(path)));
		},
		link(peer, id, options) {
			const o = options || {};
			return native.messages_link(toPeerId(peer), toId(id), {
				thread: !!o.thread,
				private: !!o.private,
			}) || null;
		},
		clickButton(peer, id, row, column, options) {
			const byText = (typeof row === 'string');
			const o = (byText ? column : options) || {};
			return request(() => native.messages_clickButton(toPeerId(peer), toId(id),
				byText ? -1 : Number(row) || 0,
				byText ? -1 : Number(column) || 0,
				{ text: byText ? row : '', open: !!o.open }));
		},
	});
}
)DZG_JS");
	},
	.appStarted = [] {
		Manager::Get().lifetime().add([] {
			Waits().clear();
		});
	},
	.sessionStarted = [](not_null<Main::Session*> session) {
		session->changes().messageUpdates(
			Data::MessageUpdate::Flag::NewUnreadReaction
		) | rpl::on_next([](const Data::MessageUpdate &update) {
			if (Manager::Exists()) {
				Manager::Get().emitEvent(
					u"message.reaction"_q,
					SerializeMessage(update.item));
			}
		}, session->lifetime());
	},
	.pluginUnloaded = [](const QString &pluginId) {
		auto &waits = Waits();
		for (auto i = begin(waits); i != end(waits);) {
			if (i->second.pluginId == pluginId) {
				i = waits.erase(i);
			} else {
				++i;
			}
		}
	},
});

} // namespace Damza::Plugins
