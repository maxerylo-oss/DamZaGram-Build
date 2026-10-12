// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/ui/plugin_boxes.h"

#include "core/file_utilities.h"
#include "damza/damza_lang.h"
#include "damza/plugins/plugin_install.h"
#include "damza/plugins/plugin_instance.h"
#include "damza/plugins/plugin_manager.h"
#include "damza/ui/plugin_devtools.h"
#include "damza/ui/settings_plugins.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/color_editor.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Damza::PluginsUi {
namespace {

using namespace Damza::Plugins;

constexpr auto kLogLinesInBox = 300;
constexpr auto kRadioOptionsLimit = 4;
constexpr auto kMaxPluginFileSize = qint64(32 * 1024 * 1024);

[[nodiscard]] QString ValueToText(const QJsonValue &value) {
	if (value.isString()) {
		return value.toString();
	} else if (value.isDouble()) {
		return QString::number(value.toDouble(), 'g', 15);
	} else if (value.isBool()) {
		return value.toBool() ? u"true"_q : u"false"_q;
	} else if (value.isNull() || value.isUndefined()) {
		return QString();
	}
	return QString::fromUtf8(QJsonDocument(value.isArray()
		? QJsonDocument(value.toArray())
		: QJsonDocument(value.toObject())).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QColor ParseColor(QString text) {
	text = text.trimmed();
	if (!text.startsWith('#')) {
		text = '#' + text;
	}
	static const auto re = QRegularExpression(
		u"^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"_q);
	if (!re.match(text).hasMatch()) {
		return QColor();
	} else if (text.size() == 9) {
		const auto rgb = QColor(text.left(7));
		auto ok = false;
		const auto alpha = text.mid(7).toInt(&ok, 16);
		auto result = rgb;
		result.setAlpha(ok ? alpha : 255);
		return result;
	}
	return QColor(text);
}

[[nodiscard]] QString ColorToText(const QColor &color) {
	auto result = u"#%1%2%3"_q
		.arg(color.red(), 2, 16, QChar('0'))
		.arg(color.green(), 2, 16, QChar('0'))
		.arg(color.blue(), 2, 16, QChar('0'));
	if (color.alpha() != 255) {
		result += u"%1"_q.arg(color.alpha(), 2, 16, QChar('0'));
	}
	return result;
}

[[nodiscard]] std::optional<double> ParseNumber(QString text) {
	text = text.trimmed().replace(',', '.');
	auto ok = false;
	const auto result = QLocale::c().toDouble(text, &ok);
	return ok ? std::make_optional(result) : std::nullopt;
}

struct Option {
	QJsonValue value;
	QString title;
};

[[nodiscard]] std::vector<Option> ParseOptions(const QJsonArray &array) {
	auto result = std::vector<Option>();
	for (const auto &entry : array) {
		if (entry.isObject()) {
			const auto object = entry.toObject();
			const auto value = object.value(u"value"_q);
			result.push_back({
				.value = value,
				.title = object.value(u"title"_q).toString(ValueToText(value)),
			});
		} else {
			result.push_back({ .value = entry, .title = ValueToText(entry) });
		}
	}
	return result;
}

[[nodiscard]] int OptionIndex(
		const std::vector<Option> &options,
		const QJsonValue &value) {
	for (auto i = 0; i != int(options.size()); ++i) {
		if (options[i].value == value
			|| ValueToText(options[i].value) == ValueToText(value)) {
			return i;
		}
	}
	return -1;
}

struct Control {
	QString key;
	QJsonValue fallback;
	Fn<std::optional<QJsonValue>()> value;
	Fn<void(const QJsonValue &)> set;
};

[[nodiscard]] style::margins RowPadding(int top = 0, int bottom = 0) {
	const auto &padding = st::boxRowPadding;
	return { padding.left(), top, padding.right(), bottom };
}

void AddHint(
		not_null<Ui::GenericBox*> box,
		const QString &text,
		const style::FlatLabel *st) {
	if (!text.isEmpty()) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(box, text, *st),
			RowPadding(st::lineWidth * 2, st::defaultVerticalListSkip));
	}
}

not_null<Ui::FlatLabel*> AddTitle(
		not_null<Ui::GenericBox*> box,
		const QString &text) {
	return box->addRow(
		object_ptr<Ui::FlatLabel>(box, text, st::defaultSubsectionTitle),
		RowPadding(st::defaultVerticalListSkip * 2, st::lineWidth * 4));
}

[[nodiscard]] Control AddBoolControl(
		not_null<Ui::GenericBox*> box,
		const QString &title,
		const QJsonValue &value) {
	const auto check = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			title,
			value.toBool(),
			st::defaultBoxCheckbox),
		RowPadding(st::defaultVerticalListSkip * 2, st::lineWidth * 2));
	return {
		.value = [=] { return std::make_optional(QJsonValue(check->checked())); },
		.set = [=](const QJsonValue &v) { check->setChecked(v.toBool()); },
	};
}

