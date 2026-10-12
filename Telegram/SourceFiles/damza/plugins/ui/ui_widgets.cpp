// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/plugins/ui/ui_widgets.h"

#include "damza/damza_lang.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"

#include <QtGui/QCursor>

namespace Damza::Plugins::UiExt {
namespace {

constexpr auto kTextButtonPadding = 12;
constexpr auto kMinPanelWidth = 240;
constexpr auto kMaxPanelWidth = 720;

struct IconEntry {
	const char *name = nullptr;
	const style::icon *icon = nullptr;
};

[[nodiscard]] const std::vector<IconEntry> &Icons() {
	static const auto result = std::vector<IconEntry>{
		{ "search", &st::menuIconSearch },
		{ "settings", &st::menuIconSettings },
		{ "customize", &st::menuIconCustomize },
		{ "edit", &st::menuIconEdit },
		{ "delete", &st::menuIconDelete },
		{ "clear", &st::menuIconClear },
		{ "copy", &st::menuIconCopy },
		{ "link", &st::menuIconLink },
		{ "links", &st::menuIconLinks },
		{ "pin", &st::menuIconPin },
		{ "unpin", &st::menuIconUnpin },
		{ "mute", &st::menuIconMute },
		{ "unmute", &st::menuIconUnmute },
		{ "sound", &st::menuIconSoundOn },
		{ "soundOff", &st::menuIconSoundOff },
		{ "silent", &st::menuIconSilent },
		{ "archive", &st::menuIconArchive },
		{ "unarchive", &st::menuIconUnarchive },
		{ "info", &st::menuIconInfo },
		{ "help", &st::menuIconFaq },
		{ "stats", &st::menuIconStats },
		{ "bot", &st::menuIconBot },
		{ "code", &st::menuIconBotCommands },
		{ "experimental", &st::menuIconExperimental },
		{ "star", &st::menuIconStar },
		{ "fave", &st::menuIconFave },
		{ "like", &st::menuIconLike },
		{ "premium", &st::menuIconPremium },
		{ "gift", &st::menuIconGiftPremium },
		{ "bell", &st::menuIconNotifications },
		{ "lock", &st::menuIconLock },
		{ "user", &st::menuIconProfile },
		{ "users", &st::menuIconGroups },
		{ "addUser", &st::menuIconInvite },
		{ "admin", &st::menuIconAdmin },
		{ "username", &st::menuIconUsername },
		{ "channel", &st::menuIconChannel },
		{ "chat", &st::menuIconChatBubble },
		{ "chats", &st::menuIconChats },
		{ "topics", &st::menuIconTopics },
		{ "discussion", &st::menuIconDiscussion },
		{ "saved", &st::menuIconSavedMessages },
		{ "folder", &st::menuIconShowInFolder },
		{ "addToFolder", &st::menuIconAddToFolder },
		{ "photo", &st::menuIconPhoto },
		{ "video", &st::menuIconVideoChat },
		{ "gif", &st::menuIconGif },
		{ "file", &st::menuIconFile },
		{ "download", &st::menuIconDownload },
		{ "upload", &st::menuIconExport },
		{ "reply", &st::menuIconReply },
		{ "forward", &st::menuIconForward },
		{ "send", &st::menuIconSend },
		{ "share", &st::menuIconShare },
		{ "translate", &st::menuIconTranslate },
		{ "palette", &st::menuIconPalette },
		{ "night", &st::menuIconNightMode },
		{ "font", &st::menuIconFont },
		{ "draw", &st::menuIconDraw },
		{ "spoiler", &st::menuIconSpoiler },
		{ "plus", &st::menuIconAdd },
		{ "close", &st::menuIconCancel },
		{ "check", &st::menuIconSelect },
		{ "markRead", &st::menuIconMarkRead },
		{ "markUnread", &st::menuIconMarkUnread },
		{ "timer", &st::menuIconTimer },
		{ "schedule", &st::menuIconSchedule },
		{ "hourglass", &st::menuIconHourglass },
		{ "location", &st::menuIconAddress },
		{ "poll", &st::menuIconCreatePoll },
		{ "todo", &st::menuIconCreateTodoList },
		{ "sticker", &st::menuIconStickers },
		{ "emoji", &st::menuIconEmoji },
		{ "reactions", &st::menuIconReactions },
		{ "phone", &st::menuIconPhone },
		{ "qr", &st::menuIconQrCode },
		{ "block", &st::menuIconBlock },
		{ "report", &st::menuIconReport },
		{ "restore", &st::menuIconRestore },
		{ "leave", &st::menuIconLeave },
		{ "network", &st::menuIconNetwork },
		{ "devices", &st::menuIconDevices },
		{ "permissions", &st::menuIconPermissions },
		{ "manage", &st::menuIconManage },
		{ "shortcut", &st::menuIconShortcut },
		{ "newWindow", &st::menuIconNewWindow },
		{ "expand", &st::menuIconExpand },
		{ "collapse", &st::menuIconCollapse },
		{ "zoomIn", &st::menuIconZoomIn },
		{ "zoomOut", &st::menuIconZoomOut },
		{ "reorder", &st::menuIconReorder },
		{ "storage", &st::menuIconStorage },
		{ "power", &st::menuIconPowerUsage },
		{ "tag", &st::menuIconTagEdit },
		{ "shop", &st::menuIconShop },
		{ "ghost", &st::ayuGhostIcon },
		{ "streamer", &st::ayuStreamerModeMenuIcon },
	};
	return result;
}

[[nodiscard]] int ClampPanelWidth(int width) {
	return std::clamp(width, kMinPanelWidth, kMaxPanelWidth);
}

} // namespace

const style::icon *LookupIcon(const QString &name) {
	if (name.isEmpty()) {
		return nullptr;
	}
	const auto latin = name.toLatin1();
	for (const auto &entry : Icons()) {
		if (!qstrcmp(entry.name, latin.constData())) {
			return entry.icon;
		}
	}
	return nullptr;
}

QStringList IconNames() {
	auto result = QStringList();
	for (const auto &entry : Icons()) {
		result.push_back(QString::fromLatin1(entry.name));
	}
	return result;
}

PluginButton::PluginButton(
	QWidget *parent,
	const style::IconButton &st,
	const style::icon *icon,
	const QString &text)
: RippleButton(parent, st.ripple)
, _st(st)
, _icon(icon)
, _text(text) {
	const auto textWidth = (!_icon && !_text.isEmpty())
		? st::semiboldFont->width(_text) + 2 * kTextButtonPadding
		: 0;
	resize(std::max(_st.width, textWidth), _st.height);
	setPointerCursor(true);
}

QRect PluginButton::rippleRect() const {
	const auto size = _st.rippleAreaSize;
	const auto top = _st.rippleAreaPosition.y();
	if (_icon || width() <= _st.width) {
		return QRect((width() - size) / 2, top, size, size);
	}
	return QRect(0, top, width(), size);
}

void PluginButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto rect = rippleRect();
	paintRipple(p, rect.topLeft());
	const auto over = isOver() || isDown();
	if (_icon) {
		_icon->paintInCenter(
			p,
			rect,
			(over ? st::menuIconFgOver : st::menuIconFg)->c);
	} else {
		p.setFont(st::semiboldFont);
		p.setPen(over ? st::windowActiveTextFg : st::windowSubTextFg);
		p.drawText(rect, _text, style::al_center);
	}
}

