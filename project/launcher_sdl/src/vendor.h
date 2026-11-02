// Vendor packages (logic.h VendorPackages): third-party runtime files fetched
// from their makers on the player's request, checked, then kept in
// <launcher folder>\vendor\<id>\.
#pragma once

#include <functional>
#include <string>

#include "logic.h"

namespace vendor {

// The PC can use it (e.g. DLSS: an NVIDIA RTX graphics card).
bool Available(const launcher::VendorPackage& p);
// Its files and license are in place, at their pinned sizes.
bool Installed(const launcher::VendorPackage& p, const std::string& dist_dir);
// Downloads one file to <name>.part, checks its size and SHA-256 and only then
// renames it; a file that doesn't check out is deleted. progress(done, total)
// returns false to cancel.
bool Fetch(const launcher::VendorPackage& p, const launcher::VendorFile& f, const std::string& dist_dir,
           const std::function<bool(unsigned long long, unsigned long long)>& progress, std::string& error);

}  // namespace vendor
