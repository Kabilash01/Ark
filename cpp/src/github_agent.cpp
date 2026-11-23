#include "ark/github_agent.hpp"

#include <git2.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <regex>

namespace ark {
namespace {

template<class T, void (*Free)(T*)>
using GitPtr = std::unique_ptr<T, decltype(Free)>;

void check_git(int status, std::string_view action) {
    if (status >= 0) return;
    const auto* error = git_error_last();
    throw std::runtime_error(std::string(action) + (error ? ": " + std::string(error->message) : ""));
}

std::string field(const Json& request, const char* key) {
    const auto value = request.at(key).get<std::string>();
    if (value.empty() || value.find('\0') != std::string::npos) {
        throw std::invalid_argument(std::string("Expected a nonempty ") + key);
    }
    return value;
}

std::string repo_name(const Json& request) {
    const auto name = field(request, "repository");
    static const std::regex pattern(R"([A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9_.-]+)");
    if (name.size() > 150 || !std::regex_match(name, pattern) || name.ends_with("/.") || name.ends_with("/..")) {
        throw std::invalid_argument("Repository must be a GitHub owner/name");
    }
    return name;
}

int credentials(git_credential** output, const char* url, const char*, unsigned allowed, void* payload) {
    const auto& token = *static_cast<const std::string*>(payload);
    if (token.empty() || !std::string_view(url).starts_with("https://github.com/")
        || !(allowed & GIT_CREDENTIAL_USERPASS_PLAINTEXT)) return GIT_PASSTHROUGH;
    return git_credential_userpass_plaintext_new(output, "x-access-token", token.c_str());
}

GitPtr<git_repository, git_repository_free> open_repository(const Json& request) {
    git_repository* raw = nullptr;
    check_git(git_repository_open_ext(&raw, field(request, "path").c_str(), GIT_REPOSITORY_OPEN_NO_SEARCH, nullptr),
              "Open repository");
    return {raw, git_repository_free};
}

std::string local_branch(const Json& request) {
    const auto branch = field(request, "branch");
    int valid = 0;
    check_git(git_branch_name_is_valid(&valid, branch.c_str()), "Check branch name");
    if (!valid) throw std::invalid_argument("Invalid Git branch name");
    return branch;
}

Json changed_files(git_repository* repo) {
    git_status_options options = GIT_STATUS_OPTIONS_INIT;
    options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;
    git_status_list* raw = nullptr;
    check_git(git_status_list_new(&raw, repo, &options), "Read working tree status");
    const GitPtr<git_status_list, git_status_list_free> status(raw, git_status_list_free);
    auto files = Json::array();
    for (std::size_t i = 0; i < git_status_list_entrycount(raw); ++i) {
        const auto* entry = git_status_byindex(raw, i);
        const auto* delta = entry->index_to_workdir ? entry->index_to_workdir : entry->head_to_index;
        const auto* path = delta->new_file.path ? delta->new_file.path : delta->old_file.path;
        files.push_back({{"path", path}, {"status_flags", entry->status}});
    }
    return {{"files", std::move(files)}};
}

GitPtr<git_commit, git_commit_free> head_commit(git_repository* repo) {
    git_object* raw = nullptr;
    check_git(git_revparse_single(&raw, repo, "HEAD^{commit}"), "Read head commit");
    return {reinterpret_cast<git_commit*>(raw), git_commit_free};
}

Json create_branch(git_repository* repo, const Json& request) {
    const auto branch = local_branch(request);
    const auto head = head_commit(repo);
    git_reference* raw = nullptr;
    check_git(git_branch_create(&raw, repo, branch.c_str(), head.get(), 0), "Create branch");
    const GitPtr<git_reference, git_reference_free> ref(raw, git_reference_free);
    if (request.value("checkout", true)) {
        check_git(git_repository_set_head(repo, git_reference_name(raw)), "Switch to branch");
    }
    return {{"branch", branch}};
}

Json commit_files(git_repository* repo, const Json& request) {
    if (git_repository_head_detached(repo) != 0 || git_repository_state(repo) != GIT_REPOSITORY_STATE_NONE) {
        throw std::invalid_argument("Commit requires an active local branch with no Git operation in progress");
    }
    const auto message = field(request, "message");
    const auto paths = request.at("files").get<std::vector<std::string>>();
    if (paths.empty() || paths.size() > 1000) throw std::invalid_argument("Supply 1 to 1000 files to commit");
    const auto head = head_commit(repo);
    git_tree* parent_tree = nullptr;
    check_git(git_commit_tree(&parent_tree, head.get()), "Read parent tree");
    const GitPtr<git_tree, git_tree_free> old_tree(parent_tree, git_tree_free);
    git_index* raw_index = nullptr;
    check_git(git_repository_index(&raw_index, repo), "Open Git index");
    const GitPtr<git_index, git_index_free> index(raw_index, git_index_free);
    git_diff* raw_diff = nullptr;
    check_git(git_diff_tree_to_index(&raw_diff, repo, parent_tree, raw_index, nullptr), "Check staged changes");
    const GitPtr<git_diff, git_diff_free> diff(raw_diff, git_diff_free);
    if (git_diff_num_deltas(raw_diff) != 0) throw std::invalid_argument("Commit requires a clean staging area");
    for (const auto& path : paths) {
        const std::filesystem::path relative(path);
        if (path.empty() || relative.is_absolute() || path.find('\0') != std::string::npos
            || std::any_of(relative.begin(), relative.end(), [](const auto& part) { return part == ".."; })) {
            throw std::invalid_argument("Commit paths must be relative to the worktree");
        }
        const auto status = git_index_add_bypath(raw_index, path.c_str());
        if (status == GIT_ENOTFOUND && git_index_get_bypath(raw_index, path.c_str(), 0)) {
            check_git(git_index_remove_bypath(raw_index, path.c_str()), "Stage deleted file");
        } else {
            check_git(status, "Stage file");
        }
    }
    git_oid tree_id{};
    check_git(git_index_write_tree(&tree_id, raw_index), "Write commit tree");
    if (git_oid_equal(&tree_id, git_tree_id(parent_tree))) throw std::invalid_argument("Selected files have no changes");
    git_tree* raw_tree = nullptr;
    check_git(git_tree_lookup(&raw_tree, repo, &tree_id), "Read commit tree");
    const GitPtr<git_tree, git_tree_free> tree(raw_tree, git_tree_free);
    git_signature* raw_signature = nullptr;
    check_git(git_signature_default(&raw_signature, repo), "Read Git author identity");
    const GitPtr<git_signature, git_signature_free> signature(raw_signature, git_signature_free);
    git_oid id{};
    const git_commit* parents[] = {head.get()};
    check_git(git_commit_create(&id, repo, "HEAD", raw_signature, raw_signature, nullptr, message.c_str(),
                                raw_tree, 1, parents), "Commit selected files");
    return {{"commit", git_oid_tostr_s(&id)}, {"index_synced", git_index_write(raw_index) == 0}};
}

Json push_branch(git_repository* repo, const Json& request, const std::string& token) {
    const auto branch = local_branch(request);
    git_remote* raw = nullptr;
    check_git(git_remote_lookup(&raw, repo, "origin"), "Look up origin");
    const GitPtr<git_remote, git_remote_free> remote(raw, git_remote_free);
    const auto* push_url = git_remote_pushurl(raw);
    if (!push_url) push_url = git_remote_url(raw);
    const std::string_view url = push_url ? push_url : "";
    if (!url.starts_with("https://github.com/") && !url.starts_with('/')) {
        throw std::invalid_argument("Origin push URL must use GitHub HTTPS or an absolute local path");
    }
    const auto ref = "refs/heads/" + branch;
    auto refspec = ref + ":" + ref;
    char* refs[] = {refspec.data()};
    const git_strarray refspecs{refs, 1};
    git_push_options options = GIT_PUSH_OPTIONS_INIT;
    std::string rejection;
    // Push can succeed at the transport level while the remote rejects the ref.
    struct PushContext { const std::string& token; std::string& rejection; } context{token, rejection};
    options.callbacks.payload = &context;
    options.callbacks.credentials = [](git_credential** out, const char* url, const char* username,
                                       unsigned allowed, void* payload) {
        return credentials(out, url, username, allowed,
                           const_cast<std::string*>(&static_cast<PushContext*>(payload)->token));
    };
    options.callbacks.push_update_reference = [](const char*, const char* status, void* payload) {
        if (!status) return 0;
        static_cast<PushContext*>(payload)->rejection = status;
        return -1;
    };
    const auto result = git_remote_push(raw, &refspecs, &options);
    if (!rejection.empty()) throw std::runtime_error("Push rejected: " + rejection);
    check_git(result, "Push branch");
    return {{"branch", branch}, {"remote", "origin"}};
}

} // namespace

GitHubAgent::GitHubAgent(Transport transport, std::string token)
    : transport_(std::move(transport)), token_(std::move(token)) {}

Json GitHubAgent::github_request(const Json& request, std::string_view action) const {
    const auto base = "https://api.github.com/repos/" + repo_name(request);
    std::vector<std::string> headers{"Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2026-03-10"};
    if (!token_.empty()) headers.push_back("Authorization: Bearer " + token_);
    if (action == "create_pull_request") {
        if (token_.empty()) throw std::invalid_argument("Set GH_TOKEN or GITHUB_TOKEN to open a pull request");
        const Json body{{"title", field(request, "title")}, {"head", field(request, "head")},
                        {"base", field(request, "base")}, {"body", request.value("body", "")},
                        {"draft", request.value("draft", false)}};
        headers.push_back("Content-Type: application/json");
        const auto response = transport_({base + "/pulls", "POST", body.dump(), headers});
        response.require_success("GitHub create pull request");
        const auto result = Json::parse(response.body);
        return {{"number", result.at("number")}, {"url", result.at("html_url")}};
    }
    const auto page = request.value("page", Json(1));
    if (!page.is_number_integer() || page.get<double>() < 1 || page.get<double>() > 10000) {
        throw std::invalid_argument("Issue page must be an integer from 1 to 10000");
    }
    const auto url = base + "/issues?state=open&per_page=100&page=" + std::to_string(page.get<int>());
    const auto response = transport_({url, "GET", "", headers});
    response.require_success("GitHub list open issues");
    const auto rows = Json::parse(response.body);
    if (!rows.is_array()) throw std::runtime_error("GitHub did not return an issue list");
    auto issues = Json::array();
    for (const auto& row : rows) {
        if (!row.contains("pull_request")) {
            issues.push_back({{"number", row.at("number")}, {"title", row.at("title")}, {"url", row.at("html_url")}});
        }
    }
    return {{"issues", std::move(issues)}, {"page", page}, {"next_page", rows.size() == 100 ? Json(page.get<int>() + 1) : Json(nullptr)}};
}

Json GitHubAgent::run(Json message) const {
    if (!message.is_object()) throw std::invalid_argument("Expected an ARKMessage");
    message["agent_version"] = "github_agent@1.0.0";
    try {
        if (!pending_for(message, "github_agent") || message.at("intent") != "GITHUB") {
            throw std::invalid_argument("GitHubAgent requires a pending GITHUB message");
        }
        struct GitLibrary {
            GitLibrary() { check_git(git_libgit2_init(), "Initialize Git"); }
            ~GitLibrary() { git_libgit2_shutdown(); }
        };
        static const GitLibrary library;
        const auto& request = message.at("payload");
        const auto action = field(request, "action");
        Json output;
        if (action == "get_open_issues" || action == "create_pull_request") {
            output = github_request(request, action);
        } else if (action == "clone_repo") {
            const auto path = field(request, "path");
            const auto url = request.contains("local_source") ? field(request, "local_source")
                : "https://github.com/" + repo_name(request) + ".git";
            if (request.contains("local_source") && !std::filesystem::is_directory(url)) {
                throw std::invalid_argument("Local clone source must be a repository directory");
            }
            if (std::filesystem::symlink_status(path).type() != std::filesystem::file_type::not_found) {
                throw std::invalid_argument("Clone destination must be a new path");
            }
            git_clone_options options = GIT_CLONE_OPTIONS_INIT;
            options.fetch_opts.callbacks.credentials = credentials;
            options.fetch_opts.callbacks.payload = const_cast<std::string*>(&token_);
            git_repository* raw = nullptr;
            const auto result = git_clone(&raw, url.c_str(), path.c_str(), &options);
            const GitPtr<git_repository, git_repository_free> repo(raw, git_repository_free);
            check_git(result, "Clone repository");
            output = {{"path", path}};
        } else {
            const auto repo = open_repository(request);
            if (action == "create_branch") output = create_branch(repo.get(), request);
            else if (action == "commit_files") output = commit_files(repo.get(), request);
            else if (action == "list_changed_files") output = changed_files(repo.get());
            else if (action == "push_branch") output = push_branch(repo.get(), request, token_);
            else throw std::invalid_argument("Unknown GitHub action");
        }
        message["status"] = "DONE";
        message["output"] = std::move(output);
    } catch (const std::exception& error) {
        message["status"] = "FAILED";
        message["quality_score"] = 0.0;
        message["output"] = {{"error_type", "GitHubError"}, {"error_message", error.what()}};
    }
    return message;
}

} // namespace ark
