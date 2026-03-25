/// \file config.h
/// \brief Configuration constants for rest4git.
/// \author Juniarto Saputra (jsaputra@riseup.net)
/// \version 2.1
/// \date Mar 2024
///
/// Application-wide configuration. Secrets are sourced from the
/// environment at process startup - no credentials are compiled into
/// the binary or committed to source control.
///
/// Required environment variables (see .env.example):
///   REST4GIT_ADMIN_TOKEN - token for /admin/* maintenance endpoints
///   REST4GIT_REDIS_PASS  - password for planned Redis cache integration

#pragma once
#include <cstdlib>
#include <string>

namespace rest4git
{

class Config
{
public:
  /// Singleton accessor. Environment variables are read exactly once
  /// at first use (i.e. during startup).
  static Config& get()
  {
    static Config instance;
    return instance;
  }

  /// Admin API token for maintenance endpoints. Empty if
  /// REST4GIT_ADMIN_TOKEN is unset, in which case /admin/* must
  /// return 503 Service Unavailable.
  const std::string& admin_token() const { return m_admin_token; }

  /// Redis password for planned caching integration (issue #142).
  /// Empty if REST4GIT_REDIS_PASS is unset.
  const std::string& redis_pass() const { return m_redis_pass; }

  bool admin_token_configured() const { return !m_admin_token.empty(); }
  bool redis_pass_configured()  const { return !m_redis_pass.empty();  }

private:
  Config()
    : m_admin_token(read_env("REST4GIT_ADMIN_TOKEN"))
    , m_redis_pass (read_env("REST4GIT_REDIS_PASS"))
  {}

  static std::string read_env(const char* name)
  {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
  }

  std::string m_admin_token;
  std::string m_redis_pass;
};

} // namespace rest4git