QImage PluginButton::prepareRippleMask() const {
	const auto rect = rippleRect();
	return (rect.width() == rect.height())
		? Ui::RippleAnimation::EllipseMask(rect.size())
		: Ui::RippleAnimation::RoundRectMask(rect.size(), rect.height() / 2);
}

QPoint PluginButton::prepareRippleStartPosition() const {
	const auto rect = rippleRect();
	const auto result = mapFromGlobal(QCursor::pos()) - rect.topLeft();
	return QRect(QPoint(), rect.size()).contains(result)
		? result
		: DisabledRippleStartPosition();
}

void ButtonsRow::setButtons(std::vector<not_null<Ui::RpWidget*>> buttons) {
	for (const auto &button : base::take(_buttons)) {
		delete button.data();
	}
	auto left = 0;
	auto height = 0;
	for (const auto button : buttons) {
		button->setParent(this);
		button->moveToLeft(left, 0);
		button->show();
		left += button->width();
		height = std::max(height, button->height());
		_buttons.push_back(button.get());
	}
	resize(left, height);
}

int ButtonsRow::contentWidth() const {
	return _buttons.empty() ? 0 : width();
}

SidePanel::SidePanel(
	QWidget *parent,
	const QString &pluginId,
	const QString &title,
	int width)
: RpWidget(parent)
, _pluginId(pluginId)
, _title(title)
, _width(ClampPanelWidth(width))
, _titleLabel(this, title, st::boxTitle)
, _close(this, st::boxTitleClose)
, _scroll(this, st::boxScroll) {
	_content = _scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(_scroll.data()));
	_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	_close->setClickedCallback([=] { _closeRequests.fire({}); });
	_content->heightValue() | rpl::on_next([=] {
		_content->update();
	}, _content->lifetime());
}

