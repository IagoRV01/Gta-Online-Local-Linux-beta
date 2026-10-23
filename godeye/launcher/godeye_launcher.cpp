// GodEye.exe: opens and closes GodEye from the desktop.
//   GodEye.exe            start the backend (and the tunnel, if configured), open the GodEye window on the configured
//                         GPU; closing the window stops what it started
//   GodEye.exe --gpus     list the graphics adapters, to pick one for `gpu=` in GodEye.ini
// Settings: GodEye.ini next to the exe, else %LOCALAPPDATA%\GodEye\GodEye.ini (see godeye/launcher/GodEye.example.ini).
// The window is Edge (or Chrome) in app mode with its own profile, pinned to one adapter with --use-adapter-luid.
#include <winsock2.h>
#include <windows.h>
#include <dxgi.h>
#include <shlobj.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

namespace {

struct Gpu { int index; std::string name; double vramGB; LONG luidHigh; DWORD luidLow; UINT vendor; };

std::string Narrow(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Wide(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::vector<Gpu> ListGpus() {
    std::vector<Gpu> out;
    IDXGIFactory1 *f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&f))) return out;
    IDXGIAdapter1 *a = nullptr;
    for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            out.push_back({(int)i, Narrow(d.Description), d.DedicatedVideoMemory / 1073741824.0, d.AdapterLuid.HighPart, d.AdapterLuid.LowPart, d.VendorId});
        a->Release();
    }
    f->Release();
    return out;
}

std::string Lower(std::string s) { for (auto &c : s) c = (char)tolower((unsigned char)c); return s; }

// "RTX 5060 Ti 8GB": every word must be in the adapter's name, and "<n>GB" must match its memory (rounded)
// "index:1" or "luid:<high>,<low>" pick one directly; "" or "auto" leaves the choice to Windows
const Gpu *Pick(const std::vector<Gpu> &gpus, const std::string &sel) {
    std::string s = Lower(sel);
    if (s.empty() || s == "auto") return nullptr;
    for (const auto &g : gpus) {
        if (s.rfind("index:", 0) == 0 && std::atoi(s.c_str() + 6) == g.index) return &g;
        if (s.rfind("luid:", 0) == 0 && s.substr(5) == std::to_string(g.luidHigh) + "," + std::to_string(g.luidLow)) return &g;
    }
    if (s.rfind("index:", 0) == 0 || s.rfind("luid:", 0) == 0) return nullptr;
    for (const auto &g : gpus) {
        std::string name = Lower(g.name);
        bool ok = true;
        size_t p = 0;
        while (ok && p < s.size()) {
            size_t e = s.find(' ', p);
            std::string w = s.substr(p, e == std::string::npos ? std::string::npos : e - p);
            p = e == std::string::npos ? s.size() : e + 1;
            if (w.empty()) continue;
            if (w.size() > 2 && w.compare(w.size() - 2, 2, "gb") == 0 && isdigit((unsigned char)w[0]))
                ok = std::lround(g.vramGB) == std::atoi(w.c_str());
            else ok = name.find(w) != std::string::npos;
        }
        if (ok) return &g;
    }
    return nullptr;
}

std::string GpuLine(const Gpu &g) {
    char b[256];
    std::snprintf(b, sizeof b, "index:%d  %s  %.0f GB  luid:%ld,%lu", g.index, g.name.c_str(), std::round(g.vramGB), g.luidHigh, g.luidLow);
    return b;
}

// ---- settings
struct Ini {
    std::string path;
    std::string Get(const char *key, const char *def) const {
        char b[1024];
        GetPrivateProfileStringA("godeye", key, def, b, sizeof b, path.c_str());
        return b;
    }
};

std::string ExeDir() {
    char p[MAX_PATH];
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string s = p;
    return s.substr(0, s.find_last_of("\\/") + 1);
}

std::string LocalAppData() {
    char p[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, p))) return std::string(p) + "\\GodEye";
    return ExeDir();
}

bool Exists(const std::string &p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// errors show the worried Zorua when the exe was built with it (icon 2, tools/zorua), else the system's error icon
void Fail(const std::string &msg) {
    MSGBOXPARAMSW mb{};
    mb.cbSize = sizeof(mb);
    mb.hInstance = GetModuleHandleW(nullptr);
    std::wstring text = Wide(msg);
    mb.lpszText = text.c_str();
    mb.lpszCaption = L"GodEye";
    mb.dwStyle = MB_OK;
    if (FindResourceW(mb.hInstance, MAKEINTRESOURCEW(2), MAKEINTRESOURCEW(14))) {   // 14: RT_GROUP_ICON
        mb.dwStyle |= MB_USERICON;
        mb.lpszIcon = MAKEINTRESOURCEW(2);
    } else mb.dwStyle |= MB_ICONERROR;
    MessageBoxIndirectW(&mb);
    ExitProcess(1);
}

std::string ErrorText(DWORD e) {
    char buf[256] = "";
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), buf, sizeof buf, nullptr);
    std::string s = buf;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s.empty() ? "error " + std::to_string(e) : s + " (" + std::to_string(e) + ")";
}

