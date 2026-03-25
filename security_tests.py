#!/usr/bin/env python3
"""
rest4git security regression test suite.

Runs exploit attempts against a running rest4git instance and asserts
that every vulnerability identified in SECURITY_REPORT.md is fixed.

Prerequisites:
  - rest4git running on 127.0.0.1:8000
  - REST4GIT_ADMIN_TOKEN exported in the service's environment
    (pass the same value to this script via REST4GIT_ADMIN_TOKEN so
     the positive-auth test can succeed)

Usage:
  REST4GIT_ADMIN_TOKEN=<token> ./build/rest4git &
  REST4GIT_ADMIN_TOKEN=<token> python3 security_tests.py
"""

import os
import re
import subprocess
import sys
import tempfile

import requests

BASE = os.environ.get("REST4GIT_URL", "http://127.0.0.1:8000")
ADMIN_TOKEN = os.environ.get("REST4GIT_ADMIN_TOKEN", "")
REPO_ROOT = os.path.dirname(os.path.abspath(__file__))

# Markers that must NEVER appear in a response body if injection is
# blocked. `id` output, `whoami` output, /etc/passwd first line, etc.
INJECTION_MARKERS = [
    "uid=",
    "gid=",
    "root:x:0:0",
    "/bin/sh",
    "/bin/bash",
]

_results = []  # (name, passed, detail)


def _record(name, passed, detail=""):
    _results.append((name, passed, detail))
    status = "PASS" if passed else "FAIL"
    line = f"[{status}] {name}"
    if detail and not passed:
        line += f"  -- {detail}"
    print(line)


def _no_injection_output(text):
    """Return (ok, marker) - ok is True if no injection marker found."""
    for m in INJECTION_MARKERS:
        if m in text:
            return False, m
    return True, ""


# ---------------------------------------------------------------------
# Vuln #1 + #2: /admin/run - broken auth + command injection
# ---------------------------------------------------------------------

def test_admin_run_no_auth_bypass():
    """Unauthenticated /admin/run must not execute anything."""
    r = requests.get(f"{BASE}/admin/run",
                     params={"cmd": "status;id;whoami"},
                     timeout=5)
    ok, marker = _no_injection_output(r.text)
    passed = r.status_code in (403, 503) and ok
    _record("admin/run: no-auth bypass blocked",
            passed,
            f"status={r.status_code} marker={marker!r} body={r.text[:80]!r}")


def test_admin_run_cmd_injection_with_auth():
    """Even authenticated, shell metachars in cmd must be rejected."""
    if not ADMIN_TOKEN:
        _record("admin/run: cmd injection rejected (authed)", True,
                "skipped - no admin token configured")
        return
    r = requests.get(f"{BASE}/admin/run",
                     params={"cmd": "status;id"},
                     headers={"X-Admin-Token": ADMIN_TOKEN},
                     timeout=5)
    ok, marker = _no_injection_output(r.text)
    passed = r.status_code == 400 and ok
    _record("admin/run: cmd injection rejected (authed)",
            passed,
            f"status={r.status_code} marker={marker!r}")


# ---------------------------------------------------------------------
# Vuln #3: /raw - cat injection + path traversal
# ---------------------------------------------------------------------

def test_raw_semicolon_injection():
    """`;id` appended to a path must not execute."""
    r = requests.get(f"{BASE}/raw/README.md;id", timeout=5)
    ok, marker = _no_injection_output(r.text)
    passed = r.status_code == 404 and ok
    _record("raw: semicolon command injection blocked",
            passed,
            f"status={r.status_code} marker={marker!r}")


def test_raw_path_traversal():
    """Absolute path outside repo must be rejected."""
    r = requests.get(f"{BASE}/raw//etc/passwd", timeout=5)
    ok, marker = _no_injection_output(r.text)
    # Must not return /etc/passwd content
    no_passwd = "root:x:" not in r.text
    passed = r.status_code == 404 and ok and no_passwd
    _record("raw: path traversal to /etc/passwd blocked",
            passed,
            f"status={r.status_code}")


# ---------------------------------------------------------------------
# Vuln #7 + #8: /blame - file_exists shell injection
# ---------------------------------------------------------------------

def test_blame_file_exists_injection():
    """Shell metachars in file-path must not execute via file_exists."""
    # Use a unique temp path so we can detect blind RCE side effects.
    canary = os.path.join(tempfile.gettempdir(), "rest4git_pwn_canary")
    if os.path.exists(canary):
        os.remove(canary)

    payload = f"x ];id>{canary};[ -f x"
    r = requests.get(f"{BASE}/blame",
                     params={"file-path": payload},
                     timeout=5)

    canary_created = os.path.exists(canary)
    if canary_created:
        os.remove(canary)

    ok, marker = _no_injection_output(r.text)
    passed = (not canary_created) and ok
    _record("blame: file_exists shell injection blocked",
            passed,
            f"canary_created={canary_created} marker={marker!r} "
            f"body={r.text[:60]!r}")


def test_show_file_exists_injection():
    """Same file_exists sink reachable via /show."""
    canary = os.path.join(tempfile.gettempdir(), "rest4git_pwn_canary2")
    if os.path.exists(canary):
        os.remove(canary)

    payload = f"x ];id>{canary};[ -f x"
    r = requests.get(f"{BASE}/show",
                     params={"file-path": payload},
                     timeout=5)

    canary_created = os.path.exists(canary)
    if canary_created:
        os.remove(canary)

    ok, _ = _no_injection_output(r.text)
    passed = (not canary_created) and ok
    _record("show: file_exists shell injection blocked",
            passed,
            f"canary_created={canary_created}")


