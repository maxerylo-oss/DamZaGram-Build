// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

namespace Damza::Plugins::Builtin {

// One file of a built-in plugin, embedded into the binary.
// The contents are split into string chunks because MSVC limits the
// size of a single string literal; `chunks` ends with nullptr.
struct File {
	const char *path = nullptr; // "<plugin id>/<path inside the plugin>"
	const char *const *chunks = nullptr;
	// Byte sizes of the chunks for binary files (they may contain zero
	// bytes), nullptr for text files.
	const int *sizes = nullptr;
};

// Generated from plugins/builtin/ by tools/damza/embed_builtins.py.
[[nodiscard]] const std::vector<File> &Files();

} // namespace Damza::Plugins::Builtin