// GodEye.ini: next to the exe, else in Local AppData (asked of the shell, then of the environment). Windows can refuse
// a file for a moment right after logon or while it is scanned, so each place is tried a few times before giving up.
std::string FindSettings() {
    std::vector<std::string> where = { ExeDir() + "GodEye.ini", LocalAppData() + "\\GodEye.ini" };
    char env[MAX_PATH];
    if (GetEnvironmentVariableA("LOCALAPPDATA", env, MAX_PATH)) where.push_back(std::string(env) + "\\GodEye\\GodEye.ini");
    if (GetEnvironmentVariableA("USERPROFILE", env, MAX_PATH)) where.push_back(std::string(env) + "\\AppData\\Local\\GodEye\\GodEye.ini");
    std::string why;
    for (int attempt = 0; attempt < 8; attempt++) {
        why.clear();
        for (const auto &p : where) {
            if (Exists(p)) return p;
            if (why.find(p) == std::string::npos) why += p + "\n    " + ErrorText(GetLastError()) + "\n";
        }
        Sleep(400);
    }
    Fail("Settings not found. Looked for GodEye.ini in:\n" + why + "\nCopy godeye\\launcher\\GodEye.example.ini to one of them and edit it.");
    return "";
}

bool PortOpen(int port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool ok = connect(s, (sockaddr *)&a, sizeof a) == 0;
    closesocket(s);
    return ok;
}

// a child that dies with this launcher (the job closes when we exit, even on a crash)
HANDLE g_job = nullptr;
// tied: in g_job, so it ends with us. job: also put it in that job (followed, not killed).
bool Start(const std::string &exe, const std::string &args, const std::string &cwd, const std::string &extraEnv, bool tied, HANDLE *proc, HANDLE job = nullptr) {
    std::string cmd = "\"" + exe + "\" " + args;
    STARTUPINFOA si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::string env;
    if (!extraEnv.empty()) {   // our environment plus one variable (the tunnel token stays off the command line)
        char *block = GetEnvironmentStringsA();
        for (char *p = block; *p; p += strlen(p) + 1) env.append(p).push_back('\0');
        FreeEnvironmentStringsA(block);
        env.append(extraEnv).push_back('\0');
        env.push_back('\0');
    }
    DWORD flags = tied ? CREATE_NO_WINDOW | CREATE_SUSPENDED : job ? CREATE_SUSPENDED : 0;
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, flags, env.empty() ? nullptr : env.data(),
                        cwd.empty() ? nullptr : cwd.c_str(), &si, &pi))
        return false;
    if (tied) AssignProcessToJobObject(g_job, pi.hProcess);
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    if (tied || job) ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    if (proc) *proc = pi.hProcess; else CloseHandle(pi.hProcess);
    return true;
}

