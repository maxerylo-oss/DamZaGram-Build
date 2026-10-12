// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

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

namespace Damza::Plugins {

// Called once the primary window has been shown.
void Start();

// Called on application quit.
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

} // namespace Damza::Plugins
