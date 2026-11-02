#include "vendor.h"

#include "system.h"

using namespace launcher;

namespace vendor {

bool Available(const VendorPackage& p) {
  if (p.needs == VendorNeeds::kAnyPc) return true;
  for (const sys::Adapter& a : sys::Adapters()) {
    if (IsNvidiaRtx(a.vendor_id, a.description)) return true;
  }
  return false;
}

bool Installed(const VendorPackage& p, const std::string& dist_dir) {
  const std::string dir = VendorDir(dist_dir, p.id);
  if (sys::FileSize(dir + p.license.name) != static_cast<long long>(p.license.size)) return false;
  for (const VendorFile& f : p.files) {
    if (sys::FileSize(dir + f.name) != static_cast<long long>(f.size)) return false;
  }
  return true;
}

bool Fetch(const VendorPackage& p, const VendorFile& f, const std::string& dist_dir,
           const std::function<bool(unsigned long long, unsigned long long)>& progress, std::string& error) {
  const std::string dir = VendorDir(dist_dir, p.id);
  sys::MakeDir(JoinPath(dist_dir, "vendor"));
  if (!sys::MakeDir(dir)) {
    error = "can't create " + dir;
    return false;
  }
  const std::string target = dir + f.name, part = target + ".part";
  if (sys::FileSize(target) == static_cast<long long>(f.size) && sys::Sha256File(target) == f.sha256) return true;
  bool ok = sys::Download(f.url, part, progress, error);
  if (ok && sys::FileSize(part) != static_cast<long long>(f.size)) {
    error = f.name + " has the wrong size";
    ok = false;
  }
  if (ok && sys::Sha256File(part) != f.sha256) {
    error = f.name + " doesn't match its checksum";
    ok = false;
  }
  if (ok && !sys::MoveOver(part, target)) {
    error = "can't write " + target;
    ok = false;
  }
  if (!ok) sys::RemoveFile(part);
  return ok;
}

}  // namespace vendor