// a visible top-level window titled "GodEye" that belongs to the browser: the page is still open. The browser can
// move the window to a process we did not start (Edge restarting itself after an update), so the process list alone
// is not enough to know.
bool GodEyeWindowOpen(const std::string &browserExe) {
    struct Ctx { std::string exe; bool found; } ctx{Lower(browserExe), false};
    EnumWindows([](HWND h, LPARAM p) -> BOOL {
        auto *c = reinterpret_cast<Ctx *>(p);
        if (!IsWindowVisible(h)) return TRUE;
        wchar_t title[64] = L"";
        GetWindowTextW(h, title, 64);
        if (wcscmp(title, L"GodEye") != 0) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        char path[MAX_PATH] = "";
        DWORD n = MAX_PATH;
        if (HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) { QueryFullProcessImageNameA(ph, 0, path, &n); CloseHandle(ph); }
        if (Lower(path) == c->exe) { c->found = true; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

std::string ReadFirstLine(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return "";
    char b[4096] = {0};
    std::fgets(b, sizeof b, f);
    std::fclose(f);
    std::string s = b;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR cmdLine, int) {
    std::string args = cmdLine ? cmdLine : "";
    auto gpus = ListGpus();
    if (args.find("--gpus") != std::string::npos) {
        std::string text = "Graphics adapters (put one of these in GodEye.ini as gpu=...):\n\n";
        for (const auto &g : gpus) text += GpuLine(g) + "\n";
        text += "\nExamples: gpu=RTX 5060 Ti 8GB    gpu=index:0    gpu=luid:0,123456    gpu=auto";
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);   // redirected (a pipe or a file): write there
        DWORD written = 0;
        if (out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN && GetFileType(out) != FILE_TYPE_CHAR) {
            text += "\n";
            WriteFile(out, text.data(), (DWORD)text.size(), &written, nullptr);
        } else if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE *o = nullptr;
            freopen_s(&o, "CONOUT$", "w", stdout);
            std::printf("\n%s\n", text.c_str());
        } else MessageBoxW(nullptr, Wide(text).c_str(), L"GodEye: graphics adapters", MB_OK);
        return 0;
    }

    Ini ini;
    ini.path = FindSettings();
    std::string backend = ini.Get("backend", ""), node = ini.Get("node", "C:\\Program Files\\nodejs\\node.exe");
    std::string url = ini.Get("url", "http://127.0.0.1:8080/");
    int port = std::atoi(ini.Get("port", "8080").c_str());
    std::string browser = ini.Get("browser", "");
    if (browser.empty())
        for (const char *b : {"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe", "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
                              "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe"})
            if (Exists(b)) { browser = b; break; }
    if (browser.empty() || !Exists(browser)) Fail("No Edge or Chrome found: set browser= in " + ini.path);

    // the GPU: only the configured one (or Windows' choice with gpu=auto)
    std::string sel = ini.Get("gpu", "auto");
    const Gpu *gpu = Pick(gpus, sel);
    if (!gpu && Lower(sel) != "auto" && !sel.empty()) {
        std::string list;
        for (const auto &g : gpus) list += "  " + GpuLine(g) + "\n";
        Fail("The graphics card \"" + sel + "\" in GodEye.ini was not found. GodEye only runs on the card you chose.\n\nAdapters on this PC:\n" + list);
    }

    g_job = CreateJobObjectA(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim{};
    lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &lim, sizeof lim);

    // a second GodEye.exe while one runs: just another window on the running one
    HANDLE mutex = CreateMutexA(nullptr, TRUE, "Local\\GodEyeLauncher");
    bool first = GetLastError() != ERROR_ALREADY_EXISTS;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    if (first && !PortOpen(port)) {   // the backend (unless it already runs, e.g. as a service)
        if (backend.empty() || !Exists(backend + "\\src\\main.ts")) Fail("backend= in " + ini.path + " does not point at godeye\\backend");
        if (!Exists(node)) Fail("Node.js not found at " + node + " (node= in GodEye.ini)");
        if (!Start(node, "src\\main.ts godeye.json", backend, "", true, nullptr)) Fail("Could not start the GodEye backend.");
        int t = 0;
        while (!PortOpen(port) && t < 15000) { Sleep(250); t += 250; }
        if (!PortOpen(port)) Fail("The GodEye backend did not start (see its log, godeye.log).");
    }
    std::string tunnel = ini.Get("cloudflared", ""), tokenFile = ini.Get("tunnel_token_file", "");
    if (first && !tunnel.empty()) {   // the public address, while GodEye is open
        std::string token = ReadFirstLine(tokenFile);
        if (!Exists(tunnel) || token.empty()) Fail("cloudflared= or tunnel_token_file= in GodEye.ini is wrong.");
        if (!Start(tunnel, "tunnel --no-autoupdate run", "", "TUNNEL_TOKEN=" + token, true, nullptr)) Fail("Could not start cloudflared.");
    }

    // Phase 12: the camera client (a second GTA) opens and closes with GodEye when camera=on
    bool camera = first && Lower(ini.Get("camera", "off")) == "on";
    std::string py = ini.Get("python", "pythonw.exe"), camScript = backend + "\\..\\camera\\camera_client.py";
    if (camera && !Start(py, "\"" + camScript + "\" start", "", "", false, nullptr)) camera = false;

    std::string profile = LocalAppData() + "\\window";
    CreateDirectoryA(LocalAppData().c_str(), nullptr);
    std::string bargs = "--app=" + url + " --user-data-dir=\"" + profile + "\" --no-first-run --no-default-browser-check --window-size=1500,920";
    if (gpu) bargs += " --use-angle=d3d11 --use-adapter-luid=" + std::to_string(gpu->luidHigh) + "," + std::to_string(gpu->luidLow);
    HANDLE win = nullptr, winJob = CreateJobObjectA(nullptr, nullptr);   // every process of the window, followed
    if (!Start(browser, bargs, "", "", false, &win, first ? winJob : nullptr)) Fail("Could not open the GodEye window with " + browser);
    if (!first) return 0;
    // GodEye stays on while its window is open. The browser we started may hand the window to a new process of its own
    // (Edge does on a fresh profile, and when it restarts after an update), so GodEye goes off only after 2 s with
    // neither a process of the window's job nor a "GodEye" window of the browser.
    WaitForSingleObject(win, INFINITE);
    CloseHandle(win);
    for (int gone = 0; gone < 4; Sleep(500)) {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION ai{};
        bool procs = QueryInformationJobObject(winJob, JobObjectBasicAccountingInformation, &ai, sizeof ai, nullptr) && ai.ActiveProcesses > 0;
        gone = procs || GodEyeWindowOpen(browser) ? 0 : gone + 1;
    }
    CloseHandle(winJob);
    if (camera) {   // close the camera GTA first (it is not in our job: Steam started it)
        HANDLE stop = nullptr;
        if (Start(py, "\"" + camScript + "\" stop", "", "", false, &stop)) { WaitForSingleObject(stop, 20000); CloseHandle(stop); }
    }
    CloseHandle(g_job);                   // stops the backend and the tunnel we started
    ReleaseMutex(mutex);
    return 0;
}