[[nodiscard]] Control AddTextControl(
		not_null<Ui::GenericBox*> box,
		const QString &title,
		const QJsonValue &value,
		bool multiline) {
	const auto field = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			(multiline
				? Ui::InputField::Mode::MultiLine
				: Ui::InputField::Mode::SingleLine),
			rpl::single(title),
			ValueToText(value)),
		RowPadding(st::defaultVerticalListSkip, 0));
	return {
		.value = [=] {
			return std::make_optional(QJsonValue(field->getLastText()));
		},
		.set = [=](const QJsonValue &v) { field->setText(ValueToText(v)); },
	};
}

[[nodiscard]] Control AddNumberControl(
		not_null<Ui::GenericBox*> box,
		const QString &title,
		const QJsonObject &entry,
		const QJsonValue &value) {
	const auto min = entry.value(u"min"_q);
	const auto max = entry.value(u"max"_q);
	const auto integer = entry.value(u"integer"_q).toBool()
		|| (entry.value(u"step"_q).toDouble() == 1.);
	auto placeholder = title;
	if (min.isDouble() && max.isDouble()) {
		placeholder += u" (%1 – %2)"_q.arg(
			ValueToText(min),
			ValueToText(max));
	} else if (min.isDouble()) {
		placeholder += u" (≥ %1)"_q.arg(ValueToText(min));
	} else if (max.isDouble()) {
		placeholder += u" (≤ %1)"_q.arg(ValueToText(max));
	}
	const auto field = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(placeholder),
			ValueToText(value)),
		RowPadding(st::defaultVerticalListSkip, 0));
	return {
		.value = [=]() -> std::optional<QJsonValue> {
			const auto number = ParseNumber(field->getLastText());
			if (!number
				|| (min.isDouble() && *number < min.toDouble())
				|| (max.isDouble() && *number > max.toDouble())) {
				field->showError();
				return std::nullopt;
			}
			return QJsonValue(integer ? std::round(*number) : *number);
		},
		.set = [=](const QJsonValue &v) { field->setText(ValueToText(v)); },
	};
}

[[nodiscard]] Control AddSelectControl(
		not_null<Ui::GenericBox*> box,
		const QString &title,
		const QJsonObject &entry,
		const QJsonValue &value) {
	const auto options = ParseOptions(entry.value(u"options"_q).toArray());
	const auto index = OptionIndex(options, value);
	const auto dropdown = entry.value(u"style"_q).toString() == u"dropdown"_q
		|| int(options.size()) > kRadioOptionsLimit;
	if (!dropdown) {
		AddTitle(box, title);
		const auto group = (index >= 0)
			? std::make_shared<Ui::RadiobuttonGroup>(index)
			: std::make_shared<Ui::RadiobuttonGroup>();
		for (auto i = 0; i != int(options.size()); ++i) {
			box->addRow(
				object_ptr<Ui::Radiobutton>(
					box,
					group,
					i,
					options[i].title,
					st::defaultBoxCheckbox),
				RowPadding(st::lineWidth * 4, st::lineWidth * 4));
		}
		return {
			.value = [=] {
				const auto i = group->hasValue() ? group->current() : -1;
				return std::make_optional((i >= 0 && i < int(options.size()))
					? options[i].value
					: QJsonValue());
			},
			.set = [=](const QJsonValue &v) {
				const auto i = OptionIndex(options, v);
				if (i >= 0) {
					group->setValue(i);
				}
			},
		};
	}
	const auto current = box->lifetime().make_state<rpl::variable<int>>(
		index);
	const auto button = Settings::AddButtonWithLabel(
		box->verticalLayout(),
		rpl::single(title),
		current->value() | rpl::map([=](int i) {
			return (i >= 0 && i < int(options.size()))
				? options[i].title
				: QString();
		}),
		st::settingsButtonNoIcon);
	const auto menu = button->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();
	button->setClickedCallback([=] {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		for (auto i = 0; i != int(options.size()); ++i) {
			(*menu)->addAction(options[i].title, [=] {
				*current = i;
			}, (i == current->current()) ? &st::menuIconSelect : nullptr);
		}
		(*menu)->popup(QCursor::pos());
	});
	return {
		.value = [=] {
			const auto i = current->current();
			return std::make_optional((i >= 0 && i < int(options.size()))
				? options[i].value
				: QJsonValue());
		},
		.set = [=](const QJsonValue &v) {
			const auto i = OptionIndex(options, v);
			if (i >= 0) {
				*current = i;
			}
		},
	};
}

