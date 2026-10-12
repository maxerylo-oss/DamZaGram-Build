// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
//
// Built-in plugins: installs the plugins embedded from plugins/builtin/
// into tdata/plugins/ on start (version-aware) and adds the cross-plugin
// command helpers dzg.commands.all() / dzg.commands.run().
#include "damza/plugins/plugin_api.h"

#include "damza/plugins/builtin/builtin_files.h"
#include "damza/plugins/plugin_bridge.h"
#include "damza/plugins/plugin_data.h"
#include "damza/plugins/plugin_instance.h"
#include "damza/plugins/plugin_manager.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>

namespace Damza::Plugins {
namespace {

struct BuiltinState {
	QString id;
	QString name;
	QString version;
	QString installedVersion;
	bool enabledByDefault = true;
	bool replaced = false;
	bool updated = false;
	QString error;
};

[[nodiscard]] std::vector<BuiltinState> &States() {
	static auto result = std::vector<BuiltinState>();
	return result;
}

[[nodiscard]] QByteArray Contents(const Builtin::File &file) {
	auto result = QByteArray();
	for (auto i = 0; file.chunks && file.chunks[i]; ++i) {
		// Binary chunks may contain zero bytes: they come with sizes.
		if (file.sizes) {
			result.append(file.chunks[i], file.sizes[i]);
		} else {
			result.append(file.chunks[i]);
		}
	}
	return result;
}

[[nodiscard]] QJsonObject ParseObject(const QByteArray &data) {
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(data, &error);
	return (error.error == QJsonParseError::NoError)
		? document.object()
		: QJsonObject();
}

[[nodiscard]] QJsonObject ReadObject(const QString &path) {
	auto file = QFile(path);
	return file.open(QIODevice::ReadOnly)
		? ParseObject(file.readAll())
		: QJsonObject();
}

struct Version {
	std::array<int, 3> numbers = {};
	bool prerelease = false;
};

[[nodiscard]] Version ParseVersion(QString text) {
	text = text.trimmed();
	if (text.startsWith('v') || text.startsWith('V')) {
		text = text.mid(1);
	}
	auto result = Version();
	const auto suffix = text.indexOf(QRegularExpression(u"[-+]"_q));
	if (suffix >= 0) {
		result.prerelease = (text[suffix] == '-');
		text = text.left(suffix);
	}
	const auto parts = text.split('.');
	for (auto i = 0; i != 3 && i < parts.size(); ++i) {
		result.numbers[i] = parts[i].toInt();
	}
	return result;
}

// -1 if a < b, 0 if equal, 1 if a > b. "1.0.0-beta" < "1.0.0".
[[nodiscard]] int CompareVersions(const QString &a, const QString &b) {
	const auto x = ParseVersion(a);
	const auto y = ParseVersion(b);
	for (auto i = 0; i != 3; ++i) {
		if (x.numbers[i] != y.numbers[i]) {
			return (x.numbers[i] < y.numbers[i]) ? -1 : 1;
		}
	}
	if (x.prerelease != y.prerelease) {
		return x.prerelease ? -1 : 1;
	}
	return 0;
}

[[nodiscard]] bool WriteFile(const QString &path, const QByteArray &data) {
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	file.write(data);
	return file.commit();
}

// A symlink (or a Windows junction) anywhere on the way to a plugin file
// is a developer's live copy: QSaveFile would write through it.
[[nodiscard]] bool LinkedPath(const QString &root, const QString &relative) {
	auto path = root;
	for (const auto &part : relative.split('/')) {
		path += part;
		const auto info = QFileInfo(path);
		if (info.isSymLink() || info.isJunction()) {
			return true;
		}
		path += '/';
	}
	return false;
}

[[nodiscard]] QString PluginIdOf(const Builtin::File &file) {
	const auto path = QString::fromUtf8(file.path);
	return path.left(path.indexOf('/'));
}

void InstallBuiltins() {
	auto &manager = Manager::Get();
	const auto root = manager.pluginsPath();
	auto byId = std::map<QString, std::vector<const Builtin::File*>>();
	for (const auto &file : Builtin::Files()) {
		const auto id = PluginIdOf(file);
		if (!id.isEmpty()) {
			byId[id].push_back(&file);
		}
	}
	auto &states = States();
	states.clear();
	for (const auto &[id, files] : byId) {
		const auto manifestPath = id + u"/manifest.json"_q;
		const auto i = ranges::find_if(files, [&](const Builtin::File *f) {
			return QString::fromUtf8(f->path) == manifestPath;
		});
		if (i == end(files)) {
			continue;
		}
		const auto embedded = ParseObject(Contents(**i));
		auto state = BuiltinState{
			.id = id,
			.name = embedded.value(u"name"_q).toString(id),
			.version = embedded.value(u"version"_q).toString(u"0.0.0"_q),
			.enabledByDefault = embedded.value(
				u"enabledByDefault"_q).toBool(true),
		};
		const auto target = root + manifestPath;
		// A symlink is a developer's live copy: never write through it.
		const auto linked = ranges::any_of(files, [&](
				const Builtin::File *f) {
			return LinkedPath(root, QString::fromUtf8(f->path));
		});
		auto install = !linked && !QFile::exists(target);
		if (QFile::exists(target)) {
			const auto installed = ReadObject(target);
			state.installedVersion = installed.value(
				u"version"_q).toString();
			if (!installed.value(u"builtin"_q).toBool()) {
				// The user replaced the plugin with their own one.
				state.replaced = true;
			} else if (!linked && CompareVersions(
					state.installedVersion,
					state.version) < 0) {
				install = true;
			}
		}
		if (install) {
			// The manifest goes last: stop at the first failure, so that a
			// half-written plugin keeps the old version and is retried.
			const auto ok = ranges::all_of(files, [&](
					const Builtin::File *file) {
				return WriteFile(
					root + QString::fromUtf8(file->path),
					Contents(*file));
			});
			if (ok) {
				state.installedVersion = state.version;
				state.updated = true;
			} else {
				state.error = u"Could not write to "_q + root + id;
			}
		}
		if (!state.replaced && state.enabledByDefault) {
			manager.enableByDefault(id);
		}
		states.push_back(std::move(state));
	}
}

[[nodiscard]] QVariantList BuiltinList() {
	auto result = QVariantList();
	for (const auto &state : States()) {
		result.push_back(QVariantMap{
			{ u"id"_q, state.id },
			{ u"name"_q, state.name },
			{ u"version"_q, state.version },
			{ u"installedVersion"_q, state.installedVersion },
			{ u"enabledByDefault"_q, state.enabledByDefault },
			{ u"replaced"_q, state.replaced },
			{ u"updated"_q, state.updated },
			{ u"error"_q, state.error },
		});
	}
	return result;
}

// Commands of every loaded plugin except the caller (it lists its own).
[[nodiscard]] QVariantList ForeignCommands(const QString &callerId) {
	auto &manager = Manager::Get();
	auto result = QVariantList();
	for (const auto &id : manager.loadedIds()) {
		if (id == callerId) {
			continue;
		}
		const auto plugin = manager.instance(id);
		if (!plugin || plugin->busy()) {
			continue;
		}
		const auto list = plugin->dispatch(
			u"builtin:commands"_q,
			QVariant()).toList();
		for (const auto &entry : list) {
			auto map = entry.toMap();
			if (map.value(u"name"_q).toString().isEmpty()) {
				continue;
			}
			map.insert(u"plugin"_q, id);
			map.insert(u"pluginName"_q, plugin->manifest().name);
			result.push_back(map);
		}
	}
	return result;
}

[[nodiscard]] bool RunForeignCommand(
		const QString &callerId,
		const QString &pluginId,
		const QString &text,
		const QString &peerId) {
	if (pluginId == callerId) {
		return false;
	}
	auto &manager = Manager::Get();
	const auto plugin = manager.instance(pluginId);
	if (!plugin || plugin->busy()) {
		return false;
	}
	const auto peer = ResolvePeer(manager.activeSession(), peerId);
	return plugin->dispatch(u"builtin:run"_q, QVariantMap{
		{ u"text"_q, text },
		{ u"peerId"_q, peerId },
		{ u"chat"_q, peer ? QVariant(SerializePeer(peer)) : QVariant() },
		{ u"replyTo"_q, 0. },
	}).toBool();
}

} // namespace

DZG_API_MODULE({
	.name = "builtin",
	.natives = [](not_null<Bridge*> bridge, std::vector<Js::NativeMethod> &out) {
		using Args = QVariantList;
		const auto callerId = [=] {
			return bridge->instance()->manifest().id;
		};
		out.push_back({ "builtin_list", [](const Args &) {
			return QVariant(BuiltinList());
		} });
		out.push_back({ "builtin_commands", [=](const Args &) {
			return QVariant(ForeignCommands(callerId()));
		} });
		out.push_back({ "builtin_runCommand", [=](const Args &args) {
			return QVariant(RunForeignCommand(
				callerId(),
				args.value(0).toString(),
				args.value(1).toString(),
				args.value(2).toString()));
		} });
	},
	.prelude = [] {
		return QString::fromUtf8(R"DZG_JS(
	const parseCommand = (text) => {
		const match = /^\/([A-Za-z0-9_]+)(?:@\S+)?(?:\s+([\s\S]*))?$/.exec(String(text || '').trim());
		if (!match) return null;
		const rest = (match[2] || '').trim();
		return { name: match[1].toLowerCase(), rest, args: rest.length ? rest.split(/\s+/) : [] };
	};
	const runOwnCommand = (payload) => {
		const parsed = parseCommand(payload && payload.text);
		if (!parsed) return false;
		const command = internal.hooks.commands.get(parsed.name);
		if (!command || typeof command.run !== 'function') return false;
		internal.safe('command /' + parsed.name, command.run, {
			args: parsed.args,
			rest: parsed.rest,
			text: String(payload.text).trim(),
			chat: payload.chat || null,
			replyTo: Number(payload.replyTo) || 0,
		});
		return true;
	};
	internal.on('builtin:commands', () => dzg.commands.list());
	internal.on('builtin:run', (payload) => runOwnCommand(payload));

	Object.assign(dzg.commands, {
		all() {
			const own = dzg.commands.list().map((c) => Object.assign({
				plugin: dzg.plugin.id,
				pluginName: dzg.plugin.name,
			}, c));
			return own.concat(native.builtin_commands() || []);
		},
		run(text, chat) {
			let value = String(text || '').trim();
			if (!value.startsWith('/')) value = '/' + value;
			const parsed = parseCommand(value);
			if (!parsed) return false;
			let target = (chat === undefined || chat === null) ? dzg.chats.current() : chat;
			if (typeof target === 'string') target = dzg.chats.get(target);
			if (internal.hooks.commands.has(parsed.name)) {
				return runOwnCommand({ text: value, chat: target, replyTo: 0 });
			}
			const owner = (native.builtin_commands() || []).find((c) => c.name === parsed.name);
			if (!owner) return false;
			return !!native.builtin_runCommand(owner.plugin, value, internal.toPeerId(target));
		},
	});

	dzg.builtin = Object.freeze({
		list() { return native.builtin_list() || []; },
	});
)DZG_JS");
	},
	.appStarted = [] {
		InstallBuiltins();
	},
});

} // namespace Damza::Plugins
