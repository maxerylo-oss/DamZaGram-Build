// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

class History;
class UserData;

namespace Damza::Plugins {

// Called from Api::Updates::handleSendActionUpdate, fires `chat.typing`.
void ChatSendActionReceived(
	not_null<History*> history,
	MsgId rootId,
	not_null<UserData*> user,
	const MTPSendMessageAction &action,
	TimeId when);

} // namespace Damza::Plugins
