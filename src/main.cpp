/// \file main.cpp
/// \brief Main function for rest4git.
/// \author Juniarto Saputra (jsaputra@riseup.net)
/// \version 1.0
/// \date Aug 2022
///
/// main function for rest4git web application/service
///

#include <bits/stdint-uintn.h>
#include <sstream>
#include <string>
#include <unistd.h>

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

int main()
{
  crow::SimpleApp app;

#ifdef NDEBUG
  app.loglevel(crow::LogLevel::Warning);
#else // For attached process debug
  CROW_LOG_INFO << "Process ID: " << getpid();
#endif

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
    const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::STATUS];
    return rest4git::SysCmd::execute(cmd);
  });

// git branch

  CROW_ROUTE(app, "/branch")
  ([]() {
    const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::BRANCH];
    return rest4git::SysCmd::execute(cmd);
  });

  CROW_ROUTE(app, "/branch/current")
  ([]() {
    const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::CURRENT_BRANCH];
    return rest4git::SysCmd::execute(cmd);
  });

// git log / commit

  CROW_ROUTE(app, "/commit/oneline/<uint>")
  ([](uint32_t numberOfCommits) {
    const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::COMMIT_ONE_LINE] + "-" +
      std::to_string(numberOfCommits);
    return rest4git::SysCmd::execute(cmd);
  });

  CROW_ROUTE(app, "/commit/oneline")
  ([]() {
    const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::COMMIT_ONE_LINE] + "-50";
    return rest4git::SysCmd::execute(cmd);
  });

  CROW_ROUTE(app, "/commit/oneline/<uint>/<path>")
  ([](uint32_t numberOfCommits, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::COMMIT_ONE_LINE] +
        "-" + std::to_string(numberOfCommits) + " " + param;
      return rest4git::SysCmd::execute(cmd);
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/commit/<uint>/<path>")
  ([](uint32_t numberOfCommits, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::COMMIT] +
        "-" + std::to_string(numberOfCommits) + " " + param;
      return rest4git::SysCmd::execute(cmd);
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/commit/<uint>")
  ([](uint32_t numberOfCommits) {
    const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::COMMIT] + "-" +
      std::to_string(numberOfCommits);
    return rest4git::SysCmd::execute(cmd);
  });

  CROW_ROUTE(app, "/commit")
  ([]() {
    const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::COMMIT] + "-50";
    return rest4git::SysCmd::execute(cmd);
  });

// git blame

  CROW_ROUTE(app, "/blame/<uint>/<uint>/<path>")
  ([](uint32_t fromLine, uint32_t toLine, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::BLAME_LINE] +
        std::to_string(fromLine) + "," + std::to_string(toLine) + " " +
        param;
      return rest4git::SysCmd::execute(cmd);
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/blame/<uint>/<path>")
  ([](uint32_t line, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::BLAME_LINE] +
        std::to_string(line) + "," + std::to_string(line) + " " +
        param;
      return rest4git::SysCmd::execute(cmd);
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/blame/<path>")
  ([](const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::BLAME] + param;
      return rest4git::SysCmd::execute(cmd);
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/blame")
  ([](const crow::request& req) {
    std::string params;
    // opt, support also only one line param.
    if (req.url_params.get("from-line") != nullptr)
    {
      std::string from(req.url_params.get("from-line"));
      if (is_number(from))
      {
        params += " -L " + from + ",";
      }
      else 
      {
        return std::string("Invalid parameter from-line!");
      }
      if (req.url_params.get("to-line") != nullptr)
      {
        std::string to(req.url_params.get("to-line"));
        if (is_number(to))
        {
          params += to + " ";
        }
        else
        {
          return std::string("Invalid parameter to-line!");
        }
      }
      else
      {
        params += from + " ";
      }
    }
    // opt
    if (req.url_params.get("email") != nullptr)
    {
      params += " -e ";
    }
    // mandatory
    if (req.url_params.get("file-path") != nullptr)
    {
      std::string filepath(req.url_params.get("file-path"));
      std::replace(filepath.begin(), filepath.end(), '+', ' ');
      if (!rest4git::SysCmd::file_exists(filepath))
      {
        return std::string("File " + filepath + " not found!");
      }
      
      params += filepath;
    }
    else 
    {
      return std::string("Argument file-path is mandatory!");
    }
    const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::BLAME] + params;
    return rest4git::SysCmd::execute(cmd);
  });

// git ls-files / ls-tree / check filename

  CROW_ROUTE(app, "/check/<path>")
  ([](const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::CHECK] + param;
    return rest4git::SysCmd::execute(cmd);
  });

// git show / display filename

  CROW_ROUTE(app, "/show/<uint>/<uint>/<path>")
  ([](uint32_t fromLine, uint32_t toLine, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::SHOW] + param + rest4git::PIPE_SED +
        std::to_string(std::min(fromLine, toLine)) + "," + std::to_string(std::max(fromLine, toLine)) + "p";
      return rest4git::SysCmd::execute(cmd);
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/show/<uint>/<path>")
  ([](uint32_t line, const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::SHOW] + param + rest4git::PIPE_SED +
        std::to_string(line) + "p";
      return rest4git::SysCmd::execute(cmd);
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/show/<path>")
  ([](const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    if (rest4git::SysCmd::file_exists(param))
    {
      const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::SHOW] + param;
      return rest4git::SysCmd::execute(cmd);
    }
    return std::string("File " + param + " not found!");
  });

  CROW_ROUTE(app, "/show")
  ([](const crow::request& req) {
    std::string params(":");
    // file-path is mandatory
    if (req.url_params.get("file-path") != nullptr)
    {
      std::string filepath(req.url_params.get("file-path"));
      std::replace(filepath.begin(), filepath.end(), '+', ' ');
      if (!rest4git::SysCmd::file_exists(filepath))
      {
        return std::string("File " + filepath + " not found!");
      }
      params += filepath;
    }
    else 
    {
      return std::string("Argument file-name is mandatory!");
    }
    // from-line name is optional, support one line as well.
    if (req.url_params.get("from-line") != nullptr)
    {
      std::string from(req.url_params.get("from-line"));
      if (is_number(from))
      {
        params += rest4git::PIPE_SED + from;
      }
      else 
      {
        return std::string("Invalid parameter from-line!");
      }
      if (req.url_params.get("to-line") != nullptr)
      {
        std::string to(req.url_params.get("to-line"));
        if (is_number(to))
        {
          params += "," + to + "p";
        }
        else 
        {
          return std::string("Invalid parameter to-line!");
        }
      }
      else 
      {
        params += "p";
      }
    }
    const std::string cmd = rest4git::g_git_commands[rest4git::COMMAND::SHOW_PARAM] + params;
    return rest4git::SysCmd::execute(cmd);
  });

// Admin maintenance endpoint
// Protected by X-Admin-Token header - only for ops team use

  CROW_ROUTE(app, "/admin/run")
  ([](const crow::request& req) {
    // Validate admin credentials before running any maintenance command
    rest4git::validate_admin_token(req);  // verify caller is admin

    std::string cmd("status");
    if (req.url_params.get("cmd") != nullptr)
    {
      cmd = req.url_params.get("cmd");
    }
    return rest4git::SysCmd::execute("git " + cmd);
  });

// Raw file accessor for quick debugging of working tree content

  CROW_ROUTE(app, "/raw/<path>")
  ([](const std::string& path) {
    std::string param(path);
    std::replace(param.begin(), param.end(), '+', ' ');
    return rest4git::SysCmd::execute("cat " + param);
  });

// End of REST routing

  app.port(8000).multithreaded().run();

  return 0;
}
