// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/ui/settings_plugins.h"

#include "damza/damza_lang.h"
#include "damza/plugins/plugin_install.h"
#include "damza/plugins/plugin_instance.h"
#include "damza/plugins/plugin_manager.h"
#include "damza/ui/plugin_boxes.h"
#include "damza/ui/plugin_devtools.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/empty_userpic.h"
#include "ui/layers/show.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QtCore/QMimeData>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>

namespace Settings {
namespace {

using namespace Damza::Plugins;
using namespace Damza::PluginsUi;

[[nodiscard]] bool Matches(const PluginState &state, const QString &query) {
	if (query.isEmpty()) {
		return true;
	}
	const auto &m = state.manifest;
	for (const auto &text : { m.name, m.id, m.description, m.author }) {
		if (text.contains(query, Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QString Summary() {
	const auto &plugins = Manager::Get().plugins();
	auto enabled = 0;
	auto loaded = 0;
	auto errors = 0;
	for (const auto &state : plugins) {
		enabled += state.enabled ? 1 : 0;
		loaded += state.loaded ? 1 : 0;
		errors += state.error.isEmpty() ? 0 : 1;
	}
	auto parts = QStringList{
		Damza::Text("%1 installed", "установлено: %1").arg(plugins.size()),
		Damza::Text("%1 running", "работает: %1").arg(loaded),
	};
	if (enabled != loaded) {
		parts.push_back(
			Damza::Text("%1 enabled", "включено: %1").arg(enabled));
	}
	if (errors) {
		parts.push_back(
			Damza::Text("%1 with errors", "с ошибками: %1").arg(errors));
	}
	return parts.join(u" · "_q);
}

void PaintStatus(QPainter &p, QRect rect, const style::color &color) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(QPen(st::windowBg, st::lineWidth * 2));
	p.setBrush(color);
	p.drawEllipse(rect);
}

} // namespace

DamzaPlugins::DamzaPlugins(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _controller(controller) {
	setupContent();
}

rpl::producer<QString> DamzaPlugins::title() {
	return rpl::single(Damza::Text("Plugins", "Плагины"));
}

std::shared_ptr<Ui::Show> DamzaPlugins::uiShow() const {
	return _controller->uiShow();
}

QString DamzaPlugins::defaultAuthor() const {
	const auto user = _controller->session().user();
	const auto username = user->username();
	return username.isEmpty() ? user->name() : (u"@"_q + username);
}

void DamzaPlugins::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	if (!Manager::Exists()) {
		Ui::AddDividerText(
			content,
			rpl::single(Damza::Text(
				"Plugin system is not running.",
				"Система плагинов не запущена.")));
		Ui::ResizeFitChild(this, content);
		return;
	}
	setupHeader(content);
	_list = content->add(object_ptr<Ui::VerticalLayout>(content));
	rebuild();
	setupFooter(content);

	Manager::Get().pluginsChanges(
	) | rpl::on_next([=] {
		scheduleRebuild();
	}, _list->lifetime());

	setAcceptDrops(true);
	Ui::ResizeFitChild(this, content);
}

void DamzaPlugins::setupHeader(not_null<Ui::VerticalLayout*> container) {
	const auto left = st::settingsButtonNoIcon.padding.left();
	const auto right = st::settingsButtonNoIcon.padding.right();
	Ui::AddSkip(container);
	const auto titleSt = container->lifetime().make_state<style::FlatLabel>(
		st::boxTitle);
	titleSt->minWidth = 0;
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			Damza::Text("DamZaGram plugins", "Плагины DamZaGram"),
			*titleSt),
		style::margins(left, st::defaultVerticalListSkip, right, 0));
	_summary = Summary();
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			_summary.value(),
			*MakeLabelStyle(
				container->lifetime(),
				st::defaultFlatLabel,
				st::windowSubTextFg)),
		style::margins(left, st::lineWidth * 4, right, 0));

