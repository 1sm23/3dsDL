#include "crash.hpp"

#include "crash_log.hpp"

#include <3ds.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <ctime>
#include <exception>

namespace crash {

namespace {

// The handler's own stack, since the crashed thread's may be what broke.
alignas(8) uint8_t handlerStack[0x4000];
char text[4096];
crashlog::ScreenTrail trail;
u32 *mainTls = nullptr;
bool runningAsCia = false;
uint32_t codeStart = 0;
uint32_t codeEnd = 0;
std::atomic_flag handling = ATOMIC_FLAG_INIT;

u32 *threadTls()
{
    return static_cast<u32 *>(getThreadLocalStorage());
}

// Clears the handler (TLS + 0x40) here and on the main thread, so a crash
// while writing the log goes to Luma3DS's own exception screen.
void uninstall()
{
    mainTls[0x10] = 0;
    threadTls()[0x10] = 0;
}

// The memory block holding address, when it can be read.
bool readableBlock(uint32_t address, uint32_t &start, uint32_t &end)
{
    MemInfo info {};
    PageInfo page {};
    if (R_FAILED(svcQueryMemory(&info, &page, address)) ||
        (info.perm & MEMPERM_READ) == 0) {
        return false;
    }
    start = info.base_addr;
    end = info.base_addr + info.size;
    return true;
}

void describe(crashlog::Record &record, const char *what, uint32_t sp)
{
    record.version = APP_VERSION;
    record.cia = runningAsCia;
    record.time = std::time(nullptr);
    record.what = what;
    record.thread = threadTls() == mainTls ? "main" : "worker";
    record.screenCount = trail.newestFirst(record.screens);
    record.freeBytes = osGetMemRegionFree(MEMREGION_APPLICATION);
    uint32_t start = 0;
    uint32_t end = 0;
    if (sp % 4 == 0 && readableBlock(sp, start, end)) {
        const uint32_t bytes = std::min<uint32_t>(end - sp, 0x2000);
        record.stackCount = crashlog::codePointers(
            reinterpret_cast<const uint32_t *>(sp), bytes / 4, codeStart,
            codeEnd, record.stack, crashlog::StackCount);
    }
}

// Straight through FS, as stdio would allocate.
void save(const crashlog::Record &record)
{
    const size_t length = crashlog::format(record, text, sizeof(text));
    Handle file = 0;
    if (R_FAILED(FSUSER_OpenFileDirectly(
            &file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
            fsMakePath(PATH_ASCII, LogPath + 5 /* without "sdmc:" */),
            FS_OPEN_WRITE | FS_OPEN_CREATE, 0))) {
        return;
    }
    u32 written = 0;
    FSFILE_SetSize(file, 0);
    FSFILE_Write(file, &written, 0, text, static_cast<u32>(length),
                 FS_WRITE_FLUSH);
    FSFILE_Close(file);
}

void onException(ERRF_ExceptionInfo *exception, CpuRegisters *registers)
{
    if (!handling.test_and_set()) {
        uninstall();
        crashlog::Record record;
        record.hasRegisters = true;
        std::copy(registers->r, registers->r + 13, record.r);
        record.sp = registers->sp;
        record.lr = registers->lr;
        record.pc = registers->pc;
        record.cpsr = registers->cpsr;
        record.hasFault = exception->type == ERRF_EXCEPTION_PREFETCH_ABORT ||
                          exception->type == ERRF_EXCEPTION_DATA_ABORT;
        record.far = exception->far;
        record.fsr = exception->fsr;
        describe(record, crashlog::exceptionName(exception->type),
                 registers->sp);
        save(record);
    }
    // The same registers on the system's error screen, which Luma3DS also
    // adds to /luma/errdisp.txt.
    ERRF_ExceptionHandler(exception, registers);
}

void onTerminate()
{
    if (!handling.test_and_set()) {
        uninstall();
        uint32_t sp = 0;
        asm volatile("mov %0, sp" : "=r"(sp));
        crashlog::Record record;
        describe(record, "terminate", sp);
        save(record);
    }
    std::abort();
}

} // namespace

void install(bool cia)
{
    runningAsCia = cia;
    mainTls = threadTls();
    // Return addresses on the stack point into the code, which is one
    // block: the one holding this function.
    readableBlock(reinterpret_cast<uint32_t>(&install), codeStart, codeEnd);
    // Luma3DS checks that the stack top is mapped, so it points inside.
    threadOnException(onException, handlerStack + sizeof(handlerStack) - 8,
                      WRITE_DATA_TO_HANDLER_STACK);
    std::set_terminate(onTerminate);
}

void noteScreen(const std::string &id)
{
    trail.note(id.c_str());
}

} // namespace crash
