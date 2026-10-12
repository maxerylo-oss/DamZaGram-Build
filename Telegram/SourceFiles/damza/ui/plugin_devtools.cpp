// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/ui/plugin_devtools.h"

#include "base/timer.h"
#include "base/unique_qptr.h"
#include "damza/damza_lang.h"
#include "damza/plugins/plugin_install.h"
#include "damza/plugins/plugin_instance.h"
#include "damza/plugins/plugin_manager.h"
#include "damza/ui/plugin_boxes.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/separate_panel.h"
#include "ui/wrap/vertical_layout.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Damza::PluginsUi {
namespace {

using namespace Damza::Plugins;

constexpr auto kLogRefresh = crl::time(400);
constexpr auto kLogTailLines = 200;
constexpr auto kMaxEntries = 200;

[[nodiscard]] QString JsString(const QString &text) {
	const auto json = QJsonDocument(QJsonArray{ text }).toJson(
		QJsonDocument::Compact);
	return QString::fromUtf8(json.mid(1, json.size() - 2));
}

class DevTools final : public Ui::RpWidget {
public:
	DevTools(QWidget *parent, const QString &pluginId);

	void setPlugin(const QString &id);
	void focusInput();

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	not_null<Ui::RoundButton*> makeButton(
		rpl::producer<QString> text,
		Fn<void()> callback,
		bool primary = false);
	void run();
	void addEntry(const QString &code, const QString &result, bool error);
	void refreshLog(bool force);
	void showPluginMenu();
	void layout();
	void scrollOutputToBottom();

	const not_null<const style::FlatLabel*> _errorSt;
	const not_null<const style::FlatLabel*> _mutedSt;
	QString _pluginId;
	rpl::variable<QString> _pluginTitle;
	std::vector<std::unique_ptr<style::RoundButton>> _styles;
	std::vector<not_null<Ui::RoundButton*>> _buttons;
	Ui::RoundButton *_pluginButton = nullptr;
	object_ptr<Ui::ScrollArea> _outputScroll;
	QPointer<Ui::VerticalLayout> _output;
	object_ptr<Ui::FlatLabel> _logTitle;
	object_ptr<Ui::ScrollArea> _logScroll;
	QPointer<Ui::FlatLabel> _log;
	object_ptr<Ui::InputField> _input;
	object_ptr<Ui::FlatLabel> _hint;
	base::Timer _logTimer;
	base::unique_qptr<Ui::PopupMenu> _menu;
	std::vector<QString> _history;
	int _logSize = -1;
	QString _logFirst;
	QString _logLast;
	int _entries = 0;

};

DevTools::DevTools(QWidget *parent, const QString &pluginId)
: RpWidget(parent)
, _errorSt(MakeLabelStyle(lifetime(), st::boxLabel, st::attentionButtonFg))
, _mutedSt(MakeLabelStyle(
	lifetime(),
	st::boxDividerLabel,
	st::windowSubTextFg))
, _outputScroll(this, st::boxScroll)
, _logTitle(this, QString(), st::defaultSubsectionTitle)
, _logScroll(this, st::boxScroll)
, _input(
	this,
	st::defaultInputField,
	Ui::InputField::Mode::MultiLine,
	rpl::single(Damza::Text(
		"JavaScript: dzg.chats.current(), await dzg.http.fetch(…)",
		"JavaScript: dzg.chats.current(), await dzg.http.fetch(…)")),
	QString())
, _hint(
	this,
	Damza::Text(
		"Ctrl+Enter — run. Code runs in the plugin's global scope; "
		"module variables are not visible, put them on globalThis.",
		"Ctrl+Enter — выполнить. Код выполняется в глобальной области "
		"плагина: переменные модуля не видны, выноси их в globalThis."),
	*_mutedSt)
, _logTimer([=] { refreshLog(false); }) {
	_output = _outputScroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(_outputScroll.data()));
	_log = _logScroll->setOwnedWidget(
		object_ptr<Ui::FlatLabel>(_logScroll.data(), QString(), st::boxLabel));
	_log->setSelectable(true);

