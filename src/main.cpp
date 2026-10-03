#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <ctype.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include "MAX30105.h"
#include "heartRate.h"

// ---------- Reglaj ----------
#define WIFI_SSID        "Wokwi-GUEST"
#define WIFI_PASS        ""
#define LOG_INTERVAL_MS  10000UL                    // 1 mezi chak 10 s
#define TZ_INFO          "EST5EDT,M3.2.0,M11.1.0"   // Ayiti

// ---------- Seuils d'alerte (modifiables) ----------
#define TH_BPM_LOW     50      // en dessous : bradycardie
#define TH_BPM_HIGH    100     // au dessus  : tachycardie
#define TH_SPO2_LOW    94      // en dessous : SpO2 basse
#define TH_TEMP_HIGH   38.0    // a partir de : fievre
#define TH_TEMP_LOW    35.0    // en dessous : hypothermie
#define ALERT_HOLD_MS  2000UL  // la valeur doit rester hors seuil 2 s avant l'alerte

// ---------- Pins ----------
#define PIN_SDA       21
#define PIN_SCL       22
#define PIN_NTC       35      // NTC : temperature corporelle
#define PIN_DHT       15      // DHT22 : ambiance
#define PIN_SD_CS      5      // SD : SCK=18, MISO=19, MOSI=23
#define SAMPLE_HZ    100      // MAX30102 : 400 sps / moyenne 4

// ---------- Ekran ----------
#define SCREEN_W 128
#define SCREEN_H 64
#define ECG_BASE 54
#define ECG_AMP  22
Adafruit_SSD1306 oled(SCREEN_W, SCREEN_H, &Wire, -1);
DHT dht(PIN_DHT, DHT22);
WebServer server(80);
MAX30105 particleSensor;
String alarmText();

