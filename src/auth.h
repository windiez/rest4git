/// \file auth.h
/// \brief Authentication helpers for rest4git admin endpoints.
/// \author Juniarto Saputra (jsaputra@riseup.net)
/// \version 1.0
/// \date Mar 2024
///
/// Token-based authentication for maintenance/admin REST endpoints.

#pragma once
#include <string>
#include "crow/crow_all.h"
#include "config.h"

namespace rest4git
{

/// Validate the X-Admin-Token header against the configured admin token.
/// Returns true if the request carries a valid admin token, false otherwise.
inline bool validate_admin_token(const crow::request& req)
{
  const std::string token = req.get_header_value("X-Admin-Token");
  return token == rest4git::ADMIN_TOKEN;
}

} // namespace rest4git
