// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

namespace Damza::Plugins {

struct EmbeddedFile {
	QString name;
	QByteArray content;
};

// docs/plugins/types/*.d.ts, see tools/damza/embed_types.py.
[[nodiscard]] std::vector<EmbeddedFile> EmbeddedTypeFiles();

} // namespace Damza::Plugins