// ---------- Paj wèb (HTML + JavaScript) ----------
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Moniteur patients</title>
<style>
:root{--bg:#f4f6f8;--card:#fff;--tx:#1b2430;--mu:#6b7785;--ac:#0b6ef3;--bd:#dde3ea;--ok:#1a9e5c;--bad:#d93025}
@media(prefers-color-scheme:dark){:root{--bg:#0f141a;--card:#18202a;--tx:#e6ecf2;--mu:#8b98a7;--ac:#4c9aff;--bd:#2a3542;--ok:#3ddc84;--bad:#ff6b5e}}
*{box-sizing:border-box}
body{margin:0;font:15px system-ui,sans-serif;background:var(--bg);color:var(--tx)}
header{display:flex;justify-content:space-between;align-items:center;padding:12px 18px;background:var(--card);border-bottom:1px solid var(--bd)}
h1{font-size:18px;margin:0}h2{font-size:15px;margin:0 0 10px}
main{display:grid;grid-template-columns:320px 1fr;gap:14px;padding:14px;max-width:1200px;margin:auto}
@media(max-width:800px){main{grid-template-columns:1fr}}
.card{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:14px;margin-bottom:14px}
.tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:10px;margin-bottom:10px}
.tile{border:1px solid var(--bd);border-radius:8px;padding:10px}
.tile b{display:block;font-size:26px}.tile span{color:var(--mu);font-size:12px}
.tile.bad{border-color:var(--bad);background:rgba(217,48,37,.08)}.tile.bad b{color:var(--bad)}
.banner{padding:10px 12px;border-radius:8px;margin-bottom:10px;font-weight:600}
.banner.ok{background:rgba(26,158,92,.12);color:var(--ok)}
.banner.bad{background:rgba(217,48,37,.12);color:var(--bad)}
.banner.off{background:var(--bg);color:var(--mu)}
.muted{color:var(--mu)}
.pt{padding:9px 10px;border:1px solid var(--bd);border-radius:8px;margin-bottom:6px;cursor:pointer;position:relative}
.pt small{display:block;color:var(--mu)}
.pt.sel{border-color:var(--ac);box-shadow:0 0 0 1px var(--ac)}
.pt.out{opacity:.6}
.badge.out{background:var(--mu)}
button.danger{background:var(--bad)}
.badge{position:absolute;right:8px;top:8px;background:var(--ok);color:#fff;font-size:11px;padding:2px 7px;border-radius:10px}
input,select,textarea,button{font:inherit;padding:8px;border:1px solid var(--bd);border-radius:6px;background:var(--bg);color:var(--tx);width:100%;margin-bottom:8px}
input[type=range]{padding:0;border:0}
input[type=checkbox]{width:auto;margin-right:6px}
button{background:var(--ac);color:#fff;border:0;cursor:pointer}
button.sec,a.btn{background:transparent;color:var(--ac);border:1px solid var(--ac)}
a.btn{display:inline-block;padding:8px 12px;border-radius:6px;text-decoration:none;margin-bottom:8px}
.row{display:flex;gap:8px;flex-wrap:wrap}.row>*{flex:1;min-width:140px}
table{width:100%;border-collapse:collapse;font-size:13px}
th,td{text-align:left;padding:5px 6px;border-bottom:1px solid var(--bd)}
tr.al td{color:var(--bad)}
.scroll{max-height:320px;overflow:auto}
#toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:var(--tx);color:var(--bg);padding:10px 16px;border-radius:8px;display:none;z-index:9;max-width:90%}
</style></head><body>
<header><h1>Moniteur patients &mdash; ESP32</h1><span id="clock" class="muted">...</span></header>
<main>
<aside>
 <div class="card">
  <h2>Patients</h2>
  <input id="q" placeholder="Rechercher..." oninput="renderList()">
  <label class="muted" style="display:block;margin-bottom:8px;font-size:13px"><input type="checkbox" id="showOut" onchange="renderList()">Afficher les patients sortis</label>
  <div id="list"></div>
  <button onclick="toggleForm()">+ Nouveau patient</button>
 </div>
 <div class="card" id="formCard" style="display:none">
  <h2>Nouveau patient</h2>
  <form id="f" onsubmit="event.preventDefault();addP()">
   <input name="nom" placeholder="Nom complet" required maxlength="40">
   <div class="row"><input name="age" type="number" min="0" max="130" placeholder="Age" required>
   <select name="sexe"><option value="M">Masculin</option><option value="F">Féminin</option></select></div>
   <select name="groupe"><option>A+</option><option>A-</option><option>B+</option><option>B-</option><option>AB+</option><option>AB-</option><option>O+</option><option>O-</option></select>
   <textarea name="note" rows="3" placeholder="Note clinique" maxlength="200"></textarea>
   <div class="row"><input name="lit" placeholder="N° de lit" required maxlength="6">
   <input name="contact" placeholder="N° de contact" maxlength="20"></div>
   <button type="submit">Enregistrer le patient</button>
  </form>
 </div>
</aside>
<section>
 <div class="card">
  <h2 id="actTitle">...</h2>
  <div id="banner" class="banner off">--</div>
  <div class="tiles">
   <div class="tile" id="tBpm"><b id="bpm">--</b><span>BPM</span></div>
   <div class="tile" id="tSpo2"><b id="spo2">--</b><span>SpO2 %</span></div>
   <div class="tile" id="tTc"><b id="tc">--</b><span>Temp. corps &deg;C</span></div>
   <div class="tile"><b id="ta">--</b><span>Temp. ambiante &deg;C</span></div>
   <div class="tile"><b id="hum">--</b><span>Humidité %</span></div>
  </div>
  <div class="muted" id="info"></div>
 </div>
 <div class="card">
  <h2>Dossier du patient</h2>
  <div id="fiche" class="muted">Sélectionnez un patient dans la liste.</div>
  <div id="acts" style="display:none;margin-top:10px">
   <div class="row">
    <button id="bAct" onclick="setAct()">Définir comme patient actif</button>
    <button id="bLog" class="sec" onclick="logNow()">Enregistrer une mesure maintenant</button>
   </div>
   <button id="bOut" class="danger" onclick="dischargeP()">Sortie du patient (libérer le lit)</button>
   <a class="btn" id="dl" href="#">Télécharger la fiche + historique complet (CSV)</a>
  </div>
 </div>
 <div class="card">
  <h2>Alertes du patient <span class="muted" id="acount"></span></h2>
  <div class="scroll"><table>
   <thead><tr><th>Date / heure</th><th>Événement</th><th>Alerte</th><th>BPM</th><th>SpO2</th><th>T corps</th></tr></thead>
   <tbody id="alist"></tbody></table></div>
 </div>
 <div class="card">
  <h2>Historique <span class="muted" id="count"></span></h2>
  <div class="muted" style="font-size:12px;margin-bottom:6px">
   <span style="color:#e5484d">&bull; BPM</span> &nbsp;
   <span style="color:#3b82f6">&bull; SpO2</span> &nbsp;
   <span style="color:#f59e0b">&bull; Temp. corps</span></div>
  <div style="width:100%"><canvas id="cv" height="180"></canvas></div>
  <div class="scroll"><table>
   <thead><tr><th>Date / heure</th><th>BPM</th><th>SpO2</th><th>T corps</th><th>T amb.</th><th>Hum.</th><th>État</th></tr></thead>
   <tbody id="hist"></tbody></table></div>
 </div>
 <div class="card" id="simCard" style="display:none">
  <h2>Simulation du capteur MAX30102</h2>
  <div class="muted" style="margin-bottom:10px">Capteur non détecté : l'ESP32 simule les valeurs. Utilisez les curseurs pour tester les alertes.</div>
  <label>BPM : <b id="sbv">72</b></label>
  <input type="range" id="sb" min="30" max="200" value="72" oninput="$('sbv').textContent=this.value" onchange="sendSim()">
  <label>SpO2 : <b id="ssv">97</b> %</label>
  <input type="range" id="ss" min="70" max="100" value="97" oninput="$('ssv').textContent=this.value" onchange="sendSim()">
  <label><input type="checkbox" id="sf" checked onchange="sendSim()">Doigt présent sur le capteur</label>
 </div>
</section>
</main>
<div id="toast"></div>
<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const j=async(u,o)=>{const r=await fetch(u,o);if(!r.ok)throw new Error(await r.text());return r.json();};
let patients=[],active='',sel='',lastLogs=-1,lastAl=-1,prevActive='',simInit=false,tt=0;

function toast(m){const t=$('toast');t.textContent=m;t.style.display='block';clearTimeout(tt);tt=setTimeout(()=>{t.style.display='none';},5000);}
function toggleForm(){const c=$('formCard');c.style.display=c.style.display==='none'?'':'none';}

async function loadPatients(){try{patients=await j('/api/patients');renderList();}catch(e){}}

function renderList(){
  const q=$('q').value.toLowerCase();
  const h=patients.filter(p=>(p.nom+p.id).toLowerCase().indexOf(q)>=0&&($('showOut').checked||!p.sortie)).map(p=>
    '<div class="pt'+(p.id===sel?' sel':'')+(p.sortie?' out':'')+'" onclick="pick(\''+p.id+'\')"><b>'+esc(p.nom)+'</b><small>'+p.id+' &middot; Lit '+esc(p.lit)+' &middot; '+esc(p.age)+' ans '+esc(p.sexe)+' &middot; '+esc(p.groupe)+'</small>'+(p.sortie?'<span class="badge out">SORTI</span>':(p.id===active?'<span class="badge">ACTIF</span>':''))+'</div>').join('');
  $('list').innerHTML=h||'<p class="muted">Aucun patient</p>';
}

async function pick(id){
  sel=id;renderList();
  const p=patients.find(x=>x.id===id);if(!p)return;
  $('fiche').innerHTML='<b>'+esc(p.nom)+'</b> <span class="muted">'+p.id+'</span><br>'+esc(p.age)+' ans &middot; '+esc(p.sexe)+' &middot; Groupe '+esc(p.groupe)+' &middot; Lit '+esc(p.lit)+'<br>Contact : '+esc(p.contact)+'<br>Admission : '+esc(p.admission)+'<br><i>'+esc(p.note)+'</i>'+(p.sortie?'<br><b>Sorti le '+esc(p.sortie)+'</b>':'');
  $('acts').style.display='';
  ['bAct','bLog','bOut'].forEach(b=>{$(b).style.display=p.sortie?'none':'';});
  $('dl').href='/api/history?id='+id+'&dl=1';
  await loadHistory();await loadAlerts();
}

async function loadHistory(){
  if(!sel)return;
  try{
    const r=await fetch('/api/history?id='+sel);if(!r.ok)return;
    const t=await r.text();
    const rows=t.trim().split('\n').slice(1).filter(Boolean).map(l=>l.trim().split(';'));
    $('count').textContent='('+rows.length+' mesure'+(rows.length>1?'s':'')+(rows.length>500?', 500 dernières affichées - CSV = historique complet':'')+')';
    $('hist').innerHTML=rows.slice(-500).reverse().map(r=>'<tr'+(r[6]&&r[6]!=='Normal'?' class="al"':'')+'><td>'+esc(r[0])+'</td><td>'+esc(r[1])+'</td><td>'+esc(r[2])+'</td><td>'+esc(r[3])+'</td><td>'+esc(r[4])+'</td><td>'+esc(r[5])+'</td><td>'+esc(r[6])+'</td></tr>').join('');
    draw(rows);
  }catch(e){}
}

async function loadAlerts(){
  if(!sel)return;
  try{
    const r=await fetch('/api/alerts?id='+sel);if(!r.ok)return;
    const t=await r.text();
    const rows=t.trim().split('\n').slice(1).filter(Boolean).map(l=>l.trim().split(';'));
    $('acount').textContent='('+rows.length+')';
    $('alist').innerHTML=rows.slice(-100).reverse().map(r=>'<tr class="al"><td>'+esc(r[0])+'</td><td>'+esc(r[3])+'</td><td>'+esc(r[4])+'</td><td>'+esc(r[5])+'</td><td>'+esc(r[6])+'</td><td>'+esc(r[7])+'</td></tr>').join('')||'<tr><td colspan="6" class="muted">Aucune alerte enregistrée</td></tr>';
  }catch(e){}
}

function draw(rows){
  const c=$('cv'),w=c.width=c.parentElement.clientWidth,h=c.height=180,x=c.getContext('2d');
  x.clearRect(0,0,w,h);
  const pts=rows.slice(-300);if(pts.length<2)return;
  [[1,40,180,'#e5484d'],[2,90,100,'#3b82f6'],[3,30,42,'#f59e0b']].forEach(s=>{
    x.beginPath();x.strokeStyle=s[3];x.lineWidth=1.5;let on=false;
    pts.forEach((r,k)=>{
      const v=parseFloat(r[s[0]]);if(isNaN(v)){on=false;return;}
      const px=k/(pts.length-1)*(w-4)+2;
      const py=h-4-(Math.min(Math.max(v,s[1]),s[2])-s[1])/(s[2]-s[1])*(h-8);
      if(on)x.lineTo(px,py);else x.moveTo(px,py);on=true;
    });
    x.stroke();
  });
}

async function addP(){
  const f=$('f');
  const nb=v=>String(v).trim().toLowerCase().replace(/^0+(?=.)/,'');
  const occ=patients.find(p=>!p.sortie&&nb(p.lit)===nb(f.elements['lit'].value));
  if(occ){alert('Le lit '+f.elements['lit'].value.trim()+' est déjà occupé par '+occ.nom+' ('+occ.id+')');return;}
  try{
    const r=await fetch('/api/patients',{method:'POST',body:new URLSearchParams(new FormData(f))});
    if(!r.ok){alert(await r.text());return;}
    const d=await r.json();
    f.reset();$('formCard').style.display='none';
    if(d.prev)toast('Patient '+d.prev+' : dernière mesure enregistrée. Patient actif : '+d.id);
    await loadPatients();await pick(d.id);live();
  }catch(e){alert('Erreur réseau');}
}

async function setAct(){
  try{
    const d=await j('/api/active',{method:'POST',body:new URLSearchParams({id:sel})});
    if(d.prev&&d.prev!==sel)toast('Patient '+d.prev+' : dernière mesure enregistrée. Patient actif : '+sel);
    await live();renderList();await loadHistory();await loadAlerts();
  }catch(e){alert('Impossible de changer de patient');}
}

async function dischargeP(){
  const p=patients.find(x=>x.id===sel);if(!p||p.sortie)return;
  if(!confirm('Confirmer la sortie de '+p.nom+' ('+p.id+') ?\nLe lit '+p.lit+' sera libéré. L\'historique est conservé.'))return;
  try{
    const d=await j('/api/discharge',{method:'POST',body:new URLSearchParams({id:sel})});
    toast('Patient '+d.id+' sorti. Lit '+d.lit+' libéré. Historique conservé.');
    await loadPatients();await pick(sel);await live();await loadHistory();await loadAlerts();
  }catch(e){alert('Sortie impossible : '+e.message);}
}

async function logNow(){
  try{await j('/api/log',{method:'POST'});await live();await loadHistory();}
  catch(e){alert('Sélectionnez d\'abord un patient actif');}
}

async function sendSim(){
  try{await j('/api/sim',{method:'POST',body:new URLSearchParams({bpm:$('sb').value,spo2:$('ss').value,finger:$('sf').checked?1:0})});live();}
  catch(e){}
}

async function live(){
  try{
    const d=await j('/api/live');
    active=d.active;
    $('clock').textContent=d.t;
    $('actTitle').textContent=d.active?('Patient actif : '+d.nom+' ('+d.active+') - Lit '+d.lit):'Aucun patient actif - créez ou sélectionnez un patient';
    const f=(v,n)=>v==null?'--':v.toFixed(n);
    $('bpm').textContent=f(d.bpm,0);$('spo2').textContent=f(d.spo2,1);
    $('tc').textContent=f(d.tc,1);$('ta').textContent=f(d.ta,1);$('hum').textContent=f(d.hum,0);
    $('tBpm').className='tile'+((d.m&3)?' bad':'');
    $('tSpo2').className='tile'+((d.m&4)?' bad':'');
    $('tTc').className='tile'+((d.m&24)?' bad':'');
    const b=$('banner');
    if(!d.active){b.className='banner off';b.textContent='Aucun patient actif';}
    else if(d.alerts.length){b.className='banner bad';b.textContent='ALERTE : '+d.alerts.join(' + ');}
    else{b.className='banner ok';b.textContent='Valeurs normales';}
    $('info').textContent='Capteur : '+(d.sim?'simulé':'MAX30102')+' - Journal : 1 mesure / '+d.interval+' s - SD : '+(d.sd?'OK':'ABSENTE')+' - Seuils : BPM '+d.th.bl+'-'+d.th.bh+', SpO2 >= '+d.th.sl+' %, T corps '+d.th.tl+'-'+d.th.th+' °C';
    $('simCard').style.display=d.sim?'':'none';
    if(d.sim&&!simInit){simInit=true;$('sb').value=d.sbpm;$('sbv').textContent=d.sbpm;$('ss').value=d.sspo2;$('ssv').textContent=d.sspo2;$('sf').checked=d.sfing;}
    if(active!==prevActive){prevActive=active;renderList();}
    if(d.logs!==lastLogs){lastLogs=d.logs;if(sel&&sel===active)loadHistory();}
    if(d.ac!==lastAl){lastAl=d.ac;if(sel)loadAlerts();}
  }catch(e){$('clock').textContent='Connexion perdue...';}
}

setInterval(live,2000);
(async()=>{await loadPatients();await live();if(active)pick(active);})();
</script></body></html>
)rawliteral";

// ---------- Mezi ----------
float bpm = 0, spo2 = 0;
float tempBody = NAN, tempAmb = NAN, hum = NAN;
bool fingerOn = false;

// ---------- Capteur MAX30102 ----------
bool maxFound = false;                 // capteur detecte sur le bus I2C
int  simBpm = 72, simSpo2 = 97;        // valeurs utilisees si le capteur est absent
bool simFinger = true;
uint32_t sampleIdx = 0, lastBeatIdx = 0;
float rates[4] = {0};
uint8_t rateN = 0, rateSpot = 0;
double sIR = 0, sRed = 0, sIR2 = 0, sRed2 = 0;
int nS = 0;

// ---------- Trase ECG ----------
int8_t trace[SCREEN_W];
float phase = 0;

// ---------- Baz done (kat SD) ----------
bool sdOk = false;
const char* FILE_PATIENTS = "/patients.csv";
const char* FILE_ALERTS   = "/alerts.csv";
const char* FILE_ACTIVE   = "/active.txt";
const char* FILE_OUT      = "/sorties.csv";     // patients sortis : id;horodatage
const int NF = 9;   // id;nom;age;groupe;sexe;note;lit;contact;admission

// ---------- Pasyan aktif ----------
String curId = "", curNom = "", curLit = "";
unsigned long logCount = 0, alertCount = 0;

// ---------- Alertes ----------
#define A_BRADY 1
#define A_TACHY 2
#define A_SPO2  4
#define A_FEVER 8
#define A_HYPO  16
#define A_NOSIG 32
const char* ALERT_NAMES[6] = { "BRADYCARDIE", "TACHYCARDIE", "SpO2 BASSE", "FIEVRE", "HYPOTHERMIE", "PAS DE SIGNAL" };
uint8_t alertMask = 0;                 // alertes confirmees
uint8_t rawMask = 0;                   // etat instantane
unsigned long rawSince = 0;

unsigned long tEcg = 0, tDraw = 0, tSlow = 0, tDht = 0, tLog = 0, tHeart = 0;

// ================= Itilitè =================
String nowStr() {
  time_t t = time(nullptr);
  if (t < 1700000000) return "uptime+" + String(millis() / 1000) + "s";   // NTP poko sinkwonize
  struct tm tm;
  localtime_r(&t, &tm);
  char b[24];
  strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tm);
  return String(b);
}

String jsonEsc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

String numJ(float v, int d) { return isnan(v) ? String("null") : String(v, d); }

String clean(String s, size_t maxLen) {
  s.trim();
  s.replace(";", ",");
  s.replace("\r", " ");
  s.replace("\n", " ");
  if (s.length() > maxLen) s = s.substring(0, maxLen);
  return s;
}

// Pou OLED la : li pa gen aksan
String asciiOnly(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (c < 128) o += (char)c;
    else if ((c & 0xC0) == 0xC0) o += '?';
  }
  return o;
}

bool validId(const String& id) {
  if (id.length() < 2 || id.length() > 6 || id[0] != 'P') return false;
  for (size_t i = 1; i < id.length(); i++) if (!isDigit(id[i])) return false;
  return true;
}

String histPath(const String& id) { return "/rec/" + id + ".csv"; }

// ================= Kat SD =================
void appendLine(const char* path, const String& header, const String& line) {
  bool isNew = !SD.exists(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) { Serial.println("Erreur ekriti SD"); return; }
  if (isNew) f.println(header);
  f.println(line);
  f.close();
}

void splitLine(const String& line, String p[NF]) {
  int from = 0;
  for (int i = 0; i < NF; i++) {
    int to = line.indexOf(';', from);
    if (to < 0) to = line.length();
    p[i] = line.substring(from, to);
    from = to + 1;
  }
}

bool findPatient(const String& id, String p[NF]) {
  File f = SD.open(FILE_PATIENTS, FILE_READ);
  if (!f) return false;
  f.readStringUntil('\n');
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.startsWith(id + ";")) {
      splitLine(line, p);
      f.close();
      return true;
    }
  }
  f.close();
  return false;
}