void ShowColorEditor(
		std::shared_ptr<Ui::Show> show,
		QColor color,
		Fn<void(QColor)> done) {
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Damza::Text("Color", "Цвет")));
		const auto editor = box->addRow(
			object_ptr<ColorEditor>(
				box,
				ColorEditor::Mode::RGBA,
				color.isValid() ? color : QColor(0x87, 0x74, 0xE1)),
			style::margins());
		const auto save = [=] {
			done(editor->color());
			box->closeBox();
		};
		editor->submitRequests(
		) | rpl::on_next(save, editor->lifetime());
		box->setFocusCallback([=] { editor->setInnerFocus(); });
		box->addButton(rpl::single(Damza::Text("Choose", "Выбрать")), save);
		box->addButton(rpl::single(Damza::Text("Cancel", "Отмена")), [=] {
			box->closeBox();
		});
		box->setWidth(editor->width());
	}));
}

[[nodiscard]] Control AddColorControl(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::Show> show,
		const QString &title,
		const QJsonValue &value) {
	const auto row = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		RowPadding(st::defaultVerticalListSkip, 0));
	const auto field = Ui::CreateChild<Ui::InputField>(
		row,
		st::defaultInputField,
		rpl::single(title + u" (#RRGGBB)"_q),
		ValueToText(value));
	const auto swatch = Ui::CreateChild<Ui::AbstractButton>(row);
	const auto size = st::defaultInputField.heightMin - st::lineWidth * 8;
	const auto skip = st::boxRowPadding.left() / 2;
	swatch->resize(size, size);
	swatch->setCursor(style::cur_pointer);
	rpl::combine(
		row->widthValue(),
		field->heightValue()
	) | rpl::on_next([=](int width, int height) {
		field->resizeToWidth(std::max(width - size - skip, 1));
		field->moveToLeft(0, 0, width);
		swatch->moveToLeft(
			width - size,
			std::max(height - size - st::lineWidth * 4, 0),
			width);
		row->resize(width, height);
	}, row->lifetime());
	swatch->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(swatch);
		auto hq = PainterHighQualityEnabler(p);
		const auto color = ParseColor(field->getLastText());
		const auto rect = QRectF(swatch->rect()).marginsRemoved(
			QMarginsF(1.5, 1.5, 1.5, 1.5));
		const auto radius = size / 4.;
		if (color.isValid()) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawRoundedRect(rect, radius, radius);
			p.setBrush(color);
		} else {
			p.setBrush(Qt::NoBrush);
		}
		p.setPen(QPen(st::inputBorderFg, st::lineWidth * 1.5));
		p.drawRoundedRect(rect, radius, radius);
	}, swatch->lifetime());
	field->changes() | rpl::on_next([=] {
		swatch->update();
	}, field->lifetime());
	swatch->setClickedCallback([=] {
		ShowColorEditor(show, ParseColor(field->getLastText()), [=](QColor c) {
			field->setText(ColorToText(c));
			swatch->update();
		});
	});
	return {
		.value = [=]() -> std::optional<QJsonValue> {
			const auto text = field->getLastText().trimmed();
			if (text.isEmpty()) {
				return QJsonValue(QString());
			}
			const auto color = ParseColor(text);
			if (!color.isValid()) {
				field->showError();
				return std::nullopt;
			}
			return QJsonValue(ColorToText(color));
		},
		.set = [=](const QJsonValue &v) {
			field->setText(ValueToText(v));
			swatch->update();
		},
	};
}

