// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
//
// Module "chats": chat list, folders, join/leave, mute, archive, pin,
// typing, members and full info. Events: chat.typing, chat.unread.
#include "damza/plugins/api/api_chats.h"

#include "api/api_chat_invite.h"
#include "api/api_chat_participants.h"
#include "api/api_send_progress.h"
#include "apiwrap.h"
#include "ayu/ayu_settings.h"
#include "base/call_delayed.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "damza/plugins/api/api_chats_common.h"
#include "damza/plugins/plugin_api.h"
#include "damza/plugins/plugin_bridge.h"
#include "damza/plugins/plugin_data.h"
#include "damza/plugins/plugin_manager.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_chat_filters.h"
#include "data/data_folder.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/notify/data_notify_settings.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_unread_things.h"
#include "main/main_session.h"
#include "menu/menu_mark_as_read.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

#include <QtCore/QRegularExpression>

namespace Damza::Plugins {
namespace {

using ChatsApi::Async;
using ChatsApi::Pending;
using SessionPtr = not_null<Main::Session*>;

constexpr auto kTypingRepeat = crl::time(4000);
constexpr auto kTypingExpire = crl::time(6500);
constexpr auto kTypingForget = crl::time(60000);
constexpr auto kTypingMapLimit = 1024;
constexpr auto kUnreadDelay = crl::time(300);
constexpr auto kMembersPage = 200;
constexpr auto kMembersMax = 10000;
constexpr auto kDefaultListLimit = 100;

[[nodiscard]] QString InviteHash(const QString &value) {
	static const auto link = QRegularExpression(
		u"^(?:https?://)?(?:www\\.)?(?:t\\.me|telegram\\.me|telegram\\.dog)"
		u"/(?:joinchat/|\\+)([A-Za-z0-9_\\-]+)"_q,
		QRegularExpression::CaseInsensitiveOption);
	static const auto deep = QRegularExpression(
		u"^tg://join\\?(?:.*&)?invite=([A-Za-z0-9_\\-]+)"_q,
		QRegularExpression::CaseInsensitiveOption);
	static const auto plain = QRegularExpression(
		u"^\\+([A-Za-z0-9_\\-]*[A-Za-z_\\-][A-Za-z0-9_\\-]*)$"_q);
	const auto text = value.trimmed();
	for (const auto regex : { &link, &deep, &plain }) {
		const auto match = regex->match(text);
		if (match.hasMatch()) {
			return match.captured(1);
		}
	}
	return QString();
}

[[nodiscard]] QString NormalizeUsername(const QString &value) {
	static const auto deep = QRegularExpression(
		u"^tg://resolve\\?(?:.*&)?domain=([A-Za-z0-9_]+)"_q,
		QRegularExpression::CaseInsensitiveOption);
	static const auto link = QRegularExpression(
		u"^(?:https?://)?(?:www\\.)?(?:t\\.me|telegram\\.me|telegram\\.dog)"
		u"/(?:s/)?([A-Za-z0-9_]+)"_q,
		QRegularExpression::CaseInsensitiveOption);
	const auto text = value.trimmed();
	for (const auto regex : { &deep, &link }) {
		const auto match = regex->match(text);
		if (match.hasMatch()) {
			return match.captured(1);
		}
	}
	return text.startsWith('@') ? text.mid(1).trimmed() : text;
}

[[nodiscard]] QString PhoneNumber(const QString &value) {
	static const auto phone = QRegularExpression(u"^\\+?[0-9]{5,}$"_q);
	auto digits = value.trimmed();
	digits.remove(QRegularExpression(u"[\\s\\-()]"_q));
	return phone.match(digits).hasMatch()
		? (digits.startsWith('+') ? digits.mid(1) : digits)
		: QString();
}

[[nodiscard]] PeerData *ProcessResolved(
		SessionPtr session,
		const MTPcontacts_ResolvedPeer &result) {
	const auto &data = result.data();
	session->data().processUsers(data.vusers());
	session->data().processChats(data.vchats());
	return session->data().peerLoaded(peerFromMTP(data.vpeer()));
}

[[nodiscard]] PeerData *FirstChat(
		SessionPtr session,
		const MTPUpdates &updates) {
	const auto find = [&](const MTPVector<MTPChat> &chats) -> PeerData* {
		for (const auto &chat : chats.v) {
			const auto id = chat.match([](const MTPDchat &data) {
				return peerFromChat(data.vid().v);
			}, [](const MTPDchannel &data) {
				return peerFromChannel(data.vid().v);
			}, [](const auto &) {
				return PeerId(0);
			});
			if (const auto peer = session->data().peerLoaded(id)) {
				return peer;
			}
		}
		return nullptr;
	};
	return updates.match([&](const MTPDupdates &data) {
		return find(data.vchats());
	}, [&](const MTPDupdatesCombined &data) {
		return find(data.vchats());
	}, [](const auto &) -> PeerData* {
		return nullptr;
	});
}

[[nodiscard]] QVariant PeerOrNull(PeerData *peer) {
	return peer ? QVariant(SerializePeer(peer)) : QVariant();
}

void AppendEntryFields(
		QVariantMap &result,
		not_null<History*> history,
		FilterId filterId) {
	result.insert(u"unread"_q, history->unreadCount());
	result.insert(u"unreadMark"_q, history->unreadMark());
	result.insert(u"mentions"_q, history->unreadMentions().count(0));
	result.insert(u"reactions"_q, history->unreadReactions().count(0));
	result.insert(u"pinned"_q, history->isPinnedDialog(filterId));
	result.insert(u"muted"_q, history->muted());
	result.insert(u"archived"_q, history->folder() != nullptr);
	const auto last = history->lastMessage();
	result.insert(
		u"lastMessage"_q,
		last ? QVariant(SerializeMessage(last)) : QVariant());
}

[[nodiscard]] QVariantMap SerializeEntry(
		not_null<History*> history,
		FilterId filterId) {
	auto result = SerializePeer(history->peer);
	AppendEntryFields(result, history, filterId);
	return result;
}

[[nodiscard]] QVariantList ListChats(
		SessionPtr session,
		const QVariantMap &options) {
	const auto folder = options.value(u"folder"_q);
	const auto limit = options.contains(u"limit"_q)
		? int(options.value(u"limit"_q).toDouble())
		: kDefaultListLimit;
	const auto unreadOnly = options.value(u"unreadOnly"_q).toBool();
	const auto types = options.value(u"types"_q).toStringList();
	const auto archived = (folder.toString() == u"archive"_q);
	const auto filterId = archived ? FilterId() : FilterId(folder.toDouble());
	auto list = (Dialogs::MainList*)nullptr;
	if (archived) {
		const auto archive = session->data().folderLoaded(Data::Folder::kId);
		if (!archive) {
			return {};
		}
		list = session->data().chatsList(archive);
	} else if (filterId) {
		const auto &filters = session->data().chatsFilters().list();
		if (!ranges::contains(filters, filterId, &Data::ChatFilter::id)) {
			return {};
		}
		list = session->data().chatsFilters().chatsList(filterId);
	} else {
		list = session->data().chatsList();
	}
	auto result = QVariantList();
	for (const auto &row : list->indexed()->all()) {
		if (limit > 0 && result.size() >= limit) {
			break;
		}
		const auto history = row->history();
		if (!history
			|| (unreadOnly
				&& !history->unreadCount()
				&& !history->unreadMark())) {
			continue;
		}
		auto entry = SerializePeer(history->peer);
		if (!types.isEmpty()
			&& !types.contains(entry.value(u"type"_q).toString())) {
			continue;
		}
		AppendEntryFields(entry, history, filterId);
		result.push_back(std::move(entry));
	}
	return result;
}

[[nodiscard]] QVariantList ListFolders(SessionPtr session) {
	auto result = QVariantList();
	for (const auto &filter : session->data().chatsFilters().list()) {
		if (!filter.id()) {
			continue;
		}
		result.push_back(QVariantMap{
			{ u"id"_q, int(filter.id()) },
			{ u"title"_q, filter.titleText().text },
			{ u"emoji"_q, filter.iconEmoji() },
			{ u"chatlist"_q, filter.chatlist() },
		});
	}
	return result;
}

[[nodiscard]] std::optional<Api::SendProgressType> ProgressType(
		const QString &action) {
	using Type = Api::SendProgressType;
	static const auto map = base::flat_map<QString, Type>{
		{ u"typing"_q, Type::Typing },
		{ u"record_video"_q, Type::RecordVideo },
		{ u"upload_video"_q, Type::UploadVideo },
		{ u"record_voice"_q, Type::RecordVoice },
		{ u"upload_voice"_q, Type::UploadVoice },
		{ u"record_round"_q, Type::RecordRound },
		{ u"upload_round"_q, Type::UploadRound },
		{ u"upload_photo"_q, Type::UploadPhoto },
		{ u"upload_document"_q, Type::UploadFile },
		{ u"choose_location"_q, Type::ChooseLocation },
		{ u"choose_contact"_q, Type::ChooseContact },
		{ u"choose_sticker"_q, Type::ChooseSticker },
		{ u"play_game"_q, Type::PlayGame },
	};
	const auto i = map.find(action);
	return (i != end(map)) ? std::make_optional(i->second) : std::nullopt;
}

[[nodiscard]] QString ActionName(const MTPSendMessageAction &action) {
	switch (action.type()) {
	case mtpc_sendMessageTypingAction: return u"typing"_q;
	case mtpc_sendMessageCancelAction: return u"cancel"_q;
	case mtpc_sendMessageRecordVideoAction: return u"record_video"_q;
	case mtpc_sendMessageUploadVideoAction: return u"upload_video"_q;
	case mtpc_sendMessageRecordAudioAction: return u"record_voice"_q;
	case mtpc_sendMessageUploadAudioAction: return u"upload_voice"_q;
	case mtpc_sendMessageRecordRoundAction: return u"record_round"_q;
	case mtpc_sendMessageUploadRoundAction: return u"upload_round"_q;
	case mtpc_sendMessageUploadPhotoAction: return u"upload_photo"_q;
	case mtpc_sendMessageUploadDocumentAction: return u"upload_document"_q;
	case mtpc_sendMessageGeoLocationAction: return u"choose_location"_q;
	case mtpc_sendMessageChooseContactAction: return u"choose_contact"_q;
	case mtpc_sendMessageChooseStickerAction: return u"choose_sticker"_q;
	case mtpc_sendMessageGamePlayAction: return u"play_game"_q;
	case mtpc_sendMessageHistoryImportAction: return u"import_history"_q;
	}
	return QString();
}

bool SetTyping(
		not_null<PeerData*> peer,
		const QString &action,
		MsgId topMsgId) {
	const auto history = peer->owner().history(peer);
	auto &manager = peer->session().sendProgressManager();
	if (action != u"cancel"_q) {
		const auto type = ProgressType(action);
		if (!type) {
			return false;
		}
		manager.update(history, topMsgId, *type);
		return true;
	}
	using Type = Api::SendProgressType;
	for (auto i = 0; i <= int(Type::Speaking); ++i) {
		if (Type(i) != Type::Speaking) {
			manager.update(history, topMsgId, Type(i), -1);
		}
	}
	const auto session = &peer->session();
	if (peer->isSelf()
		|| (peer->isChannel() && !peer->isMegagroup())
		|| !AyuSettings::ghost(session).sendUploadProgress()) {
		return true;
	}
	using Flag = MTPmessages_SetTyping::Flag;
	session->api().request(MTPmessages_SetTyping(
		MTP_flags(topMsgId ? Flag::f_top_msg_id : Flag(0)),
		peer->input(),
		MTP_int(topMsgId),
		MTP_sendMessageCancelAction()
	)).send();
	return true;
}

bool Mute(not_null<PeerData*> peer, int seconds) {
	auto &settings = peer->owner().notifySettings();
	if (seconds < 0) {
		settings.update(peer, { .unmute = true });
	} else if (!seconds) {
		settings.update(peer, { .forever = true });
	} else {
		settings.update(peer, { .period = seconds });
	}
	return true;
}

bool Archive(not_null<PeerData*> peer, bool archived) {
	const auto history = peer->owner().history(peer);
	if (Window::IsArchived(history) == archived) {
		return true;
	} else if (archived && !Window::CanArchive(history, peer)) {
		return false;
	}
	peer->session().api().toggleHistoryArchived(history, archived, [] {});
	return true;
}

bool Pin(not_null<PeerData*> peer, bool pinned, FilterId filterId) {
	const auto history = peer->owner().history(peer);
	if (history->isPinnedDialog(filterId) == pinned) {
		return true;
	} else if (filterId) {
		const auto &list = peer->owner().chatsFilters().list();
		const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
		if (i == end(list) || !i->contains(history)) {
			return false;
		}
	}
	const auto controller = Manager::Get().activeController();
	if (!controller || (&controller->session() != &peer->session())) {
		return false;
	}
	Window::TogglePinnedThread(controller, history, filterId, nullptr);
	return true;
}

bool MarkUnread(not_null<PeerData*> peer, bool unread) {
	const auto history = peer->owner().history(peer);
	auto &histories = peer->owner().histories();
	if (unread) {
		if (!history->unreadMark() && !history->unreadCount()) {
			histories.changeDialogUnreadMark(history, true);
		}
		return true;
	}
	MarkAsReadMenu::MarkAsReadThread(history);
	if (history->unreadMark()) {
		histories.changeDialogUnreadMark(history, false);
	}
	return true;
}

void Resolve(SessionPtr session, const QString &query, Pending pending) {
	const auto fail = [=](const MTP::Error &error) {
		const auto &type = error.type();
		if (type == u"USERNAME_NOT_OCCUPIED"_q
			|| type == u"USERNAME_INVALID"_q
			|| type == u"PHONE_NOT_OCCUPIED"_q
			|| type == u"PHONE_NUMBER_INVALID"_q) {
			pending.resolve(QVariant());
		} else {
			pending.reject(type);
		}
	};
	const auto done = [=](const MTPcontacts_ResolvedPeer &result) {
		pending.resolve(PeerOrNull(ProcessResolved(session, result)));
	};
	if (const auto phone = PhoneNumber(query); !phone.isEmpty()) {
		session->api().request(MTPcontacts_ResolvePhone(
			MTP_string(phone)
		)).done(done).fail(fail).send();
		return;
	}
	const auto username = NormalizeUsername(query);
	if (username.isEmpty()) {
		pending.resolve(QVariant());
		return;
	}
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(username),
		MTP_string()
	)).done(done).fail(fail).send();
}

