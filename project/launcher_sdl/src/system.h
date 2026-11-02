// Windows (and Wine) queries and actions the launcher needs. Strings are UTF-8.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "logic.h"

namespace sys {

// The launcher's folder, with a trailing backslash.
std::string ExeDir();
// The launcher's own command-line arguments (without the program name).
std::vector<std::string> LauncherArgs();
// The primary display's current mode (as EnumDisplaySettings reports it).
launcher::DisplayInfo PrimaryDisplay();
// The primary display's modes (width, height), as Windows (or Wine) lists them.
std::vector<std::pair<int, int>> DisplayModes();
// Running under Wine/Proton: ntdll exports wine_get_version.
bool RunningUnderWine();
std::string EnvVar(const char* name);

bool FileExists(const std::string& path);
bool DirExists(const std::string& path);
// Creates the folder (its parent must exist); true if it exists afterwards.
bool MakeDir(const std::string& path);
bool ReadFile(const std::string& path, std::string& out);
bool WriteFile(const std::string& path, const std::string& data);

// Files (names only, not folders) in dir; false if it can't be listed.
bool ListFiles(const std::string& dir, std::vector<std::string>& out);
// Last write time (FILETIME ticks), 0 if the file doesn't exist.
uint64_t FileTime(const std::string& path);
// Deletes the folder and everything in it; true if it's gone afterwards.
bool RemoveTree(const std::string& path);
// Renames a folder (to must not exist).
bool RenameDir(const std::string& from, const std::string& to);
// Runs a console program without a window, below normal priority, and waits.
// keep_going() is asked every 200 ms: false kills it. exit_code is its code.
bool RunHidden(const std::string& exe, const std::string& command_line, const std::string& work_dir,
               const std::function<bool()>& keep_going, int& exit_code, std::string& error);

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

// Size in bytes, or -1 if the file doesn't exist.
long long FileSize(const std::string& path);
bool RemoveFile(const std::string& path);
// Renames from -> to, replacing to.
bool MoveOver(const std::string& from, const std::string& to);
// HTTPS GET into a file (over any existing one). progress(done, total) is
// called as it goes (total 0 if unknown) and returns false to cancel.
bool Download(const std::string& url, const std::string& path,
              const std::function<bool(unsigned long long, unsigned long long)>& progress, std::string& error);
// The file's SHA-256 as lowercase hex ("" if unreadable).
std::string Sha256File(const std::string& path);

// The graphics adapters (PCI vendor id and description).
struct Adapter {
  unsigned vendor_id = 0;
  std::string description;
};
std::vector<Adapter> Adapters();

// stdout for --selftest when started from a console (the exe is a GUI one).
void AttachParentConsole();

// One copy at a time: false if the game (or another launcher) is already
// running, after bringing its window to the front.
bool ClaimSingleInstance();

}  // namespace sys
