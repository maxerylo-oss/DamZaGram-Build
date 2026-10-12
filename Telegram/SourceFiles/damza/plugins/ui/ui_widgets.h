// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

#include "base/object_ptr.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"

namespace style {
struct IconButton;
} // namespace style

namespace Ui {
class FlatLabel;
class IconButton;
class ScrollArea;
class SettingsSlider;
class VerticalLayout;
} // namespace Ui

namespace Damza::Plugins::UiExt {

// Icons plugins refer to by name: dzg.ui.icons().
[[nodiscard]] const style::icon *LookupIcon(const QString &name);
[[nodiscard]] QStringList IconNames();

// An icon or a short text button sized like the `st` icon button,
// painted with the client's icon colors.
class PluginButton final : public Ui::RippleButton {
public:
	PluginButton(
		QWidget *parent,
		const style::IconButton &st,
		const style::icon *icon,
		const QString &text);

protected:
	void paintEvent(QPaintEvent *e) override;
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	[[nodiscard]] QRect rippleRect() const;

	const style::IconButton &_st;
	const style::icon *_icon = nullptr;
	QString _text;

};

// A row of plugin buttons (compose area).
class ButtonsRow final : public Ui::RpWidget {
public:
	using RpWidget::RpWidget;

	void setButtons(std::vector<not_null<Ui::RpWidget*>> buttons);
	[[nodiscard]] int contentWidth() const;

private:
	std::vector<QPointer<Ui::RpWidget>> _buttons;

};

// One plugin panel: a title bar with a close button and scrollable
// settings-like content.
class SidePanel final : public Ui::RpWidget {
public:
	SidePanel(
		QWidget *parent,
		const QString &pluginId,
		const QString &title,
		int width);

	[[nodiscard]] const QString &pluginId() const;
	[[nodiscard]] not_null<Ui::VerticalLayout*> content() const;
	[[nodiscard]] const QString &title() const;
	void setTitle(const QString &title);
	[[nodiscard]] int desiredWidth() const;
	void setDesiredWidth(int width);
	void scrollToBottom();

	[[nodiscard]] rpl::producer<> closeRequests() const;
	[[nodiscard]] rpl::producer<> changes() const;

	// type: label, button, input, toggle, separator, skip.
	// Returns the widget to hand to the plugin, if any.
	QWidget *addItem(
		const QString &type,
		const QVariantMap &props,
		Fn<void(QString event, QVariant value)> fire);

private:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	const QString _pluginId;
	QString _title;
	int _width = 0;
	object_ptr<Ui::FlatLabel> _titleLabel;
	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<Ui::VerticalLayout> _content;
	rpl::event_stream<> _closeRequests;
	rpl::event_stream<> _changes;

};

// The column docked to the right of a main window. Several plugin
// panels become tabs; the newest one is shown.
class SidePanelHost final : public Ui::RpWidget {
public:
	explicit SidePanelHost(QWidget *parent);
	~SidePanelHost();

	void add(not_null<SidePanel*> panel);
	void activate(not_null<SidePanel*> panel);
	[[nodiscard]] bool empty() const;
	[[nodiscard]] int desiredWidth() const;
	[[nodiscard]] rpl::producer<> layoutRequests() const;

private:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void remove(QWidget *panel);
	void applyTabTitles(int width);
	void refreshTabs();
	void updateInnerGeometry();

	std::vector<QPointer<SidePanel>> _panels;
	QPointer<SidePanel> _active;
	object_ptr<Ui::SettingsSlider> _tabs = { nullptr };
	std::vector<QString> _tabTitles;
	std::vector<QString> _tabTitlesShown;
	int _tabActive = 0;
	rpl::event_stream<> _layoutRequests;

};

// A toast on top of the active window; a click on it (or on its button)
// calls `callback`.
void ShowActionToast(
	const QString &title,
	const QString &text,
	const QString &buttonText,
	Fn<void()> callback,
	Fn<void()> destroyed);

} // namespace Damza::Plugins::UiExt
