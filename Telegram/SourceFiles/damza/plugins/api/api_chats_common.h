// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

#include "damza/plugins/plugin_bridge.h"
#include "damza/plugins/plugin_manager.h"

#include <QtCore/QPointer>
#include <QtCore/QTimer>

class UserData;

namespace Main {
class Session;
} // namespace Main

namespace Damza::Plugins::ChatsApi {

// One promise created in JS with internal.request() and settled from C++.
// Safe to copy into async callbacks: the bridge is held by QPointer.
class Pending final {
public:
	explicit Pending(not_null<Bridge*> bridge)
	: _bridge(bridge.get())
	, _id(bridge->nextRequestId()) {
	}

	[[nodiscard]] int id() const {
		return _id;
	}
	[[nodiscard]] bool alive() const {
		return !_bridge.isNull();
	}
	void resolve(const QVariant &value) const {
		if (const auto bridge = _bridge.data()) {
			bridge->settle(_id, true, value);
		}
	}
	void reject(const QString &error) const {
		if (const auto bridge = _bridge.data()) {
			bridge->settle(_id, false, error);
		}
	}

private:
	QPointer<Bridge> _bridge;
	int _id = 0;

};

// Starts an async native: JS stores the promise only after the native
// returns its id, so the work itself runs on the next event loop turn.
template <typename Callback>
[[nodiscard]] QVariant Async(not_null<Bridge*> bridge, Callback callback) {
	const auto pending = Pending(bridge);
	QTimer::singleShot(0, bridge.get(), [=] {
		const auto session = Manager::Exists()
			? Manager::Get().activeSession()
			: nullptr;
		if (!session) {
			pending.reject(u"No active session"_q);
		} else {
			callback(not_null<Main::Session*>(session), pending);
		}
	});
	return QVariant(pending.id());
}

// users.getFullUser applied to the client's UserData (api_users.cpp).
void RequestUserFull(
	not_null<UserData*> user,
	Fn<void()> done,
	Fn<void(const QString &error)> fail);

} // namespace Damza::Plugins::ChatsApi
