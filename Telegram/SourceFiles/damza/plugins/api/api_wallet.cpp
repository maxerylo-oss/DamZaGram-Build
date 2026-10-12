// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
//
// dzg.wallet: Telegram Stars and TON balances of the active account.
// setLocal() changes only what this client shows; the next update from
// the server (or refresh()) brings the real balance back.
#include "damza/plugins/plugin_api.h"

#include "core/credits_amount.h"
#include "damza/plugins/plugin_manager.h"
#include "data/components/credits.h"
#include "main/main_session.h"

#include <cmath>

namespace Damza::Plugins {
namespace {

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

} // namespace

DZG_API_MODULE({
	.name = "wallet",
	.natives = [](not_null<Bridge*> bridge, std::vector<Js::NativeMethod> &out) {
		using Args = QVariantList;
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
});

} // namespace Damza::Plugins
