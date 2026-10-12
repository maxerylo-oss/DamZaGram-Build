// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

namespace style {
struct FlatLabel;
struct RoundButton;
} // namespace style

namespace Ui {
class RpWidget;
class Show;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Damza::Plugins {
struct Package;
} // namespace Damza::Plugins

namespace Damza::PluginsUi {

struct FlowButton {
	QString text;
	Fn<void()> callback;
	bool primary = false;
	bool attention = false;
};

// A row of small rounded buttons that wraps to the next line.
not_null<Ui::RpWidget*> AddButtonsFlow(
	not_null<Ui::VerticalLayout*> container,
	std::vector<FlowButton> buttons,
	const style::margins &padding);

[[nodiscard]] not_null<const style::FlatLabel*> MakeLabelStyle(
	rpl::lifetime &lifetime,
	const style::FlatLabel &base,
	const style::color &fg);

void ShowSettingsBox(std::shared_ptr<Ui::Show> show, const QString &id);
void ShowLogBox(std::shared_ptr<Ui::Show> show, const QString &id);
void ShowUninstallBox(std::shared_ptr<Ui::Show> show, const QString &id);

// Confirmation with the manifest and a full-access warning; installs and
// enables the plugin on confirm.
void ShowInstallBox(
	std::shared_ptr<Ui::Show> show,
	Plugins::Package package);
void ShowInstallFromUrlBox(std::shared_ptr<Ui::Show> show);
void ChooseFileAndInstall(std::shared_ptr<Ui::Show> show, QWidget *parent);
void ChooseFolderAndInstall(std::shared_ptr<Ui::Show> show, QWidget *parent);
void InstallFromPath(std::shared_ptr<Ui::Show> show, const QString &path);

void ShowCreateBox(std::shared_ptr<Ui::Show> show, const QString &author);

// *.dzg.zip / *.dzg.js / *.plugin.js opened from a chat.
[[nodiscard]] bool OpenPluginFile(
	Window::SessionController *controller,
	const QString &fileName,
	const QString &path);

void ShowPluginsSection(not_null<Window::SessionController*> controller);
void AddMainMenuEntry(
	not_null<Ui::VerticalLayout*> menu,
	not_null<Window::SessionController*> controller);

} // namespace Damza::PluginsUi
