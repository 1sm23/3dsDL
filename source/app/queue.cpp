#include "app.hpp"

#include "app_util.hpp"
#include "finalize.hpp"
#include "fs_util.hpp"
#include "installer.hpp"
#include "naming.hpp"
#include "qr_code.hpp"
#include "remote.hpp"
#include "remote_server.hpp"
#include "storage.hpp"
#include "tasks.hpp"
#include "text.hpp"
#include "ui.hpp"
#include "units.hpp"
#include "url.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <deque>

// The download queue, receiving links from other devices on the network
// (FBI's remote install protocol and a small web page), and installing a
// CIA without saving it.

namespace {

using screens::basename;
using screens::plural;

constexpr unsigned short WebPort = 80;
constexpr unsigned short FallbackWebPort = 8080;

std::string nameOf(const std::string &link)
{
    const std::string name = naming::chooseFilename("", link);
    return name == naming::DefaultFilename ? url::host(link) : name;
}

HistoryEntry historyEntry(const std::string &link, const std::string &folder,
                          HistoryStatus status, const std::string &message)
{
    HistoryEntry entry;
    entry.url = link;
    entry.folder = folder;
    entry.status = status;
    entry.message = message;
    return entry;
}

// The console's address on the local network, or "" when it has none.
std::string localAddress()
{
    const long id = gethostid();
    if (id == 0 || id == -1) {
        return "";
    }
    in_addr address {};
    address.s_addr = static_cast<in_addr_t>(id);
    return inet_ntoa(address);
}

// Listens on a worker thread while the Receive screen is open.
struct ReceiveJob {
    net::LinkReceiver receiver;
    std::atomic<bool> stop {false};
    LightLock lock;
    std::deque<net::ReceivedLinks> arrived;

    ReceiveJob() { LightLock_Init(&lock); }

    bool take(net::ReceivedLinks &out)
    {
        LightLock_Lock(&lock);
        const bool any = !arrived.empty();
        if (any) {
            out = arrived.front();
            arrived.pop_front();
        }
        LightLock_Unlock(&lock);
        return any;
    }
};

void receiveMain(void *argument)
{
    ReceiveJob &job = *static_cast<ReceiveJob *>(argument);
    while (!job.stop.load()) {
        net::ReceivedLinks received;
        if (job.receiver.wait(250, received)) {
            LightLock_Lock(&job.lock);
            job.arrived.push_back(received);
            LightLock_Unlock(&job.lock);
        }
    }
}

} // namespace

void App::addToQueue(const std::vector<std::string> &links,
                     const std::string &folder, bool announce)
{
    size_t added = 0;
    for (const std::string &link : links) {
        const bool already = std::any_of(
            queue_.begin(), queue_.end(), [&](const QueuedLink &item) {
                return item.url == link && item.folder == folder;
            });
        if (already) {
            continue;
        }
        QueuedLink item;
        item.id = nextQueueId_++;
        item.url = link;
        item.folder = folder;
        queue_.push_back(item);
        ++added;
    }
    if (announce) {
        ui::Notice notice;
        notice.title = added > 0 ? "Added to the Queue" : "Already in the Queue";
        notice.text = std::to_string(queue_.size()) +
                      " waiting in Queue on Home.";
        notice.icon = ui::Icon::Download;
        notice.iconColor = ui::color::IconGreen;
        ui::notify(notice);
    }
}

