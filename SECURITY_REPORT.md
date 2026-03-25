# rest4git Security Audit Report

**Date:** 2026-03-25
**Scope:** Full source review of the rest4git HTTP service (Crow-based
REST wrapper around git), including the recently added admin
maintenance, raw file accessor, and file search endpoints.

All line numbers below refer to the **original (pre-fix)** source.

---

## Summary

| # | Vulnerability | CWE | Severity | Status |
|---|---|---|---|---|
| 1 | Broken authentication on `/admin/run` | CWE-285 | Critical | Fixed |
| 2 | OS command injection in `/admin/run` | CWE-78 | Critical | Fixed |
| 3 | OS command injection + path traversal in `/raw/<path>` | CWE-78, CWE-22 | Critical | Fixed |
| 4 | OS command injection in `/search` | CWE-78 | Critical | Fixed |
| 5 | HTTP response splitting in `/search` | CWE-113 | Medium | Fixed |
| 6 | OS command injection in `/check/<path>` | CWE-78 | Critical | Fixed |
| 7 | OS command injection in `SysCmd::file_exists()` | CWE-78 | High | Fixed |
| 8 | OS command injection in v1 path routes (`/blame`, `/show`, `/commit`) | CWE-78 | High | Fixed |
| 9 | Hardcoded credentials in source | CWE-798 | High | Fixed |
| 10 | Service binds to all interfaces | CWE-1327 | Medium | Fixed |

---

## 1. Broken Authentication on `/admin/run`

- **CWE:** CWE-285 (Improper Authorization) / CWE-252 (Unchecked Return Value)
- **Severity:** Critical
- **Location:** `src/main.cpp:571`

### Vulnerable code
```cpp
CROW_ROUTE(app, "/admin/run")
([](const crow::request& req) {
  rest4git::validate_admin_token(req);  // return value discarded!
  ...
  return rest4git::SysCmd::execute("git " + cmd);
});
```

### Attack vector / impact
`validate_admin_token()` returns `bool`, but the caller never inspects
it. Control flow continues unconditionally to the command-execution
line. Any unauthenticated network client reaches the admin handler,
which combined with issue #2 yields remote code execution.