// Date de sortie d'un patient ("" = toujours hospitalise)
String dischargeDate(const String& id) {
  File f = SD.open(FILE_OUT, FILE_READ);
  if (!f) return "";
  f.readStringUntil('\n');                       // en-tete
  String res = "";
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.startsWith(id + ";")) { res = line.substring(id.length() + 1); break; }
  }
  f.close();
  return res;
}

// Ligne sans mesure dans le journal d'un patient (ex. sortie d'un patient non actif)
void logMarker(const String& id, const String& text) {
  if (!sdOk) return;
  File f = SD.open(histPath(id), FILE_APPEND);
  if (!f) return;
  f.print(nowStr());
  f.print(";;;;;;");
  f.println(text);
  f.close();
}

String newId() {
  int n = 1;
  File f = SD.open(FILE_PATIENTS, FILE_READ);
  if (f) {
    f.readStringUntil('\n');
    while (f.available()) {
      String l = f.readStringUntil('\n');
      l.trim();
      if (l.length() > 1 && l[0] == 'P') {
        int v = l.substring(1).toInt();
        if (v >= n) n = v + 1;
      }
    }
    f.close();
  }
  char b[12];
  snprintf(b, sizeof(b), "P%03d", n);
  return String(b);
}

// Normalise un numero de lit pour la comparaison : "07", "7" et " 7 " designent le meme lit
String normBed(String s) {
  s.trim();
  s.toUpperCase();
  while (s.length() > 1 && s[0] == '0') s = s.substring(1);
  return s;
}