const QString &SidePanel::pluginId() const {
	return _pluginId;
}

not_null<Ui::VerticalLayout*> SidePanel::content() const {
	return _content.data();
}

const QString &SidePanel::title() const {
	return _title;
}

void SidePanel::setTitle(const QString &title) {
	if (_title == title) {
		return;
	}
	_title = title;
	_titleLabel->setText(title);
	resizeEvent(nullptr);
	_changes.fire({});
}

int SidePanel::desiredWidth() const {
	return _width;
}

void SidePanel::setDesiredWidth(int width) {
	const auto clamped = ClampPanelWidth(width);
	if (_width != clamped) {
		_width = clamped;
		_changes.fire({});
	}
}

void SidePanel::scrollToBottom() {
	_scroll->scrollToY(_scroll->scrollTopMax());
}

rpl::producer<> SidePanel::closeRequests() const {
	return _closeRequests.events();
}

rpl::producer<> SidePanel::changes() const {
	return _changes.events();
}

void SidePanel::resizeEvent(QResizeEvent *e) {
	const auto titleHeight = st::boxTitleHeight;
	_close->moveToRight(0, (titleHeight - _close->height()) / 2);
	const auto left = st::boxRowPadding.left();
	const auto available = std::max(
		width() - left - _close->width(),
		1);
	_titleLabel->resizeToWidth(std::min(
		available,
		_titleLabel->textMaxWidth()));
	_titleLabel->moveToLeft(left, (titleHeight - _titleLabel->height()) / 2);
	_scroll->setGeometry(
		0,
		titleHeight,
		width(),
		std::max(height() - titleHeight, 0));
	_content->resizeToWidth(width());
}

void SidePanel::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(e->rect(), st::windowBg);
	p.fillRect(
		0,
		st::boxTitleHeight - st::lineWidth,
		width(),
		st::lineWidth,
		st::shadowFg);
}

