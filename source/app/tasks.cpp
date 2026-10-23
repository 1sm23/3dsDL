#include "tasks.hpp"

#include "sound.hpp"
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
Thread startWorkerThread(ThreadFunc entry, void *argument)
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
    dialog.buttons = {"Keep Going", "Stop"};
    dialog.dangerous = 1;
    dialog.cancelButton = 0;
    return ui::choose(dialog) == 1;
}

bool wantsCancel(const ui::Input &input)
{
    return (input.down & KEY_B) != 0 ||
           input.tapped(ui::ProgressCancelButton);
}

std::string capitalised(std::string value)
{
    if (!value.empty() && value[0] >= 'a' && value[0] <= 'z') {
        value[0] = static_cast<char>(value[0] - 'a' + 'A');
    }
    return value;
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

struct InstallJob : installer::Observer {
    std::string path;
    uint64_t titleId = 0;
    installer::Result result;
    std::atomic<bool> cancel {false};
    std::atomic<bool> closing {false};
    std::atomic<bool> finished {false};

    LightLock lock;
    uint64_t done = 0;
    uint64_t total = 0;

    InstallJob() { LightLock_Init(&lock); }

    bool cancelRequested() override { return cancel.load(); }
    void onProgress(uint64_t now, uint64_t expected) override
    {
        Guard guard(lock);
        done = now;
        total = expected;
    }
};

void runInstall(void *argument)
{
    InstallJob &job = *static_cast<InstallJob *>(argument);
    job.result = installer::installCia(job.path, job.titleId, job);
    job.finished = true;
}

struct FileJob : fileops::Observer {
    const std::function<fileops::Result(fileops::Observer &)> *work = nullptr;
    fileops::Result result;
    std::atomic<bool> cancel {false};
    std::atomic<bool> closing {false};
    std::atomic<bool> finished {false};

    LightLock lock;
    uint64_t done = 0;
    uint64_t total = 0;
    std::string current;

    FileJob() { LightLock_Init(&lock); }

    bool cancelRequested() override { return cancel.load(); }
    void onProgress(uint64_t now, uint64_t expected,
                    const std::string &name) override
    {
        Guard guard(lock);
        done = now;
        total = expected;
        current = name;
    }
};

void runFileJob(void *argument)
{
    FileJob &job = *static_cast<FileJob *>(argument);
    job.result = (*job.work)(job);
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
    ui::enterScreen("progress|" + what);
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
    const std::function<void(Busy &)> *work = nullptr;
    Busy busy;
    std::atomic<bool> finished {false};
};

void runBusy(void *argument)
{
    BusyJob &job = *static_cast<BusyJob *>(argument);
    (*job.work)(job.busy);
    job.finished = true;
}

} // namespace

Thread startWorker(ThreadFunc entry, void *argument)
{
    return startWorkerThread(entry, argument);
}

void busy(const std::string &header, const std::string &title,
          const std::function<void()> &work)
{
    busy(header, title, [&](Busy &) { work(); }, false);
}

bool busy(const std::string &header, const std::string &title,
          const std::function<void(Busy &)> &work, bool cancellable)
{
    BusyJob job;
    job.work = &work;
    Thread thread = startWorkerThread(runBusy, &job);
    if (thread == nullptr) {
        work(job.busy);
        return !job.busy.cancel.load();
    }
    bool pressed = false;
    while (!job.finished) {
        if (!ui::keepRunning()) {
            job.busy.cancel = true;
        }
        const ui::Input input = ui::readInput();
        const bool stopping = job.busy.cancel.load();
        if (cancellable && !stopping) {
            bool cancel = (input.down & KEY_B) != 0;
            if (input.down & KEY_TOUCH) {
                pressed = ui::BusyCancelButton.contains(input.touch);
            }
            if ((input.up & KEY_TOUCH) && pressed) {
                pressed = false;
                cancel = cancel || ui::BusyCancelButton.contains(input.touch);
            }
            if (cancel) {
                job.busy.cancel = true;
                sound::play(sound::Effect::Back);
            }
        }

        ui::BusyView view;
        view.header = header;
        view.title = title;
        view.cancellable = cancellable;
        view.stopping = job.busy.cancel.load();
        view.pressed = pressed && !view.stopping &&
                       (input.held & KEY_TOUCH) &&
                       ui::BusyCancelButton.contains(input.touch);
        const uint64_t total = job.busy.total.load();
        if (total > 0) {
            const uint64_t done = std::min(job.busy.done.load(), total);
            view.progress = true;
            view.fraction = static_cast<float>(static_cast<double>(done) /
                                               static_cast<double>(total));
            view.amount = job.busy.bytes.load()
                              ? units::bytes(done) + " of " + units::bytes(total)
                              : std::to_string(done) + " of " +
                                    std::to_string(total);
        }
        ui::drawBusy(view);
        gspWaitForVBlank();
    }
    threadJoin(thread, U64_MAX);
    threadFree(thread);
    return !job.busy.cancel.load();
}