	_pluginButton = makeButton(_pluginTitle.value(), [=] {
		showPluginMenu();
	}, true);
	makeButton(rpl::single(Damza::Text("Run", "Выполнить")), [=] { run(); });
	makeButton(rpl::single(Damza::Text("Clear", "Очистить")), [=] {
		if (_output) {
			_output->clear();
		}
		_entries = 0;
		layout();
	});
	makeButton(rpl::single(Damza::Text("Reload plugin", "Перезагрузить")), [=] {
		if (!_pluginId.isEmpty() && Manager::Exists()) {
			Manager::Get().reload(_pluginId);
			addEntry(
				QString(),
				Damza::Text("Plugin reloaded.", "Плагин перезагружен."),
				false);
		}
	});
	makeButton(rpl::single(Damza::Text("Copy log", "Скопировать лог")), [=] {
		const auto plugin = Manager::Exists()
			? Manager::Get().instance(_pluginId)
			: nullptr;
		if (plugin) {
			QGuiApplication::clipboard()->setText(plugin->logLines().join('\n'));
		}
	});
	makeButton(rpl::single(Damza::Text("Folder", "Папка")), [=] {
		if (const auto state = FindPlugin(_pluginId)) {
			RevealInFolder(state->manifest.dir);
		}
	});

	_input->setSubmitSettings(Ui::InputField::SubmitSettings::CtrlEnter);
	_input->setMaxHeight(st::boxWideWidth / 2);
	_input->submits() | rpl::on_next([=] { run(); }, _input->lifetime());
	_input->heightValue() | rpl::skip(1) | rpl::on_next([=] {
		layout();
	}, _input->lifetime());

	setPlugin(pluginId);
	_logTimer.callEach(kLogRefresh);

	if (Manager::Exists()) {
		Manager::Get().pluginsChanges() | rpl::on_next([=] {
			crl::on_main(this, [=] { setPlugin(_pluginId); });
		}, lifetime());
	}
	addEntry(
		QString(),
		Damza::Text(
			"DamZaGram DevTools. Pick a plugin above (or use the scratchpad) "
			"and type any JavaScript: the whole dzg API is available.",
			"DamZaGram DevTools. Выбери плагин сверху (или песочницу) и "
			"пиши любой JavaScript: доступен весь API dzg."),
		false);
}

not_null<Ui::RoundButton*> DevTools::makeButton(
		rpl::producer<QString> text,
		Fn<void()> callback,
		bool primary) {
	auto st = std::make_unique<style::RoundButton>(primary
		? st::defaultActiveButton
		: st::defaultLightButton);
	st->height = st::defaultActiveButton.height - st::lineWidth * 6;
	st->textTop = st::defaultActiveButton.textTop - st::lineWidth * 3;
	st->width = -st::boxRowPadding.left();
	st->radius = st->height / 2;
	st->style = st::defaultTextStyle;
	const auto button = Ui::CreateChild<Ui::RoundButton>(
		this,
		std::move(text),
		*st);
	_styles.push_back(std::move(st));
	button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	button->setClickedCallback(std::move(callback));
	button->widthValue() | rpl::skip(1) | rpl::on_next([=] {
		layout();
	}, button->lifetime());
	_buttons.push_back(button);
	return button;
}

void DevTools::setPlugin(const QString &id) {
	_pluginId = id;
	if (_pluginId.isEmpty() && Manager::Exists()) {
		const auto ids = Manager::Get().loadedIds();
		const auto scratch = QString::fromLatin1(kScratchPluginId);
		if (ranges::contains(ids, scratch)) {
			_pluginId = scratch;
		}
	}
	const auto loaded = Manager::Exists()
		&& !_pluginId.isEmpty()
		&& Manager::Get().instance(_pluginId);
	_pluginTitle = (_pluginId.isEmpty()
		? Damza::Text("Scratchpad", "Песочница")
		: _pluginId) + (loaded ? QString() : Damza::Text(
			" (not loaded)",
			" (не загружен)")) + u" ▾"_q;
	_logTitle->setText(Damza::Text("Log", "Лог")
		+ (_pluginId.isEmpty() ? QString() : u": "_q + _pluginId));
	refreshLog(true);
}

void DevTools::focusInput() {
	_input->setFocusFast();
}

