// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Damza::Plugins {
namespace details {

extern bool RawUpdatesWanted;
void FeedRawUpdateSlow(
	not_null<Main::Session*> session,
	const MTPUpdate &update);
void FeedRawUpdatesSlow(
	not_null<Main::Session*> session,
	const MTPUpdates &updates);

} // namespace details

// A single flag check while no plugin uses dzg.api.onUpdate().
inline void FeedRawUpdate(
		not_null<Main::Session*> session,
		const MTPUpdate &update) {
	if (details::RawUpdatesWanted) {
		details::FeedRawUpdateSlow(session, update);
	}
}

// Passes updateShort(Chat)Message to plugins as updateNewMessage.
inline void FeedRawUpdates(
		not_null<Main::Session*> session,
		const MTPUpdates &updates) {
	if (details::RawUpdatesWanted) {
		details::FeedRawUpdatesSlow(session, updates);
	}
}

} // namespace Damza::Plugins
