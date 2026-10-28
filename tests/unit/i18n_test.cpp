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
        {"zh-Hant", "繁體中文"},
        {"es", "Español"}, {"fr", "Français"},
        {"pt", "Português"}, {"de", "Deutsch"}, {"ja", "日本語"},
        {"it", "Italiano"}, {"ko", "한국어"}, {"ru", "Русский"},
        {"pt-BR", "Português (Brasil)"},
    };
    CHECK_EQ(i18n::languages().size(), size_t(12));
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
                 language.first == "zh-Hant" ? std::string("設定")
                 : language.first == "es" ? std::string("Configuración")
                 : language.first == "fr" ? std::string("Paramètres")
                 : language.first == "pt" ? std::string("Configurações")
                 : language.first == "de" ? std::string("Einstellungen")
                 : language.first == "ja" ? std::string("設定")
                 : language.first == "it" ? std::string("Impostazioni")
                 : language.first == "ko" ? std::string("설정")
                 : language.first == "ru" ? std::string("Настройки")
                                            : std::string("Configurações"));
    }
    i18n::setLanguage("en");
}

TEST("i18n: Russian count forms and Brazilian Portuguese are distinct")
{
    CHECK(i18n::init("../romfs/i18n.json"));
    i18n::setLanguage("ru");
    const std::vector<std::pair<uint64_t, std::string>> forms = {
        {0, "0 файлов"}, {1, "1 файл"}, {2, "2 файла"}, {4, "4 файла"},
        {5, "5 файлов"}, {11, "11 файлов"}, {12, "12 файлов"},
        {14, "14 файлов"}, {21, "21 файл"}, {22, "22 файла"},
        {101, "101 файл"}, {111, "111 файлов"}, {112, "112 файлов"},
    };
    for (const auto &form : forms) {
        CHECK_EQ(i18n::count(form.first, "file", std::to_string(form.first)), form.second);
    }
    CHECK_EQ(i18n::count(2, "entry", "2"), std::string("2 записи"));
    CHECK_EQ(i18n::count(5, "entry", "5"), std::string("5 записей"));
    i18n::setLanguage("pt-BR");
    CHECK_EQ(i18n::language(), std::string("pt-BR"));
    CHECK_EQ(i18n::count(0, "file", "0"), std::string("0 arquivo"));
    CHECK_EQ(i18n::count(1, "file", "1"), std::string("1 arquivo"));
    CHECK_EQ(i18n::count(2, "file", "2"), std::string("2 arquivos"));
    CHECK_EQ(i18n::t("settings.clock.title"), std::string("Relógio do console"));
    i18n::setLanguage("pt");
    CHECK_EQ(i18n::language(), std::string("pt"));
    CHECK_EQ(i18n::t("settings.clock.title"), std::string("Relógio da consola"));
    CHECK_EQ(i18n::count(0, "file", "0"), std::string("0 arquivos"));
    i18n::setLanguage("ru");
    const std::string russianHtml = remote::page("", true);
    CHECK(russianHtml.find("\"{0} items.few\":\"{0} элемента\"") != std::string::npos);
    i18n::setLanguage("en");
}

TEST("i18n: new locales reach the console catalog and Receive page")
{
    CHECK(i18n::init("../romfs/i18n.json"));
    const std::vector<std::pair<std::string, std::string>> web = {
        {"zh-Hant", "傳送至 3DS"},
        {"it", "Invia al 3DS"}, {"ko", "3DS로 보내기"},
        {"ru", "Отправить на 3DS"}, {"pt-BR", "Enviar para o 3DS"},
    };
    for (const auto &locale : web) {
        i18n::setLanguage(locale.first);
        const std::string html = remote::page("", true);
        CHECK(html.find("lang=\"" + locale.first + "\"") != std::string::npos);
        CHECK(html.find(locale.second + "</button>") != std::string::npos);
    }
    i18n::setLanguage("ko");
    CHECK_EQ(i18n::count(1, "file", "1"), std::string("파일 1개"));
    CHECK_EQ(i18n::count(2, "file", "2"), std::string("파일 2개"));
    i18n::setLanguage("zh-Hant");
    CHECK_EQ(i18n::language(), std::string("zh-Hant"));
    CHECK_EQ(i18n::t("settings.clock.title"), std::string("主機時鐘"));
    CHECK_EQ(i18n::count(2, "file", "2"), std::string("2 個檔案"));
    i18n::setLanguage("zh");
    CHECK_EQ(i18n::t("settings.clock.title"), std::string("主机时钟"));
    CHECK_EQ(i18n::count(2, "file", "2"), std::string("2 个文件"));
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
