/// \file main.cpp
/// \brief Main function for rest4git.
/// \author Juniarto Saputra (jsaputra@riseup.net)
/// \version 2.0
/// \date Aug 2022
///
/// main function for rest4git web application/service
///
/// Security notes (v2.0):
/// - All git commands are executed via fork+execvp (no shell). User
///   input is always passed as a distinct argv element and separated
///   from options by "--" so it cannot inject flags or shell syntax.
/// - File-path parameters are canonicalised and confined to the
///   repository root before use.
/// - The /admin/run maintenance endpoint is gated by a real token
///   check and restricted to a fixed allow-list of git subcommands.
/// - The /raw debug endpoint reads files directly (no shell) and is
///   confined to the repo root.
/// - The /search endpoint filters git ls-files output in-process and
///   sanitises the echoed header value.

#include <bits/stdint-uintn.h>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "crow/crow_all.h"
#include "syscmd.h"
#include "git_commands.h"
#include "auth.h"
#ifdef LIBGIT2_AVAILABLE
  #include "git2api.h"
#endif


bool is_number(const std::string& s)
{
    return !s.empty() &&
           std::find_if(s.begin(),
                        s.end(),
                        [](unsigned char c) {
                          return !std::isdigit(c);
                        }) == s.end();
}

/// Tokenise a trusted base command on spaces into an argv vector.
static std::vector<std::string> base_argv(rest4git::COMMAND c)
{
  std::vector<std::string> out;
  std::string tok;
  for (char ch : rest4git::g_git_commands[c])
  {
    if (ch == ' ')
    {
      if (!tok.empty()) { out.push_back(tok); tok.clear(); }
    }
    else
    {
      tok += ch;
    }
  }
  if (!tok.empty()) out.push_back(tok);
  return out;
}

/// Fix the pretty-format argument for COMMIT_ONE_LINE: the format
/// string contains spaces that belong to a single argv element.
static std::vector<std::string> commit_oneline_argv()
{
  return {
    "git", "--no-pager", "log", "--oneline",
    "--pretty=format:%h <%ae> %as %s"
  };
}

/// Extract a line range [from,to] from multi-line text. Replaces the
/// old `| sed -n` shell pipeline.
static std::string slice_lines(const std::string& text,
                               uint32_t from, uint32_t to)
{
  if (from == 0) from = 1;
  if (to == 0)   to   = from;
  if (from > to) std::swap(from, to);

  std::string out;
  uint32_t line = 1;
  size_t pos = 0;
  while (pos < text.size())
  {
    size_t eol = text.find('\n', pos);
    size_t len = (eol == std::string::npos) ? text.size() - pos
                                            : eol - pos + 1;
    if (line >= from && line <= to)
    {
      out.append(text, pos, len);
    }
    if (line > to) break;
    if (eol == std::string::npos) break;
    pos = eol + 1;
    ++line;
  }
  return out;
}

/// Filter git ls-files output by substring, replacing `| grep`.
static std::string filter_lines(const std::string& text,
                                const std::string& needle)
{
  std::string out;
  size_t pos = 0;
  while (pos < text.size())
  {
    size_t eol = text.find('\n', pos);
    size_t end = (eol == std::string::npos) ? text.size() : eol;
    std::string line = text.substr(pos, end - pos);
    if (needle.empty() || line.find(needle) != std::string::npos)
    {
      out += line;
      out += '\n';
    }
    if (eol == std::string::npos) break;
    pos = eol + 1;
  }
  return out;
}

/// Strip CR/LF to prevent HTTP response splitting when echoing user
/// input in response headers.
static std::string sanitize_header_value(const std::string& v)
{
  std::string out;
  out.reserve(v.size());
  for (char c : v)
  {
    if (c != '\r' && c != '\n')
    {
      out += c;
    }
  }
  return out;
}

