#include "../check.hpp"

#include "fs_util.hpp"
#include "i18n.hpp"

TEST("i18n: language lookup, fallback and placeholders")
{
    const std::string path = "build/i18n-test.json";
    const std::string catalog = R"JSON({
      "greeting": {"en": "Hello, {0}!", "zh": "你好，{0}！"},
      "ordered": {"en": "{0} then {1}", "zh": "{1}，然后{0}"},
      "fallback": {"en": "English only"}
    })JSON";
    CHECK(fs::writeFile(path, catalog));
    CHECK(i18n::init(path));

    i18n::setLanguage("zh");
    CHECK_EQ(i18n::t("greeting", {"3dsDL"}),
             std::string("你好，3dsDL！"));
    CHECK_EQ(i18n::t("ordered", {"A", "B"}),
             std::string("B，然后A"));
    CHECK_EQ(i18n::t("fallback"), std::string("English only"));
    CHECK_EQ(i18n::t("missing.key"), std::string("missing.key"));
    CHECK_EQ(i18n::languageName("zh"), std::string("简体中文"));

    const std::vector<std::pair<std::string, std::string>> addedLanguages = {
        {"es", "Español"}, {"fr", "Français"},
        {"pt", "Português"}, {"de", "Deutsch"}, {"ja", "日本語"},
    };
    CHECK_EQ(i18n::languages().size(), size_t(7));
    for (const auto &language : addedLanguages) {
        CHECK(i18n::findLanguage(language.first) != nullptr);
        CHECK_EQ(i18n::languageName(language.first), language.second);
    }

    i18n::setLanguage("not-supported");
    CHECK_EQ(i18n::language(), std::string("en"));
    CHECK_EQ(i18n::t("greeting", {"3dsDL"}),
             std::string("Hello, 3dsDL!"));
    fs::removeFile(path);

    CHECK(i18n::init("../romfs/i18n.json"));
    i18n::setLanguage("zh");
    CHECK_EQ(i18n::t("settings.other.title"), std::string("其他设置"));
    CHECK_EQ(i18n::t("settings.sort.size"), std::string("大小，最大优先"));
    for (const auto &language : addedLanguages) {
        i18n::setLanguage(language.first);
        CHECK_EQ(i18n::t("settings.title"),
                 language.first == "es" ? std::string("Configuración")
                 : language.first == "fr" ? std::string("Paramètres")
                 : language.first == "pt" ? std::string("Configurações")
                 : language.first == "de" ? std::string("Einstellungen")
                                            : std::string("設定"));
    }
    i18n::setLanguage("en");
}
