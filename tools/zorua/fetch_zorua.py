"""GodEye's Zorua art: community sprites from PMDCollab SpriteCollab (https://github.com/PMDCollab/SpriteCollab).

  python tools/zorua/fetch_zorua.py

The sprites are CC BY-NC 4.0 by their artists (CREDITS.md, written next to them). Zorua itself belongs to Nintendo /
Game Freak, so this repository does not carry the files: this script downloads them, pinned to one SpriteCollab commit
and checked against known SHA-256 sums, into godeye/frontend/zorua (git-ignored). GodEye uses them when they are there
and its plain eye when not. With Pillow installed it also makes the launcher's icons (godeye/launcher/zorua*.ico);
rebuild GodEye.exe afterwards (./build.sh godeye).
"""
import hashlib, json, os, sys, urllib.request
import xml.etree.ElementTree as ET

COMMIT = "0dc6643393a57c6d6eddd024d002e679585a2249"
BASE = f"https://raw.githubusercontent.com/PMDCollab/SpriteCollab/{COMMIT}/"
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "godeye", "frontend", "zorua")
LAUNCHER = os.path.join(ROOT, "godeye", "launcher")

PORTRAITS = ["Normal", "Happy", "Joyous", "Inspired", "Determined", "Worried", "Surprised", "Shouting", "Sigh", "Sad",
             "Pain", "Dizzy", "Stunned", "Angry", "Crying"]
FILES = {f"{p}.png": f"portrait/0570/{p}.png" for p in PORTRAITS}
FILES.update({
    "Hisui-Determined.png": "portrait/0570/0001/Determined.png",
    "Hisui-Normal.png": "portrait/0570/0001/Normal.png",
    "Shiny-Normal.png": "portrait/0570/0000/0001/Normal.png",
    "Idle-Anim.png": "sprite/0570/Idle-Anim.png",
    "Sleep-Anim.png": "sprite/0570/Sleep-Anim.png",
    "AnimData.xml": "sprite/0570/AnimData.xml",
    "credits-portrait.txt": "portrait/0570/credits.txt",
    "credits-portrait-hisui.txt": "portrait/0570/0001/credits.txt",
    "credits-portrait-shiny.txt": "portrait/0570/0000/0001/credits.txt",
    "credits-sprite.txt": "sprite/0570/credits.txt",
})
SHA256 = {   # the pinned commit's files; a mismatch stops the script
    "Normal.png": "1d9a18291532983c46b7bc5a937445c79abfccdd5873aa716f0b950ece9e462e",
    "Happy.png": "6c90fde37da5accfb68bc786b468129113726c38360bd831846124ed7dfc0a57",
    "Joyous.png": "30558df122194fb2ec7aeacc670bb3cb83b5e3d8faa106c2ce945c9b58f42720",
    "Inspired.png": "94b42e1d530e4fde787432153c0d09bbee90855b137a5e80e2c7b975aa005482",
    "Determined.png": "7dde7614d5a542fe4db80770b89ec7956447000758a38ddd6774a4f64599c267",
    "Worried.png": "d4fda70baa268eb407b7bd7b74bb51fc113783d9274e36e0db7b72e1e73b637c",
    "Surprised.png": "b2cce85bc043f2725ae74386a98f9a4d7422204303b82f60f4c0b5b2ba049f53",
    "Shouting.png": "ba9781e8d0a9f3a974f8614d5a4479261028679c6954224d8db340963d291a43",
    "Sigh.png": "770cdb281366a6691aef7233b03acf11e0ddfe956a656b2bc16072d60fa0fb6a",
    "Sad.png": "1d05b159128a8186d8736ff86883e3d5c52adb7f6f60951a9b79b0a2afc87862",
    "Pain.png": "77869d396046118e0c5c861738cfc9c521e47247b609d1bd73f2291903a2f161",
    "Dizzy.png": "510c6cd5c1fc17f7bd980b1755b4376a2e72be2503e64f34f7e95a9f5a40b69f",
    "Stunned.png": "41fb8a301cc5442110cb6f05f97e22329d8c942aceafd0e2b3b763440113fa50",
    "Angry.png": "a54f819fe8086b35451b11a7c36f39b9b89e5d252ad220505eae700f01a9ba32",
    "Crying.png": "f929b2801ad60f52dd16725ecda03be40a2a840e05025503678872ad99c124f7",
    "Hisui-Determined.png": "0b1d6c7abb0a602d6b3fb8e09d1cc6ef185202a152a56993517e3c35fdb5facf",
    "Hisui-Normal.png": "026d8db4abb80436a824a7ba97d5db9e8756cd220a6231189449f1985cf4d47b",
    "Shiny-Normal.png": "5140a3e0aac362cc38f65b820a201b60044cac23b30885435a3ec1c13ffede4f",
    "Idle-Anim.png": "cae62b440c57cd5e3352bcbb54505ecc5bac29b93b8a1c29675cfe929d7bd72c",
    "Sleep-Anim.png": "ebf10dc515171f5aeed7c452dd6b098bdf44a8aabdbef97143bebbe22f4c5562",
    "AnimData.xml": "03fcbd859355d6a441f56b8817c1e827d9af183d8f5f419e1bb771ab0cad944e",
    "credits-portrait.txt": "c67be8b776e5c4299a8a601d667596c5cada76a54509f4c6b863fc5dd3472cb2",
    "credits-portrait-hisui.txt": "57282990ab42a1f4019966a8bb146e73a59007f1fac0dce47bd153601c6c35f1",
    "credits-portrait-shiny.txt": "068ca3ca084176fb708d62ebed5ed9bbfe6dfbcabd15c1522e48e792088c48dc",
    "credits-sprite.txt": "6b3584d1c1fae19ce026f3f7eabe1217dfdd19c806e1caa348f2df4e67e6647f",
}

