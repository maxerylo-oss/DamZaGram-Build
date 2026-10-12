#pragma once
namespace Damza {
[[nodiscard]] inline QString Text(const char *en, const char *) {
	return QString::fromUtf8(en);
}
} // namespace Damza
