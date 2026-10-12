// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/plugins/plugin_bridge.h"

#include "api/api_common.h"
#include "api/api_editing.h"
#include "apiwrap.h"
#include "core/application.h"
#include "core/version.h"
#include "damza/plugins/plugin_data.h"
#include "damza/plugins/plugin_instance.h"
#include "damza/plugins/plugin_manager.h"
#include "damza/plugins/plugin_widgets.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"
#include "ui/layers/generic_box.h"
#include "ui/style/style_core_palette.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QSysInfo>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtWidgets/QApplication>

namespace Damza::Plugins {
namespace {

std::optional<QByteArray> SavedPalette;

[[nodiscard]] QVariant JsonToVariant(const QJsonValue &value) {
	return value.toVariant();
}

[[nodiscard]] QJsonValue VariantToJson(const QVariant &value) {
	return QJsonValue::fromVariant(value);
}

void ApplyWidgetGeometry(not_null<QWidget*> widget, const QVariantMap &props) {
	const auto has = [&](const char *key) {
		return props.contains(QString::fromLatin1(key));
	};
	const auto get = [&](const char *key, int fallback) {
		return has(key)
			? props.value(QString::fromLatin1(key)).toInt()
			: fallback;
	};
	if (has("width") || has("height")) {
		widget->resize(
			get("width", widget->width()),
			get("height", widget->height()));
	}
	if (has("x") || has("y")) {
		widget->move(get("x", widget->x()), get("y", widget->y()));
	}
}

// A plain colored rectangle plugins can use as a container.
class Panel final : public Ui::RpWidget {
public:
	Panel(QWidget *parent, QColor color, int radius)
	: RpWidget(parent)
	, _color(color)
	, _radius(radius) {
		setAttribute(Qt::WA_TranslucentBackground);
	}

	void setColor(QColor color) {
		_color = color;
		update();
	}

	Fn<void()> clicked;

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(_color);
		p.drawRoundedRect(rect(), _radius, _radius);
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		if (clicked && rect().contains(e->pos())) {
			clicked();
		}
	}

private:
	QColor _color;
	int _radius = 0;

};

} // namespace

Bridge::Bridge(not_null<Instance*> instance)
: _instance(instance) {
}

Bridge::~Bridge() {
	for (const auto &widget : base::take(_created)) {
		delete widget.data();
	}
	if (_themeChanged) {
		resetTheme();
	}
	for (const auto &[id, timer] : _timers) {
		delete timer;
	}
	const auto requests = base::take(_requests);
	for (const auto &[id, reply] : requests) {
		if (reply) {
			QObject::disconnect(reply, nullptr, this, nullptr);
			reply->abort();
			reply->deleteLater();
		}
	}
}