# SpriteCollab credits name artists by Discord id (credit_names.txt in that repository)
ARTISTS = {
    "544245909639397378": "baronessfaron",
    "356635814668664832": "Emmuffin (https://twitter.com/Ernmuffin)",
    "207815740399681538": "MilesFarber (https://github.com/MilesFarber)",
    "237286997645983744": "an earlier SpriteCollab contributor",
}


def fetch(name, path):
    with urllib.request.urlopen(BASE + path, timeout=30) as r:
        data = r.read(512 * 1024 + 1)
    if len(data) > 512 * 1024:
        sys.exit(f"{path}: larger than expected, stopping")
    want = SHA256.get(name)
    got = hashlib.sha256(data).hexdigest()
    if want and want != got:
        sys.exit(f"{path}: SHA-256 {got} is not the pinned {want}, stopping")
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(data)
    return got


def anim(xml_path, name):
    """frame size and per-frame durations (in 1/60 s) of one animation in AnimData.xml"""
    root = ET.parse(xml_path).getroot()
    for a in root.iter("Anim"):
        if a.findtext("Name") == name:
            return {"w": int(a.findtext("FrameWidth")), "h": int(a.findtext("FrameHeight")),
                    "durations": [int(d.text) for d in a.iter("Duration")]}
    sys.exit(f"AnimData.xml has no {name} animation")


def credits():
    lines = ["# Zorua art in GodEye", "",
             "Community sprites from [PMDCollab SpriteCollab](https://github.com/PMDCollab/SpriteCollab) "
             f"(commit `{COMMIT[:7]}`), licensed [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/). "
             "Zorua is © Nintendo / Creatures / GAME FREAK; GodEye is a non-commercial fan project.", ""]
    for title, fn in [("Portraits", "credits-portrait.txt"), ("Hisuian Zorua portraits", "credits-portrait-hisui.txt"),
                      ("Shiny Zorua portraits", "credits-portrait-shiny.txt"), ("Sprites (Idle, Sleep)", "credits-sprite.txt")]:
        who = []
        for row in open(os.path.join(OUT, fn), encoding="utf-8"):
            parts = row.rstrip("\n").split("\t")
            if len(parts) >= 3 and parts[2] == "CUR":
                n = ARTISTS.get(parts[1].strip("<@!>"), parts[1])
                if n not in who:
                    who.append(n)
        lines.append(f"- **{title}:** {', '.join(who)}")
    open(os.path.join(OUT, "CREDITS.md"), "w", encoding="utf-8").write("\n".join(lines) + "\n")


def icons():
    try:
        from PIL import Image
    except ImportError:
        print("Pillow not installed: no launcher icons (pip install pillow, then run this again)")
        return
    for src, dst in [("Normal.png", "zorua.ico"), ("Worried.png", "zorua-error.ico")]:
        im = Image.open(os.path.join(OUT, src)).convert("RGBA")   # 40×40 pixel art: scale by whole numbers only
        sizes = []
        for size in (16, 24, 32, 48, 64, 128, 256):
            k = max(1, size // im.width)
            big = im.resize((im.width * k, im.height * k), Image.NEAREST) if size >= im.width else im.resize((size, size), Image.LANCZOS)
            canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
            canvas.paste(big, ((size - big.width) // 2, (size - big.height) // 2))
            sizes.append(canvas)
        sizes[-1].save(os.path.join(LAUNCHER, dst), sizes=[(s.width, s.height) for s in sizes], append_images=sizes[:-1])
        print(f"made godeye/launcher/{dst}")


def main():
    os.makedirs(OUT, exist_ok=True)
    sums = {name: fetch(name, path) for name, path in FILES.items()}
    print(f"{len(sums)} files in {OUT}")
    manifest = {"commit": COMMIT, "portraits": PORTRAITS + ["Hisui-Determined", "Hisui-Normal", "Shiny-Normal"],
                "idle": anim(os.path.join(OUT, "AnimData.xml"), "Idle"), "sleep": anim(os.path.join(OUT, "AnimData.xml"), "Sleep")}
    json.dump(manifest, open(os.path.join(OUT, "manifest.json"), "w"), indent=1)
    credits()
    icons()
    if "--sums" in sys.argv:
        print(json.dumps(sums, indent=1))


if __name__ == "__main__":
    main()
