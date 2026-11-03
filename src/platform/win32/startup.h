#pragma once

namespace picowp {

bool SetRunAtStartup(bool enabled);
// Keep the legacy value until the replacement is written successfully.
bool MigrateLegacyRunAtStartup(bool enabled);
// Respect an externally removed Run value; repair the command after an exe move.
// On registry errors leave the caller's setting unchanged and return false.
bool ReconcileRunAtStartup(bool& enabled);

} // namespace picowp