std::vector<Js::NativeMethod> Bridge::methods() {
	using Args = QVariantList;
	const auto str = [](const Args &a, int i) { return a.value(i).toString(); };
	const auto num = [](const Args &a, int i) { return a.value(i).toDouble(); };
	const auto map = [](const Args &a, int i) { return a.value(i).toMap(); };
	const auto none = QVariant();
	return {
		{ "log", [=](const Args &a) { log(str(a, 0), str(a, 1)); return none; } },
		{ "setHooks", [=](const Args &a) {
			setHooks(a.value(0).toBool(), a.value(1).toBool());
			return none;
		} },
		{ "pluginInfo", [=](const Args &) { return QVariant(pluginInfo()); } },
		{ "appInfo", [=](const Args &) { return QVariant(appInfo()); } },
		{ "readPluginFile", [=](const Args &a) { return readPluginFile(str(a, 0)); } },
		{ "startTimer", [=](const Args &a) {
			return QVariant(startTimer(int(num(a, 0)), a.value(1).toBool()));
		} },
		{ "stopTimer", [=](const Args &a) { stopTimer(int(num(a, 0))); return none; } },
		{ "storageGet", [=](const Args &a) { return storageGet(str(a, 0)); } },
		{ "storageSet", [=](const Args &a) { storageSet(str(a, 0), a.value(1)); return none; } },
		{ "storageRemove", [=](const Args &a) { storageRemove(str(a, 0)); return none; } },
		{ "storageKeys", [=](const Args &) { return QVariant(storageKeys()); } },
		{ "settings", [=](const Args &) { return QVariant(settings()); } },
		{ "setSetting", [=](const Args &a) { setSetting(str(a, 0), a.value(1)); return none; } },
		{ "toast", [=](const Args &a) { toast(str(a, 0), str(a, 1)); return none; } },
		{ "showBox", [=](const Args &a) { return QVariant(showBox(map(a, 0))); } },
		{ "mainWindow", [=](const Args &) { return mainWindow(); } },
		{ "focusedWidget", [=](const Args &) { return focusedWidget(); } },
		{ "findWidgets", [=](const Args &a) { return findWidgets(str(a, 0), str(a, 1)); } },
		{ "createWidget", [=](const Args &a) {
			return createWidget(int(num(a, 0)), str(a, 1), map(a, 2));
		} },
		{ "widgetCall", [=](const Args &a) {
			return widgetCall(int(num(a, 0)), str(a, 1), a.value(2).toList());
		} },
		{ "repaintAll", [=](const Args &) { repaintAll(); return none; } },
		{ "setThemeColor", [=](const Args &a) {
			return QVariant(setThemeColor(str(a, 0), str(a, 1)));
		} },
		{ "resetTheme", [=](const Args &) { resetTheme(); return none; } },
		{ "currentChat", [=](const Args &) { return currentChat(); } },
		{ "getPeer", [=](const Args &a) { return getPeer(str(a, 0)); } },
		{ "findPeer", [=](const Args &a) { return findPeer(str(a, 0)); } },
		{ "openChat", [=](const Args &a) {
			return QVariant(openChat(str(a, 0), num(a, 1)));
		} },
		{ "getMessage", [=](const Args &a) { return getMessage(str(a, 0), num(a, 1)); } },
		{ "loadedMessages", [=](const Args &a) {
			return QVariant(loadedMessages(str(a, 0), int(num(a, 1))));
		} },
		{ "sendMessage", [=](const Args &a) {
			return QVariant(sendMessage(str(a, 0), str(a, 1), map(a, 2)));
		} },
		{ "editMessage", [=](const Args &a) {
			return QVariant(editMessage(str(a, 0), num(a, 1), str(a, 2)));
		} },
		{ "deleteMessages", [=](const Args &a) {
			return QVariant(deleteMessages(
				str(a, 0),
				a.value(1).toList(),
				a.value(2).toBool()));
		} },
		{ "refilter", [=](const Args &) { refilter(); return none; } },
		{ "httpRequest", [=](const Args &a) { return QVariant(httpRequest(map(a, 0))); } },
		{ "openUrl", [=](const Args &a) { openUrl(str(a, 0)); return none; } },
		{ "clipboardText", [=](const Args &) { return QVariant(clipboardText()); } },
		{ "setClipboardText", [=](const Args &a) { setClipboardText(str(a, 0)); return none; } },
		{ "readFile", [=](const Args &a) { return readFile(str(a, 0)); } },
		{ "writeFile", [=](const Args &a) {
			return QVariant(writeFile(str(a, 0), str(a, 1)));
		} },
		{ "listDir", [=](const Args &a) { return QVariant(listDir(str(a, 0))); } },
		{ "fileExists", [=](const Args &a) { return QVariant(fileExists(str(a, 0))); } },
	};
}

not_null<Instance*> Bridge::instance() const {
	return _instance;
}

int Bridge::nextRequestId() {
	return ++_nextRequestId;
}

void Bridge::settle(int id, bool ok, const QVariant &value) {
	_instance->dispatch(u"settle"_q, QVariant(QVariantMap{
		{ u"id"_q, id },
		{ u"ok"_q, ok },
		{ u"value"_q, value },
	}));
}

void Bridge::log(const QString &level, const QString &text) {
	_instance->log(level, text);
}

void Bridge::setHooks(bool filter, bool beforeSend) {
	const auto filterChanged = (_instance->hasFilter() != filter);
	_instance->setHasFilter(filter);
	_instance->setHasSendHook(beforeSend);
	if (filterChanged) {
		Manager::Get().invalidateFilters();
	}
}

QVariantMap Bridge::pluginInfo() const {
	const auto &manifest = _instance->manifest();
	return {
		{ u"id"_q, manifest.id },
		{ u"name"_q, manifest.name },
		{ u"version"_q, manifest.version },
		{ u"author"_q, manifest.author },
		{ u"description"_q, manifest.description },
		{ u"dir"_q, manifest.dir },
		{ u"dataDir"_q, Manager::Get().dataPath() },
	};
}