void HandleJoinResult(
		SessionPtr session,
		const MTPmessages_ChatInviteJoinResult &result,
		Fn<void(const MTPUpdates &)> done,
		Pending pending) {
	result.match([&](const MTPDmessages_chatInviteJoinResultOk &data) {
		done(data.vupdates());
	}, [&](const MTPDmessages_chatInviteJoinResultWebView &data) {
		const auto controller = Manager::Get().activeController();
		if (controller && (&controller->session() == session.get())) {
			Api::ProcessChatInviteJoinResult(
				session,
				controller->uiShow(),
				result,
				[=](const MTPUpdates &updates) {
					session->api().applyUpdates(updates);
				},
				base::make_weak(controller));
		}
		pending.reject(u"JOIN_VERIFICATION_REQUIRED"_q);
	});
}

void JoinChannel(
		SessionPtr session,
		not_null<ChannelData*> channel,
		Pending pending) {
	if (channel->amIn()) {
		pending.resolve(SerializePeer(channel));
		return;
	}
	session->api().request(MTPchannels_JoinChannel(
		channel->inputChannel()
	)).done([=](const MTPmessages_ChatInviteJoinResult &result) {
		HandleJoinResult(session, result, [=](const MTPUpdates &updates) {
			session->api().applyUpdates(updates);
			pending.resolve(SerializePeer(channel));
		}, pending);
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"USER_ALREADY_PARTICIPANT"_q) {
			pending.resolve(SerializePeer(channel));
		} else {
			pending.reject(error.type());
		}
	}).send();
}

