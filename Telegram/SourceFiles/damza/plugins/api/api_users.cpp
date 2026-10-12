// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
//
// Modules "users" and "accounts": self, full user info, contacts,
// blocking, account list and switching. Event: user.status.
#include "api/api_blocked_peers.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "damza/plugins/api/api_chats_common.h"
#include "damza/plugins/plugin_api.h"
#include "damza/plugins/plugin_bridge.h"
#include "damza/plugins/plugin_data.h"
#include "damza/plugins/plugin_manager.h"
#include "data/data_birthday.h"
#include "data/data_changes.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"

namespace Damza::Plugins {
namespace {

using ChatsApi::Async;
using ChatsApi::Pending;
using SessionPtr = not_null<Main::Session*>;

constexpr auto kStatusWarmup = crl::time(15000);

struct StatusInfo {
	bool online = false;
	TimeId lastSeen = 0;
	QString kind;
};

[[nodiscard]] StatusInfo ComputeStatus(not_null<UserData*> user) {
	const auto now = base::unixtime::now();
	const auto status = user->lastseen();
	if (Data::IsUserOnline(user, now)) {
		return { .online = true, .lastSeen = now, .kind = u"online"_q };
	} else if (const auto till = status.onlineTill()) {
		return { .lastSeen = till, .kind = u"offline"_q };
	} else if (status.isRecently()) {
		return { .kind = u"recently"_q };
	} else if (status.isWithinWeek()) {
		return { .kind = u"lastWeek"_q };
	} else if (status.isWithinMonth()) {
		return { .kind = u"lastMonth"_q };
	} else if (status.isLongAgo()) {
		return { .kind = u"longAgo"_q };
	}
	return { .kind = u"hidden"_q };
}

[[nodiscard]] QVariantMap SerializeStatus(not_null<UserData*> user) {
	const auto status = ComputeStatus(user);
	return {
		{ u"online"_q, status.online },
		{ u"lastSeen"_q, double(status.lastSeen) },
		{ u"kind"_q, status.kind },
		{ u"text"_q, Data::OnlineText(user, base::unixtime::now()) },
	};
}

[[nodiscard]] QVariantMap SerializeUserInfo(not_null<UserData*> user) {
	auto result = QVariantMap{
		{ u"peer"_q, SerializePeer(user) },
		{ u"bio"_q, user->about() },
		{ u"phone"_q, user->phone() },
		{ u"firstName"_q, user->firstName },
		{ u"lastName"_q, user->lastName },
		{ u"isContact"_q, user->isContact() },
		{ u"isPremium"_q, user->isPremium() },
		{ u"isBot"_q, user->isBot() },
		{ u"isVerified"_q, user->isVerified() },
		{ u"isScam"_q, user->isScam() },
		{ u"isFake"_q, user->isFake() },
		{ u"isBlocked"_q, user->isBlocked() },
		{ u"commonChats"_q, user->commonChatsCount() },
		{ u"status"_q, SerializeStatus(user) },
		{ u"birthday"_q, QVariant() },
	};
	const auto birthday = user->birthday();
	if (birthday.valid()) {
		result.insert(u"birthday"_q, QVariantMap{
			{ u"day"_q, birthday.day() },
			{ u"month"_q, birthday.month() },
			{ u"year"_q, birthday.year() },
		});
	}
	return result;
}

[[nodiscard]] UserData *UserArg(const QVariantList &args) {
	const auto peer = ResolvePeer(
		Manager::Get().activeSession(),
		args.value(0).toString());
	return peer ? peer->asUser() : nullptr;
}

void Block(
		SessionPtr session,
		not_null<PeerData*> peer,
		bool blocked,
		Pending pending) {
	if (!blocked) {
		// Forced: without it unblock() returns early, never calling back,
		// when the client thinks the peer is not blocked (that flag is often
		// unknown until the full info is loaded).
		session->api().blockedPeers().unblock(peer, [=](bool success) {
			if (success) {
				pending.resolve(QVariant());
			} else {
				pending.reject(u"Could not unblock"_q);
			}
		}, true);
		return;
	} else if (peer->isBlocked()) {
		pending.resolve(QVariant());
		return;
	}
	session->api().request(MTPcontacts_Block(
		MTP_flags(0),
		peer->input()
	)).done([=] {
		peer->setIsBlocked(true);
		session->api().blockedPeers().reload();
		pending.resolve(QVariant());
	}).fail([=](const MTP::Error &error) {
		pending.reject(error.type());
	}).send();
}

void CommonChats(
		SessionPtr session,
		not_null<UserData*> user,
		int limit,
		Pending pending) {
	session->api().request(MTPmessages_GetCommonChats(
		user->inputUser(),
		MTP_long(0), // max_id
		MTP_int(limit)
	)).done([=](const MTPmessages_Chats &result) {
		auto list = QVariantList();
		const auto &chats = result.match([](const auto &data) {
			return data.vchats().v;
		});
		for (const auto &chat : chats) {
			list.push_back(SerializePeer(session->data().processChat(chat)));
		}
		pending.resolve(list);
	}).fail([=](const MTP::Error &error) {
		pending.reject(error.type());
	}).send();
}

void AddContact(
		SessionPtr session,
		not_null<UserData*> user,
		QVariantMap options,
		Pending pending) {
	using Flag = MTPcontacts_AddContact::Flag;
	const auto value = [&](const QString &key, const QString &fallback) {
		const auto result = options.value(key).toString().trimmed();
		return result.isEmpty() ? fallback : result;
	};
	const auto first = value(u"firstName"_q, user->firstName);
	const auto last = value(u"lastName"_q, user->lastName);
	const auto phone = value(u"phone"_q, user->phone());
	const auto share = options.value(u"sharePhone"_q).toBool();
	if (first.isEmpty()) {
		pending.reject(u"First name is required"_q);
		return;
	}
	session->api().request(MTPcontacts_AddContact(
		MTP_flags(share ? Flag::f_add_phone_privacy_exception : Flag(0)),
		user->inputUser(),
		MTP_string(first),
		MTP_string(last),
		MTP_string(phone),
		MTPTextWithEntities() // note
	)).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		pending.resolve(QVariant());
	}).fail([=](const MTP::Error &error) {
		pending.reject(error.type());
	}).send();
}

