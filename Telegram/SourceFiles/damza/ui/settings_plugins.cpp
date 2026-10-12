// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/ui/settings_plugins.h"

#include "damza/damza_lang.h"
#include "damza/plugins/plugin_instance.h"
#include "damza/plugins/plugin_manager.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QtCore/QJsonArray>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>

namespace Settings {
namespace {

using namespace Damza::Plugins;

constexpr auto kLogLinesInBox = 80;

void ShowLogBox(
		not_null<Window::SessionController*> controller,
		const QString &id) {
	const auto plugin = Manager::Get().instance(id);
	auto lines = plugin ? plugin->logLines() : QStringList();
	if (lines.size() > kLogLinesInBox) {
		lines = lines.mid(lines.size() - kLogLinesInBox);
	}
	const auto text = lines.isEmpty()
		? Damza::Text("Log is empty.", "Лог пуст.")
		: lines.join('\n');
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Damza::Text("Plugin log", "Лог плагина")
			+ u": "_q + id));
		box->setWidth(st::boxWideWidth);
		const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			text,
			st::boxLabel));
		label->setSelectable(true);
		box->addButton(rpl::single(Damza::Text("Copy", "Копировать")), [=] {
			QGuiApplication::clipboard()->setText(text);
		});
		box->addButton(rpl::single(Damza::Text("Close", "Закрыть")), [=] {
			box->closeBox();
		});
	}));
}

void ShowPluginBox(
		not_null<Window::SessionController*> controller,
		const PluginState &state) {
	const auto id = state.manifest.id;
	const auto schema = state.manifest.settings;
	const auto values = Manager::Get().settingsValues(id);
	const auto manifest = state.manifest;
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(manifest.name));
		auto about = manifest.description;
		if (!manifest.author.isEmpty()) {
			about += (about.isEmpty() ? QString() : u"\n\n"_q)
				+ Damza::Text("Author", "Автор") + u": "_q + manifest.author;
		}
		about += (about.isEmpty() ? QString() : u"\n"_q)
			+ Damza::Text("Version", "Версия") + u": "_q + manifest.version
			+ u"\n"_q + manifest.dir;
		box->addRow(object_ptr<Ui::FlatLabel>(box, about, st::boxLabel));

		struct Control {
			QString key;
			QString type;
			Ui::Checkbox *check = nullptr;
			Ui::InputField *field = nullptr;
		};
		const auto controls = box->lifetime().make_state<
			std::vector<Control>>();
		for (const auto &entry : schema) {
			const auto object = entry.toObject();
			const auto key = object.value(u"key"_q).toString();
			if (key.isEmpty()) {
				continue;
			}
			const auto type = object.value(u"type"_q).toString(u"string"_q);
			const auto title = object.value(u"title"_q).toString(key);
			const auto value = values.value(key);
			auto control = Control{ .key = key, .type = type };
			if (type == u"bool"_q) {
				control.check = box->addRow(object_ptr<Ui::Checkbox>(
					box,
					title,
					value.toBool(),
					st::defaultBoxCheckbox));
			} else {
				control.field = box->addRow(object_ptr<Ui::InputField>(
					box,
					st::defaultInputField,
					(type == u"text"_q
						? Ui::InputField::Mode::MultiLine
						: Ui::InputField::Mode::SingleLine),
					rpl::single(title),
					(value.isDouble()
						? QString::number(value.toDouble())
						: value.toString())));
			}
			controls->push_back(control);
		}
		if (!controls->empty()) {
			box->addButton(rpl::single(Damza::Text("Save", "Сохранить")), [=] {
				auto &manager = Manager::Get();
				for (const auto &control : *controls) {
					if (control.check) {
						manager.setSettingValue(
							id,
							control.key,
							control.check->checked());
					} else if (control.type == u"number"_q) {
						manager.setSettingValue(
							id,
							control.key,
							control.field->getLastText().toDouble());
					} else {
						manager.setSettingValue(
							id,
							control.key,
							control.field->getLastText());
					}
				}
				box->closeBox();
			});
		}
		box->addButton(rpl::single(Damza::Text("Close", "Закрыть")), [=] {
			box->closeBox();
		});
		box->addLeftButton(rpl::single(Damza::Text("Log", "Лог")), [=] {
			ShowLogBox(controller, id);
		});
	}));
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

	Ui::AddSkip(content);
	AddButtonWithIcon(
		content,
		rpl::single(Damza::Text("Open plugins folder", "Открыть папку плагинов")),
		st::settingsButton,
		{ &st::menuIconShowInFolder }
	)->addClickHandler([] {
		QDesktopServices::openUrl(
			QUrl::fromLocalFile(Manager::Get().pluginsPath()));
	});
	AddButtonWithIcon(
		content,
		rpl::single(Damza::Text("Reload all plugins", "Перезагрузить все плагины")),
		st::settingsButton,
		{ &st::menuIconRestore }
	)->addClickHandler([] {
		crl::on_main([] {
			if (Manager::Exists()) {
				Manager::Get().reloadAll();
			}
		});
	});
	AddButtonWithIcon(
		content,
		rpl::single(Damza::Text(
			"Hot reload on file change",
			"Перезагружать при изменении файлов")),
		st::settingsButton,
		{ &st::menuIconCustomize }
	)->toggleOn(
		rpl::single(Manager::Get().devMode())
	)->toggledChanges(
	) | rpl::on_next([](bool enabled) {
		Manager::Get().setDevMode(enabled);
	}, content->lifetime());
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		rpl::single(Damza::Text(
			"Every plugin is a folder with manifest.json and main.js "
			"inside the plugins folder. Plugins have full access to the "
			"client and your account: install only code you trust.",
			"Каждый плагин — папка с manifest.json и main.js в папке "
			"плагинов. Плагины получают полный доступ к клиенту и "
			"аккаунту: ставь только тот код, которому доверяешь.")));

	const auto list = content->add(object_ptr<Ui::VerticalLayout>(content));
	rebuild(list);

	Manager::Get().pluginsChanges(
	) | rpl::on_next([=] {
		// Rebuild later: the change may come from a button in the list.
		crl::on_main(list, [=] { rebuild(list); });
	}, list->lifetime());

	Ui::ResizeFitChild(this, content);
}

