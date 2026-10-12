// Tests for damza/plugins/plugin_install.cpp, see install_test.sh.
#include "damza/plugins/plugin_install.h"
#include "damza/plugins/plugin_manager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QTemporaryDir>
#include <cstdio>

using namespace Damza::Plugins;

namespace {

int Failures = 0;

void Check(bool condition, const char *what, const QString &details = QString()) {
	if (!condition) {
		++Failures;
		std::printf("FAIL: %s %s\n", what, details.toUtf8().constData());
	} else {
		std::printf("ok: %s\n", what);
	}
}

QByteArray Read(const QString &path) {
	auto file = QFile(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	const auto fixtures = QString::fromLocal8Bit(argv[1]);
	QTemporaryDir temp;
	auto &manager = Manager::Get();
	manager.root = temp.path();
	QDir().mkpath(manager.pluginsPath());
	QDir().mkpath(manager.dataPath());

	const auto header = ParseScriptHeader(QString::fromUtf8(R"(#!/usr/bin/env qjs
// ==DamZaGram Plugin==
// @id          shout
// @name        Shout it
// @version     1.2.0
// @author      @wetakush
// @description First.
// @description Second.
// @setting     {"key":"suffix","type":"string","default":"!!!"}
// @settings    [{"key":"a","type":"bool"},{"key":"b","type":"number"}]
// @homepage    https://t.me/wetakush
// ==/DamZaGram Plugin==
/**
 * @license MIT
 */
const x = 1; // @name not-a-header
)"));
	Check(header.value(u"id"_q).toString() == u"shout"_q, "header id");
	Check(header.value(u"name"_q).toString() == u"Shout it"_q, "header name");
	Check(header.value(u"description"_q).toString() == u"First. Second."_q, "header description joined", header.value(u"description"_q).toString());
	Check(header.value(u"settings"_q).toArray().size() == 3, "header settings");
	Check(header.value(u"homepage"_q).toString() == u"https://t.me/wetakush"_q, "header extra key");
	Check(header.value(u"license"_q).toString() == u"MIT"_q, "header block comment");
	Check(header.value(u"name"_q).toString() != u"not-a-header"_q, "header stops at code");

	const auto script = ReadPackageFromScript("// @name My Cool Plugin\ndzg.log(1);\n", u"https://x.y/whatever.plugin.js"_q);
	Check(script.valid(), "script package valid", script.error);
	Check(script.id() == u"my-cool-plugin"_q, "script id from name", script.id());
	Check(script.files.contains(u"manifest.json"_q) && script.files.contains(u"main.js"_q), "script files");
	Check(script.generatedManifest, "script generated manifest");

	const auto unnamed = ReadPackageFromScript("dzg.log(1);\n", u"Some Thing.dzg.js"_q);
	Check(unnamed.id() == u"some-thing"_q, "script id from file name", unnamed.id());

	const auto nested = ReadPackageFromPath(fixtures + u"/nested.zip"_q);
	Check(nested.valid(), "nested zip valid", nested.error);
	Check(nested.id() == u"zipped"_q, "nested zip id", nested.id());
	Check(nested.files.contains(u"lib/util.js"_q), "nested zip keeps subfolders");
	Check(!nested.files.contains(u".git/config"_q), "nested zip skips .git");
	Check(!nested.files.contains(u"outside.txt"_q), "nested zip drops files outside plugin folder");

	const auto single = ReadPackageFromPath(fixtures + u"/single.dzg.zip"_q);
	Check(single.valid() && single.id() == u"lonely"_q, "zip with one .js", single.error + ' ' + single.id());

	const auto evil = ReadPackageFromPath(fixtures + u"/evil.zip"_q);
	Check(!evil.files.contains(u"../escape.js"_q), "zip path traversal ignored");

	const auto broken = ReadPackageFromBytes("<!DOCTYPE html><html>", u"page"_q);
	Check(!broken.valid() && !broken.error.isEmpty(), "html page rejected");

	const auto notZip = ReadPackageFromBytes("PK\x03\x04garbage", u"x.zip"_q);
	Check(!notZip.valid(), "garbage zip rejected", notZip.error);

	const auto folder = ReadPackageFromPath(fixtures + u"/folder"_q);
	Check(folder.valid() && folder.id() == u"folder-plugin"_q, "folder package", folder.error);

	Check(NormalizePackageUrl(u"https://github.com/a/b/blob/main/x/y.plugin.js"_q) == u"https://raw.githubusercontent.com/a/b/main/x/y.plugin.js"_q, "github blob url");
	Check(NormalizePackageUrl(u"https://github.com/a/b"_q) == u"https://github.com/a/b/archive/HEAD.zip"_q, "github repo url");
	Check(NormalizePackageUrl(u"github.com/a/b.git"_q) == u"https://github.com/a/b/archive/HEAD.zip"_q, "github .git url", NormalizePackageUrl(u"github.com/a/b.git"_q));
	Check(NormalizePackageUrl(u"https://github.com/a/b/tree/dev"_q) == u"https://github.com/a/b/archive/dev.zip"_q, "github tree url");
	Check(NormalizePackageUrl(u"https://example.com/x.zip"_q) == u"https://example.com/x.zip"_q, "plain url");

	Check(IsPluginFileName(u"a.dzg.zip"_q) && IsPluginFileName(u"A.PLUGIN.JS"_q) && !IsPluginFileName(u"a.plugin"_q) && !IsPluginFileName(u"a.zip"_q), "plugin file names");
	Check(IsValidPluginId(u"a.b-c_1"_q) && !IsValidPluginId(u".."_q) && !IsValidPluginId(u"a/b"_q) && !IsValidPluginId(u"-a"_q), "plugin ids");

	auto result = InstallPackage(nested, true);
	Check(result.error.isEmpty() && result.id == u"zipped"_q && !result.updated, "install new", result.error);
	Check(QFile::exists(manager.pluginsPath() + u"zipped/lib/util.js"_q), "installed files");
	Check(manager.enabled[u"zipped"_q], "installed plugin enabled");
	Check(!QDir(manager.dataPath() + u"_staging/zipped"_q).exists(), "staging cleaned");

	auto file = QFile(manager.pluginsPath() + u"zipped/stale.js"_q);
	file.open(QIODevice::WriteOnly);
	file.write("x");
	file.close();
	result = InstallPackage(nested, true);
	Check(result.error.isEmpty() && result.updated, "update", result.error);
	Check(!QFile::exists(manager.pluginsPath() + u"zipped/stale.js"_q), "update replaces folder");

	result = InstallPackage(script, false);
	Check(result.error.isEmpty() && !manager.enabled[u"my-cool-plugin"_q], "install disabled", result.error);

	const auto created = CreatePlugin({ .id = u"made"_q, .name = u"Made"_q, .description = u"d"_q, .author = u"@wetakush"_q });
	Check(created.error.isEmpty(), "create plugin", created.error);
	Check(Read(manager.pluginsPath() + u"made/main.js"_q).contains("commands.register('made'"), "scaffold main.js");
	Check(QFile::exists(manager.pluginsPath() + u"made/types/core.d.ts"_q), "scaffold types");
	Check(QFile::exists(manager.pluginsPath() + u"made/jsconfig.json"_q), "scaffold jsconfig");
	const auto again = CreatePlugin({ .id = u"made"_q });
	Check(!again.error.isEmpty(), "create duplicate fails");

	auto error = QString();
	QFile(manager.dataPath() + u"zipped.json"_q).open(QIODevice::WriteOnly);
	Check(UninstallPlugin(u"zipped"_q, true, &error), "uninstall", error);
	Check(!QDir(manager.pluginsPath() + u"zipped"_q).exists(), "uninstall removes folder");
	Check(!QFile::exists(manager.dataPath() + u"zipped.json"_q), "uninstall removes data");
	Check(!UninstallPlugin(u"nope"_q, false, &error), "uninstall missing fails");

	QDir().mkpath(manager.pluginsPath() + u"core"_q);
	auto builtin = QFile(manager.pluginsPath() + u"core/manifest.json"_q);
	builtin.open(QIODevice::WriteOnly);
	builtin.write(R"({"id":"core","builtin":true})");
	builtin.close();
	manager.rescan();
	manager.setEnabled(u"core"_q, true);
	Check(IsBuiltinPlugin(u"core"_q), "builtin detected");
	Check(UninstallPlugin(u"core"_q, false, &error) && !manager.enabled[u"core"_q], "builtin only disabled");
	Check(QDir(manager.pluginsPath() + u"core"_q).exists(), "builtin folder kept");

	std::printf(Failures ? "FAILED: %d\n" : "ALL OK\n", Failures);
	return Failures ? 1 : 0;
}