void DeleteContact(
		SessionPtr session,
		not_null<UserData*> user,
		Pending pending) {
	session->api().request(MTPcontacts_DeleteContacts(
		MTP_vector<MTPInputUser>(1, user->inputUser())
	)).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		pending.resolve(QVariant());
	}).fail([=](const MTP::Error &error) {
		pending.reject(error.type());
	}).send();
}

[[nodiscard]] QVariant WithUser(
		not_null<Bridge*> bridge,
		const QVariantList &args,
		Fn<void(SessionPtr, not_null<UserData*>, Pending)> callback) {
	const auto peerId = args.value(0).toString();
	return Async(bridge, [=](SessionPtr session, Pending pending) {
		const auto peer = ResolvePeer(session, peerId);
		if (const auto user = peer ? peer->asUser() : nullptr) {
			callback(session, user, pending);
		} else {
			pending.reject(u"User not found: "_q + peerId);
		}
	});
}

[[nodiscard]] QVariantList ListContacts(SessionPtr session) {
	auto result = QVariantList();
	for (const auto &row : session->data().contactsList()->all()) {
		if (const auto history = row->history()) {
			result.push_back(SerializePeer(history->peer));
		}
	}
	return result;
}

[[nodiscard]] QVariantList ListAccounts() {
	auto result = QVariantList();
	auto &domain = Core::App().domain();
	if (!domain.started()) {
		return result;
	}
	const auto active = &domain.active();
	for (const auto &[index, account] : domain.accounts()) {
		if (!account->sessionExists()) {
			continue;
		}
		const auto session = &account->session();
		result.push_back(QVariantMap{
			{ u"index"_q, index },
			{ u"self"_q, SerializePeer(session->user()) },
			{ u"active"_q, account.get() == active },
			{ u"premium"_q, session->premium() },
		});
	}
	return result;
}