QVariantMap Bridge::appInfo() const {
	return {
		{ u"name"_q, u"DamZaGram"_q },
		{ u"version"_q, QString::fromLatin1(AppVersionStr) },
		{ u"apiVersion"_q, 1 },
		{ u"platform"_q, QSysInfo::productType() },
		{ u"os"_q, QSysInfo::prettyProductName() },
		{ u"pluginsPath"_q, Manager::Get().pluginsPath() },
		{ u"devMode"_q, Manager::Get().devMode() },
	};
}

QVariant Bridge::readPluginFile(const QString &relativePath) const {
	const auto root = QDir(_instance->manifest().dir);
	const auto path = QDir::cleanPath(root.filePath(relativePath));
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QVariant();
	}
	return QString::fromUtf8(file.readAll());
}

int Bridge::startTimer(int ms, bool repeat) {
	const auto id = ++_nextTimerId;
	const auto timer = new QTimer();
	timer->setSingleShot(!repeat);
	timer->setInterval(ms);
	QObject::connect(timer, &QTimer::timeout, this, [=] {
		if (!repeat) {
			_timers.erase(id);
			timer->deleteLater();
		}
		_instance->dispatch(u"timer"_q, QVariant(id));
	});
	_timers.emplace(id, timer);
	timer->start();
	return id;
}

void Bridge::stopTimer(int id) {
	const auto i = _timers.find(id);
	if (i != end(_timers)) {
		i->second->stop();
		i->second->deleteLater();
		_timers.erase(i);
	}
}

QVariant Bridge::storageGet(const QString &key) const {
	const auto &storage = _instance->storage();
	const auto i = storage.find(key);
	return (i == storage.end()) ? QVariant() : JsonToVariant(*i);
}

void Bridge::storageSet(const QString &key, const QVariant &value) {
	if (!value.isValid()) {
		storageRemove(key);
		return;
	}
	_instance->storage().insert(key, VariantToJson(value));
	_instance->saveStorage();
}

void Bridge::storageRemove(const QString &key) {
	_instance->storage().remove(key);
	_instance->saveStorage();
}

QStringList Bridge::storageKeys() const {
	return _instance->storage().keys();
}

QVariantMap Bridge::settings() const {
	return Manager::Get().settingsValues(_instance->manifest().id)
		.toVariantMap();
}

void Bridge::setSetting(const QString &key, const QVariant &value) {
	Manager::Get().setSettingValue(
		_instance->manifest().id,
		key,
		VariantToJson(value));
}

void Bridge::toast(const QString &text, const QString &title) {
	Ui::Toast::Show(Ui::Toast::Config{
		.title = title,
		.text = { text },
	});
}