void JoinPeer(SessionPtr session, not_null<PeerData*> peer, Pending pending) {
	if (const auto channel = peer->asChannel()) {
		JoinChannel(session, channel, pending);
	} else if (peer->isChat() && !peer->asChat()->amIn()) {
		pending.reject(u"Basic groups can be joined only by invite link"_q);
	} else {
		pending.resolve(SerializePeer(peer));
	}
}

void CheckJoinedInvite(SessionPtr session, QString hash, Pending pending) {
	session->api().request(MTPmessages_CheckChatInvite(
		MTP_string(hash)
	)).done([=](const MTPChatInvite &result) {
		result.match([&](const MTPDchatInviteAlready &data) {
			pending.resolve(
				SerializePeer(session->data().processChat(data.vchat())));
		}, [&](const MTPDchatInvitePeek &data) {
			pending.resolve(
				SerializePeer(session->data().processChat(data.vchat())));
		}, [&](const auto &) {
			pending.reject(u"USER_ALREADY_PARTICIPANT"_q);
		});
	}).fail([=](const MTP::Error &error) {
		pending.reject(error.type());
	}).send();
}

void JoinByInvite(SessionPtr session, QString hash, Pending pending) {
	session->api().request(MTPmessages_ImportChatInvite(
		MTP_string(hash)
	)).done([=](const MTPmessages_ChatInviteJoinResult &result) {
		HandleJoinResult(session, result, [=](const MTPUpdates &updates) {
			session->api().applyUpdates(updates);
			if (const auto peer = FirstChat(session, updates)) {
				pending.resolve(SerializePeer(peer));
			} else {
				CheckJoinedInvite(session, hash, pending);
			}
		}, pending);
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"USER_ALREADY_PARTICIPANT"_q) {
			CheckJoinedInvite(session, hash, pending);
		} else {
			pending.reject(error.type());
		}
	}).send();
}

void Join(SessionPtr session, const QString &target, Pending pending) {
	if (const auto hash = InviteHash(target); !hash.isEmpty()) {
		JoinByInvite(session, hash, pending);
		return;
	} else if (const auto known = ResolvePeer(session, target)) {
		JoinPeer(session, known, pending);
		return;
	}
	const auto username = NormalizeUsername(target);
	if (username.isEmpty()) {
		pending.reject(u"Empty username"_q);
		return;
	}
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(username),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		if (const auto peer = ProcessResolved(session, result)) {
			JoinPeer(session, peer, pending);
		} else {
			pending.reject(u"USERNAME_NOT_OCCUPIED"_q);
		}
	}).fail([=](const MTP::Error &error) {
		pending.reject(error.type());
	}).send();
}

void Leave(SessionPtr session, not_null<PeerData*> peer, Pending pending) {
	const auto done = [=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		pending.resolve(QVariant());
	};
	const auto fail = [=](const MTP::Error &error) {
		if (error.type() == u"USER_NOT_PARTICIPANT"_q) {
			pending.resolve(QVariant());
		} else {
			pending.reject(error.type());
		}
	};
	if (const auto channel = peer->asChannel()) {
		session->api().request(MTPchannels_LeaveChannel(
			channel->inputChannel()
		)).done(done).fail(fail).send();
	} else if (const auto chat = peer->asChat()) {
		session->api().request(MTPmessages_DeleteChatUser(
			MTP_flags(0),
			chat->inputChat(),
			session->user()->inputUser()
		)).done(done).fail(fail).send();
	} else {
		pending.reject(u"Only groups and channels can be left"_q);
	}
}

[[nodiscard]] QString RoleName(Api::ChatParticipant::Type type) {
	using Type = Api::ChatParticipant::Type;
	switch (type) {
	case Type::Creator: return u"creator"_q;
	case Type::Admin: return u"admin"_q;
	case Type::Member: return u"member"_q;
	case Type::Restricted: return u"restricted"_q;
	case Type::Left: return u"left"_q;
	case Type::Banned: return u"banned"_q;
	}
	return u"member"_q;
}

[[nodiscard]] bool MatchesQuery(not_null<PeerData*> peer, const QString &q) {
	return q.isEmpty()
		|| peer->name().toLower().contains(q)
		|| peer->username().toLower().contains(q);
}

