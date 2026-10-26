#include "../check.hpp"

#include "crash_log.hpp"

#include <string>

TEST("crash log: the screen trail keeps the newest, newest first")
{
    crashlog::ScreenTrail trail;
    const char *screens[crashlog::ScreenCount] = {};
    CHECK_EQ(trail.newestFirst(screens), size_t(0));
    for (int index = 1; index <= 8; ++index) {
        trail.note(("screen " + std::to_string(index)).c_str());
    }
    CHECK_EQ(trail.newestFirst(screens), crashlog::ScreenCount);
    CHECK_EQ(std::string(screens[0]), std::string("screen 8"));
    CHECK_EQ(std::string(screens[5]), std::string("screen 3"));

    const std::string longName(200, 'x');
    trail.note(longName.c_str());
    trail.newestFirst(screens);
    CHECK_EQ(std::string(screens[0]).size(), crashlog::ScreenLength - 1);
}

TEST("crash log: code pointers are the stack words inside the code")
{
    const uint32_t stack[] = {0x00000000, 0x00100000, 0x0FFFFF00, 0x001FFFFC,
                              0x00200000, 0x00123456};
    uint32_t found[4] = {};
    CHECK_EQ(crashlog::codePointers(stack, 6, 0x00100000, 0x00200000, found, 4),
             size_t(3));
    CHECK_EQ(found[0], uint32_t(0x00100000));
    CHECK_EQ(found[1], uint32_t(0x001FFFFC));
    CHECK_EQ(found[2], uint32_t(0x00123456));
    CHECK_EQ(crashlog::codePointers(stack, 6, 0x00100000, 0x00200000, found, 1),
             size_t(1));
}

TEST("crash log: an exception is written as key and value lines")
{
    crashlog::Record record;
    record.version = "0.8.2-beta.6";
    record.cia = true;
    record.time = 1792765385; // 2026-10-23 14:23:05
    record.what = crashlog::exceptionName(1);
    record.thread = "worker";
    const char *screens[] = {"list|Software", "titles"};
    record.screens[0] = screens[0];
    record.screens[1] = screens[1];
    record.screenCount = 2;
    record.freeBytes = 1234;
    record.hasRegisters = true;
    record.pc = 0x0010ABCD;
    record.lr = 0x00110000;
    record.sp = 0x0FFFFE00;
    record.cpsr = 0x60000010;
    record.hasFault = true;
    record.far = 0x00000004;
    record.fsr = 0x00000805;
    record.r[12] = 0xDEADBEEF;
    for (uint32_t index = 0; index < 9; ++index) {
        record.stack[index] = 0x00100000 + index * 4;
    }
    record.stackCount = 9;

    char text[2048];
    const size_t length = crashlog::format(record, text, sizeof(text));
    const std::string written(text, length);
    CHECK_EQ(written.find("3dsDL crash\nversion: 0.8.2-beta.6\nbuild: CIA\n"),
             size_t(0));
    CHECK(written.find("time: 2026-10-23 14:23:05\n") != std::string::npos);
    CHECK(written.find("what: data abort\nthread: worker\n") != std::string::npos);
    CHECK(written.find("screen: list|Software\nscreen: titles\n") !=
          std::string::npos);
    CHECK(written.find("pc: 0010ABCD\nlr: 00110000\n") != std::string::npos);
    CHECK(written.find("far: 00000004\nfsr: 00000805\n") != std::string::npos);
    CHECK(written.find("r12: DEADBEEF\n") != std::string::npos);
    CHECK(written.find("stack: 00100000 00100004 00100008 0010000C 00100010 "
                       "00100014 00100018 0010001C\nstack: 00100020\n") !=
          std::string::npos);
    CHECK_EQ(written.back(), '\n');
}

TEST("crash log: without registers only the stack says where")
{
    crashlog::Record record;
    record.version = "0.8.2";
    record.what = "terminate";
    record.thread = "main";
    record.stack[0] = 0x00104000;
    record.stackCount = 1;
    char text[512];
    const std::string written(text, crashlog::format(record, text, sizeof(text)));
    CHECK(written.find("build: 3DSX\n") != std::string::npos);
    CHECK(written.find("time:") == std::string::npos);
    CHECK(written.find("pc:") == std::string::npos);
    CHECK(written.find("stack: 00104000\n") != std::string::npos);
}

TEST("crash log: the last log lines follow the stack")
{
    crashlog::Record record;
    record.version = "0.8.3";
    record.what = "abort";
    record.thread = "titles";
    record.stack[0] = 0x00104000;
    record.stackCount = 1;
    const char *lines[] = {"2026-10-26 14:03:20 I screen id=titles",
                           "2026-10-26 14:03:22 T titles.step step=extdata"};
    record.log = lines;
    record.logCount = 2;
    char text[512];
    const std::string written(text, crashlog::format(record, text, sizeof(text)));
    CHECK(written.find("thread: titles\n") != std::string::npos);
    CHECK(written.find("stack: 00104000\nlog: 2026-10-26 14:03:20 I screen "
                       "id=titles\nlog: 2026-10-26 14:03:22 T titles.step "
                       "step=extdata\n") != std::string::npos);
}

TEST("crash log: a small buffer keeps whole lines")
{
    crashlog::Record record;
    record.version = "0.8.2";
    record.what = "data abort";
    char text[40];
    const size_t length = crashlog::format(record, text, sizeof(text));
    const std::string written(text, length);
    CHECK_EQ(written, std::string("3dsDL crash\nversion: 0.8.2\nbuild: 3DSX\n"));
    CHECK_EQ(text[length], '\0');
}
