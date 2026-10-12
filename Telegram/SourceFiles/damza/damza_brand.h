// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#pragma once

struct TextWithEntities;

namespace Damza::Brand {

inline constexpr auto kName = "DamZaGram";
inline constexpr auto kFullName = "DamZaGram Desktop";
inline constexpr auto kAuthorUsername = "wetakush";
inline constexpr auto kAuthorLink = "https://t.me/wetakush";
inline constexpr auto kUpstreamLink = "https://github.com/AyuGram/AyuGramDesktop";
inline constexpr auto kTelegramLink = "https://github.com/telegramdesktop/tdesktop";
inline constexpr auto kLicenseLink = "https://www.gnu.org/licenses/gpl-3.0.html";

[[nodiscard]] QString Name();
[[nodiscard]] QString FullName();
[[nodiscard]] QString AuthorHandle();
[[nodiscard]] QString AuthorLink();

// "AyuGram" -> "DamZaGram" in a user-visible string.
[[nodiscard]] QString Rebrand(QString text);

// Downloaded AyuGram translations: rename the product, keep the credits.
[[nodiscard]] QString TranslatedValue(const QString &key, QString value);

// Strings DamZaGram words differently from AyuGram, in the current language.
void ApplyLanguageOverrides();

[[nodiscard]] TextWithEntities AboutText();

} // namespace Damza::Brand