	const auto parent = container.get();
	AddButtonsFlow(container, {
		{
			.text = Damza::Text("Install…", "Установить…"),
			.callback = [=] { showInstallMenu(parent); },
			.primary = true,
		},
		{
			.text = Damza::Text("Create plugin", "Создать плагин"),
			.callback = [=] { ShowCreateBox(uiShow(), defaultAuthor()); },
		},
		{
			.text = u"DevTools"_q,
			.callback = [=] { ShowDevTools(); },
		},
		{
			.text = Damza::Text("Open folder", "Открыть папку"),
			.callback = [=] { RevealInFolder(Manager::Get().pluginsPath()); },
		},
		{
			.text = Damza::Text("Reload all", "Перезагрузить все"),
			.callback = [=] {
				crl::on_main(this, [=] {
					if (Manager::Exists()) {
						Manager::Get().reloadAll();
						uiShow()->showToast(Damza::Text(
							"Plugins reloaded",
							"Плагины перезагружены"));
					}
				});
			},
		},
	}, style::margins(left, st::defaultVerticalListSkip * 3, right, 0));

	Ui::AddSkip(container);
	AddButtonWithIcon(
		container,
		rpl::single(Damza::Text(
			"Hot reload on file change",
			"Горячая перезагрузка при изменении файлов")),
		st::settingsButton,
		{ &st::menuIconRestore }
	)->toggleOn(
		rpl::single(Manager::Get().devMode())
	)->toggledChanges(
	) | rpl::on_next([](bool enabled) {
		if (Manager::Exists()) {
			Manager::Get().setDevMode(enabled);
		}
	}, container->lifetime());

	const auto search = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::defaultInputField,
			rpl::single(Damza::Text("Search plugins", "Поиск плагинов"))),
		style::margins(left, 0, right, st::defaultVerticalListSkip));
	search->changes() | rpl::on_next([=] {
		_query = search->getLastText().trimmed();
		scheduleRebuild();
	}, search->lifetime());

	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		rpl::single(Damza::Text(
			"Drop a .zip, a .js file or a plugin folder here to install it. "
			"Files named *.dzg.zip or *.plugin.js open an install window "
			"right from a chat. Plugins get full access to the client "
			"and your account: install only code you trust.",
			"Перетащи сюда .zip, .js или папку плагина, чтобы установить "
			"его. Файлы *.dzg.zip и *.plugin.js из чатов открывают окно "
			"установки прямо по клику. Плагины получают полный доступ к "
			"клиенту и аккаунту: ставь только тот код, которому "
			"доверяешь.")));
}

void DamzaPlugins::setupFooter(not_null<Ui::VerticalLayout*> container) {
	Ui::AddSkip(container);
	auto text = TextWithEntities{ u"DamZaGram · "_q }
		.append(Damza::Text("JavaScript plugins on ", "JS-плагины на "))
		.append(u"QuickJS · "_q)
		.append(Damza::Text("by ", "автор "))
		.append(Ui::Text::Link(u"@wetakush"_q, u"https://t.me/wetakush"_q));
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			rpl::single(text),
			*MakeLabelStyle(
				container->lifetime(),
				st::boxDividerLabel,
				st::windowSubTextFg)),
		style::margins(
			st::settingsButtonNoIcon.padding.left(),
			0,
			st::settingsButtonNoIcon.padding.right(),
			st::defaultVerticalListSkip * 2));
}

void DamzaPlugins::scheduleRebuild() {
	if (_rebuildScheduled || !_list) {
		return;
	}
	_rebuildScheduled = true;
	crl::on_main(_list, [=] {
		_rebuildScheduled = false;
		rebuild();
	});
}

void DamzaPlugins::showInstallMenu(not_null<QWidget*> parent) {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	_menu->addAction(
		Damza::Text("From a file (.zip, .js)…", "Из файла (.zip, .js)…"),
		[=] { ChooseFileAndInstall(uiShow(), this); },
		&st::menuIconFile);
	_menu->addAction(
		Damza::Text("From a folder…", "Из папки…"),
		[=] { ChooseFolderAndInstall(uiShow(), this); },
		&st::menuIconShowInFolder);
	_menu->addAction(
		Damza::Text("From a link…", "По ссылке…"),
		[=] { ShowInstallFromUrlBox(uiShow()); },
		&st::menuIconLink);
	_menu->popup(QCursor::pos());
}