QWidget *SidePanel::addItem(
		const QString &type,
		const QVariantMap &props,
		Fn<void(QString event, QVariant value)> fire) {
	const auto container = content();
	const auto text = props.value(u"text"_q).toString();
	const auto padding = st::boxRowPadding;
	const auto skip = st::defaultVerticalListSkip;
	auto result = (QWidget*)nullptr;
	if (type == u"label"_q) {
		const auto bold = props.value(u"bold"_q).toBool();
		const auto muted = props.value(u"muted"_q).toBool();
		const auto label = container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				text,
				(bold
					? st::defaultSubsectionTitle
					: muted
					? st::boxDividerLabel
					: st::boxLabel)),
			bold
				? st::defaultSubsectionTitlePadding
				: (padding + QMargins(0, skip / 2, 0, skip / 2)));
		label->setSelectable(!bold);
		result = label;
	} else if (type == u"button"_q) {
		const auto style = props.value(u"style"_q).toString();
		const auto icon = LookupIcon(props.value(u"icon"_q).toString());
		if (style == u"row"_q || icon) {
			const auto button = Settings::AddButtonWithIcon(
				container,
				rpl::single(text),
				icon ? st::settingsButton : st::settingsButtonNoIcon,
				{ icon });
			button->setClickedCallback([=] { fire(u"click"_q, QVariant()); });
			result = button;
		} else {
			const auto button = container->add(
				object_ptr<Ui::RoundButton>(
					container,
					rpl::single(text),
					(style == u"light"_q
						? st::defaultLightButton
						: st::defaultActiveButton)),
				padding + QMargins(0, skip / 2, 0, skip / 2));
			button->setTextTransform(
				Ui::RoundButtonTextTransform::NoTransform);
			button->setFullWidth(std::max(
				width() - padding.left() - padding.right(),
				st::defaultActiveButton.width));
			container->widthValue() | rpl::on_next([=](int width) {
				button->setFullWidth(std::max(
					width - padding.left() - padding.right(),
					1));
			}, button->lifetime());
			button->setClickedCallback([=] { fire(u"click"_q, QVariant()); });
			result = button;
		}
	} else if (type == u"input"_q) {
		const auto multiline = props.value(u"multiline"_q).toBool();
		const auto field = container->add(
			object_ptr<Ui::InputField>(
				container,
				st::defaultInputField,
				(multiline
					? Ui::InputField::Mode::MultiLine
					: Ui::InputField::Mode::SingleLine),
				rpl::single(props.value(u"placeholder"_q).toString()),
				props.value(u"value"_q).toString()),
			padding + QMargins(0, skip / 2, 0, skip / 2));
		if (multiline) {
			field->setMaxHeight(st::defaultInputField.heightMax);
			field->setSubmitSettings(Ui::InputSubmitSettings::CtrlEnter);
		}
		field->changes() | rpl::on_next([=] {
			fire(u"change"_q, field->getLastText());
		}, field->lifetime());
		field->submits() | rpl::on_next([=] {
			fire(u"submit"_q, field->getLastText());
		}, field->lifetime());
		result = field;
	} else if (type == u"toggle"_q) {
		const auto button = container->add(object_ptr<Ui::SettingsButton>(
			container,
			rpl::single(text),
			st::settingsButtonNoIcon));
		button->toggleOn(rpl::single(props.value(u"checked"_q).toBool()));
		button->toggledChanges() | rpl::on_next([=](bool checked) {
			fire(u"change"_q, checked);
		}, button->lifetime());
		result = button;
	} else if (type == u"separator"_q) {
		Ui::AddSkip(container, skip / 2);
		Ui::AddDivider(container);
		Ui::AddSkip(container, skip / 2);
	} else if (type == u"skip"_q) {
		const auto px = props.value(u"px"_q).toInt();
		Ui::AddSkip(container, (px > 0) ? px : skip);
	} else {
		return nullptr;
	}
	container->resizeToWidth(width());
	return result;
}

SidePanelHost::SidePanelHost(QWidget *parent)
: RpWidget(parent) {
	hide();
}

SidePanelHost::~SidePanelHost() {
	// ~QWidget deletes the panels after our members are gone, and Qt still
	// delivers their destroyed() to remove() then (the window was closed
	// with panels open). Cut those connections while we are intact.
	for (const auto &panel : base::take(_panels)) {
		if (const auto raw = panel.data()) {
			QObject::disconnect(raw, nullptr, this, nullptr);
		}
	}
}

void SidePanelHost::add(not_null<SidePanel*> panel) {
	panel->setParent(this);
	_panels.push_back(panel.get());
	QObject::connect(panel, &QObject::destroyed, this, [=] {
		remove(panel);
	});
	panel->changes() | rpl::on_next([=] {
		refreshTabs();
		_layoutRequests.fire({});
	}, lifetime());
	activate(panel);
}

void SidePanelHost::activate(not_null<SidePanel*> panel) {
	_active = panel.get();
	for (const auto &other : _panels) {
		if (other) {
			other->setVisible(other == _active);
		}
	}
	refreshTabs();
	updateInnerGeometry();
	_layoutRequests.fire({});
}

void SidePanelHost::remove(QWidget *panel) {
	const auto i = ranges::find(_panels, panel, [](const auto &weak) {
		return static_cast<QWidget*>(weak.data());
	});
	if (i != end(_panels)) {
		_panels.erase(i);
	}
	_panels.erase(
		ranges::remove(_panels, nullptr, [](const auto &weak) {
			return weak.data();
		}),
		end(_panels));
	if (_active == panel || !_active) {
		_active = _panels.empty() ? nullptr : _panels.back().data();
		if (_active) {
			_active->show();
		}
	}
	refreshTabs();
	updateInnerGeometry();
	_layoutRequests.fire({});
}

