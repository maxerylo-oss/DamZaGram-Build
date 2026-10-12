// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Damza {

// "Welcome to DamZaGram" / "What's new" box.
void ShowWelcomeBox(not_null<Window::SessionController*> controller);

// Shows the box once per client version, a bit after the window appears.
void ShowWelcomeIfNeeded(not_null<Window::SessionController*> controller);

} // namespace Damza
