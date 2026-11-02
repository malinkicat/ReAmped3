// save_backup.cpp - Automatic backups of the game's saves
//
// The profile folders in the user data folder (Documents\amped3\<profile>)
// are copied to backups\<date_time>\ when they've changed and then stayed
// unchanged for 10 s (checked once a minute, and once at start), keeping the
// newest 10. A restore chosen in the ReAmped 3 menu is applied at the next
// start, before the game reads its saves (the running game keeps its save in
// memory and would write it back).
//
// Never losing a save: a backup is copied to "<name>.partial" and renamed only
// when complete; the backup chosen for restoring is never pruned; a restore
// first backs up the current saves and stops if that fails, and swaps each
// profile in by renames (the current one is removed only after the restored
// copy is in place).

#include "save_backup.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

REXCVAR_DEFINE_BOOL(save_backups, true, "Saves", "Back up the game's saves automatically");
REXCVAR_DECLARE(std::string, user_data_root);

namespace amped3::save_backup {
namespace {

namespace fs = std::filesystem;
constexpr size_t kKeep = 10;
std::mutex g_mutex;  // one backup / restore / prune at a time

std::string U8(const fs::path& path) {
  const auto u8 = path.u8string();
  return std::string(u8.begin(), u8.end());
}

fs::path UserRoot() {
  const std::string root = REXCVAR_GET(user_data_root);
  return root.empty() ? rex::filesystem::GetUserFolder() / "amped3" : fs::path(root);
}
fs::path BackupRoot() { return UserRoot() / "backups"; }
fs::path ScheduleFile() { return BackupRoot() / "restore_next_start.txt"; }

// The profile save folders under `root` (everything but the shader cache, the
// backups, the achievements - the runtime keeps those, and a restore mustn't
// take them back - and leftovers of an interrupted restore).
std::vector<fs::path> SaveFolders(const fs::path& root) {
  std::vector<fs::path> out;
  std::error_code ec;
  fs::directory_iterator it(root, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    const fs::path& p = it->path();
    const std::string name = U8(p.filename());
    std::error_code dir_ec;
    if (!fs::is_directory(p, dir_ec) || name == "cache" || name == "backups" || name == "shaders" ||
        name == "achievements" || name.find('.') != std::string::npos) {
      continue;
    }
    out.push_back(p);
  }
  return out;
}

// A fingerprint of the saves: newest change time and total size.
std::pair<int64_t, uintmax_t> Fingerprint() {
  int64_t newest = 0;
  uintmax_t size = 0;
  for (const fs::path& top : SaveFolders(UserRoot())) {
    std::error_code ec;
    fs::recursive_directory_iterator it(top, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
      std::error_code file_ec;
      if (!it->is_regular_file(file_ec)) continue;
      newest = std::max<int64_t>(newest, it->last_write_time(file_ec).time_since_epoch().count());
      size += it->file_size(file_ec);
    }
  }
  return {newest, size};
}

std::string Stamp() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &t);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H%M%S", &tm);
  return buffer;
}

std::string Scheduled() {
  std::string name;
  if (std::ifstream in(ScheduleFile()); in) std::getline(in, name);
  return name;
}

// Complete backups, newest first.
std::vector<fs::path> Backups() {
  std::vector<fs::path> out;
  std::error_code ec;
  fs::directory_iterator it(BackupRoot(), ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    std::error_code dir_ec;
    const std::string name = U8(it->path().filename());
    if (fs::is_directory(it->path(), dir_ec) && name.find('.') == std::string::npos) {
      out.push_back(it->path());
    }
  }
  std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) { return a > b; });
  return out;
}

// Keeps the newest kKeep, and always the one scheduled for restoring.
void Prune() {
  const std::string keep = Scheduled();
  const auto all = Backups();
  for (size_t i = kKeep; i < all.size(); ++i) {
    if (U8(all[i].filename()) == keep) continue;
    std::error_code ec;
    fs::remove_all(all[i], ec);
  }
}

// Copies the save folders to backups\<stamp>. True if there was nothing to
// back up or the copy is complete; false (and nothing left behind) on error.
bool Backup(const std::string& reason, bool prune = true) {
  const auto folders = SaveFolders(UserRoot());
  if (folders.empty()) return true;
  std::string name = Stamp();
  std::error_code ec;
  while (fs::exists(BackupRoot() / name, ec)) name += "b";  // two in one second
  const fs::path partial = BackupRoot() / (name + ".partial");
  fs::remove_all(partial, ec);
  fs::create_directories(partial, ec);
  for (const fs::path& top : folders) {
    ec.clear();
    fs::copy(top, partial / top.filename(), fs::copy_options::recursive, ec);
    if (ec) {
      REXLOG_WARN("Amped3: save backup: copying {} failed: {}", U8(top.filename()), ec.message());
      std::error_code cleanup;
      fs::remove_all(partial, cleanup);
      return false;
    }
  }
  fs::rename(partial, BackupRoot() / name, ec);
  if (ec) {
    REXLOG_WARN("Amped3: save backup: finishing {} failed: {}", name, ec.message());
    std::error_code cleanup;
    fs::remove_all(partial, cleanup);
    return false;
  }
  REXLOG_INFO("Amped3: save backup: {} ({})", name, reason);
  if (prune) Prune();
  return true;
}

std::once_flag g_started;