bool SidePanelHost::empty() const {
	return !_active;
}

int SidePanelHost::desiredWidth() const {
	return _active ? _active->desiredWidth() : 0;
}

rpl::producer<> SidePanelHost::layoutRequests() const {
	return _layoutRequests.events();
}

void SidePanelHost::refreshTabs() {
	if (_panels.size() < 2) {
		_tabs.destroy();
		return;
	}
	if (!_tabs) {
		_tabs.create(this, st::defaultTabsSlider);
		_tabs->sectionActivated() | rpl::on_next([=](int index) {
			// setActiveSectionFast() below fires this as well: activating
			// the active panel again would loop through refreshTabs().
			if (index < 0 || index >= int(_panels.size())) {
				return;
			}
			const auto panel = _panels[index];
			if (!panel || panel == _active) {
				return;
			}
			crl::on_main(this, [=] {
				if (panel && panel != _active) {
					activate(panel.data());
				}
			});
		}, _tabs->lifetime());
		_tabs->show();
	}
	auto titles = std::vector<QString>();
	auto active = 0;
	for (auto i = 0; i != int(_panels.size()); ++i) {
		const auto panel = _panels[i].data();
		titles.push_back(panel ? panel->title() : QString());
		if (panel == _active) {
			active = i;
		}
	}
	_tabs->setSections(titles);
	_tabs->setActiveSectionFast(active);
	updateInnerGeometry();
}

void SidePanelHost::updateInnerGeometry() {
	const auto left = st::lineWidth;
	const auto inner = std::max(width() - left, 0);
	auto top = 0;
	if (_tabs) {
		_tabs->resizeToWidth(inner);
		_tabs->moveToLeft(left, 0);
		top += _tabs->height();
	}
	if (_active) {
		_active->setGeometry(left, top, inner, std::max(height() - top, 0));
	}
}

void SidePanelHost::resizeEvent(QResizeEvent *e) {
	updateInnerGeometry();
}

void SidePanelHost::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(e->rect(), st::windowBg);
	p.fillRect(0, 0, st::lineWidth, height(), st::shadowFg);
}

void ShowActionToast(
		const QString &title,
		const QString &text,
		const QString &buttonText,
		Fn<void()> callback,
		Fn<void()> destroyed) {
	auto config = Ui::Toast::Config{
		.title = title,
		.text = { text },
		.duration = crl::time(5000),
	};
	const auto st = std::make_shared<style::Toast>(*config.st);
	const auto &buttonSt = st::historyPremiumViewSet;
	if (callback) {
		st->padding.setRight(st->padding.right()
			+ buttonSt.style.font->width(buttonText)
			- buttonSt.width);
		config.acceptinput = true;
	}
	config.st = st.get();
	const auto weak = Ui::Toast::Show(std::move(config));
	const auto strong = weak.get();
	if (!strong) {
		if (destroyed) {
			destroyed();
		}
		return;
	}
	const auto widget = strong->widget();
	widget->lifetime().add([st, destroyed] {
		if (destroyed) {
			destroyed();
		}
	});
	if (!callback) {
		return;
	}
	const auto hide = [weak] {
		if (const auto strong = weak.get()) {
			strong->hideAnimated();
		}
	};
	const auto fired = std::make_shared<bool>(false);
	const auto activate = [=] {
		if (!*fired) {
			*fired = true;
			callback();
		}
		hide();
	};
	const auto background = Ui::CreateChild<Ui::AbstractButton>(
		widget.get());
	background->setAcceptBoth();
	background->show();
	background->addClickHandler([=](Qt::MouseButton button) {
		if (button == Qt::LeftButton) {
			activate();
		} else {
			hide();
		}
	});
	const auto button = Ui::CreateChild<Ui::RoundButton>(
		widget.get(),
		rpl::single(buttonText),
		buttonSt);
	button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	button->show();
	button->setClickedCallback(activate);
	rpl::combine(
		widget->sizeValue(),
		button->sizeValue()
	) | rpl::on_next([=](QSize outer, QSize inner) {
		button->moveToRight(
			0,
			(outer.height() - inner.height()) / 2,
			outer.width());
		background->resize(outer);
	}, widget->lifetime());
}

} // namespace Damza::Plugins::UiExt