void LoadChannelMembers(
		SessionPtr session,
		not_null<ChannelData*> channel,
		QString query,
		int limit,
		int offset,
		std::shared_ptr<QVariantList> collected,
		Pending pending) {
	const auto take = std::min(kMembersPage, limit - int(collected->size()));
	const auto filter = query.isEmpty()
		? MTP_channelParticipantsRecent()
		: MTP_channelParticipantsSearch(MTP_string(query));
	session->api().request(MTPchannels_GetParticipants(
		channel->inputChannel(),
		filter,
		MTP_int(offset),
		MTP_int(take),
		MTP_long(0) // hash
	)).done([=](const MTPchannels_ChannelParticipants &result) {
		if (!pending.alive()) {
			return;
		}
		result.match([&](const MTPDchannels_channelParticipants &data) {
			const auto parsed = Api::ChatParticipants::Parse(channel, data);
			for (const auto &participant : parsed.list) {
				const auto peer = session->data().peerLoaded(participant.id());
				if (!peer) {
					continue;
				}
				auto entry = SerializePeer(peer);
				entry.insert(u"role"_q, RoleName(participant.type()));
				if (!participant.rank().isEmpty()) {
					entry.insert(u"rank"_q, participant.rank());
				}
				collected->push_back(std::move(entry));
			}
			const auto received = int(data.vparticipants().v.size());
			const auto next = offset + received;
			if (received < take
				|| int(collected->size()) >= limit
				|| next >= parsed.availableCount) {
				pending.resolve(*collected);
			} else {
				LoadChannelMembers(
					session,
					channel,
					query,
					limit,
					next,
					collected,
					pending);
			}
		}, [&](const MTPDchannels_channelParticipantsNotModified &) {
			pending.resolve(*collected);
		});
	}).fail([=](const MTP::Error &error) {
		pending.reject(error.type());
	}).send();
}

[[nodiscard]] QVariantList ChatMembers(
		not_null<ChatData*> chat,
		const QString &query,
		int limit) {
	auto result = QVariantList();
	for (const auto &user : chat->participants) {
		if (!MatchesQuery(user, query)) {
			continue;
		}
		auto entry = SerializePeer(user);
		const auto role = (peerToUser(user->id) == chat->creator)
			? u"creator"_q
			: chat->admins.contains(user)
			? u"admin"_q
			: u"member"_q;
		entry.insert(u"role"_q, role);
		const auto rank = chat->memberRanks.find(peerToUser(user->id));
		if (rank != end(chat->memberRanks) && !rank->second.isEmpty()) {
			entry.insert(u"rank"_q, rank->second);
		}
		result.push_back(std::move(entry));
		if (int(result.size()) >= limit) {
			break;
		}
	}
	return result;
}

void Members(
		SessionPtr session,
		not_null<PeerData*> peer,
		const QVariantMap &options,
		Pending pending) {
	const auto limit = std::clamp(
		options.contains(u"limit"_q)
			? int(options.value(u"limit"_q).toDouble())
			: kMembersPage,
		1,
		kMembersMax);
	const auto query = options.value(u"query"_q).toString().trimmed();
	if (const auto channel = peer->asChannel()) {
		LoadChannelMembers(
			session,
			channel,
			query,
			limit,
			0,
			std::make_shared<QVariantList>(),
			pending);
	} else if (const auto chat = peer->asChat()) {
		const auto lower = query.toLower();
		if (!chat->participants.empty()) {
			pending.resolve(ChatMembers(chat, lower, limit));
			return;
		}
		session->api().request(MTPmessages_GetFullChat(
			chat->inputChat()
		)).done([=](const MTPmessages_ChatFull &result) {
			session->api().processFullPeer(chat, result);
			pending.resolve(ChatMembers(chat, lower, limit));
		}).fail([=](const MTP::Error &error) {
			pending.reject(error.type());
		}).send();
	} else {
		pending.reject(u"Members are available only for groups and channels"_q);
	}
}

[[nodiscard]] QVariantMap SerializeInfo(
		not_null<PeerData*> peer,
		int onlineCount) {
	auto result = QVariantMap{
		{ u"peer"_q, SerializePeer(peer) },
		{ u"about"_q, peer->about() },
		{ u"membersCount"_q, 0 },
		{ u"onlineCount"_q, onlineCount },
		{ u"inviteLink"_q, QString() },
		{ u"linkedChat"_q, QVariant() },
		{ u"slowmodeSeconds"_q, 0 },
		{ u"amIn"_q, true },
		{ u"isCreator"_q, false },
		{ u"isAdmin"_q, false },
		{ u"isForum"_q, peer->isForum() },
		{ u"isVerified"_q, peer->isVerified() },
		{ u"isScam"_q, peer->isScam() },
		{ u"isFake"_q, peer->isFake() },
	};
	if (const auto channel = peer->asChannel()) {
		result.insert(u"membersCount"_q, channel->membersCount());
		result.insert(u"inviteLink"_q, channel->inviteLink());
		result.insert(u"linkedChat"_q, PeerOrNull(channel->discussionLink()));
		result.insert(u"slowmodeSeconds"_q, channel->slowmodeSeconds());
		result.insert(u"amIn"_q, channel->amIn());
		result.insert(u"isCreator"_q, channel->amCreator());
		result.insert(
			u"isAdmin"_q,
			channel->amCreator() || channel->hasAdminRights());
	} else if (const auto chat = peer->asChat()) {
		result.insert(
			u"membersCount"_q,
			std::max(chat->count, int(chat->participants.size())));
		result.insert(u"inviteLink"_q, chat->inviteLink());
		result.insert(u"amIn"_q, chat->amIn());
		result.insert(u"isCreator"_q, chat->amCreator());
		result.insert(
			u"isAdmin"_q,
			chat->amCreator() || chat->hasAdminRights());
	}
	return result;
}

[[nodiscard]] int ChatOnlineCount(not_null<ChatData*> chat) {
	const auto now = base::unixtime::now();
	return int(ranges::count_if(chat->participants, [&](const auto &user) {
		return user->lastseen().isOnline(now);
	}));
}

