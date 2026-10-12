// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

#include <QtCore/QJsonObject>

namespace Damza::Plugins {

struct PluginState;

// A plugin ready to be written into the plugins folder: from a folder,
// a .zip archive, a single .js file with a header comment or a URL.
struct Package {
	QJsonObject manifest;
	std::map<QString, QByteArray> files;
	QString source;
	QString error;
	bool generatedManifest = false;

	[[nodiscard]] QString id() const;
	[[nodiscard]] QString name() const;
	[[nodiscard]] QString version() const;
	[[nodiscard]] QString author() const;
	[[nodiscard]] QString description() const;
	[[nodiscard]] qint64 totalSize() const;
	[[nodiscard]] bool valid() const;
};

struct InstallResult {
	QString id;
	QString error;
	bool updated = false;
};

struct ScaffoldOptions {
	QString id;
	QString name;
	QString description;
	QString author;
};

[[nodiscard]] bool IsValidPluginId(const QString &id);
[[nodiscard]] QString SanitizePluginId(const QString &text);

// *.dzg.zip, *.dzg.js and *.plugin.js are DamZaGram plugin files.
[[nodiscard]] bool IsPluginFileName(const QString &name);

// Reads `// @key value` lines from the leading comments of a script.
[[nodiscard]] QJsonObject ParseScriptHeader(const QString &code);

[[nodiscard]] Package ReadPackageFromDirectory(const QString &path);
[[nodiscard]] Package ReadPackageFromZip(
	const QByteArray &bytes,
	const QString &source);
[[nodiscard]] Package ReadPackageFromScript(
	const QByteArray &code,
	const QString &source);
// Chooses the format by content (zip magic) and by name.
[[nodiscard]] Package ReadPackageFromBytes(
	const QByteArray &bytes,
	const QString &source);
// A folder, a .zip or a .js file.
[[nodiscard]] Package ReadPackageFromPath(const QString &path);

// GitHub page links are turned into raw / archive links.
[[nodiscard]] QString NormalizePackageUrl(const QString &url);
void DownloadPackage(const QString &url, Fn<void(Package)> done);

// Writes the package over the plugin with the same id, rescans and
// (optionally) enables it.
[[nodiscard]] InstallResult InstallPackage(
	const Package &package,
	bool enable = true);

[[nodiscard]] const PluginState *FindPlugin(const QString &id);
[[nodiscard]] bool IsBuiltinPlugin(const QString &id);
[[nodiscard]] bool IsBuiltinDirectory(const QString &dir);
[[nodiscard]] QString PluginDataFile(const QString &id);

// Builtin plugins are only disabled: the client restores them on start.
[[nodiscard]] bool UninstallPlugin(
	const QString &id,
	bool removeData,
	QString *error);

// Scaffolds tdata/plugins/<id>/ with manifest.json, main.js, jsconfig.json
// and the API types, then enables the plugin.
[[nodiscard]] InstallResult CreatePlugin(const ScaffoldOptions &options);
// Copies the bundled .d.ts files and writes jsconfig.json into a folder.
bool WriteTypesInto(const QString &dir);

// The "devtools" plugin: a JS scratchpad for the DevTools console.
inline constexpr auto kScratchPluginId = "devtools";
[[nodiscard]] QString EnsureScratchPlugin(QString *error);

void RevealInFolder(const QString &path);

} // namespace Damza::Plugins
