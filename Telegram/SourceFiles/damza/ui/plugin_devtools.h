// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

namespace Damza::PluginsUi {

// A separate window with a JS console for a loaded plugin (or the
// "devtools" scratchpad plugin) and a live tail of its log.
void ShowDevTools(const QString &pluginId = QString());

// Wraps console input so that top-level `await` works: the value then
// arrives in the log as "[devtools] => ...".
[[nodiscard]] QString WrapConsoleCode(const QString &code);

} // namespace Damza::PluginsUi
