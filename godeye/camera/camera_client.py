"""The GodEye camera client (Phase 12) on the Windows server PC: GTA V with LocalOnline.asi in camera mode.

  python camera_client.py start [--desk N]     start GTA through Steam and keep its windows on virtual desktop N (2)
  python camera_client.py stop                 close the camera GTA
  python camera_client.py status

The game is launched with `steam.exe -applaunch 271590 -nobattleye -noBE -scOffline` (no BattlEye, which blocks
ScriptHookV; no Rockstar sign-in; Steam's own settings stay as they are). Its windows (Rockstar Launcher, then the game) are moved to another virtual desktop as soon as they appear,
never activated, so whoever uses the PC is not interrupted. Rockstar's landing page needs a click on STORY MODE: it is
done (a real click, GTA ignores window messages) only when nobody has used the PC for a minute. Needs Windows 10/11 and `pip install pyvda`.
The graphics card is GTA's own setting (AdapterIndex in Documents\\Rockstar Games\\GTA V\\settings.xml); see README.
"""
import ctypes, ctypes.wintypes as wt, os, subprocess, sys, time, winreg

u32, k32 = ctypes.windll.user32, ctypes.windll.kernel32
try: ctypes.windll.shcore.SetProcessDpiAwareness(2)
except OSError: pass
GAME_EXES = {"gta5.exe", "playgtav.exe", "launcher.exe", "launcherpatcher.exe", "rockstarerrorhandler.exe", "socialclubhelper.exe"}



# started from GodEye (node, detached) this script has no console, so every tasklist / taskkill opened a console window of
# its own: a stream of terminals for as long as the start waited for the game (2026-11-17). They run windowless now.
NO_WINDOW = 0x08000000   # CREATE_NO_WINDOW

def steam_exe():
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as k:
        return winreg.QueryValueEx(k, "SteamExe")[0]


def exe_of(pid):
    h = k32.OpenProcess(0x1000, False, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
    if not h: return ""
    buf = ctypes.create_unicode_buffer(520); n = wt.DWORD(520)
    ok = k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n))
    k32.CloseHandle(h)
    return buf.value if ok else ""


def _pid_of(h):
    pid = wt.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
    return pid.value


def game_windows():
    out = []
    def cb(h, _):
        if not u32.IsWindowVisible(h): return True
        pid = wt.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if os.path.basename(exe_of(pid.value)).lower() in GAME_EXES: out.append(h)
        return True
    u32.EnumWindows(ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)(cb), 0)
    return out


def idle_seconds():
    class LII(ctypes.Structure): _fields_ = [("cbSize", wt.UINT), ("dwTime", wt.DWORD)]
    l = LII(); l.cbSize = 8
    u32.GetLastInputInfo(ctypes.byref(l))
    return (k32.GetTickCount() - l.dwTime) // 1000


def gta_memory_mb(pid):
    class PMC(ctypes.Structure):
        _fields_ = [("cb", wt.DWORD), ("PageFaultCount", wt.DWORD)] + [(n, ctypes.c_size_t) for n in
                    ("PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage",
                     "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage")]
    h = k32.OpenProcess(0x1000 | 0x0010, False, pid)
    if not h: return 0
    m = PMC(); m.cb = ctypes.sizeof(PMC)
    ctypes.windll.psapi.GetProcessMemoryInfo(h, ctypes.byref(m), m.cb)
    k32.CloseHandle(h)
    return m.WorkingSetSize // 1048576


