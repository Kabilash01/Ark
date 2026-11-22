#include "ark/browser.hpp"

#include "ark/privacy.hpp"

#include <curl/curl.h>

#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <optional>
#include <regex>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

extern char** environ;

namespace ark {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr std::size_t max_frame = 16 * 1024 * 1024;

void check_frame_size(std::size_t size) {
    if (size > max_frame) {
        throw std::runtime_error("Chromium protocol message exceeded 16 MiB");
    }
}

class BrowserTimeout : public std::runtime_error {
public:
    BrowserTimeout() : std::runtime_error("Chromium operation timed out") {}
};

[[noreturn]] void system_error(const char* action, int error = errno) {
    throw std::system_error(error, std::generic_category(), action);
}

void check_spawn(int result, const char* action) {
    if (result != 0) {
        system_error(action, result);
    }
}

class Fd {
public:
    explicit Fd(int fd = -1) : fd_(fd) {}
    ~Fd() { reset(); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return fd_; }
    void reset(int fd = -1) {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_;
};

std::string lowercase(std::string value) {
    for (auto& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

std::string url_host(std::string_view address) {
    if (address.empty() || address.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Browser requires an HTTP or HTTPS URL");
    }
    const std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(), curl_url_cleanup);
    if (!url || curl_url_set(url.get(), CURLUPART_URL, std::string(address).c_str(), CURLU_DISALLOW_USER) != CURLUE_OK) {
        throw std::invalid_argument("Browser requires a valid HTTP or HTTPS URL without credentials");
    }
    const auto component = [&](CURLUPart part) {
        char* text = nullptr;
        if (curl_url_get(url.get(), part, &text, 0) != CURLUE_OK) {
            throw std::invalid_argument("Browser URL is missing its scheme or host");
        }
        const std::unique_ptr<char, decltype(&curl_free)> value(text, curl_free);
        return lowercase(value.get());
    };
    const auto scheme = component(CURLUPART_SCHEME);
    if (scheme != "http" && scheme != "https") {
        throw std::invalid_argument("Browser requires an HTTP or HTTPS URL");
    }
    return component(CURLUPART_HOST);
}

bool loopback(std::string_view host) {
    return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

std::string temp_profile() {
    auto path = (std::filesystem::temp_directory_path() / "ark-chromium-XXXXXX").string();
    if (!mkdtemp(path.data())) {
        system_error("Create temporary Chromium profile");
    }
    return path;
}

void ready(int fd, short interest, Clock::time_point deadline) {
    while (Clock::now() < deadline) {
        pollfd descriptor{fd, interest, 0};
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();
        const auto status = poll(&descriptor, 1, static_cast<int>(std::max<std::int64_t>(0, remaining)));
        if (status > 0) {
            return;
        }
        if (status < 0 && errno != EINTR) {
            system_error("Wait for Chromium");
        }
    }
    throw BrowserTimeout();
}

std::string js_call(std::string_view function, const Json& argument) {
    return "(" + std::string(function) + ")(" + argument.dump() + ")";
}

constexpr auto visible_element = R"js((selector) => {
    const element = document.querySelector(selector);
    if (!element || !element.getClientRects().length) return false;
    const { width, height } = element.getBoundingClientRect();
    return width > 0 && height > 0 && getComputedStyle(element).visibility === "visible";
})js";

} // namespace

struct Browser::Impl {
    explicit Impl(BrowserOptions input)
        : options(std::move(input)),
          privacy(TaskPrivacy::resolve(options.privacy, {{"privacy", options.private_mode}, {"airgap_mode", options.airgap}})) {}

    BrowserOptions options;
    TaskPrivacy privacy;
    std::string profile;
    Fd channel;
    Fd blocked_proxy;
    pid_t child = -1;
    unsigned command_id = 0;
    std::string page;
    std::string received;
    std::deque<std::string> ready_loaders;