void Info(SessionPtr session, not_null<PeerData*> peer, Pending pending) {
	const auto fail = [=](const MTP::Error &error) {
		pending.reject(error.type());
	};
	if (const auto user = peer->asUser()) {
		ChatsApi::RequestUserFull(user, [=] {
			pending.resolve(SerializeInfo(user, 0));
		}, [=](const QString &error) {
			pending.reject(error);
		});
	} else if (const auto chat = peer->asChat()) {
		session->api().request(MTPmessages_GetFullChat(
			chat->inputChat()
		)).done([=](const MTPmessages_ChatFull &result) {
			session->api().processFullPeer(chat, result);
			pending.resolve(SerializeInfo(chat, ChatOnlineCount(chat)));
		}).fail(fail).send();
	} else if (const auto channel = peer->asChannel()) {
		session->api().request(MTPchannels_GetFullChannel(
			channel->inputChannel()
		)).done([=](const MTPmessages_ChatFull &result) {
			const auto online = result.data().vfull_chat().match([](
					const MTPDchannelFull &data) {
				return data.vonline_count().value_or_empty();
			}, [](const auto &) {
				return 0;
			});
			session->api().processFullPeer(channel, result);
			pending.resolve(SerializeInfo(channel, online));
		}).fail(fail).send();
	} else {
		pending.reject(u"Unknown peer type"_q);
	}
}

[[nodiscard]] std::vector<not_null<UserData*>> UsersByIds(
		SessionPtr session,
		const QVariantList &ids) {
	auto result = std::vector<not_null<UserData*>>();
	for (const auto &id : ids) {
		const auto peer = ResolvePeer(session, id.toString());
		if (const auto user = peer ? peer->asUser() : nullptr) {
			if (!user->isSelf()) {
				result.push_back(user);
			}
		}
	}
	return result;
}

[[nodiscard]] QVector<MTPInputUser> InputUsers(
		SessionPtr session,
		const QVariantList &ids) {
	auto result = QVector<MTPInputUser>();
	for (const auto &user : UsersByIds(session, ids)) {
		result.push_back(user->inputUser());
	}
	return result;
}

[[nodiscard]] QVariantList MissingInvitees(
		const MTPmessages_InvitedUsers &result) {
	auto list = QVariantList();
	for (const auto &missing : result.data().vmissing_invitees().v) {
		list.push_back(PeerIdToString(
			peerFromUser(missing.data().vuser_id().v)));
	}
	return list;
}

void InviteToChannel(
		SessionPtr session,
		not_null<ChannelData*> channel,
		QVector<MTPInputUser> users,
		Fn<void(QVariantList missing)> done,
		Fn<void(const QString &error)> fail) {
	if (users.isEmpty()) {
		done({});
		return;
	}
	session->api().request(MTPchannels_InviteToChannel(
		channel->inputChannel(),
		MTP_vector<MTPInputUser>(users)
	)).done([=](const MTPmessages_InvitedUsers &result) {
		session->api().applyUpdates(result.data().vupdates());
		done(MissingInvitees(result));
	}).fail([=](const MTP::Error &error) {
		fail(error.type());
	}).send();
}

void Create(SessionPtr session, const QVariantMap &options, Pending pending) {
	const auto title = options.value(u"title"_q).toString().trimmed();
	const auto about = options.value(u"about"_q).toString();
	const auto type = options.value(u"type"_q).toString();
	const auto users = InputUsers(session, options.value(u"users"_q).toList());
	const auto fail = [=](const MTP::Error &error) {
		pending.reject(error.type());
	};
	if (title.isEmpty()) {
		pending.reject(u"Title is required"_q);
		return;
	} else if (type == u"group"_q) {
		session->api().request(MTPmessages_CreateChat(
			MTP_flags(0),
			MTP_vector<MTPInputUser>(users),
			MTP_string(title),
			MTPint() // ttl_period
		)).done([=](const MTPmessages_InvitedUsers &result) {
			const auto &updates = result.data().vupdates();
			session->api().applyUpdates(updates);
			const auto peer = FirstChat(session, updates);
			if (!peer) {
				pending.reject(u"Created, but the chat is unknown"_q);
				return;
			} else if (!about.isEmpty()) {
				session->api().request(MTPmessages_EditChatAbout(
					peer->input(),
					MTP_string(about)
				)).done([=] {
					peer->setAbout(about);
				}).send();
			}
			pending.resolve(SerializePeer(peer));
		}).fail(fail).send();
		return;
	} else if (type != u"supergroup"_q && type != u"channel"_q) {
		pending.reject(u"Unknown chat type: "_q + type);
		return;
	}
	using Flag = MTPchannels_CreateChannel::Flag;
	const auto flags = ((type == u"channel"_q)
		? Flag::f_broadcast
		: Flag::f_megagroup)
		| (options.value(u"forum"_q).toBool() ? Flag::f_forum : Flag(0));
	session->api().request(MTPchannels_CreateChannel(
		MTP_flags(flags),
		MTP_string(title),
		MTP_string(about),
		MTPInputGeoPoint(), // geo_point
		MTPstring(), // address
		MTPint() // ttl_period
	)).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		const auto peer = FirstChat(session, result);
		const auto channel = peer ? peer->asChannel() : nullptr;
		if (!channel) {
			pending.reject(u"Created, but the chat is unknown"_q);
			return;
		}
		const auto finish = [=] {
			pending.resolve(SerializePeer(channel));
		};
		InviteToChannel(session, channel, users, [=](QVariantList) {
			finish();
		}, [=](const QString &) {
			finish();
		});
	}).fail(fail).send();
}

void AddMembers(
		SessionPtr session,
		not_null<PeerData*> peer,
		QVariantList ids,
		Pending pending) {
	const auto users = UsersByIds(session, ids);
	if (users.empty()) {
		pending.reject(u"No users to add"_q);
		return;
	}
	const auto done = [=](const QVariantList &missing) {
		pending.resolve(QVariantMap{
			{ u"added"_q, int(users.size()) - int(missing.size()) },
			{ u"missing"_q, missing },
		});
	};
	if (const auto channel = peer->asChannel()) {
		const auto inputs = InputUsers(session, ids);
		InviteToChannel(session, channel, inputs, done, [=](
				const QString &error) {
			pending.reject(error);
		});
	} else if (const auto chat = peer->asChat()) {
		const auto missing = std::make_shared<QVariantList>();
		const auto left = std::make_shared<int>(int(users.size()));
		const auto error = std::make_shared<QString>();
		const auto finished = [=] {
			if (--*left) {
				return;
			} else if (int(missing->size()) == int(users.size())
				&& !error->isEmpty()) {
				pending.reject(*error);
			} else {
				done(*missing);
			}
		};
		for (const auto &user : users) {
			session->api().request(MTPmessages_AddChatUser(
				chat->inputChat(),
				user->inputUser(),
				MTP_int(100) // fwd_limit
			)).done([=](const MTPmessages_InvitedUsers &result) {
				session->api().applyUpdates(result.data().vupdates());
				for (const auto &id : MissingInvitees(result)) {
					missing->push_back(id);
				}
				finished();
			}).fail([=](const MTP::Error &e) {
				*error = e.type();
				missing->push_back(PeerIdToString(user->id));
				finished();
			}).send();
		}
	} else {
		pending.reject(u"Members can be added only to groups and channels"_q);
	}
}

