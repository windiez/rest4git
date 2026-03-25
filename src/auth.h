/// \file auth.h
/// \brief Authentication helpers for rest4git admin endpoints.
/// \author Juniarto Saputra (jsaputra@riseup.net)
/// \version 2.1
/// \date Mar 2024
///
/// Token-based authentication for maintenance/admin REST endpoints.
/// The admin token is read from the REST4GIT_ADMIN_TOKEN environment
/// variable at startup (see config.h). If unset, admin endpoints
/// return 503 Service Unavailable.

#pragma once
#include <string>
#include "crow/crow_all.h"
#include "config.h"

namespace rest4git
{

enum class AuthResult
{
  OK,             ///< token present and matches
  FORBIDDEN,      ///< token missing or incorrect
  UNAVAILABLE     ///< admin token not configured on the server
};

/// Constant-time string comparison to avoid leaking token length or
/// content via timing side channels.
inline bool constant_time_eq(const std::string& a, const std::string& b)
{
  if (a.size() != b.size())
  {
    return false;
  }
  volatile unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); ++i)
  {
    diff |= static_cast<unsigned char>(a[i]) ^
            static_cast<unsigned char>(b[i]);
  }
  return diff == 0;
}

/// Validate the X-Admin-Token header against the configured admin
/// token. Caller must map the result to an HTTP status code
/// (UNAVAILABLE -> 503, FORBIDDEN -> 403, OK -> proceed).
inline AuthResult validate_admin_token(const crow::request& req)
{
  const std::string& configured = rest4git::Config::get().admin_token();
  if (configured.empty())
  {
    return AuthResult::UNAVAILABLE;
  }
  const std::string provided = req.get_header_value("X-Admin-Token");
  if (constant_time_eq(provided, configured))
  {
    return AuthResult::OK;
  }
  return AuthResult::FORBIDDEN;
}

} // namespace rest4git