void App::runQueue(const std::vector<uint64_t> *ids)
{
    std::vector<uint64_t> order;
    for (const QueuedLink &item : queue_) {
        if (ids == nullptr ||
            std::find(ids->begin(), ids->end(), item.id) != ids->end()) {
            order.push_back(item.id);
        }
    }
    if (order.empty()) {
        return;
    }
    if (platform::wifiStatus() == platform::Wifi::Disconnected) {
        ui::message("Not Connected",
                    "Turn wireless communication on, then download the queue "
                    "again.",
                    ui::Tone::Warning);
        return;
    }

    // What to do with CIAs is asked once per run, when the first one lands.
    CiaAction ciaAction = settings_.afterCiaDownload;
    size_t saved = 0;
    std::vector<std::string> savedNames;
    size_t installed = 0;
    size_t failed = 0;
    bool stopped = false;
    for (size_t position = 0; position < order.size() && !stopped; ++position) {
        const auto found = std::find_if(
            queue_.begin(), queue_.end(),
            [&](const QueuedLink &item) { return item.id == order[position]; });
        if (found == queue_.end()) {
            continue;
        }
        QueuedLink item = *found;
        if (!fs::makeDirectories(item.folder)) {
            found->state = QueuedLink::State::Failed;
            found->message = "Could not create " + item.folder + ".";
            ++failed;
            continue;
        }

        net::Request request;
        request.url = item.url;
        request.folder = item.folder;
        request.caFile = http::CaBundle;
        request.userAgent = http::UserAgent;
        request.allowResume = settings_.resumeDownloads;
        request.socketBufferSize = http::SocketBufferSize;
        request.connections = http::DownloadConnections;
        request.freeSpace = [](const std::string &) {
            return platform::sdFreeBytes();
        };
        const std::string step = std::to_string(position + 1) + " of " +
                                 std::to_string(order.size());
        const net::Result result =
            tasks::download(request, settings_.keepAwake, step);
        if (!ui::keepRunning()) {
            return;
        }
        if (result.outcome == net::Outcome::Cancelled) {
            found->state = QueuedLink::State::Stopped;
            found->message = "Stopped.";
            HistoryEntry stoppedEntry = historyEntry(
                item.url, item.folder, HistoryStatus::Cancelled, result.message);
            stoppedEntry.name = result.filename;
            record(stoppedEntry);
            stopped = true;
            break;
        }
        if (result.outcome != net::Outcome::Completed) {
            found->state = QueuedLink::State::Failed;
            found->message = result.message;
            HistoryEntry entry = historyEntry(item.url, item.folder,
                                              HistoryStatus::Failed,
                                              result.message);
            entry.resumable = result.resumable;
            entry.bytes = result.bytes;
            entry.name = result.filename;
            record(entry);
            ++failed;
            continue;
        }

        // A queue never stops to ask about names: a clash keeps both.
        const finalize::Result placed =
            finalize::commit(result.partPath, item.folder, result.filename,
                             false, platform::now());
        HistoryEntry entry = historyEntry(
            item.url, item.folder,
            placed.ok ? HistoryStatus::Saved : HistoryStatus::Failed,
            placed.ok ? "Saved from the queue" : placed.message);
        entry.bytes = result.bytes;
        entry.path = placed.path;
        entry.name = result.filename;
        record(entry);
        if (!placed.ok) {
            found->state = QueuedLink::State::Failed;
            found->message = placed.message;
            ++failed;
            continue;
        }
        queue_.erase(found);
        ++saved;
        savedNames.push_back(basename(placed.path));

        if (!cia::isCiaFile(placed.path) || !installer::available()) {
            continue;
        }
        if (ciaAction == CiaAction::Ask) {
            ui::Dialog dialog;
            dialog.header = "Queue";
            dialog.title = "Install the CIAs from this queue?";
            dialog.tone = ui::Tone::Info;
            dialog.body.push_back({basename(placed.path) +
                                       " is the first. The answer is used "
                                       "for every CIA in this queue.",
                                   ui::color::Text, 0.5f, 3});
            dialog.buttons = {"Install", "Install, Delete", "Keep Files"};
            dialog.cancelButton = 2;
            const int choice = ui::choose(dialog);
            ciaAction = choice == 0   ? CiaAction::Install
                        : choice == 1 ? CiaAction::InstallAndDelete
                                      : CiaAction::Keep;
        }
        if (ciaAction == CiaAction::Keep) {
            continue;
        }
        cia::Info info;
        std::string error;
        if (cia::readInfo(placed.path, info, error) &&
            installCia(placed.path, info,
                       ciaAction == CiaAction::InstallAndDelete, item.url,
                       true)) {
            ++installed;
        }
    }

    std::string summary = plural(saved, "file") + " downloaded";
    if (installed > 0) {
        summary += ", " + std::to_string(installed) + " installed";
    }
    if (failed > 0) {
        summary += ", " + std::to_string(failed) + " failed";
    }
    summary += ".";
    if (stopped) {
        summary += " The queue was stopped; the rest are still in it.";
    } else if (failed > 0) {
        summary += " What failed stays in the queue to try again.";
    }
    // The names go on the top screen, which would otherwise show the last
    // progress frame behind the dialog.
    std::string names;
    for (size_t index = 0; index < savedNames.size() && index < 6; ++index) {
        names += (index > 0 ? ", " : "") + savedNames[index];
    }
    if (savedNames.size() > 6) {
        names += " and " + std::to_string(savedNames.size() - 6) + " more";
    }
    ui::message(failed == 0 && !stopped ? "Queue Done" : "Queue Finished",
                summary,
                failed == 0 && !stopped ? ui::Tone::Success
                                        : ui::Tone::Warning,
                names.empty() ? "" : "Saved: " + names + ".");
}

