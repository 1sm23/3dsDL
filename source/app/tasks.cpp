#include "tasks.hpp"

#include "ui.hpp"
#include "units.hpp"
#include "url.hpp"

#include <3ds.h>

#include <algorithm>
#include <atomic>

namespace tasks {
namespace {

// curl and mbedTLS need a generous stack for the TLS handshake.
constexpr size_t WorkerStackSize = 256 * 1024;

class Guard {
public:
    explicit Guard(LightLock &lock) : lock_(lock) { LightLock_Lock(&lock_); }
    ~Guard() { LightLock_Unlock(&lock_); }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;

private:
    LightLock &lock_;
};

// Keeps the console out of sleep mode for as long as it exists. Sleep turns
// Wi-Fi off, which would break the transfer.
class StayAwake {
public:
    explicit StayAwake(bool active) : active_(active)
    {
        if (active_) {
            aptSetSleepAllowed(false);
        }
    }
    ~StayAwake()
    {
        if (active_) {
            aptSetSleepAllowed(true);
        }
    }

private:
    bool active_;
};

// The worker runs one step below the main thread's priority so drawing and
// the cancel button stay responsive even while TLS keeps the CPU busy.
int workerPriority()
{
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    return std::min(0x3f, static_cast<int>(priority) + 1);
}

// On a New 3DS the worker gets the third core, which nothing else uses, so
// TLS and inflating never wait for drawing. Elsewhere, or when the system
// refuses, it shares the app core with the main thread.
Thread startWorker(ThreadFunc entry, void *argument)
{
    bool isNew3ds = false;
    if (R_SUCCEEDED(APT_CheckNew3DS(&isNew3ds)) && isNew3ds) {
        if (Thread thread = threadCreate(entry, argument, WorkerStackSize,
                                         workerPriority(), 2, false)) {
            return thread;
        }
    }
    return threadCreate(entry, argument, WorkerStackSize, workerPriority(),
                        -2, false);
}

bool confirmStop(const std::string &what)
{
    ui::Dialog dialog;
    dialog.header = "cancel";
    dialog.title = "Stop " + what + "?";
    dialog.tone = ui::Tone::Warning;
    dialog.body.push_back({"It keeps running in the background while this "
                           "question is open.",
                           ui::color::Muted, 0.48f, 2});
    dialog.buttons = {"KEEP GOING", "STOP"};
    dialog.dangerous = 1;
    dialog.cancelButton = 0;
    return ui::choose(dialog) == 1;
}

bool wantsCancel(const ui::Input &input)
{
    return (input.down & KEY_B) != 0 ||
           input.tapped(ui::ProgressCancelButton);
}

std::string footer(bool keepAwake)
{
    return keepAwake ? "Sleep mode is paused until this finishes."
                     : "Closing the lid will interrupt this.";
}

struct DownloadJob : net::Observer {
    const net::Request *request = nullptr;
    net::Result result;
    std::atomic<bool> cancel {false};
    std::atomic<bool> closing {false};
    std::atomic<bool> finished {false};

    LightLock lock;
    net::Phase phase = net::Phase::Connecting;
    std::string filename;
    uint64_t received = 0;
    uint64_t total = 0;
    uint64_t resumedFrom = 0;
    int connections = 0;

    DownloadJob() { LightLock_Init(&lock); }

    bool cancelRequested() override { return cancel.load(); }
    bool keepPartialOnCancel() override { return closing.load(); }
    void onPhase(net::Phase value) override
    {
        Guard guard(lock);
        phase = value;
        if (value == net::Phase::Restarting) {
            resumedFrom = 0;
            received = 0;
            total = 0;
        }
    }
    void onFilename(const std::string &name) override
    {
        Guard guard(lock);
        filename = name;
    }
    void onProgress(uint64_t now, uint64_t expected) override
    {
        Guard guard(lock);
        received = now;
        total = expected;
    }
    void onResumed(uint64_t offset) override
    {
        Guard guard(lock);
        resumedFrom = offset;
    }
    void onConnections(int count) override
    {
        Guard guard(lock);
        connections = count;
    }
};

void runDownload(void *argument)
{
    DownloadJob &job = *static_cast<DownloadJob *>(argument);
    job.result = net::download(*job.request, job);
    job.finished = true;
}

struct ExtractJob : zip::Observer {
    std::string zipPath;
    const zip::Archive *archive = nullptr;
    std::string folder;
    zip::Existing existing = zip::Existing::Skip;
    zip::Result result;
    std::atomic<bool> cancel {false};
    std::atomic<bool> closing {false};
    std::atomic<bool> finished {false};

    LightLock lock;
    uint64_t done = 0;
    std::string current;

    ExtractJob() { LightLock_Init(&lock); }