[[nodiscard]] QString PluginMeta(const Manifest &manifest) {
	auto parts = QStringList();
	parts.push_back(u"v"_q + manifest.version);
	if (!manifest.author.isEmpty()) {
		parts.push_back(manifest.author);
	}
	parts.push_back(manifest.id);
	return parts.join(u" · "_q);
}

void ShowError(
		std::shared_ptr<Ui::Show> show,
		const QString &title,
		const QString &text) {
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		box->addRow(object_ptr<Ui::FlatLabel>(box, text, st::boxLabel));
		box->addButton(rpl::single(u"OK"_q), [=] { box->closeBox(); });
	}));
}

} // namespace

not_null<const style::FlatLabel*> MakeLabelStyle(
		rpl::lifetime &lifetime,
		const style::FlatLabel &base,
		const style::color &fg) {
	const auto result = lifetime.make_state<style::FlatLabel>(base);
	result->textFg = fg;
	return result;
}

not_null<Ui::RpWidget*> AddButtonsFlow(
		not_null<Ui::VerticalLayout*> container,
		std::vector<FlowButton> buttons,
		const style::margins &padding) {
	const auto result = container->add(
		object_ptr<Ui::RpWidget>(container),
		padding);
	const auto make = [&](const style::RoundButton &base) {
		const auto st = result->lifetime().make_state<style::RoundButton>(
			base);
		st->height = st::defaultActiveButton.height - st::lineWidth * 6;
		st->textTop = st::defaultActiveButton.textTop - st::lineWidth * 3;
		st->width = -st::boxRowPadding.left();
		st->radius = st->height / 2;
		st->style = st::defaultTextStyle;
		return st;
	};
	const auto light = make(st::defaultLightButton);
	const auto active = make(st::defaultActiveButton);
	const auto attention = make(st::defaultLightButton);
	attention->textFg = st::attentionButtonFg;
	attention->textFgOver = st::attentionButtonFgOver;
	attention->textBgOver = st::attentionButtonBgOver;
	attention->ripple.color = st::attentionButtonBgRipple;
	auto widgets = std::vector<not_null<Ui::RoundButton*>>();
	for (auto &button : buttons) {
		const auto st = button.primary
			? active
			: button.attention
			? attention
			: light;
		const auto widget = Ui::CreateChild<Ui::RoundButton>(
			result,
			rpl::single(button.text),
			*st);
		widget->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		widget->setClickedCallback(std::move(button.callback));
		widgets.push_back(widget);
	}
	const auto skip = st::lineWidth * 8;
	result->widthValue() | rpl::on_next([=](int width) {
		auto x = 0;
		auto y = 0;
		auto line = 0;
		for (const auto &widget : widgets) {
			if (x > 0 && x + widget->width() > width) {
				x = 0;
				y += line + skip;
				line = 0;
			}
			widget->moveToLeft(x, y, width);
			x += widget->width() + skip;
			line = std::max(line, widget->height());
		}
		result->resize(width, y + line);
	}, result->lifetime());
	return result;
}