void Kick(
		SessionPtr session,
		not_null<PeerData*> peer,
		QString userId,
		bool ban,
		Pending pending) {
	const auto participant = ResolvePeer(session, userId);
	if (!participant) {
		pending.reject(u"Peer not found: "_q + userId);
		return;
	}
	const auto fail = [=](const MTP::Error &error) {
		pending.reject(error.type());
	};
	if (const auto chat = peer->asChat()) {
		const auto user = participant->asUser();
		if (!user) {
			pending.reject(u"Only users are members of basic groups"_q);
			return;
		}
		session->api().request(MTPmessages_DeleteChatUser(
			MTP_flags(0),
			chat->inputChat(),
			user->inputUser()
		)).done([=](const MTPUpdates &result) {
			session->api().applyUpdates(result);
			pending.resolve(QVariant());
		}).fail(fail).send();
		return;
	}
	const auto channel = peer->asChannel();
	if (!channel) {
		pending.reject(u"Members can be removed only from groups"_q);
		return;
	}
	const auto rights = [](bool banned) {
		using Flag = MTPDchatBannedRights::Flag;
		return MTP_chatBannedRights(
			MTP_flags(banned ? Flag::f_view_messages : Flag(0)),
			MTP_int(0));
	};
	const auto unban = [=] {
		session->api().request(MTPchannels_EditBanned(
			channel->inputChannel(),
			participant->input(),
			rights(false)
		)).done([=](const MTPUpdates &result) {
			session->api().applyUpdates(result);
			pending.resolve(QVariant());
		}).fail([=] {
			pending.resolve(QVariant());
		}).send();
	};
	session->api().request(MTPchannels_EditBanned(
		channel->inputChannel(),
		participant->input(),
		rights(true)
	)).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		if (ban) {
			pending.resolve(QVariant());
		} else {
			unban();
		}
	}).fail(fail).send();
}

void SetTitle(
		SessionPtr session,
		not_null<PeerData*> peer,
		QString title,
		Pending pending) {
	const auto done = [=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		pending.resolve(QVariant());
	};
	const auto fail = [=](const MTP::Error &error) {
		if (error.type() == u"CHAT_NOT_MODIFIED"_q) {
			pending.resolve(QVariant());
		} else {
			pending.reject(error.type());
		}
	};
	if (title.trimmed().isEmpty()) {
		pending.reject(u"Title is required"_q);
	} else if (const auto chat = peer->asChat()) {
		session->api().request(MTPmessages_EditChatTitle(
			chat->inputChat(),
			MTP_string(title.trimmed())
		)).done(done).fail(fail).send();
	} else if (const auto channel = peer->asChannel()) {
		session->api().request(MTPchannels_EditTitle(
			channel->inputChannel(),
			MTP_string(title.trimmed())
		)).done(done).fail(fail).send();
	} else {
		pending.reject(u"Only groups and channels have a title"_q);
	}
}

void SetAbout(
		SessionPtr session,
		not_null<PeerData*> peer,
		QString about,
		Pending pending) {
	if (peer->isUser()) {
		pending.reject(u"Use it for groups and channels"_q);
		return;
	}
	session->api().request(MTPmessages_EditChatAbout(
		peer->input(),
		MTP_string(about)
	)).done([=] {
		peer->setAbout(about);
		pending.resolve(QVariant());
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"CHAT_ABOUT_NOT_MODIFIED"_q) {
			pending.resolve(QVariant());
		} else {
			pending.reject(error.type());
		}
	}).send();
}

void CreateInviteLink(
		SessionPtr session,
		not_null<PeerData*> peer,
		QVariantMap options,
		Pending pending) {
	using Flag = MTPmessages_ExportChatInvite::Flag;
	const auto expire = int(options.value(u"expireDate"_q).toDouble());
	const auto usage = int(options.value(u"usageLimit"_q).toDouble());
	const auto title = options.value(u"title"_q).toString();
	const auto request = options.value(u"requestNeeded"_q).toBool();
	const auto flags = (expire > 0 ? Flag::f_expire_date : Flag(0))
		| (usage > 0 ? Flag::f_usage_limit : Flag(0))
		| (title.isEmpty() ? Flag(0) : Flag::f_title)
		| (request ? Flag::f_request_needed : Flag(0));
	session->api().request(MTPmessages_ExportChatInvite(
		MTP_flags(flags),
		peer->input(),
		MTP_int(expire),
		MTP_int(usage),
		MTP_string(title),
		MTPStarsSubscriptionPricing()
	)).done([=](const MTPExportedChatInvite &result) {
		result.match([&](const MTPDchatInviteExported &data) {
			pending.resolve(qs(data.vlink()));
		}, [&](const auto &) {
			pending.reject(u"Unexpected invite link type"_q);
		});
	}).fail([=](const MTP::Error &error) {
		pending.reject(error.type());
	}).send();
}

bool ClearHistory(not_null<PeerData*> peer, bool revoke) {
	peer->session().api().clearHistory(peer, revoke);
	return true;
}

struct TypingSeen {
	QString action;
	crl::time emitted = 0;
	crl::time updated = 0;
};

using TypingKey = std::tuple<uint64, PeerId, PeerId>;

[[nodiscard]] base::flat_map<TypingKey, TypingSeen> &TypingMap() {
	static auto result = base::flat_map<TypingKey, TypingSeen>();
	return result;
}

void ForgetOldTyping(crl::time now) {
	auto &map = TypingMap();
	if (int(map.size()) < kTypingMapLimit) {
		return;
	}
	for (auto i = begin(map); i != end(map);) {
		if (now - i->second.updated > kTypingForget) {
			i = map.erase(i);
		} else {
			++i;
		}
	}
}

void EmitTyping(
		not_null<History*> history,
		MsgId rootId,
		not_null<UserData*> user,
		const QString &action) {
	if (!Manager::Exists()
		|| (Manager::Get().activeSession() != &history->session())) {
		return;
	}
	Manager::Get().emitEvent(u"chat.typing"_q, QVariantMap{
		{ u"chat"_q, SerializePeer(history->peer) },
		{ u"user"_q, SerializePeer(user) },
		{ u"action"_q, action },
		{ u"topicId"_q, double(rootId.bare) },
	});
}

void ExpireTyping(
		not_null<History*> history,
		MsgId rootId,
		not_null<UserData*> user,
		TypingKey key) {
	auto &map = TypingMap();
	const auto i = map.find(key);
	if (i == end(map)
		|| i->second.action == u"cancel"_q
		|| (crl::now() - i->second.updated < kTypingExpire - 100)) {
		return;
	}
	i->second.action = u"cancel"_q;
	EmitTyping(history, rootId, user, u"cancel"_q);
}

