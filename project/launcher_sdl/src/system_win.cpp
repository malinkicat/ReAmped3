#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>

#include "system.h"

namespace sys {

namespace {

std::wstring Wide(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}

std::string Utf8(const wchar_t* w, int len = -1) {
  if (!w) return {};
  if (len < 0) len = int(wcslen(w));
  if (len == 0) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, len, s.data(), n, nullptr, nullptr);
  return s;
}

}  // namespace

std::string ExeDir() {
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
    if (n < buf.size()) {
      buf.resize(n);
      break;
    }
    buf.resize(buf.size() * 2);
  }
  size_t slash = buf.find_last_of(L"\\/");
  return Utf8(buf.c_str(), int(slash == std::wstring::npos ? 0 : slash + 1));
}

std::vector<std::string> LauncherArgs() {
  std::vector<std::string> out;
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) return out;
  for (int i = 1; i < argc; ++i) out.push_back(Utf8(argv[i]));
  LocalFree(argv);
  return out;
}

launcher::DisplayInfo PrimaryDisplay() {
  launcher::DisplayInfo d;
  DEVMODEW dm = {};
  dm.dmSize = sizeof(dm);
  if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
    d.width = int(dm.dmPelsWidth);
    d.height = int(dm.dmPelsHeight);
    d.hz = dm.dmDisplayFrequency > 1 ? int(dm.dmDisplayFrequency) : 0;
  }
  return d;
}

bool RunningUnderWine() {
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

std::string EnvVar(const char* name) {
  std::wstring wname = Wide(name);
  DWORD n = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
  if (n == 0) return {};
  std::wstring buf(n, L'\0');
  n = GetEnvironmentVariableW(wname.c_str(), buf.data(), n);
  return Utf8(buf.c_str(), int(n));
}

bool FileExists(const std::string& path) {
  DWORD a = GetFileAttributesW(Wide(path).c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::string& path) {
  DWORD a = GetFileAttributesW(Wide(path).c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadFile(const std::string& path, std::string& out) {
  FILE* f = _wfopen(Wide(path).c_str(), L"rb");
  if (!f) return false;
  out.clear();
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
  fclose(f);
  return true;
}

bool WriteFile(const std::string& path, const std::string& data) {
  FILE* f = _wfopen(Wide(path).c_str(), L"wb");
  if (!f) return false;
  bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
  return fclose(f) == 0 && ok;
}

std::vector<std::string> Drives() {
  std::vector<std::string> out;
  wchar_t buf[512];
  DWORD n = GetLogicalDriveStringsW(511, buf);
  for (const wchar_t* p = buf; n && *p; p += wcslen(p) + 1) out.push_back(Utf8(p));
  return out;
}

bool ListFolders(const std::string& dir, std::vector<std::string>& out) {
  out.clear();
  std::string pattern = launcher::JoinPath(dir, "*");
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileExW(Wide(pattern).c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0);
  if (h == INVALID_HANDLE_VALUE) return false;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
    if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
    if (fd.cFileName[0] == L'.') continue;  // ".", ".." and Linux dot folders
    out.push_back(Utf8(fd.cFileName));
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
    return CompareStringOrdinal(Wide(a).c_str(), -1, Wide(b).c_str(), -1, TRUE) == CSTR_LESS_THAN;
  });
  return true;
}

bool StartProcess(const std::string& exe, const std::string& command_line, const std::string& work_dir,
                  Process& out, std::string& error) {
  std::wstring cmd = Wide(command_line);
  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessW(Wide(exe).c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      Wide(work_dir).c_str(), &si, &pi)) {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, GetLastError(), 0, (wchar_t*)&msg, 0, nullptr);
    error = msg ? Utf8(msg) : "error " + std::to_string(GetLastError());
    while (!error.empty() && (error.back() == '\n' || error.back() == '\r' || error.back() == '.')) error.pop_back();
    if (msg) LocalFree(msg);
    return false;
  }
  CloseHandle(pi.hThread);
  out.handle = pi.hProcess;
  out.pid = pi.dwProcessId;
  return true;
}

bool ProcessExited(const Process& p) { return !p.handle || WaitForSingleObject(p.handle, 0) == WAIT_OBJECT_0; }

void WaitForExit(const Process& p) {
  if (p.handle) WaitForSingleObject(p.handle, INFINITE);
}

void CloseProcess(Process& p) {
  if (p.handle) CloseHandle(p.handle);
  p = {};
}

void AllowForeground(const Process& p) { AllowSetForegroundWindow(p.pid); }

namespace {
struct FindWindowData {
  DWORD pid;
  HWND found;
};
BOOL CALLBACK FindGameWindowProc(HWND h, LPARAM l) {
  auto* d = reinterpret_cast<FindWindowData*>(l);
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  if (pid != d->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
  wchar_t title[64] = {};
  GetWindowTextW(h, title, 64);
  // The game window ("ReAmped 3 | ..."), not the F10 frame-graph overlay.
  if (wcsncmp(title, L"ReAmped 3", 9) != 0) return TRUE;
  d->found = h;
  return FALSE;
}
}  // namespace

bool BringGameWindowToFront(const Process& p) {
  FindWindowData d = {p.pid, nullptr};
  EnumWindows(FindGameWindowProc, reinterpret_cast<LPARAM>(&d));
  if (!d.found) return false;
  SetForegroundWindow(d.found);
  return true;
}

void AttachParentConsole() {
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN) return;  // pipe or file
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
  }
}

}  // namespace sys