int Bridge::showBox(const QVariantMap &spec) {
	const auto id = nextRequestId();
	const auto window = Manager::Get().activeWindow();
	const auto weak = QPointer<Bridge>(this);
	const auto settle = [=](QString button, QVariantMap values) {
		if (!weak) {
			return;
		}
		_instance->dispatch(u"settle"_q, QVariant(QVariantMap{
			{ u"id"_q, id },
			{ u"ok"_q, true },
			{ u"value"_q, QVariantMap{
				{ u"button"_q, button },
				{ u"values"_q, values },
			} },
		}));
	};
	if (!window) {
		// The caller registers the promise after we return.
		QTimer::singleShot(0, this, [=] { settle(u"cancel"_q, {}); });
		return id;
	}
	window->show(Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			bool done = false;
			std::vector<std::pair<QString, Ui::InputField*>> inputs;
			std::vector<std::pair<QString, Ui::Checkbox*>> toggles;
		};
		const auto state = box->lifetime().make_state<State>();
		const auto collect = [=] {
			auto values = QVariantMap();
			for (const auto &[key, field] : state->inputs) {
				values.insert(key, field->getLastText());
			}
			for (const auto &[key, check] : state->toggles) {
				values.insert(key, check->checked());
			}
			return values;
		};
		const auto finish = [=](QString button) {
			if (state->done) {
				return;
			}
			state->done = true;
			settle(button, collect());
		};

		const auto title = spec.value(u"title"_q).toString();
		if (!title.isEmpty()) {
			box->setTitle(rpl::single(title));
		}
		const auto text = spec.value(u"text"_q).toString();
		if (!text.isEmpty()) {
			box->addRow(object_ptr<Ui::FlatLabel>(box, text, st::boxLabel));
		}
		for (const auto &entry : spec.value(u"inputs"_q).toList()) {
			const auto input = entry.toMap();
			const auto multiline = input.value(u"multiline"_q).toBool();
			const auto field = box->addRow(object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				(multiline
					? Ui::InputField::Mode::MultiLine
					: Ui::InputField::Mode::SingleLine),
				rpl::single(input.value(u"placeholder"_q).toString()),
				input.value(u"value"_q).toString()));
			state->inputs.emplace_back(input.value(u"id"_q).toString(), field);
			if (state->inputs.size() == 1) {
				box->setFocusCallback([=] { field->setFocusFast(); });
				if (!multiline) {
					field->submits() | rpl::on_next([=] {
						finish(u"ok"_q);
						box->closeBox();
					}, field->lifetime());
				}
			}
		}
		for (const auto &entry : spec.value(u"toggles"_q).toList()) {
			const auto toggle = entry.toMap();
			const auto check = box->addRow(object_ptr<Ui::Checkbox>(
				box,
				toggle.value(u"text"_q).toString(),
				toggle.value(u"checked"_q).toBool(),
				st::defaultBoxCheckbox));
			state->toggles.emplace_back(toggle.value(u"id"_q).toString(), check);
		}
		auto buttons = spec.value(u"buttons"_q).toList();
		if (buttons.isEmpty()) {
			buttons.push_back(QVariantMap{
				{ u"id"_q, u"ok"_q },
				{ u"text"_q, u"OK"_q },
			});
		}
		for (const auto &entry : buttons) {
			const auto button = entry.toMap();
			const auto buttonId = button.value(u"id"_q).toString();
			box->addButton(
				rpl::single(button.value(u"text"_q).toString()),
				[=] { finish(buttonId); box->closeBox(); },
				(button.value(u"danger"_q).toBool()
					? st::attentionBoxButton
					: st::defaultBoxButton));
		}
		box->boxClosing() | rpl::on_next([=] {
			finish(u"cancel"_q);
		}, box->lifetime());
	}));
	return id;
}

int Bridge::registerWidget(QWidget *widget) {
	if (!widget) {
		return 0;
	}
	const auto i = _widgetIds.find(widget);
	if (i != end(_widgetIds)) {
		return i->second;
	}
	const auto id = ++_nextWidgetId;
	_widgetIds.emplace(widget, id);
	_widgetsById.emplace(id, widget);
	QObject::connect(widget, &QObject::destroyed, this, [=] {
		_widgetIds.erase(widget);
		_widgetsById.erase(id);
	});
	return id;
}

QWidget *Bridge::widgetById(int id) const {
	const auto i = _widgetsById.find(id);
	return (i != end(_widgetsById)) ? i->second.data() : nullptr;
}

QVariant Bridge::widgetIds(const QList<QWidget*> &list) {
	auto result = QVariantList();
	result.reserve(list.size());
	for (const auto widget : list) {
		result.push_back(registerWidget(widget));
	}
	return result;
}

QVariant Bridge::mainWindow() {
	const auto window = Manager::Get().activeWindow();
	const auto id = registerWidget(window ? window->widget().get() : nullptr);
	return id ? QVariant(id) : QVariant();
}

QVariant Bridge::focusedWidget() {
	const auto id = registerWidget(QApplication::focusWidget());
	return id ? QVariant(id) : QVariant();
}

QVariant Bridge::findWidgets(
		const QString &className,
		const QString &objectName) {
	return widgetIds(FindWidgets(nullptr, className, objectName));
}

QVariant Bridge::widgetCall(
		int id,
		const QString &method,
		const QVariantList &args) {
	const auto widget = widgetById(id);
	if (!widget) {
		return (method == u"valid"_q) ? QVariant(false) : QVariant();
	}
	const auto result = CallWidgetMethod(widget, method, args, {
		.wrap = [=](QWidget *other) {
			const auto otherId = registerWidget(other);
			return otherId ? QVariant(otherId) : QVariant();
		},
		.wrapList = [=](const QList<QWidget*> &list) {
			return widgetIds(list);
		},
	});
	if (!result) {
		_instance->log(u"error"_q, u"Unknown widget method: "_q + method);
		return QVariant();
	}
	return *result;
}