bool SwitchAccount(int index) {
	auto &domain = Core::App().domain();
	if (!domain.started()) {
		return false;
	}
	for (const auto &[i, account] : domain.accounts()) {
		if (i != index || !account->sessionExists()) {
			continue;
		}
		const auto raw = account.get();
		if (raw != &domain.active()) {
			crl::on_main(raw, [=] {
				Core::App().domain().maybeActivate(raw);
			});
		}
		return true;
	}
	return false;
}

void StartStatusTracking(SessionPtr session) {
	struct Seen {
		bool online = false;
		TimeId lastSeen = 0;
	};
	struct State {
		base::flat_map<PeerId, Seen> seen;
		crl::time started = 0;
	};
	const auto state = session->lifetime().make_state<State>();
	state->started = crl::now();
	session->changes().peerUpdates(
		Data::PeerUpdate::Flag::OnlineStatus
	) | rpl::on_next([=](const Data::PeerUpdate &update) {
		const auto user = update.peer->asUser();
		if (!user || user->isBot()) {
			return;
		}
		const auto status = ComputeStatus(user);
		const auto now = Seen{
			.online = status.online,
			.lastSeen = status.online ? 0 : status.lastSeen,
		};
		const auto i = state->seen.find(user->id);
		if (i == end(state->seen)) {
			state->seen.emplace(user->id, now);
			if (crl::now() - state->started < kStatusWarmup) {
				return;
			}
		} else if (i->second.online == now.online
			&& i->second.lastSeen == now.lastSeen) {
			return;
		} else {
			i->second = now;
		}
		if (!Manager::Exists()
			|| (Manager::Get().activeSession() != session.get())) {
			return;
		}
		Manager::Get().emitEventLater(u"user.status"_q, QVariantMap{
			{ u"user"_q, SerializePeer(user) },
			{ u"online"_q, status.online },
			{ u"lastSeen"_q, double(status.lastSeen) },
			{ u"kind"_q, status.kind },
		});
	}, session->lifetime());
}

} // namespace

void ChatsApi::RequestUserFull(
		not_null<UserData*> user,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	const auto session = &user->session();
	session->api().request(MTPusers_GetFullUser(
		user->inputUser()
	)).done([=](const MTPusers_UserFull &result) {
		result.match([&](const MTPDusers_userFull &data) {
			session->data().processUsers(data.vusers());
			session->data().processChats(data.vchats());
			data.vfull_user().match([&](const MTPDuserFull &fields) {
				Data::ApplyUserUpdate(user, fields);
			});
		});
		session->changes().peerUpdated(
			user,
			Data::PeerUpdate::Flag::FullInfo);
		done();
	}).fail([=](const MTP::Error &error) {
		fail(error.type());
	}).send();
}

