<script>
// K-Line test card on the system tab (board build only). Talks to the box's /api/* (read-only requests).
(function () {
  const $ = id => document.getElementById(id);
  const L = (th, en) => document.documentElement.lang === "en" ? en : th;   // page language (TH / EN)
  if (!$("kl-card")) return;
  let prev = null, timer = null;
  const j = u => fetch(u, { cache: "no-store" }).then(r => r.json());
  function showLog(t) { const l = $("kl-log"); l.textContent = t || ""; l.scrollTop = l.scrollHeight; }
  function st(a) { const e = $("kl-st"); e.textContent = a ? L("ตื่นแล้ว ✓", "Awake ✓") : L("ยังไม่ตื่น", "Not awake"); e.style.color = a ? "var(--ok)" : ""; }
  function busy(on) { document.querySelectorAll("#kl-card button").forEach(b => { if (b.id !== "kl-live") b.disabled = on; }); }
  async function status() {
    try {
      const d = await j("/api/status"), e = $("kl-line");
      e.textContent = d.line >= 90 ? L("มีไฟรอ ✓", "Idle high ✓") : d.line <= 10 ? L("ไม่มีไฟ ✗", "No voltage ✗") : L("แกว่ง ", "Unstable ") + d.line + "%";
      e.style.color = d.line >= 90 ? "var(--ok)" : d.line <= 10 ? "var(--crit)" : "var(--warn)";
      $("kl-line").title = d.line <= 10 ? L("เช็คสายเหลือง, 12V ของโมดูล และ GND", "Check the yellow wire, the module 12 V and GND") : "";
      st(d.awake); showLog(d.log);
    } catch (e) { $("kl-line").textContent = L("ต่อบอร์ดไม่ได้", "Cannot reach the board"); }
  }
  async function act(u) { busy(true); try { const d = await j(u); st(d.awake); showLog(d.log); } catch (e) {} busy(false); }
  function table(a) {
    let h = "";
    for (let i = 0; i < a.length; i++) {
      const ch = prev && prev.length === a.length && prev[i] !== a[i];
      const c = ch ? ' style="background:color-mix(in srgb,var(--warn) 30%,transparent)"' : "";
      h += `<tr${c}><td>${i}</td><td>${a[i].toString(16).toUpperCase().padStart(2, "0")}</td><td>${a[i]}</td><td>${a[i] - 40}</td><td>${i + 1 < a.length ? a[i] * 256 + a[i + 1] : ""}</td></tr>`;
    }
    $("kl-dt").innerHTML = h; prev = a;
    $("kl-guess").textContent = a.length > 1 ? L("เดา: รอบเครื่องอาจเป็นคู่ไบต์ 0–1 = ", "Guess: rpm may be bytes 0–1 = ") + (a[0] * 256 + a[1]) + L(" rpm (ต้องยืนยัน)", " rpm (to be confirmed)") : "";
  }
  async function readOnce() {
    try { const d = await j("/api/read?t=" + $("kl-tbl").value); st(d.awake); showLog(d.log); if (d.ok) table(d.data); } catch (e) {}
  }
  $("kl-auto").onclick = () => act("/api/wake?mode=auto");
  $("kl-mod").onclick = () => act("/api/wake?mode=module");
  $("kl-npn").onclick = () => act("/api/wake?mode=npn");
  $("kl-check").onclick = status;
  $("kl-clear").onclick = () => act("/api/clear");
  $("kl-read").onclick = readOnce;
  $("kl-tbl").onchange = () => { prev = null; };
  $("kl-live").onclick = () => {
    const b = $("kl-live");
    if (timer) { clearInterval(timer); timer = null; b.textContent = L("อ่านต่อเนื่อง", "Read continuously"); return; }
    b.textContent = L("หยุดอ่าน", "Stop reading"); timer = setInterval(readOnce, 350);
  };
  $("kl-copy").onclick = async () => {
    let t = ""; try { t = await (await fetch("/api/log", { cache: "no-store" })).text(); } catch (e) { t = $("kl-log").textContent; }
    const ta = $("kl-ta"); ta.value = t; ta.select();
    let ok = false; try { ok = document.execCommand("copy"); } catch (e) {}
    alert(ok ? L("คัดลอกแล้ว ไปวางในแชทได้เลย", "Copied, paste it into the chat") : L("คัดลอกไม่ได้ กดค้างที่ข้อความในกล่องบันทึกแล้วเลือกคัดลอก", "Could not copy. Long-press the log text and choose Copy"));
  };
  // Shown only when the board has tried to wake the ECU and it still isn't answering.
  // Once shown it stays for this visit, so a successful wake from the card doesn't hide what you're testing.
  async function poll() {
    try {
      const d = await j("/api/state");
      if (!d.awake && d.tried && $("kl-card").hidden) { $("kl-card").hidden = false; status(); }
      if (!$("kl-card").hidden) st(d.awake);
    } catch (e) {}
  }
  // "add a bike model": full table scan (read requests only), result copied for the developer
  if ($("scan-card")) {
    const M = { 0x17: "Wave 110i / 125i", 0x67: "0x67 · ทดลอง", 0x10: "0x10 · ทดลอง", 0x11: "0x11 · ทดลอง", 0x13: "0x13 · ทดลอง", 0x60: "0x60 · ทดลอง", 0x61: "0x61 · ทดลอง", 0x63: "0x63 · ทดลอง" };
    setInterval(() => { const E = window.ECU; $("scan-tbl").textContent = E && E.on && E.lt ? "0x" + E.lt.toString(16).toUpperCase().padStart(2, "0") + " (" + (M[E.lt] || "?") + ")" : "–"; }, 2000);
    $("scan-go").onclick = async () => {
      const b = $("scan-go"), out = $("scan-out"); b.disabled = true; b.textContent = L("กำลังสแกน… 30–60 วินาที", "Scanning… 30–60 s");
      out.hidden = false; out.textContent = "";
      try { out.textContent = await (await fetch("/api/scan", { cache: "no-store" })).text(); $("scan-copy").disabled = false; }
      catch (e) { out.textContent = L("สแกนไม่สำเร็จ ลองอีกครั้ง", "Scan failed, try again"); }
      b.disabled = false; b.textContent = L("สแกนอีกครั้ง", "Scan again");
    };
    $("scan-copy").onclick = () => {
      const ta = $("kl-ta"); ta.value = $("scan-out").textContent; ta.select();
      let ok = false; try { ok = document.execCommand("copy"); } catch (e) {}
      alert(ok ? L("คัดลอกแล้ว ส่งให้ผู้พัฒนาได้เลย", "Copied, send it to the developer") : L("คัดลอกไม่ได้ กดค้างที่ข้อความเพื่อคัดลอกเอง", "Could not copy: long-press the text to copy it"));
    };
  }
  poll(); setInterval(poll, 3000);
})();
</script>