// Retourne "Nom (Pxxx)" si le lit est deja occupe, sinon ""
String bedOccupant(const String& lit) {
  String target = normBed(lit);
  String res = "";
  File f = SD.open(FILE_PATIENTS, FILE_READ);
  if (!f) return res;
  f.readStringUntil('\n');
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    String p[NF];
    splitLine(line, p);
    if (dischargeDate(p[0]).length()) continue;   // patient sorti : le lit est libre
    if (normBed(p[6]) == target) { res = p[1] + " (" + p[0] + ")"; break; }
  }
  f.close();
  return res;
}

// Retient le patient actif (reprise apres redemarrage)
void saveActive() {
  if (!sdOk) return;
  File f = SD.open(FILE_ACTIVE, FILE_WRITE);
  if (f) { f.print(curId); f.close(); }
}

void loadActive() {
  File f = SD.open(FILE_ACTIVE, FILE_READ);
  if (!f) return;
  String id = f.readStringUntil('\n');
  f.close();
  id.trim();
  String p[NF];
  if (validId(id) && findPatient(id, p) && dischargeDate(id).length() == 0) { curId = p[0]; curNom = p[1]; curLit = p[6]; }
}

// Jounal medikal : ajoute yon mezi ak dat/lè nan dosye pasyan aktif la
bool logRecord(const String& note = "") {
  if (!sdOk || curId == "") return false;
  File f = SD.open(histPath(curId), FILE_APPEND);
  if (!f) return false;
  String etat = note.length() ? note : alarmText();
  etat.replace(";", ",");
  char b[96];
  snprintf(b, sizeof(b), "%s;%.0f;%.1f;%.1f;%.1f;%.0f;",
           nowStr().c_str(), bpm, spo2, tempBody, tempAmb, hum);
  f.print(b);
  f.println(etat);
  f.close();
  logCount++;
  return true;
}

