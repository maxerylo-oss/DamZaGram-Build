// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/plugins/plugin_widgets.h"

#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QPointer>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGraphicsOpacityEffect>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QWidget>

#include <typeinfo>

#if defined __GNUC__ || defined __clang__
#include <cxxabi.h>
#endif

namespace Damza::Plugins {
namespace {

[[nodiscard]] QVariantMap RectToMap(const QRect &rect) {
	return {
		{ u"x"_q, rect.x() },
		{ u"y"_q, rect.y() },
		{ u"width"_q, rect.width() },
		{ u"height"_q, rect.height() },
	};
}

[[nodiscard]] QList<QWidget*> DirectChildren(not_null<QWidget*> widget) {
	return widget->findChildren<QWidget*>(
		QString(),
		Qt::FindDirectChildrenOnly);
}

[[nodiscard]] QString WidgetText(not_null<QWidget*> widget) {
	if (const auto field = dynamic_cast<Ui::InputField*>(widget.get())) {
		return field->getLastText();
	} else if (const auto label = dynamic_cast<Ui::FlatLabel*>(
			widget.get())) {
		return label->accessibilityName();
	} else if (const auto label = qobject_cast<QLabel*>(widget)) {
		return label->text();
	} else if (const auto edit = qobject_cast<QLineEdit*>(widget)) {
		return edit->text();
	} else if (const auto button = qobject_cast<QAbstractButton*>(widget)) {
		return button->text();
	} else if (const auto rp = dynamic_cast<Ui::RpWidget*>(widget.get())) {
		const auto name = rp->accessibilityName();
		if (!name.isEmpty()) {
			return name;
		}
	}
	return widget->windowTitle();
}

void SetWidgetText(not_null<QWidget*> widget, const QString &text) {
	if (const auto field = dynamic_cast<Ui::InputField*>(widget.get())) {
		field->setText(text);
	} else if (const auto label = dynamic_cast<Ui::FlatLabel*>(
			widget.get())) {
		label->setText(text);
	} else if (const auto label = qobject_cast<QLabel*>(widget)) {
		label->setText(text);
	} else if (const auto edit = qobject_cast<QLineEdit*>(widget)) {
		edit->setText(text);
	} else if (const auto button = qobject_cast<QAbstractButton*>(widget)) {
		button->setText(text);
	} else if (const auto round = dynamic_cast<Ui::RoundButton*>(
			widget.get())) {
		round->setText(rpl::single(text));
	} else if (const auto flat = dynamic_cast<Ui::FlatButton*>(
			widget.get())) {
		flat->setText(text);
	} else if (const auto link = dynamic_cast<Ui::LinkButton*>(
			widget.get())) {
		link->setText(text);
	}
}

void SetOpacity(not_null<QWidget*> widget, double opacity) {
	if (opacity >= 1.) {
		widget->setGraphicsEffect(nullptr);
		return;
	}
	auto effect = qobject_cast<QGraphicsOpacityEffect*>(
		widget->graphicsEffect());
	if (!effect) {
		effect = new QGraphicsOpacityEffect(widget);
		widget->setGraphicsEffect(effect);
	}
	effect->setOpacity(std::clamp(opacity, 0., 1.));
}

void Click(not_null<QWidget*> widget, int x, int y) {
	auto target = widget.get();
	auto local = QPointF(x, y);
	if (const auto child = widget->childAt(x, y)) {
		local = child->mapFrom(widget.get(), QPoint(x, y));
		target = child;
	}
	const auto global = QPointF(target->mapToGlobal(local.toPoint()));
	const auto weak = QPointer<QWidget>(target);
	auto press = QMouseEvent(
		QEvent::MouseButtonPress,
		local,
		global,
		Qt::LeftButton,
		Qt::LeftButton,
		Qt::NoModifier);
	QCoreApplication::sendEvent(target, &press);
	if (!weak) {
		return;
	}
	auto release = QMouseEvent(
		QEvent::MouseButtonRelease,
		local,
		global,
		Qt::LeftButton,
		Qt::NoButton,
		Qt::NoModifier);
	QCoreApplication::sendEvent(target, &release);
}

void TypeText(not_null<QWidget*> widget, const QString &text) {
	const auto weak = QPointer<QWidget>(widget.get());
	for (const auto &ch : text) {
		auto press = QKeyEvent(QEvent::KeyPress, 0, Qt::NoModifier, ch);
		QCoreApplication::sendEvent(widget, &press);
		if (!weak) {
			return;
		}
		auto release = QKeyEvent(QEvent::KeyRelease, 0, Qt::NoModifier, ch);
		QCoreApplication::sendEvent(widget, &release);
		if (!weak) {
			return;
		}
	}
}

[[nodiscard]] QStringList PropertyNames(not_null<QWidget*> widget) {
	auto result = QStringList();
	const auto meta = widget->metaObject();
	for (auto i = 0; i != meta->propertyCount(); ++i) {
		result.push_back(QString::fromLatin1(meta->property(i).name()));
	}
	for (const auto &name : widget->dynamicPropertyNames()) {
		result.push_back(QString::fromUtf8(name));
	}
	return result;
}

[[nodiscard]] QVariantMap Dump(not_null<QWidget*> widget, int depth) {
	auto result = QVariantMap{
		{ u"className"_q, RealClassName(widget) },
		{ u"name"_q, widget->objectName() },
		{ u"visible"_q, widget->isVisible() },
		{ u"geometry"_q, RectToMap(widget->geometry()) },
	};
	if (depth > 0) {
		auto children = QVariantList();
		for (const auto child : DirectChildren(widget)) {
			children.push_back(Dump(child, depth - 1));
		}
		result.insert(u"children"_q, children);
	}
	return result;
}

[[nodiscard]] QStringList ClassChain(not_null<QWidget*> widget) {
	auto result = QStringList{ RealClassName(widget) };
	for (auto meta = widget->metaObject(); meta; meta = meta->superClass()) {
		const auto name = QString::fromLatin1(meta->className());
		if (!result.contains(name)) {
			result.push_back(name);
		}
	}
	return result;
}

} // namespace

QString RealClassName(not_null<QWidget*> widget) {
	const auto raw = typeid(*widget.get()).name();
#if defined __GNUC__ || defined __clang__
	auto status = 0;
	const auto demangled = abi::__cxa_demangle(raw, nullptr, nullptr, &status);
	if (demangled && !status) {
		const auto result = QString::fromLatin1(demangled);
		std::free(demangled);
		return result;
	}
#endif // __GNUC__ || __clang__
	return QString::fromLatin1(raw);
}

bool WidgetMatches(
		not_null<QWidget*> widget,
		const QString &className,
		const QString &objectName) {
	if (!objectName.isEmpty() && widget->objectName() != objectName) {
		return false;
	} else if (className.isEmpty()) {
		return true;
	}
	const auto real = RealClassName(widget);
	if (real == className || real.endsWith(u"::"_q + className)) {
		return true;
	}
	return widget->inherits(className.toLatin1().constData());
}

QList<QWidget*> FindWidgets(
		QWidget *root,
		const QString &className,
		const QString &objectName) {
	auto result = QList<QWidget*>();
	const auto check = [&](QWidget *widget) {
		if (WidgetMatches(widget, className, objectName)) {
			result.push_back(widget);
		}
	};
	const auto roots = root
		? QList<QWidget*>{ root }
		: QApplication::topLevelWidgets();
	for (const auto top : roots) {
		check(top);
		for (const auto child : top->findChildren<QWidget*>()) {
			check(child);
		}
	}
	return result;
}

std::optional<QVariant> CallWidgetMethod(
		not_null<QWidget*> widget,
		const QString &method,
		const QVariantList &args,
		const WidgetCallContext &context) {
	const auto arg = [&](int index) {
		return args.value(index);
	};
	const auto integer = [&](int index) {
		return arg(index).toInt();
	};
	const auto string = [&](int index) {
		return arg(index).toString();
	};
	const auto none = QVariant();

	if (method == u"valid"_q) {
		return true;
	} else if (method == u"className"_q) {
		return RealClassName(widget);
	} else if (method == u"classChain"_q) {
		return ClassChain(widget);
	} else if (method == u"name"_q) {
		return widget->objectName();
	} else if (method == u"setName"_q) {
		widget->setObjectName(string(0));
		return none;
	} else if (method == u"geometry"_q) {
		return RectToMap(widget->geometry());
	} else if (method == u"globalGeometry"_q) {
		return RectToMap(QRect(widget->mapToGlobal(QPoint()), widget->size()));
	} else if (method == u"setGeometry"_q) {
		widget->setGeometry(integer(0), integer(1), integer(2), integer(3));
		return none;
	} else if (method == u"move"_q) {
		widget->move(integer(0), integer(1));
		return none;
	} else if (method == u"resize"_q) {
		widget->resize(integer(0), integer(1));
		return none;
	} else if (method == u"visible"_q) {
		return widget->isVisible();
	} else if (method == u"setVisible"_q) {
		widget->setVisible(arg(0).toBool());
		return none;
	} else if (method == u"show"_q) {
		widget->show();
		return none;
	} else if (method == u"hide"_q) {
		widget->hide();
		return none;
	} else if (method == u"raise"_q) {
		widget->raise();
		return none;
	} else if (method == u"lower"_q) {
		widget->lower();
		return none;
	} else if (method == u"update"_q) {
		widget->update();
		return none;
	} else if (method == u"setOpacity"_q) {
		SetOpacity(widget, arg(0).toDouble());
		return none;
	} else if (method == u"setEnabled"_q) {
		widget->setEnabled(arg(0).toBool());
		return none;
	} else if (method == u"setFocus"_q) {
		widget->setFocus();
		return none;
	} else if (method == u"destroy"_q) {
		widget->deleteLater();
		return none;
	} else if (method == u"parentWidget"_q) {
		return context.wrap(widget->parentWidget());
	} else if (method == u"window"_q) {
		return context.wrap(widget->window());
	} else if (method == u"childWidgets"_q) {
		return context.wrapList(DirectChildren(widget));
	} else if (method == u"find"_q) {
		auto list = FindWidgets(widget, string(0), string(1));
		list.removeAll(widget.get());
		return context.wrapList(list);
	} else if (method == u"childAt"_q) {
		return context.wrap(widget->childAt(integer(0), integer(1)));
	} else if (method == u"prop"_q) {
		return widget->property(string(0).toUtf8().constData());
	} else if (method == u"setProp"_q) {
		return widget->setProperty(string(0).toUtf8().constData(), arg(1));
	} else if (method == u"propNames"_q) {
		return PropertyNames(widget);
	} else if (method == u"click"_q) {
		Click(widget, integer(0), integer(1));
		return none;
	} else if (method == u"typeText"_q) {
		TypeText(widget, string(0));
		return none;
	} else if (method == u"grab"_q) {
		const auto path = context.resolvePath
			? context.resolvePath(string(0))
			: string(0);
		return widget->grab().save(path);
	} else if (method == u"text"_q) {
		return WidgetText(widget);
	} else if (method == u"setText"_q) {
		SetWidgetText(widget, string(0));
		return none;
	} else if (method == u"dump"_q) {
		return Dump(widget, integer(0));
	}
	return std::nullopt;
}

} // namespace Damza::Plugins
