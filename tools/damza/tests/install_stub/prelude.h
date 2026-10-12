// Forced include for install_test.sh: the bits of the client's precompiled
// header that plugin_install.cpp relies on.
#pragma once
#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <array>
#include <range/v3/all.hpp>

inline QString operator""_q(const char16_t *s, size_t n) {
	return QString(reinterpret_cast<const QChar*>(s), qsizetype(n));
}
template <typename T>
using Fn = std::function<T>;
#define LOG(x) ((void)0)
using uint32 = quint32;
using uint64 = quint64;
using int32 = qint32;
using int64 = qint64;