void StartUnreadTracking(SessionPtr session) {
	struct Seen {
		int unread = 0;
		bool mark = false;
	};
	struct State {
		base::flat_map<PeerId, Seen> seen;
		base::flat_set<PeerId> dirty;
		base::Timer timer;
	};
	const auto state = session->lifetime().make_state<State>();
	state->timer.setCallback([=] {
		const auto dirty = base::take(state->dirty);
		const auto active = Manager::Exists()
			&& (Manager::Get().activeSession() == session.get());
		for (const auto &peerId : dirty) {
			const auto history = session->data().historyLoaded(peerId);
			if (!history || !history->unreadCountKnown()) {
				continue;
			}
			const auto now = Seen{
				.unread = history->unreadCount(),
				.mark = history->unreadMark(),
			};
			const auto i = state->seen.find(peerId);
			if (i == end(state->seen)) {
				state->seen.emplace(peerId, now);
				if (!now.unread && !now.mark) {
					continue;
				}
			} else if (i->second.unread == now.unread
				&& i->second.mark == now.mark) {
				continue;
			} else {
				i->second = now;
			}
			if (active) {
				Manager::Get().emitEvent(u"chat.unread"_q, QVariantMap{
					{ u"chat"_q, SerializePeer(history->peer) },
					{ u"unread"_q, now.unread },
					{ u"unreadMark"_q, now.mark },
					{ u"mentions"_q, history->unreadMentions().count(0) },
				});
			}
		}
	});
	session->changes().historyUpdates(
		Data::HistoryUpdate::Flag::UnreadView
	) | rpl::on_next([=](const Data::HistoryUpdate &update) {
		state->dirty.emplace(update.history->peer->id);
		if (!state->timer.isActive()) {
			state->timer.callOnce(kUnreadDelay);
		}
	}, session->lifetime());
}

} // namespace

void ChatSendActionReceived(
		not_null<History*> history,
		MsgId rootId,
		not_null<UserData*> user,
		const MTPSendMessageAction &action,
		TimeId when) {
	const auto name = ActionName(action);
	if (!when || name.isEmpty() || !Manager::Exists()) {
		return;
	}
	const auto session = &history->session();
	const auto now = crl::now();
	ForgetOldTyping(now);
	const auto key = TypingKey{
		session->uniqueId(),
		history->peer->id,
		user->id,
	};
	auto &seen = TypingMap()[key];
	seen.updated = now;
	const auto cancel = (name == u"cancel"_q);
	if (cancel) {
		if (seen.action.isEmpty() || seen.action == name) {
			seen.action = name;
			return;
		}
	} else if (seen.action == name && (now - seen.emitted < kTypingRepeat)) {
		return;
	}
	seen.action = name;
	seen.emitted = now;
	EmitTyping(history, rootId, user, name);
	if (!cancel) {
		base::call_delayed(kTypingExpire, session, [=] {
			ExpireTyping(history, rootId, user, key);
		});
	}
}

