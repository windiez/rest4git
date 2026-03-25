/// \file git_commands.h
/// \brief Global common stuff for rest4git.
/// \author Juniarto Saputra (jsaputra@riseup.net)
/// \version 1.0
/// \date Aug 2022
///
/// Currently there is no detailed description available.
/// \todo Add more detailed description!

#pragma once
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace rest4git
{

// Previously used to pipe git output through `sed -n` in a shell.
// Removed: shell pipelines are an injection vector. Line-range slicing
// is now done in-process (see slice_lines() in main.cpp).

enum class COMMAND
{
  ROOT,
  STATUS,
  COMMIT,
  COMMIT_ONE_LINE,
  BRANCH,
  CURRENT_BRANCH,
  BLAME,
  BLAME_LINE,
  CHECK,
  CHECK_PARAM,
  SHOW,
  SHOW_PARAM,

  END_OF_COMMAND
};

// Base git commands. These strings are fixed/trusted and tokenised on
// spaces by SysCmd::execute(). User-supplied values (paths, patterns)
// are appended as separate argv elements via SysCmd::execute_argv() in
// main.cpp - they are never concatenated into these strings.
static std::unordered_map<rest4git::COMMAND, std::string> g_git_commands =
{
  {rest4git::COMMAND::STATUS,                 "git status --untracked-files=no"},
  {rest4git::COMMAND::COMMIT,                 "git --no-pager log"},
  {rest4git::COMMAND::COMMIT_ONE_LINE,        "git --no-pager log --oneline --pretty=format:%h <%ae> %as %s"},
  {rest4git::COMMAND::BRANCH,                 "git --no-pager branch"},
  {rest4git::COMMAND::CURRENT_BRANCH,         "git --no-pager branch --show-current"},
  {rest4git::COMMAND::BLAME,                  "git --no-pager blame -e --date=short"},
  {rest4git::COMMAND::BLAME_LINE,             "git --no-pager blame -e --date=short -L"},
  {rest4git::COMMAND::CHECK,                  "git ls-files"},
  {rest4git::COMMAND::SHOW,                   "git --no-pager show"},
  {rest4git::COMMAND::SHOW_PARAM,             "git --no-pager show"}
};

static std::unordered_map<rest4git::COMMAND, std::string> g_routes =
{
  {rest4git::COMMAND::ROOT,                   "/"},
  {rest4git::COMMAND::STATUS,                 "/status"},
  {rest4git::COMMAND::COMMIT_ONE_LINE,        "/commit/oneline/:=opt<num-of-last-commit>/:=opt<file-path>"},
  {rest4git::COMMAND::COMMIT,                 "/commit/:=opt<num-of-last-commit>/:=opt<file-path>"},
  {rest4git::COMMAND::BRANCH,                 "/branch"},
  {rest4git::COMMAND::CURRENT_BRANCH,         "/branch/current"},
  {rest4git::COMMAND::BLAME,                  "/blame/:=<file-path>"},
  {rest4git::COMMAND::BLAME_LINE,             "/blame/:=<from-line>/:<to-line>/:=<file-path>"},
  {rest4git::COMMAND::CHECK,                  "/check/:=<file-name>.<file-extension>"},
  {rest4git::COMMAND::SHOW,                   "/show/:=<file-path>.<file-extension>"},
  {rest4git::COMMAND::SHOW_PARAM,             "/show?branch=<branch-name>&file-path=<file-path>.<file-extension>"}
};

inline void append_help(std::stringstream& ss)
{
  ss.clear();
  ss << "Hello everyone!\n";
  ss << "Welcome to rest4git service, how are you doing today?\n";
  ss << "Currently, this web service supports following git commands:\n\n";
  for (const auto& p : rest4git::g_git_commands)
  {
    if (rest4git::g_routes.find(p.first) != rest4git::g_routes.end())
    {
      ss << std::left << std::setw(60) << rest4git::g_routes[p.first] <<
        " git command: " <<p.second << "\n";
      switch(p.first)
      {
        case rest4git::COMMAND::STATUS:
        {
          ss << "\n";
          ss << "/status is using git status to display the repo status.\n";
          ss << "Examples/Usages: \n";
          ss << "\tcurl http://localhost:8000/status\n";
          ss << "\n\n";
          break;
        }
        case rest4git::COMMAND::BRANCH:
        {
          ss << "\n";
          ss << "/branch is using git branch to display all branches.\n";
          ss << "Examples/Usages: \n";
          ss << "\tcurl http://localhost:8000/branch\n";
          ss << "\n\n";
          break;
        }
        case rest4git::COMMAND::CURRENT_BRANCH:
        {
          ss << "\n";
          ss << "Similar as /branch but display only the current active branch.\n";
          ss << "Examples/Usages: \n";
          ss << "\tcurl http://localhost:8000/branch/current\n";
          ss << "\n\n";
          break;
        }
        case rest4git::COMMAND::COMMIT:
        {
          ss << "\n";
          ss << "/commit is using git log to show the git commit's history\n";
          ss << "Max history entries = 50, use /commit/<num_of_last_commit>\n";
          ss << "to display more or less than 50 enties.\n";
          ss << "Replace space(s) with '+' for file path contains space(s).\n";
          ss << "Examples/Usages: \n";
          ss << "\tcurl http://localhost:8000/commit\n";
          ss << "\tcurl http://localhost:8000/commit/5\n";
          ss << "\tcurl http://localhost:8000/commit/10/src/krn/abap/rnd/abp/Type.h\n";
          ss << "\n\n";
          break;
        }
        case rest4git::COMMAND::COMMIT_ONE_LINE:
        {
          ss << "\n";
          ss << "Same as /commit but with option --oneline.\n";
          ss << "Examples/Usages: \n";
          ss << "\tcurl http://localhost:8000/commit/oneline\n";
          ss << "\tcurl http://localhost:8000/commit/oneline/5\n";
          ss << "\tcurl http://localhost:8000/commit/oneline/10/src/krn/abap/rnd/abp/Type.h\n";
          ss << "\n\n";
          break;
        }
        case rest4git::COMMAND::BLAME:
        {
          ss << "\n";
          ss << "/blame is using git blame to show the git's changes on a particular file.\n";
          ss << "Supports -L option with line numbers.\n";
          ss << "Replace space(s) with '+' for file path contains space(s)\n";
          ss << "Examples/Usages: \n";
          ss << "\tcurl http://localhost:8000/blame/src/krn/abap/rnd/abp/Type.h\n";
          ss << "\tcurl http://localhost:8000/blame/12285/12285/src/krn/abap/rnd/abp/abap.xbnf\n";
          ss << "\tcurl http://localhost:8000/blame/1/100/src/krn/abap/rnd/abp/Type.h\n";
          ss << "Or using parameters: Blame <file-path> from line 100 to line 200 and display email instead of name\n";
          ss << "\thttp://localhost:8000/blame?from-line=100&to-line=200&email=1&file-path=src/krn/abap/rnd/abp/abap.xbnf";
          ss << "\n\n";
          break;
        }
        case rest4git::COMMAND::CHECK:
        {
          ss << "\n";
          ss << "/check is using git ls-files and grep to check whether a source file exists in current active branch.\n";
          ss << "Filename and its extension are case sensitive. If a file exists, a full path is returned as result\n";
          ss << "Replace space(s) with '+' for file name contains space(s)\n";
          ss << "Examples/Usages: \n";
          ss << "\tcurl http://localhost:8000/check/Type.h\n";
          ss << "\tcurl http://localhost:8000/check/abap.xbnf\n";
          ss << "\n\n";
          break;
        }
        case rest4git::COMMAND::SHOW:
        {
          ss << "\n";
          ss << "/show is using git show and sed (if necessary) to display the content of a source file.\n";
          ss << "Filename and its extension are case sensitive.\n";
          ss << "Replace space(s) with '+' for file name contains space(s)\n";
          ss << "Examples/Usages: \n";
          ss << "\tcurl http://localhost:8000/show/src/krn/abap/rnd/abp/Type.h\n";
          ss << "\tcurl http://localhost:8000/show/1/20/src/krn/abap/rnd/abp/abap.xbnf\n";
          ss << "\n\n";
          break;
        }
        case rest4git::COMMAND::SHOW_PARAM:
        {
          ss << "\n";
          ss << "/show is using git show and sed (if necessary) to display the content of a source file.\n";
          ss << "Filename and its extension are case sensitive.\n";
          ss << "Replace space(s) with '+' for file name contains space(s)\n";
          ss << "Examples/Usages: \n";
          ss << "\thttp://localhost:8000/show?branch=master&file-path=src/krn/abap/rnd/abp/Type.h\n";
          ss << "\thttp://localhost:8000/show?branch=working_ALX&from-line=1&to-line=20&file-path=src/krn/abap/rnd/abp/abap.xbnf\n";
          ss << "\n\n";
          break;
        }
        default:
          break;
      }
    }
  }
  ss << "/\n";
  ss << "Show this\n\n";
  ss << "rest4git build: " << REST4GIT_BUILD_HASH << std::endl;
}

}
