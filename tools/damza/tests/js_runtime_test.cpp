// Tests of Js::Runtime (damza/plugins/plugin_js.cpp): values crossing the
// boundary (cycles, huge arrays, throwing getters), the watchdog and the
// stack limit. Built by js_runtime_test.sh against QtCore and QuickJS only.
#include "damza/plugins/plugin_js.h"

#include <cstdio>

using namespace Damza::Plugins::Js;

namespace {

int Failures = 0;

void Check(bool condition, const char *what, const QString &details = {}) {
	if (!condition) {
		++Failures;
		std::printf(
			"FAIL: %s %s\n",
			what,
			details.toUtf8().constData());
	} else {
		std::printf("ok: %s\n", what);
	}
}

} // namespace

int main() {
	auto log = QStringList();
	auto runtime = Runtime(QString(), [&](const QString &level, const QString &text) {
		log.push_back(level + u": "_q + text);
	});
	auto taken = QVariantList();
	auto calls = 0;
	runtime.installObject("native", {
		{ "take", [&](const QVariantList &args) {
			++calls;
			taken = args;
			return QVariant(QVariantMap{
				{ u"list"_q, QVariantList{ 1, u"two"_q, true, QVariant() } },
				{ u"big"_q, qint64(1) << 40 },
			});
		} },
	});
	runtime.setTimeLimit(2000);

	auto error = QString();
	const auto run = [&](const char *code) {
		error = QString();
		const auto result = runtime.evaluateToString(QString::fromUtf8(code), &error);
		return error.isEmpty() ? result : (u"ERROR "_q + error);
	};

	{
		const auto result = run("const r = native.take({ a: [1, 'x', null, false], f() {}, u: undefined, n: 1.5 }); r.big + ':' + r.list.join(',')");
		Check(result == u"\"1099511627776:1,two,true,\""_q, "native result converts", result);
		const auto map = taken.value(0).toMap();
		Check(map.value(u"a"_q).toList().size() == 4, "array argument");
		Check(!map.contains(u"f"_q) && !map.contains(u"u"_q), "functions and undefined dropped");
		Check(map.value(u"n"_q).toDouble() == 1.5, "double argument");
	}
	{
		const auto result = run("const c = { x: 1 }; c.self = c; c.list = [c, 2]; native.take(c); 'done'");
		const auto map = taken.value(0).toMap();
		Check(result == u"\"done\""_q, "cyclic argument accepted", result);
		Check(map.value(u"x"_q).toInt() == 1, "cyclic: plain field kept");
		Check(!map.value(u"self"_q).isValid(), "cyclic: back reference is null");
		Check(map.value(u"list"_q).toList().value(1).toInt() == 2, "cyclic: rest of the list kept");
		const auto shared = run("const s = { v: 1 }; native.take({ a: s, b: s }); 'done'");
		Check(taken.value(0).toMap().value(u"b"_q).toMap().value(u"v"_q).toInt() == 1, "shared (not cyclic) object converted twice", shared);
	}
	{
		const auto started = crl::now();
		calls = 0;
		const auto result = run("let d = {}; for (let i = 0; i < 40; ++i) d = { a: d, b: d }; try { native.take(d); 'passed' } catch (e) { (e instanceof RangeError) ? 'range' : String(e) }");
		Check(result == u"\"range\""_q, "exponential DAG rejected", result);
		Check(calls == 0, "exponential DAG: native not called");
		Check(crl::now() - started < 2000, "exponential DAG: fast");
	}
	{
		const auto started = crl::now();
		const auto result = run("const h = []; h.length = 4294967295; try { native.take(h); 'passed' } catch (e) { (e instanceof RangeError) ? 'range' : String(e) }");
		Check(result == u"\"range\""_q, "huge sparse array rejected", result);
		const auto other = run("const g = []; g.length = 1e9; try { native.take(g); 'passed' } catch (e) { (e instanceof RangeError) ? 'range' : String(e) }");
		Check(other == u"\"range\""_q, "1e9 sparse array rejected", other);
		Check(crl::now() - started < 2000, "huge arrays: fast");
		const auto fine = run("native.take(new Array(1000).fill(7)); 'ok'");
		Check(fine == u"\"ok\""_q && taken.value(0).toList().size() == 1000, "regular array of 1000", fine);
	}
	{
		calls = 0;
		const auto result = run("try { native.take({ get x() { throw new Error('boom'); } }); 'passed' } catch (e) { e.message }");
		Check(result == u"\"boom\""_q, "throwing getter propagates", result);
		Check(calls == 0, "throwing getter: native not called");
		const auto after = run("1 + 1");
		Check(after == u"2"_q, "runtime usable after a throwing getter", after);
	}
	{
		Check(runtime.evalScript(u"globalThis.__d = (type) => { const r = { t: type }; r.r = r; return r; };"_q, u"d.js"_q, &error), "dispatcher defined");
		Check(runtime.bindGlobalFunction("__d"), "dispatcher bound");
		error = QString();
		const auto result = runtime.callBound({ u"x"_q }, &error);
		Check(error.isEmpty() && result.toMap().value(u"t"_q).toString() == u"x"_q, "cyclic dispatch result", error);
		Check(runtime.evalScript(u"globalThis.__d = () => { const a = []; a.length = 1e9; return a; };"_q, u"d.js"_q, &error), "dispatcher redefined");
		Check(runtime.bindGlobalFunction("__d"), "dispatcher rebound");
		error = QString();
		const auto huge = runtime.callBound({}, &error);
		Check(!error.isEmpty() && !huge.isValid(), "huge dispatch result reported", error);
	}
	{
		const auto result = run("function f(n) { return f(n + 1) + 1; } try { f(0); 'passed' } catch (e) { (e instanceof RangeError) ? 'stack' : String(e) }");
		Check(result == u"\"stack\""_q, "deep recursion throws", result);
	}
	{
		// A plugin entered from another plugin's native call (an event,
		// a send hook, plugins.eval) must not get a fresh stack budget.
		auto inner = Runtime(QString(), [](const QString &, const QString &) {});
		auto innerError = QString();
		Check(inner.evalScript(u"globalThis.depth = () => { let max = 0; const g = (n) => { max = n; g(n + 1); }; try { g(0); } catch (e) {} return max; };"_q, u"inner.js"_q, &innerError), "inner runtime", innerError);
		const auto measure = [&] {
			innerError = QString();
			return inner.evaluateToString(u"depth()"_q, &innerError).toInt();
		};
		runtime.installObject("nested", {
			{ "depth", [&](const QVariantList &) { return QVariant(measure()); } },
		});
		const auto alone = measure();
		const auto result = run("(() => { let max = 0; const h = (n) => { max = n; h(n + 1); }; try { h(0); } catch (e) {} const f = (n) => n ? f(n - 1) : nested.depth(); return f(Math.floor(max * 0.9)); })()");
		Check(alone > 100 && result.toInt() < alone / 2, "nested runtime shares the stack budget", result + u" vs "_q + QString::number(alone));
		const auto after = measure();
		Check(after == alone, "stack budget restored after the nested call", QString::number(after));
	}
	{
		runtime.setTimeLimit(200);
		const auto started = crl::now();
		const auto result = run("for (;;) {}");
		Check(result.contains(u"Script took longer than 200 ms"_q), "watchdog", result);
		Check(crl::now() - started < 2000, "watchdog: fast");
		const auto after = run("'alive'");
		Check(after == u"\"alive\""_q, "runtime usable after the watchdog", after);
	}
	if (Failures) {
		std::printf("%d check(s) failed\n", Failures);
		return 1;
	}
	std::printf("js_runtime_test: all checks passed\n");
	return 0;
}
