// save_backup.h - Automatic backups of the game's saves
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace amped3::save_backup {

// Starts backing up (a thread; once).
void Start();

// The backups, newest first.
std::vector<std::filesystem::path> List();

// Restores this backup at the next start.
void ScheduleRestore(const std::filesystem::path& backup);

// Cancels a scheduled restore.
void CancelScheduledRestore();

// The backup's name scheduled for restoring at the next start ("" if none).
std::string ScheduledRestore();

// At start, before the game reads its saves: applies a scheduled restore.
void ApplyScheduledRestore();

}  // namespace amped3::save_backup
