// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

namespace Damza::Plugins {
class Bridge;
} // namespace Damza::Plugins

// Module-wide state of the `ui` API extensions: what plugins registered
// (buttons, menu items, shortcuts, panels, render transforms) and the
// client widgets showing it. Main thread only.
namespace Damza::Plugins::UiExt {

// A JS function kept by the plugin's `ui` module under a numeric id.
struct Callback {
	QString pluginId;
	int id = 0;

	explicit operator bool() const {
		return (id != 0) && !pluginId.isEmpty();
	}
};

// Calls the function now and returns its result.
QVariant Call(const Callback &callback, const QVariantList &args = {});
// Calls it from the event loop: safe inside click handlers of widgets
// the callback may destroy.
void CallLater(const Callback &callback, QVariantList args = {});

struct ButtonSpec {
	Callback click;
	Callback when;
	QString icon;
	QString text;
	QString tooltip;
};

struct MenuItemSpec {
	Callback click;
	QString title;
	QString icon;
};

// Each returns a registration id (> 0) or 0 on failure.
[[nodiscard]] int AddTopBarButton(ButtonSpec spec);
[[nodiscard]] int AddComposeButton(ButtonSpec spec);
[[nodiscard]] int AddMainMenuItem(MenuItemSpec spec);
[[nodiscard]] int AddShortcut(Callback callback, const QString &keys);
bool Remove(const QString &pluginId, int id);
// Re-evaluates `when` of top bar buttons.
void RefreshButtons();

void Notify(const QString &pluginId, const QVariantMap &spec);
void OpenLink(const QString &url);

[[nodiscard]] QVariant ComposeGet();
bool ComposeSet(const QString &text);
bool ComposeInsert(const QString &text);
void ComposeFocus();

[[nodiscard]] QVariant PanelOpen(
	not_null<Bridge*> bridge,
	const QVariantMap &spec);
[[nodiscard]] QVariant PanelAdd(
	not_null<Bridge*> bridge,
	int panelId,
	const QString &type,
	const QVariantMap &props);
bool PanelSet(not_null<Bridge*> bridge, int panelId, const QVariantMap &props);
bool PanelClear(not_null<Bridge*> bridge, int panelId);
bool PanelClose(not_null<Bridge*> bridge, int panelId);
[[nodiscard]] bool PanelIsOpen(not_null<Bridge*> bridge, int panelId);

void SetRenderTransforms(const QString &pluginId, bool has);
void RefreshRender();

void AppStarted();
void PluginUnloaded(const QString &pluginId);

} // namespace Damza::Plugins::UiExt