QVariant Bridge::createWidget(
		int parentId,
		const QString &type,
		const QVariantMap &props) {
	auto parent = widgetById(parentId);
	if (!parent) {
		const auto window = Manager::Get().activeWindow();
		parent = window ? window->widget().get() : nullptr;
	}
	if (!parent) {
		return QVariant();
	}
	const auto text = props.value(u"text"_q).toString();
	const auto weak = QPointer<Bridge>(this);
	const auto id = std::make_shared<int>(0);
	const auto fire = [=](QString event, QVariant value = QVariant()) {
		if (weak && *id) {
			weak->widgetEvent(*id, event, value);
		}
	};
	auto widget = (QWidget*)nullptr;
	if (type == u"label"_q) {
		widget = Ui::CreateChild<Ui::FlatLabel>(
			parent,
			text,
			st::defaultFlatLabel);
	} else if (type == u"button"_q) {
		const auto button = Ui::CreateChild<Ui::RoundButton>(
			parent,
			rpl::single(text),
			(props.value(u"style"_q).toString() == u"light"_q
				? st::defaultLightButton
				: st::defaultActiveButton));
		button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		button->setClickedCallback([=] { fire(u"click"_q); });
		widget = button;
	} else if (type == u"input"_q) {
		const auto field = Ui::CreateChild<Ui::InputField>(
			parent,
			st::defaultInputField,
			(props.value(u"multiline"_q).toBool()
				? Ui::InputField::Mode::MultiLine
				: Ui::InputField::Mode::SingleLine),
			rpl::single(props.value(u"placeholder"_q).toString()),
			text);
		field->changes() | rpl::on_next([=] {
			fire(u"change"_q, field->getLastText());
		}, field->lifetime());
		field->submits() | rpl::on_next([=] {
			fire(u"submit"_q, field->getLastText());
		}, field->lifetime());
		widget = field;
	} else if (type == u"checkbox"_q) {
		const auto check = Ui::CreateChild<Ui::Checkbox>(
			parent,
			text,
			props.value(u"checked"_q).toBool(),
			st::defaultBoxCheckbox);
		check->checkedChanges() | rpl::on_next([=](bool checked) {
			fire(u"change"_q, checked);
		}, check->lifetime());
		widget = check;
	} else if (type == u"panel"_q) {
		const auto panel = Ui::CreateChild<Panel>(
			parent,
			QColor(props.value(u"color"_q, u"#80000000"_q).toString()),
			props.value(u"radius"_q).toInt());
		panel->clicked = [=] { fire(u"click"_q); };
		widget = panel;
	} else {
		_instance->log(u"error"_q, u"Unknown widget type: %1"_q.arg(type));
		return QVariant();
	}
	if (props.contains(u"name"_q)) {
		widget->setObjectName(props.value(u"name"_q).toString());
	}
	ApplyWidgetGeometry(widget, props);
	*id = registerWidget(widget);
	adoptWidget(widget);
	QObject::connect(widget, &QObject::destroyed, this, [=] {
		widgetEvent(*id, u"destroyed"_q);
	});
	if (!props.contains(u"visible"_q) || props.value(u"visible"_q).toBool()) {
		widget->show();
		widget->raise();
	}
	return *id;
}

void Bridge::adoptWidget(QWidget *widget) {
	if (widget) {
		_created.push_back(widget);
	}
}

void Bridge::widgetEvent(
		int widgetId,
		const QString &event,
		const QVariant &value) {
	_instance->dispatch(u"widget"_q, QVariant(QVariantMap{
		{ u"id"_q, widgetId },
		{ u"event"_q, event },
		{ u"value"_q, value },
	}));
}

void Bridge::repaintAll() {
	for (const auto widget : QApplication::allWidgets()) {
		widget->update();
	}
}

bool Bridge::setThemeColor(const QString &name, const QString &color) {
	const auto parsed = QColor(color);
	if (!parsed.isValid()) {
		return false;
	}
	if (!SavedPalette) {
		SavedPalette = style::main_palette::save();
	}
	const auto latin = name.toLatin1();
	const auto result = style::main_palette::setColor(
		QLatin1String(latin.constData(), latin.size()),
		uchar(parsed.red()),
		uchar(parsed.green()),
		uchar(parsed.blue()),
		uchar(parsed.alpha()));
	if (result != style::palette::SetResult::Ok) {
		return false;
	}
	_themeChanged = true;
	style::NotifyPaletteChanged();
	return true;
}