DZG_API_MODULE({
	.name = "chats",
	.natives = [](not_null<Bridge*> bridge, std::vector<Js::NativeMethod> &out) {
		using Args = QVariantList;
		const auto peerArg = [](const Args &args) {
			return ResolvePeer(
				Manager::Get().activeSession(),
				args.value(0).toString());
		};
		const auto withPeer = [=](
				const Args &args,
				Fn<void(SessionPtr, not_null<PeerData*>, Pending)> callback) {
			const auto peerId = args.value(0).toString();
			return Async(bridge, [=](SessionPtr session, Pending pending) {
				if (const auto peer = ResolvePeer(session, peerId)) {
					callback(session, peer, pending);
				} else {
					pending.reject(u"Peer not found: "_q + peerId);
				}
			});
		};
		out.push_back({ "chats_list", [](const Args &args) {
			const auto session = Manager::Get().activeSession();
			return session
				? QVariant(ListChats(session, args.value(0).toMap()))
				: QVariant(QVariantList());
		} });
		out.push_back({ "chats_entry", [=](const Args &args) {
			const auto peer = peerArg(args);
			return peer
				? QVariant(SerializeEntry(peer->owner().history(peer), 0))
				: QVariant();
		} });
		out.push_back({ "chats_folders", [](const Args &) {
			const auto session = Manager::Get().activeSession();
			return session
				? QVariant(ListFolders(session))
				: QVariant(QVariantList());
		} });
		out.push_back({ "chats_resolve", [=](const Args &args) {
			const auto query = args.value(0).toString();
			return Async(bridge, [=](SessionPtr session, Pending pending) {
				Resolve(session, query, pending);
			});
		} });
		out.push_back({ "chats_join", [=](const Args &args) {
			const auto target = args.value(0).toString();
			return Async(bridge, [=](SessionPtr session, Pending pending) {
				Join(session, target, pending);
			});
		} });
		out.push_back({ "chats_leave", [=](const Args &args) {
			return withPeer(args, Leave);
		} });
		out.push_back({ "chats_members", [=](const Args &args) {
			const auto options = args.value(1).toMap();
			return withPeer(args, [=](
					SessionPtr session,
					not_null<PeerData*> peer,
					Pending pending) {
				Members(session, peer, options, pending);
			});
		} });
		out.push_back({ "chats_info", [=](const Args &args) {
			return withPeer(args, Info);
		} });
		out.push_back({ "chats_mute", [=](const Args &args) {
			const auto peer = peerArg(args);
			const auto seconds = std::clamp(
				args.value(1).toDouble(),
				-1.,
				double(std::numeric_limits<int>::max()));
			return QVariant(peer && Mute(peer, int(seconds)));
		} });
		out.push_back({ "chats_archive", [=](const Args &args) {
			const auto peer = peerArg(args);
			return QVariant(peer && Archive(peer, args.value(1).toBool()));
		} });
		out.push_back({ "chats_pin", [=](const Args &args) {
			const auto peer = peerArg(args);
			const auto filterId = FilterId(args.value(2).toDouble());
			return QVariant(peer
				&& Pin(peer, args.value(1).toBool(), filterId));
		} });
		out.push_back({ "chats_markUnread", [=](const Args &args) {
			const auto peer = peerArg(args);
			return QVariant(peer && MarkUnread(peer, args.value(1).toBool()));
		} });
		out.push_back({ "chats_setTyping", [=](const Args &args) {
			const auto peer = peerArg(args);
			const auto topMsgId = MsgId(int64(args.value(2).toDouble()));
			return QVariant(peer
				&& SetTyping(peer, args.value(1).toString(), topMsgId));
		} });
		out.push_back({ "chats_clearHistory", [=](const Args &args) {
			const auto peer = peerArg(args);
			return QVariant(peer && ClearHistory(peer, args.value(1).toBool()));
		} });
		out.push_back({ "chats_create", [=](const Args &args) {
			const auto options = args.value(0).toMap();
			return Async(bridge, [=](SessionPtr session, Pending pending) {
				Create(session, options, pending);
			});
		} });
		out.push_back({ "chats_addMembers", [=](const Args &args) {
			const auto ids = args.value(1).toList();
			return withPeer(args, [=](
					SessionPtr session,
					not_null<PeerData*> peer,
					Pending pending) {
				AddMembers(session, peer, ids, pending);
			});
		} });
		out.push_back({ "chats_kick", [=](const Args &args) {
			const auto userId = args.value(1).toString();
			const auto ban = args.value(2).toBool();
			return withPeer(args, [=](
					SessionPtr session,
					not_null<PeerData*> peer,
					Pending pending) {
				Kick(session, peer, userId, ban, pending);
			});
		} });
		out.push_back({ "chats_setTitle", [=](const Args &args) {
			const auto title = args.value(1).toString();
			return withPeer(args, [=](
					SessionPtr session,
					not_null<PeerData*> peer,
					Pending pending) {
				SetTitle(session, peer, title, pending);
			});
		} });
		out.push_back({ "chats_setAbout", [=](const Args &args) {
			const auto about = args.value(1).toString();
			return withPeer(args, [=](
					SessionPtr session,
					not_null<PeerData*> peer,
					Pending pending) {
				SetAbout(session, peer, about, pending);
			});
		} });
		out.push_back({ "chats_createInviteLink", [=](const Args &args) {
			const auto options = args.value(1).toMap();
			return withPeer(args, [=](
					SessionPtr session,
					not_null<PeerData*> peer,
					Pending pending) {
				CreateInviteLink(session, peer, options, pending);
			});
		} });
	},
	.prelude = [] {
		return QString::fromUtf8(R"DZG_JS(
	const toPeerId = internal.toPeerId;
	const request = internal.request;
	const typingActions = [
		'typing', 'cancel', 'record_video', 'upload_video', 'record_voice',
		'upload_voice', 'record_round', 'upload_round', 'upload_photo',
		'upload_document', 'choose_location', 'choose_contact',
		'choose_sticker', 'play_game',
	];
	const toTypes = (value) => {
		if (value === undefined || value === null || value === '') return [];
		return (Array.isArray(value) ? value : [value]).map(String);
	};
	Object.assign(dzg.chats, {
		typingActions: Object.freeze(typingActions.slice()),
		list(options) {
			const o = options || {};
			const folder = (o.folder === 'archive') ? 'archive' : (Number(o.folder) || 0);
			const limit = (o.limit === undefined || o.limit === null) ? 100 : Math.max(0, Math.floor(Number(o.limit) || 0));
			return native.chats_list({
				folder,
				limit,
				unreadOnly: !!o.unreadOnly,
				types: toTypes(o.type),
			}) || [];
		},
		entry(peer) {
			return native.chats_entry(toPeerId(peer)) || null;
		},
		folders() {
			return native.chats_folders() || [];
		},
		resolve(username) {
			return request(() => native.chats_resolve(String(username === undefined || username === null ? '' : username)));
		},
		join(target) {
			const value = (target && typeof target === 'object') ? toPeerId(target) : String(target === undefined || target === null ? '' : target);
			return request(() => native.chats_join(value));
		},
		leave(peer) {
			return request(() => native.chats_leave(toPeerId(peer))).then(() => undefined);
		},
		mute(peer, seconds) {
			let value = 0;
			if (seconds === false) {
				value = -1;
			} else if (seconds !== true) {
				const n = Number(seconds);
				value = (n > 0) ? Math.max(1, Math.round(n)) : 0;
			}
			return !!native.chats_mute(toPeerId(peer), value);
		},
		unmute(peer) {
			return dzg.chats.mute(peer, false);
		},
		archive(peer, archived) {
			return !!native.chats_archive(toPeerId(peer), archived !== false);
		},
		pin(peer, pinned, options) {
			const folder = Number((options && options.folder) || 0) || 0;
			return !!native.chats_pin(toPeerId(peer), pinned !== false, folder);
		},
		markUnread(peer, unread) {
			return !!native.chats_markUnread(toPeerId(peer), unread !== false);
		},
		markRead(peer) {
			return dzg.chats.markUnread(peer, false);
		},
		setTyping(peer, action, options) {
			const name = (action === undefined || action === null) ? 'typing' : String(action);
			if (typingActions.indexOf(name) < 0) {
				throw new TypeError('Unknown typing action: ' + name + '. Use one of: ' + typingActions.join(', '));
			}
			const topicId = Number((options && (options.topicId || options.topMsgId)) || 0) || 0;
			return !!native.chats_setTyping(toPeerId(peer), name, topicId);
		},
		members(peer, options) {
			const o = options || {};
			const limit = (o.limit === undefined || o.limit === null) ? 200 : Math.max(1, Math.floor(Number(o.limit) || 1));
			return request(() => native.chats_members(toPeerId(peer), {
				limit,
				query: (o.query === undefined || o.query === null) ? '' : String(o.query),
			}));
		},
		info(peer) {
			return request(() => native.chats_info(toPeerId(peer)));
		},
		create(options) {
			const o = (typeof options === 'string') ? { title: options } : (options || {});
			return request(() => native.chats_create({
				title: String(o.title === undefined || o.title === null ? '' : o.title),
				type: String(o.type || 'group'),
				about: String(o.about || ''),
				users: (Array.isArray(o.users) ? o.users : (o.users ? [o.users] : [])).map(toPeerId),
				forum: !!o.forum,
			}));
		},
		addMembers(peer, users) {
			const list = (Array.isArray(users) ? users : [users]).map(toPeerId).filter(Boolean);
			return request(() => native.chats_addMembers(toPeerId(peer), list));
		},
		kick(peer, user, options) {
			const ban = !!(options && options.ban);
			return request(() => native.chats_kick(toPeerId(peer), toPeerId(user), ban)).then(() => undefined);
		},
		setTitle(peer, title) {
			return request(() => native.chats_setTitle(toPeerId(peer), String(title === undefined || title === null ? '' : title))).then(() => undefined);
		},
		setAbout(peer, about) {
			return request(() => native.chats_setAbout(toPeerId(peer), String(about === undefined || about === null ? '' : about))).then(() => undefined);
		},
		createInviteLink(peer, options) {
			const o = options || {};
			return request(() => native.chats_createInviteLink(toPeerId(peer), {
				expireDate: Math.max(0, Math.floor(Number(o.expireDate) || 0)),
				usageLimit: Math.max(0, Math.floor(Number(o.usageLimit) || 0)),
				title: String(o.title || ''),
				requestNeeded: !!o.requestNeeded,
			}));
		},
		clearHistory(peer, options) {
			return !!native.chats_clearHistory(toPeerId(peer), !!(options && options.revoke));
		},
	});
)DZG_JS");
	},
	.sessionStarted = [](not_null<Main::Session*> session) {
		StartUnreadTracking(session);
	},
});

} // namespace Damza::Plugins