// ================= Capteur cardiaque =================
void resetHeart() {
  bpm = 0; spo2 = 0;
  rateN = 0; rateSpot = 0;
  lastBeatIdx = 0;
  sIR = sRed = sIR2 = sRed2 = 0; nS = 0;
}

void readHeartReal() {
  particleSensor.check();
  while (particleSensor.available()) {
    uint32_t ir  = particleSensor.getFIFOIR();
    uint32_t red = particleSensor.getFIFORed();
    particleSensor.nextSample();
    sampleIdx++;

    if (ir < 50000) {                         // pas de doigt
      fingerOn = false;
      resetHeart();
      continue;
    }
    fingerOn = true;

    // Frequence cardiaque : detection de battement
    if (checkForBeat((int32_t)ir)) {
      if (lastBeatIdx != 0) {
        float b = 60.0f * SAMPLE_HZ / (float)(sampleIdx - lastBeatIdx);
        if (b > 30 && b < 220) {
          rates[rateSpot] = b;
          rateSpot = (rateSpot + 1) % 4;
          if (rateN < 4) rateN++;
          float sum = 0;
          for (int i = 0; i < rateN; i++) sum += rates[i];
          bpm = sum / rateN;
        }
      }
      lastBeatIdx = sampleIdx;
    }

    // SpO2 : rapport AC/DC rouge / infrarouge sur 2 secondes
    sIR += ir;   sRed += red;
    sIR2 += (double)ir * ir;   sRed2 += (double)red * red;
    if (++nS >= 2 * SAMPLE_HZ) {
      double mIR = sIR / nS, mRed = sRed / nS;
      double vIR = sIR2 / nS - mIR * mIR, vRed = sRed2 / nS - mRed * mRed;
      if (vIR > 0 && vRed >= 0) {
        double R = (sqrt(vRed) / mRed) / (sqrt(vIR) / mIR);
        spo2 = constrain(110.0 - 25.0 * R, 70.0, 100.0);
      }
      sIR = sRed = sIR2 = sRed2 = 0; nS = 0;
    }
  }
}

void readHeart() {
  if (maxFound) { readHeartReal(); return; }
  fingerOn = simFinger;                       // mode simulation (capteur absent)
  if (!simFinger) { bpm = 0; spo2 = 0; }
  else { bpm = simBpm; spo2 = simSpo2; }
}

// ================= Kapte yo =================
float gauss(float x, float mu, float sigma, float amp) {
  float d = (x - mu) / sigma;
  return amp * expf(-0.5f * d * d);
}

float ecgWave(float p) {
  return gauss(p, 0.16, 0.035,  0.12)
       + gauss(p, 0.26, 0.012, -0.12)
       + gauss(p, 0.285, 0.014, 1.00)
       + gauss(p, 0.31, 0.012, -0.22)
       + gauss(p, 0.52, 0.050,  0.28);
}

void ecgStep() {
  const int SUB = 4;
  const float dt = 0.020f / SUB;
  float best = 0;
  for (int i = 0; i < SUB; i++) {
    phase += dt * bpm / 60.0f;
    if (phase >= 1.0f) phase -= 1.0f;
    float v = ecgWave(phase) + (random(-100, 101) / 100.0f) * 0.015f;
    if (fabsf(v) > fabsf(best)) best = v;
  }
  memmove(trace, trace + 1, SCREEN_W - 1);
  trace[SCREEN_W - 1] = (int8_t)roundf(best * ECG_AMP);
}

void readNtc() {
  long s = 0;
  for (int i = 0; i < 8; i++) s += analogRead(PIN_NTC);
  int v = s / 8;
  if (v <= 0 || v >= 4095) tempBody = NAN;
  else tempBody = 1.0 / (log(v / (4095.0 - v)) / 3950.0 + 1.0 / 298.15) - 273.15;
}