void DamzaPlugins::rebuild(not_null<Ui::VerticalLayout*> list) {
	list->clear();
	const auto &plugins = Manager::Get().plugins();
	Ui::AddSkip(list);
	Ui::AddSubsectionTitle(
		list,
		rpl::single(Damza::Text("Installed", "Установленные")
			+ u" ("_q + QString::number(plugins.size()) + ')'));
	if (plugins.empty()) {
		Ui::AddDividerText(
			list,
			rpl::single(Damza::Text(
				"No plugins yet. Copy a plugin folder into the plugins "
				"folder and it will show up here.",
				"Плагинов пока нет. Скопируй папку плагина в папку "
				"плагинов, и он появится здесь.")));
		list->resizeToWidth(width());
		return;
	}
	for (const auto &state : plugins) {
		const auto id = state.manifest.id;
		const auto title = state.manifest.name
			+ u"  "_q + state.manifest.version;
		AddButtonWithIcon(
			list,
			rpl::single(title),
			st::settingsButtonNoIcon
		)->toggleOn(
			rpl::single(state.enabled)
		)->toggledChanges(
		) | rpl::on_next([=](bool enabled) {
			crl::on_main([=] {
				if (Manager::Exists()) {
					Manager::Get().setEnabled(id, enabled);
				}
			});
		}, list->lifetime());

		const auto copy = state;
		AddButtonWithIcon(
			list,
			rpl::single(Damza::Text("Settings and log", "Настройки и лог")),
			st::settingsButtonNoIcon
		)->addClickHandler([=] {
			ShowPluginBox(_controller, copy);
		});
		if (state.enabled) {
			AddButtonWithIcon(
				list,
				rpl::single(Damza::Text("Reload", "Перезагрузить")),
				st::settingsButtonNoIcon
			)->addClickHandler([=] {
				crl::on_main([=] {
					if (Manager::Exists()) {
						Manager::Get().reload(id);
					}
				});
			});
		}

		auto about = state.manifest.description;
		if (!state.error.isEmpty()) {
			about += (about.isEmpty() ? QString() : u"\n"_q)
				+ Damza::Text("Error", "Ошибка") + u": "_q + state.error;
		} else if (state.enabled && !state.loaded) {
			about += (about.isEmpty() ? QString() : u"\n"_q)
				+ Damza::Text("Not loaded", "Не загружен");
		}
		if (!about.isEmpty()) {
			Ui::AddDividerText(list, rpl::single(about));
		} else {
			Ui::AddDivider(list);
		}
	}
	list->resizeToWidth(width());
}

} // namespace Settings
