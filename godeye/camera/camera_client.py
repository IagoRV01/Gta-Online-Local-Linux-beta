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
    u32.mouse_event(0x0002, 0, 0, 0, 0); time.sleep(0.12); u32.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(1.5)
    if prev and prev != hwnd: u32.SetForegroundWindow(prev)
    view.move(VirtualDesktop(desk))
    print("clicked STORY MODE on the landing page", flush=True)


def gta_pids():
    r = subprocess.run(["tasklist", "/FI", "IMAGENAME eq GTA5.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True)
    return [int(line.split('","')[1]) for line in r.stdout.splitlines() if line.startswith('"GTA5.exe"')]


def start(desk):
    from pyvda import AppView, VirtualDesktop
    if gta_pids():
        print("GTA is already running"); return
    # no BattlEye (it blocks ScriptHookV, so the game never shows a window) and no Rockstar sign-in
    subprocess.Popen([steam_exe(), "-applaunch", "271590", "-nobattleye", "-noBE", "-scOffline"])
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
        pids = gta_pids()
        game = [h for h in game_windows() if os.path.basename(exe_of(_pid_of(h))).lower() == "gta5.exe"]
        if pids and game and not landed:
            # the landing page: the game sits at ~1.1 GB until story mode is picked (loading it takes 2 GB and more)
            mem = max(gta_memory_mb(p) for p in pids)
            if mem > 1800: landed = True; print(f"story mode loading ({mem} MB)", flush=True)
            elif low_since is None: low_since = time.time()
            elif time.time() - low_since > 40 and idle_seconds() >= 60:
                click_story_mode(game[0])
                low_since = time.time()
        if landed and time.time() - t0 > 60: break
        time.sleep(0.25)
    print("camera client started" if gta_pids() else "GTA did not start")


def stop():
    pids = gta_pids()
    for pid in pids:
        subprocess.run(["taskkill", "/PID", str(pid)], capture_output=True)
    time.sleep(5)
    for pid in gta_pids():
        subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True)
    print(f"stopped {len(pids)} GTA process(es)")


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    desk = int(sys.argv[sys.argv.index("--desk") + 1]) if "--desk" in sys.argv else 2
    if cmd == "start": start(desk)
    elif cmd == "stop": stop()
    else: print("running" if gta_pids() else "not running")
