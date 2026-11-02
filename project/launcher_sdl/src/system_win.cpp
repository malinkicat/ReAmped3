#include <windows.h>
#include <bcrypt.h>
#include <dxgi.h>
#include <shellapi.h>
#include <winhttp.h>

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

std::vector<std::pair<int, int>> DisplayModes() {
  std::vector<std::pair<int, int>> modes;
  DEVMODEW dm = {};
  dm.dmSize = sizeof(dm);
  for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); ++i) {
    const std::pair<int, int> m(int(dm.dmPelsWidth), int(dm.dmPelsHeight));
    if (std::find(modes.begin(), modes.end(), m) == modes.end()) modes.push_back(m);
  }
  return modes;
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

bool MakeDir(const std::string& path) {
  CreateDirectoryW(Wide(path).c_str(), nullptr);
  return DirExists(path);
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

bool ListFiles(const std::string& dir, std::vector<std::string>& out) {
  out.clear();
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileExW(Wide(launcher::JoinPath(dir, "*")).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
                              nullptr, 0);
  if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(Utf8(fd.cFileName));
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return true;
}

uint64_t FileTime(const std::string& path) {
  WIN32_FILE_ATTRIBUTE_DATA d;
  if (!GetFileAttributesExW(Wide(path).c_str(), GetFileExInfoStandard, &d)) return 0;
  return (uint64_t(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime;
}

bool RemoveTree(const std::string& path) {
  if (!DirExists(path)) return true;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileExW(Wide(launcher::JoinPath(path, "*")).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
                              nullptr, 0);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      std::string name = Utf8(fd.cFileName);
      if (name == "." || name == "..") continue;
      std::string child = launcher::JoinPath(path, name);
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        RemoveTree(child);
      } else {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(Wide(child).c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(Wide(child).c_str());
      }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  RemoveDirectoryW(Wide(path).c_str());
  return !DirExists(path);
}

bool RenameDir(const std::string& from, const std::string& to) {
  return MoveFileExW(Wide(from).c_str(), Wide(to).c_str(), 0) != 0;
}

bool RunHidden(const std::string& exe, const std::string& command_line, const std::string& work_dir,
               const std::function<bool()>& keep_going, int& exit_code, std::string& error) {
  std::wstring cmd = Wide(command_line);
  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessW(Wide(exe).c_str(), cmd.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, Wide(work_dir).c_str(), &si, &pi)) {
    error = "can't start " + exe + " (" + std::to_string(GetLastError()) + ")";
    return false;
  }
  CloseHandle(pi.hThread);
  bool ok = true;
  while (WaitForSingleObject(pi.hProcess, 200) == WAIT_TIMEOUT) {
    if (keep_going && !keep_going()) {
      TerminateProcess(pi.hProcess, 1);
      WaitForSingleObject(pi.hProcess, 5000);
      error = "cancelled";
      ok = false;
      break;
    }
  }
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  exit_code = int(code);
  CloseHandle(pi.hProcess);
  return ok;
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

namespace {
// Brings the first top-level window whose title starts with `prefix` forward.
void FocusWindowTitled(const wchar_t* prefix) {
  struct Find {
    const wchar_t* prefix;
    HWND found = nullptr;
  } find{prefix};
  EnumWindows(
      [](HWND hwnd, LPARAM param) -> BOOL {
        auto* f = reinterpret_cast<Find*>(param);
        wchar_t title[128] = {};
        GetWindowTextW(hwnd, title, 127);
        if (IsWindowVisible(hwnd) && wcsncmp(title, f->prefix, wcslen(f->prefix)) == 0) {
          f->found = hwnd;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&find));
  if (find.found) {
    if (IsIconic(find.found)) ShowWindow(find.found, SW_RESTORE);
    SetForegroundWindow(find.found);
  }
}
}  // namespace

bool ClaimSingleInstance() {
  // The game holds "Local\ReAmped3.Game" (amped3_app.h).
  if (HANDLE game = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\ReAmped3.Game")) {
    CloseHandle(game);
    FocusWindowTitled(L"ReAmped 3 |");
    return false;
  }
  static HANDLE mine = CreateMutexW(nullptr, FALSE, L"Local\\ReAmped3.Launcher");
  if (mine && GetLastError() == ERROR_ALREADY_EXISTS) {
    FocusWindowTitled(L"ReAmped 3");
    return false;
  }
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

long long FileSize(const std::string& path) {
  WIN32_FILE_ATTRIBUTE_DATA d;
  if (!GetFileAttributesExW(Wide(path).c_str(), GetFileExInfoStandard, &d)) return -1;
  if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return -1;
  return (static_cast<long long>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
}

bool RemoveFile(const std::string& path) { return DeleteFileW(Wide(path).c_str()) != 0; }

bool MoveOver(const std::string& from, const std::string& to) {
  return MoveFileExW(Wide(from).c_str(), Wide(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool Download(const std::string& url, const std::string& path,
              const std::function<bool(unsigned long long, unsigned long long)>& progress, std::string& error) {
  std::wstring wurl = Wide(url);
  URL_COMPONENTS parts = {};
  parts.dwStructSize = sizeof(parts);
  wchar_t host[256] = {};
  wchar_t object[2048] = {};
  parts.lpszHostName = host;
  parts.dwHostNameLength = DWORD(std::size(host));
  parts.lpszUrlPath = object;
  parts.dwUrlPathLength = DWORD(std::size(object));
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
    error = "bad address";
    return false;
  }
  HINTERNET session = WinHttpOpen(L"ReAmped3-launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) session = WinHttpOpen(L"ReAmped3-launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0);
  HINTERNET connect = session ? WinHttpConnect(session, host, parts.nPort, 0) : nullptr;
  HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", object, nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                              : nullptr;
  auto close = [&] {
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
  };
  if (!request || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request, nullptr)) {
    error = "no connection (" + std::to_string(GetLastError()) + ")";
    close();
    return false;
  }
  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                      &status, &size, WINHTTP_NO_HEADER_INDEX);
  if (status != 200) {
    error = "the server answered " + std::to_string(status);
    close();
    return false;
  }
  unsigned long long total = 0;
  wchar_t length[32] = {};
  size = sizeof(length);
  if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length, &size,
                          WINHTTP_NO_HEADER_INDEX)) {
    total = _wcstoui64(length, nullptr, 10);
  }
  HANDLE file = CreateFileW(Wide(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    error = "can't write " + path;
    close();
    return false;
  }
  std::vector<char> buffer(1 << 16);
  unsigned long long done = 0;
  bool ok = true;
  for (;;) {
    DWORD got = 0;
    if (!WinHttpReadData(request, buffer.data(), DWORD(buffer.size()), &got)) {
      error = "the download stopped (" + std::to_string(GetLastError()) + ")";
      ok = false;
      break;
    }
    if (got == 0) break;
    DWORD written = 0;
    if (!::WriteFile(file, buffer.data(), got, &written, nullptr) || written != got) {
      error = "can't write " + path;
      ok = false;
      break;
    }
    done += got;
    if (progress && !progress(done, total)) {
      error = "cancelled";
      ok = false;
      break;
    }
  }
  CloseHandle(file);
  close();
  return ok;
}

std::string Sha256File(const std::string& path) {
  HANDLE file = CreateFileW(Wide(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::string hex;
  if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
      BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
    std::vector<unsigned char> buffer(1 << 16);
    DWORD got = 0;
    bool ok = true;
    while (::ReadFile(file, buffer.data(), DWORD(buffer.size()), &got, nullptr) && got) {
      if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), got, 0))) {
        ok = false;
        break;
      }
    }
    unsigned char digest[32];
    if (ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
      static const char* kHex = "0123456789abcdef";
      for (unsigned char b : digest) {
        hex += kHex[b >> 4];
        hex += kHex[b & 15];
      }
    }
  }
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  CloseHandle(file);
  return hex;
}

std::vector<Adapter> Adapters() {
  std::vector<Adapter> out;
  IDXGIFactory1* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return out;
  IDXGIAdapter1* adapter = nullptr;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc;
    if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
      out.push_back({desc.VendorId, Utf8(desc.Description)});
    }
    adapter->Release();
  }
  factory->Release();
  return out;
}

}  // namespace sys
