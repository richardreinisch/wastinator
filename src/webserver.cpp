#include "webserver.h"
#include "storage.h"
#include <Arduino.h>

static WebServer server(80);
static AppConfig* _cfg = nullptr;
static bool _running    = false;

// ─── EMBEDDED HTML ────────────────────────────────────────────────────────────
static const char HTML_PAGE[] = R"HTMLEOF(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>🗑 Wastinator</title>
<style>
:root{
  --bg:#0f1117;--card:#181c27;--border:#252d42;--accent:#6ee7b7;
  --text:#e2e8f0;--muted:#64748b;--radius:14px;--danger:#f87171;
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:system-ui,sans-serif;
  min-height:100vh;padding:16px 16px 60px}

/* ── Header ── */
.header{text-align:center;padding:20px 0 6px}
.logo{font-size:2.4rem;font-weight:900;letter-spacing:-2px;
  background:linear-gradient(135deg,#6ee7b7,#3b82f6);
  -webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text}
.tagline{color:var(--muted);font-size:.8rem;margin-top:2px;margin-bottom:24px}

/* ── Warning banner ── */
#timeWarning{display:none;background:#431407;border:1px solid #c2410c;
  border-radius:var(--radius);padding:14px 18px;margin-bottom:16px;
  align-items:center;gap:12px}
