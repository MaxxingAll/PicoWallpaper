#pragma once

namespace miniwp {

bool SetRunAtStartup(bool enabled);
// Respect an externally removed Run value; repair the command after an exe move.
// On registry errors leave the caller's setting unchanged and return false.
bool ReconcileRunAtStartup(bool& enabled);

} // namespace miniwp