    ~Impl() {
        if (child > 0) {
            try {
                send({{"id", ++command_id}, {"method", "Browser.close"}}, Clock::now() + 100ms);
            } catch (const std::exception&) {
                // A disconnected browser still has to be reaped.
            }
            channel.reset();
            if (!reap_for(500ms)) {
                kill(-child, SIGTERM);
                if (!reap_for(500ms)) {
                    kill(-child, SIGKILL);
                    while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
                }
            }
        }
        if (!profile.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(profile, ignored);
        }
    }

    bool reap_for(std::chrono::milliseconds timeout) {
        const auto until = Clock::now() + timeout;
        do {
            const auto status = waitpid(child, nullptr, WNOHANG);
            if (status == child || (status < 0 && errno == ECHILD)) {
                return true;
            }
            std::this_thread::sleep_for(10ms);
        } while (Clock::now() < until);
        return false;
    }

    void proxy_flags(std::vector<std::string>& arguments) {
        if (!privacy.enabled) {
            arguments.emplace_back("--proxy-server=direct://");
            return;
        }
        // The bound socket reserves a port that declines proxy connections.
        blocked_proxy.reset(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (blocked_proxy.get() < 0
            || bind(blocked_proxy.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            system_error("Reserve Chromium's blocked proxy endpoint");
        }
        socklen_t length = sizeof(address);
        if (getsockname(blocked_proxy.get(), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            system_error("Get Chromium's blocked proxy endpoint");
        }
        arguments.push_back("--proxy-server=http://127.0.0.1:" + std::to_string(ntohs(address.sin_port)));
        std::string bypass = "--proxy-bypass-list=<-loopback>;localhost;127.0.0.1;[::1]";
        if (!privacy.airgap && options.privacy.contains("allowed_external")) {
            static const std::regex host_pattern(R"([a-zA-Z0-9][a-zA-Z0-9.-]*)");
            const auto& hosts = options.privacy.at("allowed_external");
            if (!hosts.is_array()) {
                throw std::invalid_argument("Browser privacy allowlist must be an array of hosts");
            }
            for (const auto& host : hosts) {
                const auto& name = host.get_ref<const std::string&>();
                if (!std::regex_match(name, host_pattern)) {
                    throw std::invalid_argument("Browser privacy allowlist requires exact host names");
                }
                bypass += ";" + lowercase(name);
            }
        }
        arguments.push_back(std::move(bypass));
    }

    void start() {
        if (options.executable.empty() || options.timeout <= 0ms || options.timeout > 120s) {
            throw std::invalid_argument("Browser requires an executable and a timeout from 1 to 120000 milliseconds");
        }
        profile = temp_profile();
        std::vector<std::string> arguments{
            options.executable, "--headless", "--remote-debugging-pipe", "--user-data-dir=" + profile,
            "--no-first-run", "--no-default-browser-check", "--disable-background-networking", "--disable-sync",
            "--disable-component-update", "--disable-dev-shm-usage", "--disable-quic",
            "--force-webrtc-ip-handling-policy=disable_non_proxied_udp", "about:blank",
        };
        proxy_flags(arguments);
        std::vector<char*> argv;
        for (auto& argument : arguments) {
            argv.push_back(argument.data());
        }
        argv.push_back(nullptr);

        int sockets[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) < 0) {
            system_error("Create Chromium DevTools socket");
        }
        channel.reset(sockets[0]);
        Fd peer(sockets[1]);
        Fd target(fcntl(peer.get(), F_DUPFD_CLOEXEC, 5));
        if (target.get() < 0) {
            system_error("Duplicate Chromium DevTools socket");
        }
        struct Actions {
            posix_spawn_file_actions_t value;
            Actions() { check_spawn(posix_spawn_file_actions_init(&value), "Prepare Chromium file actions"); }
            ~Actions() { posix_spawn_file_actions_destroy(&value); }
        } actions;
        for (const int fd : {0, 1, 2}) {
            check_spawn(posix_spawn_file_actions_addopen(&actions.value, fd, "/dev/null", fd == 0 ? O_RDONLY : O_WRONLY, 0),
                        "Set Chromium standard streams");
        }
        for (const int fd : {3, 4}) {
            check_spawn(posix_spawn_file_actions_adddup2(&actions.value, target.get(), fd), "Set Chromium DevTools descriptors");
        }
        struct Attributes {
            posix_spawnattr_t value;
            Attributes() { check_spawn(posix_spawnattr_init(&value), "Prepare Chromium process attributes"); }
            ~Attributes() { posix_spawnattr_destroy(&value); }
        } attributes;
        check_spawn(posix_spawnattr_setflags(&attributes.value, POSIX_SPAWN_SETPGROUP), "Set Chromium process flags");
        check_spawn(posix_spawnattr_setpgroup(&attributes.value, 0), "Set Chromium process group");
        pid_t launched = -1;
        check_spawn(posix_spawnp(&launched, argv.front(), &actions.value, &attributes.value, argv.data(), environ),
                    "Launch Chromium");
        child = launched;
        peer.reset();
        target.reset();
        channel_start();
    }

    void channel_start() {
        const auto target = command("Target.createTarget", {{"url", "about:blank"}}, false).at("targetId");
        page = command("Target.attachToTarget", {{"targetId", target}, {"flatten", true}}, false)
            .at("sessionId").get<std::string>();
        command("Page.enable");
        command("Page.setLifecycleEventsEnabled", {{"enabled", true}});
        command("Emulation.setDeviceMetricsOverride",
                {{"width", 1280}, {"height", 800}, {"deviceScaleFactor", 1}, {"mobile", false}});
        command("Browser.setDownloadBehavior", {{"behavior", "deny"}}, false);
    }

    void send(const Json& data, Clock::time_point deadline) {
        auto frame = data.dump();
        check_frame_size(frame.size());
        frame += '\0';
        std::string_view remaining(frame);
        while (!remaining.empty()) {
            ready(channel.get(), POLLOUT, deadline);
            const auto count = ::send(channel.get(), remaining.data(), remaining.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
            if (count > 0) {
                remaining.remove_prefix(count);
            } else if (count == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
                throw std::runtime_error("Chromium closed the DevTools channel");
            }
        }
    }

    Json receive(Clock::time_point deadline) {
        std::array<char, 16384> bytes{};
        while (true) {
            if (const auto separator = received.find('\0'); separator != std::string::npos) {
                check_frame_size(separator);
                auto result = Json::parse(received.begin(), received.begin() + separator);
                received.erase(0, separator + 1);
                return result;
            }
            check_frame_size(received.size());
            ready(channel.get(), POLLIN, deadline);
            const auto count = recv(channel.get(), bytes.data(), bytes.size(), MSG_DONTWAIT);
            if (count > 0) {
                received.append(bytes.data(), count);
            } else if (count == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
                throw std::runtime_error("Chromium closed the DevTools channel");
            }
        }
    }

    void handle_event(const Json& message, Clock::time_point deadline) {
        if (message.value("sessionId", "") != page) return;
        const auto method = message.value("method", "");
        if (method == "Page.lifecycleEvent" && message.at("params").value("name", "") == "DOMContentLoaded") {
            ready_loaders.push_back(message.at("params").at("loaderId").get<std::string>());
            if (ready_loaders.size() > 64) ready_loaders.pop_front();
        } else if (method == "Page.javascriptDialogOpening") {
            send({{"id", ++command_id}, {"method", "Page.handleJavaScriptDialog"},
                  {"sessionId", page}, {"params", {{"accept", false}}}}, deadline);
        }
    }

    Json command(std::string_view method, Json params = Json::object(), bool in_page = true,
                 std::optional<Clock::time_point> limit = std::nullopt) {
        const auto deadline = limit.value_or(Clock::now() + options.timeout);
        const auto id = ++command_id;
        Json message{{"id", id}, {"method", method}, {"params", std::move(params)}};
        if (in_page) message["sessionId"] = page;
        send(message, deadline);
        while (true) {
            auto reply = receive(deadline);
            if (reply.value("id", 0U) == id) {
                if (reply.contains("error")) {
                    throw std::runtime_error(std::string(method) + ": " + reply.at("error").value("message", "Chromium error"));
                }
                return reply.at("result");
            }
            handle_event(reply, deadline);
        }
    }

    Json evaluate(std::string expression, std::optional<Clock::time_point> deadline = std::nullopt) {
        const auto result = command("Runtime.evaluate", {{"expression", std::move(expression)}, {"returnByValue", true}},
                                    true, deadline);
        if (result.contains("exceptionDetails")) {
            const auto& exception = result.at("exceptionDetails");
            const auto object = exception.value("exception", Json::object());
            throw std::runtime_error(object.value("description", exception.value("text", "Page operation failed")));
        }
        return result.at("result").value("value", Json(nullptr));
    }

    bool wait_for(std::string_view selector, Clock::time_point deadline) {
        const auto script = js_call(visible_element, Json(selector));
        try {
            while (Clock::now() < deadline) {
                if (evaluate(script, deadline) == true) return true;
                std::this_thread::sleep_until(std::min(deadline, Clock::now() + 35ms));
            }
        } catch (const BrowserTimeout&) {
            return false;
        }
        return false;
    }

    Clock::time_point require_element(std::string_view selector) {
        const auto deadline = Clock::now() + options.timeout;
        if (!wait_for(selector, deadline)) {
            throw std::runtime_error("Browser element did not become visible before the timeout");
        }
        return deadline;
    }
};

Browser::Browser(BrowserOptions options) : impl_(std::make_unique<Impl>(std::move(options))) { impl_->start(); }
Browser::~Browser() = default;

Browser::Impl& Browser::session() {
    if (!impl_) throw std::logic_error("Browser session is closed");
    return *impl_;
}

void Browser::close_browser() { impl_.reset(); }

void Browser::navigate(std::string_view url) {
    auto& browser = session();
    const auto host = url_host(url);
    if (!loopback(host) && !browser.privacy.allows_external(browser.options.privacy, host)) {
        throw std::invalid_argument("Browser privacy settings block this host");
    }
    const auto deadline = Clock::now() + browser.options.timeout;
    browser.ready_loaders.clear();
    const auto response = browser.command("Page.navigate", {{"url", url}}, true, deadline);
    if (response.value("isDownload", false)) {
        throw std::runtime_error("Browser navigation requested a download");
    }
    if (const auto error = response.value("errorText", ""); !error.empty()) {
        throw std::runtime_error("Browser navigation failed: " + error);
    }
    if (response.contains("loaderId")) {
        const auto loader = response.at("loaderId").get<std::string>();
        while (std::find(browser.ready_loaders.begin(), browser.ready_loaders.end(), loader) == browser.ready_loaders.end()) {
            browser.handle_event(browser.receive(deadline), deadline);
        }
    }
}

std::string Browser::get_page_text() {
    return session().evaluate("document.body?.innerText ?? ''").get<std::string>();
}

std::string Browser::get_page_html() {
    return session().evaluate("document.documentElement?.outerHTML ?? ''").get<std::string>();
}

bool Browser::wait_for_element(std::string_view selector) {
    auto& browser = session();
    return browser.wait_for(selector, Clock::now() + browser.options.timeout);
}

void Browser::click(std::string_view selector) {
    auto& browser = session();
    const auto deadline = browser.require_element(selector);
    const auto position = browser.evaluate(js_call(R"js((selector) => {
        const element = document.querySelector(selector);
        if (!element || element.matches(":disabled")) throw new Error("Element cannot be clicked");
        element.scrollIntoView({ block: "center", inline: "center", behavior: "instant" });
        const { left, right, top, bottom } = element.getBoundingClientRect();
        const x = (Math.max(0, left) + Math.min(innerWidth, right)) / 2;
        const y = (Math.max(0, top) + Math.min(innerHeight, bottom)) / 2;
        if (!element.contains(document.elementFromPoint(x, y))) throw new Error("Element is covered or outside the viewport");
        return { x, y };
    })js", Json(selector)), deadline);
    Json event{{"x", position.at("x")}, {"y", position.at("y")}, {"button", "left"}, {"clickCount", 1}};
    for (const auto* type : {"mousePressed", "mouseReleased"}) {
        event["type"] = type;
        browser.command("Input.dispatchMouseEvent", event, true, deadline);
    }
}

void Browser::type_text(std::string_view selector, std::string_view text) {
    auto& browser = session();
    const auto deadline = browser.require_element(selector);
    browser.evaluate(js_call(R"js((selector) => {
        const element = document.querySelector(selector);
        const textInput = element.matches("textarea,input:is([type=text],[type=search],[type=email],[type=tel],[type=url],[type=password],[type=number],:not([type]))");
        if ((!textInput && !element.isContentEditable) || element.matches(":disabled,[readonly]")) {
            throw new Error("Element is not editable text");
        }
        element.focus();
        if (document.activeElement !== element && !element.contains(document.activeElement)) throw new Error("Element could not receive focus");
    })js", Json(selector)), deadline);
    browser.command("Input.insertText", {{"text", text}}, true, deadline);
}

std::string Browser::screenshot() {
    return session().command("Page.captureScreenshot", {{"format", "png"}}).at("data").get<std::string>();
}

void Browser::fill_form(const Json& fields) {
    if (!fields.is_object()) throw std::invalid_argument("Form fields must map CSS selectors to strings or booleans");
    auto& browser = session();
    for (const auto& [selector, value] : fields.items()) {
        if (!value.is_string() && !value.is_boolean()) {
            throw std::invalid_argument("Form field values must be strings or booleans");
        }
        const auto deadline = browser.require_element(selector);
        browser.evaluate(js_call(R"js(([selector, value]) => {
            const element = document.querySelector(selector);
            if (!element || element.matches(":disabled,[readonly]")) throw new Error("Form field is not editable");
            if (typeof value === "boolean") {
                if (!element.matches("input[type=checkbox],input[type=radio]")) throw new Error("Boolean requires a checkbox or radio button");
                Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "checked").set.call(element, value);
            } else {
                const prototype = element instanceof HTMLSelectElement ? HTMLSelectElement.prototype
                    : element instanceof HTMLTextAreaElement ? HTMLTextAreaElement.prototype
                    : element.matches("input:not([type=file],[type=checkbox],[type=radio],[type=submit],[type=button],[type=reset],[type=image],[type=hidden])")
                        ? HTMLInputElement.prototype : null;
                if (!prototype) throw new Error("String requires a text input, textarea, or select");
                if (element instanceof HTMLSelectElement && !Array.from(element.options).some((option) => option.value === value)) {
                    throw new Error("Select option was not found");
                }
                Object.getOwnPropertyDescriptor(prototype, "value").set.call(element, value);
            }
            element.dispatchEvent(new Event("input", { bubbles: true }));
            element.dispatchEvent(new Event("change", { bubbles: true }));
        })js", Json::array({selector, value})), deadline);
    }
}

Json Browser::extract_links() {
    return session().evaluate(R"js(Array.from(document.querySelectorAll("a[href]"), (link) =>
        ({ text: link.innerText.trim(), url: link.href })))js");
}

Json Browser::extract_table(std::string_view selector) {
    auto& browser = session();
    const auto deadline = browser.require_element(selector);
    return browser.evaluate(js_call(R"js((selector) => {
        const table = document.querySelector(selector);
        if (!(table instanceof HTMLTableElement)) throw new Error("Selector does not refer to a table");
        return Array.from(table.rows, (row) => Array.from(row.cells, (cell) => cell.innerText.trim()));
    })js", Json(selector)), deadline);
}

} // namespace ark
