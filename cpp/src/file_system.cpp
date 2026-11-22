#include "ark/file_system.hpp"

#include "ark/storage.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace ark {
namespace {

constexpr auto name = "file_system_agent";
constexpr auto version = "file_system_agent@1.0.0";
constexpr std::size_t max_bytes = 1024 * 1024;

[[noreturn]] void file_error(const char* action) {
    throw std::system_error(errno, std::generic_category(), action);
}

class File {
public:
    explicit File(int fd) : fd_(fd) {
        if (fd < 0) {
            file_error("Open workspace entry");
        }
    }
    ~File() { close(fd_); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    int get() const { return fd_; }
    void descend(const std::string& component) {
        const int new_fd = openat(fd_, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (new_fd < 0) {
            file_error("Open workspace directory");
        }
        close(fd_);
        fd_ = new_fd;
    }

private:
    int fd_;
};

std::vector<std::string> path_parts(std::string_view path) {
    std::vector<std::string> components;
    while (!path.empty()) {
        const auto split = path.find('/');
        const auto component = path.substr(0, split);
        if (component.empty() || component == "." || component == ".." || component.find('\0') != std::string_view::npos) {
            throw std::invalid_argument("Path must be relative to the configured workspace");
        }
        components.emplace_back(component);
        if (split == std::string_view::npos) {
            break;
        }
        path.remove_prefix(split + 1);
    }
    return components;
}

Json list_directory(int directory) {
    const int duplicate = fcntl(directory, F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0) {
        file_error("Open workspace directory");
    }
    auto* stream = fdopendir(duplicate);
    if (!stream) {
        const auto error = errno;
        close(duplicate);
        throw std::system_error(error, std::generic_category(), "Read workspace directory");
    }
    const std::unique_ptr<DIR, int (*)(DIR*)> dir(stream, closedir);
    std::vector<std::string> entries;
    while (true) {
        errno = 0;
        const auto* entry = readdir(dir.get());
        if (!entry) {
            if (errno != 0) {
                file_error("Read workspace directory");
            }
            break;
        }
        const std::string_view filename(entry->d_name);
        if (filename != "." && filename != "..") {
            entries.emplace_back(filename);
            if (entries.size() > 1000) {
                throw std::invalid_argument("Workspace directory exceeds 1000 entries");
            }
        }
    }
    std::sort(entries.begin(), entries.end());
    return {{"entries", entries}};
}

Json read_file(int directory, const std::string& filename) {
    const File file(openat(directory, filename.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    struct stat info{};
    if (fstat(file.get(), &info) != 0) {
        file_error("Inspect workspace file");
    }
    if (!S_ISREG(info.st_mode) || info.st_size > static_cast<off_t>(max_bytes)) {
        throw std::invalid_argument("Workspace reads require a regular text file no larger than 1 MiB");
    }
    std::string contents;
    std::array<char, 4096> bytes{};
    while (true) {
        const auto count = read(file.get(), bytes.data(), bytes.size());
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            file_error("Read workspace file");
        }
        if (count == 0) {
            break;
        }
        contents.append(bytes.data(), count);
        if (contents.size() > max_bytes) {
            throw std::invalid_argument("Workspace file exceeds 1 MiB");
        }
    }
    const Json result{{"content", contents}};
    (void)result.dump();
    return result;
}

void write_file(int directory, const std::string& filename, std::string_view contents, bool overwrite) {
    const std::string temp = ".ark-" + random_uuid();
    const File file(openat(directory, temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
    struct Cleanup {
        int directory;
        const std::string& path;
        ~Cleanup() { unlinkat(directory, path.c_str(), 0); }
    } cleanup{directory, temp};
    while (!contents.empty()) {
        const auto count = write(file.get(), contents.data(), contents.size());
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            file_error("Write workspace file");
        }
        contents.remove_prefix(count);
    }
    if (fsync(file.get()) != 0) {
        file_error("Flush workspace file");
    }
    if (!overwrite) {
        if (linkat(directory, temp.c_str(), directory, filename.c_str(), 0) != 0) {
            file_error("Create workspace file");
        }
        return;
    }
    struct stat target{};
    if (fstatat(directory, filename.c_str(), &target, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISREG(target.st_mode)) {
            throw std::invalid_argument("Only regular files can be replaced");
        }
        if (fchmod(file.get(), target.st_mode & 0777) != 0) {
            file_error("Preserve workspace file permissions");
        }
    } else if (errno != ENOENT) {
        file_error("Inspect workspace file");
    }
    if (renameat(directory, temp.c_str(), directory, filename.c_str()) != 0) {
        file_error("Replace workspace file");
    }
}

} // namespace

FileSystemAgent::FileSystemAgent(Json allowed_directories) : directories_(std::move(allowed_directories)) {
    if (!directories_.is_object()) {
        throw std::invalid_argument("allowed_directories must map aliases to absolute directory paths");
    }
}

Json FileSystemAgent::run(Json message, EpisodicMemory& episodes) const {
    if (!message.is_object()) {
        throw std::invalid_argument("An ARKMessage must be a JSON object");
    }
    message["agent_version"] = version;
    try {
        if (!pending_for(message, name) || message.at("intent") != "FILES") {
            throw std::invalid_argument("FileSystemAgent requires a pending FILES message");
        }
        const auto& payload = message.at("payload");
        const auto alias = payload.at("root").get<std::string>();
        const auto root = directories_.value(alias, std::string{});
        if (!root.starts_with('/') || root.find('\0') != std::string::npos) {
            throw std::invalid_argument("Workspace alias must select a configured absolute directory");
        }
        const auto path = payload.value("path", std::string{});
        const auto components = path_parts(path);
        const auto operation = payload.at("operation").get<std::string>();
        const bool list = operation == "list";
        if (!list && components.empty()) {
            throw std::invalid_argument("File operation requires a path");
        }
        File directory(open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        const auto parents = components.size() - (list ? 0 : 1);
        for (std::size_t index = 0; index < parents; ++index) {
            directory.descend(components[index]);
        }
        if (list) {
            message["output"] = list_directory(directory.get());
        } else if (operation == "read") {
            message["output"] = read_file(directory.get(), components.back());
        } else if (operation == "write") {
            const auto& contents = payload.at("content").get_ref<const std::string&>();
            if (contents.size() > max_bytes) {
                throw std::invalid_argument("Workspace file exceeds 1 MiB");
            }
            const bool overwrite = payload.value("overwrite", false);
            Json event{{"task_id", message.at("task_id")}, {"agent_name", name}, {"agent_version", version},
                       {"intent", "FILES"}, {"summary", "Workspace file write requested"},
                       {"metadata", {{"event", "file_write_requested"}, {"root", alias}, {"path", path},
                                     {"bytes", contents.size()}}}};
            episodes.log(event);
            write_file(directory.get(), components.back(), contents, overwrite);
            event["summary"] = "Workspace file written";
            event["metadata"]["event"] = "file_write";
            episodes.log(event);
            message["output"] = {{"bytes_written", contents.size()}};
        } else {
            throw std::invalid_argument("Expected list, read, or write operation");
        }
        message["status"] = "DONE";
    } catch (const std::exception& error) {
        message["status"] = "FAILED";
        message["quality_score"] = 0.0;
        message["output"] = {{"error_type", "FileSystemError"}, {"error_message", error.what()}};
    }
    return message;
}

} // namespace ark
