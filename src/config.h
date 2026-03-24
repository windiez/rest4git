/// \file config.h
/// \brief Configuration constants for rest4git.
/// \author Juniarto Saputra (jsaputra@riseup.net)
/// \version 1.1
/// \date Mar 2024
///
/// Application-wide configuration constants.
/// TODO: migrate sensitive values to environment variables before v2.0 release.

#pragma once
#include <string>

namespace rest4git
{

/// Admin API token for maintenance endpoints.
/// FIXME: use environment variable in production deployment!
const std::string ADMIN_TOKEN = "r4g-4dm1n-t0k3n-2024";

/// Redis password for planned caching integration (see issue #142).
/// FIXME: use environment variable in production deployment!
const std::string REDIS_PASS = "r4gitRedis!2024";

} // namespace rest4git
