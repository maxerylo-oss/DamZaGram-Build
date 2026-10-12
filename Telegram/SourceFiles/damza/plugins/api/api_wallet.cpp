// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
//
// dzg.wallet: Telegram Stars and TON balances of the active account.
// setLocal() changes only what this client shows; the next update from
// the server (or refresh()) brings the real balance back. setHistory()
// prepends fake entries to the Stars transactions list, locally only.
#include "damza/plugins/plugin_api.h"
#include "damza/plugins/plugins.h"

#include "core/credits_amount.h"
#include "damza/plugins/plugin_bridge.h"
#include "damza/plugins/plugin_data.h"
#include "damza/plugins/plugin_instance.h"
#include "damza/plugins/plugin_manager.h"
#include "data/components/credits.h"
#include "data/data_credits.h"
#include "main/main_session.h"

#include <QtCore/QDateTime>
#include <cmath>

namespace Damza::Plugins {
namespace {

// Fake transaction entries, kept per plugin so unload clears them.
base::flat_map<QString, QVariantList> HistoryByPlugin;

[[nodiscard]] QVariantMap Balances(not_null<Main::Session*> session) {
	auto &credits = session->credits();
	return {
		{ u"stars"_q, credits.balance().value() },
		{ u"ton"_q, credits.tonBalance().value() },
		{ u"starsLoaded"_q, credits.loaded() },
		{ u"tonLoaded"_q, credits.tonLoaded() },
	};
}

[[nodiscard]] CreditsAmount ToAmount(double value, CreditsType type) {
	const auto clamped = std::clamp(value, 0., 1e15);
	const auto whole = int64(std::floor(clamped));
	const auto nano = int64(std::llround((clamped - whole) * kOneStarInNano));
	return CreditsAmount(whole, nano, type);
}

[[nodiscard]] Data::CreditsHistoryEntry ToEntry(
		const QString &pluginId,
		int index,
		const QVariantMap &map) {
	const auto ton = (map.value(u"currency"_q).toString() == u"ton"_q);
	auto entry = Data::CreditsHistoryEntry();
	entry.id = map.value(u"id"_q).toString();
	if (entry.id.isEmpty()) {
		entry.id = u"dzg:%1:%2"_q.arg(pluginId).arg(index);
	}
	entry.title = map.value(u"title"_q).toString();
	entry.description = TextWithEntities{
		map.value(u"description"_q).toString(),
	};
	const auto date = map.value(u"date"_q).toDouble();
	entry.date = (date > 0)
		? QDateTime::fromSecsSinceEpoch(int64(date))
		: QDateTime::currentDateTime();
	entry.credits = ToAmount(
		map.value(u"amount"_q).toDouble(),
		ton ? CreditsType::Ton : CreditsType::Stars);
	if (const auto id = PeerIdFromString(map.value(u"peerId"_q).toString())) {
		entry.barePeerId = id.value;
	}
	entry.peerType = Data::CreditsHistoryEntry::PeerType::Peer;
	entry.in = map.value(u"incoming"_q, true).toBool();
	return entry;
}

} // namespace

void InjectCreditsHistory(
		not_null<Main::Session*> session,
		Data::CreditsStatusSlice &slice,
		bool in,
		bool out) {
	if (HistoryByPlugin.empty()
		|| !Manager::Exists()
		|| Manager::Get().activeSession() != session) {
		return;
	}
	auto add = std::vector<Data::CreditsHistoryEntry>();
	for (const auto &[pluginId, entries] : HistoryByPlugin) {
		auto index = 0;
		for (const auto &value : entries) {
			const auto map = value.toMap();
			const auto incoming = map.value(u"incoming"_q, true).toBool();
			++index;
			if ((incoming && in) || (!incoming && out)) {
				add.push_back(ToEntry(pluginId, index, map));
			}
		}
	}
	slice.list.insert(begin(slice.list), begin(add), end(add));
}

DZG_API_MODULE({
	.name = "wallet",
	.natives = [](not_null<Bridge*> bridge, std::vector<Js::NativeMethod> &out) {
		using Args = QVariantList;
		const auto id = bridge->instance()->manifest().id;
		out.push_back({ "wallet_get", [](const Args &) {
			const auto session = Manager::Get().activeSession();
			return session ? QVariant(Balances(session)) : QVariant();
		} });
		out.push_back({ "wallet_setLocal", [](const Args &args) {
			const auto session = Manager::Get().activeSession();
			if (!session) {
				return QVariant(false);
			}
			const auto values = args.value(0).toMap();
			auto &credits = session->credits();
			if (values.contains(u"stars"_q)) {
				credits.apply(ToAmount(
					values.value(u"stars"_q).toDouble(),
					CreditsType::Stars));
			}
			if (values.contains(u"ton"_q)) {
				credits.apply(ToAmount(
					values.value(u"ton"_q).toDouble(),
					CreditsType::Ton));
			}
			return QVariant(true);
		} });
		out.push_back({ "wallet_refresh", [](const Args &) {
			const auto session = Manager::Get().activeSession();
			if (!session) {
				return QVariant(false);
			}
			session->credits().load(true);
			session->credits().tonLoad(true);
			return QVariant(true);
		} });
		out.push_back({ "wallet_setHistory", [=](const Args &args) {
			const auto entries = args.value(0).toList();
			if (entries.isEmpty()) {
				HistoryByPlugin.remove(id);
			} else {
				HistoryByPlugin[id] = entries;
			}
			return QVariant();
		} });
	},
	.prelude = [] {
		return QString::fromUtf8(R"DZG_JS(
	const toNumber = (value) => {
		const number = Number(value);
		if (!Number.isFinite(number) || number < 0) {
			throw new RangeError('Balance must be a non-negative number');
		}
		return number;
	};
	dzg.wallet = {
		get() { return native.wallet_get(); },
		setLocal(values) {
			const v = values || {};
			const payload = {};
			if (v.stars !== undefined) payload.stars = toNumber(v.stars);
			if (v.ton !== undefined) payload.ton = toNumber(v.ton);
			return !!native.wallet_setLocal(payload);
		},
		refresh() { return !!native.wallet_refresh(); },
		setHistory(entries) {
			const list = (entries || []).map((e) => ({
				id: e.id !== undefined ? String(e.id) : undefined,
				title: String(e.title || ''),
				description: String(e.description || ''),
				amount: toNumber(e.amount || 0),
				currency: e.currency === 'ton' ? 'ton' : 'stars',
				peerId: e.peer !== undefined ? internal.toPeerId(e.peer) : '',
				incoming: e.incoming !== false,
				date: Number(e.date) || 0,
			}));
			native.wallet_setHistory(list);
			return list.length;
		},
	};
)DZG_JS");
	},
	.sessionStarted = [](not_null<Main::Session*> session) {
		const auto emit = [=] {
			if (!Manager::Exists()
				|| Manager::Get().activeSession() != session
				|| !Manager::Get().anyListens(u"wallet.changed"_q)) {
				return;
			}
			Manager::Get().emitEventLater(
				u"wallet.changed"_q,
				Balances(session));
		};
		session->credits().balanceValue(
		) | rpl::on_next([=](CreditsAmount) {
			emit();
		}, session->lifetime());
		session->credits().tonBalanceValue(
		) | rpl::on_next([=](CreditsAmount) {
			emit();
		}, session->lifetime());
	},
	.pluginUnloaded = [](const QString &id) {
		HistoryByPlugin.remove(id);
	},
});

} // namespace Damza::Plugins
