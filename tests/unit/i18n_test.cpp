#include "../check.hpp"

#include "fs_util.hpp"
#include "i18n.hpp"
#include "remote.hpp"
#include "timezone.hpp"

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

TEST("i18n: arguments are literal, catalog pointers survive language changes")
{
    const std::string path = "build/i18n-format-test.json";
    CHECK(fs::writeFile(path, R"JSON({
      "ordered": {"en":"{1}: {0}, {1}; {99}", "zh":"{0} / {1}"},
      "{0} file": {"en":"{0} file", "fr":"{0} fichier", "zh":"{0} 个文件"},
      "{0} files": {"en":"{0} files", "fr":"{0} fichiers", "zh":"{0} 个文件"},
      "{0} entries": {"en":"{0} entries"},
      "Send to 3DS": {"en":"Send to 3DS", "zh":"发送 <到> 3DS"},
      "Cancel": {"en":"Cancel", "zh":"</script><img src=x>"}
    })JSON"));
    CHECK(i18n::init(path));
    i18n::setLanguage("en");
    CHECK_EQ(i18n::t("ordered", {"file-{1}.cia", "B"}),
             std::string("B: file-{1}.cia, B; {99}"));
    CHECK_EQ(i18n::t("ordered", {}), std::string("{1}: {0}, {1}; {99}"));
    const char *english = i18n::c("Send to 3DS");
    i18n::setLanguage("zh");
    CHECK_EQ(std::string(english), std::string("Send to 3DS"));
    CHECK_EQ(std::string(i18n::c("unknown")), std::string("unknown"));
    CHECK_EQ(i18n::count(2, "file", "2"), std::string("2 个文件"));
    i18n::setLanguage("fr");
    CHECK_EQ(i18n::count(0, "file", "0"), std::string("0 fichier"));
    CHECK_EQ(i18n::count(2, "file", "2"), std::string("2 fichiers"));
    CHECK_EQ(i18n::count(2, "entry", "2"), std::string("2 entries"));
    i18n::setLanguage("zh");
    const std::string html = remote::page("@L7@ <file>", true);
    CHECK(html.find("lang=\"zh\"") != std::string::npos);
    CHECK(html.find("发送 &lt;到&gt; 3DS</button>") != std::string::npos);
    CHECK(html.find("@L7@ &lt;file&gt;") != std::string::npos);
    CHECK(html.find("</script><img src=x>") == std::string::npos);
    CHECK(html.find("\\u003c/script>") != std::string::npos);
    fs::removeFile(path);
    CHECK(i18n::init("../romfs/i18n.json"));
    CHECK_EQ(tz::describeDrift(0, 600), i18n::t("{0} minutes behind", {"10"}));
    i18n::setLanguage("en");
}