void Bridge::resetTheme() {
	if (!SavedPalette) {
		return;
	}
	style::main_palette::load(*SavedPalette);
	SavedPalette = std::nullopt;
	_themeChanged = false;
	style::NotifyPaletteChanged();
}

QVariant Bridge::currentChat() const {
	const auto controller = Manager::Get().activeController();
	if (!controller) {
		return QVariant();
	}
	const auto peer = controller->activeChatCurrent().peer();
	return peer ? QVariant(SerializePeer(peer)) : QVariant();
}

QVariant Bridge::getPeer(const QString &peerId) const {
	const auto peer = ResolvePeer(Manager::Get().activeSession(), peerId);
	return peer ? QVariant(SerializePeer(peer)) : QVariant();
}

QVariant Bridge::findPeer(const QString &username) const {
	const auto session = Manager::Get().activeSession();
	const auto peer = session
		? session->data().peerByUsername(username)
		: nullptr;
	return peer ? QVariant(SerializePeer(peer)) : QVariant();
}

bool Bridge::openChat(const QString &peerId, double msgId) {
	const auto controller = Manager::Get().activeController();
	const auto peer = ResolvePeer(Manager::Get().activeSession(), peerId);
	if (!controller || !peer) {
		return false;
	}
	controller->showPeerHistory(
		peer,
		Window::SectionShow::Way::ClearStack,
		msgId > 0 ? MsgId(int64(msgId)) : ShowAtUnreadMsgId);
	return true;
}

QVariant Bridge::getMessage(const QString &peerId, double msgId) const {
	const auto item = ResolveMessage(
		Manager::Get().activeSession(),
		peerId,
		msgId);
	return item ? QVariant(SerializeMessage(item)) : QVariant();
}

QVariantList Bridge::loadedMessages(const QString &peerId, int limit) const {
	auto result = QVariantList();
	const auto session = Manager::Get().activeSession();
	const auto peer = ResolvePeer(session, peerId);
	if (!peer || limit <= 0) {
		return result;
	}
	const auto history = session->data().historyLoaded(peer);
	if (!history) {
		return result;
	}
	auto items = std::vector<not_null<HistoryItem*>>();
	for (const auto &block : history->blocks) {
		for (const auto &view : block->messages) {
			items.push_back(view->data());
		}
	}
	const auto from = std::max(0, int(items.size()) - limit);
	for (auto i = from; i != int(items.size()); ++i) {
		result.push_back(SerializeMessage(items[i]));
	}
	return result;
}

bool Bridge::sendMessage(
		const QString &peerId,
		const QString &text,
		const QVariantMap &options) {
	const auto session = Manager::Get().activeSession();
	const auto peer = ResolvePeer(session, peerId);
	if (!peer || text.isEmpty()) {
		return false;
	}
	const auto history = session->data().history(peer);
	auto sendOptions = Api::SendOptions();
	sendOptions.silent = options.value(u"silent"_q).toBool();
	auto action = Api::SendAction(history, sendOptions);
	action.clearDraft = false;
	if (const auto replyTo = options.value(u"replyTo"_q).toLongLong()) {
		action.replyTo.messageId = FullMsgId(peer->id, MsgId(replyTo));
	}
	auto message = Api::MessageToSend(action);
	message.textWithTags = TextWithTags{ text };
	const auto runHooks = options.value(u"runHooks"_q).toBool();
	auto &manager = Manager::Get();
	const auto was = manager.sendingFromPlugin();
	manager.setSendingFromPlugin(!runHooks);
	session->api().sendMessage(std::move(message));
	manager.setSendingFromPlugin(was);
	return true;
}

bool Bridge::editMessage(
		const QString &peerId,
		double msgId,
		const QString &text) {
	const auto item = ResolveMessage(
		Manager::Get().activeSession(),
		peerId,
		msgId);
	if (!item) {
		return false;
	}
	const auto weak = QPointer<Bridge>(this);
	Api::EditTextMessage(
		item,
		TextWithEntities{ text },
		Data::WebPageDraft(),
		Api::SendOptions(),
		[](mtpRequestId) {},
		[=](const QString &error, mtpRequestId) {
			if (weak) {
				weak->log(u"warn"_q, u"editMessage failed: "_q + error);
			}
		},
		false);
	return true;
}

