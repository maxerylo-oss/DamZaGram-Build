// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/plugins/tl/tl_schema.h"

#include <QtCore/QRegularExpression>

#include <algorithm>

namespace Damza::Tl {
namespace {

// api.tl nests Vector<> two levels deep at most. Type texts also come from
// plugins (dzg.api.encode / decode), unbounded nesting would overflow the
// stack here and in the codec.
constexpr auto kMaxTypeNesting = 16;

[[nodiscard]] bool IsBareName(const QString &name) {
	const auto dot = name.lastIndexOf(QLatin1Char('.'));
	const auto first = name.mid(dot + 1, 1);
	return !first.isEmpty() && first[0].isLower();
}

[[nodiscard]] TypeRef ParseType(
	const QString &text,
	const QString &generic,
	int depth);

[[nodiscard]] std::shared_ptr<const TypeRef> Inner(
		const QString &text,
		const QString &generic,
		int depth) {
	return std::make_shared<const TypeRef>(
		ParseType(text, generic, depth + 1));
}

TypeRef ParseType(const QString &text, const QString &generic, int depth) {
	auto result = TypeRef();
	if (depth > kMaxTypeNesting) {
		// No such constructor, so encoding and decoding fail cleanly.
		result.kind = TypeKind::Bare;
		result.name = QStringLiteral("(too deeply nested type)");
		return result;
	}
	const auto type = text.trimmed();
	const auto simple = [&](TypeKind kind) {
		result.kind = kind;
		result.name = type;
		return result;
	};
	if (type == QLatin1String("#")) {
		return simple(TypeKind::Flags);
	} else if (type.startsWith(QLatin1Char('!'))) {
		result.kind = TypeKind::Query;
		result.name = type.mid(1);
		return result;
	} else if (!generic.isEmpty() && type == generic) {
		return simple(TypeKind::Generic);
	} else if (type == QLatin1String("int")) {
		return simple(TypeKind::Int);
	} else if (type == QLatin1String("long")) {
		return simple(TypeKind::Long);
	} else if (type == QLatin1String("double")) {
		return simple(TypeKind::Double);
	} else if (type == QLatin1String("string")) {
		return simple(TypeKind::String);
	} else if (type == QLatin1String("bytes")) {
		return simple(TypeKind::Bytes);
	} else if (type == QLatin1String("int128")) {
		return simple(TypeKind::Int128);
	} else if (type == QLatin1String("int256")) {
		return simple(TypeKind::Int256);
	} else if (type == QLatin1String("true")) {
		return simple(TypeKind::True);
	} else if (type == QLatin1String("Bool")) {
		return simple(TypeKind::Bool);
	} else if (type == QLatin1String("Object") || type.isEmpty()) {
		return simple(TypeKind::Object);
	} else if (type.endsWith(QLatin1Char('>'))
		&& (type.startsWith(QLatin1String("Vector<"))
			|| type.startsWith(QLatin1String("vector<")))) {
		result.kind = type.startsWith(QLatin1Char('V'))
			? TypeKind::Vector
			: TypeKind::BareVector;
		result.name = type.left(6);
		result.inner = Inner(type.mid(7, type.size() - 8), generic, depth);
		return result;
	} else if (type.startsWith(QLatin1Char('%'))) {
		return simple(TypeKind::Bare);
	} else if (IsBareName(type)) {
		return simple(TypeKind::Bare);
	}
	return simple(TypeKind::Boxed);
}

} // namespace

TypeRef ParseType(const QString &text, const QString &generic) {
	return ParseType(text, generic, 0);
}

QString TypeRef::text() const {
	switch (kind) {
	case TypeKind::Vector:
	case TypeKind::BareVector:
		return name
			+ QLatin1Char('<')
			+ (inner ? inner->text() : QString())
			+ QLatin1Char('>');
	case TypeKind::Query:
		return QLatin1Char('!') + name;
	default:
		return name;
	}
}

Schema::Schema() = default;

Schema::~Schema() = default;

int Schema::add(const QString &text, bool allowMethods) {
	static const auto layerRegExp = QRegularExpression(
		QStringLiteral("^//\\s*LAYER\\s+(\\d+)\\s*$"));
	auto method = false;
	auto statement = QString();
	auto count = 0;
	const auto lines = text.split(QLatin1Char('\n'));
	for (const auto &raw : lines) {
		auto line = raw.trimmed();
		if (line.startsWith(QLatin1String("//"))) {
			const auto match = layerRegExp.match(line);
			if (match.hasMatch()) {
				_layer = match.captured(1).toInt();
			}
			continue;
		}
		const auto comment = line.indexOf(QLatin1String("//"));
		if (comment >= 0) {
			line = line.left(comment).trimmed();
		}
		if (line.isEmpty()) {
			continue;
		} else if (line == QLatin1String("---functions---")) {
			method = true;
			continue;
		} else if (line == QLatin1String("---types---")) {
			method = false;
			continue;
		}
		statement += QLatin1Char(' ') + line;
		while (true) {
			const auto end = statement.indexOf(QLatin1Char(';'));
			if (end < 0) {
				break;
			}
			const auto one = statement.left(end).trimmed();
			statement = statement.mid(end + 1);
			if ((!method || allowMethods) && parseCombinator(one, method)) {
				++count;
			}
		}
	}
	return count;
}

bool Schema::parseCombinator(const QString &text, bool method) {
	const auto equals = text.indexOf(QLatin1Char('='));
	if (equals < 0) {
		return false;
	}
	const auto left = text.left(equals).simplified();
	const auto right = text.mid(equals + 1).trimmed();
	const auto tokens = left.split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (tokens.isEmpty()) {
		return false;
	}
	const auto &head = tokens.front();
	const auto hash = head.indexOf(QLatin1Char('#'));
	if (hash <= 0) {
		return false;
	}
	auto parsed = false;
	const auto id = head.mid(hash + 1).toUInt(&parsed, 16);
	const auto name = head.left(hash);
	if (!parsed || name == QLatin1String("vector")) {
		return false;
	}
	auto result = std::make_unique<Combinator>();
	result->name = name;
	result->id = id;
	result->method = method;
	static const auto conditional = QRegularExpression(
		QStringLiteral("^([A-Za-z0-9_]+)\\.(\\d+)\\?(.+)$"));
	for (auto i = 1; i < tokens.size(); ++i) {
		const auto &token = tokens[i];
		if (token.startsWith(QLatin1Char('{')) && token.endsWith(QLatin1Char('}'))) {
			const auto colon = token.indexOf(QLatin1Char(':'));
			if (colon > 1) {
				result->generic = token.mid(1, colon - 1);
			}
			continue;
		}
		const auto colon = token.indexOf(QLatin1Char(':'));
		if (colon <= 0) {
			return false;
		}
		auto param = Param();
		param.name = token.left(colon);
		param.typeText = token.mid(colon + 1);
		auto typeText = param.typeText;
		const auto match = conditional.match(typeText);
		if (match.hasMatch()) {
			const auto field = match.captured(1);
			param.bit = match.captured(2).toInt();
			typeText = match.captured(3);
			for (auto j = 0; j != int(result->params.size()); ++j) {
				const auto &other = result->params[j];
				if (other.name == field && other.type.kind == TypeKind::Flags) {
					param.flagsIndex = j;
					break;
				}
			}
			if (param.flagsIndex < 0 || param.bit < 0 || param.bit > 31) {
				return false;
			}
		}
		param.type = ParseType(typeText, result->generic);
		result->params.push_back(std::move(param));
	}
	result->typeText = right;
	result->type = ParseType(right, result->generic);

	const auto raw = result.get();
	if (method) {
		if (_methodsByName.contains(name)) {
			return false;
		}
		_methodsByName.insert(name, raw);
		_methodsById.insert(id, raw);
	} else {
		if (_constructorsByName.contains(name)) {
			return false;
		}
		_constructorsByName.insert(name, raw);
		_constructorsById.insert(id, raw);
		_byType[right].push_back(raw);
	}
	_list.push_back(std::move(result));
	return true;
}

const Combinator *Schema::constructor(const QString &name) const {
	return _constructorsByName.value(name, nullptr);
}

const Combinator *Schema::constructor(quint32 id) const {
	return _constructorsById.value(id, nullptr);
}

const Combinator *Schema::method(const QString &name) const {
	return _methodsByName.value(name, nullptr);
}

const Combinator *Schema::method(quint32 id) const {
	return _methodsById.value(id, nullptr);
}

const std::vector<const Combinator*> &Schema::constructorsOf(
		const QString &type) const {
	static const auto empty = std::vector<const Combinator*>();
	const auto i = _byType.constFind(type);
	return (i != _byType.cend()) ? *i : empty;
}

QStringList Schema::search(const QString &query, int limit) const {
	auto result = QStringList();
	for (const auto &entry : _list) {
		if (entry->name.contains(query, Qt::CaseInsensitive)) {
			result.push_back(entry->name);
		}
	}
	std::sort(result.begin(), result.end(), [&](
			const QString &a,
			const QString &b) {
		const auto aExact = a.compare(query, Qt::CaseInsensitive) == 0;
		const auto bExact = b.compare(query, Qt::CaseInsensitive) == 0;
		if (aExact != bExact) {
			return aExact;
		}
		return (a.size() != b.size()) ? (a.size() < b.size()) : (a < b);
	});
	if (limit > 0 && result.size() > limit) {
		result = result.mid(0, limit);
	}
	return result;
}

int Schema::layer() const {
	return _layer;
}

int Schema::constructorsCount() const {
	return int(_constructorsByName.size());
}

int Schema::methodsCount() const {
	return int(_methodsByName.size());
}

const Schema &Schema::Embedded() {
	static const auto instance = [] {
		auto result = std::make_unique<Schema>();
		result->add(EmbeddedApiSchema(), true);
		const auto layer = result->layer();
		result->add(EmbeddedMtprotoSchema(), false);
		result->_layer = layer ? layer : EmbeddedLayer();
		return result;
	}();
	return *instance;
}

} // namespace Damza::Tl
