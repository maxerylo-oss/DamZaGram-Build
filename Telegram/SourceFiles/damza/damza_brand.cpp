// This is the source code of DamZaGram for Desktop,
// a fork of AyuGram for Desktop.
#include "damza/damza_brand.h"

#include "damza/damza_lang.h"
#include "lang/lang_instance.h"
#include "lang/lang_text_entity.h"
#include "ui/text/text_utilities.h"

namespace Damza::Brand {
namespace {

struct Override {
	const char *key = nullptr;
	const char *en = nullptr;
	const char *ru = nullptr;
};

constexpr Override kOverrides[] = {
	{
		"ayu_SettingsWatermark",
		"DamZaGram by @wetakush. Based on AyuGram by Radolyn Labs.",
		"DamZaGram от @wetakush. Основан на AyuGram от Radolyn Labs.",
	},
	{
		"ayu_SettingsDescription",
		"AyuGram fork with JavaScript plugins, ghost mode "
		"and deep customization.",
		"Форк AyuGram с JavaScript-плагинами, режимом призрака "
		"и глубокой кастомизацией.",
	},
	{
		"ayu_FirstLaunchAlert",
		"DamZaGram is **free** software by **@wetakush**, based on AyuGram. "
		"Get it only from the author. You assume **full responsibility** "
		"for using this application with your account.",
		"DamZaGram — **бесплатная** программа от **@wetakush** на основе "
		"AyuGram. Скачивайте её только у автора. Вы берёте на себя "
		"**полную ответственность** за использование этого приложения "
		"с вашим аккаунтом.",
	},
	{
		"ayu_ExteraChatsAlert",
		"Don't ask questions related to **DamZaGram** in **exteraGram** "
		"or **AyuGram** chats. If you need help, contact **@wetakush**.",
		"Не задавайте вопросы о **DamZaGram** в чатах **exteraGram** "
		"и **AyuGram**. Если нужна помощь — пишите **@wetakush**.",
	},
	{
		"ayu_PluginsNotAvailable",
		"exteraGram plugins are Android-only. DamZaGram runs its own "
		"JavaScript plugins: Settings > DamZaGram Preferences > Plugins.",
		"Плагины exteraGram работают только на Android. У DamZaGram свои "
		"плагины на JavaScript: Настройки > Настройки DamZaGram > Плагины.",
	},
};

// These name AyuGram on purpose: badges and resources of the upstream.
constexpr const char *kKeepUpstream[] = {
	"ayu_SupporterPopup",
	"ayu_OfficialResourcePopup",
	"ayu_DeveloperPopup",
	"ayu_IconAlternative",
};

[[nodiscard]] QString BaseKey(const QString &key) {
	const auto hash = key.indexOf('#');
	return (hash >= 0) ? key.mid(0, hash) : key;
}

} // namespace

QString Name() {
	return QString::fromLatin1(kName);
}

QString FullName() {
	return QString::fromLatin1(kFullName);
}

QString AuthorHandle() {
	return u"@"_q + QString::fromLatin1(kAuthorUsername);
}

QString AuthorLink() {
	return QString::fromLatin1(kAuthorLink);
}

QString Rebrand(QString text) {
	return text
		.replace(u"AyuGram"_q, Name())
		.replace(u"Ayugram"_q, Name());
}

QString TranslatedValue(const QString &key, QString value) {
	const auto base = BaseKey(key);
	for (const auto keep : kKeepUpstream) {
		if (base == QLatin1String(keep)) {
			return value;
		}
	}
	return Rebrand(std::move(value));
}

void ApplyLanguageOverrides() {
	auto &instance = Lang::GetInstance();
	for (const auto &entry : kOverrides) {
		const auto key = QByteArray(entry.key);
		instance.resetValue(key);
		instance.applyValue(key, Damza::Text(entry.en, entry.ru).toUtf8());
	}
}

TextWithEntities AboutText() {
	const auto link = [](const QString &text, const char *url) {
		return tr::link(text, QString::fromLatin1(url));
	};
	auto result = TextWithEntities();
	result.append(tr::bold(FullName()));
	result.append(Damza::Text(" by ", " от "));
	result.append(link(AuthorHandle(), kAuthorLink));
	result.append(Damza::Text(
		" — a Telegram client with JavaScript plugins, ghost mode "
		"and deep customization.",
		" — клиент Telegram с JavaScript-плагинами, режимом призрака "
		"и глубокой кастомизацией."));
	result.append(u"\n\n"_q);
	result.append(Damza::Text("Based on ", "Основан на "));
	result.append(link(u"AyuGram"_q, kUpstreamLink));
	result.append(Damza::Text(" by Radolyn Labs and ", " от Radolyn Labs и "));
	result.append(link(u"Telegram Desktop"_q, kTelegramLink));
	result.append(Damza::Text(" by Telegram FZ-LLC.", " от Telegram FZ-LLC."));
	result.append(u"\n\n"_q);
	result.append(Damza::Text(
		"This software is licensed under ",
		"Эта программа распространяется по лицензии "));
	result.append(link(u"GNU GPL"_q, kLicenseLink));
	result.append(Damza::Text(" version 3.", " версии 3."));
	return result;
}

} // namespace Damza::Brand
