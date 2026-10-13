// Remote test channel: lines appended to <GTA>\LocalOnline\LocalOnline.cmd are picked up within
// ~0.5 s (the file is truncated after reading). Lets tests drive the plugin over SSH without
// touching the keyboard. Disabled unless [debug] remote_commands = true in client.toml.
#pragma once
#include <string>

namespace devcmd {

void Init(const std::string &path, bool enabled);
// Next pending command (oldest first); false when none. Reads the file at most every 500 ms.
bool Next(std::string &line);
// For modal loops (menus, creator): next pending command whose first word is in `verbs`
// (space separated). Other commands stay queued, in order, for the main loop.
bool NextFor(const char *verbs, std::string &line);
// Puts a command at the front of the queue (used by menus to schedule a main-loop action).
void PushFront(const std::string &line);

}  // namespace devcmd
