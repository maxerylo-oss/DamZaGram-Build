// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
//
// dzg.gifts: intercept the "Offer to Buy" action on a unique (NFT) gift.
// A plugin registers dzg.gifts.onOffer(fn); the client then shows the same
// native price box (amount + currency), and on confirm hands the chosen
// values to the plugin instead of sending a real offer to Telegram.
#include "damza/plugins/plugin_api.h"
#include "damza/plugins/plugins.h"

#include "base/weak_qptr.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/credits_amount.h"
#include "damza/plugins/plugin_bridge.h"
#include "damza/plugins/plugin_data.h"
#include "damza/plugins/plugin_instance.h"
#include "damza/plugins/plugin_manager.h"
#include "data/data_msg_id.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "history/view/controls/history_view_suggest_options.h"
#include "lang/lang_tag.h"
#include "main/main_session.h"
#include "ui/layers/box_content.h"
#include "ui/layers/generic_box.h"
#include "window/window_controller.h"

namespace Damza::Plugins {
namespace {

// Plugin ids that registered a gift-offer handler.
base::flat_set<QString> Intercepting;

[[nodiscard]] QVariantMap OfferPayload(
		not_null<Data::UniqueGift*> unique,
		not_null<PeerData*> owner,
		SuggestOptions options) {
	const auto price = options.price();
	return {
		{ u"slug"_q, unique->slug },
		{ u"link"_q, u"https://t.me/nft/"_q + unique->slug },
		{ u"title"_q, Data::UniqueGiftName(*unique) },
		{ u"amount"_q, Lang::FormatCreditsAmountDecimal(price) },
		{ u"amountNumber"_q, price.value() },
		{ u"currency"_q, price.ton() ? u"ton"_q : u"stars"_q },
		{ u"duration"_q, options.offerDuration },
		{ u"ownerId"_q, PeerIdToString(owner->id) },
		{ u"ownerUsername"_q, owner->username() },
		{ u"ownerName"_q, owner->name() },
	};
}

} // namespace

bool InterceptGiftOffer(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> unique) {
	if (Intercepting.empty() || !show || !unique || !Manager::Exists()) {
		return false;
	}
	using namespace HistoryView;
	const auto owner = show->session().data().peer(unique->ownerId);
	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
	const auto done = [=](SuggestOptions result) {
		if (Manager::Exists()) {
			const auto payload = QVariant(OfferPayload(
				unique.get(),
				owner,
				result));
			// Snapshot the ids: a handler may reload plugins.
			const auto ids = Intercepting;
			for (const auto &id : ids) {
				if (const auto plugin = Manager::Get().instance(id)) {
					plugin->dispatch(u"gift:offer"_q, payload);
				}
			}
		}
		if (const auto strong = weak->get()) {
			strong->closeBox();
		}
	};
	auto options = SuggestOptions{
		.exists = 1,
		.priceWhole = uint32(std::max(unique->starsMinOffer, 0)),
	};
	auto priceBox = Box(ChooseSuggestPriceBox, SuggestPriceBoxArgs{
		.peer = owner,
		.done = done,
		.value = options,
		.mode = SuggestMode::Gift,
		.giftName = Data::UniqueGiftName(*unique),
	});
	*weak = priceBox.data();
	show->show(std::move(priceBox));
	return true;
}

DZG_API_MODULE({
	.name = "gifts",
	.natives = [](not_null<Bridge*> bridge, std::vector<Js::NativeMethod> &out) {
		const auto id = bridge->instance()->manifest().id;
		out.push_back({ "gifts_setIntercept", [=](const QVariantList &args) {
			if (args.value(0).toBool()) {
				Intercepting.emplace(id);
			} else {
				Intercepting.remove(id);
			}
			return QVariant();
		} });
		out.push_back({ "gifts_close", [](const QVariantList &) {
			if (Manager::Exists()) {
				if (const auto window = Manager::Get().activeWindow()) {
					window->hideLayer();
				}
			}
			return QVariant();
		} });
	},
	.prelude = [] {
		return QString::fromUtf8(R"DZG_JS(
	const handlers = [];
	internal.on('gift:offer', (payload) => {
		for (const fn of handlers.slice()) {
			internal.safe('gifts.onOffer', fn, payload);
		}
	});
	dzg.gifts = {
		// Закрыть окна подарка и профиля, вернуться в чат.
		close() { native.gifts_close(); },
		onOffer(fn) {
			if (typeof fn !== 'function') {
				throw new TypeError('onOffer expects a function');
			}
			handlers.push(fn);
			if (handlers.length === 1) native.gifts_setIntercept(true);
			return () => {
				const index = handlers.indexOf(fn);
				if (index >= 0) handlers.splice(index, 1);
				if (!handlers.length) native.gifts_setIntercept(false);
			};
		},
	};
)DZG_JS");
	},
	.pluginUnloaded = [](const QString &id) {
		Intercepting.remove(id);
	},
});

} // namespace Damza::Plugins