void ShowSettingsBox(std::shared_ptr<Ui::Show> show, const QString &id) {
	const auto state = FindPlugin(id);
	if (!state) {
		return;
	}
	const auto manifest = state->manifest;
	const auto schema = manifest.settings;
	const auto values = Manager::Get().settingsValues(id);
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(manifest.name));
		box->setWidth(st::boxWideWidth);
		const auto muted = MakeLabelStyle(
			box->lifetime(),
			st::boxDividerLabel,
			st::windowSubTextFg);
		box->addRow(
			object_ptr<Ui::FlatLabel>(box, PluginMeta(manifest), *muted),
			RowPadding(0, st::defaultVerticalListSkip));
		if (!manifest.description.isEmpty()) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				manifest.description,
				st::boxLabel));
		}

		const auto controls = box->lifetime().make_state<
			std::vector<Control>>();
		for (const auto &value : schema) {
			const auto entry = value.toObject();
			const auto key = entry.value(u"key"_q).toString();
			if (key.isEmpty()) {
				continue;
			}
			const auto type = entry.value(u"type"_q).toString(u"string"_q);
			const auto title = entry.value(u"title"_q).toString(key);
			const auto current = values.value(key);
			auto control = (type == u"bool"_q || type == u"boolean"_q)
				? AddBoolControl(box, title, current)
				: (type == u"number"_q || type == u"int"_q)
				? AddNumberControl(box, title, entry, current)
				: (type == u"select"_q || type == u"enum"_q)
				? AddSelectControl(box, title, entry, current)
				: (type == u"color"_q)
				? AddColorControl(box, show, title, current)
				: AddTextControl(box, title, current, type == u"text"_q);
			control.key = key;
			control.fallback = entry.value(u"default"_q);
			AddHint(
				box,
				entry.value(u"description"_q).toString(
					entry.value(u"hint"_q).toString()),
				muted);
			controls->push_back(std::move(control));
		}
		if (controls->empty()) {
			box->addRow(
				object_ptr<Ui::FlatLabel>(
					box,
					Damza::Text(
						"This plugin has no settings.",
						"У этого плагина нет настроек."),
					*muted),
				RowPadding(st::defaultVerticalListSkip * 2, 0));
			box->addButton(rpl::single(Damza::Text("Close", "Закрыть")), [=] {
				box->closeBox();
			});
		} else {
			box->addButton(rpl::single(Damza::Text("Save", "Сохранить")), [=] {
				auto changed = std::vector<std::pair<QString, QJsonValue>>();
				const auto now = Manager::Get().settingsValues(id);
				for (const auto &control : *controls) {
					const auto value = control.value();
					if (!value) {
						return;
					} else if (now.value(control.key) != *value) {
						changed.emplace_back(control.key, *value);
					}
				}
				for (const auto &[key, value] : changed) {
					Manager::Get().setSettingValue(id, key, value);
				}
				box->closeBox();
				if (!changed.empty()) {
					show->showToast(Damza::Text(
						"Settings saved",
						"Настройки сохранены"));
				}
			});
			box->addButton(rpl::single(Damza::Text("Cancel", "Отмена")), [=] {
				box->closeBox();
			});
			box->addLeftButton(rpl::single(Damza::Text("Reset", "Сбросить")), [=] {
				for (const auto &control : *controls) {
					control.set(control.fallback);
				}
			});
		}
	}));
}

void ShowLogBox(std::shared_ptr<Ui::Show> show, const QString &id) {
	const auto plugin = Manager::Get().instance(id);
	auto lines = plugin ? plugin->logLines() : QStringList();
	if (lines.size() > kLogLinesInBox) {
		lines = lines.mid(lines.size() - kLogLinesInBox);
	}
	const auto text = lines.join('\n');
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Damza::Text("Log", "Лог") + u": "_q + id));
		box->setWidth(st::boxWideWidth * 3 / 2);
		const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(text.isEmpty()
				? TextWithEntities{ Damza::Text(
					"The log is empty.",
					"Лог пуст.") }
				: Ui::Text::Wrapped({ text }, EntityType::Pre)),
			st::boxLabel));
		label->setSelectable(true);
		box->addButton(rpl::single(Damza::Text("Copy", "Копировать")), [=] {
			QGuiApplication::clipboard()->setText(text);
			show->showToast(Damza::Text("Copied", "Скопировано"));
		});
		box->addButton(rpl::single(Damza::Text("Close", "Закрыть")), [=] {
			box->closeBox();
		});
		box->addLeftButton(rpl::single(u"DevTools"_q), [=] {
			box->closeBox();
			ShowDevTools(id);
		});
	}));
}