.warn-icon{font-size:1.5rem;flex-shrink:0}
.warn-title{color:#fed7aa;font-weight:700;font-size:.95rem}
.warn-body{color:#fdba74;font-size:.82rem;margin-top:3px}

/* ── Panel ── */
.panel{background:var(--card);border:1px solid var(--border);
  border-radius:var(--radius);padding:14px 18px;margin-bottom:16px}
.panel-title{font-size:.7rem;font-weight:700;text-transform:uppercase;
  letter-spacing:.08em;color:var(--muted);margin-bottom:12px}

/* ── Toggles ── */
.row{display:flex;flex-wrap:wrap;gap:14px;align-items:center}
label.tog{display:flex;align-items:center;gap:8px;cursor:pointer;font-size:.88rem}
.toggle{appearance:none;width:42px;height:22px;background:#374151;
  border-radius:11px;cursor:pointer;position:relative;transition:background .2s;flex-shrink:0}
.toggle:checked{background:#10b981}
.toggle::after{content:'';position:absolute;top:3px;left:3px;width:16px;height:16px;
  background:#fff;border-radius:50%;transition:left .2s}
.toggle:checked::after{left:23px}

/* ── Buttons ── */
.btn{border:none;border-radius:8px;padding:7px 18px;font-size:.85rem;
  font-weight:700;cursor:pointer;transition:opacity .15s}
.btn:hover{opacity:.82}
.btn-green{background:#10b981;color:#fff}
.btn-blue{background:#3b82f6;color:#fff}
.btn-sm{padding:5px 12px;font-size:.8rem}

/* ── Time bar ── */
.timebar{display:flex;flex-wrap:wrap;align-items:center;gap:10px}
.timebar input[type=datetime-local]{background:#1e2a42;border:1px solid var(--border);
  color:var(--text);border-radius:8px;padding:6px 10px;font-size:.82rem}
#currentTime{margin-left:auto;font-size:.8rem;color:var(--muted)}

/* ── Bin Grid ── */
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(290px,1fr));
  gap:18px;margin-bottom:8px}
.bin-card{background:var(--card);border:1px solid var(--border);
  border-radius:var(--radius);overflow:hidden}
.bin-header{display:flex;align-items:center;gap:10px;padding:12px 16px;
  border-bottom:1px solid var(--border)}
.color-dot{width:18px;height:18px;border-radius:50%;flex-shrink:0;
  border:2px solid rgba(255,255,255,.15)}
.bin-name-input{background:transparent;border:none;color:var(--text);
  font-weight:700;font-size:.98rem;flex:1;outline:none;min-width:0}
.bin-name-input:focus{border-bottom:1px solid var(--accent)}

/* Color picker row */
.color-row{display:flex;gap:6px;flex-wrap:wrap;padding:10px 16px 4px;
  border-bottom:1px solid var(--border)}
.color-swatch{width:24px;height:24px;border-radius:50%;cursor:pointer;
  border:2px solid transparent;transition:transform .15s,border-color .15s;flex-shrink:0}
.color-swatch:hover{transform:scale(1.18)}
.color-swatch.selected{border-color:#fff;transform:scale(1.15)}

/* Pickup list */
.bin-body{padding:10px 16px 14px}
.pickup-list{list-style:none;margin-bottom:8px;max-height:200px;overflow-y:auto}
.pickup-list li{display:flex;align-items:center;justify-content:space-between;
  padding:5px 8px;border-radius:6px;font-size:.84rem;margin-bottom:2px;
  background:#0d1425}
.del-btn{background:none;border:none;color:var(--danger);cursor:pointer;
  font-size:.95rem;padding:2px 6px;border-radius:4px;line-height:1}
.del-btn:hover{background:#7f1d1d44}
.add-row{display:flex;gap:8px;margin-top:6px}
.add-row input{flex:1;background:#1e2a42;border:1px solid var(--border);
  color:var(--text);border-radius:8px;padding:6px 10px;font-size:.82rem;min-width:0}
.add-btn{border:none;color:#fff;border-radius:8px;padding:6px 14px;
  font-size:.82rem;font-weight:700;cursor:pointer;white-space:nowrap;
  transition:opacity .15s}
.add-btn:hover{opacity:.8}
.empty{color:var(--muted);font-size:.8rem;padding:5px 0;text-align:center}

/* Toast */
#toast{position:fixed;bottom:20px;left:50%;
  transform:translateX(-50%) translateY(80px);
  background:#10b981;color:#fff;padding:9px 22px;border-radius:32px;
  font-weight:700;font-size:.88rem;transition:transform .3s;
  pointer-events:none;z-index:999;white-space:nowrap}
#toast.show{transform:translateX(-50%) translateY(0)}

/* ── Upcoming panel ── */
#upcoming{margin-bottom:16px}
.up-title{font-size:.7rem;font-weight:700;text-transform:uppercase;
  letter-spacing:.08em;color:var(--muted);margin-bottom:10px}
.up-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:10px}
.up-item{background:#0d1425;border-radius:10px;padding:10px 14px;
  display:flex;align-items:center;gap:10px}
.up-dot{width:12px;height:12px;border-radius:50%;flex-shrink:0}
.up-info{min-width:0}
.up-name{font-size:.82rem;font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.up-date{font-size:.78rem;color:var(--muted);margin-top:1px}
.up-soon{color:#fbbf24}   /* within 7 days  */
.up-today{color:#f87171}  /* tomorrow = alarm day */
.up-empty{color:var(--muted);font-size:.8rem;font-style:italic}
.up-next-badge{display:inline-block;background:#6ee7b733;color:#6ee7b7;
  font-size:.65rem;font-weight:700;padding:1px 6px;border-radius:4px;
  margin-left:6px;vertical-align:middle;white-space:nowrap}

footer{text-align:center;color:var(--muted);font-size:.72rem;margin-top:16px}
</style>
</head><body>

<div class="header">
  <div class="logo">🗑 WASTINATOR</div>
  <p class="tagline">Waste Collection Reminder &bull; ESP32 WROOM-32</p>
</div>

<!-- Time warning -->
<div id="timeWarning">
  <span class="warn-icon">⚠️</span>
  <div>
    <div class="warn-title">Clock not set — WiFi started automatically!</div>
    <div class="warn-body">After a power outage the clock must be reset for alerts to work.
      Enter the current date &amp; time below and press <strong>Set time</strong>.
      WiFi will remain active until you turn it off (hold button 10s).</div>
  </div>
</div>

<!-- Settings -->
<div class="panel">
  <div class="panel-title">⚙️ Settings</div>
  <div class="row">
    <label class="tog"><input type="checkbox" class="toggle" id="chkBuzzer"> Buzzer alerts</label>
    <label class="tog"><input type="checkbox" class="toggle" id="chkRepeat"> Repeat notifications</label>
    <button class="btn btn-green btn-sm" style="margin-left:auto" onclick="saveSettings()">💾 Save settings</button>
  </div>
</div>

<!-- Clock -->
<div class="panel">
  <div class="panel-title">🕐 Controller Clock</div>
  <div class="timebar">
    <input type="datetime-local" id="dtInput">
    <button class="btn btn-blue btn-sm" onclick="setTime()">Set time</button>
    <span id="currentTime"></span>
  </div>
</div>

<!-- Upcoming pickups summary -->
<div class="panel" id="upcoming">
  <div class="up-title">📅 Upcoming Pickups</div>
  <div class="up-grid" id="upGrid"><div class="up-empty">Loading…</div></div>
</div>

<!-- Bin cards -->
<div class="grid" id="grid"></div>

<div id="toast"></div>
<footer>Wastinator &bull; ESP32 WROOM-32 &bull; No internet required</footer>

<script>
// ── Color palette (must match firmware COLOR_HEX[]) ──────────────────────────
const COLORS=[
  {name:'Black',  hex:'#1f2937'},
  {name:'White',  hex:'#f1f5f9'},
  {name:'Brown',  hex:'#92400e'},
  {name:'Yellow', hex:'#eab308'},
  {name:'Green',  hex:'#16a34a'},
  {name:'Red',    hex:'#ef4444'},
  {name:'Blue',   hex:'#3b82f6'},
  {name:'Orange', hex:'#f97316'}
];

// ── Default state ─────────────────────────────────────────────────────────────
let state={
  buzzerEnabled:false,
  repeatNotify:false,
  bins:[
    {name:'General Waste',         colorIndex:0, pickups:[]},
    {name:'Paper',                 colorIndex:5, pickups:[]},
    {name:'Lightweight Packaging', colorIndex:3, pickups:[]},
    {name:'Organic Waste',         colorIndex:2, pickups:[]}
  ]
};

// ── Load ──────────────────────────────────────────────────────────────────────
async function load(){
  try{
    const r=await fetch('/api/config');
    const d=await r.json();
    state=d;
  }catch(e){console.warn('Offline/demo mode')}
  document.getElementById('chkBuzzer').checked=!!state.buzzerEnabled;
  document.getElementById('chkRepeat').checked=!!state.repeatNotify;
  const n=new Date();
  n.setMinutes(n.getMinutes()-n.getTimezoneOffset());
  document.getElementById('dtInput').value=n.toISOString().slice(0,16);
  render();
}

// ── Render ────────────────────────────────────────────────────────────────────
function render(){
  const grid=document.getElementById('grid');
  grid.innerHTML='';
  state.bins.forEach((bin,bi)=>{
    const color=COLORS[bin.colorIndex]||COLORS[0];
    const sorted=(bin.pickups||[]).slice().sort();
    const items=sorted.length
      ? sorted.map((d,i)=>`<li>
          <span>${fmtDate(d)}</span>
          <button class="del-btn" onclick="delPickup(${bi},${i})" title="Remove">✕</button>
        </li>`).join('')
      : '<li class="empty">No pickups scheduled</li>';

    // Color swatches
    const swatches=COLORS.map((c,ci)=>`
      <div class="color-swatch${ci===bin.colorIndex?' selected':''}"
        style="background:${c.hex}" title="${c.name}"
        onclick="setColor(${bi},${ci})"></div>`).join('');

    grid.innerHTML+=`
    <div class="bin-card">
      <div class="bin-header">
        <div class="color-dot" id="dot_${bi}" style="background:${color.hex}"></div>
        <input class="bin-name-input" id="name_${bi}" value="${esc(bin.name)}"
          maxlength="23" onchange="setBinName(${bi},this.value)"
          placeholder="Bin name">
      </div>
      <div class="color-row" id="cr_${bi}">${swatches}</div>
      <div class="bin-body">
        <ul class="pickup-list">${items}</ul>
        <div class="add-row">
          <input type="date" id="inp_${bi}">
          <button class="add-btn" style="background:${color.hex}"
            id="addbtn_${bi}" onclick="addPickup(${bi})">+ Add</button>
        </div>
      </div>
    </div>`;
  });
  renderUpcoming();
}

function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/"/g,'&quot;')}
function fmtDate(d){const[y,m,day]=d.split('-');return`${day}/${m}/${y}`}

// ── Upcoming pickups ─────────────────────────────────────────────────────────
function renderUpcoming(){
  const today=new Date(); today.setHours(0,0,0,0);
  const tomorrow=new Date(today); tomorrow.setDate(tomorrow.getDate()+1);

  // Build list: one entry per bin with its next upcoming pickup
  let entries=[];
  state.bins.forEach((bin,bi)=>{
    const color=COLORS[bin.colorIndex]||COLORS[0];
    const future=(bin.pickups||[])
      .filter(d=>new Date(d)>=today)
      .sort();
    if(future.length){
      const next=new Date(future[0]);
      const diffDays=Math.round((next-today)/(1000*60*60*24));
      entries.push({bi, bin, color, date:future[0], next, diffDays});
    }
  });

  // Sort all entries by date ascending
  entries.sort((a,b)=>a.date.localeCompare(b.date));

  const el=document.getElementById('upGrid');
  if(!entries.length){
    el.innerHTML='<div class="up-empty">No upcoming pickups scheduled.</div>';
    return;
  }

  // Find soonest date across all bins for the "next" badge
  const soonestDate=entries[0].date;

  el.innerHTML=entries.map(e=>{
    let dateLabel, urgencyClass='';
    if(e.diffDays===0)       { dateLabel='Today!';     urgencyClass='up-today'; }
    else if(e.diffDays===1)  { dateLabel='Tomorrow';   urgencyClass='up-today'; }
    else if(e.diffDays<=7)   { dateLabel=`in ${e.diffDays} days`; urgencyClass='up-soon'; }
    else                     { dateLabel=fmtDate(e.date); }
    const badge=e.date===soonestDate?'<span class="up-next-badge">NEXT</span>':'';
    return`<div class="up-item">
      <div class="up-dot" style="background:${e.color.hex}"></div>
      <div class="up-info">
        <div class="up-name">${esc(e.bin.name)}${badge}</div>
        <div class="up-date ${urgencyClass}">${dateLabel} &mdash; ${fmtDate(e.date)}</div>
      </div>
    </div>`;
  }).join('');
}

// ── Color picker ──────────────────────────────────────────────────────────────
function setColor(bi,ci){
  state.bins[bi].colorIndex=ci;
  render();
  saveAll();
}

// ── Bin name ──────────────────────────────────────────────────────────────────
function setBinName(bi,val){
  state.bins[bi].name=val.trim().slice(0,23)||'Bin '+(bi+1);
  // Don't re-render (input has focus), just save
  saveAll(false);
}

// ── Pickups ───────────────────────────────────────────────────────────────────
function addPickup(bi){
  const inp=document.getElementById('inp_'+bi);
  const val=inp.value;
  if(!val){showToast('Please select a date','#f59e0b');return}
  if(!state.bins[bi].pickups) state.bins[bi].pickups=[];
  if(state.bins[bi].pickups.includes(val)){showToast('Date already added','#f59e0b');return}
  state.bins[bi].pickups.push(val);
  inp.value='';
  render();
  saveAll();
}

function delPickup(bi,idx){
  const sorted=state.bins[bi].pickups.slice().sort();
  const val=sorted[idx];
  state.bins[bi].pickups=state.bins[bi].pickups.filter(d=>d!==val);
  render();
  saveAll();
}

// ── Settings ──────────────────────────────────────────────────────────────────
function saveSettings(){
  state.buzzerEnabled=document.getElementById('chkBuzzer').checked;
  state.repeatNotify=document.getElementById('chkRepeat').checked;
  saveAll();
}

// ── Save ──────────────────────────────────────────────────────────────────────
let saveTimer;
async function saveAll(showMsg=true){
  clearTimeout(saveTimer);
  saveTimer=setTimeout(async()=>{
    try{
      const r=await fetch('/api/config',{
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body:JSON.stringify(state)
      });
      if(showMsg) showToast('✅ Saved','#10b981');
    }catch(e){showToast('⚠️ Save failed','#ef4444')}
  },400);
}

// ── Clock ─────────────────────────────────────────────────────────────────────
async function setTime(){
  const val=document.getElementById('dtInput').value;
  if(!val) return;
  const ts=Math.floor(new Date(val).getTime()/1000);
  try{
    await fetch('/api/settime?ts='+ts);
    showToast('🕐 Time set!','#3b82f6');
    updateClock();
  }catch(e){showToast('⚠️ Error','#ef4444')}
}

async function updateClock(){
  try{
    const r=await fetch('/api/time');
    const d=await r.json();
    const warn=document.getElementById('timeWarning');
    if(d.ts===0){
      warn.style.display='flex';
      document.getElementById('currentTime').textContent='⚠️ Clock not set';
    }else{
      warn.style.display='none';
      const dt=new Date(d.ts*1000);
      document.getElementById('currentTime').textContent=
        dt.toLocaleString('en-GB');
    }
  }catch(e){}
}

// ── Toast ─────────────────────────────────────────────────────────────────────
let toastTimer;
function showToast(msg,color='#10b981'){
  const t=document.getElementById('toast');
  t.textContent=msg;t.style.background=color;
  t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer=setTimeout(()=>t.classList.remove('show'),2500);
}

load();
setInterval(updateClock,5000);
updateClock();
</script>
</body></html>
)HTMLEOF";

// ─── HELPERS ──────────────────────────────────────────────────────────────────

static bool parseDate(const String& s, uint16_t& yr, uint8_t& mo, uint8_t& dy) {
    if (s.length() != 10) return false;
    yr = s.substring(0,4).toInt();
    mo = s.substring(5,7).toInt();
    dy = s.substring(8,10).toInt();
    return (yr >= 2024 && mo >= 1 && mo <= 12 && dy >= 1 && dy <= 31);
}

static String pickupToStr(const Pickup& p) {
    if (!p.active) return "";
    char buf[12];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", p.year, p.month, p.day);
    return String(buf);
}

// ─── JSON BUILDER ─────────────────────────────────────────────────────────────
static String buildConfigJson() {
    String j = "{";
    j += "\"buzzerEnabled\":" + String(_cfg->buzzerEnabled ? "true" : "false") + ",";
    j += "\"repeatNotify\":"  + String(_cfg->repeatNotify  ? "true" : "false") + ",";
    j += "\"bins\":[";
    for (int b = 0; b < NUM_BINS; b++) {
        if (b) j += ",";
        j += "{";
        // Escape bin name
        j += "\"name\":\"";
        const char* nm = _cfg->bins[b].name;
        for (int i = 0; nm[i] && i < 23; i++) {
            char c = nm[i];
            if (c == '"') j += "\\\"";
            else if (c == '\\') j += "\\\\";
            else j += c;
        }
        j += "\",";
        j += "\"colorIndex\":" + String(_cfg->bins[b].colorIndex) + ",";
        j += "\"pickups\":[";
        bool first = true;
        for (int i = 0; i < MAX_PICKUPS_PER_BIN; i++) {
            const Pickup& p = _cfg->bins[b].pickups[i];
            if (p.active) {
                if (!first) j += ",";
                j += "\"" + pickupToStr(p) + "\"";
                first = false;
            }
        }
        j += "]}";
    }
    j += "]}";
    return j;
}

// ─── ROUTE HANDLERS ───────────────────────────────────────────────────────────
static void handleRoot() {
    server.send(200, "text/html", HTML_PAGE);
}

static void handleGetConfig() {
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", buildConfigJson());
}

static void handlePostConfig() {
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "No body"); return; }
    String body = server.arg("plain");

    // buzzerEnabled
    int idx = body.indexOf("\"buzzerEnabled\":");
    if (idx >= 0) _cfg->buzzerEnabled = body.substring(idx+16, idx+22).startsWith("true");

    // repeatNotify
    idx = body.indexOf("\"repeatNotify\":");
    if (idx >= 0) _cfg->repeatNotify = body.substring(idx+15, idx+21).startsWith("true");

    // bins array
    idx = body.indexOf("\"bins\":[");
    if (idx >= 0) {
        int pos = idx + 8;
        for (int b = 0; b < NUM_BINS; b++) {
            // Find opening brace for this bin
            int bstart = body.indexOf('{', pos);
            if (bstart < 0) break;

            // name
            int ni = body.indexOf("\"name\":", bstart);
            if (ni >= 0 && ni < bstart + 200) {
                int q1 = body.indexOf('"', ni + 7);
                int q2 = q1 >= 0 ? body.indexOf('"', q1 + 1) : -1;
                if (q1 >= 0 && q2 >= 0) {
                    String nm = body.substring(q1+1, q2);
                    nm.replace("\\\"", "\"");
                    nm.replace("\\\\", "\\");
                    nm = nm.substring(0, 23);
                    strncpy(_cfg->bins[b].name, nm.c_str(), sizeof(_cfg->bins[b].name)-1);
                    _cfg->bins[b].name[sizeof(_cfg->bins[b].name)-1] = '\0';
                }
            }

            // colorIndex
            int ci = body.indexOf("\"colorIndex\":", bstart);
            if (ci >= 0 && ci < bstart + 300) {
                int valStart = ci + 13;
                while (valStart < (int)body.length() && body[valStart] == ' ') valStart++;
                int valEnd = valStart;
                while (valEnd < (int)body.length() && isDigit(body[valEnd])) valEnd++;
                uint8_t cidx = body.substring(valStart, valEnd).toInt();
                if (cidx < NUM_COLORS) _cfg->bins[b].colorIndex = cidx;
            }

            // pickups array
            int pi = body.indexOf("\"pickups\":[", bstart);
            // Reset pickups for this bin
            for (int i = 0; i < MAX_PICKUPS_PER_BIN; i++) _cfg->bins[b].pickups[i].active = false;
            if (pi >= 0 && pi < bstart + 1000) {
                int arrStart = pi + 11;
                int arrEnd = body.indexOf("]", arrStart);
                if (arrEnd >= 0) {
                    String arr = body.substring(arrStart, arrEnd);
                    int apos = 0, cnt = 0;
                    while (apos < (int)arr.length() && cnt < MAX_PICKUPS_PER_BIN) {
                        int q1 = arr.indexOf('"', apos);
                        if (q1 < 0) break;
                        int q2 = arr.indexOf('"', q1+1);
                        if (q2 < 0) break;
                        String ds = arr.substring(q1+1, q2);
                        uint16_t yr; uint8_t mo, dy;
                        if (parseDate(ds, yr, mo, dy)) {
                            _cfg->bins[b].pickups[cnt++] = {dy, mo, yr, true};
                        }
                        apos = q2 + 1;
                    }
                    pos = arrEnd + 1;
                }
            } else {
                pos = bstart + 1;
            }
        }
    }

    storage_save(*_cfg);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleSetTime() {
    if (server.hasArg("ts")) {
        extern void setSystemTime(unsigned long);
        setSystemTime(server.arg("ts").toInt());
        server.send(200, "application/json", "{\"ok\":true}");
    } else {
        server.send(400, "text/plain", "Missing ts");
    }
}

static void handleGetTime() {
    extern unsigned long getSystemTime();
    server.send(200, "application/json", "{\"ts\":" + String(getSystemTime()) + "}");
}

static void handleNotFound() {
    server.send(404, "text/plain", "Not found");
}

// ─── PUBLIC API ───────────────────────────────────────────────────────────────
void webserver_init(AppConfig& cfg) {
    _cfg = &cfg;
    server.on("/",            HTTP_GET,  handleRoot);
    server.on("/api/config",  HTTP_GET,  handleGetConfig);
    server.on("/api/config",  HTTP_POST, handlePostConfig);
    server.on("/api/settime", HTTP_GET,  handleSetTime);
    server.on("/api/time",    HTTP_GET,  handleGetTime);
    server.onNotFound(handleNotFound);
    server.begin();
    _running = true;
    Serial.println("[Web] Server running at http://192.168.4.1");
}

void webserver_handle() { if (_running) server.handleClient(); }
void webserver_stop()   { server.stop(); _running = false; }
bool webserver_running(){ return _running; }
