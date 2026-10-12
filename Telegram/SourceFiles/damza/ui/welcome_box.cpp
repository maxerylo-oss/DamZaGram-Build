// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/ui/welcome_box.h"

#include "base/call_delayed.h"
#include "core/application.h"
#include "core/version.h"
#include "damza/damza_brand.h"
#include "damza/damza_lang.h"
#include "damza/ui/settings_plugins.h"
#include "lang/lang_text_entity.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

#include <QtCore/QFile>
#include <QtSvg/QSvgRenderer>

namespace Damza {
namespace {

// Bump to show the box again for the same client version.
constexpr auto kWelcomeRevision = 1;
constexpr auto kShowDelay = crl::time(1200);
constexpr auto kRetryDelay = crl::time(3000);
constexpr auto kLogoSize = 92;

struct Entry {
	const style::icon *icon = nullptr;
	QString title;
	QString about;
};

[[nodiscard]] QString StatePath() {
	return cWorkingDir() + u"tdata/damza_welcome"_q;
}

[[nodiscard]] QString CurrentStamp() {
	return u"%1:%2"_q.arg(AppVersion).arg(kWelcomeRevision);
}

[[nodiscard]] QString ReadStamp() {
	auto file = QFile(StatePath());
	return file.open(QIODevice::ReadOnly)
		? QString::fromUtf8(file.readAll()).trimmed()
		: QString();
}

void WriteStamp() {
	auto file = QFile(StatePath());
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(CurrentStamp().toUtf8());
	}
}

[[nodiscard]] std::vector<Entry> Entries() {
	return {
		{
			&st::menuIconBotCommands,
			Text("JavaScript plugins", "JavaScript-плагины"),
			Text(
				"Modern JS on QuickJS: async/await, modules, classes. "
				"Plugins read chats, send messages, add buttons "
				"and react to everything that happens.",
				"Современный JS на QuickJS: async/await, модули, классы. "
				"Плагины читают чаты, отправляют сообщения, добавляют "
				"кнопки и реагируют на всё, что происходит."),
		},
		{
			&st::menuIconRestore,
			Text("Hot reload", "Горячая перезагрузка"),
			Text(
				"Save main.js and the plugin restarts instantly, "
				"no client restart needed.",
				"Сохрани main.js — плагин перезапустится сразу, "
				"без перезапуска клиента."),
		},
		{
			&st::menuIconFave,
			Text("Built-in plugins", "Встроенные плагины"),
			Text(
				"Useful plugins ship with the client. "
				"Turn them on in one click.",
				"Полезные плагины уже в комплекте. "
				"Включаются в один клик."),
		},
		{
			&st::menuIconExperimental,
			Text("DevTools console", "Консоль DevTools"),
			Text(
				"Run JavaScript against the live client "
				"and read plugin logs.",
				"Выполняй JavaScript прямо в клиенте "
				"и читай логи плагинов."),
		},
		{
			&st::menuIconStealth,
			Text("Everything from AyuGram", "Всё из AyuGram"),
			Text(
				"Ghost mode, saved deleted and edited messages, "
				"filters and deep customization.",
				"Режим призрака, сохранение удалённых и изменённых "
				"сообщений, фильтры и глубокая кастомизация."),
		},
	};
}

[[nodiscard]] TextWithEntities FooterText() {
	auto result = TextWithEntities();
	result.append(Text("Plugins live in ", "Плагины — в разделе "));
	result.append(tr::bold(Text(
		"Settings › DamZaGram Preferences › Plugins",
		"Настройки › Настройки DamZaGram › Плагины")));
	result.append(u".\n\n"_q);
	result.append(Brand::Name());
	result.append(Text(" by ", " от "));
	result.append(tr::link(Brand::AuthorHandle(), Brand::AuthorLink()));
	result.append(Text(" · based on ", " · основан на "));
	result.append(tr::link(
		u"AyuGram"_q,
		QString::fromLatin1(Brand::kUpstreamLink)));
	return result;
}

void AddLogo(not_null<Ui::GenericBox*> box) {
	const auto size = style::ConvertScale(kLogoSize);
	auto owned = object_ptr<Ui::RpWidget>(box->verticalLayout());
	owned->resize(size, size);
	owned->setNaturalWidth(size);
	const auto widget = box->addRow(std::move(owned), style::al_top);
	const auto cache = widget->lifetime().make_state<QImage>();
	widget->paintRequest(
	) | rpl::on_next([=] {
		const auto ratio = style::DevicePixelRatio();
		if (cache->isNull() || cache->devicePixelRatio() != ratio) {
			*cache = QImage(
				QSize(size, size) * ratio,
				QImage::Format_ARGB32_Premultiplied);
			cache->setDevicePixelRatio(ratio);
			cache->fill(Qt::transparent);
			auto q = QPainter(cache);
			auto svg = QSvgRenderer(u":/gui/art/ayu/default/app.svg"_q);
			svg.render(&q, QRectF(0, 0, size, size));
		}
		auto p = QPainter(widget);
		p.drawImage(0, 0, *cache);
	}, widget->lifetime());
}

void AddEntries(not_null<Ui::GenericBox*> box) {
	const auto content = box->verticalLayout().get();
	const auto padding = QMargins(
		st::settingsButton.padding.left(),
		st::boxRowPadding.top(),
		st::boxRowPadding.right(),
		st::boxRowPadding.bottom());
	for (const auto &entry : Entries()) {
		const auto top = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				entry.title,
				st::channelEarnSemiboldLabel),
			padding);
		Ui::AddSkip(content, st::channelEarnHistoryThreeSkip);
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				entry.about,
				st::channelEarnHistoryRecipientLabel),
			padding);
		const auto icon = entry.icon;
		const auto left = Ui::CreateChild<Ui::RpWidget>(content);
		left->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(left);
			icon->paint(p, 0, 0, left->width());
		}, left->lifetime());
		left->resize(icon->size());
		top->geometryValue(
		) | rpl::on_next([=](const QRect &g) {
			left->moveToLeft(
				(g.left() - left->width()) / 2,
				g.top() + st::channelEarnHistoryThreeSkip);
		}, left->lifetime());
		Ui::AddSkip(content);
		Ui::AddSkip(content);
	}
}

void WelcomeBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		bool update) {
	box->setNoContentMargin(true);
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	const auto content = box->verticalLayout().get();
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	AddLogo(box);
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	const auto title = update
		? (Text("What's new in DamZaGram", "Что нового в DamZaGram"))
		: (Text("Welcome to DamZaGram", "Добро пожаловать в DamZaGram"));
	box->addRow(
		object_ptr<Ui::FlatLabel>(content, title, st::boxTitle),
		style::al_top);
	Ui::AddSkip(content);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			content,
			Text(
				"A Telegram client you can program.",
				"Telegram-клиент, который можно программировать."),
			st::channelEarnHistoryRecipientLabel),
		style::al_top);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	AddEntries(box);

	const auto footer = box->addRow(
		object_ptr<Ui::FlatLabel>(
			content,
			rpl::single(FooterText()),
			st::channelEarnHistoryRecipientLabel),
		st::boxRowPadding);
	footer->setLinksTrusted();
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	const auto &st = st::premiumPreviewDoubledLimitsBox;
	box->setStyle(st);
	auto button = object_ptr<Ui::RoundButton>(
		box,
		rpl::single(Text("Open plugins", "Открыть плагины")),
		st::defaultActiveButton);
	button->resizeToWidth(box->width()
		- st.buttonPadding.left()
		- st.buttonPadding.left());
	button->setClickedCallback([=] {
		box->closeBox();
		controller->showSettings(Settings::DamzaPlugins::Id());
	});
	box->addButton(std::move(button));
}

void ScheduleWelcome(
		not_null<Window::SessionController*> controller,
		bool update,
		crl::time delay) {
	base::call_delayed(delay, controller, [=] {
		if (Core::App().passcodeLocked()) {
			ScheduleWelcome(controller, update, kRetryDelay);
			return;
		}
		WriteStamp();
		controller->show(Box(WelcomeBox, controller, update));
	});
}

} // namespace

void ShowWelcomeBox(not_null<Window::SessionController*> controller) {
	controller->show(Box(WelcomeBox, controller, true));
}

void ShowWelcomeIfNeeded(not_null<Window::SessionController*> controller) {
	static auto scheduled = false;
	if (scheduled) {
		return;
	}
	const auto previous = ReadStamp();
	if (previous == CurrentStamp()) {
		return;
	}
	scheduled = true;
	ScheduleWelcome(controller, !previous.isEmpty(), kShowDelay);
}

} // namespace Damza
