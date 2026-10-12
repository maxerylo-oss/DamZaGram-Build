// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

#include "data/data_peer_id.h"

class HistoryItem;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Damza::Plugins {

// Peer ids travel to JS as decimal strings: they don't fit
// into a double without loss once the type bits are added.
[[nodiscard]] QString PeerIdToString(PeerId id);
[[nodiscard]] PeerId PeerIdFromString(const QString &value);

[[nodiscard]] PeerData *ResolvePeer(
	Main::Session *session,
	const QString &peerId);
[[nodiscard]] HistoryItem *ResolveMessage(
	Main::Session *session,
	const QString &peerId,
	double msgId);

[[nodiscard]] QVariantMap SerializePeer(not_null<PeerData*> peer);
[[nodiscard]] QVariantMap SerializeMessage(not_null<HistoryItem*> item);

} // namespace Damza::Plugins