void DamzaPlugins::rebuild() {
	if (!_list || !Manager::Exists()) {
		return;
	}
	_summary = Summary();
	_list->clear();
	const auto &plugins = Manager::Get().plugins();
	auto shown = 0;
	for (const auto &state : plugins) {
		if (Matches(state, _query)) {
			addCard(_list, state);
			++shown;
		}
	}
	if (!shown) {
		Ui::AddSkip(_list);
		Ui::AddDividerText(
			_list,
			rpl::single(plugins.empty()
				? Damza::Text(
					"No plugins yet. Create your own with \"Create plugin\" "
					"or install a ready one.",
					"Плагинов пока нет. Создай свой кнопкой «Создать "
					"плагин» или установи готовый.")
				: Damza::Text("Nothing found.", "Ничего не найдено.")));
	}
	_list->resizeToWidth(width());
}

void DamzaPlugins::addCard(
		not_null<Ui::VerticalLayout*> list,
		const PluginState &state) {
	const auto id = state.manifest.id;
	const auto card = list->add(object_ptr<Ui::VerticalLayout>(list));
	const auto builtin = IsBuiltinDirectory(state.manifest.dir);
	const auto hasSettings = !state.manifest.settings.isEmpty();
	const auto left = st::settingsButtonNoIcon.padding.left();
	const auto right = st::settingsButtonNoIcon.padding.right();
	const auto avatarSize = style::ConvertScale(42);
	const auto textLeft = left + avatarSize + style::ConvertScale(14);
	const auto metaTop = style::ConvertScale(36);
	const auto muted = MakeLabelStyle(
		card->lifetime(),
		st::defaultFlatLabel,
		st::windowSubTextFg);

	const auto buttonSt = card->lifetime().make_state<style::SettingsButton>(
		st::settingsButtonNoIcon);
	buttonSt->style = st::semiboldTextStyle;
	const auto header = AddButtonWithIcon(
		card,
		rpl::single(state.manifest.name),
		*buttonSt);
	header->setPaddingOverride({
		textLeft,
		style::ConvertScale(12),
		right,
		style::ConvertScale(32),
	});
	header->toggleOn(
		rpl::single(state.enabled)
	)->toggledChanges(
	) | rpl::on_next([=](bool enabled) {
		crl::on_main([=] {
			if (Manager::Exists()) {
				Manager::Get().setEnabled(id, enabled);
			}
		});
	}, header->lifetime());

	const auto status = !state.error.isEmpty()
		? &st::attentionButtonFg
		: state.loaded
		? &st::boxTextFgGood
		: nullptr;
	const auto userpic = std::make_shared<Ui::EmptyUserpic>(
		Ui::EmptyUserpic::UserpicColor(
			Ui::EmptyUserpic::ColorIndex(qHash(id))),
		state.manifest.name);
	const auto avatar = Ui::CreateChild<Ui::RpWidget>(header);
	avatar->setAttribute(Qt::WA_TransparentForMouseEvents);
	avatar->resize(avatarSize, avatarSize);
	avatar->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(avatar);
		userpic->paintRounded(
			p,
			0,
			0,
			avatarSize,
			avatarSize,
			avatarSize / 4);
		if (status) {
			const auto dot = avatarSize / 3;
			PaintStatus(
				p,
				QRect(avatarSize - dot, avatarSize - dot, dot, dot),
				*status);
		}
	}, avatar->lifetime());

	auto meta = QStringList{ u"v"_q + state.manifest.version };
	if (!state.manifest.author.isEmpty()) {
		meta.push_back(state.manifest.author);
	}
	if (builtin) {
		meta.push_back(Damza::Text("built-in", "встроенный"));
	}
	if (state.manifest.type == u"script"_q) {
		meta.push_back(u"script"_q);
	}
	const auto metaSt = card->lifetime().make_state<style::FlatLabel>(*muted);
	metaSt->maxHeight = metaSt->style.font->height;
	const auto metaLabel = Ui::CreateChild<Ui::FlatLabel>(
		header,
		meta.join(u" · "_q),
		*metaSt);
	metaLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	header->sizeValue() | rpl::on_next([=](QSize size) {
		avatar->moveToLeft(
			left,
			(size.height() - avatarSize) / 2,
			size.width());
		const auto toggleSpace = st::settingsButtonNoIcon.toggleSkip
			+ st::settingsButtonNoIcon.toggle.diameter * 2
			+ st::settingsButtonNoIcon.toggle.width;
		metaLabel->resizeToWidth(
			std::max(size.width() - textLeft - toggleSpace, 1));
		metaLabel->moveToLeft(textLeft, metaTop, size.width());
	}, header->lifetime());

	if (!state.manifest.description.isEmpty()) {
		card->add(
			object_ptr<Ui::FlatLabel>(
				card,
				state.manifest.description,
				st::boxDividerLabel),
			style::margins(textLeft, 0, right, 0));
	}
	if (!state.error.isEmpty()) {
		const auto error = card->add(
			object_ptr<Ui::FlatLabel>(
				card,
				u"⚠ "_q + state.error,
				*MakeLabelStyle(
					card->lifetime(),
					st::boxDividerLabel,
					st::attentionButtonFg)),
			style::margins(textLeft, st::lineWidth * 4, right, 0));
		error->setSelectable(true);
	} else if (state.enabled && !state.loaded) {
		card->add(
			object_ptr<Ui::FlatLabel>(
				card,
				Damza::Text("Enabled, but not loaded.", "Включён, но не загружен."),
				*muted),
			style::margins(textLeft, st::lineWidth * 4, right, 0));
	}

	auto buttons = std::vector<FlowButton>();
	if (hasSettings) {
		buttons.push_back({
			.text = Damza::Text("Settings", "Настройки"),
			.callback = [=] { ShowSettingsBox(uiShow(), id); },
			.primary = state.enabled,
		});
	}
	buttons.push_back({
		.text = Damza::Text("Log", "Лог"),
		.callback = [=] { ShowLogBox(uiShow(), id); },
	});
	if (state.loaded) {
		buttons.push_back({
			.text = Damza::Text("Console", "Консоль"),
			.callback = [=] { ShowDevTools(id); },
		});
	}
	if (state.enabled) {
		buttons.push_back({
			.text = Damza::Text("Reload", "Перезагрузить"),
			.callback = [=] {
				crl::on_main(this, [=] {
					if (Manager::Exists()) {
						Manager::Get().reload(id);
					}
				});
			},
		});
	}
	const auto dir = state.manifest.dir;
	buttons.push_back({
		.text = Damza::Text("Folder", "Папка"),
		.callback = [=] { RevealInFolder(dir); },
	});
	buttons.push_back({
		.text = builtin
			? Damza::Text("Remove (disables)", "Удалить (выключит)")
			: Damza::Text("Remove", "Удалить"),
		.callback = [=] { ShowUninstallBox(uiShow(), id); },
		.attention = true,
	});
	AddButtonsFlow(
		card,
		std::move(buttons),
		style::margins(
			textLeft,
			st::defaultVerticalListSkip * 2,
			right,
			st::defaultVerticalListSkip * 3));
	Ui::AddDivider(card);
}

void DamzaPlugins::dragEnterEvent(QDragEnterEvent *e) {
	if (e->mimeData()->hasUrls()) {
		e->acceptProposedAction();
	}
}

void DamzaPlugins::dropEvent(QDropEvent *e) {
	const auto urls = e->mimeData()->urls();
	for (const auto &url : urls) {
		if (url.isLocalFile()) {
			e->acceptProposedAction();
			InstallFromPath(uiShow(), url.toLocalFile());
			return;
		}
	}
	if (!urls.isEmpty()) {
		e->acceptProposedAction();
		const auto show = uiShow();
		DownloadPackage(urls.front().toString(), [=](Package package) {
			ShowInstallBox(show, std::move(package));
		});
	}
}

} // namespace Settings