int main()
{
  crow::SimpleApp app;

#ifdef NDEBUG
  app.loglevel(crow::LogLevel::Warning);
#else // For attached process debug
  CROW_LOG_INFO << "Process ID: " << getpid();
#endif

  // Load configuration from environment at startup and warn about
  // any missing secrets. See .env.example for the required variables.
  rest4git::Config& cfg = rest4git::Config::get();
  if (!cfg.admin_token_configured())
  {
    CROW_LOG_WARNING
      << "REST4GIT_ADMIN_TOKEN is not set - /admin/* endpoints will "
         "return 503 Service Unavailable";
  }
  if (!cfg.redis_pass_configured())
  {
    CROW_LOG_WARNING
      << "REST4GIT_REDIS_PASS is not set - Redis caching disabled";
  }

// Start of REST routing

  CROW_ROUTE(app, "/")
  ([]() {
    std::stringstream ss;
    rest4git::append_help(ss);
    return ss.str();
  });

#ifdef LIBGIT2_AVAILABLE
  CROW_ROUTE(app, "/testme")
  ([]() {
    /**
     * Placeholder for any REST test
     * Currently: git log something
     */
    std::stringstream ss;
    rest4git::Git2API::get_instance().git_log(ss, 30, false, "src/krn/abap/gen/scsyconv.c");
    return ss.str();
  });

  CROW_ROUTE(app, "/status/v2")
  ([]() {
    std::stringstream ss;
    rest4git::Git2API::get_instance().git_status(ss);
    return ss.str();
  });

  CROW_ROUTE(app, "/branch/v2")
  ([]() {
    std::stringstream ss;
    rest4git::Git2API::get_instance().git_branch(ss);
    return ss.str();
  });

  CROW_ROUTE(app, "/branch/all")
  ([]() {
    std::stringstream ss;
    rest4git::Git2API::get_instance().git_branch(ss, true);
    return ss.str();
  });

  CROW_ROUTE(app, "/branch/v2/current")
  ([]() {
    std::stringstream ss(rest4git::Git2API::get_instance().current_branch_name());
    return ss.str();
  });

  CROW_ROUTE(app, "/check/v2/<path>")
  ([](const std::string& path) {
    std::string param("/");
    param += path;
    std::replace(param.begin(), param.end(), '+', ' ');
    std::stringstream ss;
    rest4git::Git2API::get_instance().git_lf_files(ss, param);
    return ss.str();
  });

  CROW_ROUTE(app, "/blame/v2/<uint>/<uint>/<path>")
  ([](uint32_t fromLine, uint32_t toLine, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      std::stringstream ss;
      rest4git::Git2API::get_instance().git_blame(ss, param,
        std::min(fromLine, toLine), std::max(fromLine, toLine));
      return ss.str();
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/blame/v2/<uint>/<path>")
  ([](uint32_t line, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      std::stringstream ss;
      rest4git::Git2API::get_instance().git_blame(ss, param, line, line);
      return ss.str();
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/blame/v2/<path>")
  ([](const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      std::stringstream ss;
      rest4git::Git2API::get_instance().git_blame(ss, param);
      return ss.str();
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/show/v2/<uint>/<uint>/<path>")
  ([](uint32_t fromLine, uint32_t toLine, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      std::stringstream ss;
      rest4git::Git2API::get_instance().git_show(ss, param, std::min(fromLine, toLine), std::max(fromLine, toLine));
      return ss.str();
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/show/v2/<uint>/<path>")
  ([](uint32_t line, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      std::stringstream ss;
      rest4git::Git2API::get_instance().git_show(ss, param, line, line);
      return ss.str();
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/show/v2/<path>")
  ([](const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      std::stringstream ss;
      rest4git::Git2API::get_instance().git_show(ss, param);
      return ss.str();
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/show/v2")
  ([](const crow::request& req) {
    std::string file;
    uint32_t from = 1;
    uint32_t to = 0;
    // file-path is mandatory
    if (req.url_params.get("file-path") != nullptr)
    {
      file = req.url_params.get("file-path");
      std::replace(file.begin(), file.end(), '+', ' ');
      if (!rest4git::SysCmd::file_exists(file))
      {
        return std::string("File " + file + " not found!");
      }
    }
    else
    {
      return std::string("Argument file-name is mandatory!");
    }
    // from-line name is optional, support one line as well.
    if (req.url_params.get("from-line") != nullptr)
    {
      std::string from_str(req.url_params.get("from-line"));
      if (is_number(from_str))
      {
        from = std::stoi(from_str);
      }
      else
      {
        return std::string("Invalid parameter from-line!");
      }
      if (req.url_params.get("to-line") != nullptr)
      {
        std::string to_str(req.url_params.get("to-line"));
        if (is_number(to_str))
        {
          to = std::stoi(to_str);
        }
        else
        {
          return std::string("Invalid parameter to-line!");
        }
      }
      else
      {
        to = from;
      }
    }
    std::stringstream ss;
    rest4git::Git2API::get_instance().git_show(ss, file, std::min(from, to), std::max(from, to));
    return ss.str();
  });

  CROW_ROUTE(app, "/commit/v2/<uint>/<path>")
  ([](uint32_t numberOfCommits, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      std::stringstream ss;
      rest4git::Git2API::get_instance().git_log(ss, numberOfCommits, false, param);
      return ss.str();
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/commit/oneline/v2/<uint>/<path>")
  ([](uint32_t numberOfCommits, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      std::stringstream ss;
      rest4git::Git2API::get_instance().git_log(ss, numberOfCommits, true, param);
      return ss.str();
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/commit/v2/<uint>")
  ([](uint32_t numberOfCommits) {
    std::stringstream ss;
    rest4git::Git2API::get_instance().git_log(ss, numberOfCommits);
    return ss.str();
  });

  CROW_ROUTE(app, "/commit/oneline/v2/<uint>")
  ([](uint32_t numberOfCommits) {
    std::stringstream ss;
    rest4git::Git2API::get_instance().git_log(ss, numberOfCommits, true);
    return ss.str();
  });

  CROW_ROUTE(app, "/commit/oneline/v2")
  ([]() {
    std::stringstream ss;
    rest4git::Git2API::get_instance().git_log(ss, 50, true);
    return ss.str();
  });

  CROW_ROUTE(app, "/commit/v2")
  ([]() {
    std::stringstream ss;
    rest4git::Git2API::get_instance().git_log(ss, 50);
    return ss.str();
  });
#endif

// git status

  CROW_ROUTE(app, "/status")
  ([]() {
    return rest4git::SysCmd::execute_argv(base_argv(rest4git::COMMAND::STATUS));
  });

// git branch

  CROW_ROUTE(app, "/branch")
  ([]() {
    return rest4git::SysCmd::execute_argv(base_argv(rest4git::COMMAND::BRANCH));
  });

  CROW_ROUTE(app, "/branch/current")
  ([]() {
    return rest4git::SysCmd::execute_argv(base_argv(rest4git::COMMAND::CURRENT_BRANCH));
  });

// git log / commit

  CROW_ROUTE(app, "/commit/oneline/<uint>")
  ([](uint32_t numberOfCommits) {
    auto argv = commit_oneline_argv();
    argv.push_back("-" + std::to_string(numberOfCommits));
    return rest4git::SysCmd::execute_argv(argv);
  });

  CROW_ROUTE(app, "/commit/oneline")
  ([]() {
    auto argv = commit_oneline_argv();
    argv.push_back("-50");
    return rest4git::SysCmd::execute_argv(argv);
  });

  CROW_ROUTE(app, "/commit/oneline/<uint>/<path>")
  ([](uint32_t numberOfCommits, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      auto argv = commit_oneline_argv();
      argv.push_back("-" + std::to_string(numberOfCommits));
      argv.push_back("--");           // end of options
      argv.push_back(param);          // user path as its own argv element
      return rest4git::SysCmd::execute_argv(argv);
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/commit/<uint>/<path>")
  ([](uint32_t numberOfCommits, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      auto argv = base_argv(rest4git::COMMAND::COMMIT);
      argv.push_back("-" + std::to_string(numberOfCommits));
      argv.push_back("--");
      argv.push_back(param);
      return rest4git::SysCmd::execute_argv(argv);
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/commit/<uint>")
  ([](uint32_t numberOfCommits) {
    auto argv = base_argv(rest4git::COMMAND::COMMIT);
    argv.push_back("-" + std::to_string(numberOfCommits));
    return rest4git::SysCmd::execute_argv(argv);
  });

  CROW_ROUTE(app, "/commit")
  ([]() {
    auto argv = base_argv(rest4git::COMMAND::COMMIT);
    argv.push_back("-50");
    return rest4git::SysCmd::execute_argv(argv);
  });

// git blame

  CROW_ROUTE(app, "/blame/<uint>/<uint>/<path>")
  ([](uint32_t fromLine, uint32_t toLine, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      auto argv = base_argv(rest4git::COMMAND::BLAME_LINE);
      argv.push_back(std::to_string(fromLine) + "," + std::to_string(toLine));
      argv.push_back("--");
      argv.push_back(param);
      return rest4git::SysCmd::execute_argv(argv);
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/blame/<uint>/<path>")
  ([](uint32_t line, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      auto argv = base_argv(rest4git::COMMAND::BLAME_LINE);
      argv.push_back(std::to_string(line) + "," + std::to_string(line));
      argv.push_back("--");
      argv.push_back(param);
      return rest4git::SysCmd::execute_argv(argv);
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/blame/<path>")
  ([](const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      auto argv = base_argv(rest4git::COMMAND::BLAME);
      argv.push_back("--");
      argv.push_back(param);
      return rest4git::SysCmd::execute_argv(argv);
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/blame")
  ([](const crow::request& req) {
    auto argv = base_argv(rest4git::COMMAND::BLAME);

    // opt, support also only one line param.
    if (req.url_params.get("from-line") != nullptr)
    {
      std::string from(req.url_params.get("from-line"));
      if (!is_number(from))
      {
        return std::string("Invalid parameter from-line!");
      }
      std::string to = from;
      if (req.url_params.get("to-line") != nullptr)
      {
        to = req.url_params.get("to-line");
        if (!is_number(to))
        {
          return std::string("Invalid parameter to-line!");
        }
      }
      argv.push_back("-L");
      argv.push_back(from + "," + to);
    }
    // opt - note: base command already passes -e; keep param for API compat
    if (req.url_params.get("email") != nullptr)
    {
      argv.push_back("-e");
    }
    // mandatory
    if (req.url_params.get("file-path") == nullptr)
    {
      return std::string("Argument file-path is mandatory!");
    }
    std::string filepath(req.url_params.get("file-path"));
    std::replace(filepath.begin(), filepath.end(), '+', ' ');
    if (!rest4git::SysCmd::file_exists(filepath))
    {
      return std::string("File " + filepath + " not found!");
    }
    argv.push_back("--");
    argv.push_back(filepath);
    return rest4git::SysCmd::execute_argv(argv);
  });

// git ls-files / check filename

  CROW_ROUTE(app, "/check/<path>")
  ([](const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    // List tracked files and filter in-process - no shell, no grep pipe.
    std::string listing = rest4git::SysCmd::execute_argv(
      base_argv(rest4git::COMMAND::CHECK));
    // Preserve legacy behaviour of `grep /<pattern>` by prefixing "/".
    return filter_lines(listing, "/" + param);
  });

// git show / display filename

  CROW_ROUTE(app, "/show/<uint>/<uint>/<path>")
  ([](uint32_t fromLine, uint32_t toLine, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      auto argv = base_argv(rest4git::COMMAND::SHOW);
      argv.push_back(":" + param);
      std::string full = rest4git::SysCmd::execute_argv(argv);
      return slice_lines(full, std::min(fromLine, toLine),
                               std::max(fromLine, toLine));
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/show/<uint>/<path>")
  ([](uint32_t line, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      auto argv = base_argv(rest4git::COMMAND::SHOW);
      argv.push_back(":" + param);
      std::string full = rest4git::SysCmd::execute_argv(argv);
      return slice_lines(full, line, line);
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/show/<path>")
  ([](const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      auto argv = base_argv(rest4git::COMMAND::SHOW);
      argv.push_back(":" + param);
      return rest4git::SysCmd::execute_argv(argv);
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/show")
  ([](const crow::request& req) {
    // file-path is mandatory
    if (req.url_params.get("file-path") == nullptr)
    {
      return std::string("Argument file-name is mandatory!");
    }
    std::string filepath(req.url_params.get("file-path"));
    std::replace(filepath.begin(), filepath.end(), '+', ' ');
    if (!rest4git::SysCmd::file_exists(filepath))
    {
      return std::string("File " + filepath + " not found!");
    }

    auto argv = base_argv(rest4git::COMMAND::SHOW_PARAM);
    argv.push_back(":" + filepath);
    std::string full = rest4git::SysCmd::execute_argv(argv);

    // from-line is optional; to-line defaults to from-line.
    if (req.url_params.get("from-line") == nullptr)
    {
      return full;
    }
    std::string from(req.url_params.get("from-line"));
    if (!is_number(from))
    {
      return std::string("Invalid parameter from-line!");
    }
    std::string to = from;
    if (req.url_params.get("to-line") != nullptr)
    {
      to = req.url_params.get("to-line");
      if (!is_number(to))
      {
        return std::string("Invalid parameter to-line!");
      }
    }
    return slice_lines(full,
                       static_cast<uint32_t>(std::stoul(from)),
                       static_cast<uint32_t>(std::stoul(to)));
  });

// Admin maintenance endpoint
// Protected by X-Admin-Token header - only for ops team use.
// Restricted to a fixed allow-list of read-only git subcommands.

  CROW_ROUTE(app, "/admin/run")
  ([](const crow::request& req) {
    // Enforce authentication - the return value is actually checked.
    switch (rest4git::validate_admin_token(req))
    {
      case rest4git::AuthResult::UNAVAILABLE:
        return crow::response(503,
          "Service Unavailable: admin token not configured");
      case rest4git::AuthResult::FORBIDDEN:
        return crow::response(403, "Forbidden");
      case rest4git::AuthResult::OK:
        break;
    }

    std::string cmd("status");
    if (req.url_params.get("cmd") != nullptr)
    {
      cmd = req.url_params.get("cmd");
    }

    // Allow-list of safe, side-effect-free maintenance commands. The
    // entire value must match one of these - no arguments permitted.
    static const std::set<std::string> allowed = {
      "status", "gc", "fsck", "fetch", "remote", "branch"
    };
    if (allowed.find(cmd) == allowed.end())
    {
      return crow::response(400, "Command not allowed");
    }

    std::vector<std::string> argv = { "git", cmd };
    return crow::response(rest4git::SysCmd::execute_argv(argv));
  });

// Raw file accessor for quick debugging of working-tree content.
// Reads files directly with ifstream (no shell) and confines the
// resolved path to the repository root.

  CROW_ROUTE(app, "/raw/<path>")
  ([](const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');

    std::string safe = rest4git::SysCmd::safe_repo_path(param);
    if (safe.empty())
    {
      return crow::response(404, "File " + param + " not found!");
    }

    std::ifstream f(safe, std::ios::binary);
    if (!f)
    {
      return crow::response(404, "File " + param + " not found!");
    }
    std::ostringstream buf;
    buf << f.rdbuf();
    return crow::response(buf.str());
  });

// File search with result caching hint via response header.

  CROW_ROUTE(app, "/search")
  ([](const crow::request& req) {
    crow::response res;
    std::string pattern;
    if (req.url_params.get("pattern") != nullptr)
    {
      pattern = req.url_params.get("pattern");
      // Echo pattern back for client-side cache keying. Strip CR/LF to
      // prevent HTTP response splitting / header injection.
      res.set_header("X-Search-Pattern", sanitize_header_value(pattern));
    }
    // List tracked files (no shell) and filter in-process.
    std::string listing = rest4git::SysCmd::execute_argv(
      base_argv(rest4git::COMMAND::CHECK));
    res.body = filter_lines(listing, pattern);
    return res;
  });

// End of REST routing

  // Bind to loopback only - this is an internal tool. Expose via a
  // reverse proxy if remote access is required.
  app.bindaddr("127.0.0.1").port(8000).multithreaded().run();

  return 0;
}