// ================= Alertes automatiques =================
uint8_t computeRaw() {
  uint8_t m = 0;
  if (!fingerOn) m |= A_NOSIG;
  else {
    if (bpm > 0 && bpm < TH_BPM_LOW) m |= A_BRADY;
    if (bpm > TH_BPM_HIGH)           m |= A_TACHY;
    if (spo2 > 0 && spo2 < TH_SPO2_LOW) m |= A_SPO2;
  }
  if (!isnan(tempBody)) {
    if (tempBody >= TH_TEMP_HIGH)     m |= A_FEVER;
    else if (tempBody < TH_TEMP_LOW)  m |= A_HYPO;
  }
  return m;
}

String maskText(uint8_t m) {
  String s = "";
  for (int i = 0; i < 6; i++) {
    if (m & (1 << i)) {
      if (s.length()) s += " + ";
      s += ALERT_NAMES[i];
    }
  }
  return s;
}

String alarmText() { return alertMask ? maskText(alertMask) : String("Normal"); }

// Enregistre une alerte dans /alerts.csv ET dans le journal du patient
void logAlertEvent(const char* ev, int bit) {
  if (curId == "") return;
  alertCount++;
  String nom = curNom;
  nom.replace(";", ",");
  char b[64];
  snprintf(b, sizeof(b), ";%.0f;%.1f;%.1f", bpm, spo2, tempBody);
  String line = nowStr() + ";" + curId + ";" + nom + ";" + ev + ";" + ALERT_NAMES[bit] + b;
  if (sdOk) appendLine(FILE_ALERTS, "horodatage;id;nom;evenement;alerte;bpm;spo2;temp_corps", line);
  logRecord(String("ALERTE ") + ev + ": " + ALERT_NAMES[bit]);
}

void updateAlerts(unsigned long now) {
  uint8_t m = computeRaw();
  if (m != rawMask) { rawMask = m; rawSince = now; }
  if (m != alertMask && now - rawSince >= ALERT_HOLD_MS) {
    uint8_t raised = m & ~alertMask, cleared = alertMask & ~m;
    alertMask = m;
    for (int i = 0; i < 6; i++) {
      if (raised  & (1 << i)) logAlertEvent("DEBUT", i);
      if (cleared & (1 << i)) logAlertEvent("FIN", i);
    }
  }
}

// ================= Changement de patient a la volee =================
// Le patient precedent garde tout son historique (fichier /rec/Pxxx.csv).
// Sa derniere mesure et ses alertes en cours sont enregistrees avant le changement.
void switchPatient(String p[NF]) {
  String prev = curId;
  bool changed = (prev != p[0]);
  if (changed && prev != "") {
    logRecord("CHANGEMENT -> " + p[0]);
    for (int i = 0; i < 6; i++) if (alertMask & (1 << i)) logAlertEvent("FIN (CHANGEMENT)", i);
  }
  curId = p[0]; curNom = p[1]; curLit = p[6];
  saveActive();
  tLog = 0;                                   // premiere mesure du nouveau patient tout de suite
  if (changed) {
    for (int i = 0; i < 6; i++) if (alertMask & (1 << i)) logAlertEvent("EN COURS", i);
  }
}

// ================= Woutin API =================
void handleLive() {
  String o = "{\"t\":\"" + nowStr() + "\",\"active\":\"" + curId + "\",\"nom\":\"" + jsonEsc(curNom) +
             "\",\"lit\":\"" + jsonEsc(curLit) + "\"";
  o += ",\"bpm\":" + numJ(bpm, 0) + ",\"spo2\":" + numJ(spo2, 1) + ",\"tc\":" + numJ(tempBody, 1) +
       ",\"ta\":" + numJ(tempAmb, 1) + ",\"hum\":" + numJ(hum, 0);
  o += ",\"fing\":" + String(fingerOn ? "true" : "false") + ",\"m\":" + String(alertMask) + ",\"alerts\":[";
  bool first = true;
  for (int i = 0; i < 6; i++) {
    if (alertMask & (1 << i)) {
      if (!first) o += ",";
      first = false;
      o += "\"" + String(ALERT_NAMES[i]) + "\"";
    }
  }
  o += "],\"alarm\":\"" + alarmText() + "\",\"sd\":" + String(sdOk ? "true" : "false") +
       ",\"logs\":" + String(logCount) + ",\"ac\":" + String(alertCount) +
       ",\"interval\":" + String(LOG_INTERVAL_MS / 1000);
  o += ",\"sim\":" + String(maxFound ? "false" : "true") + ",\"sbpm\":" + String(simBpm) +
       ",\"sspo2\":" + String(simSpo2) + ",\"sfing\":" + String(simFinger ? "true" : "false");
  o += ",\"th\":{\"bl\":" + String(TH_BPM_LOW) + ",\"bh\":" + String(TH_BPM_HIGH) +
       ",\"sl\":" + String(TH_SPO2_LOW) + ",\"tl\":" + String(TH_TEMP_LOW, 0) +
       ",\"th\":" + String(TH_TEMP_HIGH, 0) + "}}";
  server.send(200, "application/json", o);
}

void handlePatients() {
  String out = "[";
  bool first = true;
  File f = SD.open(FILE_PATIENTS, FILE_READ);
  if (f) {
    f.readStringUntil('\n');
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      String p[NF];
      splitLine(line, p);
      if (!first) out += ",";
      first = false;
      out += "{\"id\":\"" + jsonEsc(p[0]) + "\",\"nom\":\"" + jsonEsc(p[1]) + "\",\"age\":\"" + jsonEsc(p[2]) +
             "\",\"groupe\":\"" + jsonEsc(p[3]) + "\",\"sexe\":\"" + jsonEsc(p[4]) + "\",\"note\":\"" + jsonEsc(p[5]) +
             "\",\"lit\":\"" + jsonEsc(p[6]) + "\",\"contact\":\"" + jsonEsc(p[7]) +
             "\",\"admission\":\"" + jsonEsc(p[8]) + "\",\"sortie\":\"" + jsonEsc(dischargeDate(p[0])) + "\"}";
    }
    f.close();
  }
  out += "]";
  server.send(200, "application/json", out);
}