void DevTools::showPluginMenu() {
	if (!Manager::Exists()) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		_pluginButton,
		st::popupMenuWithIcons);
	for (const auto &state : Manager::Get().plugins()) {
		const auto id = state.manifest.id;
		if (!state.loaded) {
			continue;
		}
		_menu->addAction(
			state.manifest.name + u"  ("_q + id + u")"_q,
			[=] { setPlugin(id); },
			(id == _pluginId) ? &st::menuIconSelect : &st::menuIconBotCommands);
	}
	_menu->addSeparator();
	_menu->addAction(
		Damza::Text("Scratchpad (devtools)", "Песочница (devtools)"),
		[=] {
			auto error = QString();
			const auto id = EnsureScratchPlugin(&error);
			if (id.isEmpty()) {
				addEntry(QString(), error, true);
			} else {
				setPlugin(id);
			}
		},
		&st::menuIconEdit);
	_menu->popup(QCursor::pos());
}

void DevTools::run() {
	const auto code = _input->getLastText();
	if (code.trimmed().isEmpty() || !Manager::Exists()) {
		return;
	}
	auto id = _pluginId;
	if (id.isEmpty() || !Manager::Get().instance(id)) {
		auto error = QString();
		id = EnsureScratchPlugin(&error);
		if (id.isEmpty()) {
			addEntry(code, error, true);
			return;
		}
		setPlugin(id);
	}
	const auto plugin = Manager::Get().instance(id);
	if (!plugin) {
		addEntry(code, Damza::Text("Plugin is not loaded.", "Плагин не загружен."), true);
		return;
	} else if (plugin->busy()) {
		addEntry(code, Damza::Text("Plugin is busy.", "Плагин занят."), true);
		return;
	}
	auto error = QString();
	const auto result = plugin->evaluate(WrapConsoleCode(code), &error);
	addEntry(code, error.isEmpty() ? result : error, !error.isEmpty());
	if (_history.empty() || _history.back() != code) {
		_history.push_back(code);
	}
	_input->clear();
	refreshLog(false);
}

void DevTools::addEntry(
		const QString &code,
		const QString &result,
		bool error) {
	if (!_output) {
		return;
	}
	if (++_entries > kMaxEntries) {
		_output->clear();
		_entries = 1;
	}
	const auto padding = style::margins(
		st::boxRowPadding.left() / 2,
		st::defaultVerticalListSkip,
		st::boxRowPadding.right() / 2,
		0);
	if (!code.isEmpty()) {
		const auto label = _output->add(
			object_ptr<Ui::FlatLabel>(
				_output,
				rpl::single(Ui::Text::Wrapped(
					{ code },
					EntityType::Pre)),
				st::boxLabel),
			padding);
		label->setSelectable(true);
	}
	const auto label = _output->add(
		object_ptr<Ui::FlatLabel>(
			_output,
			(code.isEmpty() ? QString() : (error ? u"✕ "_q : u"← "_q))
				+ (result.isEmpty() ? u"undefined"_q : result),
			error ? *_errorSt : code.isEmpty() ? *_mutedSt : st::boxLabel),
		padding);
	label->setSelectable(true);
	layout();
	scrollOutputToBottom();
}

void DevTools::scrollOutputToBottom() {
	crl::on_main(this, [=] {
		if (_output) {
			_outputScroll->scrollToY(_output->height());
		}
	});
}

void DevTools::refreshLog(bool force) {
	if (!_log) {
		return;
	}
	const auto plugin = (Manager::Exists() && !_pluginId.isEmpty())
		? Manager::Get().instance(_pluginId)
		: nullptr;
	const auto lines = plugin ? plugin->logLines() : QStringList();
	const auto first = lines.isEmpty() ? QString() : lines.front();
	const auto last = lines.isEmpty() ? QString() : lines.back();
	if (!force
		&& lines.size() == _logSize
		&& first == _logFirst
		&& last == _logLast) {
		return;
	}
	_logSize = lines.size();
	_logFirst = first;
	_logLast = last;
	const auto atBottom = (_logScroll->scrollTop() + _logScroll->height()
		>= _log->height() - st::lineWidth * 4);
	const auto tail = (lines.size() > kLogTailLines)
		? lines.mid(lines.size() - kLogTailLines)
		: lines;
	_log->setMarkedText(tail.isEmpty()
		? TextWithEntities{ plugin
			? Damza::Text("The log is empty.", "Лог пуст.")
			: Damza::Text("No plugin selected.", "Плагин не выбран.") }
		: Ui::Text::Wrapped({ tail.join('\n') }, EntityType::Pre));
	_log->resizeToWidth(std::max(_logScroll->width() - st::boxRowPadding.left(), 1));
	if (atBottom || force) {
		crl::on_main(this, [=] {
			if (_log) {
				_logScroll->scrollToY(_log->height());
			}
		});
	}
}