    bool cancelRequested() override { return cancel.load(); }
    void onProgress(uint64_t now, uint64_t, const std::string &name) override
    {
        Guard guard(lock);
        done = now;
        current = name;
    }
};

void runExtract(void *argument)
{
    ExtractJob &job = *static_cast<ExtractJob *>(argument);
    job.result = zip::extract(job.zipPath, *job.archive, job.folder,
                              job.existing, job);
    job.finished = true;
}

// Waits for the worker while drawing frames with draw(). Returns once the
// worker has finished, having asked it to stop if the user cancelled.
template <typename Job, typename Draw>
void supervise(Job &job, Thread thread, const std::string &what, Draw draw)
{
    // 20 frames a second is plenty for a progress screen, and on an Old 3DS
    // the time not spent drawing goes to the worker.
    constexpr unsigned FramesPerDraw = 3;
    unsigned frame = 0;
    while (!job.finished) {
        if (!ui::keepRunning()) {
            job.closing = true;
            job.cancel = true;
            break;
        }
        const ui::Input input = ui::readInput();
        if (!job.cancel && wantsCancel(input) && confirmStop(what)) {
            job.cancel = true;
        }
        if (frame++ % FramesPerDraw == 0) {
            draw();
        }
        gspWaitForVBlank();
    }
    threadJoin(thread, U64_MAX);
    threadFree(thread);
}

struct BusyJob {
    const std::function<void()> *work = nullptr;
    std::atomic<bool> finished {false};
};

void runBusy(void *argument)
{
    BusyJob &job = *static_cast<BusyJob *>(argument);
    (*job.work)();
    job.finished = true;
}

} // namespace

void busy(const std::string &header, const std::string &title,
          const std::function<void()> &work)
{
    BusyJob job;
    job.work = &work;
    Thread thread = startWorker(runBusy, &job);
    if (thread == nullptr) {
        work();
        return;
    }
    while (!job.finished) {
        ui::keepRunning();
        ui::readInput();
        ui::drawBusy(header, title);
        gspWaitForVBlank();
    }
    threadJoin(thread, U64_MAX);
    threadFree(thread);
}

net::Result download(const net::Request &request, bool keepAwake)
{
    DownloadJob job;
    job.request = &request;

    StayAwake awake(keepAwake);
    Thread thread = startWorker(runDownload, &job);
    if (thread == nullptr) {
        job.result.message = "Could not start the download thread.";
        return job.result;
    }

    const std::string host = url::host(request.url);
    units::RateMeter meter;
    supervise(job, thread, "this download", [&]() {
        ui::ProgressView view;
        {
            Guard guard(job.lock);
            view.title = job.filename.empty() ? host : job.filename;
            view.status = job.cancel ? "stopping"
                        : job.phase == net::Phase::Connecting  ? "connecting"
                        : job.phase == net::Phase::Restarting ? "starting over"
                        : job.connections > 1
                            ? std::to_string(job.connections) + " connections"
                            : "downloading";
            if (job.resumedFrom > 0) {
                view.note = "Continuing from " +
                            units::bytes(job.resumedFrom) +
                            " downloaded earlier";
            }
            meter.sample(osGetTime(), job.received);
            if (job.total > 0) {
                view.fraction = static_cast<float>(
                    static_cast<double>(job.received) /
                    static_cast<double>(job.total));
                view.amounts = units::bytes(job.received) + " of " +
                               units::bytes(job.total) + " (" +
                               std::to_string(static_cast<int>(
                                   view.fraction * 100.0f)) +
                               "%)";
            } else {
                view.amounts = units::bytes(job.received);
            }
            if (meter.hasRate()) {
                view.speed = units::rate(meter.bytesPerSecond());
                const int64_t left = job.total > job.received
                    ? meter.secondsLeft(job.total - job.received)
                    : -1;
                if (left >= 0) {
                    view.speed += ", " + units::duration(left) + " left";
                }
            } else if (job.phase == net::Phase::Downloading) {
                view.speed = "Measuring speed...";
            }
        }
        view.header = "downloading";
        view.subtitle = host;
        view.folder = request.folder;
        view.footer = footer(keepAwake);
        ui::drawProgress(view);
    });
    return job.result;
}

zip::Result extract(const std::string &zipPath, const zip::Archive &archive,
                    const std::string &folder, zip::Existing existing,
                    bool keepAwake)
{
    ExtractJob job;
    job.zipPath = zipPath;
    job.archive = &archive;
    job.folder = folder;
    job.existing = existing;

    StayAwake awake(keepAwake);
    Thread thread = startWorker(runExtract, &job);
    if (thread == nullptr) {
        job.result.message = "Could not start the extraction thread.";
        return job.result;
    }

    const std::string name =
        zipPath.substr(zipPath.find_last_of('/') + 1);
    supervise(job, thread, "extracting", [&]() {
        ui::ProgressView view;
        {
            Guard guard(job.lock);
            view.subtitle = job.current.empty() ? "Reading the archive..."
                                                : job.current;
            const uint64_t total = archive.totalSize;
            view.fraction = total > 0
                ? static_cast<float>(static_cast<double>(job.done) /
                                     static_cast<double>(total))
                : -1.0f;
            view.amounts = units::bytes(job.done) + " of " +
                           units::bytes(total);
        }
        view.header = "extracting";
        view.title = name;
        view.status = job.cancel ? "stopping" : "extracting";
        view.speed = std::to_string(archive.fileCount) + " files";
        view.folder = folder;
        view.footer = footer(keepAwake);
        ui::drawProgress(view);
    });
    return job.result;
}

} // namespace tasks
