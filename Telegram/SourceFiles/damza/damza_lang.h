// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

#include "lang/lang_instance.h"

namespace Damza {

// DamZaGram strings live here instead of lang.strings, so adding one
// doesn't regenerate lang_auto.h and rebuild half of the client.
[[nodiscard]] inline QString Text(const char *en, const char *ru) {
	const auto id = Lang::GetInstance().id();
	const auto base = Lang::GetInstance().baseId();
	const auto russian = id.startsWith(u"ru"_q) || base.startsWith(u"ru"_q);
	return QString::fromUtf8(russian ? ru : en);
}

} // namespace Damza
