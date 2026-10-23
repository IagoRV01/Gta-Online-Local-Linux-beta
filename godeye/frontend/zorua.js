// GodEye's mascot: Zorua, community sprites from PMDCollab SpriteCollab (CC BY-NC 4.0; artists in zorua/CREDITS.md).
// They are fetched by tools/zorua/fetch_zorua.py and never committed; without them every helper here returns nothing
// and the page keeps its plain eye. Each moment of the page has the expression that fits it.
"use strict";

let zorua = null;   // zorua/manifest.json
const MOOD = {
  login: "Normal", loginBad: "Angry", welcome: "Happy",
  hack: "Determined", hackLow: "Worried", granted: "Joyous", denied: "Pain", slow: "Dizzy",
  credits: "Sad", cooldown: "Sigh", offline: "Crying", error: "Stunned", aborted: "Sigh", ok: "Happy",
};
const ROLE_ZORUA = { HACKER: "Hisui-Determined", DISPATCH: "Shouting", OPERATOR: "Inspired", ADMIN: "Shiny-Normal" };
const EVENT_ZORUA = { storm: "Surprised", gridlock: "Stunned", manhunt: "Shouting", happyhour: "Joyous" };

// the <img> for a portrait (40×40 pixel art), or "" without the sprites
function zImg(name, cls = "zport") {
  return zorua && zorua.portraits.includes(name) ? `<img class="${cls}" src="zorua/${name}.png" alt="" width="40" height="40">` : "";
}
const zMood = (mood, cls) => zImg(MOOD[mood] ?? mood, cls);
const zRole = (role) => zImg(ROLE_ZORUA[role], "zrole");
const zEvent = (key) => zImg(EVENT_ZORUA[key], "zport zsmall");

// put a portrait into an <img> slot (hidden without the sprites)
function zSet(id, name) {
  const el = $(id);
  if (!el) return;
  if (!zorua) { el.hidden = true; return; }
  el.src = `zorua/${MOOD[name] ?? name}.png`;
  el.hidden = false;
}

// the header mark: Zorua idling while the game server is connected, asleep when it is not
function brand() {
  const el = $("zbrand"), idle = zorua.idle, sleep = zorua.sleep;
  el.hidden = false;
  document.querySelector(".brand .eye").hidden = true;
  let frame = 0, left = 0, mode = "";
  setInterval(() => {   // 60 ticks a second, as the sprite's frame durations count them
    const awake = $("dot").classList.contains("on");
    const a = awake ? idle : sleep;
    if ((awake ? "idle" : "sleep") !== mode) {
      mode = awake ? "idle" : "sleep";
      el.className = "zbrand " + mode;
      el.style.width = a.w + "px";
      el.style.height = Math.min(a.h, 24) + "px";   // Zorua stands in the top 24 px of each frame
      el.style.backgroundImage = `url("zorua/${awake ? "Idle" : "Sleep"}-Anim.png")`;
      frame = 0; left = 0;
    }
    if (--left > 0) return;
    frame = (frame + 1) % a.durations.length;
    left = a.durations[frame];
    el.style.backgroundPosition = `-${frame * a.w}px 0`;   // row 0: facing the viewer
  }, 1000 / 60);
}

(async () => {
  try {
    const r = await fetch("zorua/manifest.json");
    if (!r.ok) return;
    zorua = await r.json();
  } catch { return; }
  const icon = document.createElement("link");
  icon.rel = "icon"; icon.href = "zorua/Normal.png";
  document.head.appendChild(icon);
  brand();
  zSet("loginZ", "login");
  zSet("hackZ", "hack");
  $("zcredits").hidden = false;
  if (typeof renderAuth === "function" && auth.user) renderAuth();
  if (typeof onGameEvent === "function") onGameEvent(gameEvent);
})();