def click_story_mode(hwnd):
    """Rockstar's landing page waits for STORY MODE (bottom right). GTA reads the mouse as raw input, and only while it
    has the focus, so this needs a real click on a focused window: only when nobody has touched the PC for a minute;
    the window goes back to its desktop and the focus back where it was."""
    from pyvda import AppView, VirtualDesktop
    view = AppView(hwnd)
    desk = view.desktop.number
    if desk != VirtualDesktop.current().number:
        view.move(VirtualDesktop.current())
        time.sleep(1.5)   # let the desktop take it before asking for the focus
    prev = u32.GetForegroundWindow()
    for _ in range(5):   # an Alt tap lets SetForegroundWindow through; check that GTA really has the focus
        u32.keybd_event(0x12, 0, 0, 0); u32.keybd_event(0x12, 0, 2, 0)
        u32.SetForegroundWindow(hwnd)
        time.sleep(0.5)
        if u32.GetForegroundWindow() == hwnd: break
    r = wt.RECT(); u32.GetClientRect(hwnd, ctypes.byref(r))
    pt = wt.POINT(int(r.right * 1167 / 1280), int(r.bottom * 684 / 720))   # "STORY MODE" on a 16:9 landing page
    u32.ClientToScreen(hwnd, ctypes.byref(pt))
    u32.SetCursorPos(pt.x - 4, pt.y); time.sleep(0.2)
    u32.SetCursorPos(pt.x, pt.y); time.sleep(0.4)   # GTA needs to see the cursor arrive (hover) before the click
    # never click blind: a Windows prompt (e.g. the firewall's "allow this app?" with its dimmed screen) can cover GTA
    under = u32.GetAncestor(u32.WindowFromPoint(pt), 2)
    if under == hwnd:
        u32.mouse_event(0x0002, 0, 0, 0, 0); time.sleep(0.12); u32.mouse_event(0x0004, 0, 0, 0, 0)
        time.sleep(1.5)
    if prev and prev != hwnd: u32.SetForegroundWindow(prev)
    view.move(VirtualDesktop(desk))
    if under == hwnd:
        print("clicked STORY MODE on the landing page", flush=True)
    else:
        cls = ctypes.create_unicode_buffer(256); u32.GetClassNameW(under, cls, 256)
        print(f"not clicking: another window covers GTA ({cls.value}); a pending Windows prompt needs an answer", flush=True)


def answer_scripthook_prompt():
    """ScriptHookV asks "CHECK FOR SCRIPT HOOK V UPDATES ?" at start (a yes opens its download page): No, by a message to
    the button (the dialog sits on the other virtual desktop; nothing is clicked on screen)."""
    h = u32.FindWindowW(None, "SCRIPT HOOK V")
    if not h: return False
    found = []
    def cb(c, _):
        cls = ctypes.create_unicode_buffer(64); u32.GetClassNameW(c, cls, 64)
        t = ctypes.create_unicode_buffer(64); u32.GetWindowTextW(c, t, 64)
        if cls.value == "Button" and t.value.replace("&", "").strip().lower() == "no": found.append(c)
        return True
    u32.EnumChildWindows(h, ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)(cb), 0)
    for c in found: u32.SendMessageW(c, 0x00F5, 0, 0)   # BM_CLICK
    if found: print("answered No to ScriptHookV's update prompt", flush=True)
    return bool(found)


# The camera's mods live outside the game folder (2026-11-16) and are put in only while the camera runs, so this PC's
# own GTA is the plain game the rest of the time: the ASI loader, ScriptHookV, RageOpenV (the mods folder, a junction to
# D:\GTACamera\mods), LocalOnline in camera mode and Gori Online (the world only, in camera mode)
CAMERA_BIN = os.environ.get("CAMERA_BIN", "D:/GTACamera/bin")
CAMERA_FILES = ["dinput8.dll", "ScriptHookV.dll", "RageOpenV.asi", "LocalOnline.asi", "GoriOnline.asi"]


def gta_dir():
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as k:
        steam = os.path.dirname(winreg.QueryValueEx(k, "SteamExe")[0])
    return os.path.join(steam, "steamapps", "common", "Grand Theft Auto V")


def install_mods():
    import shutil
    g = gta_dir()
    for f in CAMERA_FILES:
        src = os.path.join(CAMERA_BIN, f)
        if os.path.exists(src): shutil.copy2(src, os.path.join(g, f))
    print(f"camera mods put in {g}", flush=True)


def remove_mods():
    g = gta_dir()
    n = 0
    for f in CAMERA_FILES:
        p = os.path.join(g, f)
        if os.path.exists(p) and os.path.exists(os.path.join(CAMERA_BIN, f)):   # only our own copies
            os.remove(p); n += 1
    print(f"camera mods taken out ({n} files): this PC's GTA is the plain game again", flush=True)


