// Windows (and Wine) queries and actions the launcher needs. Strings are UTF-8.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "logic.h"

namespace sys {

// The launcher's folder, with a trailing backslash.
std::string ExeDir();
// The launcher's own command-line arguments (without the program name).
std::vector<std::string> LauncherArgs();
// The primary display's current mode (as EnumDisplaySettings reports it).
launcher::DisplayInfo PrimaryDisplay();
// Running under Wine/Proton: ntdll exports wine_get_version.
bool RunningUnderWine();
std::string EnvVar(const char* name);

bool FileExists(const std::string& path);
bool DirExists(const std::string& path);
bool ReadFile(const std::string& path, std::string& out);
bool WriteFile(const std::string& path, const std::string& data);

// Drive roots ("C:\", ...).
std::vector<std::string> Drives();
// Sub-folders of dir (names only), sorted, without hidden ones.
bool ListFolders(const std::string& dir, std::vector<std::string>& out);

// The started game.
struct Process {
  void* handle = nullptr;
  uint32_t pid = 0;
};
bool StartProcess(const std::string& exe, const std::string& command_line, const std::string& work_dir,
                  Process& out, std::string& error);
bool ProcessExited(const Process& p);
void WaitForExit(const Process& p);
void CloseProcess(Process& p);
// Lets the game take the foreground (Windows only lets the foreground process
// hand it on).
void AllowForeground(const Process& p);
// The game's own window (visible, title starting "ReAmped 3"), brought to the
// front; false while it doesn't exist yet.
bool BringGameWindowToFront(const Process& p);

// stdout for --selftest when started from a console (the exe is a GUI one).
void AttachParentConsole();

}  // namespace sys
