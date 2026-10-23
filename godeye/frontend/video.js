// GodEye live video (Phase 13): the camera clients' picture. On the LAN, WebRTC (WHEP, about 0.2 s behind the game);
// on the public address, or when WebRTC cannot connect, low-latency HLS (a few seconds behind). Both come from GodEye's
// own origin (/video/<stream>/...), relayed to mediamtx by the backend.
"use strict";

let streams = [], current = null, pc = null, hls = null, whepSession = null, playToken = 0;

const onLan = () => /^(localhost|127\.|10\.|192\.168\.|172\.(1[6-9]|2\d|3[01])\.|\[?::1\]?$)/.test(location.hostname);

async function loadStreams() {
  try { streams = await (await fetch("api/video", { cache: "no-cache" })).json(); } catch { streams = []; }
  $("liveBtn").hidden = !streams.length;
  const pick = $("vPick"), keep = pick.value;
  pick.innerHTML = streams.map((s) => `<option value="${esc(s.path)}">${esc(s.name)}${s.live ? "" : " (offline)"}</option>`).join("");
  if ([...pick.options].some((o) => o.value === keep)) pick.value = keep;
  pick.hidden = streams.length < 2;
}

function vstate(text) { $("vState").textContent = text; $("vState").hidden = !text; }

async function stopVideo() {
  playToken++;
  if (hls) { hls.destroy(); hls = null; }
  if (pc) { pc.close(); pc = null; }
  if (whepSession) { fetch(whepSession, { method: "DELETE" }).catch(() => {}); whepSession = null; }
  const v = $("vid");
  v.srcObject = null;
  v.removeAttribute("src");
  v.load();
}

// WebRTC through WHEP: an offer with every ICE candidate, mediamtx's answer back; true once media flows
async function playWhep(path, token) {
  pc = new RTCPeerConnection();
  const mine = pc;
  pc.addTransceiver("video", { direction: "recvonly" });
  pc.ontrack = (e) => { $("vid").srcObject = e.streams[0] || new MediaStream([e.track]); };
  await pc.setLocalDescription(await pc.createOffer());
  await new Promise((done) => {   // gather the candidates (at most 2 s)
    if (pc.iceGatheringState === "complete") return done();
    const t = setTimeout(done, 2000);
    pc.addEventListener("icegatheringstatechange", () => { if (pc?.iceGatheringState === "complete") { clearTimeout(t); done(); } });
  });
  if (token !== playToken) return false;
  const r = await fetch(`video/${path}/whep`, { method: "POST", headers: { "Content-Type": "application/sdp" }, body: pc.localDescription.sdp });
  if (!r.ok) return false;
  whepSession = r.headers.get("location");
  await pc.setRemoteDescription({ type: "answer", sdp: await r.text() });
  return await new Promise((done) => {
    const t = setTimeout(() => done(false), 6000);
    const check = () => {
      if (mine.connectionState === "connected") { clearTimeout(t); done(true); }
      if (mine.connectionState === "failed") { clearTimeout(t); done(false); }
    };
    mine.addEventListener("connectionstatechange", check);
    check();
  });
}

function loadScript(src) {
  return new Promise((ok, fail) => { const s = document.createElement("script"); s.src = src; s.onload = ok; s.onerror = fail; document.head.appendChild(s); });
}

async function playHls(path, token) {
  const v = $("vid"), url = `video/${path}/index.m3u8`;
  if (v.canPlayType("application/vnd.apple.mpegurl")) { v.src = url; return true; }   // Safari, iOS, Android
  if (!window.Hls) await loadScript(`video/${path}/hls.min.js`);   // hls.js, as mediamtx ships it
  if (token !== playToken || !window.Hls?.isSupported()) return false;
  hls = new window.Hls({ lowLatencyMode: true, liveSyncDurationCount: 2 });
  hls.loadSource(url);
  hls.attachMedia(v);
  return true;
}

async function playVideo(path) {
  await stopVideo();
  const token = playToken;
  current = path;
  const s = streams.find((x) => x.path === path);
  vstate(s && !s.live ? "The camera is not on right now." : "Connecting…");
  let ok = false;
  if (onLan()) {
    try { ok = await playWhep(path, token); } catch { ok = false; }
    if (token !== playToken) return;
    if (ok) { vstate(""); $("vMode").textContent = "WebRTC"; return; }
    if (pc) { pc.close(); pc = null; }
  }
  try { ok = await playHls(path, token); } catch { ok = false; }
  if (token !== playToken) return;
  $("vMode").textContent = ok ? "HLS" : "";
  if (ok) vstate(""); else vstate("No video: the camera is not streaming.");
}

function cameraLabel() {   // what the camera shows, from the camera telemetry (control.js)
  const s = streams.find((x) => x.path === current);
  const c = typeof camList !== "undefined" ? camList.find((x) => x.name === s?.name) : null;
  return c ? c.label : s?.name ?? "";
}

$("liveBtn").onclick = async () => {
  const box = $("videoBox");
  box.hidden = !box.hidden;
  $("liveBtn").classList.toggle("on", !box.hidden);
  if (box.hidden) { await stopVideo(); return; }
  await loadStreams();
  if (streams.length) playVideo($("vPick").value || streams[0].path);
};
$("vClose").onclick = () => $("liveBtn").click();
$("vPick").onchange = () => playVideo($("vPick").value);
$("vid").addEventListener("playing", () => vstate(""));
setInterval(() => { $("vTitle").textContent = cameraLabel(); }, 1000);
loadStreams();
setInterval(loadStreams, 15000);