DZG_API_MODULE({
	.name = "users",
	.natives = [](not_null<Bridge*> bridge, std::vector<Js::NativeMethod> &out) {
		using Args = QVariantList;
		out.push_back({ "users_self", [](const Args &) {
			const auto session = Manager::Get().activeSession();
			return session
				? QVariant(SerializePeer(session->user()))
				: QVariant();
		} });
		out.push_back({ "users_status", [](const Args &args) {
			const auto user = UserArg(args);
			return user ? QVariant(SerializeStatus(user)) : QVariant();
		} });
		out.push_back({ "users_isBlocked", [](const Args &args) {
			const auto peer = ResolvePeer(
				Manager::Get().activeSession(),
				args.value(0).toString());
			return QVariant(peer && peer->isBlocked());
		} });
		out.push_back({ "users_contacts", [](const Args &) {
			const auto session = Manager::Get().activeSession();
			return session
				? QVariant(ListContacts(session))
				: QVariant(QVariantList());
		} });
		out.push_back({ "users_info", [=](const Args &args) {
			return WithUser(bridge, args, [](
					SessionPtr session,
					not_null<UserData*> user,
					Pending pending) {
				ChatsApi::RequestUserFull(user, [=] {
					pending.resolve(SerializeUserInfo(user));
				}, [=](const QString &error) {
					pending.reject(error);
				});
			});
		} });
		out.push_back({ "users_commonChats", [=](const Args &args) {
			const auto limit = std::clamp(int(args.value(1).toDouble()), 1, 100);
			return WithUser(bridge, args, [=](
					SessionPtr session,
					not_null<UserData*> user,
					Pending pending) {
				CommonChats(session, user, limit, pending);
			});
		} });
		out.push_back({ "users_addContact", [=](const Args &args) {
			const auto options = args.value(1).toMap();
			return WithUser(bridge, args, [=](
					SessionPtr session,
					not_null<UserData*> user,
					Pending pending) {
				AddContact(session, user, options, pending);
			});
		} });
		out.push_back({ "users_deleteContact", [=](const Args &args) {
			return WithUser(bridge, args, DeleteContact);
		} });
		out.push_back({ "users_block", [=](const Args &args) {
			const auto peerId = args.value(0).toString();
			const auto blocked = args.value(1).toBool();
			return Async(bridge, [=](SessionPtr session, Pending pending) {
				if (const auto peer = ResolvePeer(session, peerId)) {
					Block(session, peer, blocked, pending);
				} else {
					pending.reject(u"Peer not found: "_q + peerId);
				}
			});
		} });
		out.push_back({ "accounts_list", [](const Args &) {
			return QVariant(ListAccounts());
		} });
		out.push_back({ "accounts_switchTo", [](const Args &args) {
			// NaN or out of range: converting it to int is undefined
			// (0 on ARM, which would switch to the first account).
			const auto index = args.value(0).toDouble();
			return QVariant(index >= 0.
				&& index <= double(std::numeric_limits<int>::max())
				&& SwitchAccount(int(index)));
		} });
	},
	.prelude = [] {
		return QString::fromUtf8(R"DZG_JS(
	const toPeerId = internal.toPeerId;
	const request = internal.request;
	dzg.users = Object.assign(dzg.users || {}, {
		self() {
			return native.users_self() || null;
		},
		get(peer) {
			return dzg.chats.get(peer);
		},
		status(peer) {
			return native.users_status(toPeerId(peer)) || null;
		},
		isBlocked(peer) {
			return !!native.users_isBlocked(toPeerId(peer));
		},
		info(peer) {
			return request(() => native.users_info(toPeerId(peer)));
		},
		contacts() {
			return native.users_contacts() || [];
		},
		block(peer, blocked) {
			return request(() => native.users_block(toPeerId(peer), blocked !== false)).then(() => undefined);
		},
		unblock(peer) {
			return dzg.users.block(peer, false);
		},
		commonChats(peer, options) {
			const o = options || {};
			const limit = (o.limit === undefined || o.limit === null) ? 100 : Math.min(100, Math.max(1, Math.floor(Number(o.limit) || 1)));
			return request(() => native.users_commonChats(toPeerId(peer), limit));
		},
		addContact(peer, options) {
			const o = options || {};
			return request(() => native.users_addContact(toPeerId(peer), {
				firstName: String(o.firstName || ''),
				lastName: String(o.lastName || ''),
				phone: String(o.phone || ''),
				sharePhone: !!o.sharePhone,
			})).then(() => undefined);
		},
		deleteContact(peer) {
			return request(() => native.users_deleteContact(toPeerId(peer))).then(() => undefined);
		},
	});
	dzg.accounts = Object.assign(dzg.accounts || {}, {
		list() {
			return native.accounts_list() || [];
		},
		current() {
			return dzg.accounts.list().find((a) => a.active) || null;
		},
		switchTo(index) {
			const target = (index && typeof index === 'object') ? index.index : index;
			// Number(null) and Number('') are 0: never switch to the first
			// account because of a missing entry (list().find(...) || null).
			const valid = (typeof target === 'number')
				|| (typeof target === 'string' && target.trim() !== '');
			const n = valid ? Number(target) : NaN;
			return Number.isInteger(n) ? !!native.accounts_switchTo(n) : false;
		},
	});
)DZG_JS");
	},
	.sessionStarted = [](not_null<Main::Session*> session) {
		StartStatusTracking(session);
	},
});

} // namespace Damza::Plugins
