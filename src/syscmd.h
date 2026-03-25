/// \file syscmd.h
/// \brief Static SysCmd class for rest4git.
/// \author Juniarto Saputra (jsaputra@riseup.net)
/// \version 2.0
/// \date Aug 2022
///
/// Safe command execution helpers. All commands are executed without a
/// shell (fork + execvp) so user-supplied arguments cannot inject shell
/// metacharacters. File existence and path confinement use libc rather
/// than shelling out.

#pragma once
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <array>
#include <vector>
#include <climits>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

const unsigned int MAX_CHAR = 262144;

namespace rest4git
{

class SysCmd
{
public:
  /// Execute a command without invoking a shell. argv[0] is the program
  /// name, the rest are arguments passed verbatim (no interpretation of
  /// ; | & $ etc). Returns combined stdout+stderr.
  static std::string execute_argv(const std::vector<std::string>& argv)
  {
    if (argv.empty())
    {
      return "execute_argv: empty argv";
    }

    int pipefd[2];
    if (pipe(pipefd) != 0)
    {
      return "pipe failed!";
    }

    pid_t pid = fork();
    if (pid < 0)
    {
      close(pipefd[0]);
      close(pipefd[1]);
      return "fork failed!";
    }

    if (pid == 0)
    {
      // Child: redirect stdout+stderr to pipe, then exec without a shell.
      close(pipefd[0]);
      dup2(pipefd[1], STDOUT_FILENO);
      dup2(pipefd[1], STDERR_FILENO);
      close(pipefd[1]);

      std::vector<char*> cargv;
      cargv.reserve(argv.size() + 1);
      for (const auto& a : argv)
      {
        cargv.push_back(const_cast<char*>(a.c_str()));
      }
      cargv.push_back(nullptr);

      execvp(cargv[0], cargv.data());
      // execvp only returns on error
      perror("execvp");
      _exit(127);
    }

    // Parent: read child output
    close(pipefd[1]);
    std::string res;
    std::array<char, MAX_CHAR> buf;
    ssize_t n;
    while ((n = read(pipefd[0], buf.data(), buf.size())) > 0)
    {
      res.append(buf.data(), static_cast<size_t>(n));
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    return res;
  }

  /// Back-compat helper for fixed (trusted) command strings defined in
  /// git_commands.h. Splits on spaces ONLY - no user input must ever be
  /// concatenated into `command` before calling this overload. Routes
  /// that accept user input must call execute_argv() directly with the
  /// user value as a separate vector element.
  static std::string execute(const std::string& command)
  {
    std::vector<std::string> argv;
    std::string tok;
    for (char c : command)
    {
      if (c == ' ')
      {
        if (!tok.empty()) { argv.push_back(tok); tok.clear(); }
      }
      else
      {
        tok += c;
      }
    }
    if (!tok.empty()) argv.push_back(tok);
    return execute_argv(argv);
  }

  /// Check that a path refers to an existing regular file whose
  /// canonical location is inside the current working directory.
  /// Uses realpath() - no shell, no traversal outside the repo root.
  static bool file_exists(const std::string& path)
  {
    if (path.empty())
    {
      return false;
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr)
    {
      return false;
    }
    std::string root(cwd);
    if (root.back() != '/')
    {
      root += '/';
    }

    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) == nullptr)
    {
      return false;
    }

    struct stat st;
    if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode))
    {
      return false;
    }

    std::string canon(resolved);
    return canon.compare(0, root.size(), root) == 0;
  }

  /// Resolve `path` to a canonical path confined under the current
  /// working directory. Returns empty string on any traversal attempt
  /// or non-existent file.
  static std::string safe_repo_path(const std::string& path)
  {
    if (!file_exists(path))
    {
      return {};
    }
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) == nullptr)
    {
      return {};
    }
    return std::string(resolved);
  }
};

}