bool Bridge::deleteMessages(
		const QString &peerId,
		const QVariantList &ids,
		bool revoke) {
	const auto session = Manager::Get().activeSession();
	const auto peer = ResolvePeer(session, peerId);
	if (!peer) {
		return false;
	}
	auto list = MessageIdsList();
	for (const auto &id : ids) {
		list.push_back(FullMsgId(peer->id, MsgId(id.toLongLong())));
	}
	if (list.empty()) {
		return false;
	}
	session->data().histories().deleteMessages(list, revoke);
	session->data().sendHistoryChangeNotifications();
	return true;
}

void Bridge::refilter() {
	Manager::Get().invalidateFilters();
}

int Bridge::httpRequest(const QVariantMap &request) {
	const auto id = nextRequestId();
	auto req = QNetworkRequest(QUrl(request.value(u"url"_q).toString()));
	const auto headers = request.value(u"headers"_q).toMap();
	for (auto i = headers.begin(); i != headers.end(); ++i) {
		req.setRawHeader(i.key().toUtf8(), i.value().toString().toUtf8());
	}
	if (!req.hasRawHeader("User-Agent")) {
		req.setRawHeader(
			"User-Agent",
			u"DamZaGram/%1"_q.arg(AppVersionStr).toUtf8());
	}
	req.setTransferTimeout(request.value(u"timeout"_q, 30000).toInt());
	req.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	const auto method = request.value(u"method"_q, u"GET"_q).toString();
	const auto body = request.value(u"body"_q).toString().toUtf8();
	const auto reply = Manager::Get().network()->sendCustomRequest(
		req,
		method.toUtf8(),
		body);
	_requests.emplace(id, reply);
	QObject::connect(reply, &QNetworkReply::finished, this, [=] {
		_requests.erase(id);
		reply->deleteLater();
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (!status && reply->error() != QNetworkReply::NoError) {
			_instance->dispatch(u"settle"_q, QVariant(QVariantMap{
				{ u"id"_q, id },
				{ u"ok"_q, false },
				{ u"value"_q, reply->errorString() },
			}));
			return;
		}
		auto responseHeaders = QVariantMap();
		for (const auto &[key, value] : reply->rawHeaderPairs()) {
			responseHeaders.insert(
				QString::fromUtf8(key).toLower(),
				QString::fromUtf8(value));
		}
		_instance->dispatch(u"settle"_q, QVariant(QVariantMap{
			{ u"id"_q, id },
			{ u"ok"_q, true },
			{ u"value"_q, QVariantMap{
				{ u"status"_q, status },
				{ u"headers"_q, responseHeaders },
				{ u"url"_q, reply->url().toString() },
				{ u"body"_q, QString::fromUtf8(reply->readAll()) },
			} },
		}));
	});
	return id;
}

void Bridge::openUrl(const QString &url) {
	QDesktopServices::openUrl(QUrl(url));
}

QString Bridge::clipboardText() const {
	return QGuiApplication::clipboard()->text();
}

void Bridge::setClipboardText(const QString &text) {
	QGuiApplication::clipboard()->setText(text);
}

QString Bridge::resolvePath(const QString &path) const {
	if (QDir::isAbsolutePath(path)) {
		return QDir::cleanPath(path);
	}
	return QDir::cleanPath(QDir(_instance->manifest().dir).filePath(path));
}

QVariant Bridge::readFile(const QString &path) const {
	auto file = QFile(resolvePath(path));
	if (!file.open(QIODevice::ReadOnly)) {
		return QVariant();
	}
	return QString::fromUtf8(file.readAll());
}

bool Bridge::writeFile(const QString &path, const QString &text) {
	const auto resolved = resolvePath(path);
	QDir().mkpath(QFileInfo(resolved).absolutePath());
	auto file = QFile(resolved);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	return file.write(text.toUtf8()) >= 0;
}

QStringList Bridge::listDir(const QString &path) const {
	return QDir(resolvePath(path)).entryList(
		QDir::AllEntries | QDir::NoDotAndDotDot);
}

bool Bridge::fileExists(const QString &path) const {
	return QFileInfo::exists(resolvePath(path));
}

} // namespace Damza::Plugins