void ShowUninstallBox(std::shared_ptr<Ui::Show> show, const QString &id) {
	const auto state = FindPlugin(id);
	if (!state) {
		return;
	}
	const auto name = state->manifest.name;
	const auto builtin = IsBuiltinDirectory(state->manifest.dir);
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(builtin
			? Damza::Text("Built-in plugin", "Встроенный плагин")
			: Damza::Text("Remove plugin", "Удалить плагин")));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			(builtin
				? Damza::Text(
					"\"%1\" ships with DamZaGram and comes back on every "
					"start, so it can't be removed. It will be disabled "
					"instead; turn it on again any time.",
					"«%1» встроен в DamZaGram и возвращается при каждом "
					"запуске, поэтому удалить его нельзя. Вместо этого он "
					"будет выключен — включить можно в любой момент.")
				: Damza::Text(
					"Remove \"%1\"? Its folder will be deleted from disk.",
					"Удалить «%1»? Папка плагина будет удалена с диска."))
				.arg(name),
			st::boxLabel));
		const auto data = builtin
			? nullptr
			: box->addRow(
				object_ptr<Ui::Checkbox>(
					box,
					Damza::Text(
						"Also delete its saved data",
						"Удалить и сохранённые данные"),
					false,
					st::defaultBoxCheckbox),
				RowPadding(st::defaultVerticalListSkip * 2, 0));
		box->addButton(
			rpl::single(builtin
				? Damza::Text("Disable", "Выключить")
				: Damza::Text("Remove", "Удалить")),
			[=] {
				auto error = QString();
				const auto removeData = data && data->checked();
				box->closeBox();
				if (!UninstallPlugin(id, removeData, &error)) {
					show->showToast(error);
				} else {
					show->showToast(builtin
						? Damza::Text("Plugin disabled", "Плагин выключен")
						: Damza::Text("Plugin removed", "Плагин удалён"));
				}
			},
			st::attentionBoxButton);
		box->addButton(rpl::single(Damza::Text("Cancel", "Отмена")), [=] {
			box->closeBox();
		});
	}));
}

void ShowInstallBox(std::shared_ptr<Ui::Show> show, Package package) {
	if (!show->valid()) {
		return;
	} else if (!package.valid()) {
		ShowError(
			show,
			Damza::Text("Can't install the plugin", "Не удалось установить плагин"),
			(package.error.isEmpty()
				? Damza::Text("The package is empty.", "Пакет пуст.")
				: package.error)
				+ (package.source.isEmpty()
					? QString()
					: u"\n\n"_q + package.source));
		return;
	}
	const auto installed = FindPlugin(package.id());
	const auto existing = (installed != nullptr);
	const auto installedVersion = existing
		? installed->manifest.version
		: QString();
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(existing
			? Damza::Text("Update plugin", "Обновление плагина")
			: Damza::Text("Install plugin", "Установка плагина")));
		box->setWidth(st::boxWideWidth);
		const auto muted = MakeLabelStyle(
			box->lifetime(),
			st::boxDividerLabel,
			st::windowSubTextFg);
		const auto warning = MakeLabelStyle(
			box->lifetime(),
			st::boxLabel,
			st::attentionButtonFg);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(Ui::Text::Bold(package.name())),
			st::boxLabel));
		auto meta = QStringList{ u"v"_q + package.version() };
		if (!package.author().isEmpty()) {
			meta.push_back(package.author());
		}
		meta.push_back(package.id());
		box->addRow(
			object_ptr<Ui::FlatLabel>(box, meta.join(u" · "_q), *muted),
			RowPadding(st::lineWidth * 2, st::defaultVerticalListSkip));
		if (!package.description().isEmpty()) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				package.description(),
				st::boxLabel));
		}
		auto details = Damza::Text("Files: %1, %2 KB", "Файлов: %1, %2 КБ")
			.arg(package.files.size())
			.arg(std::max(package.totalSize() / 1024, qint64(1)));
		if (package.generatedManifest) {
			details += u"\n"_q + Damza::Text(
				"Single-file plugin: the manifest is built from its header.",
				"Плагин из одного файла: манифест собран из шапки.");
		}
		if (!package.source.isEmpty()) {
			details += u"\n"_q + Damza::Text("Source", "Источник")
				+ u": "_q + package.source;
		}
		if (existing) {
			details += u"\n"_q + Damza::Text(
				"Installed: v%1, it will be replaced.",
				"Установлена v%1, она будет заменена.").arg(installedVersion);
		}
		box->addRow(
			object_ptr<Ui::FlatLabel>(box, details, *muted),
			RowPadding(st::defaultVerticalListSkip, 0));
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				Damza::Text(
					"Plugins get full access to the client, your account, "
					"messages, files and network. Install only code you "
					"trust.",
					"Плагин получит полный доступ к клиенту, аккаунту, "
					"перепискам, файлам и сети. Устанавливай только тот "
					"код, которому доверяешь."),
				*warning),
			RowPadding(st::defaultVerticalListSkip * 2, 0));
		const auto enable = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				Damza::Text("Enable right away", "Сразу включить"),
				true,
				st::defaultBoxCheckbox),
			RowPadding(st::defaultVerticalListSkip * 2, 0));
		box->addButton(
			rpl::single(existing
				? Damza::Text("Update", "Обновить")
				: Damza::Text("Install", "Установить")),
			[=] {
				const auto result = InstallPackage(package, enable->checked());
				box->closeBox();
				if (!result.error.isEmpty()) {
					ShowError(
						show,
						Damza::Text(
							"Can't install the plugin",
							"Не удалось установить плагин"),
						result.error);
					return;
				}
				const auto state = FindPlugin(result.id);
				const auto failed = state && !state->error.isEmpty();
				show->showToast(failed
					? (Damza::Text(
						"Installed, but failed to start: ",
						"Установлен, но не запустился: ") + state->error)
					: result.updated
					? Damza::Text("Plugin updated", "Плагин обновлён")
					: Damza::Text("Plugin installed", "Плагин установлен"));
			});
		box->addButton(rpl::single(Damza::Text("Cancel", "Отмена")), [=] {
			box->closeBox();
		});
	}));
}

