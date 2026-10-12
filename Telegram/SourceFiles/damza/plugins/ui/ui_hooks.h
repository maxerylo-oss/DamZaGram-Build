// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

// Hooks the client calls so plugins can extend its UI (dzg.ui.*).
// Every function is a cheap no-op while no plugin uses the feature.

class HistoryItem;
class PeerData;
struct TextWithEntities;

namespace Ui {
class InputField;
class VerticalLayout;
} // namespace Ui

namespace Window {
class MainWindow;
class SessionController;
} // namespace Window

namespace HistoryView {
class TopBarWidget;
} // namespace HistoryView

namespace Damza::Plugins {

// HistoryView::TopBarWidget: refresh plugin buttons for the chat
// (nullptr hides them) and lay them out from the right edge.
// Place returns the width taken.
void UpdateTopBarButtons(
	not_null<HistoryView::TopBarWidget*> bar,
	PeerData *peer);
[[nodiscard]] int PlaceTopBarButtons(
	not_null<HistoryView::TopBarWidget*> bar,
	int right,
	int top);

// Main menu: appends plugin items, kept in sync while the menu is open.
void FillMainMenu(
	not_null<Ui::VerticalLayout*> menu,
	not_null<Window::SessionController*> controller);

// Message fields (HistoryWidget, ComposeControls). `owner` hosts the
// plugin compose buttons, `relayout` must lay the controls out again.
void RegisterComposeField(
	not_null<QWidget*> owner,
	not_null<Ui::InputField*> field,
	Fn<PeerData*()> peer,
	Fn<void()> relayout);
[[nodiscard]] int ComposeButtonsWidth(not_null<QWidget*> owner, bool shown);
[[nodiscard]] int PlaceComposeButtons(
	not_null<QWidget*> owner,
	int right,
	int top,
	bool shown);

// Window::MainWindow::updateControlsGeometry: docks plugin panels to the
// right side of `area` and returns the width they took from it.
[[nodiscard]] int PlaceSidePanels(
	not_null<Window::MainWindow*> window,
	QRect area);

// HistoryView::Element::validateText: text to display for the item.
[[nodiscard]] TextWithEntities DisplayText(
	not_null<HistoryItem*> item,
	TextWithEntities &&text);

} // namespace Damza::Plugins