void App::queueScreen()
{
    int cursor = 0;
    while (ui::keepRunning() && !quit_) {
        ui::ListSpec spec;
        spec.title = "Queue";
        spec.emptyText = "Nothing is waiting. Choose Add to Queue after "
                         "scanning or entering a link, or send links with "
                         "Receive.";
        for (const QueuedLink &item : queue_) {
            ui::ListItem row;
            row.label = nameOf(item.url);
            row.icon = ui::Icon::Download;
            row.iconColor = ui::color::IconGreen;
            if (item.state == QueuedLink::State::Failed) {
                row.badge = "Failed";
                row.badgeColor = ui::color::Bad;
            } else if (item.state == QueuedLink::State::Stopped) {
                row.badge = "Stopped";
                row.badgeColor = ui::color::Neutral;
            } else {
                row.detail = url::host(item.url);
            }
            spec.items.push_back(row);
        }
        if (!queue_.empty()) {
            spec.action = "Download All";
            spec.actionKey = "X";
            spec.actionKeys = KEY_X;
        }
        spec.drawTop = [&](int highlighted) {
            ui::drawTopChrome("Queue", ui::Icon::Download, ui::color::IconGreen);
            ui::drawPanel();
            if (highlighted < 0 || highlighted >= static_cast<int>(queue_.size())) {
                ui::drawPanelText(spec.emptyText, ui::TopPanel, ui::type::Small);
                return;
            }
            const QueuedLink &item = queue_[highlighted];
            const float left = ui::TopPanel.x + 18;
            ui::drawIcon(ui::Icon::Download, left, ui::TopPanel.y + 14, 30,
                         ui::color::IconGreen);
            ui::drawMarquee(nameOf(item.url), left + 40, ui::TopPanel.y + 10,
                            0.62f, ui::color::InkStrong, 300);
            ui::drawText("from " + url::host(item.url), left + 40,
                         ui::TopPanel.y + 36, ui::type::Small,
                         ui::color::InkSoft);
            float y = ui::drawWrapped(item.url, left, ui::TopPanel.y + 58,
                                      ui::type::Tiny, ui::color::InkSoft,
                                      ui::TopPanel.w - 36, 2) +
                      4;
            y = ui::drawTagRow(left, y, 74, "Save to", item.folder);
            if (!item.message.empty()) {
                ui::drawTagRow(left, y, 74,
                               item.state == QueuedLink::State::Failed ? "Failed"
                                                                       : "Note",
                               item.message,
                               item.state == QueuedLink::State::Failed
                                   ? ui::color::Bad
                                   : ui::color::Neutral);
            } else {
                ui::drawTagRow(left, y, 74, "Position",
                               std::to_string(highlighted + 1) + " of " +
                                   std::to_string(queue_.size()));
            }
        };
        const ui::ListResult result = ui::runList(spec, cursor);
        if (result.key == KEY_X && !queue_.empty()) {
            runQueue();
            continue;
        }
        if (result.key != KEY_A || result.index < 0 ||
            result.index >= static_cast<int>(queue_.size())) {
            return;
        }
        const QueuedLink item = queue_[result.index];
        const int choice = ui::menu(
            {{"Download Now", false, true},
             {"Remove", true, true},
             {"Remove All", true, true}},
            [&](int) {
                ui::drawTopChrome("Queue", ui::Icon::Download,
                                  ui::color::IconGreen);
                ui::drawPanel();
                ui::drawPanelText(nameOf(item.url), ui::TopPanel);
            });
        if (choice == 0) {
            const std::vector<uint64_t> only = {item.id};
            runQueue(&only);
        } else if (choice == 1) {
            queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                        [&](const QueuedLink &link) {
                                            return link.id == item.id;
                                        }),
                         queue_.end());
        } else if (choice == 2) {
            ui::Dialog dialog;
            dialog.header = "Queue";
            dialog.title = "Remove " + plural(queue_.size(), "link") +
                           " from the queue?";
            dialog.tone = ui::Tone::Warning;
            dialog.buttons = {"Remove", "Cancel"};
            dialog.dangerous = 0;
            dialog.initial = 1;
            dialog.cancelButton = 1;
            if (ui::choose(dialog) == 0) {
                queue_.clear();
            }
        }
    }
}