net::Result download(const net::Request &request, bool keepAwake,
                     const std::string &step, bool direct)
{
    DownloadJob job;
    job.request = &request;

    StayAwake awake(keepAwake);
    Thread thread = startWorkerThread(runDownload, &job);
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
            view.name = job.filename.empty() ? host : job.filename;
            view.title = job.phase == net::Phase::Connecting ? "Connecting"
                       : job.phase == net::Phase::Restarting
                           ? "Starting Over"
                       : direct ? "Installing"
                                : "Downloading";
            if (!step.empty()) {
                view.title += " " + step;
            }
            if (job.connections > 1) {
                view.tag = std::to_string(job.connections) + " connections";
            }
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
                               units::bytes(job.total);
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
        view.icon = ui::Icon::Download;
        view.iconColor = ui::color::IconGreen;
        view.source = "from " + host;
        view.message = direct ? "Installing as it downloads"
                              : "Saving to " + request.folder;
        view.stopping = job.cancel;
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
    Thread thread = startWorkerThread(runExtract, &job);
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
            view.source = job.current.empty() ? "Reading the archive..."
                                              : job.current;
            const uint64_t total = archive.totalSize;
            view.fraction = total > 0
                ? static_cast<float>(static_cast<double>(job.done) /
                                     static_cast<double>(total))
                : -1.0f;
            view.amounts = units::bytes(job.done) + " of " +
                           units::bytes(total);
        }
        view.title = "Extracting";
        view.icon = ui::Icon::Zip;
        view.iconColor = ui::color::IconGold;
        view.name = name;
        view.tag = std::to_string(archive.fileCount) + " files";
        view.message = "Extracting into " + folder;
        view.stopping = job.cancel;
        ui::drawProgress(view);
    });
    return job.result;
}

installer::Result install(const std::string &path, uint64_t titleId,
                          const std::string &name, bool keepAwake,
                          const std::vector<uint16_t> &icon)
{
    InstallJob job;
    job.path = path;
    job.titleId = titleId;

    StayAwake awake(keepAwake);
    Thread thread = startWorkerThread(runInstall, &job);
    if (thread == nullptr) {
        job.result.message = "Could not start the install thread.";
        return job.result;
    }

    units::RateMeter meter;
    supervise(job, thread, "installing", [&]() {
        ui::ProgressView view;
        {
            Guard guard(job.lock);
            meter.sample(osGetTime(), job.done);
            view.fraction = job.total > 0
                ? static_cast<float>(static_cast<double>(job.done) /
                                     static_cast<double>(job.total))
                : -1.0f;
            view.amounts = units::bytes(job.done) + " of " +
                           units::bytes(job.total);
            if (meter.hasRate()) {
                view.speed = units::rate(meter.bytesPerSecond());
                const int64_t left = job.total > job.done
                    ? meter.secondsLeft(job.total - job.done)
                    : -1;
                if (left >= 0) {
                    view.speed += ", " + units::duration(left) + " left";
                }
            }
        }
        view.title = "Installing";
        view.icon = ui::Icon::Package;
        view.iconColor = ui::color::IconBlue;
        view.name = name;
        view.source = path.substr(path.find_last_of('/') + 1);
        view.titleIcon = icon;
        view.message = "Do not turn off the power or remove the SD card.";
        view.stopping = job.cancel;
        ui::drawProgress(view);
    });
    if (job.closing && job.result.outcome == installer::Outcome::Cancelled) {
        job.result.message = "Interrupted when 3dsDL closed.";
    }
    return job.result;
}

fileops::Result fileOperation(
    const std::string &header, const std::string &title,
    const std::string &folder, uint64_t total, bool countsFiles,
    const std::function<fileops::Result(fileops::Observer &)> &work,
    bool keepAwake)
{
    FileJob job;
    job.work = &work;
    job.total = total;

    StayAwake awake(keepAwake);
    Thread thread = startWorkerThread(runFileJob, &job);
    if (thread == nullptr) {
        job.result.status = fileops::Status::Failed;
        job.result.message = "Could not start the worker thread.";
        return job.result;
    }

    units::RateMeter meter;
    supervise(job, thread, header, [&]() {
        ui::ProgressView view;
        {
            Guard guard(job.lock);
            const uint64_t expected = std::max(job.total, job.done);
            view.fraction = expected > 0
                ? static_cast<float>(static_cast<double>(job.done) /
                                     static_cast<double>(expected))
                : -1.0f;
            if (countsFiles) {
                view.amounts = std::to_string(job.done) + " of " +
                               std::to_string(expected) + " files";
            } else {
                view.amounts = units::bytes(job.done) + " of " +
                               units::bytes(expected);
                meter.sample(osGetTime(), job.done);
                if (meter.hasRate()) {
                    view.speed = units::rate(meter.bytesPerSecond());
                }
            }
            view.source = job.current;
        }
        view.title = capitalised(header);
        view.icon = header == "deleting" ? ui::Icon::Trash : ui::Icon::Folder;
        view.iconColor = header == "deleting" ? ui::color::Bad
                                              : ui::color::IconOrange;
        view.name = title;
        view.message = "In " + folder;
        view.stopping = job.cancel;
        ui::drawProgress(view);
    });
    return job.result;
}

} // namespace tasks