void ShowInstallFromUrlBox(std::shared_ptr<Ui::Show> show) {
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Damza::Text(
			"Install from a link",
			"Установка по ссылке")));
		box->setWidth(st::boxWideWidth);
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(u"https://…/plugin.dzg.zip"_q)));
		const auto muted = MakeLabelStyle(
			box->lifetime(),
			st::boxDividerLabel,
			st::windowSubTextFg);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				Damza::Text(
					"A direct link to a .zip or .js file, a GitHub file "
					"page or a GitHub repository.",
					"Прямая ссылка на .zip или .js, страница файла на "
					"GitHub или целый репозиторий GitHub."),
				*muted),
			RowPadding(st::defaultVerticalListSkip, 0));
		const auto clip = QGuiApplication::clipboard()->text().trimmed();
		if (clip.startsWith(u"http"_q) && !clip.contains('\n')) {
			field->setText(clip);
		}
		box->setFocusCallback([=] { field->setFocusFast(); });
		const auto loading = box->lifetime().make_state<bool>(false);
		const auto submit = [=] {
			const auto url = field->getLastText().trimmed();
			if (url.isEmpty()) {
				field->showError();
				return;
			} else if (*loading) {
				return;
			}
			*loading = true;
			box->showLoading(true);
			const auto weak = QPointer<Ui::GenericBox>(box.get());
			DownloadPackage(url, [=](Package package) {
				if (const auto strong = weak.data()) {
					strong->closeBox();
				}
				ShowInstallBox(show, std::move(package));
			});
		};
		field->submits() | rpl::on_next(submit, field->lifetime());
		box->addButton(rpl::single(Damza::Text("Download", "Скачать")), submit);
		box->addButton(rpl::single(Damza::Text("Cancel", "Отмена")), [=] {
			box->closeBox();
		});
	}));
}

void InstallFromPath(std::shared_ptr<Ui::Show> show, const QString &path) {
	ShowInstallBox(show, ReadPackageFromPath(path));
}

void ChooseFileAndInstall(std::shared_ptr<Ui::Show> show, QWidget *parent) {
	FileDialog::GetOpenPath(
		parent,
		Damza::Text("Choose a plugin", "Выбери плагин"),
		u"DamZaGram plugins (*.zip *.js *.mjs);;"_q
			+ FileDialog::AllFilesFilter(),
		[=](FileDialog::OpenResult &&result) {
			if (!result.paths.isEmpty()) {
				InstallFromPath(show, result.paths.front());
			} else if (!result.remoteContent.isEmpty()) {
				ShowInstallBox(
					show,
					ReadPackageFromBytes(result.remoteContent, QString()));
			}
		});
}

void ChooseFolderAndInstall(std::shared_ptr<Ui::Show> show, QWidget *parent) {
	FileDialog::GetFolder(
		parent,
		Damza::Text("Choose a plugin folder", "Выбери папку плагина"),
		QString(),
		[=](QString &&path) {
			if (!path.isEmpty()) {
				InstallFromPath(show, path);
			}
		});
}