void DevTools::resizeEvent(QResizeEvent *e) {
	layout();
}

void DevTools::layout() {
	const auto w = width();
	const auto h = height();
	if (w <= 0 || h <= 0) {
		return;
	}
	const auto pad = st::boxRowPadding.left() / 2;
	const auto skip = st::lineWidth * 8;

	auto x = pad;
	auto y = pad;
	auto line = 0;
	for (const auto &button : _buttons) {
		if (x > pad && x + button->width() > w - pad) {
			x = pad;
			y += line + skip;
			line = 0;
		}
		button->moveToLeft(x, y, w);
		x += button->width() + skip;
		line = std::max(line, button->height());
	}
	const auto top = y + line + pad;

	const auto wide = (w >= st::boxWideWidth * 2);
	const auto leftWidth = wide ? (w * 3 / 5) : w;
	_hint->resizeToWidth(leftWidth - 2 * pad);
	_input->resizeToWidth(leftWidth - 2 * pad);
	const auto bottom = h - pad;
	const auto hintTop = bottom - _hint->height();
	const auto inputTop = hintTop - skip - _input->height();
	_hint->moveToLeft(pad, hintTop, w);
	_input->moveToLeft(pad, inputTop, w);

	auto outputBottom = inputTop - skip;
	auto logLeft = 0;
	auto logTop = 0;
	auto logWidth = 0;
	auto logBottom = 0;
	if (wide) {
		logLeft = leftWidth;
		logTop = top;
		logWidth = w - leftWidth - pad;
		logBottom = bottom;
	} else {
		const auto available = outputBottom - top;
		const auto logHeight = available * 2 / 5;
		logLeft = pad;
		logWidth = w - 2 * pad;
		logBottom = outputBottom;
		logTop = outputBottom - logHeight;
		outputBottom = logTop - skip;
	}
	_outputScroll->setGeometry(
		pad,
		top,
		leftWidth - 2 * pad,
		std::max(outputBottom - top, 1));
	if (_output) {
		_output->resizeToWidth(_outputScroll->width());
	}
	_logTitle->resizeToWidth(logWidth);
	_logTitle->moveToLeft(logLeft, logTop, w);
	const auto logScrollTop = logTop + _logTitle->height() + skip / 2;
	_logScroll->setGeometry(
		logLeft,
		logScrollTop,
		std::max(logWidth, 1),
		std::max(logBottom - logScrollTop, 1));
	if (_log) {
		_log->resizeToWidth(std::max(logWidth - st::boxRowPadding.left(), 1));
	}
}

QPointer<Ui::SeparatePanel> Panel;
QPointer<DevTools> Content;

} // namespace

QString WrapConsoleCode(const QString &code) {
	if (!code.contains(u"await"_q)) {
		return code;
	}
	return uR"((() => {
	const source = %1;
	const AsyncFunction = (async () => {}).constructor;
	let run;
	try {
		run = new AsyncFunction('return (' + source + '\n);');
	} catch (e) {
		run = new AsyncFunction(source);
	}
	run().then(
		(value) => console.log('[devtools] =>', value),
		(e) => console.warn('[devtools] x', (e && e.stack) ? (e + '\n' + e.stack) : String(e)));
	return 'Promise: the result will appear in the log';
})())"_q.arg(JsString(code));
}

void ShowDevTools(const QString &pluginId) {
	if (Panel && Content) {
		if (!pluginId.isEmpty()) {
			Content->setPlugin(pluginId);
		}
		Panel->showAndActivate();
		Content->focusInput();
		return;
	}
	const auto panel = new Ui::SeparatePanel(Ui::SeparatePanelArgs());
	Panel = panel;
	panel->setTitle(rpl::single(u"DamZaGram DevTools"_q));
	panel->setInnerSize(
		QSize(st::boxWideWidth * 5 / 2, st::boxWideWidth * 3 / 2),
		true);
	auto content = base::make_unique_q<DevTools>(panel, pluginId);
	Content = content.get();
	panel->showInner(std::move(content));
	panel->closeRequests() | rpl::on_next([=] {
		panel->hideGetDuration();
	}, panel->lifetime());
	panel->closeEvents() | rpl::on_next([=] {
		panel->deleteLater();
	}, panel->lifetime());
	panel->showAndActivate();
	if (Content) {
		Content->focusInput();
	}
}

} // namespace Damza::PluginsUi
