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
#include "damza/plugins/plugin_instance.h"
#include "damza/plugins/plugin_manager.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QFont>
#include <QtGui/QFontMetrics>
#include <QtGui/QImage>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <algorithm>

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

// Accent colour pulled from the cover (average, saturated a bit), or a
// Spotify-ish green when there is no art.
[[nodiscard]] QColor AccentFrom(const QImage &cover) {
	if (cover.isNull()) {
		return QColor(0x1d, 0xb9, 0x54);
	}
	const auto one = cover
		.scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
		.convertToFormat(QImage::Format_ARGB32);
	auto color = one.pixelColor(0, 0);
	int h = 0, s = 0, v = 0;
	color.getHsv(&h, &s, &v);
	if (h < 0) {
		h = 150;
	}
	s = std::min(255, s + 70);
	v = std::clamp(v + 40, 150, 255);
	return QColor::fromHsv(h, s, v);
}

// Draw a "now playing" card (cover + title/artist/album) to a PNG under the
// plugins data dir and return its path. Cross-platform: just QPainter.
[[nodiscard]] QString RenderCardFile(
		const QString &pluginId,
		const QVariantMap &p,
		const QImage &cover) {
	constexpr auto kW = 1080;
	constexpr auto kH = 424;
	constexpr auto kM = 44;
	constexpr auto kArt = kH - 2 * kM;

	auto img = QImage(kW, kH, QImage::Format_RGB32);
	auto painter = QPainter(&img);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);

	const auto accent = AccentFrom(cover);

	// Background: the cover scaled way down and back up reads as a cheap
	// blur; a dark overlay keeps the text readable. No art -> gradient.
	if (!cover.isNull()) {
		const auto small = cover.scaled(
			kW / 24,
			kH / 24,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		painter.drawImage(img.rect(), small.scaled(
			kW,
			kH,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation));
		painter.fillRect(img.rect(), QColor(0, 0, 0, 155));
	} else {
		auto gradient = QLinearGradient(0, 0, kW, kH);
		gradient.setColorAt(0, QColor(0x22, 0x24, 0x2d));
		gradient.setColorAt(1, QColor(0x12, 0x13, 0x18));
		painter.fillRect(img.rect(), gradient);
	}

	// Album art: square, rounded, centre-cropped.
	const auto artRect = QRect(kM, kM, kArt, kArt);
	auto artPath = QPainterPath();
	artPath.addRoundedRect(artRect, 26, 26);
	painter.save();
	painter.setClipPath(artPath);
	if (!cover.isNull()) {
		const auto scaled = cover.scaled(
			kArt,
			kArt,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
		painter.drawImage(artRect, scaled, QRect(
			(scaled.width() - kArt) / 2,
			(scaled.height() - kArt) / 2,
			kArt,
			kArt));
	} else {
		painter.fillRect(artRect, accent.darker(170));
	}
	painter.restore();
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(QColor(255, 255, 255, 36), 2));
	painter.drawPath(artPath);

	const auto tx = kM + kArt + 48;
	const auto tw = kW - tx - kM;
	auto font = painter.font();

	// "СЕЙЧАС ИГРАЕТ" label with an accent dot.
	auto label = p.value(u"label"_q).toString().toUpper();
	if (label.isEmpty()) {
		label = u"СЕЙЧАС ИГРАЕТ"_q;
	}
	font.setPixelSize(24);
	font.setBold(true);
	font.setLetterSpacing(QFont::AbsoluteSpacing, 2);
	painter.setFont(font);
	const auto labelY = kM + 14;
	painter.setPen(Qt::NoPen);
	painter.setBrush(accent);
	painter.drawEllipse(QPoint(tx + 7, labelY + 14), 7, 7);
	painter.setPen(accent);
	painter.drawText(
		QRect(tx + 28, labelY, tw - 28, 32),
		Qt::AlignVCenter | Qt::AlignLeft,
		label);

	// Title.
	font.setLetterSpacing(QFont::AbsoluteSpacing, 0);
	font.setPixelSize(58);
	font.setBold(true);
	painter.setFont(font);
	painter.setPen(QColor(255, 255, 255));
	const auto titleY = kM + 70;
	painter.drawText(
		QRect(tx, titleY, tw, 74),
		Qt::AlignLeft | Qt::AlignVCenter,
		QFontMetrics(font).elidedText(
			p.value(u"title"_q).toString(),
			Qt::ElideRight,
			tw));

	// Artist.
	font.setPixelSize(38);
	font.setBold(false);
	painter.setFont(font);
	painter.setPen(QColor(236, 236, 236));
	painter.drawText(
		QRect(tx, titleY + 80, tw, 54),
		Qt::AlignLeft | Qt::AlignVCenter,
		QFontMetrics(font).elidedText(
			p.value(u"artist"_q).toString(),
			Qt::ElideRight,
			tw));

	// Album (optional).
	const auto album = p.value(u"album"_q).toString();
	if (!album.isEmpty()) {
		font.setPixelSize(28);
		painter.setFont(font);
		painter.setPen(QColor(188, 188, 188));
		painter.drawText(
			QRect(tx, titleY + 146, tw, 42),
			Qt::AlignLeft | Qt::AlignVCenter,
			QFontMetrics(font).elidedText(album, Qt::ElideRight, tw));
	}

	painter.end();

	const auto dir = Manager::Get().dataPath();
	QDir().mkpath(dir);
	const auto path = dir + pluginId + u"-nowplaying.png"_q;
	return img.save(path, "PNG") ? path : QString();
}

// Start rendering: fetch the cover (http or file), draw, settle with a path.
void RenderCard(not_null<Bridge*> bridge, int id, const QVariantMap &payload) {
	const auto self = bridge.get();
	const auto pluginId = bridge->instance()->manifest().id;
	const auto finish = [=](const QImage &cover) {
		const auto path = RenderCardFile(pluginId, payload, cover);
		self->settle(
			id,
			!path.isEmpty(),
			path.isEmpty() ? QVariant(u"render failed"_q) : QVariant(path));
	};
	const auto cover = payload.value(u"cover"_q).toString();
	const auto url = QUrl(cover);
	if (cover.isEmpty()) {
		QTimer::singleShot(0, self, [=] { finish(QImage()); });
		return;
	}
	if (url.isLocalFile() || cover.startsWith(u"file://"_q)) {
		const auto local = url.toLocalFile();
		QTimer::singleShot(0, self, [=] {
			auto image = QImage();
			image.load(local);
			finish(image);
		});
		return;
	}
	auto request = QNetworkRequest(url);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(15000);
	const auto reply = Manager::Get().network()->get(request);
	QObject::connect(reply, &QNetworkReply::finished, self, [=] {
		reply->deleteLater();
		auto image = QImage();
		if (reply->error() == QNetworkReply::NoError) {
			image.loadFromData(reply->readAll());
		}
		finish(image);
	});
}

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
		out.push_back({ "media_renderCard", [bridge](const QVariantList &args) {
			const auto id = bridge->nextRequestId();
			RenderCard(bridge, id, args.value(0).toMap());
			return QVariant(id);
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
		// Нарисовать красивую карточку трека в PNG. -> Promise<путь>.
		// track: { title, artist, album, cover, label }.
		renderCard(track) {
			const t = track || {};
			return internal.request(() => native.media_renderCard({
				title: String(t.title || ''),
				artist: String(t.artist || ''),
				album: String(t.album || ''),
				cover: fixCover(t.cover),
				label: String(t.label || ''),
			}));
		},
	};
)DZG_JS");
	},
});

} // namespace Damza::Plugins
