// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
//
// dzg.media: what a local media player is playing, read over MPRIS
// (org.mpris.MediaPlayer2) on the session D-Bus bus. Linux only; on other
// platforms the natives return nothing. No Last.fm, no accounts — it reads
// the running player (Spotify and anything else that speaks MPRIS) directly.
#include "damza/plugins/plugin_api.h"
#include "damza/plugins/plugins.h"

#include "damza/plugins/plugin_bridge.h"

#include <QtCore/QStringList>

#ifdef Q_OS_LINUX
#include <gio/gio.h>
#endif // Q_OS_LINUX

namespace Damza::Plugins {
namespace {

#ifdef Q_OS_LINUX

// The session bus, fetched once. MPRIS lives on the session bus.
[[nodiscard]] GDBusConnection *SessionBus() {
	static GDBusConnection *bus = [] {
		GError *error = nullptr;
		const auto result = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
		if (error) {
			g_error_free(error);
		}
		return result;
	}();
	return bus;
}

[[nodiscard]] QString VariantString(GVariant *value) {
	if (!value) {
		return QString();
	}
	if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
		return QString::fromUtf8(g_variant_get_string(value, nullptr));
	}
	return QString();
}

// A single string from an a{sv} dict: either a plain 's' or the first of 'as'.
[[nodiscard]] QString DictString(GVariant *dict, const char *key) {
	if (!dict) {
		return QString();
	}
	auto result = QString();
	if (const auto str = g_variant_lookup_value(
			dict, key, G_VARIANT_TYPE_STRING)) {
		result = VariantString(str);
		g_variant_unref(str);
	} else if (const auto arr = g_variant_lookup_value(
			dict, key, G_VARIANT_TYPE_STRING_ARRAY)) {
		auto parts = QStringList();
		GVariantIter iter;
		g_variant_iter_init(&iter, arr);
		const char *item = nullptr;
		while (g_variant_iter_next(&iter, "&s", &item)) {
			const auto part = QString::fromUtf8(item).trimmed();
			if (!part.isEmpty()) {
				parts << part;
			}
		}
		g_variant_unref(arr);
		result = parts.join(u", "_q);
	}
	return result;
}

// All MPRIS bus names currently on the bus (org.mpris.MediaPlayer2.*).
[[nodiscard]] QStringList Players(GDBusConnection *bus) {
	auto result = QStringList();
	GError *error = nullptr;
	const auto reply = g_dbus_connection_call_sync(
		bus,
		"org.freedesktop.DBus",
		"/org/freedesktop/DBus",
		"org.freedesktop.DBus",
		"ListNames",
		nullptr,
		G_VARIANT_TYPE("(as)"),
		G_DBUS_CALL_FLAGS_NONE,
		1500,
		nullptr,
		&error);
	if (!reply) {
		if (error) {
			g_error_free(error);
		}
		return result;
	}
	const auto array = g_variant_get_child_value(reply, 0);
	GVariantIter iter;
	g_variant_iter_init(&iter, array);
	const char *name = nullptr;
	while (g_variant_iter_next(&iter, "&s", &name)) {
		if (g_str_has_prefix(name, "org.mpris.MediaPlayer2.")) {
			result << QString::fromUtf8(name);
		}
	}
	g_variant_unref(array);
	g_variant_unref(reply);
	return result;
}

// Player name after the MPRIS prefix: "spotify", "firefox.instance123", ...
[[nodiscard]] QString ShortName(const QString &busName) {
	static const auto kPrefix = u"org.mpris.MediaPlayer2."_q;
	return busName.startsWith(kPrefix)
		? busName.mid(kPrefix.size())
		: busName;
}

// PlaybackStatus + Metadata of one player, as a plugin-facing map, or empty.
[[nodiscard]] QVariantMap Query(GDBusConnection *bus, const QString &busName) {
	GError *error = nullptr;
	const auto reply = g_dbus_connection_call_sync(
		bus,
		busName.toUtf8().constData(),
		"/org/mpris/MediaPlayer2",
		"org.freedesktop.DBus.Properties",
		"GetAll",
		g_variant_new("(s)", "org.mpris.MediaPlayer2.Player"),
		G_VARIANT_TYPE("(a{sv})"),
		G_DBUS_CALL_FLAGS_NONE,
		1500,
		nullptr,
		&error);
	if (!reply) {
		if (error) {
			g_error_free(error);
		}
		return {};
	}
	const auto props = g_variant_get_child_value(reply, 0);
	auto status = QString();
	if (const auto value = g_variant_lookup_value(
			props, "PlaybackStatus", G_VARIANT_TYPE_STRING)) {
		status = VariantString(value);
		g_variant_unref(value);
	}
	auto map = QVariantMap();
	if (const auto meta = g_variant_lookup_value(
			props, "Metadata", G_VARIANT_TYPE("a{sv}"))) {
		map.insert(u"title"_q, DictString(meta, "xesam:title"));
		map.insert(u"artist"_q, DictString(meta, "xesam:artist"));
		map.insert(u"album"_q, DictString(meta, "xesam:album"));
		map.insert(u"cover"_q, DictString(meta, "mpris:artUrl"));
		map.insert(u"url"_q, DictString(meta, "xesam:url"));
		g_variant_unref(meta);
	}
	g_variant_unref(props);
	g_variant_unref(reply);
	if (map.value(u"title"_q).toString().isEmpty()
		&& map.value(u"artist"_q).toString().isEmpty()) {
		return {}; // nothing loaded in this player
	}
	map.insert(u"player"_q, ShortName(busName));
	map.insert(u"status"_q, status);
	map.insert(u"playing"_q, status == u"Playing"_q);
	return map;
}

// Pick a player: prefer one matching `prefer`, then a playing one, then any.
[[nodiscard]] QVariant NowPlaying(const QString &prefer) {
	const auto bus = SessionBus();
	if (!bus) {
		return QVariant();
	}
	const auto names = Players(bus);
	if (names.isEmpty()) {
		return QVariant();
	}
	auto preferred = QVariantMap();
	auto playing = QVariantMap();
	auto anyLoaded = QVariantMap();
	const auto want = prefer.trimmed().toLower();
	for (const auto &name : names) {
		const auto map = Query(bus, name);
		if (map.isEmpty()) {
			continue;
		}
		const auto isPlaying = map.value(u"playing"_q).toBool();
		const auto matches = !want.isEmpty()
			&& ShortName(name).toLower().contains(want);
		if (matches && isPlaying) {
			return map; // best possible: the wanted player, playing
		}
		if (matches && preferred.isEmpty()) {
			preferred = map;
		}
		if (isPlaying && playing.isEmpty()) {
			playing = map;
		}
		if (anyLoaded.isEmpty()) {
			anyLoaded = map;
		}
	}
	if (!preferred.isEmpty()) {
		return preferred;
	}
	if (!playing.isEmpty()) {
		return playing;
	}
	if (!anyLoaded.isEmpty()) {
		return anyLoaded;
	}
	return QVariant();
}

[[nodiscard]] QVariant PlayersList() {
	const auto bus = SessionBus();
	if (!bus) {
		return QVariant();
	}
	auto result = QVariantList();
	for (const auto &name : Players(bus)) {
		const auto map = Query(bus, name);
		result.push_back(QVariantMap{
			{ u"player"_q, ShortName(name) },
			{ u"playing"_q, map.value(u"playing"_q, false) },
			{ u"status"_q, map.value(u"status"_q) },
			{ u"title"_q, map.value(u"title"_q) },
			{ u"artist"_q, map.value(u"artist"_q) },
		});
	}
	return result;
}

#else // Q_OS_LINUX

[[nodiscard]] QVariant NowPlaying(const QString &) {
	return QVariant();
}

[[nodiscard]] QVariant PlayersList() {
	return QVariant();
}

#endif // Q_OS_LINUX

} // namespace

