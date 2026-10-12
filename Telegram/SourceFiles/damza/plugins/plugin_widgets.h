// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

class QWidget;

namespace Damza::Plugins {

[[nodiscard]] QString RealClassName(not_null<QWidget*> widget);

[[nodiscard]] bool WidgetMatches(
	not_null<QWidget*> widget,
	const QString &className,
	const QString &objectName);

// root == nullptr searches every top-level window.
[[nodiscard]] QList<QWidget*> FindWidgets(
	QWidget *root,
	const QString &className,
	const QString &objectName);

struct WidgetCallContext {
	Fn<QVariant(QWidget*)> wrap;
	Fn<QVariant(const QList<QWidget*>&)> wrapList;
};

// Runs a `Widget` method from the JS API on a live widget.
// Returns std::nullopt for an unknown method name.
[[nodiscard]] std::optional<QVariant> CallWidgetMethod(
	not_null<QWidget*> widget,
	const QString &method,
	const QVariantList &args,
	const WidgetCallContext &context);

} // namespace Damza::Plugins