void ShowCreateBox(std::shared_ptr<Ui::Show> show, const QString &author) {
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Damza::Text("New plugin", "Новый плагин")));
		box->setWidth(st::boxWideWidth);
		const auto name = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Damza::Text("Name", "Название"))));
		const auto id = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Damza::Text(
				"ID (folder name, latin)",
				"ID (имя папки, латиницей)"))),
			RowPadding(st::defaultVerticalListSkip, 0));
		const auto description = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			rpl::single(Damza::Text("Description", "Описание")),
			QString()),
			RowPadding(st::defaultVerticalListSkip, 0));
		const auto authorField = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Damza::Text("Author", "Автор")),
			author),
			RowPadding(st::defaultVerticalListSkip, 0));
		const auto muted = MakeLabelStyle(
			box->lifetime(),
			st::boxDividerLabel,
			st::windowSubTextFg);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				Damza::Text(
					"Creates tdata/plugins/<id>/ with manifest.json, "
					"main.js (an ES module with examples), jsconfig.json "
					"and API types for VS Code hints, enables the plugin "
					"and opens its folder. Saved changes reload it on "
					"the fly.",
					"Будет создана папка tdata/plugins/<id>/ с "
					"manifest.json, main.js (ES-модуль с примерами), "
					"jsconfig.json и типами API для подсказок в VS Code. "
					"Плагин сразу включится, а его папка откроется. "
					"Сохранённые изменения подхватываются на лету."),
				*muted),
			RowPadding(st::defaultVerticalListSkip * 2, 0));
		const auto idEdited = box->lifetime().make_state<bool>(false);
		name->changes() | rpl::on_next([=] {
			if (!*idEdited) {
				const auto text = name->getLastText().trimmed();
				id->setText(text.isEmpty() ? QString() : SanitizePluginId(text));
			}
		}, name->lifetime());
		id->changes() | rpl::on_next([=] {
			if (id->hasFocus()) {
				*idEdited = true;
			}
		}, id->lifetime());
		box->setFocusCallback([=] { name->setFocusFast(); });
		const auto submit = [=] {
			const auto value = id->getLastText().trimmed();
			if (!IsValidPluginId(value)) {
				id->showError();
				return;
			} else if (FindPlugin(value)) {
				id->showError();
				show->showToast(Damza::Text(
					"A plugin with this ID already exists.",
					"Плагин с таким ID уже есть."));
				return;
			}
			const auto result = CreatePlugin({
				.id = value,
				.name = name->getLastText().trimmed(),
				.description = description->getLastText().trimmed(),
				.author = authorField->getLastText().trimmed(),
			});
			if (!result.error.isEmpty()) {
				show->showToast(result.error);
				return;
			}
			box->closeBox();
			show->showToast(Damza::Text(
				"Plugin created. Edit main.js — it reloads on save.",
				"Плагин создан. Редактируй main.js — он перезагрузится "
				"при сохранении."));
			if (const auto state = FindPlugin(result.id)) {
				RevealInFolder(state->manifest.dir);
			}
		};
		name->submits() | rpl::on_next([=] { id->setFocusFast(); }, name->lifetime());
		id->submits() | rpl::on_next(submit, id->lifetime());
		box->addButton(rpl::single(Damza::Text("Create", "Создать")), submit);
		box->addButton(rpl::single(Damza::Text("Cancel", "Отмена")), [=] {
			box->closeBox();
		});
	}));
}

bool OpenPluginFile(
		Window::SessionController *controller,
		const QString &fileName,
		const QString &path) {
	if (!controller
		|| path.isEmpty()
		|| !Manager::Exists()
		|| !IsPluginFileName(fileName)) {
		return false;
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	} else if (file.size() > kMaxPluginFileSize) {
		ShowInstallBox(controller->uiShow(), Package{
			.source = fileName,
			.error = Damza::Text("The file is too large.", "Файл слишком большой."),
		});
		return true;
	}
	ShowInstallBox(
		controller->uiShow(),
		ReadPackageFromBytes(file.readAll(), fileName));
	return true;
}

void ShowPluginsSection(not_null<Window::SessionController*> controller) {
	controller->showSettings(Settings::DamzaPlugins::Id());
}

void AddMainMenuEntry(
		not_null<Ui::VerticalLayout*> menu,
		not_null<Window::SessionController*> controller) {
	Settings::AddButtonWithIcon(
		menu,
		rpl::single(Damza::Text("Plugins", "Плагины")),
		st::mainMenuButton,
		{ &st::menuIconBotCommands }
	)->setClickedCallback([=] {
		ShowPluginsSection(controller);
	});
}

} // namespace Damza::PluginsUi