void App::receiveScreen()
{
    if (platform::wifiStatus() == platform::Wifi::Disconnected) {
        ui::message("Not Connected",
                    "Receiving links needs the same Wi-Fi network as the "
                    "phone or computer sending them. Turn wireless "
                    "communication on first.",
                    ui::Tone::Warning);
        return;
    }
    const std::string address = localAddress();
    if (address.empty()) {
        ui::message("No Network Address",
                    "The console has no address on the local network yet. "
                    "Check the connection and try again.",
                    ui::Tone::Warning);
        return;
    }

    ReceiveJob job;
    std::string error;
    if (!job.receiver.open(remote::FbiPort, WebPort, FallbackWebPort, error)) {
        ui::message("Cannot Receive", error, ui::Tone::Error);
        return;
    }
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    Thread thread = threadCreate(receiveMain, &job, 32 * 1024,
                                 std::min(0x3f, static_cast<int>(priority) + 1),
                                 -2, false);
    if (thread == nullptr) {
        ui::message("Cannot Receive", "The network listener could not start.",
                    ui::Tone::Error);
        return;
    }

    const std::string page =
        "http://" + address +
        (job.receiver.webPort() == 80
             ? std::string("/")
             : ":" + std::to_string(job.receiver.webPort()) + "/");
    const QrCode code(page);
    std::string last;
    bool backPressed = false;
    const ui::Rect back = ui::tabRect(ui::Tab::Left);

    while (ui::keepRunning() && !quit_) {
        net::ReceivedLinks received;
        if (job.take(received)) {
            const bool fromFbiTool = received.replySocket >= 0;
            ui::Dialog dialog;
            dialog.header = "Receive";
            dialog.title = "Received " + plural(received.links.size(), "link") +
                           " from " + received.from + ".";
            dialog.tone = ui::Tone::Info;
            std::string names;
            for (size_t index = 0; index < received.links.size() && index < 4;
                 ++index) {
                names += (index > 0 ? ", " : "") + nameOf(received.links[index]);
            }
            if (received.links.size() > 4) {
                names += " and " + std::to_string(received.links.size() - 4) +
                         " more";
            }
            dialog.body.push_back({names, ui::color::Text, 0.5f, 3});
            dialog.body.push_back({"They go to " + settings_.destination + ".",
                                   ui::color::Muted, 0.44f, 2});
            if (fromFbiTool) {
                // The sender stops serving its files once told the links are
                // done, so these cannot wait in the queue.
                dialog.buttons = {"Download", "Cancel"};
                dialog.cancelButton = 1;
            } else {
                dialog.buttons = {"Download", "Add to Queue", "Cancel"};
                dialog.cancelButton = 2;
            }
            const int choice = ui::choose(dialog);
            if (choice == 0 || (!fromFbiTool && choice == 1)) {
                const uint64_t first = nextQueueId_;
                addToQueue(received.links, settings_.destination, choice == 1);
                if (choice == 0) {
                    std::vector<uint64_t> ids;
                    for (const QueuedLink &item : queue_) {
                        if (item.id >= first ||
                            std::find(received.links.begin(),
                                      received.links.end(),
                                      item.url) != received.links.end()) {
                            ids.push_back(item.id);
                        }
                    }
                    runQueue(&ids);
                }
                last = plural(received.links.size(), "link") + " from " +
                       received.from;
            }
            net::finishReceived(received);
            continue;
        }

        const ui::Input input = ui::readInput();
        if (input.down & KEY_B) {
            break;
        }
        if (input.down & KEY_TOUCH) {
            backPressed = back.contains(input.touch);
        }
        if ((input.up & KEY_TOUCH) && backPressed) {
            backPressed = false;
            if (back.contains(input.touch)) {
                break;
            }
        }

        ui::beginFrame();
        ui::drawTopChrome("Receive", ui::Icon::Chip, ui::color::IconPink);
        ui::drawPanel();
        const float px = code.size() <= 25 ? 4 : 3;
        const float qrSide = code.size() * px;
        const float qrX = ui::TopPanel.x + 12 + 2 * px;
        const float qrY = ui::TopPanel.y + (ui::TopPanel.h - qrSide) / 2;
        code.draw(qrX, qrY, px);
        const float left = qrX + qrSide + 2 * px + 12;
        const float width = ui::TopPanel.x + ui::TopPanel.w - 12 - left;
        // The address itself is large on the touch screen.
        const float y = ui::drawWrapped("Scan this with a phone on the same "
                                        "Wi-Fi, or open the address below.",
                                        left, ui::TopPanel.y + 12,
                                        ui::type::Small, ui::color::Ink, width,
                                        4) +
                        10;
        ui::drawWrapped(job.receiver.fbiPort() != 0
                            ? "FBI's tools (servefiles.py, Boop): " + address +
                                  ", port " +
                                  std::to_string(job.receiver.fbiPort()) + "."
                            : "FBI's tools cannot connect: port 5000 is busy.",
                        left, y, ui::type::Tiny, ui::color::InkSoft, width, 3);

        ui::beginBottom();
        const ui::Rect panel = {13, 12, 294, 136};
        ui::drawPanel(panel);
        ui::drawSpinner(160, panel.y + 26, 12);
        ui::drawText("Waiting for links at", 160, panel.y + 46, ui::type::Body,
                     ui::color::Ink, C2D_AlignCenter);
        ui::drawMarquee(page, 160, panel.y + 76, ui::type::Body,
                        ui::color::Accent, 280, C2D_AlignCenter);
        ui::drawText(last.empty() ? "Keep this screen open while sending."
                                  : "Last: " + last,
                     160, panel.y + 112, ui::type::Tiny, ui::color::InkSoft,
                     C2D_AlignCenter);
        ui::drawWrapped("Links download into " + settings_.destination +
                            " one after another.",
                        160, 160, ui::type::Tiny, ui::color::InkSoft, 270, 2,
                        C2D_AlignCenter);
        ui::drawTab(ui::Tab::Left, "Back", "", false,
                    backPressed && (input.held & KEY_TOUCH));
        ui::endFrame();
    }

    job.stop.store(true);
    threadJoin(thread, U64_MAX);
    threadFree(thread);
    // Senders still waiting are told the links are done, so they exit.
    net::ReceivedLinks pending;
    while (job.take(pending)) {
        net::finishReceived(pending);
    }
    job.receiver.close();
}

