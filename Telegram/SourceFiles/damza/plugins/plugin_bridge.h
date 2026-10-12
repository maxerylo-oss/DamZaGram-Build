// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

#include "damza/plugins/plugin_js.h"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QVariant>

class QTimer;
class QNetworkReply;
class QWidget;

namespace Damza::Plugins {

class Instance;

// Native half of the plugin API. The JS prelude wraps it into `dzg`.
// Method names here are internal; plugins never see this object directly.
class Bridge final : public QObject {
public:
	explicit Bridge(not_null<Instance*> instance);
	~Bridge();

	// The client loaded another palette (theme or night mode switch):
	// take it as the new base and put plugin overrides back on top.
	static void ForgetThemeOverrides();

	[[nodiscard]] std::vector<Js::NativeMethod> methods();

	// Core.
	void log(const QString &level, const QString &text);
	void setHooks(bool filter, bool beforeSend);
	[[nodiscard]] QVariantMap pluginInfo() const;
	[[nodiscard]] QVariantMap appInfo() const;
	[[nodiscard]] QVariant readPluginFile(const QString &relativePath) const;

	// Timers.
	[[nodiscard]] int startTimer(int ms, bool repeat);
	void stopTimer(int id);

	// Storage and settings.
	[[nodiscard]] QVariant storageGet(const QString &key) const;
	void storageSet(const QString &key, const QVariant &value);
	void storageRemove(const QString &key);
	[[nodiscard]] QStringList storageKeys() const;
	[[nodiscard]] QVariantMap settings() const;
	void setSetting(const QString &key, const QVariant &value);

	// UI.
	void toast(const QString &text, const QString &title);
	[[nodiscard]] int showBox(const QVariantMap &spec);
	[[nodiscard]] QVariant mainWindow();
	[[nodiscard]] QVariant focusedWidget();
	[[nodiscard]] QVariant findWidgets(
		const QString &className,
		const QString &objectName);
	[[nodiscard]] QVariant createWidget(
		int parentId,
		const QString &type,
		const QVariantMap &props);
	[[nodiscard]] QVariant widgetCall(
		int id,
		const QString &method,
		const QVariantList &args);
	void repaintAll();
	[[nodiscard]] bool setThemeColor(const QString &name, const QString &color);
	[[nodiscard]] QVariant themeColor(const QString &name) const;
	void resetTheme();

	// Chats and messages.
	[[nodiscard]] QVariant currentChat() const;
	[[nodiscard]] QVariant getPeer(const QString &peerId) const;
	[[nodiscard]] QVariant findPeer(const QString &username) const;
	[[nodiscard]] bool openChat(const QString &peerId, double msgId);
	[[nodiscard]] QVariant getMessage(const QString &peerId, double msgId) const;
	[[nodiscard]] QVariantList loadedMessages(
		const QString &peerId,
		int limit) const;
	[[nodiscard]] bool sendMessage(
		const QString &peerId,
		const QString &text,
		const QVariantMap &options);
	[[nodiscard]] bool editMessage(
		const QString &peerId,
		double msgId,
		const QString &text);
	[[nodiscard]] bool deleteMessages(
		const QString &peerId,
		const QVariantList &ids,
		bool revoke);
	void refilter();

	// System.
	[[nodiscard]] int httpRequest(const QVariantMap &request);
	void openUrl(const QString &url);
	[[nodiscard]] QString clipboardText() const;
	void setClipboardText(const QString &text);
	[[nodiscard]] QVariant readFile(const QString &path) const;
	[[nodiscard]] bool writeFile(const QString &path, const QString &text);
	[[nodiscard]] QStringList listDir(const QString &path) const;
	[[nodiscard]] bool fileExists(const QString &path) const;

	// For API modules.
	[[nodiscard]] not_null<Instance*> instance() const;
	void widgetEvent(
		int widgetId,
		const QString &event,
		const QVariant &value = QVariant());
	// Resolves a promise created in JS with internal.request().
	void settle(int id, bool ok, const QVariant &value);
	// Ids for internal.request(): one sequence for every async call.
	[[nodiscard]] int nextRequestId();
	// Widgets travel to JS as numeric ids, stable per widget.
	[[nodiscard]] int registerWidget(QWidget *widget);
	[[nodiscard]] QWidget *widgetById(int id) const;
	[[nodiscard]] QVariant widgetIds(const QList<QWidget*> &list);
	// Relative paths resolve against the plugin folder.
	[[nodiscard]] QString resolvePath(const QString &path) const;
	// Track a widget created for the plugin: deleted when it unloads.
	void adoptWidget(QWidget *widget);

private:

	const not_null<Instance*> _instance;
	std::map<int, QTimer*> _timers;
	std::map<int, QPointer<QNetworkReply>> _requests;
	std::map<int, QPointer<QWidget>> _widgetsById;
	std::map<QWidget*, int> _widgetIds;
	std::vector<QPointer<QWidget>> _created;
	// Palette colors this plugin overrides, reapplied in plugin order over
	// the client's palette. See ApplyThemeLayers in plugin_bridge.cpp.
	base::flat_map<QByteArray, QColor> _themeOverrides;
	int _nextTimerId = 0;
	int _nextBoxId = 0;
	int _nextRequestId = 0;
	int _nextWidgetId = 0;

};

} // namespace Damza::Plugins
