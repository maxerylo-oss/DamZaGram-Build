// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

#include <memory>

// Entry points the rest of the client calls into the plugin system.
// Everything here is safe to call before plugins are loaded: it is a no-op.

class HistoryItem;
class PeerData;

namespace Api {
struct MessageToSend;
} // namespace Api

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
struct UniqueGift;
} // namespace Data

namespace Data {
struct CreditsStatusSlice;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Damza::Plugins {

// Called once the primary window has been shown.
void Start();

// Called from Core::Application destructor, while windows and sessions live.
void Shutdown();

// True if some plugin asked to hide this message.
[[nodiscard]] bool Filtered(not_null<HistoryItem*> item);

// Lets plugins rewrite or swallow an outgoing text message.
// Returns false when the message must not be sent.
[[nodiscard]] bool BeforeSend(Api::MessageToSend &message);

// Notifies plugins that the server deleted the message.
void MessageDeleted(not_null<HistoryItem*> item);

// Appends plugin actions to a message context menu.
void FillMessageMenu(not_null<Ui::PopupMenu*> menu, HistoryItem *item);

// Appends plugin actions to a chat (peer) menu.
void FillPeerMenu(PeerData *peer, const Ui::Menu::MenuCallback &addAction);

// NFT gift "offer to buy": if a plugin registered dzg.gifts.onOffer, show the
// native price box and hand the chosen amount and currency to the plugin,
// instead of sending a real offer. Returns true when a plugin took over.
[[nodiscard]] bool InterceptGiftOffer(
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::UniqueGift> unique);

// Prepends plugin-provided fake entries to a Stars transactions list.
// `in` / `out` say which directions this list shows (full tab: both).
void InjectCreditsHistory(
	not_null<Main::Session*> session,
	Data::CreditsStatusSlice &slice,
	bool in,
	bool out);

// True if a plugin asked to show the total reaction count in the bottom
// info line, next to views and forwards. See dzg.reactions.showTotal.
[[nodiscard]] bool ShowReactionsTotal();

} // namespace Damza::Plugins