void App::installDirectly(const std::string &address)
{
    const std::string folder = storage::DataFolder;
    if (!fs::makeDirectories(folder)) {
        ui::message("Not Installed", "Could not create " + folder + ".",
                    ui::Tone::Error);
        return;
    }
    installer::StreamInstall sink;
    net::Request request;
    request.url = address;
    request.folder = folder;
    request.caFile = http::CaBundle;
    request.userAgent = http::UserAgent;
    request.allowResume = false;
    request.socketBufferSize = http::SocketBufferSize;
    request.connections = 1; // the install has to receive the data in order
    request.write = [&sink](const void *data, size_t size, FILE *file) {
        const off_t at = ftello(file);
        return at >= 0 && sink.write(static_cast<uint64_t>(at), data, size)
                   ? size
                   : static_cast<size_t>(0);
    };
    const net::Result result =
        tasks::download(request, settings_.keepAwake, "", true);
    if (!result.partPath.empty()) {
        fs::removeFile(result.partPath); // empty: the data went to the install
    }

    installer::Result installed;
    if (result.outcome == net::Outcome::Completed) {
        installed = sink.finish();
    } else {
        sink.cancel();
        installed = sink.result();
        if (installed.message.empty()) {
            installed.message = result.message;
        }
        if (result.outcome == net::Outcome::Cancelled) {
            installed.outcome = installer::Outcome::Cancelled;
        }
    }
    if (!ui::keepRunning()) {
        return;
    }

    const cia::Info &info = sink.info();
    const std::string name = info.hasSmdh ? screens::ciaName(info)
                                          : nameOf(address);
    const auto recordResult = [&](HistoryStatus status,
                                  const std::string &message) {
        HistoryEntry entry = historyEntry(address, folder, status, message);
        entry.name = result.filename;
        entry.bytes = installed.bytes;
        record(entry);
    };
    if (installed.outcome == installer::Outcome::Cancelled) {
        recordResult(HistoryStatus::Cancelled, "Direct install stopped");
        ui::message("Install Stopped", "Nothing was changed.");
        return;
    }
    if (installed.outcome != installer::Outcome::Installed) {
        recordResult(HistoryStatus::Failed,
                     "Direct install failed: " + installed.message);
        ui::message("Install Failed", installed.message, ui::Tone::Error, name);
        return;
    }
    recordResult(HistoryStatus::Installed, "Installed directly, nothing saved");

    std::string seedNote;
    if (info.seed == cia::Seed::Needed) {
        tasks::busy("Installing", "Getting the seed " + name + " needs...",
                    [&]() { installer::installSeed(info.titleId, seedNote); });
    }
    const uint64_t launchId = cia::baseTitle(info.titleId);
    const cia::Kind kind = cia::kind(launchId);
    const bool canLaunch = launchId == info.titleId &&
                           (kind == cia::Kind::Application ||
                            kind == cia::Kind::Demo);
    ui::Dialog dialog;
    dialog.header = "Installation Complete";
    dialog.title = name + " has been installed." +
                   (canLaunch ? " You can find it on the HOME Menu." : "");
    dialog.tone = ui::Tone::Success;
    dialog.icon = info.hasSmdh ? ui::showIcon(info.smdh.icon) : nullptr;
    dialog.body.push_back({name, ui::color::Text, 0.5f, 1});
    dialog.body.push_back({std::string(cia::kindName(cia::kind(info.titleId))) +
                               ", " + cia::versionText(info.version) + ", " +
                               units::bytes(installed.bytes) +
                               ", installed directly without saving the file.",
                           ui::color::Muted, 0.46f, 3});
    if (!seedNote.empty()) {
        dialog.body.push_back({seedNote, ui::color::Muted, 0.44f, 2});
    }
    dialog.buttons = canLaunch ? std::vector<std::string>{"Close", "Launch"}
                               : std::vector<std::string>{"Close"};
    dialog.initial = canLaunch ? 1 : 0;
    dialog.cancelButton = 0;
    if (ui::choose(dialog) == 1 && canLaunch) {
        std::string error;
        if (!installer::launch(launchId, error)) {
            ui::message("Could Not Launch", error, ui::Tone::Error);
        }
    }
}
