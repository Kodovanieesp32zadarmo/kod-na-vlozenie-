// ====================================================================================
// CAST 2: HTML ROZHRANIE A POMOCNE FUNKCIE
// ====================================================================================

// --- Podporne a meracie funkcie, Rolling filtre, Multi-turn SA5600 a Preferences NVS ---

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="sk">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Ovladanie Terasy V84</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@2.9.4"></script>
<style>
body { font-family:Arial,sans-serif; background:#121212; color:#e0e0e0; margin:0; padding:10px; text-align:center; user-select:none; }
h1, h2, h3, h4 { color:#ffffff; margin:10px 0; }
.container { max-width:1100px; margin:0 auto; padding:5px; }
.card { background:#1e1e1e; border:1px solid #2d2d2d; border-radius:10px; padding:15px; margin:10px auto; box-shadow:0 4px 6px rgba(0,0,0,0.5); text-align:left; clear:both; }
.grid-sensors { display:grid; grid-template-columns:repeat(auto-fit, minmax(220px, 1fr)); gap:10px; }
.sensor-card { background:#252525; padding:15px; border-radius:8px; border:1px solid #333; text-align:center; position:relative; }
.stat { font-size:24px; font-weight:bold; color:#00adb5; display:block; margin:5px 0; }
.sub-stat { font-size:12px; color:#aaaaaa; margin-top:2px; }
.row { display:flex; justify-content:space-between; align-items:center; border-bottom:1px solid #2d2d2d; padding:8px 0; }
.row:last-child { border:none; }
.btn { padding:10px 18px; font-size:14px; margin:4px; border:none; border-radius:6px; cursor:pointer; font-weight:bold; color:#fff; transition: background 0.2s; }
.btn-up { background:#28a745; } .btn-up:hover { background:#218838; }
.btn-down { background:#007bff; } .btn-down:hover { background:#0069d9; }
.btn-stop { background:#dc3545; width:95%; margin:5px auto; padding:15px; font-size:18px; display:block; }
.btn-stop:hover { background:#c82333; }
.btn-all { background:#f39c12; width:45%; padding:12px; margin:4px; font-size:13px; }
.btn-all:hover { background:#d68910; }
.btn-all-time { background:#8e44ad; width:45%; padding:12px; margin:4px; font-size:13px; }
.btn-all-time:hover { background:#732d91; }
.btn-reset { background:#c0392b; padding:4px 8px; font-size:11px; min-width:auto; margin-left:10px; border-radius:4px; border:none; }
.btn-solo-main { padding: 18px; font-size: 16px; width: 45%; margin: 5px 2%; box-sizing: border-box; position:relative; }
.btn-timer { position: absolute; top: 2px; right: 5px; font-size: 10px; background: rgba(0,0,0,0.5); padding: 2px 4px; border-radius: 3px; color: #00ffff; font-weight: bold; display:none; }
.inp-w { width:70px; background:#2a2a2a; color:#fff; border:1px solid #444; padding:5px; border-radius:4px; text-align:center; }
.conf-grid { display:grid; grid-template-columns:40px 45px repeat(6, 1fr); gap:4px; text-align:center; align-items:center; font-size:11px; margin-bottom:5px; }
.conf-grid div { padding:3px 0; }
.conf-grid input, .conf-grid select { width:95%; font-size:11px; padding:3px 0; background:#2a2a2a; color:#fff; border:1px solid #444; border-radius:3px; text-align:center; }
.preset-grid { display:grid; grid-template-columns:40px repeat(8, 1fr); gap:4px; text-align:center; align-items:center; font-size:11px; margin-bottom:5px; }
.preset-grid input { width:95%; font-size:11px; padding:3px 0; background:#2a2a2a; color:#fff; border:1px solid #444; border-radius:3px; text-align:center; }
#alarm { background:#721c24; color:#f8d7da; border:2px solid #f5c6cb; padding:15px; border-radius:8px; font-weight:bold; font-size:18px; margin:15px auto; display:none; animation: blink 1s infinite; }
@keyframes blink { 0% { opacity: 1; } 50% { opacity: 0.4; } 100% { opacity: 1; } }
.saved-banner { position:fixed; top:20px; left:50%; transform:translateX(-50%); background:#28a745; color:white; padding:10px 20px; border-radius:5px; font-weight:bold; display:none; z-index:1000; }
.chart-container { position: relative; background:#1e1e1e; padding:15px; border-radius:8px; border:1px solid #2d2d2d; margin:20px auto; height:260px; width:100%; box-sizing: border-box; }
.btn-active-hold { background: #ff5500 !important; color: #fff !important; box-shadow: 0 0 10px #ff5500; }
.btn-active-hore { background: #155724 !important; color: #d4edda !important; box-shadow: 0 0 10px #28a745; }
.btn-active-dole { background: #1c3d5a !important; color: #cce5ff !important; box-shadow: 0 0 10px #007bff; }
.badge-light { font-size:11px; font-weight:bold; padding:2px 6px; border-radius:4px; display:inline-block; margin-top:4px; border:1px solid #333; }
.blind-row { border-bottom: 1px solid #333; padding: 15px 0; }
.blind-row:last-child { border: none; }
</style>
</head>
<body>
<div class="saved-banner" id="sb">Nastavenia ulozene v NVS!</div>
<div class="container">
  <h1>Ovladanie Terasy V84 - FIXED</h1>

  <div class="grid-sensors">
    <div class="sensor-card">
      <div>Teplota Vnutorna</div>
      <span class="stat"><span id="ti">0.0</span> C <button class="btn btn-reset" onclick="resetMinMax('int')">R</button></span>
      <div class="sub-stat">Min: <span id="timn">0.0</span> C | Max: <span id="timx">0.0</span> C</div>
    </div>
    <div class="sensor-card">
      <div>Teplota Vonkajsia</div>
      <span class="stat"><span id="te">0.0</span> C <button class="btn btn-reset" onclick="resetMinMax('ext')">R</button></span>
      <div class="sub-stat">Min: <span id="temn">0.0</span> C | Max: <span id="temx">0.0</span> C</div>
    </div>
    <div class="sensor-card">
      <div>Rychlost Vetra</div>
      <span class="stat"><span id="ws">0.0</span> km/h <button class="btn btn-reset" onclick="resetWindMax()">R</button></span>
      <div class="sub-stat">Max: <span id="wsmx">0.0</span> km/h | 24h: <span id="wsmx24">0.0</span> km/h</div>
    </div>
    <div class="sensor-card">
      <div>Senzor Svetla / Wi-Fi</div>
      <span class="stat"><span id="lt">0</span> / <span id="rssi">0</span> dBm</span>
      <div style="margin-top:5px;">
        <span id="btn-light-state" class="badge-light">S1: ???</span>
        <span id="btn-light2-state" class="badge-light" style="margin-left:5px;">S2: ???</span>
      </div>
    </div>
  </div>

  <div id="alarm">VETERNY POPLACH! ZALUZIE SU ZABLOKOVANE V HORNEJ POZICII!</div>

  <div class="card">
    <h3>Celkove ovladanie vsetkych zaluzii (150ms kaskada)</h3>
    <div style="display:flex; justify-content:space-around; flex-wrap:wrap;">
      <button class="btn btn-all-time" onclick="timeAll(1)">VSETKO HORE (Cas)</button>
      <button class="btn btn-all-time" onclick="timeAll(2)">VSETKO DOLE (Cas)</button>
      <button class="btn btn-all" id="btn-allhold-H" onmousedown="holdAll(1,1)" onmouseup="holdAll(1,0)" ontouchstart="holdAll(1,1)" ontouchend="holdAll(1,0)">VSETKO HORE (Hold)</button>
      <button class="btn btn-all" id="btn-allhold-D" onmousedown="holdAll(2,1)" onmouseup="holdAll(2,0)" ontouchstart="holdAll(2,1)" ontouchend="holdAll(2,0)">VSETKO DOLE (Hold)</button>
    </div>
    <button class="btn btn-stop" onclick="stopAll()">STOP VSETKO</button>
  </div>

  <div class="card">
    <h3>Samostatne ovladanie a predvolby naklapania</h3>
    <div id="solo-controls"></div>
  </div>

  <div class="card">
    <h3>Ovladanie Osvetlenia Terasy (Dualne Okruhy)</h3>
    <div class="row">
      <span><b>Svetlo 1: Hlavne</b> (Bit 7, PCF2)</span>
      <div>
        <button class="btn btn-up" onclick="toggleLight(1,1)">ZAPNUT</button>
        <button class="btn btn-down" onclick="toggleLight(1,0)">VYPNUT</button>
      </div>
    </div>
    <div class="row">
      <span>Casovac Svetla 1:</span>
      <div>
        <input type="number" id="inp-light-timer" class="inp-w" min="0" max="240" placeholder="Min">
        <button class="btn btn-all-time" onclick="setLightTimer(1)">Spustit a Zapnut</button>
      </div>
    </div>
    <hr style="border:0; border-top:1px dashed #333; margin:15px 0;">
    <div class="row">
      <span><b>Svetlo 2: Pomocne</b> (Bit 7, PCF1)</span>
      <div>
        <button class="btn btn-up" onclick="toggleLight(2,1)">ZAPNUT</button>
        <button class="btn btn-down" onclick="toggleLight(2,0)">VYPNUT</button>
      </div>
    </div>
    <div class="row">
      <span>Casovac Svetla 2:</span>
      <div>
        <input type="number" id="inp-light2-timer" class="inp-w" min="0" max="240" placeholder="Min">
        <button class="btn btn-all-time" onclick="setLightTimer(2)">Spustit a Zapnut</button>
      </div>
    </div>
  </div>

  <div class="card">
    <h3>Grafy Historia Snimacov</h3>
    <div class="chart-container"><canvas id="tempChart"></canvas></div>
    <div class="chart-container"><canvas id="lightChart"></canvas></div>
    <div class="chart-container"><canvas id="windChart"></canvas></div>
  </div>

  <div class="card" style="overflow-x:auto;">
    <h3>Konfiguracia automatiky a poloh zaluzii</h3>
    <div class="row">
      <span>Globalne povolenie automatiky:</span>
      <select id="gaute" onchange="saveGlobalAuto()" class="inp-w" style="width:100px;">
        <option value="1">Povolena</option>
        <option value="0">Zakazana</option>
      </select>
    </div>
    
    <div class="row" style="margin-top:8px;">
      <span>Casove okno pre fungovanie automatiky (Hodiny):</span>
      <div>
        Od: <input type="number" id="hstart" class="inp-w" min="0" max="23" onchange="saveGlobalAuto()" style="width:50px;">
        Do: <input type="number" id="hend" class="inp-w" min="0" max="23" onchange="saveGlobalAuto()" style="width:50px;">
      </div>
    </div>
    
    <h4>Casy uplneho chodu (sekundy)</h4>
    <div id="times-inputs"></div>

    <h4>Kalibracia vychodzieho bodu 0 (Zero Offset)</h4>
    <div id="zero-offset-inputs"></div>

    <h4>Preddefinovane polohy naklapania (Rozsah 0 - 130000)</h4>
    <div id="presets-autom"></div>
    
    <h4>Teplotna automatika</h4>
    <div id="temp-autom"></div>

    <h4>Svetelna automatika</h4>
    <div id="light-autom"></div>

    <h4>Dvojfazova automatika pre Svetlo</h4>
    <div id="seq-light-autom"></div>

    <h4>Dvojfazova automatika pre Teplotu</h4>
    <div id="seq-temp-autom"></div>

    <h4>Casova automatika naklapania polohy (P1 - P8)</h4>
    <div id="time-tilt-autom"></div>

    <h3>Kalibracia a filtre systemu</h3>
    <div class="row"><span>Tolerancia priblizenia k cielu:</span><span><input type="number" id="mcth" class="inp-w" onchange="saveCalib()"> bodov</span></div>

    <h4>Nastavenie vonkajsieho tlacidla (Kanal 1)</h4>
    <div class="row"><span>Trvanie chodu HORE (tlacidlo):</span><span><input type="number" id="etu1" class="inp-w" onchange="saveCalib()"> s</span></div>
    <div class="row"><span>Trvanie chodu DOLE (tlacidlo):</span><span><input type="number" id="etd1" class="inp-w" onchange="saveCalib()"> s</span></div>

    <h4>Anemometer (Vietor)</h4>
    <div class="row"><span>Limit vetra pre poplach:</span><span><input type="number" id="wlim" class="inp-w" onchange="saveCalib()"> km/h</span></div>
    <div class="row"><span>Impulzy pre 25 km/h:</span><span><input type="number" id="wp25" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Pocet impulzov na otacku:</span><span><input type="number" id="ppturn" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Trvanie blokovania poplachu:</span><span><input type="number" id="wsafedur" class="inp-w" onchange="saveCalib()"> s</span></div>
    <div class="row"><span>Cas chodu HORE po vetre:</span><span><input type="number" id="wruntime" class="inp-w" onchange="saveCalib()"> s</span></div>
    <div class="row"><span>Pocet kritickych pulzov:</span><span><input type="number" id="wmaxhits" class="inp-w" onchange="saveCalib()"></span></div>

    <h4>Filter Svetla (LDR)</h4>
    <div class="row"><span>Okno klzaveho filtra LDR:</span><span><input type="number" id="ldrsmp" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Koeficient filtra LDR:</span><span><input type="number" step="0.01" id="ldrflt" class="inp-w" onchange="saveCalib()"></span></div>

    <h4>Filter a kalibracia NTC - Vnutorny</h4>
    <div class="row"><span>NTC Odpor pri 25C (R0):</span><span><input type="number" id="ntciR0" class="inp-w" onchange="saveCalib()"> Ohm</span></div>
    <div class="row"><span>NTC Beta koeficient:</span><span><input type="number" id="ntciB" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Seriovy odpor:</span><span><input type="number" id="ntciSR" class="inp-w" onchange="saveCalib()"> Ohm</span></div>
    <div class="row"><span>Okno klzaveho filtra:</span><span><input type="number" id="ntciSmp" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Koeficient filtra:</span><span><input type="number" step="0.01" id="ntciFlt" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Teplotny offset:</span><span><input type="number" step="0.1" id="ntciOff" class="inp-w" onchange="saveCalib()"> C</span></div>

    <h4>Filter a kalibracia NTC - Vonkajsi</h4>
    <div class="row"><span>NTC Odpor pri 25C (R0):</span><span><input type="number" id="ntceR0" class="inp-w" onchange="saveCalib()"> Ohm</span></div>
    <div class="row"><span>NTC Beta koeficient:</span><span><input type="number" id="ntceB" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Seriovy odpor:</span><span><input type="number" id="ntceSR" class="inp-w" onchange="saveCalib()"> Ohm</span></div>
    <div class="row"><span>Okno klzaveho filtra:</span><span><input type="number" id="ntceSmp" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Koeficient filtra:</span><span><input type="number" step="0.01" id="ntceFlt" class="inp-w" onchange="saveCalib()"> C</span></div>
    <div class="row"><span>Teplotny offset:</span><span><input type="number" step="0.1" id="ntceOff" class="inp-w" onchange="saveCalib()"> C</span></div>
  </div>
</div>

<script>
const chartColors = { text: '#ffffff', grid: 'rgba(255, 255, 255, 0.06)', intTemp: '#ff4d4d', extTemp: '#33ccff', light: '#ffcc00', wind: '#00adb5' };
const showSaved = () => { const b = document.getElementById("sb"); b.style.display = "block"; setTimeout(() => { b.style.display="none"; }, 2000); };

let lockoutCounter = 0;
const toggleLight = (id, val) => { fetch('/light?id=' + id + '&s=' + val); };
const setLightTimer = (id) => { 
  let m = (id === 1) ? document.getElementById("inp-light-timer").value : document.getElementById("inp-light2-timer").value;
  fetch('/lighttimer?id=' + id + '&m=' + m); 
};
const triggerSeq = (i, type) => { fetch('/seqtrigger?i=' + i + '&t=' + type).then(showSaved); };
const holdSolo = (i, d, val) => { fetch('/hold?i=' + i + '&d=' + d + '&v=' + val); };
const timeSolo = (i, d) => { fetch('/time?i=' + i + '&d=' + d); };
const setPreset = (i, p) => { fetch('/setpreset?i=' + i + '&p=' + p); };
const holdAll = (d, val) => { fetch('/holdall?d=' + d + '&v=' + val); };
const timeAll = (d) => { fetch('/timeall?d=' + d); };
const stopAll = () => { fetch('/stop'); };
const resetMinMax = (type) => { fetch('/resetminmax?type=' + type); };
const resetWindMax = () => { fetch('/resetwindmax'); };
const zeroBlind = (i) => { if(confirm("Naozaj resetovat vychodzi bod zaluzie "+(i+1)+" na 0?")) fetch('/zero?i='+i).then(showSaved); };
const saveGlobalAuto = () => { lockoutCounter = 5; fetch('/saveglobalauto?val=' + document.getElementById("gaute").value + '&hstart=' + document.getElementById("hstart").value + '&hend=' + document.getElementById("hend").value).then(showSaved); };

const saveRow = (i) => {
  lockoutCounter = 5;
  let url = '/saverow?i='+i+'&tu='+document.getElementById("tu"+i).value+'&td='+document.getElementById("td"+i).value+'&zOff='+document.getElementById("zOff"+i).value;
  url += '&ate='+(document.getElementById("ate"+i).checked?1:0)+'&tmd='+document.getElementById("tmd"+i).value+'&tlu='+document.getElementById("tlu"+i).value+'&tta='+document.getElementById("tta"+i).value+'&tld='+document.getElementById("tld"+i).value+'&ttd='+document.getElementById("ttd"+i).value+'&thyst='+document.getElementById("thyst"+i).value;
  url += '&ale='+(document.getElementById("ale"+i).checked?1:0)+'&lmd='+document.getElementById("lmd"+i).value+'&llu='+document.getElementById("llu"+i).value+'&lta='+document.getElementById("lta"+i).value+'&lld='+document.getElementById("lld"+i).value+'&ltd='+document.getElementById("ltd"+i).value+'&lhyst='+document.getElementById("lhyst"+i).value;
  url += '&sqle='+(document.getElementById("sqle"+i).checked?1:0)+'&sqlp='+document.getElementById("sqlp"+i).value+'&sqld='+document.getElementById("sqld"+i).value+'&sqlt='+document.getElementById("sqlt"+i).value;
  url += '&sqte='+(document.getElementById("sqte"+i).checked?1:0)+'&sqtp='+document.getElementById("sqtp"+i).value+'&sqtd='+document.getElementById("sqtd"+i).value+'&sqtt='+document.getElementById("sqtt"+i).value;
  for(let p=0; p<8; p++) {
    url += '&p'+p+'='+document.getElementById('prst-'+i+'-'+p).value;
  }
  fetch(url).then(showSaved);
};

const saveCalib = () => {
  lockoutCounter = 5; 
  let url = '/savecalib?wlim='+document.getElementById("wlim").value+'&wp25='+document.getElementById("wp25").value+'&ppturn='+document.getElementById("ppturn").value+'&wsafedur='+document.getElementById("wsafedur").value+'&wruntime='+document.getElementById("wruntime").value+'&wmaxhits='+document.getElementById("wmaxhits").value+'&mcth='+document.getElementById("mcth").value+'&etu1='+document.getElementById("etu1").value+'&etd1='+document.getElementById("etd1").value;
  url += '&ldrsmp='+document.getElementById("ldrsmp").value+'&ldrflt='+document.getElementById("ldrflt").value;
  url += '&ntciR0='+document.getElementById("ntciR0").value+'&ntciB='+document.getElementById("ntciB").value+'&ntciSR='+document.getElementById("ntciSR").value+'&ntciSmp='+document.getElementById("ntciSmp").value+'&ntciFlt='+document.getElementById("ntciFlt").value+'&ntciOff='+document.getElementById("ntciOff").value;
  url += '&ntceR0='+document.getElementById("ntceR0").value+'&ntceB='+document.getElementById("ntceB").value+'&ntceSR='+document.getElementById("ntceSR").value+'&ntceSmp='+document.getElementById("ntceSmp").value+'&ntceFlt='+document.getElementById("ntceFlt").value+'&ntceOff='+document.getElementById("ntceOff").value;
  fetch(url).then(showSaved);
};

const buildUI = () => {
  let solosHtml = "";
  for(let i=0; i<6; i++) {
    solosHtml += `
    <div class="blind-row">
      <strong>Zaluzia ${i+1}</strong> (Poloha: <span id="pos-val-${i}" style="font-weight:bold; color:#ffffff;">0</span>)
      <span id="stat-auto-${i}" style="margin-left:12px; font-size:11px; font-weight:bold; background:#111111; color:#f1c40f; padding:2px 6px; border-radius:3px; border:1px solid #333;">--</span>
      <div style="margin-top:5px; display:flex; justify-content:space-between; flex-wrap:wrap;">
        <button class="btn btn-up btn-solo-main" id="btn-time-H-${i}" onclick="timeSolo(${i},1)">Z${i+1} HORE<span class="btn-timer" id="timer-TH-${i}"></span></button>
        <button class="btn btn-down btn-solo-main" id="btn-time-D-${i}" onclick="timeSolo(${i},2)">Z${i+1} DOLE<span class="btn-timer" id="timer-TD-${i}"></span></button>
        <button class="btn btn-up btn-solo-main" style="background:#1e7e34;" id="btn-hold-H-${i}" onmousedown="holdSolo(${i},1,1)" onmouseup="holdSolo(${i},1,0)" ontouchstart="holdSolo(${i},1,1)" ontouchend="holdSolo(${i},1,0)">Z${i+1} HORE (Hold)</button>
        <button class="btn btn-down btn-solo-main" style="background:#0062cc;" id="btn-hold-D-${i}" onmousedown="holdSolo(${i},2,1)" onmouseup="holdSolo(${i},2,0)" ontouchstart="holdSolo(${i},2,1)" ontouchend="holdSolo(${i},2,0)">Z${i+1} DOLE (Hold)</button>
      </div>
      <div style="margin-top:5px; display:flex; justify-content:space-between; flex-wrap:wrap; width:100%;">
        <span style="font-size:11px; align-self:center; margin-right:5px;">Naklopenie:</span>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},0)">P1</button>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},1)">P2</button>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},2)">P3</button>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},3)">P4</button>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},4)">P5</button>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},5)">P6</button>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},6)">P7</button>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},7)">P8</button>
      </div>
    </div>`;
  }
  document.getElementById("solo-controls").innerHTML = solosHtml;

  let timesHtml = '<div class="conf-grid" style="font-weight:bold;"><div>Zal.</div><div>Cas HORE</div><div>Cas DOLE</div><div>-</div><div>-</div><div>-</div><div>-</div></div>';
  for(let i=0; i<6; i++) {
    timesHtml += `<div class="conf-grid"><div>Z${i+1}</div><div><input type="number" id="tu${i}" onchange="saveRow(${i})"></div><div><input type="number" id="td${i}" onchange="saveRow(${i})"></div><div></div><div></div><div></div><div></div></div>`;
  }
  document.getElementById("times-inputs").innerHTML = timesHtml;

  let zeroHtml = '<div class="conf-grid" style="font-weight:bold;"><div>Zal.</div><div>Hodnota 0</div><div>Servisny Nulovaci Povely</div><div>-</div><div>-</div><div>-</div><div>-</div></div>';
  for(let i=0; i<6; i++) {
    zeroHtml += `<div class="conf-grid"><div>Z${i+1}</div><div><input type="number" id="zOff${i}" onchange="saveRow(${i})"></div><div><button class="btn" style="background:#d35400; font-size:10px; padding:4px 8px; flex:1;" onclick="zeroBlind(${i})">Nulovac</button></div><div></div><div></div><div></div><div></div></div>`;
  }
  document.getElementById("zero-offset-inputs").innerHTML = zeroHtml;

  let presetHtml = '<div class="preset-grid" style="font-weight:bold;"><div>Zal.</div><div>P1</div><div>P2</div><div>P3</div><div>P4</div><div>P5</div><div>P6</div><div>P7</div><div>P8</div></div>';
  for(let i=0; i<6; i++) {
    presetHtml += `<div class="preset-grid"><div>Z${i+1}</div>`;
    for(let p=0; p<8; p++) {
      presetHtml += `<div><input type="number" id="prst-${i}-${p}" onchange="saveRow(${i})"></div>`;
    }
    presetHtml += `</div>`;
  }
  document.getElementById("presets-autom").innerHTML = presetHtml;

  let tempHtml = '<div class="conf-grid" style="font-weight:bold;"><div>Zal.</div><div>Zap</div><div>Mod</div><div>Lim.HORE</div><div>Cas HORE</div><div>Lim.DOLE</div><div>Cas DOLE</div><div>Hyst</div></div>';
  for(let i=0; i<6; i++) {
    tempHtml += `<div class="conf-grid"><div>Z${i+1}</div><div><input type="checkbox" id="ate${i}" onchange="saveRow(${i})" style="width:18px;height:18px;"></div><div><select id="tmd${i}" onchange="saveRow(${i})"><option value="0">OFF</option><option value="1">Vnitra</option><option value="2">Vonka</option></select></div><div><input type="number" step="0.1" id="tlu${i}" onchange="saveRow(${i})"></div><div><input type="number" id="tta${i}" onchange="saveRow(${i})"></div><div><input type="number" step="0.1" id="tld${i}" onchange="saveRow(${i})"></div><div><input type="number" id="ttd${i}" onchange="saveRow(${i})"></div><div><input type="number" step="0.1" id="thyst${i}" onchange="saveRow(${i})"></div></div>`;
  }
  document.getElementById("temp-autom").innerHTML = tempHtml;

  let lightHtml = '<div class="conf-grid" style="font-weight:bold;"><div>Zal.</div><div>Zap</div><div>Mod</div><div>Lim.HORE</div><div>Cas HORE</div><div>Lim.DOLE</div><div>Cas DOLE</div><div>Hyst</div></div>';
  for(let i=0; i<6; i++) {
    lightHtml += `<div class="conf-grid"><div>Z${i+1}</div><div><input type="checkbox" id="ale${i}" onchange="saveRow(${i})" style="width:18px;height:18px;"></div><div><select id="lmd${i}" onchange="saveRow(${i})"><option value="0">OFF</option><option value="1">Automatika</option></select></div><div><input type="number" id="llu${i}" onchange="saveRow(${i})"></div><div><input type="number" id="lta${i}" onchange="saveRow(${i})"></div><div><input type="number" id="lld${i}" onchange="saveRow(${i})"></div><div><input type="number" id="ltd${i}" onchange="saveRow(${i})"></div><div><input type="number" id="lhyst${i}" onchange="saveRow(${i})"></div></div>`;
  }
  document.getElementById("light-autom").innerHTML = lightHtml;

  let seqLightHtml = '<div class="conf-grid" style="font-weight:bold;"><div>Zal.</div><div>Zap</div><div>Pauza</div><div>Smer2</div><div>Cas2</div><div>Manual</div><div>-</div></div>';
  for(let i=0; i<6; i++) {
    seqLightHtml += `<div class="conf-grid"><div>Z${i+1}</div><div><input type="checkbox" id="sqle${i}" onchange="saveRow(${i})" style="width:20px;height:20px;"></div><div><input type="number" step="0.1" id="sqlp${i}" onchange="saveRow(${i})"></div><div><select id="sqld${i}" onchange="saveRow(${i})"><option value="1">HORE</option><option value="2">DOLE</option></select></div><div><input type="number" step="0.1" id="sqlt${i}" onchange="saveRow(${i})"></div><div><button class="btn" style="background:#555; font-size:10px; padding:4px 6px;" onclick="triggerSeq(${i},1)">Trigger</button></div><div>-</div></div>`;
  }
  document.getElementById("seq-light-autom").innerHTML = seqLightHtml;

  let seqTempHtml = '<div class="conf-grid" style="font-weight:bold;"><div>Zal.</div><div>Zap</div><div>Pauza</div><div>Smer2</div><div>Cas2</div><div>Manual</div><div>-</div></div>';
  for(let i=0; i<6; i++) {
    seqTempHtml += `<div class="conf-grid"><div>Z${i+1}</div><div><input type="checkbox" id="sqte${i}" onchange="saveRow(${i})" style="width:20px;height:20px;"></div><div><input type="number" step="0.1" id="sqtp${i}" onchange="saveRow(${i})"></div><div><select id="sqtd${i}" onchange="saveRow(${i})"><option value="1">HORE</option><option value="2">DOLE</option></select></div><div><input type="number" step="0.1" id="sqtt${i}" onchange="saveRow(${i})"></div><div><button class="btn" style="background:#555; font-size:10px; padding:4px 6px;" onclick="triggerSeq(${i},2)">Trigger</button></div><div>-</div></div>`;
  }
  document.getElementById("seq-temp-autom").innerHTML = seqTempHtml;

  let timeTiltHtml = `
  <div class="row">
    <span>Vyber roletu:</span>
    <select id="sel-tt-blind" class="inp-w" onchange="loadTimeTiltFields()" style="width:110px;">
      <option value="0">Zaluzia 1</option>
      <option value="1">Zaluzia 2</option>
      <option value="2">Zaluzia 3</option>
      <option value="3">Zaluzia 4</option>
      <option value="4">Zaluzia 5</option>
      <option value="5">Zaluzia 6</option>
    </select>
  </div>
  <div class="row">
    <span>Pouzit časovú automatiku naklápania pre tuto roletu:</span>
    <input type="checkbox" id="utt_input" onchange="saveTimeTiltRow()" style="width:22px;height:22px;">
  </div>
  <hr style="border:0; border-top:1px dashed #333; margin:10px 0;">
  <div class="row">
    <span>Vyber predvoľbu (P1 - P8) pre nastavenie časov:</span>
    <select id="sel-tt-preset" class="inp-w" onchange="loadTimeTiltFields()" style="width:110px;">
      <option value="0">P1</option>
      <option value="1">P2</option>
      <option value="2">P3</option>
      <option value="3">P4</option>
      <option value="4">P5</option>
      <option value="5">P6</option>
      <option value="6">P7</option>
      <option value="7">P8</option>
    </select>
  </div>
  <div class="conf-grid" style="font-weight:bold; grid-template-columns: repeat(5, 1fr); margin-top:10px;">
    <div>Smer 1</div><div>Cas 1 (s)</div><div>Pauza (s)</div><div>Smer 2</div><div>Cas 3 (s)</div>
  </div>
  <div class="conf-grid" style="grid-template-columns: repeat(5, 1fr);">
    <div><select id="ttd1_input" onchange="saveTimeTiltRow()"><option value="1">HORE</option><option value="2">DOLE</option></select></div>
    <div><input type="number" step="0.05" id="ttt1_input" onchange="saveTimeTiltRow()"></div>
    <div><input type="number" step="0.05" id="ttp_input" onchange="saveTimeTiltRow()"></div>
    <div><select id="ttd2_input" onchange="saveTimeTiltRow()"><option value="1">HORE</option><option value="2">DOLE</option></select></div>
    <div><input type="number" step="0.05" id="ttt2_input" onchange="saveTimeTiltRow()"></div>
  </div>`;
  document.getElementById("time-tilt-autom").innerHTML = timeTiltHtml;
};

let tChart, lChart, wChart;
const initCharts = (labels, dataInt, dataExt, dataLight, dataWind) => {
  const commonOptions = { responsive: true, maintainAspectRatio: false, scales: { xAxes: [{ gridLines: { color: chartColors.grid }, ticks: { fontColor: chartColors.text } }], yAxes: [{ gridLines: { color: chartColors.grid }, ticks: { fontColor: chartColors.text } }] }, legend: { labels: { fontColor: chartColors.text } } };
  tChart = new Chart(document.getElementById('tempChart').getContext('2d'), { type: 'line', data: { labels: labels, datasets: [{ label: 'Vnutorna Teplota (C)', data: dataInt, borderColor: chartColors.intTemp, backgroundColor: 'rgba(255,77,77,0.1)', borderWidth: 2, fill: false }, { label: 'Vonkajsia Teplota (C)', data: dataExt, borderColor: chartColors.extTemp, backgroundColor: 'rgba(51,204,255,0.1)', borderWidth: 2, fill: false }] }, options: commonOptions });
  lChart = new Chart(document.getElementById('lightChart').getContext('2d'), { type: 'line', data: { labels: labels, datasets: [{ label: 'Intenzita svetla (0-1000)', data: dataLight, borderColor: chartColors.light, backgroundColor: 'rgba(255,204,0,0.1)', borderWidth: 2, fill: false }] }, options: commonOptions });
  wChart = new Chart(document.getElementById('windChart').getContext('2d'), { type: 'line', data: { labels: labels, datasets: [{ label: 'Rychlost vetra (km/h)', data: dataWind, borderColor: chartColors.wind, backgroundColor: 'rgba(0,173,181,0.1)', borderWidth: 2, fill: false }] }, options: commonOptions });
};

const updateCharts = (labels, dataInt, dataExt, dataLight, dataWind) => {
  if(!tChart) { initCharts(labels, dataInt, dataExt, dataLight, dataWind); return; }
  tChart.data.labels = labels; tChart.data.datasets[0].data = dataInt; tChart.data.datasets[1].data = dataExt; tChart.update();
  lChart.data.labels = labels; lChart.data.datasets[0].data = dataLight; lChart.update();
  wChart.data.labels = labels; wChart.data.datasets[0].data = dataWind; wChart.update();
};

let global_tt_data = { utt: [], ttd1: [], ttt1: [], ttp: [], ttd2: [], ttt2: [] };
const loadTimeTiltFields = () => {
  if (!global_tt_data.utt || global_tt_data.utt.length === 0) return;
  let i = parseInt(document.getElementById("sel-tt-blind").value);
  let p = parseInt(document.getElementById("sel-tt-preset").value);
  
  document.getElementById("utt_input").checked = (global_tt_data.utt[i] == "1");
  document.getElementById("ttd1_input").value = global_tt_data.ttd1[i][p];
  document.getElementById("ttt1_input").value = global_tt_data.ttt1[i][p].toFixed(2);
  document.getElementById("ttp_input").value = global_tt_data.ttp[i][p].toFixed(2);
  document.getElementById("ttd2_input").value = global_tt_data.ttd2[i][p];
  document.getElementById("ttt2_input").value = global_tt_data.ttt2[i][p].toFixed(2);
};

const saveTimeTiltRow = () => {
  lockoutCounter = 5;
  let i = document.getElementById("sel-tt-blind").value;
  let p = document.getElementById("sel-tt-preset").value;
  let utt = document.getElementById("utt_input").checked ? 1 : 0;
  let d1 = document.getElementById("ttd1_input").value;
  let t1 = document.getElementById("ttt1_input").value;
  let pz = document.getElementById("ttp_input").value;
  let d2 = document.getElementById("ttd2_input").value;
  let t2 = document.getElementById("ttt2_input").value;

  global_tt_data.utt[i] = utt.toString();
  global_tt_data.ttd1[i][p] = parseInt(d1);
  global_tt_data.ttt1[i][p] = parseFloat(t1);
  global_tt_data.ttp[i][p] = parseFloat(pz);
  global_tt_data.ttd2[i][p] = parseInt(d2);
  global_tt_data.ttt2[i][p] = parseFloat(t2);

  let url = `/savetimetilt?i=${i}&p=${p}&utt=${utt}&d1=${d1}&t1=${t1}&pz=${pz}&d2=${d2}&t2=${t2}`;
  fetch(url).then(showSaved);
};

const updateStatus = () => {
  const ae = document.activeElement;
  if (ae && (ae.tagName === "INPUT" || ae.tagName === "SELECT")) return;
  fetch('/status').then(r => r.json()).then(c => {
    const txt = (id, val) => { const el = document.getElementById(id); if (el) el.innerText = val; };
    const val = (id, val) => { const el = document.getElementById(id); if (el) el.value = val; };
    const chk = (id, val) => { const el = document.getElementById(id); if (el) el.checked = val; };

    txt("ti", c.ti.toFixed(1)); txt("te", c.te.toFixed(1));
    txt("timn", c.timn.toFixed(1)); txt("timx", c.timx.toFixed(1));
    txt("temn", c.temn.toFixed(1)); txt("temx", c.temx.toFixed(1));
    txt("ws", c.ws.toFixed(1)); txt("wsmx", c.wsmx.toFixed(1)); txt("wsmx24", c.wsmx24.toFixed(1));
    txt("lt", c.lt); txt("rssi", c.rssi); txt("clock", c.clk);
    
    const s1 = document.getElementById("btn-light-state"); if(s1) { s1.innerText = c.light ? "S1: ZAPNUTE" : "S1: VYPNUTE"; s1.style.background = c.light ? "#d35400" : "#2c3e50"; if(c.lrem > 0) s1.innerText += " (" + c.lrem + "m)"; }
    const s2 = document.getElementById("btn-light2-state"); if(s2) { s2.innerText = c.l2 ? "S2: ZAPNUTE" : "S2: VYPNUTE"; s2.style.background = c.l2 ? "#d35400" : "#2c3e50"; if(c.l2rem > 0) s2.innerText += " (" + c.l2rem + "m)"; }

    global_tt_data.utt = c.utt;
    global_tt_data.ttd1 = c.ttd1;
    global_tt_data.ttt1 = c.ttt1;
    global_tt_data.ttp = c.ttp;
    global_tt_data.ttd2 = c.ttd2;
    global_tt_data.ttt2 = c.ttt2;
    
    if (document.getElementById("sel-tt-blind")) { loadTimeTiltFields(); }
    if (lockoutCounter > 0) { lockoutCounter--; return; }
    
    val("gaute", c.gaute); val("hstart", c.hst); val("hend", c.hnd);
    const alarmEl = document.getElementById("alarm"); if (alarmEl) alarmEl.style.display = (c.safety == "1") ? "block" : "none";
    
    for(let i=0; i<6; i++) {
      val("tu"+i, c.tu[i]); val("td"+i, c.td[i]); val("zOff"+i, c.zOff[i]);
      chk("ate"+i, c.ate[i] == "1"); val("tmd"+i, c.tmd[i]);
      val("tlu"+i, c.tlu[i]); val("tta"+i, c.tta[i]); val("tld"+i, c.tld[i]);
      val("ttd"+i, c.ttd[i]); val("thyst"+i, c.thyst[i]);
      chk("ale"+i, c.ale[i] == "1"); val("lmd"+i, c.lmd[i]); val("llu"+i, c.llu[i]);
      val("lta"+i, c.lta[i]); val("lld"+i, c.lld[i]); val("ltd"+i, c.ltd[i]); val("lhyst"+i, c.lhyst[i]);
      chk("sqle"+i, c.sqle[i] == "1"); val("sqlp"+i, c.sqlp[i]); val("sqld"+i, c.sqld[i]); val("sqlt"+i, c.sqlt[i]);
      chk("sqte"+i, c.sqte[i] == "1"); val("sqtp"+i, c.sqtp[i]); val("sqtd"+i, c.sqtd[i]); val("sqtt"+i, c.sqtt[i]);

      for(let p=0; p<8; p++) { val('prst-'+i+'-'+p, c.prst[i][p]); }
      
      txt("pos-val-"+i, c.pos[i]);
      let bhH = document.getElementById("btn-hold-H-"+i);
      let bhD = document.getElementById("btn-hold-D-"+i);
      let btH = document.getElementById("btn-time-H-"+i); let btD = document.getElementById("btn-time-D-"+i);
      let timTH = document.getElementById("timer-TH-"+i); let timTD = document.getElementById("timer-TD-"+i);
      if(bhH) bhH.classList.remove("btn-active-hold"); if(bhD) bhD.classList.remove("btn-active-hold");
      if(btH) { btH.classList.remove("btn-active-hore"); if(timTH) timTH.style.display="none"; }
      if(btD) { btD.classList.remove("btn-active-dole"); if(timTD) timTD.style.display="none"; }
      
      if (c.rDir[i] == "1") { if (c.whH[i] == "1") { if(bhH) bhH.classList.add("btn-active-hold"); } else { if(btH) { btH.classList.add("btn-active-hore"); if(timTH) { timTH.style.display="inline-block"; timTH.innerText = c.rem[i] + "s"; } } } }
      else if (c.rDir[i] == "2") { if (c.whD[i] == "1") { if(bhD) bhD.classList.add("btn-active-hold"); } else { if(btD) { btD.classList.add("btn-active-dole"); if(timTD) { timTD.style.display="inline-block"; timTD.innerText = c.rem[i] + "s"; } } } }
      
      let stA = document.getElementById("stat-auto-"+i);
      if(stA) {
        if(c.as[i] == 1) stA.innerHTML = '<span style="color:#2ecc71;">Teplota</span>';
        else if(c.as[i] == 2) stA.innerHTML = '<span style="color:#2ecc71;">Svetlo</span>';
        else if(c.as[i] == 3) stA.innerHTML = '<span style="color:#f39c12;">Pauza 2.F</span>';
        else if(c.as[i] == 4) stA.innerHTML = '<span style="color:#2ecc71;">2.Faza</span>';
        else stA.innerHTML = '<span style="color:#7f8c8d;">OFF</span>';
      }
    }
    if(firstLoad) { 
      val("mcth", c.mcth); val("wlim", c.wlim); val("wp25", c.wp25); val("ppturn", c.ppturn); val("wsafedur", Math.round(c.wsafedur/1000)); val("wruntime", c.wruntime); val("wmaxhits", c.wmaxhits);
      val("etu1", c.etu1); val("etd1", c.etd1); val("ldrsmp", c.ldrs); val("ldrflt", c.ldrf);
      val("ntciR0", c.ntciR0); val("ntciB", c.ntciB); val("ntciSR", c.ntciSR); val("ntciSmp", c.ntciSmp); val("ntciFlt", c.ntciFlt); val("ntciOff", c.ntciOff);
      val("ntceR0", c.ntceR0); val("ntceB", c.ntceB); val("ntceSR", c.ntceSR); val("ntceSmp", c.ntceSmp); val("ntceFlt", c.ntceFlt); val("ntceOff", c.ntceOff);
      firstLoad = false;
    }
    if(c.labels && c.labels.length > 0) { updateCharts(c.labels, c.histInt, c.gExt, c.histLight, c.histWind); }
  });
};
let firstLoad = true; 
window.onload = () => { buildUI(); updateStatus(); setInterval(updateStatus, 1500); };
</script>
</body>
</html>
)rawliteral";

void IRAM_ATTR countPulse() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();
  if (interruptTime - lastInterruptTime > 15) { 
    pulseCount++;
  }
  lastInterruptTime = interruptTime;
}

String getClockTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return String("--:--");
  }
  char timeStringBuff[10];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M", &timeinfo);
  return String(timeStringBuff);
}

void smtpCallback(SMTP_Status status) {
  Serial.println(status.info());
}

void sendWindAlarmEmail(float currentWind, float currentLimit) {
  Session_Config config;
  config.server.host_name = SMTP_HOST;
  config.server.port = SMTP_PORT;
  config.login.email = AUTHOR_EMAIL;
  config.login.password = AUTHOR_PASSWORD;
  config.login.user_domain = "";

  SMTP_Message message;
  message.sender.name = "Smart Terasa Zaluzie";
  message.sender.email = AUTHOR_EMAIL;
  message.subject = "=?UTF-8?B?4pyoIEFMQVJNOiDFvWEx+nppZSB2eXRpYWhudXTDqSAtIFNpbG7DvSB2aWV0b3Ih?=";
  message.addRecipient("Pouzivatel", RECIPIENT_EMAIL);

  String htmlMsg = "<div style='font-family:Arial,sans-serif;border:2px solid #ff4444;padding:15px;border-radius:8px;background-color:#1e1e1e;color:#e0e0e0;'>";
  htmlMsg += "<h2 style='color:#ff4444;margin-top:0;'>ALARM: VETERNY POPLACH - AKTIVACIA OCHRANY</h2>";
  htmlMsg += "<p>Automaticky system ochrany detegoval nebezpecne zvysenie rychlosti vetra.</p>";
  htmlMsg += "<h3>METEO A SENSORICKA DIAGNOSTIKA:</h3><ul>";
  htmlMsg += "<li><b>Namerana rychlost vetra:</b> <span style='color:#ff4444;font-weight:bold;'>" + String(currentWind, 1) + " km/h</span></li>";
  htmlMsg += "<li><b>Nastaveny limit ochrany:</b> " + String(currentLimit, 1) + " km/h</li>";
  htmlMsg += "<li><b>Cas incidentu v systeme:</b> " + getClockTime() + "</li>";
  htmlMsg += "<li><b>Vnutorna teplota terasy:</b> " + String(tempInt, 1) + " C</li>";
  htmlMsg += "<li><b>Vonkajsia teplota prostredia:</b> " + String(tempExt, 1) + " C</li>";
  htmlMsg += "<li><b>Senzor jasu (LDR):</b> " + String(lightLevel) + " bodov</li>";
  htmlMsg += "<li><b>Sila Wi-Fi signalu (RSSI):</b> " + String(wifiRSSI) + " dBm</li>";
  htmlMsg += "</ul>";
  htmlMsg += "<h3>POLOHY ROLET V MOMENTE INCIDENTU:</h3><ul>";
  for(int i=0; i<6; i++) {
    htmlMsg += "<li><b>Zaluzia " + String(i+1) + ":</b> " + String(getEffectivePosition(i)) + " bodov (Zero Offset: " + String(zeroOffset[i]) + ")</li>";
  }
  htmlMsg += "</ul>";
  htmlMsg += "<p style='background:#721c24;padding:12px;border-radius:4px;color:#f8d7da;font-weight:bold;'>STAV AKCIE: Vsetkych 6 zaluzii bolo kaskadovito vytiahnutych do HORNEJ pozicie (0). Ovladani blokavane na 60 sekund.</p>";
  htmlMsg += "</div>";
  
  message.html.content = htmlMsg.c_str();
  message.html.charSet = "utf-8";
  message.html.transfer_encoding = "7bit";
  message.priority = esp_mail_smtp_priority_normal;
  if (!smtp.connect(&config)) {
    Serial.println("Zlyhalo pripojenie k SMTP serveru.");
    return;
  }
  MailClient.sendMail(&smtp, &message, false);
}

void checkAndSaveMinMax() {
  if (millis() - bootMillis < 45000) { 
    return;
  }
  if (tempInt > -50.0 && tempInt < 100.0) {
    if (tempInt < tempIntMin) { tempIntMin = tempInt; }
    if (tempInt > tempIntMax) { tempIntMax = tempInt; }
  }
  if (tempExt > -50.0 && tempExt < 100.0) {
    if (tempExt < tempExtMin) { tempExtMin = tempExt; }
    if (tempExt > tempExtMax) { tempExtMax = tempExt; }
  }
}

float readNTCRolling(int pin, float r0, float beta, float seriesR, int samples, float filterCoeff, float offset, float lastValidTemp, float* buffer, int& bufCount) {
  int raw = analogRead(pin);
  if (raw >= 4095 || raw <= 0) return lastValidTemp;
  
  float resistance = seriesR / ((4095.0 / (float)raw) - 1.0);
  float steinhart = resistance / r0;
  steinhart = log(steinhart);
  steinhart /= beta;
  steinhart += 1.0 / (25.0 + 273.15);
  steinhart = 1.0 / steinhart;
  steinhart -= 273.15;
  float rawTemp = steinhart + offset;
  
  if (bufCount == 0) {
    for (int i = 0; i < 60; i++) buffer[i] = rawTemp;
    bufCount = 60;
  }
  if (samples > 60) samples = 60;
  if (samples < 1) samples = 1;
  for (int i = 59; i > 0; i--) {
    buffer[i] = buffer[i - 1];
  }
  buffer[0] = rawTemp;

  float sum = 0.0;
  for (int i = 0; i < samples; i++) {
    sum += buffer[i];
  }
  float avgTemp = sum / (float)samples;

  if (lastValidTemp > -50.0 && lastValidTemp < 100.0) {
    return (avgTemp * filterCoeff) + (lastValidTemp * (1.0 - filterCoeff));
  }
  return avgTemp;
}

float readLDRRolling(int pin, int samples, float filterCoeff, float lastValidLDR, float* buffer, int& bufCount) {
  int raw = analogRead(pin);
  float normalized = ((float)raw / 4095.0) * 1000.0;

  if (bufCount == 0) {
    for (int i = 0; i < 60; i++) buffer[i] = normalized;
    bufCount = 60;
  }
  if (samples > 60) samples = 60;
  if (samples < 1) samples = 1;
  for (int i = 59; i > 0; i--) {
    buffer[i] = buffer[i - 1];
  }
  buffer[0] = normalized;

  float sum = 0.0;
  for (int i = 0; i < samples; i++) {
    sum += buffer[i];
  }
  float avgLDR = sum / (float)samples;

  if (lastValidLDR >= 0.0) {
    return (avgLDR * filterCoeff) + (lastValidLDR * (1.0 - filterCoeff));
  }
  return avgLDR;
}