void Run() {
  try {
    std::pair<int64_t, uintmax_t> last;
    {
      std::lock_guard lock(g_mutex);
      last = Fingerprint();
      if (last.second && Backups().empty()) Backup("first run");
    }
    bool pending = false;
    std::pair<int64_t, uintmax_t> pending_print{};
    for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(pending ? 10 : 60));
      if (!REXCVAR_GET(save_backups)) continue;
      std::lock_guard lock(g_mutex);
      const auto now_print = Fingerprint();
      if (now_print == last) {
        pending = false;
        continue;
      }
      if (!pending || now_print != pending_print) {
        // Changed: wait until it stays the same for 10 s (the game writes in steps).
        pending = true;
        pending_print = now_print;
        continue;
      }
      if (Backup("saves changed")) last = now_print;
      pending = false;
    }
  } catch (const std::exception& e) {
    REXLOG_ERROR("Amped3: save backup: stopped: {}", e.what());
  } catch (...) {
    REXLOG_ERROR("Amped3: save backup: stopped");
  }
}

}  // namespace

std::vector<fs::path> List() {
  std::lock_guard lock(g_mutex);
  return Backups();
}

void Start() {
  if (!REXCVAR_GET(save_backups)) return;
  std::call_once(g_started, [] { std::thread(Run).detach(); });
}

void ScheduleRestore(const fs::path& backup) {
  std::lock_guard lock(g_mutex);
  std::ofstream(ScheduleFile()) << U8(backup.filename()) << "\n";
  REXLOG_INFO("Amped3: save backup: {} will be restored at the next start", U8(backup.filename()));
}

void CancelScheduledRestore() {
  std::lock_guard lock(g_mutex);
  std::error_code ec;
  fs::remove(ScheduleFile(), ec);
  REXLOG_INFO("Amped3: save backup: restore at the next start cancelled");
}

std::string ScheduledRestore() {
  std::lock_guard lock(g_mutex);
  return Scheduled();
}

void ApplyScheduledRestore() {
  try {
    std::lock_guard lock(g_mutex);
    // An interrupted restore (the game closed mid-swap): put any profile left
    // aside back in place.
    {
      std::error_code ec;
      fs::directory_iterator it(UserRoot(), ec), end;
      std::vector<fs::path> aside;
      for (; !ec && it != end; it.increment(ec)) {
        const std::string n = U8(it->path().filename());
        if (n.size() > 9 && n.compare(n.size() - 9, 9, ".replaced") == 0) aside.push_back(it->path());
      }
      for (const fs::path& p : aside) {
        const std::string n = U8(p.filename());
        const fs::path original = UserRoot() / fs::u8path(n.substr(0, n.size() - 9));
        std::error_code rec;
        if (!fs::exists(original, rec)) {
          fs::rename(p, original, rec);
          REXLOG_WARN("Amped3: save backup: put back {} after an interrupted restore", U8(original.filename()));
        }
      }
    }
    const std::string name = Scheduled();
    if (name.empty()) return;
    std::error_code ec;
    fs::remove(ScheduleFile(), ec);  // once, whatever happens below
    const fs::path source = BackupRoot() / fs::u8path(name);
    if (name.find_first_of("/\\.") != std::string::npos || !fs::is_directory(source, ec)) {
      REXLOG_WARN("Amped3: save backup: {} to restore is missing", name);
      return;
    }
    const auto restored = SaveFolders(source);
    if (restored.empty()) {
      REXLOG_WARN("Amped3: save backup: {} is empty; nothing restored", name);
      return;
    }
    // The current saves first; no restore without that copy.
    if (!Backup("before restoring " + name, false)) {
      REXLOG_ERROR("Amped3: save backup: couldn't back up the current saves; {} not restored", name);
      return;
    }
    for (const fs::path& from : restored) {
      const fs::path dest = UserRoot() / from.filename();
      const fs::path incoming = UserRoot() / (U8(from.filename()) + ".restoring");
      const fs::path outgoing = UserRoot() / (U8(from.filename()) + ".replaced");
      fs::remove_all(incoming, ec);
      if (fs::exists(outgoing, ec) && fs::exists(dest, ec)) fs::remove_all(outgoing, ec);
      ec.clear();
      fs::copy(from, incoming, fs::copy_options::recursive, ec);
      if (ec) {
        REXLOG_ERROR("Amped3: save backup: restoring {} failed: {}; current save kept",
                     U8(from.filename()), ec.message());
        std::error_code cleanup;
        fs::remove_all(incoming, cleanup);
        return;
      }
      const bool had = fs::exists(dest, ec);
      if (had) fs::rename(dest, outgoing, ec);
      if (!ec) fs::rename(incoming, dest, ec);
      if (ec) {
        REXLOG_ERROR("Amped3: save backup: swapping in {} failed: {}", U8(from.filename()), ec.message());
        std::error_code undo;
        if (had && !fs::exists(dest, undo)) fs::rename(outgoing, dest, undo);
        fs::remove_all(incoming, undo);
        return;
      }
      fs::remove_all(outgoing, ec);
    }
    REXLOG_INFO("Amped3: save backup: restored {}", name);
    Prune();
  } catch (const std::exception& e) {
    REXLOG_ERROR("Amped3: save backup: restore stopped: {}", e.what());
  }
}

}  // namespace amped3::save_backup