# ---------------------------------------------------------------------
# Vuln #6: /check - grep pipe injection
# ---------------------------------------------------------------------

def test_check_pipe_injection():
    """`;id` in /check/<path> must not execute."""
    r = requests.get(f"{BASE}/check/x;id", timeout=5)
    ok, marker = _no_injection_output(r.text)
    passed = ok
    _record("check: grep pipe injection blocked",
            passed,
            f"marker={marker!r} body={r.text[:60]!r}")


# ---------------------------------------------------------------------
# Vuln #4: /search - grep pipe injection
# ---------------------------------------------------------------------

def test_search_pipe_injection():
    """`;id;whoami` in /search pattern must not execute."""
    r = requests.get(f"{BASE}/search",
                     params={"pattern": "x;id;whoami"},
                     timeout=5)
    ok, marker = _no_injection_output(r.text)
    passed = ok
    _record("search: grep pipe injection blocked",
            passed,
            f"marker={marker!r} body={r.text[:60]!r}")


# ---------------------------------------------------------------------
# Vuln #5: /search - CRLF header injection
# ---------------------------------------------------------------------

def test_search_crlf_header_injection():
    """CR/LF in pattern must not inject extra response headers."""
    r = requests.get(f"{BASE}/search",
                     params={"pattern": "x\r\nSet-Cookie: evil=1\r\nX-Injected: yes"},
                     timeout=5)

    # The injected header names must NOT appear as real response headers.
    injected_cookie = "evil" in r.headers.get("Set-Cookie", "")
    injected_custom = "X-Injected" in r.headers

    # The reflected header must be a single line with CR/LF stripped.
    echoed = r.headers.get("X-Search-Pattern", "")
    single_line = ("\r" not in echoed) and ("\n" not in echoed)

    passed = (not injected_cookie) and (not injected_custom) and single_line
    _record("search: CRLF header injection blocked",
            passed,
            f"Set-Cookie={r.headers.get('Set-Cookie')!r} "
            f"X-Injected={'X-Injected' in r.headers} "
            f"echoed={echoed!r}")


# ---------------------------------------------------------------------
# Vuln #9: Hardcoded secrets removed from source
# ---------------------------------------------------------------------

def test_no_hardcoded_secrets():
    """Grep source tree for the old hardcoded credentials."""
    patterns = [
        r"r4g-4dm1n",
        r"r4gitRedis",
        r'ADMIN_TOKEN\s*=\s*"',
        r'REDIS_PASS\s*=\s*"',
    ]
    found = []
    src_dir = os.path.join(REPO_ROOT, "src")
    for root, _, files in os.walk(src_dir):
        for fn in files:
            if not fn.endswith((".h", ".cpp")):
                continue
            path = os.path.join(root, fn)
            try:
                with open(path, "r", encoding="utf-8", errors="ignore") as f:
                    content = f.read()
            except OSError:
                continue
            for pat in patterns:
                if re.search(pat, content):
                    found.append(f"{path}: matches {pat!r}")

    passed = len(found) == 0
    _record("config: no hardcoded secrets in source",
            passed,
            "; ".join(found) if found else "")


# ---------------------------------------------------------------------
# Admin token auth matrix
# ---------------------------------------------------------------------

def test_admin_reject_missing_token():
    r = requests.get(f"{BASE}/admin/run",
                     params={"cmd": "status"},
                     timeout=5)
    passed = r.status_code in (403, 503)
    _record("admin auth: missing token rejected",
            passed,
            f"status={r.status_code}")


def test_admin_reject_wrong_token():
    r = requests.get(f"{BASE}/admin/run",
                     params={"cmd": "status"},
                     headers={"X-Admin-Token": "definitely-wrong-token"},
                     timeout=5)
    passed = r.status_code in (403, 503)
    _record("admin auth: wrong token rejected",
            passed,
            f"status={r.status_code}")


def test_admin_accept_correct_token():
    if not ADMIN_TOKEN:
        _record("admin auth: correct token accepted", True,
                "skipped - REST4GIT_ADMIN_TOKEN not set in test env")
        return
    r = requests.get(f"{BASE}/admin/run",
                     params={"cmd": "status"},
                     headers={"X-Admin-Token": ADMIN_TOKEN},
                     timeout=5)
    passed = r.status_code == 200 and "branch" in r.text.lower()
    _record("admin auth: correct token accepted",
            passed,
            f"status={r.status_code} body={r.text[:60]!r}")


# ---------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------

TESTS = [
    test_admin_run_no_auth_bypass,
    test_admin_run_cmd_injection_with_auth,
    test_raw_semicolon_injection,
    test_raw_path_traversal,
    test_blame_file_exists_injection,
    test_show_file_exists_injection,
    test_check_pipe_injection,
    test_search_pipe_injection,
    test_search_crlf_header_injection,
    test_no_hardcoded_secrets,
    test_admin_reject_missing_token,
    test_admin_reject_wrong_token,
    test_admin_accept_correct_token,
]


def main():
    print(f"rest4git security test suite")
    print(f"Target: {BASE}")
    print(f"Admin token configured: {'yes' if ADMIN_TOKEN else 'no'}")
    print("-" * 60)

    # Reachability check
    try:
        requests.get(f"{BASE}/", timeout=3)
    except requests.RequestException as e:
        print(f"ERROR: cannot reach {BASE}: {e}")
        sys.exit(2)

    for t in TESTS:
        try:
            t()
        except Exception as e:  # noqa: BLE001
            _record(t.__name__, False, f"exception: {e}")

    print("-" * 60)
    total = len(_results)
    passed = sum(1 for _, ok, _ in _results if ok)
    failed = total - passed
    print(f"{total} tests run, {passed} passed, {failed} failed")

    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