DZG_API_MODULE({
	.name = "media",
	.natives = [](not_null<Bridge*> bridge, std::vector<Js::NativeMethod> &out) {
		out.push_back({ "media_nowPlaying", [](const QVariantList &args) {
			return NowPlaying(args.value(0).toString());
		} });
		out.push_back({ "media_players", [](const QVariantList &) {
			return PlayersList();
		} });
	},
	.prelude = [] {
		return QString::fromUtf8(R"DZG_JS(
	// Spotify's mpris:artUrl is often an open.spotify.com/image link that
	// doesn't render; the i.scdn.co host with the same hash does.
	const fixCover = (url) => {
		const s = String(url || '');
		return s.replace('https://open.spotify.com/image/', 'https://i.scdn.co/image/');
	};
	const normalize = (raw) => {
		if (!raw || typeof raw !== 'object') return null;
		if (!raw.title && !raw.artist) return null;
		return {
			title: String(raw.title || ''),
			artist: String(raw.artist || ''),
			album: String(raw.album || ''),
			cover: fixCover(raw.cover),
			url: String(raw.url || ''),
			status: String(raw.status || ''),
			playing: !!raw.playing,
			player: String(raw.player || ''),
		};
	};
	dzg.media = {
		// Что играет сейчас. opts: строка-имя плеера или { player }.
		// Linux + MPRIS (Spotify и любой другой плеер). Иначе null.
		nowPlaying(opts) {
			const o = (typeof opts === 'string') ? { player: opts } : (opts || {});
			return normalize(native.media_nowPlaying(o.player ? String(o.player) : ''));
		},
		// Список MPRIS-плееров на шине (для отладки/выбора).
		players() { return native.media_players() || []; },
	};
)DZG_JS");
	},
});

} // namespace Damza::Plugins
