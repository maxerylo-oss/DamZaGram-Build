// Forced include for js_runtime_test.sh: the bits of the client's
// precompiled header that plugin_js.cpp relies on.
#pragma once
#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <chrono>
#include <functional>
#include <memory>
#include <vector>
#include <range/v3/all.hpp>

inline QString operator""_q(const char16_t *s, size_t n) {
	return QString(reinterpret_cast<const QChar*>(s), qsizetype(n));
}
template <typename T>
using Fn = std::function<T>;
template <typename T>
using not_null = T;

namespace crl {
using time = qint64;
inline time now() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace crl

namespace gsl {
template <typename F>
class final_action {
public:
	explicit final_action(F f) : _f(std::move(f)) {
	}
	~final_action() {
		_f();
	}
private:
	F _f;
};
template <typename F>
final_action<F> finally(F f) {
	return final_action<F>(std::move(f));
}
} // namespace gsl