### Fix applied
`validate_admin_token()` now returns a tri-state `AuthResult`
(`OK` / `FORBIDDEN` / `UNAVAILABLE`). The handler switches on the
result: `UNAVAILABLE -> 503`, `FORBIDDEN -> 403`, `OK -> proceed`.
The token itself is sourced from the `REST4GIT_ADMIN_TOKEN`
environment variable (see issue #9) and compared in constant time.

### Before / after
```bash
# Before (vulnerable)
$ curl "http://localhost:8000/admin/run?cmd=status"
On branch main
...                          # executed without any auth

# After (fixed) - no token header
$ curl -i "http://localhost:8000/admin/run?cmd=status"
HTTP/1.1 403 Forbidden
Forbidden

# After (fixed) - wrong token
$ curl -i -H "X-Admin-Token: wrong" "http://localhost:8000/admin/run?cmd=status"
HTTP/1.1 403 Forbidden
Forbidden

# After (fixed) - correct token
$ curl -i -H "X-Admin-Token: $REST4GIT_ADMIN_TOKEN" \
       "http://localhost:8000/admin/run?cmd=status"
HTTP/1.1 200 OK
On branch main
...

# After (fixed) - token not configured on server
$ curl -i "http://localhost:8000/admin/run?cmd=status"
HTTP/1.1 503 Service Unavailable
Service Unavailable: admin token not configured
```

---

## 2. OS Command Injection in `/admin/run`

- **CWE:** CWE-78 (OS Command Injection)
- **Severity:** Critical
- **Location:** `src/main.cpp:578`

### Vulnerable code
```cpp
std::string cmd = req.url_params.get("cmd");
return rest4git::SysCmd::execute("git " + cmd);  // popen()
```

### Attack vector / impact
The `cmd` query parameter is concatenated into a string passed to
`popen()`, which invokes `/bin/sh -c`. Shell metacharacters (`;`,
`|`, `&&`, `$()`, backticks) are interpreted. Combined with #1 this
gives unauthenticated RCE as the service user.

### Fix applied
Two layers:
1. `cmd` is validated against a fixed allow-list of read-only git
   subcommands (`status`, `gc`, `fsck`, `fetch`, `remote`, `branch`).
   The entire string must match exactly - no arguments accepted.
2. Execution uses `execute_argv({"git", cmd})` which calls
   `fork`+`execvp` directly - no shell is ever spawned.

### Before / after
```bash
# Before (vulnerable)
$ curl "http://localhost:8000/admin/run?cmd=status;id;whoami;uname+-a"
On branch main ...
uid=1000(wdha) gid=1000(wdha) groups=...
wdha
Linux athena 6.12.58-gentoo-dist #1 SMP ...

# After (fixed)
$ curl -H "X-Admin-Token: $TOKEN" \
       "http://localhost:8000/admin/run?cmd=status;id"
HTTP/1.1 400 Bad Request
Command not allowed
```

---

## 3. OS Command Injection + Path Traversal in `/raw/<path>`

- **CWE:** CWE-78 (OS Command Injection), CWE-22 (Path Traversal)
- **Severity:** Critical
- **Location:** `src/main.cpp:587`

### Vulnerable code
```cpp
CROW_ROUTE(app, "/raw/<path>")
([](const std::string& path) {
  std::string param(path);
  std::replace(param.begin(), param.end(), '+', ' ');
  return rest4git::SysCmd::execute("cat " + param);  // popen()
});
```

### Attack vector / impact
User-controlled `path` is concatenated into a shell command. This
yields both RCE (via shell metacharacters) and arbitrary file read
(via absolute paths or `..` traversal). An attacker can read
`/etc/passwd`, SSH keys, or the service's own source containing
hardcoded credentials (#9).

### Fix applied
- File is read with `std::ifstream` - no subprocess at all.
- Path is canonicalised with `realpath()` and rejected unless it
  resolves inside the repository root (`SysCmd::safe_repo_path()`).

### Before / after
```bash
# Before (vulnerable) - RCE
$ curl "http://localhost:8000/raw/README.md;id"
# rest4git
...
uid=1000(wdha) gid=1000(wdha) groups=...

# Before (vulnerable) - arbitrary file read
$ curl "http://localhost:8000/raw//etc/passwd"
root:x:0:0:root:/root:/usr/bin/zsh
bin:x:1:1:bin:/bin:/bin/false
...

# After (fixed)
$ curl "http://localhost:8000/raw/README.md;id"
HTTP/1.1 404 Not Found
File README.md;id not found!

$ curl "http://localhost:8000/raw//etc/hostname"
HTTP/1.1 404 Not Found
File /etc/hostname not found!
```

---

## 4. OS Command Injection in `/search`

- **CWE:** CWE-78 (OS Command Injection)
- **Severity:** Critical
- **Location:** `src/main.cpp:602-603`, `src/git_commands.h:47`

### Vulnerable code
```cpp
// git_commands.h
{rest4git::COMMAND::CHECK, "git ls-files | grep /"},

// main.cpp
pattern = req.url_params.get("pattern");
res.body = rest4git::SysCmd::execute(
  rest4git::g_git_commands[rest4git::COMMAND::CHECK] + pattern);
// popen("git ls-files | grep /<pattern>")
```

### Attack vector / impact
`pattern` is appended to a shell pipeline. Semicolons break out of
the grep invocation into arbitrary commands. Unauthenticated RCE.

### Fix applied
The shell pipeline is removed entirely. `git ls-files` is executed
via `execute_argv` (no shell) and the output is filtered in-process
by a C++ substring match (`filter_lines()`).

### Before / after
```bash
# Before (vulnerable)
$ curl "http://localhost:8000/search?pattern=x;id;whoami"
uid=1000(wdha) gid=1000(wdha) groups=...
wdha

# After (fixed)
$ curl "http://localhost:8000/search?pattern=x;id;whoami"
(empty - no tracked filename contains the literal string "x;id;whoami")

$ curl "http://localhost:8000/search?pattern=git2"
cmake/Findlibgit2.cmake
src/git2api.cpp
src/git2api.h
```

---

## 5. HTTP Response Splitting in `/search`

- **CWE:** CWE-113 (HTTP Response Splitting / CRLF Injection)
- **Severity:** Medium
- **Location:** `src/main.cpp:600`

### Vulnerable code
```cpp
pattern = req.url_params.get("pattern");
res.set_header("X-Search-Pattern", pattern);  // raw user input
```

### Attack vector / impact
URL-decoded `%0d%0a` becomes `\r\n` and is written verbatim into the
response header block. An attacker can inject arbitrary headers or
terminate the header block early to supply a forged body. Enables:
- Session fixation (inject `Set-Cookie`)
- Cache poisoning on shared proxies
- Cross-site scripting via forged `Content-Type` + body
- Open redirect (inject `Location`)
- Security-header override (e.g. weaken CSP)

### Fix applied
A `sanitize_header_value()` helper strips `\r` and `\n` before the
value is passed to `set_header()`. The fix is applied at the point
where the header is set.

### Before / after
```bash
# Before (vulnerable)
$ curl -si "http://localhost:8000/search?pattern=x%0d%0aSet-Cookie:%20evil=1"
HTTP/1.1 200 OK
X-Search-Pattern: x
Set-Cookie: evil        <-- injected header
Content-Length: 0
...

# After (fixed)
$ curl -si "http://localhost:8000/search?pattern=x%0d%0aSet-Cookie:%20evil=1"
HTTP/1.1 200 OK
X-Search-Pattern: xSet-Cookie: evil    <-- single header, CRLF stripped
Content-Length: 0
...
```

---

## 6. OS Command Injection in `/check/<path>`

- **CWE:** CWE-78 (OS Command Injection)
- **Severity:** Critical
- **Location:** `src/main.cpp:470-471`, `src/git_commands.h:47`

### Vulnerable code
```cpp
const std::string cmd =
  rest4git::g_git_commands[rest4git::COMMAND::CHECK] + param;
// "git ls-files | grep /" + param
return rest4git::SysCmd::execute(cmd);
```

### Attack vector / impact
Same root cause as #4 - user path is appended to a shell pipeline.
Semicolons in the URL path segment break out to arbitrary commands.

### Fix applied
Same as #4: `git ls-files` via `execute_argv`, in-process substring
filtering via `filter_lines()`. No shell, no grep pipe.

### Before / after
```bash
# Before (vulnerable)
$ curl 'http://localhost:8000/check/x;id'
uid=1000(wdha) gid=1000(wdha) groups=...

# After (fixed)
$ curl 'http://localhost:8000/check/x;id'
(empty - no match for literal "/x;id")

$ curl 'http://localhost:8000/check/main.cpp'
src/main.cpp
```

---

## 7. OS Command Injection in `SysCmd::file_exists()`

- **CWE:** CWE-78 (OS Command Injection)
- **Severity:** High
- **Location:** `src/syscmd.h:45-50`

### Vulnerable code
```cpp
static bool file_exists(const std::string& path)
{
  std::string p = "[ -f " + path + " ] && echo '1' || echo '0'";
  std::string res = rest4git::SysCmd::execute(p);   // popen()
  return (res == "1\n") ? true : false;
}
```

### Attack vector / impact
`file_exists()` is called by **17 route handlers** (every v1 and v2
endpoint that accepts a file path: `/blame*`, `/show*`,
`/commit/<n>/<path>`). The path goes straight into a shell `test`
expression. An attacker who closes the `[` early can run arbitrary
commands. Output is not returned to the caller, so exploitation is
blind (write to file, spawn reverse shell, exfiltrate via DNS).

**Exploit chain:**
| Step | Value |
|---|---|
| HTTP request | `curl -G .../show --data-urlencode "file-path=x ];id>/tmp/pwn.txt;[ -f x"` |
| Shell command | `[ -f x ];id>/tmp/pwn.txt;[ -f x ] && echo '1' \|\| echo '0'` |
| Execution | `id` runs, output written to `/tmp/pwn.txt` |
| Return | `false` (stdout was `0\n`) |
| HTTP response | `File x ];id>/tmp/pwn.txt;[ -f x not found!` |

### Fix applied
`file_exists()` rewritten to use POSIX APIs only - no shell:
```cpp
static bool file_exists(const std::string& path)
{
  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) == nullptr) return false;
  std::string root(cwd); root += '/';

  char resolved[PATH_MAX];
  if (realpath(path.c_str(), resolved) == nullptr) return false;

  struct stat st;
  if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode)) return false;

  return std::string(resolved).compare(0, root.size(), root) == 0;
}
```
Also adds path-confinement to the repo root as defence-in-depth.

### Before / after
```bash
# Before (vulnerable)
$ curl -G "http://localhost:8000/show" \
       --data-urlencode "file-path=x ];id>/tmp/pwn.txt;[ -f x"
File x ];id>/tmp/pwn.txt;[ -f x not found!
$ cat /tmp/pwn.txt
uid=1000(wdha) gid=1000(wdha) groups=...     <-- blind RCE confirmed

# After (fixed)
$ curl -G "http://localhost:8000/show" \
       --data-urlencode "file-path=x ];id>/tmp/pwn.txt;[ -f x"
File x ];id>/tmp/pwn.txt;[ -f x not found!
$ cat /tmp/pwn.txt
cat: /tmp/pwn.txt: No such file or directory   <-- no execution
```

---

## 8. OS Command Injection in v1 Path Routes

- **CWE:** CWE-78 (OS Command Injection)
- **Severity:** High
- **Location:** `src/main.cpp:333, 346, 374, 388, 401, 454, 460, 482, 495, 508, 561`

### Vulnerable code (representative - `/blame` at line 460)
```cpp
params += filepath;                              // tainted
const std::string cmd =
  rest4git::g_git_commands[rest4git::COMMAND::BLAME] + params;
return rest4git::SysCmd::execute(cmd);           // popen()
```

### Attack vector / impact
All v1 routes that accept a path build the final git command by
string concatenation and pass it to `popen()`. The `file_exists()`
pre-check (#7) is itself exploitable, so an attacker can craft a
payload that both passes the check (emits `1\n`) and injects into the
second-stage command. This upgrades #7 from blind to **non-blind**
RCE because the main command's stdout is returned in the HTTP body.

**Two-stage chain:**
| Step | Value |
|---|---|
| Request | `curl -G .../blame --data-urlencode "file-path=README.md ];id>/tmp/pwn.txt;echo '1' #"` |
| Stage 1 (`file_exists`) | `[ -f README.md ];id>/tmp/pwn.txt;echo '1' # ] && ...` -> stdout `1\n` -> returns `true` |
| Stage 2 (`git blame`) | `git --no-pager blame -e --date=short README.md ];id>/tmp/pwn.txt;echo '1' #` -> `id` runs again |
| HTTP body | full blame output followed by `1\n` |

### Fix applied
Root cause removed: `SysCmd::execute_argv()` uses `fork`+`execvp` so
no shell is ever invoked. Every route now builds an argv vector with
the user path as a separate element preceded by `--` (end-of-options
marker) so it cannot be interpreted as a flag either.

```cpp
auto argv = base_argv(rest4git::COMMAND::BLAME);
argv.push_back("--");
argv.push_back(filepath);          // one argv slot, passed verbatim
return rest4git::SysCmd::execute_argv(argv);
```

The `| sed -n` pipeline previously used by `/show` for line slicing
is replaced by an in-process `slice_lines()` function.

### Before / after
```bash
# Before (vulnerable)
$ curl -G "http://localhost:8000/blame" \
       --data-urlencode "file-path=README.md ];id>/tmp/pwn.txt;echo '1' #"
<git blame output>
1
$ cat /tmp/pwn.txt
uid=1000(wdha) ...

# After (fixed)
$ curl -G "http://localhost:8000/blame" \
       --data-urlencode "file-path=README.md ];id>/tmp/pwn.txt;echo '1' #"
File README.md ];id>/tmp/pwn.txt;echo '1' # not found!
$ ls /tmp/pwn.txt
ls: cannot access '/tmp/pwn.txt': No such file or directory
```

---

## 9. Hardcoded Credentials in Source

- **CWE:** CWE-798 (Use of Hard-coded Credentials)
- **Severity:** High
- **Location:** `src/config.h:18, 22`

### Vulnerable code
```cpp
const std::string ADMIN_TOKEN = "r4g-4dm1n-t0k3n-2024";
const std::string REDIS_PASS  = "r4gitRedis!2024";
```

### Attack vector / impact
Secrets are compiled into the binary and committed to source control.
They are disclosed to anyone with repo read access, anyone who can
run `strings` on the binary, or anyone who can reach `/raw` (#3) or
`/show` to read `src/config.h` over HTTP. The admin token grants
access to `/admin/run` (RCE). The Redis password compromises the
planned caching tier.

### Fix applied
`config.h` rewritten as a `Config` singleton that reads
`REST4GIT_ADMIN_TOKEN` and `REST4GIT_REDIS_PASS` from the environment
once at startup. No defaults. If `REST4GIT_ADMIN_TOKEN` is unset the
service logs a warning and `/admin/*` returns `503 Service
Unavailable`. A `.env.example` file in the repo root documents both
variables with placeholder values; `.env` is git-ignored.

### Before / after
```bash
# Before (vulnerable)
$ curl "http://localhost:8000/raw/src/config.h" | grep -E 'TOKEN|PASS'
const std::string ADMIN_TOKEN = "r4g-4dm1n-t0k3n-2024";
const std::string REDIS_PASS = "r4gitRedis!2024";

# After (fixed)
$ curl "http://localhost:8000/raw/src/config.h" | grep -E 'TOKEN|PASS'
///   REST4GIT_ADMIN_TOKEN - token for /admin/* maintenance endpoints
///   REST4GIT_REDIS_PASS  - password for planned Redis cache integration
  /// REST4GIT_ADMIN_TOKEN is unset, in which case /admin/* must
    : m_admin_token(read_env("REST4GIT_ADMIN_TOKEN"))
    , m_redis_pass (read_env("REST4GIT_REDIS_PASS"))
# (only comments and env-var names - no secret values)

$ grep -rn "r4g-4dm1n\|r4gitRedis" src/
(no output)
```

---

## 10. Service Binds to All Interfaces

- **CWE:** CWE-1327 (Binding to an Unrestricted IP Address)
- **Severity:** Medium
- **Location:** `src/main.cpp:609`

### Vulnerable code
```cpp
app.port(8000).multithreaded().run();   // no bindaddr -> 0.0.0.0
```

### Attack vector / impact
Crow defaults to `0.0.0.0`. An "internal developer tool" is
reachable from every network segment the host is attached to,
massively widening the attack surface for issues #1-#8.

### Fix applied
```cpp
app.bindaddr("127.0.0.1").port(8000).multithreaded().run();
```
Remote access, if required, should be fronted by an authenticating
reverse proxy.

### Before / after
```bash
# Before (vulnerable)
$ ss -tlnp | grep 8000
LISTEN 0 4096  0.0.0.0:8000  0.0.0.0:*  users:(("rest4git",...))

# After (fixed)
$ ss -tlnp | grep 8000
LISTEN 0 4096  127.0.0.1:8000  0.0.0.0:*  users:(("rest4git",...))
```

---

## Root-Cause Remediation Summary

| Theme | Original approach | Fixed approach |
|---|---|---|
| Command execution | `popen()` with string concatenation | `fork`+`execvp` with argv vectors; user input is always its own element after `--` |
| File existence check | Shell `[ -f ... ]` via `popen()` | `realpath()` + `stat()` with repo-root confinement |
| Line slicing | `\| sed -n` shell pipeline | In-process `slice_lines()` |
| File search | `\| grep` shell pipeline | In-process `filter_lines()` |
| Raw file read | `cat` via `popen()` | `std::ifstream` with `safe_repo_path()` confinement |
| Secrets | Hardcoded string literals | Environment variables loaded at startup; `.env.example` template; `.env` git-ignored |
| Header reflection | Raw user input to `set_header()` | `sanitize_header_value()` strips CR/LF |
| Network exposure | Default `0.0.0.0` bind | Explicit `127.0.0.1` bind |

All existing v1 and v2 routes preserve their request/response
contracts for legitimate inputs.