def gta_pids():
    r = subprocess.run(["tasklist", "/FI", "IMAGENAME eq GTA5.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True, creationflags=NO_WINDOW)
    return [int(line.split('","')[1]) for line in r.stdout.splitlines() if line.startswith('"GTA5.exe"')]


def start(desk):
    from pyvda import AppView, VirtualDesktop
    if gta_pids():
        print("GTA is already running"); return
    install_mods()
    # no BattlEye (it blocks ScriptHookV, so the game never shows a window) and no Rockstar sign-in
    # the camera's window and card by command line (2026-11-16), so the PC's own GTA settings (settings.xml: the
    # user's 4K borderless on card 0) stay as they are: a 1280x720 window on DXGI adapter 1, the RTX 5060 Ti 8 GB
    subprocess.Popen(creationflags=NO_WINDOW, args=[steam_exe(), "-applaunch", "271590", "-nobattleye", "-noBE", "-scOffline",
                      "-windowed", "-width", "1280", "-height", "720", "-adapter", os.environ.get("CAMERA_ADAPTER", "1")])
    moved, t0, landed, low_since = set(), time.time(), False, None
    while time.time() - t0 < 1800:   # the launcher, then the game window (loading takes minutes)
        for h in game_windows():
            if h in moved: continue
            try:
                AppView(h).move(VirtualDesktop(desk))
                moved.add(h)
                print(f"window {h} -> desktop {desk}", flush=True)
            except Exception as e:
                print("move failed", e, flush=True)
        answer_scripthook_prompt()
        pids = gta_pids()
        game = [h for h in game_windows() if os.path.basename(exe_of(_pid_of(h))).lower() == "gta5.exe"]
        if pids and game and not landed:
            # story mode has loaded once LocalOnline writes its log (2026-11-16: memory told it no more, build 3889's
            # landing page already takes 1.9 GB); until then the landing page waits for STORY MODE
            lo = os.path.join(os.path.dirname(exe_of(pids[0])), "LocalOnline", "LocalOnline.log")
            if os.path.exists(lo) and os.path.getmtime(lo) > t0: landed = True; print("story mode loaded (LocalOnline is up)", flush=True)
            elif low_since is None: low_since = time.time()
            elif time.time() - low_since > 40 and idle_seconds() >= 60:
                click_story_mode(game[0])
                low_since = time.time()
        if landed and time.time() - t0 > 60: break
        time.sleep(0.25)
    print("camera client started" if gta_pids() else "GTA did not start")


def launcher_pids():
    """PlayGTAV.exe and the Rockstar Launcher stay after the game closes; while they run Steam takes GTA for running and
    a new start does nothing (2026-11-16). Only those of this GTA's install and of Rockstar Games."""
    r = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, text=True, creationflags=NO_WINDOW)
    out = []
    for line in r.stdout.splitlines():
        f = line.strip('"').split('","')
        if len(f) > 1 and f[0].lower() in ("playgtav.exe", "launcher.exe", "socialclubhelper.exe", "rockstarservice.exe"):
            path = exe_of(int(f[1])).lower()
            if "grand theft auto v" in path or "rockstar games" in path: out.append(int(f[1]))
    return out


def stop():
    pids = gta_pids()
    for pid in pids:
        subprocess.run(["taskkill", "/PID", str(pid)], capture_output=True, creationflags=NO_WINDOW)
    time.sleep(5)
    for pid in gta_pids():
        subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True, creationflags=NO_WINDOW)
    lp = launcher_pids()
    for pid in lp:
        subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True, creationflags=NO_WINDOW)
    print(f"stopped {len(pids)} GTA process(es) and {len(lp)} launcher process(es)")
    time.sleep(2)
    remove_mods()


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    desk = int(sys.argv[sys.argv.index("--desk") + 1]) if "--desk" in sys.argv else 2
    if cmd == "start": start(desk)
    elif cmd == "stop": stop()
    else: print("running" if gta_pids() else "not running")