void handleAddPatient() {
  if (!sdOk) { server.send(503, "text/plain", "Carte SD absente"); return; }
  String nom     = clean(server.arg("nom"), 40);
  String age     = clean(server.arg("age"), 3);
  String groupe  = clean(server.arg("groupe"), 3);
  String sexe    = clean(server.arg("sexe"), 1);
  String note    = clean(server.arg("note"), 200);
  String lit     = clean(server.arg("lit"), 6);
  String contact = clean(server.arg("contact"), 20);
  sexe.toUpperCase();

  if (nom.length() == 0 || age.length() == 0 || lit.length() == 0) {
    server.send(400, "text/plain", "Nom, age et numero de lit obligatoires"); return;
  }
  int a = age.toInt();
  if (a < 0 || a > 130) { server.send(400, "text/plain", "Age invalide"); return; }
  if (sexe != "M" && sexe != "F") { server.send(400, "text/plain", "Sexe invalide"); return; }
  String occ = bedOccupant(lit);                   // un lit = un seul patient
  if (occ.length()) {
    server.send(409, "text/plain", "Le lit " + lit + " est deja occupe par " + occ);
    return;
  }

  String id = newId();
  String adm = nowStr();
  String line = id + ";" + nom + ";" + age + ";" + groupe + ";" + sexe + ";" + note + ";" +
                lit + ";" + contact + ";" + adm;
  appendLine(FILE_PATIENTS, "id;nom;age;groupe_sanguin;sexe;note_clinique;lit;contact;admission", line);

  File h = SD.open(histPath(id), FILE_WRITE);      // dosye istorik pasyan an
  if (h) { h.println("horodatage;bpm;spo2;temp_corps;temp_amb;humidite;etat"); h.close(); }

  String np[NF];
  np[0] = id; np[1] = nom; np[2] = age; np[3] = groupe; np[4] = sexe;
  np[5] = note; np[6] = lit; np[7] = contact; np[8] = adm;
  String prev = curId;
  switchPatient(np);                                // nouveau patient = patient actif
  server.send(200, "application/json", "{\"id\":\"" + id + "\",\"prev\":\"" + prev + "\"}");
}

void handleActive() {
  String id = server.arg("id");
  String p[NF];
  if (!validId(id) || !findPatient(id, p)) { server.send(404, "text/plain", "Patient introuvable"); return; }
  if (dischargeDate(id).length()) { server.send(409, "text/plain", "Patient deja sorti"); return; }
  String prev = curId;
  switchPatient(p);
  server.send(200, "application/json", "{\"ok\":true,\"prev\":\"" + prev + "\"}");
}

// Sortie d'un patient : le lit est libere, l'historique et les alertes sont conserves
void handleDischarge() {
  if (!sdOk) { server.send(503, "text/plain", "Carte SD absente"); return; }
  String id = server.arg("id");
  String p[NF];
  if (!validId(id) || !findPatient(id, p)) { server.send(404, "text/plain", "Patient introuvable"); return; }
  if (dischargeDate(id).length()) { server.send(409, "text/plain", "Patient deja sorti"); return; }

  bool wasActive = (id == curId);
  if (wasActive) {
    logRecord("SORTIE");                                    // derniere mesure du patient
    for (int i = 0; i < 6; i++) if (alertMask & (1 << i)) logAlertEvent("FIN (SORTIE)", i);
    curId = ""; curNom = ""; curLit = "";                   // plus de patient actif
    saveActive();
  } else {
    logMarker(id, "SORTIE");
  }
  appendLine(FILE_OUT, "id;horodatage", id + ";" + nowStr());
  server.send(200, "application/json", "{\"ok\":true,\"id\":\"" + id + "\",\"lit\":\"" + jsonEsc(p[6]) +
              "\",\"wasActive\":" + String(wasActive ? "true" : "false") + "}");
}

void handleLog() {
  if (curId == "") { server.send(400, "text/plain", "Aucun patient actif"); return; }
  if (!logRecord()) { server.send(500, "text/plain", "Erreur SD"); return; }
  server.send(200, "application/json", "{\"ok\":true}");
}

// Nom de fichier propre : lettres, chiffres et "_"
String safeName(const String& s, size_t maxLen) {
  String o = "";
  for (size_t i = 0; i < s.length() && o.length() < maxLen; i++) {
    char c = s[i];
    if (isalnum((unsigned char)c)) o += c;
    else if (c == ' ' || c == '-' || c == '_') o += '_';
  }
  return o;
}

void handleHistory() {
  String id = server.arg("id");
  if (!validId(id)) { server.send(400, "text/plain", "ID invalide"); return; }
  File f = SD.open(histPath(id), FILE_READ);
  if (!f) { server.send(404, "text/plain", "Historique introuvable"); return; }

  if (!server.hasArg("dl")) {                       // affichage dans la page
    server.streamFile(f, "text/csv");
    f.close();
    return;
  }

  // ----- Telechargement : fiche du patient + mesures (id, nom, lit et date sur chaque ligne) -----
  String p[NF];
  if (!findPatient(id, p)) { f.close(); server.send(404, "text/plain", "Patient introuvable"); return; }
  String sortie = dischargeDate(id);
  String day = nowStr().substring(0, 10);
  String fname = id + "_" + safeName(p[1], 20);
  if (day.length() && isDigit(day[0])) fname += "_" + day;
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + fname + ".csv\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv; charset=utf-8", "");

  String buf = "\xEF\xBB\xBF";                      // BOM UTF-8 : accents corrects dans Excel
  buf += "FICHE PATIENT\r\n";
  buf += "Identifiant;" + p[0] + "\r\n";
  buf += "Nom;" + p[1] + "\r\n";
  buf += "Age;" + p[2] + "\r\n";
  buf += "Sexe;" + p[4] + "\r\n";
  buf += "Groupe sanguin;" + p[3] + "\r\n";
  buf += "Lit;" + p[6] + "\r\n";
  buf += "Contact;" + p[7] + "\r\n";
  buf += "Note clinique;" + p[5] + "\r\n";
  buf += "Date d'admission;" + p[8] + "\r\n";
  buf += "Date de sortie;" + (sortie.length() ? sortie : String("Toujours hospitalise")) + "\r\n";
  buf += "Exporte le;" + nowStr() + "\r\n\r\n";
  buf += "id;nom;lit;horodatage;bpm;spo2;temp_corps;temp_amb;humidite;etat\r\n";
  server.sendContent(buf);
  buf = "";

  String prefix = id + ";" + p[1] + ";" + p[6] + ";";
  f.readStringUntil('\n');                          // saute l'en-tete d'origine
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    buf += prefix + line + "\r\n";
    if (buf.length() > 1000) { server.sendContent(buf); buf = ""; }
  }
  if (buf.length()) server.sendContent(buf);
  f.close();
  server.sendContent("");                           // fin de la reponse
}

