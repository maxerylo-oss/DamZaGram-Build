// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/plugins/plugin_install.h"

#include "base/flat_set.h"
#include "base/zlib_help.h"
#include "damza/damza_lang.h"
#include "damza/plugins/builtin/builtin_files.h"
#include "damza/plugins/plugin_manager.h"
#include "damza/plugins/plugin_types_embedded.h"

#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>
#include <QtGui/QDesktopServices>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Damza::Plugins {
namespace {

constexpr auto kMaxPackageSize = qint64(32 * 1024 * 1024);
constexpr auto kMaxFileSize = 16 * 1024 * 1024;
constexpr auto kMaxFiles = 4000;
constexpr auto kMaxZipName = 1024;
constexpr auto kDownloadTimeout = 60000;

const auto kSkipDirs = std::array{
	u".git"_q,
	u"node_modules"_q,
	u".vscode"_q,
	u".idea"_q,
	u"__MACOSX"_q,
};

[[nodiscard]] QString ManifestFileName() {
	return u"manifest.json"_q;
}

[[nodiscard]] QString CleanRelative(QString path) {
	path.replace('\\', '/');
	path = QDir::cleanPath(path);
	while (path.startsWith(u"./"_q)) {
		path = path.mid(2);
	}
	if (path.isEmpty()
		|| path == u"."_q
		|| path == u".."_q
		|| path.startsWith(u"../"_q)
		|| path.startsWith('/')
		|| path.contains(':')) {
		return QString();
	}
	return path;
}

[[nodiscard]] bool SkippedPath(const QString &relative) {
	const auto parts = relative.split('/', Qt::SkipEmptyParts);
	for (const auto &part : parts) {
		if (ranges::contains(kSkipDirs, part) || part == u".DS_Store"_q) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QString StripPluginSuffix(QString name) {
	for (const auto &suffix : {
			u".dzg.zip"_q,
			u".dzg.js"_q,
			u".plugin.js"_q,
			u".zip"_q,
			u".mjs"_q,
			u".js"_q }) {
		if (name.endsWith(suffix, Qt::CaseInsensitive)) {
			return name.left(name.size() - suffix.size());
		}
	}
	return name;
}

[[nodiscard]] bool IsLoopbackHost(const QString &host) {
	const auto lower = host.toLower();
	return (lower == u"localhost"_q)
		|| lower.endsWith(u".localhost"_q)
		|| (lower == u"127.0.0.1"_q)
		|| (lower == u"::1"_q);
}

[[nodiscard]] QString SourceBaseName(const QString &source) {
	const auto url = QUrl(source);
	const auto path = (url.isValid() && !url.scheme().isEmpty()
		&& url.scheme().size() > 1)
		? url.path()
		: source;
	return StripPluginSuffix(QFileInfo(path).fileName());
}

void Finalize(Package &package) {
	if (!package.error.isEmpty()) {
		return;
	}
	const auto id = package.id();
	if (id.isEmpty()) {
		package.error = Damza::Text("manifest.json has no \"id\".", "В manifest.json нет \"id\".");
	} else if (!IsValidPluginId(id)
		|| package.manifest.value(u"id"_q).toString() != id) {
		// The client reads the id from manifest.json as is, untrimmed.
		package.error = Damza::Text(
			"Bad plugin id \"%1\": use latin letters, digits, "
			"'-', '_' and '.'.",
			"Плохой id плагина \"%1\": только латиница, цифры, "
			"'-', '_' и '.'.").arg(id);
	} else {
		const auto main = package.manifest.value(u"main"_q).toString(
			u"main.js"_q);
		if (!package.files.contains(CleanRelative(main))) {
			package.error = Damza::Text("Entry file \"%1\" is missing.", "Нет входного файла \"%1\".").arg(main);
		} else if (package.totalSize() > kMaxPackageSize) {
			package.error = Damza::Text("The plugin is too large.", "Плагин слишком большой.");
		}
	}
}

[[nodiscard]] QString ZipEntryName(zlib::FileToRead &zip) {
	auto info = unz_file_info();
	char name[kMaxZipName + 1] = { 0 };
	const auto result = zip.getCurrentFileInfo(
		&info,
		name,
		kMaxZipName,
		nullptr,
		0,
		nullptr,
		0);
	return (result == UNZ_OK) ? QString::fromUtf8(name) : QString();
}

[[nodiscard]] bool WriteFile(const QString &path, const QByteArray &data) {
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QFile(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
		&& (file.write(data) == data.size());
}

[[nodiscard]] bool CopyRecursively(const QString &from, const QString &to) {
	auto ok = true;
	auto it = QDirIterator(
		from,
		QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
		QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const auto path = it.next();
		const auto relative = QDir(from).relativeFilePath(path);
		const auto target = QDir(to).filePath(relative);
		QDir().mkpath(QFileInfo(target).absolutePath());
		QFile::remove(target);
		ok = QFile::copy(path, target) && ok;
	}
	return ok;
}

[[nodiscard]] bool InsidePluginsFolder(const QString &dir) {
	const auto root = QDir(Manager::Get().pluginsPath()).absolutePath();
	const auto path = QDir(dir).absolutePath();
	return path.startsWith(root + '/') && path != root;
}

// A plugin folder that is a symlink (or a junction) is a developer's live
// copy: QDir::removeRecursively() would wipe the folder it points to.
[[nodiscard]] bool IsLink(const QString &path) {
	const auto info = QFileInfo(path);
	return info.isSymLink() || info.isJunction();
}

// Removes the link itself, never the files it points to.
[[nodiscard]] bool RemoveLink(const QString &path) {
	return QFile::remove(path) || QDir().rmdir(path);
}

[[nodiscard]] bool IsShippedBuiltin(const QString &id) {
	static const auto ids = [] {
		auto result = base::flat_set<QString>();
		for (const auto &file : Builtin::Files()) {
			const auto path = QString::fromUtf8(file.path);
			result.emplace(path.left(path.indexOf('/')));
		}
		return result;
	}();
	return !id.isEmpty() && ids.contains(id);
}

[[nodiscard]] QString CommandName(const QString &id) {
	auto result = id;
	result.replace(QRegularExpression(u"[^A-Za-z0-9_]"_q), u"_"_q);
	return result.toLower();
}

[[nodiscard]] QString OneLine(QString text) {
	text.replace('\r', ' ');
	text.replace('\n', ' ');
	return text.trimmed();
}

[[nodiscard]] QByteArray ScaffoldMain(const ScaffoldOptions &options) {
	auto result = QString::fromUtf8(R"DZG_TEMPLATE(// %NAME% — плагин для DamZaGram.
// Автор: %AUTHOR%
//
// Это ES-модуль: работают import/export, async/await и top-level await.
// Подсказки по API в VS Code берутся из types/*.d.ts (см. jsconfig.json).
// Пока в настройках плагинов включена горячая перезагрузка, плагин
// перезапускается при каждом сохранении файла.

const { ui, messages, commands, settings } = dzg;

const greeting = () => {
  const text = String(settings.get('greeting') || 'Привет!');
  return settings.get('mode') === 'loud' ? text.toUpperCase() + '!' : text;
};

dzg.on('ready', () => {
  ui.toast(greeting(), dzg.plugin.name);
});

// Напиши в любом чате «/%COMMAND% мир»: команда не уйдёт в чат,
// вместо неё плагин отправит своё сообщение.
commands.register('%COMMAND%', {
  description: 'Пример команды плагина ' + dzg.plugin.name,
  run({ chat, rest }) {
    messages.send(chat.id, `${greeting()} ${rest}`.trim());
  },
});

// Пункт в контекстном меню сообщения.
ui.addMessageMenuItem({
  title: 'Скопировать как цитату',
  when: (message) => !!message.text,
  onClick(message) {
    dzg.clipboard.set(message.text.split('\n').map((line) => '> ' + line).join('\n'));
    ui.toast('Скопировано');
  },
});

// Тост, если во входящем сообщении есть слово из настроек.
dzg.on('message.new', (message) => {
  const word = String(settings.get('keyword') || '').trim().toLowerCase();
  if (!settings.get('notify') || !word || message.out) return;
  if (message.text.toLowerCase().includes(word)) {
    ui.toast(`${message.fromName}: ${message.text}`, message.chat.name);
  }
});

dzg.on('settings.changed', ({ key, value }) => {
  console.log('Настройка изменена:', key, value);
});

dzg.on('unload', () => console.log(dzg.plugin.name + ' выгружен'));
)DZG_TEMPLATE");
	result.replace(u"%NAME%"_q, OneLine(options.name));
	result.replace(u"%AUTHOR%"_q, OneLine(options.author));
	result.replace(u"%COMMAND%"_q, CommandName(options.id));
	return result.toUtf8();
}

[[nodiscard]] QJsonObject ScaffoldManifest(const ScaffoldOptions &options) {
	const auto setting = [](
			const QString &key,
			const QString &type,
			const QString &title,
			const QJsonValue &value) {
		return QJsonObject{
			{ u"key"_q, key },
			{ u"type"_q, type },
			{ u"title"_q, title },
			{ u"default"_q, value },
		};
	};
	auto mode = setting(
		u"mode"_q,
		u"select"_q,
		u"Режим приветствия"_q,
		u"normal"_q);
	mode.insert(u"options"_q, QJsonArray{
		QJsonObject{ { u"value"_q, u"normal"_q }, { u"title"_q, u"Обычный"_q } },
		QJsonObject{ { u"value"_q, u"loud"_q }, { u"title"_q, u"Громкий"_q } },
	});
	return QJsonObject{
		{ u"id"_q, options.id },
		{ u"name"_q, options.name },
		{ u"version"_q, u"0.1.0"_q },
		{ u"author"_q, options.author },
		{ u"description"_q, options.description },
		{ u"main"_q, u"main.js"_q },
		{ u"type"_q, u"module"_q },
		{ u"settings"_q, QJsonArray{
			setting(
				u"greeting"_q,
				u"string"_q,
				u"Приветствие"_q,
				u"Привет из плагина!"_q),
			mode,
			setting(
				u"notify"_q,
				u"bool"_q,
				u"Тост, если пришло ключевое слово"_q,
				true),
			setting(
				u"keyword"_q,
				u"string"_q,
				u"Ключевое слово"_q,
				u"damza"_q),
		} },
	};
}

} // namespace

QString Package::id() const {
	return manifest.value(u"id"_q).toString().trimmed();
}

QString Package::name() const {
	const auto result = manifest.value(u"name"_q).toString().trimmed();
	return result.isEmpty() ? id() : result;
}

QString Package::version() const {
	return manifest.value(u"version"_q).toString(u"0.0.0"_q);
}

QString Package::author() const {
	return manifest.value(u"author"_q).toString();
}

QString Package::description() const {
	return manifest.value(u"description"_q).toString();
}

qint64 Package::totalSize() const {
	auto result = qint64();
	for (const auto &[path, content] : files) {
		result += content.size();
	}
	return result;
}

bool Package::valid() const {
	return error.isEmpty() && IsValidPluginId(id()) && !files.empty();
}

bool IsValidPluginId(const QString &id) {
	static const auto re = QRegularExpression(
		u"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"_q);
	return re.match(id).hasMatch();
}

QString SanitizePluginId(const QString &text) {
	auto result = text.trimmed().toLower();
	result.replace(QRegularExpression(u"[^a-z0-9._-]+"_q), u"-"_q);
	while (!result.isEmpty() && !result[0].isLetterOrNumber()) {
		result.remove(0, 1);
	}
	while (result.endsWith('-') || result.endsWith('.')) {
		result.chop(1);
	}
	result = result.left(64);
	return result.isEmpty() ? u"plugin"_q : result;
}

bool IsPluginFileName(const QString &name) {
	return name.endsWith(u".dzg.zip"_q, Qt::CaseInsensitive)
		|| name.endsWith(u".dzg.js"_q, Qt::CaseInsensitive)
		|| name.endsWith(u".plugin.js"_q, Qt::CaseInsensitive);
}

QJsonObject ParseScriptHeader(const QString &code) {
	auto result = QJsonObject();
	auto settings = QJsonArray();
	auto inBlock = false;
	const auto lines = code.split('\n');
	for (auto line : lines) {
		line = line.trimmed();
		if (line.isEmpty()) {
			continue;
		} else if (inBlock) {
			const auto end = line.indexOf(u"*/"_q);
			if (end >= 0) {
				line = line.left(end);
				inBlock = false;
			}
		} else if (line.startsWith(u"//"_q)) {
			line = line.mid(2);
		} else if (line.startsWith(u"/*"_q)) {
			line = line.mid(2);
			const auto end = line.indexOf(u"*/"_q);
			if (end >= 0) {
				line = line.left(end);
			} else {
				inBlock = true;
			}
		} else if (line.startsWith(u"#!"_q)) {
			continue;
		} else {
			break;
		}
		line = line.trimmed();
		while (line.startsWith('*')) {
			line = line.mid(1).trimmed();
		}
		if (!line.startsWith('@')) {
			continue;
		}
		const auto space = line.indexOf(QRegularExpression(u"\\s"_q));
		const auto key = ((space < 0) ? line.mid(1) : line.mid(1, space - 1))
			.trimmed();
		const auto value = (space < 0)
			? QString()
			: line.mid(space + 1).trimmed();
		if (key.isEmpty()) {
			continue;
		} else if (key == u"setting"_q) {
			const auto object = QJsonDocument::fromJson(value.toUtf8());
			if (object.isObject()) {
				settings.push_back(object.object());
			}
		} else if (key == u"settings"_q) {
			const auto array = QJsonDocument::fromJson(value.toUtf8());
			for (const auto &entry : array.array()) {
				settings.push_back(entry);
			}
		} else if (key == u"description"_q
			&& result.contains(u"description"_q)) {
			result.insert(
				key,
				result.value(key).toString() + ' ' + value);
		} else {
			result.insert(key, value);
		}
	}
	if (!settings.isEmpty()) {
		result.insert(u"settings"_q, settings);
	}
	return result;
}

Package ReadPackageFromScript(
		const QByteArray &code,
		const QString &source) {
	auto result = Package{ .source = source, .generatedManifest = true };
	const auto header = ParseScriptHeader(QString::fromUtf8(code));
	auto manifest = header;
	auto id = header.value(u"id"_q).toString().trimmed();
	if (id.isEmpty()) {
		const auto name = header.value(u"name"_q).toString();
		id = SanitizePluginId(name.isEmpty() ? SourceBaseName(source) : name);
	}
	manifest.insert(u"id"_q, id);
	if (manifest.value(u"name"_q).toString().trimmed().isEmpty()) {
		manifest.insert(u"name"_q, id);
	}
	if (manifest.value(u"version"_q).toString().isEmpty()) {
		manifest.insert(u"version"_q, u"1.0.0"_q);
	}
	if (manifest.value(u"type"_q).toString() != u"script"_q) {
		manifest.insert(u"type"_q, u"module"_q);
	}
	manifest.insert(u"main"_q, u"main.js"_q);
	result.manifest = manifest;
	result.files.emplace(u"main.js"_q, code);
	result.files.emplace(
		ManifestFileName(),
		QJsonDocument(manifest).toJson(QJsonDocument::Indented));
	Finalize(result);
	return result;
}

Package ReadPackageFromDirectory(const QString &path) {
	auto result = Package{ .source = path };
	const auto dir = QDir(path);
	if (!dir.exists()) {
		result.error = Damza::Text("Folder not found: %1", "Папка не найдена: %1").arg(path);
		return result;
	}
	if (!dir.exists(ManifestFileName())) {
		const auto scripts = dir.entryList(
			{ u"*.js"_q, u"*.mjs"_q },
			QDir::Files);
		if (scripts.size() == 1) {
			auto file = QFile(dir.filePath(scripts.front()));
			if (file.size() <= kMaxFileSize
				&& file.open(QIODevice::ReadOnly)) {
				return ReadPackageFromScript(
					file.readAll(),
					dir.filePath(scripts.front()));
			}
		}
		result.error = Damza::Text("No manifest.json in %1", "Нет manifest.json в %1").arg(path);
		return result;
	}
	auto it = QDirIterator(
		path,
		QDir::Files | QDir::NoDotAndDotDot,
		QDirIterator::Subdirectories);
	auto total = qint64();
	while (it.hasNext()) {
		const auto full = it.next();
		const auto relative = CleanRelative(dir.relativeFilePath(full));
		if (relative.isEmpty() || SkippedPath(relative)) {
			continue;
		}
		auto file = QFile(full);
		if (file.size() > kMaxFileSize || !file.open(QIODevice::ReadOnly)) {
			result.error = Damza::Text("Can't read %1", "Не удалось прочитать %1").arg(relative);
			return result;
		}
		const auto content = file.readAll();
		total += content.size();
		if (total > kMaxPackageSize || result.files.size() > kMaxFiles) {
			result.error = Damza::Text("The plugin folder is too large.", "Папка плагина слишком большая.");
			return result;
		}
		result.files.emplace(relative, content);
	}
	auto parseError = QJsonParseError();
	const auto document = QJsonDocument::fromJson(
		result.files[ManifestFileName()],
		&parseError);
	if (parseError.error != QJsonParseError::NoError) {
		result.error = u"manifest.json: "_q + parseError.errorString();
		return result;
	}
	result.manifest = document.object();
	if (!result.manifest.contains(u"id"_q)) {
		result.manifest.insert(
			u"id"_q,
			SanitizePluginId(QFileInfo(dir.absolutePath()).fileName()));
	}
	Finalize(result);
	return result;
}

Package ReadPackageFromZip(const QByteArray &bytes, const QString &source) {
	auto result = Package{ .source = source };
	auto zip = zlib::FileToRead(bytes);
	if (zip.goToFirstFile() != UNZ_OK) {
		result.error = Damza::Text("Not a zip archive.", "Это не zip-архив.");
		return result;
	}
	auto entries = std::map<QString, QByteArray>();
	auto total = qint64();
	do {
		const auto raw = ZipEntryName(zip);
		const auto name = CleanRelative(raw);
		if (!raw.endsWith('/') && !name.isEmpty() && !SkippedPath(name)) {
			const auto content = zip.readCurrentFileContent(kMaxFileSize);
			if (zip.error() != UNZ_OK) {
				result.error = Damza::Text("Can't unpack %1", "Не удалось распаковать %1").arg(name);
				return result;
			}
			total += content.size();
			if (total > kMaxPackageSize || entries.size() > kMaxFiles) {
				result.error = Damza::Text("The archive is too large.", "Архив слишком большой.");
				return result;
			}
			entries.emplace(name, content);
		}
		const auto jump = zip.goToNextFile();
		if (jump == UNZ_END_OF_LIST_OF_FILE) {
			break;
		} else if (jump != UNZ_OK) {
			result.error = Damza::Text("The archive is damaged.", "Архив повреждён.");
			return result;
		}
	} while (true);

	auto manifestPath = QString();
	for (const auto &[name, content] : entries) {
		if (name != ManifestFileName()
			&& !name.endsWith('/' + ManifestFileName())) {
			continue;
		}
		if (manifestPath.isEmpty()
			|| name.count('/') < manifestPath.count('/')) {
			manifestPath = name;
		}
	}
	if (manifestPath.isEmpty()) {
		auto scripts = std::vector<QString>();
		for (const auto &[name, content] : entries) {
			if (name.endsWith(u".js"_q, Qt::CaseInsensitive)) {
				scripts.push_back(name);
			}
		}
		if (scripts.size() == 1) {
			auto script = ReadPackageFromScript(
				entries[scripts.front()],
				scripts.front());
			script.source = source;
			return script;
		}
		result.error = Damza::Text("No manifest.json in the archive.", "В архиве нет manifest.json.");
		return result;
	}
	const auto prefix = manifestPath.left(
		manifestPath.size() - ManifestFileName().size());
	for (auto &[name, content] : entries) {
		if (name.startsWith(prefix)) {
			result.files.emplace(name.mid(prefix.size()), std::move(content));
		}
	}
	auto parseError = QJsonParseError();
	const auto document = QJsonDocument::fromJson(
		result.files[ManifestFileName()],
		&parseError);
	if (parseError.error != QJsonParseError::NoError) {
		result.error = u"manifest.json: "_q + parseError.errorString();
		return result;
	}
	result.manifest = document.object();
	if (!result.manifest.contains(u"id"_q)) {
		const auto folder = prefix.isEmpty()
			? SourceBaseName(source)
			: prefix.section('/', -2, -2);
		result.manifest.insert(u"id"_q, SanitizePluginId(folder));
	}
	Finalize(result);
	return result;
}

Package ReadPackageFromBytes(const QByteArray &bytes, const QString &source) {
	if (bytes.startsWith("PK\x03\x04")
		|| source.endsWith(u".zip"_q, Qt::CaseInsensitive)) {
		return ReadPackageFromZip(bytes, source);
	}
	const auto head = bytes.left(512).trimmed().toLower();
	if (head.startsWith("<!doctype") || head.startsWith("<html")) {
		return Package{
			.source = source,
			.error = Damza::Text("Got a web page instead of a plugin file.", "По ссылке веб-страница, а не файл плагина."),
		};
	}
	return ReadPackageFromScript(bytes, source);
}

Package ReadPackageFromPath(const QString &path) {
	const auto info = QFileInfo(path);
	if (info.isDir()) {
		return ReadPackageFromDirectory(path);
	} else if (info.fileName() == ManifestFileName()) {
		return ReadPackageFromDirectory(info.absolutePath());
	} else if (!info.exists()) {
		return Package{
			.source = path,
			.error = Damza::Text("File not found: %1", "Файл не найден: %1").arg(path),
		};
	} else if (info.size() > kMaxPackageSize) {
		return Package{
			.source = path,
			.error = Damza::Text("The file is too large.", "Файл слишком большой."),
		};
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return Package{
			.source = path,
			.error = Damza::Text("Can't open %1", "Не удалось открыть %1").arg(path),
		};
	}
	return ReadPackageFromBytes(file.readAll(), info.fileName());
}

QString NormalizePackageUrl(const QString &url) {
	auto result = url.trimmed();
	if (result.startsWith(u"github.com/"_q)
		|| result.startsWith(u"raw.githubusercontent.com/"_q)) {
		result = u"https://"_q + result;
	}
	static const auto blob = QRegularExpression(
		u"^https?://github\\.com/([^/]+)/([^/]+)/blob/(.+)$"_q);
	static const auto repo = QRegularExpression(
		u"^https?://github\\.com/([^/]+)/([^/?#]+?)(?:\\.git)?/?$"_q);
	static const auto tree = QRegularExpression(
		u"^https?://github\\.com/([^/]+)/([^/]+)/tree/([^/?#]+)/?$"_q);
	if (const auto match = blob.match(result); match.hasMatch()) {
		return u"https://raw.githubusercontent.com/%1/%2/%3"_q.arg(
			match.captured(1),
			match.captured(2),
			match.captured(3));
	} else if (const auto match = repo.match(result); match.hasMatch()) {
		return u"https://github.com/%1/%2/archive/HEAD.zip"_q.arg(
			match.captured(1),
			match.captured(2));
	} else if (const auto match = tree.match(result); match.hasMatch()) {
		return u"https://github.com/%1/%2/archive/%3.zip"_q.arg(
			match.captured(1),
			match.captured(2),
			match.captured(3));
	}
	return result;
}

void DownloadPackage(const QString &url, Fn<void(Package)> done) {
	const auto normalized = NormalizePackageUrl(url);
	const auto parsed = QUrl(normalized);
	if (parsed.isLocalFile()) {
		done(ReadPackageFromPath(parsed.toLocalFile()));
		return;
	} else if (QFileInfo(normalized).isAbsolute()
		&& QFileInfo::exists(normalized)) {
		done(ReadPackageFromPath(normalized));
		return;
	}
	const auto scheme = parsed.scheme().toLower();
	if (!parsed.isValid() || (scheme != u"https"_q && scheme != u"http"_q)) {
		done(Package{
			.source = url,
			.error = Damza::Text("Bad link: %1", "Неверная ссылка: %1").arg(url),
		});
		return;
	} else if (scheme == u"http"_q && !IsLoopbackHost(parsed.host())) {
		// Plugin code over plain http can be swapped on the way.
		done(Package{
			.source = url,
			.error = Damza::Text(
				"Only https:// links are allowed (http:// only for "
				"localhost): %1",
				"Разрешены только ссылки https:// (http:// — только для "
				"localhost): %1").arg(url),
		});
		return;
	}
	if (!Manager::Exists()) {
		done(Package{ .source = url, .error = Damza::Text("Plugins are off.", "Система плагинов выключена.") });
		return;
	}
	auto request = QNetworkRequest(parsed);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setHeader(
		QNetworkRequest::UserAgentHeader,
		u"DamZaGram plugin installer"_q);
	request.setTransferTimeout(kDownloadTimeout);
	const auto reply = Manager::Get().network()->get(request);
	const auto tooLarge = std::make_shared<bool>(false);
	QObject::connect(reply, &QNetworkReply::downloadProgress, [=](
			qint64 received,
			qint64 total) {
		if (received > kMaxPackageSize || total > kMaxPackageSize) {
			*tooLarge = true;
			reply->abort();
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (*tooLarge) {
			done(Package{
				.source = normalized,
				.error = Damza::Text("The plugin is too large.", "Плагин слишком большой."),
			});
			return;
		} else if (reply->error() != QNetworkReply::NoError) {
			done(Package{
				.source = normalized,
				.error = reply->errorString(),
			});
			return;
		}
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (status >= 400) {
			done(Package{
				.source = normalized,
				.error = u"HTTP %1"_q.arg(status),
			});
			return;
		}
		done(ReadPackageFromBytes(reply->readAll(), normalized));
	});
}

const PluginState *FindPlugin(const QString &id) {
	if (!Manager::Exists()) {
		return nullptr;
	}
	for (const auto &state : Manager::Get().plugins()) {
		if (state.manifest.id == id) {
			return &state;
		}
	}
	return nullptr;
}

bool IsBuiltinDirectory(const QString &dir) {
	auto file = QFile(QDir(dir).filePath(ManifestFileName()));
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	const auto root = QJsonDocument::fromJson(file.readAll()).object();
	// A foreign plugin copying "builtin": true must stay removable.
	const auto id = root.value(u"id"_q).toString(QDir(dir).dirName());
	return root.value(u"builtin"_q).toBool() && IsShippedBuiltin(id);
}

bool IsBuiltinPlugin(const QString &id) {
	const auto state = FindPlugin(id);
	return state && IsBuiltinDirectory(state->manifest.dir);
}

QString PluginDataFile(const QString &id) {
	return Manager::Get().dataPath() + id + u".json"_q;
}

InstallResult InstallPackage(const Package &package, bool enable) {
	if (!Manager::Exists()) {
		return { .error = Damza::Text("Plugins are off.", "Система плагинов выключена.") };
	} else if (!package.valid()) {
		return {
			.error = package.error.isEmpty()
				? Damza::Text("The plugin package is empty.", "Пакет плагина пуст.")
				: package.error,
		};
	}
	auto &manager = Manager::Get();
	const auto id = package.id();
	const auto existing = FindPlugin(id);
	const auto updated = (existing != nullptr);
	const auto wasEnabled = existing && existing->enabled;
	const auto target = existing
		? existing->manifest.dir
		: QDir(manager.pluginsPath()).filePath(id);
	if (existing && !InsidePluginsFolder(target)) {
		return { .error = Damza::Text("Bad plugin folder: %1", "Неверная папка плагина: %1").arg(target) };
	} else if (IsLink(target)) {
		return { .error = Damza::Text(
			"The plugin folder is a link, update the files it points "
			"to yourself: %1",
			"Папка плагина — ссылка, обнови файлы, на которые она "
			"указывает, сам: %1").arg(target) };
	} else if (!existing && QFileInfo::exists(target)) {
		// Another plugin (with a different id) or someone's files.
		return { .error = Damza::Text("The folder is taken: %1", "Папка уже занята: %1").arg(target) };
	}
	const auto staging = manager.dataPath() + u"_staging/"_q + id;
	QDir(staging).removeRecursively();
	for (const auto &[path, content] : package.files) {
		if (!WriteFile(QDir(staging).filePath(path), content)) {
			QDir(staging).removeRecursively();
			return { .error = Damza::Text("Can't write %1", "Не удалось записать %1").arg(path) };
		}
	}
	QDir(target).removeRecursively();
	if (!QDir().rename(staging, target)) {
		const auto copied = CopyRecursively(staging, target);
		QDir(staging).removeRecursively();
		if (!copied) {
			return { .error = Damza::Text("Can't copy files to %1", "Не удалось скопировать файлы в %1").arg(target) };
		}
	}
	LOG(("Plugins: installed %1 from %2").arg(id, package.source));
	if (updated) {
		manager.reload(id);
	} else {
		if (!enable) {
			// The state may remember the id as enabled (a folder deleted
			// by hand): "Enable right away" off must not run its code.
			manager.setEnabled(id, false);
		}
		manager.refresh();
	}
	if (enable && !wasEnabled) {
		manager.setEnabled(id, true);
	}
	return { .id = id, .updated = updated };
}

bool UninstallPlugin(const QString &id, bool removeData, QString *error) {
	const auto state = FindPlugin(id);
	if (!state) {
		*error = Damza::Text("Plugin not found: %1", "Плагин не найден: %1").arg(id);
		return false;
	}
	auto &manager = Manager::Get();
	const auto dir = state->manifest.dir;
	if (IsBuiltinDirectory(dir)) {
		manager.setEnabled(id, false);
		return true;
	} else if (!InsidePluginsFolder(dir)) {
		*error = Damza::Text("Bad plugin folder: %1", "Неверная папка плагина: %1").arg(dir);
		return false;
	}
	manager.setEnabled(id, false);
	const auto removed = IsLink(dir)
		? RemoveLink(dir)
		: QDir(dir).removeRecursively();
	if (removed && removeData && IsValidPluginId(id)) {
		QFile::remove(PluginDataFile(id));
	}
	manager.refresh();
	if (!removed) {
		*error = Damza::Text("Can't remove %1", "Не удалось удалить %1").arg(dir);
	}
	return removed;
}

bool WriteTypesInto(const QString &dir) {
	auto ok = true;
	for (const auto &file : EmbeddedTypeFiles()) {
		ok = WriteFile(QDir(dir).filePath(u"types/"_q + file.name), file.content)
			&& ok;
	}
	const auto jsconfig = QJsonObject{
		{ u"compilerOptions"_q, QJsonObject{
			{ u"checkJs"_q, true },
			{ u"target"_q, u"ES2023"_q },
			{ u"module"_q, u"ES2022"_q },
			{ u"lib"_q, QJsonArray{ u"ES2023"_q } },
		} },
		{ u"include"_q, QJsonArray{ u"**/*.js"_q, u"types/*.d.ts"_q } },
	};
	return WriteFile(
		QDir(dir).filePath(u"jsconfig.json"_q),
		QJsonDocument(jsconfig).toJson(QJsonDocument::Indented)) && ok;
}

InstallResult CreatePlugin(const ScaffoldOptions &options) {
	if (!Manager::Exists()) {
		return { .error = Damza::Text("Plugins are off.", "Система плагинов выключена.") };
	} else if (!IsValidPluginId(options.id)) {
		return { .error = Damza::Text("Bad plugin id.", "Плохой id плагина.") };
	}
	auto &manager = Manager::Get();
	const auto dir = QDir(manager.pluginsPath()).filePath(options.id);
	if (FindPlugin(options.id) || QFileInfo::exists(dir)) {
		return { .error = Damza::Text("Plugin \"%1\" already exists.", "Плагин \"%1\" уже есть.").arg(options.id) };
	}
	auto filled = options;
	if (filled.name.trimmed().isEmpty()) {
		filled.name = options.id;
	}
	const auto manifest = QJsonDocument(ScaffoldManifest(filled)).toJson(
		QJsonDocument::Indented);
	if (!WriteFile(QDir(dir).filePath(ManifestFileName()), manifest)
		|| !WriteFile(QDir(dir).filePath(u"main.js"_q), ScaffoldMain(filled))) {
		return { .error = Damza::Text("Can't write to %1", "Не удалось записать в %1").arg(dir) };
	}
	WriteTypesInto(dir);
	manager.refresh();
	manager.setEnabled(options.id, true);
	return { .id = options.id };
}

QString EnsureScratchPlugin(QString *error) {
	const auto id = QString::fromLatin1(kScratchPluginId);
	if (!Manager::Exists()) {
		*error = Damza::Text("Plugins are off.", "Система плагинов выключена.");
		return QString();
	}
	auto &manager = Manager::Get();
	if (!FindPlugin(id)) {
		const auto dir = QDir(manager.pluginsPath()).filePath(id);
		if (QFileInfo::exists(dir) || IsLink(dir)) {
			// Never overwrite main.js of a folder that is not the scratchpad.
			*error = Damza::Text("The folder is taken: %1", "Папка уже занята: %1").arg(dir);
			return QString();
		}
		const auto manifest = QJsonObject{
			{ u"id"_q, id },
			{ u"name"_q, u"DevTools scratchpad"_q },
			{ u"version"_q, u"1.0.0"_q },
			{ u"author"_q, u"@wetakush"_q },
			{ u"description"_q, u"Песочница консоли DevTools: код из "
				"консоли выполняется здесь, если не выбран другой плагин."_q },
			{ u"main"_q, u"main.js"_q },
			{ u"type"_q, u"script"_q },
		};
		const auto ok = WriteFile(
			QDir(dir).filePath(ManifestFileName()),
			QJsonDocument(manifest).toJson(QJsonDocument::Indented))
			&& WriteFile(
				QDir(dir).filePath(u"main.js"_q),
				"// DevTools scratchpad: globals defined in the console "
				"live here until reload.\n");
		if (!ok) {
			*error = Damza::Text("Can't write to %1", "Не удалось записать в %1").arg(dir);
			return QString();
		}
		WriteTypesInto(dir);
		manager.refresh();
	}
	if (!manager.instance(id)) {
		manager.setEnabled(id, true);
	}
	if (!manager.instance(id)) {
		const auto state = FindPlugin(id);
		*error = (state && !state->error.isEmpty())
			? state->error
			: Damza::Text("Can't start the scratchpad.", "Не удалось запустить песочницу.");
		return QString();
	}
	return id;
}

void RevealInFolder(const QString &path) {
	QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

} // namespace Damza::Plugins
