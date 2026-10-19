#include "updater.hpp"

#include "app_util.hpp"
#include "fetch.hpp"

#include <3ds.h>

#include <algorithm>
#include <atomic>

namespace updater {

namespace {

// TLS needs a deep stack; the check never holds much else.
constexpr size_t CheckStackSize = 128 * 1024;
constexpr const char *DefaultHomebrewPath = "sdmc:/3ds/3dsDL.3dsx";

LightLock lock;
bool lockReady = false;
Status current;
std::atomic<uint32_t> finished(0);
std::atomic<bool> cancel(false);
std::atomic<bool> running(false);
Thread thread = nullptr;
std::string launchPath;

void ensureLock()
{
    if (!lockReady) {
        LightLock_Init(&lock);
        lockReady = true;
    }
}

void publish(const Status &status)
{
    LightLock_Lock(&lock);
    current = status;
    LightLock_Unlock(&lock);
}

net::FetchRequest githubRequest(const std::string &url, size_t maxBytes)
{
    net::FetchRequest request;
    request.url = url;
    request.caFile = http::CaBundle;
    request.userAgent = http::UserAgent;
    request.maxBytes = maxBytes;
    request.cancel = &cancel;
    return request;
}

std::string explain(const net::FetchResult &result)
{
    if (result.httpStatus == 403 || result.httpStatus == 429) {
        return "GitHub is limiting how often it can be asked from this "
               "network. Try again in an hour.";
    }
    if (result.httpStatus == 404) {
        return "No release of 3dsDL has been published yet.";
    }
    return result.message;
}

Status check()
{
    Status status;
    net::FetchRequest request = githubRequest(update::LatestReleaseUrl, 512 * 1024);
    request.headers = {"Accept: application/vnd.github+json",
                       "X-GitHub-Api-Version: 2022-11-28"};
    const net::FetchResult result = net::fetch(request);
    if (!result.ok) {
        status.state = State::Failed;
        status.message = explain(result);
        status.certificateProblem = result.certificateProblem;
        return status;
    }
    std::string error;
    if (!update::parseRelease(result.body, status.release, error)) {
        status.state = State::Failed;
        status.message = error;
        return status;
    }
    status.state = update::isNewer(status.release.version, APP_VERSION)
                       ? State::Available
                       : State::UpToDate;
    return status;
}

void finish(const Status &status)
{
    publish(status);
    finished.fetch_add(1);
}

void threadMain(void *)
{
    finish(check());
    running.store(false);
}

void reap()
{
    if (thread != nullptr && !running.load()) {
        threadJoin(thread, U64_MAX);
        threadFree(thread);
        thread = nullptr;
    }
}

} // namespace

void setLaunchPath(const char *path)
{
    launchPath = path != nullptr ? path : "";
}

bool runningAsCia()
{
    return !envIsHomebrew();
}

std::string homebrewPath()
{
    // The Homebrew Launcher passes "sdmc:/3ds/3dsDL.3dsx"; some loaders
    // leave out the device.
    std::string path = launchPath;
    if (path.rfind("/", 0) == 0) {
        path = "sdmc:" + path;
    }
    const std::string ending = ".3dsx";
    if (path.rfind("sdmc:/", 0) != 0 || path.size() <= ending.size() ||
        path.compare(path.size() - ending.size(), ending.size(), ending) != 0) {
        return DefaultHomebrewPath;
    }
    return path;
}

const char *assetName()
{
    return runningAsCia() ? update::CiaAsset : update::HomebrewAsset;
}

void startCheck()
{
    ensureLock();
    reap();
    if (running.load()) {
        return;
    }
    cancel.store(false);
    running.store(true);
    Status checking = status();
    checking.state = State::Checking;
    publish(checking);

    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    // Below the main thread, so drawing never waits for TLS.
    thread = threadCreate(threadMain, nullptr, CheckStackSize,
                          std::min(0x3f, static_cast<int>(priority) + 2), -2,
                          false);
    if (thread == nullptr) {
        running.store(false);
        Status failed;
        failed.state = State::Failed;
        failed.message = "The update check could not start.";
        finish(failed);
    }
}

Status checkNow()
{
    ensureLock();
    if (thread != nullptr) {
        threadJoin(thread, U64_MAX);
        threadFree(thread);
        thread = nullptr;
        running.store(false);
        return status();
    }
    cancel.store(false);
    const Status result = check();
    finish(result);
    return result;
}

Status status()
{
    ensureLock();
    LightLock_Lock(&lock);
    const Status copy = current;
    LightLock_Unlock(&lock);
    return copy;
}

uint32_t generation()
{
    return finished.load();
}

void shutdown()
{
    if (thread != nullptr) {
        cancel.store(true);
        threadJoin(thread, U64_MAX);
        threadFree(thread);
        thread = nullptr;
        running.store(false);
    }
}

bool fetchChecksums(const update::Release &release, std::string &listing,
                    std::string &error)
{
    const update::Asset *sums = update::findAsset(release, update::ChecksumAsset);
    if (sums == nullptr) {
        error = "This release has no checksum file, so 3dsDL cannot make "
                "sure the download is intact.";
        return false;
    }
    cancel.store(false);
    const net::FetchResult result = net::fetch(githubRequest(sums->url, 64 * 1024));
    if (!result.ok) {
        error = explain(result);
        return false;
    }
    listing = result.body;
    return true;
}

} // namespace updater
