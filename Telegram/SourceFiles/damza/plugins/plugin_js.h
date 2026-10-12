// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

struct JSRuntime;
struct JSContext;

namespace Damza::Plugins::Js {

using Native = Fn<QVariant(const QVariantList &args)>;

struct NativeMethod {
	const char *name = nullptr;
	Native method;
};

// One QuickJS runtime and context. Not thread-safe: main thread only.
// Values cross the boundary as QVariant (null, bool, number, string,
// QVariantList, QVariantMap); functions never leave the engine.
class Runtime final {
public:
	using LogCallback = Fn<void(const QString &level, const QString &text)>;

	Runtime(QString moduleRoot, LogCallback log);
	~Runtime();

	Runtime(const Runtime &) = delete;
	Runtime &operator=(const Runtime &) = delete;

	// Creates a global object `name` with the given native functions.
	void installObject(const char *name, std::vector<NativeMethod> methods);

	// Global script (sloppy top level, defines globals).
	[[nodiscard]] bool evalScript(
		const QString &code,
		const QString &fileName,
		QString *error);
	// ES module: import/export and top-level await.
	[[nodiscard]] bool evalModule(
		const QString &code,
		const QString &fileName,
		QString *error);

	// Evaluates code as a global script and returns the completion value
	// as text: JSON when possible, String(value) otherwise. Awaits nothing.
	[[nodiscard]] QString evaluateToString(const QString &code, QString *error);

	// Remembers a global function to call later with callGlobal().
	[[nodiscard]] bool bindGlobalFunction(const char *name);
	QVariant callBound(const QVariantList &args, QString *error);

	void deleteGlobal(const char *name);

	// Max time a single entry from C++ into JS may run.
	void setTimeLimit(crl::time ms);

	[[nodiscard]] bool busy() const;

	// Used by the static QuickJS callbacks.
	[[nodiscard]] bool shouldInterrupt() const;
	void reportRejection(const QString &reason);
	[[nodiscard]] const QString &moduleRoot() const;
	[[nodiscard]] QString exceptionText();

private:
	class Entry;
	friend class Entry;

	void runJobs();

	const QString _moduleRoot;
	const LogCallback _log;
	JSRuntime *_runtime = nullptr;
	JSContext *_context = nullptr;
	void *_bound = nullptr; // JSValue *
	crl::time _timeLimit = 3000;
	crl::time _deadline = 0;
	int _depth = 0;
	bool _interrupted = false;

};

} // namespace Damza::Plugins::Js