// Alertes d'un patient (lignes filtrees dans /alerts.csv)
void handleAlerts() {
  String id = server.arg("id");
  if (!validId(id)) { server.send(400, "text/plain", "ID invalide"); return; }
  String out = "";
  File f = SD.open(FILE_ALERTS, FILE_READ);
  if (f) {
    f.readStringUntil('\n');
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      int c = line.indexOf(';');
      if (c < 0) continue;
      if (line.substring(c + 1, c + 1 + id.length() + 1) == id + ";") {
        out += line + "\n";
        if (out.length() > 12000) {               // on garde les plus recentes
          int cut = out.indexOf('\n', 2000);
          if (cut > 0) out = out.substring(cut + 1);
        }
      }
    }
    f.close();
  }
  server.send(200, "text/csv", "horodatage;id;nom;evenement;alerte;bpm;spo2;temp_corps\n" + out);
}

// Valeurs du capteur simule (seulement si le MAX30102 est absent)
void handleSim() {
  if (maxFound) { server.send(400, "text/plain", "Capteur reel actif"); return; }
  if (server.hasArg("bpm"))    simBpm    = constrain(server.arg("bpm").toInt(), 30, 200);
  if (server.hasArg("spo2"))   simSpo2   = constrain(server.arg("spo2").toInt(), 70, 100);
  if (server.hasArg("finger")) simFinger = server.arg("finger").toInt() != 0;
  server.send(200, "application/json", "{\"ok\":true}");
}

// ================= OLED =================
String oledAlert() {
  if (!alertMask) return "Normal";
  for (int i = 0; i < 6; i++) {
    if (alertMask & (1 << i)) {
      String s = ALERT_NAMES[i];
      if (s.length() > 11) s = s.substring(0, 11);
      if (alertMask & ~(1 << i)) s += "+";
      return s;
    }
  }
  return "";
}

void drawOled() {
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.printf("BPM:%3.0f SpO2:%4.1f%%\n", bpm, spo2);
  oled.printf("Tc:%4.1fC Ta:%4.1fC\n", tempBody, tempAmb);
  oled.printf("Hum:%2.0f%% %s\n", hum, oledAlert().c_str());

  bool showIp = (curId == "") || ((millis() / 3000) % 2 == 1);
  String pl;
  if (showIp) pl = (WiFi.status() == WL_CONNECTED) ? "IP " + WiFi.localIP().toString() : String("WiFi hors ligne");
  else        pl = curId + " " + asciiOnly(curNom).substring(0, 10) + " L" + curLit;
  oled.println(pl.substring(0, 21));

  for (int x = 1; x < SCREEN_W; x++)
    oled.drawLine(x - 1, ECG_BASE - trace[x - 1], x, ECG_BASE - trace[x], SSD1306_WHITE);
  oled.display();
}

// ================= setup / loop =================
void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  dht.begin();

  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("OLED introuvable (0x3C)");
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  memset(trace, 0, sizeof(trace));
  readNtc();

  // MAX30102 : si absent, le firmware simule les valeurs (curseurs sur le tableau de bord)
  maxFound = particleSensor.begin(Wire, I2C_SPEED_FAST);
  if (maxFound) {
    particleSensor.setup(0x3F, 4, 2, 400, 411, 4096);   // rouge + IR, 400 sps, moyenne 4
    Serial.println("MAX30102 detecte");
  } else {
    Serial.println("MAX30102 absent : mode simulation");
  }

  sdOk = SD.begin(PIN_SD_CS);
  if (sdOk) { SD.mkdir("/rec"); loadActive(); }
  Serial.println(sdOk ? "Carte SD OK" : "Carte SD ERREUR");

  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.println("Connexion WiFi...");
  oled.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(250);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP : ");
    Serial.println(WiFi.localIP());
    configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");   // dat ak lè reyèl
  } else {
    Serial.println("WiFi ERREUR");
  }

  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", PAGE); });
  server.on("/api/live", HTTP_GET, handleLive);
  server.on("/api/patients", HTTP_GET, handlePatients);
  server.on("/api/patients", HTTP_POST, handleAddPatient);
  server.on("/api/active", HTTP_POST, handleActive);
  server.on("/api/discharge", HTTP_POST, handleDischarge);
  server.on("/api/log", HTTP_POST, handleLog);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/alerts", HTTP_GET, handleAlerts);
  server.on("/api/sim", HTTP_POST, handleSim);
  server.onNotFound([]() { server.send(404, "text/plain", "Introuvable"); });
  server.begin();
  Serial.println("Serveur web demarre (port 80)");
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  if (now - tHeart >= 20) { tHeart = now; readHeart(); }

  if (now - tSlow >= 200) { tSlow = now; readNtc(); updateAlerts(now); }

  if (now - tDht >= 2000) {
    tDht = now;
    float t = dht.readTemperature(), h = dht.readHumidity();
    if (!isnan(t)) tempAmb = t;
    if (!isnan(h)) hum = h;
  }

  if (now - tEcg >= 20)  { tEcg = now;  ecgStep(); }
  if (now - tDraw >= 60) { tDraw = now; drawOled(); }

  // jounal medikal otomatik
  if (curId != "" && now - tLog >= LOG_INTERVAL_MS) { tLog = now; logRecord(); }
}
