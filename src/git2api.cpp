/// \file git2api.cpp
/// \brief Implementation for rest4git::Git2API.
/// \author Juniarto Saputra (jsaputra@riseup.net)
/// \version 1.0
/// \date Dec 2022
///
/// Implementation for Git2API singleton class
///




#include <git2/branch.h>
#ifdef LIBGIT2_AVAILABLE
#include <cstdio>
#include <ctime>
#include <git2/commit.h>
#include <git2/diff.h>
#include <git2/object.h>
#include <git2/pathspec.h>
#include <git2/revwalk.h>
#include <git2/tree.h>
#include <git2/types.h>
#include <git2/refs.h>
#include <sstream>
#include <cstddef>
#include <cstdint>
#include <string>
#include <iostream>

#include "git2api.h"
#include "utils.h"
#include "crow/crow_all.h"

namespace rest4git
{

#define GIT_OID_SHA1_HEX  40
#define GIT_OID_SHA1_HEX_SHORT  11

namespace
{

bool next_line_bounds(const char* rawdata, int64_t rawsize, std::size_t offset, std::size_t& line_size, std::size_t& next_offset)
{
  if (offset >= static_cast<std::size_t>(rawsize))
  {
    return false;
  }

  const char* line_start = rawdata + offset;
  const std::size_t remaining = static_cast<std::size_t>(rawsize) - offset;
  const char* eol = static_cast<const char*>(memchr(line_start, '\n', remaining));
  if (eol == nullptr)
  {
    line_size = remaining;
    next_offset = static_cast<std::size_t>(rawsize);
    return true;
  }

  line_size = static_cast<std::size_t>(eol - line_start);
  next_offset = static_cast<std::size_t>(eol - rawdata) + 1;
  return true;
}

} // namespace

void convert_git_time_to_string(const git_time* input, std::string& output)
{
  char sign;
  char time[32];
  int offset = input->offset;
  if (offset < 0)
  {
    sign = '-';
    offset = -offset;
  }
  else
  {
    sign = '+';
  }
  int h = offset / 60;
  int m = offset % 60;
  time_t t = static_cast<time_t>(input->time) + (input->offset * 60);
  struct tm* gmt = gmtime(&t);
  strftime(time, sizeof(time), "%a %b %e %T %Y", gmt);
  output = time;
  output += " ";
  output += sign;
  output += std::string(2 - 
    std::min(2, static_cast<int>(std::to_string(h).length())), '0') + 
    std::to_string(h);
  output += std::string(2 - 
    std::min(2, static_cast<int>(std::to_string(m).length())), '0') + 
    std::to_string(m);
}

bool match_with_parent(git_commit* commit, uint32_t i, git_diff_options* opt)
{
  git_commit* parent(nullptr);
  int ndeltas(0);
  if (git_commit_parent(&parent, commit, i) == 0)
  {
    git_tree* a(nullptr), *b(nullptr);
    if (git_commit_tree(&a, parent) == 0 && git_commit_tree(&b, commit) == 0)
    {
      git_diff *diff;
      if (git_diff_tree_to_tree(&diff, 
                                git_commit_owner(commit), 
                                a, 
                                b, 
                                opt) == 0)
      {
        ndeltas = git_diff_num_deltas(diff);
      }
      git_diff_free(diff);
    }
    git_tree_free(a);
    git_tree_free(b);
  }
  git_commit_free(parent);

  return (ndeltas > 0);
}

void print_log(std::stringstream& ss, git_commit* commit)
{
  char buf[GIT_OID_SHA1_HEX + 1];
  git_oid_tostr(buf, sizeof(buf), git_commit_id(commit));
  ss << "commit " << buf << std::endl;

  const git_signature* signature = git_commit_author(commit);
  if (signature)
  {
    ss << "Author: " << signature->name << " <" << signature->email << ">" << std::endl;
    std::string when;
    convert_git_time_to_string(&signature->when, when);
    ss << "Date:\t" << when << std::endl << std::endl;
  }

  const char* scan = nullptr;
  const char* eol = nullptr;
  for (scan = git_commit_message(commit); scan && *scan;)
  {
    for (eol = scan; *eol && *eol != '\n'; ++eol);
    char message[1024];
    snprintf(message, sizeof(message), "    %.*s\n", 
      (int)(eol - scan), 
      scan);
    ss << message;
    scan = *eol ? eol + 1 : nullptr;
  }
}

void print_log_oneline(std::stringstream& ss, git_commit* commit)
{
  char buf[GIT_OID_SHA1_HEX_SHORT + 1];
  git_oid_tostr(buf, sizeof(buf), git_commit_id(commit));
  ss << buf;

  const git_signature* signature = git_commit_author(commit);
  if (signature)
  {
    char date[11] = {0};
    strftime(date, 11, "%Y-%m-%d", localtime(&signature->when.time));
    ss << " <" << signature->email << "> " << date << " ";
  }

  std::string msg(git_commit_message(commit));
  size_t eol = msg.find("\n\n");
  if (eol != std::string::npos)
  {
    msg = msg.substr(0, eol);
    unsigned i = 0;
    while (i < msg.length())
    {
      if (msg[i] == '\n')
      {
        msg[i] = ' ';
      }
      i++;
    }
  }
  ss << msg;
}

Git2API& Git2API::get_instance()
{
  static Git2API instance;
  return instance;
}

Git2API::Git2API()
  : m_repo(nullptr, git_repository_free)
  , m_ref(nullptr, git_reference_free)
{
  git_libgit2_init();
  git_repository* repo = nullptr;
  int err = git_repository_open(&repo, rest4git::Utils::pwd().c_str());
  CROW_LOG_INFO << "pwd: " << rest4git::Utils::pwd().c_str();
  if (err != 0)
  {
    CROW_LOG_ERROR << "git_repository_open() err = " << err;
    CROW_LOG_ERROR << giterr_last()->message;
    git_libgit2_shutdown();
  }
  else
  {
    // Transfer ownership of repo and head
    std::unique_ptr<git_repository, decltype(&git_repository_free)> unique_repo{std::move(repo), git_repository_free};
    m_repo = std::move(unique_repo);

    git_reference* ref = nullptr;
    err = git_repository_head(&ref, m_repo.get());
    CROW_LOG_INFO << "git_repository_head() err: " << err;
    if (err == 0)
    {
      std::unique_ptr<git_reference, decltype(&git_reference_free)> unique_ref{std::move(ref), git_reference_free};
      m_ref = std::move(unique_ref);
      m_current_branch_name = git_reference_shorthand(m_ref.get());
    }
    else
    {
      CROW_LOG_ERROR << "git_repository_head() err = " << err;
      CROW_LOG_ERROR << giterr_last()->message;
    }
  }
}

Git2API::~Git2API()
{
  (void)m_ref.release();
  (void)m_repo.release();
  int err = git_libgit2_shutdown();
  CROW_LOG_INFO << "git_libgit2_shutdown() err: " << err;
}

bool Git2API::okay() const
{
  return m_repo != nullptr ? true : false;
}

bool Git2API::okay(std::stringstream &ss) const
{
  if (!okay())
  {
    ss << "Repository is not opened or uninitialized" << std::endl;
    return false;
  }

  return true;
}

const std::string& Git2API::current_branch_name() const
{
  return m_current_branch_name;
}

void Git2API::git_status(std::stringstream &ss)
{
  ss.clear();
  ss << "rest4git build: " << REST4GIT_BUILD_HASH << std::endl;
  ss << "pwd: " << rest4git::Utils::pwd().c_str() << std::endl;

  if (!okay(ss))
  {
    return;
  }

  // git status implementation
  if (!m_current_branch_name.empty())
  {
    ss << "On branch " << m_current_branch_name << std::endl;
  }
  else
  {
    ss << "Not currently on any branch." << std::endl;
    return;
  }

  git_status_options opts { GIT_STATUS_OPTIONS_VERSION, GIT_STATUS_SHOW_INDEX_ONLY };
  opts.flags = GIT_STATUS_OPT_DEFAULTS;
  git_status_list* status = NULL;

  int err = git_status_list_new(&status, m_repo.get(), &opts);
  CROW_LOG_INFO << "git_status_list_new() err: " << err;
  if (err == 0)
  {
    if (git_status_list_entrycount(status) == 0)
    {
      ss << "Repository is clean" << std::endl;
    }
    else
    {
      ss << "Repository is dirty" << std::endl;
    }
    git_status_list_free(status);
  }
  else
  {
    ss << "git_status_list_new() err = " << err << std::endl;
    CROW_LOG_ERROR << "git_status_list_new() err = " << err;
    CROW_LOG_ERROR << giterr_last()->message;
  }
}

void Git2API::git_branch(std::stringstream &ss, bool all)
{
  ss.clear();

  if (!okay(ss))
  {
    return;
  }

  git_branch_iterator* iter = nullptr;
  git_branch_t flags = { GIT_BRANCH_LOCAL };

  if (all)
  {
    flags = { GIT_BRANCH_ALL };
  }

  int err = git_branch_iterator_new(&iter, m_repo.get(), flags);
  CROW_LOG_INFO << "git_branch_iterator_new() err: " << err;
  if (err != 0)
  {
    ss << "git_branch_iterator_new() err = " << err << std::endl;
    CROW_LOG_ERROR << "git_status_list_new() err = " << err;
    CROW_LOG_ERROR << giterr_last()->message;

    return;
  }

  git_reference* out;
  git_branch_t type;
  while (git_branch_next(&out, &type, iter) != GIT_ITEROVER)
  {
    const char* branch = nullptr;
    if (!git_branch_name(&branch, out))
    {
      if (git_branch_is_checked_out(out))
      {
        ss << "--->\t" << branch << std::endl;
      }
      else
      {
        ss << "\t" << branch << std::endl;
      }
    }
  }
  git_branch_iterator_free(iter);
}

void Git2API::git_lf_files(std::stringstream& ss, const std::string& pattern)
{
  ss.clear();

  if (!okay(ss))
  {
    return;
  }

  git_index* index = NULL;
  int err = git_repository_index(&index, m_repo.get());
  CROW_LOG_INFO << "git_repository_index() err: " << err;
  if (err != 0)
  {
    ss << "git_repository_index() err = " << err << std::endl;
    CROW_LOG_ERROR << "git_repository_index() err = " << err;
    CROW_LOG_ERROR << giterr_last()->message;

    return;
  }

  const size_t count = git_index_entrycount(index);
  bool first = true;
  for (size_t i = 0; i < count; ++i)
  {
    const git_index_entry* entry = git_index_get_byindex(index, i);
    if (pattern.empty())
    {
      if (!first)
      {
        ss << std::endl;
      }
      ss << entry->path;
      first = false;
    }
    else
    {
      std::string path(entry->path);
      if (rest4git::Utils::find(path, pattern) != std::string::npos)
      {
        if (!first)
        {
          ss << std::endl;
        }
        ss << entry->path;
        first = false;
      }
    }
  }
  git_index_free(index);
}

void Git2API::git_blame(std::stringstream &ss, const std::string& file, uint32_t from, uint32_t to)
{
  ss.clear();

  if (!okay(ss))
  {
    return;
  }

  struct git_blame* blame = nullptr;
  git_blame_options blameopts = GIT_BLAME_OPTIONS_INIT;
  blameopts.min_line = from;
  blameopts.max_line = to;

  int err = git_blame_file(&blame, m_repo.get(), file.c_str(), &blameopts);
  CROW_LOG_INFO << "git_blame_file() err: " << err;
  if (err != 0)
  {
    ss << "git_blame_file() err = " << err << std::endl;
    CROW_LOG_ERROR << "git_blame_file() err = " << err;
    CROW_LOG_ERROR << giterr_last()->message;

    return;
  }

  std::string spec("HEAD");
  if (!file.empty())
  {
    spec += ":";
    spec += file;
  }

  git_object *obj = nullptr;
  err = git_revparse_single(&obj, m_repo.get(), spec.c_str());
  CROW_LOG_INFO << "git_revparse_single() err: " << err;
  if (err != 0)
  {
    ss << "git_revparse_single() err = " << err << std::endl;
    CROW_LOG_ERROR << "git_revparse_single() err = " << err;
    CROW_LOG_ERROR << giterr_last()->message;
    git_blame_free(blame);

    return;
  }

  git_blob *blob = nullptr;
  err = git_blob_lookup(&blob, m_repo.get(), git_object_id(obj));
  CROW_LOG_INFO << "git_blob_lookup() err: " << err;
  if (err != 0)
  {
    ss << "git_blob_lookup() err = " << err << std::endl;
    CROW_LOG_ERROR << "git_blob_lookup() err = " << err;
    CROW_LOG_ERROR << giterr_last()->message;
    git_object_free(obj);
    git_blame_free(blame);

    return;
  }

  git_object_free(obj);

  const char* rawdata = static_cast<const char*>(git_blob_rawcontent(blob));
  const int64_t rawsize = git_blob_rawsize(blob);

  uint32_t line = 1;
  std::size_t i = 0;
  while (i < static_cast<std::size_t>(rawsize))
  {
    std::size_t line_size = 0;
    std::size_t next_offset = 0;
    if (!next_line_bounds(rawdata, rawsize, i, line_size, next_offset))
    {
      break;
    }

    const git_blame_hunk* hunk = git_blame_get_hunk_byline(blame, line);
    if (hunk)
    {
      char oid[13] = {0};
      char sig[65] = {0};
      char date[11] = {0};
      char out[1024] = {0};
      git_oid_tostr(oid, 13, &hunk->final_commit_id);
      snprintf(sig, 65, "<%s>", hunk->final_signature->email);
      strftime(date, 11, "%Y-%m-%d", localtime(&hunk->final_signature->when.time));
      snprintf(out, 1024,
"%s (%-28s %-10s %3d) %.*s\n",
        oid,
        sig, 
        date, 
        line, 
        static_cast<int>(line_size),
        rawdata + i);
      ss << out;
    }
    i = next_offset;
    line++;
  }
  

  git_blob_free(blob);
  git_blame_free(blame);
}

void Git2API::git_show(std::stringstream &ss, const std::string &file, uint32_t from, uint32_t to)
{
  ss.clear();

  if (!okay(ss))
  {
    return;
  }

  std::string spec("HEAD:");
  spec += file;

  git_object *obj = nullptr;
  git_blob *blob = nullptr;
  int err = git_revparse_single(&obj, m_repo.get(), spec.c_str());
  CROW_LOG_INFO << "git_revparse_single() err: " << err;
  if (err != 0)
  {
    ss << "git_revparse_single() err = " << err << std::endl;
    CROW_LOG_ERROR << "git_revparse_single() err = " << err;
    CROW_LOG_ERROR << giterr_last()->message;

    return;
  }

  err = git_blob_lookup(&blob, m_repo.get(), git_object_id(obj));
  CROW_LOG_INFO << "git_blob_lookup() err: " << err;
  if (err != 0)
  {
    ss << "git_blob_lookup() err = " << err << std::endl;
    CROW_LOG_ERROR << "git_blob_lookup() err = " << err;
    CROW_LOG_ERROR << giterr_last()->message;
    git_object_free(obj);

    return;
  }

  git_object_free(obj);
  const char* rawdata = static_cast<const char*>(git_blob_rawcontent(blob));
  const int64_t rawsize = git_blob_rawsize(blob);

  if (from == 1 && to == 0)
  {
    ss.write(rawdata, rawsize);
  }
  else
  {
    uint32_t line = 1;
    std::size_t i = 0;
    while (i < static_cast<std::size_t>(rawsize))
    {
      std::size_t line_size = 0;
      std::size_t next_offset = 0;
      if (!next_line_bounds(rawdata, rawsize, i, line_size, next_offset))
      {
        break;
      }

      if (line >= from && line <= to)
      {
        char out[1024] = {0};
        snprintf(out, 1024, "%.*s\n", static_cast<int>(line_size), rawdata + i);
        ss << out;
      }
      i = next_offset;
      line++;
    }
  }
  git_blob_free(blob);
}

void Git2API::git_log(std::stringstream &ss, uint32_t max, bool oneline, const std::string& file)
{
  ss.clear();

  if (!okay(ss))
  {
    return;
  }

  git_revwalk *walker = nullptr;
  int err = git_revwalk_new(&walker, m_repo.get());
  CROW_LOG_INFO << "git_revwalk_new() err: " << err;
  if (err != 0)
  {
    CROW_LOG_ERROR << "git_revwalk_new() err = " << err;
    CROW_LOG_ERROR << giterr_last()->message;
    
    return;
  }

  err = git_revwalk_push_ref(walker, git_reference_name(m_ref.get()));
  CROW_LOG_INFO << "git_revwalk_push_ref() err: " << err;
  if (err != 0)
  {
    CROW_LOG_ERROR << "git_revwalk_push_ref() err = " << err;
    CROW_LOG_ERROR << giterr_last()->message;
    git_revwalk_free(walker);

    return;
  }

  git_revwalk_simplify_first_parent(walker);

  git_pathspec* pathspec = nullptr;
  git_diff_options opt = GIT_DIFF_FIND_OPTIONS_INIT;
  if (!file.empty())
  {
    char* filepath = (char*)file.c_str();
    opt.pathspec.strings = &filepath;
    opt.pathspec.count = 1;

    err = git_pathspec_new(&pathspec, &opt.pathspec);
    if (err != 0)
    {
      CROW_LOG_ERROR << "git_pathspec_new() err = " << err;
      CROW_LOG_ERROR << giterr_last()->message;
      pathspec = nullptr;
    }
  }

  int unmatched = 0;
  uint32_t count = 0;
  git_oid oid;
  git_commit* commit = nullptr;
  for (; !git_revwalk_next(&oid, walker); git_commit_free(commit))
  {
    if (!git_commit_lookup(&commit, m_repo.get(), &oid))
    {
      if (pathspec)
      {
        uint32_t parents = git_commit_parentcount(commit);
        unmatched = static_cast<int>(parents);
        if (!parents)
        {
          git_tree* tree = nullptr;
          if (!git_commit_tree(&tree, commit))
          {
            if (git_pathspec_match_tree(NULL, 
                                        tree, 
                                        GIT_PATHSPEC_NO_MATCH_ERROR, 
                                        pathspec) != 0 )
            {
              unmatched = 1;
            }
            git_tree_free(tree);
          }
        }
        else
        {
          for (uint32_t i = 0; i < parents; ++i)
          {
            if (match_with_parent(commit, i, &opt))
            {
              unmatched--;
            }
          }
        }
        if (unmatched > 0)
        {
          continue;
        }
      }

      if (max != 0 && count++ >= max)
      {
        git_commit_free(commit);
        break;
      }
      else if (count > 1)
      {
        ss << std::endl;
      }

      if (oneline)
      {
        print_log_oneline(ss, commit);
      }
      else
      {
        print_log(ss, commit);
      }
    }
  }
  git_pathspec_free(pathspec);
  git_revwalk_free(walker);
}

} // rest4git

#endif // LIBGIT2_AVAILABLE
