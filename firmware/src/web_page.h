/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// WebUI: una sola pagina. Habla con el mismo protocolo JSON que la app (POST /api, una orden por linea)

static const char WEB_PAGE[] = R"HTML(<!DOCTYPE html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>APRS LoRa+ Rotor</title>
<style>
:root{--bg:#10151c;--card:#19212b;--fg:#e6edf3;--mut:#8b98a5;--acc:#4aa3ff;--red:#ff5d5d;--ok:#3fb950}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);color:var(--fg)}
/* Cabecera como la del tracker APRS LoRa+: logo, "APRS LoRa+" y debajo "Rotor" con la version */
header{background:#b21d1d;border-bottom:1px solid rgba(0,0,0,.25);height:56px;display:flex;align-items:center;gap:10px;padding:0 16px;position:sticky;top:0;z-index:10}
header .logo{height:40px;width:auto;flex-shrink:0}
header .brand{font-weight:600;color:#fff;font-size:1.1rem;display:flex;flex-direction:column;line-height:1.1}
header .brand-sub{font-weight:400;font-size:.68rem;opacity:.85;letter-spacing:.02em}
header .right{margin-left:auto;color:#fff;opacity:.85;font-size:12px;text-align:right}
main{max-width:980px;margin:auto;padding:12px;display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(300px,1fr))}
.c{background:var(--card);border-radius:10px;padding:14px}h3{margin:0 0 10px;font-size:15px;color:var(--mut);font-weight:600}
.big{font-size:34px;font-variant-numeric:tabular-nums}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:6px 0}
button{background:#263241;color:var(--fg);border:1px solid #33475c;border-radius:8px;padding:10px 14px;font-size:15px;cursor:pointer}
button:active{background:var(--acc)}button.stop{background:var(--red);border-color:var(--red);font-weight:700;flex:1}
input,select,textarea{background:#0d1218;color:var(--fg);border:1px solid #33475c;border-radius:6px;padding:8px;font-size:15px}
input{width:90px}textarea{width:100%;height:320px;font-family:monospace;font-size:12px}
canvas{display:block;margin:auto;max-width:100%}.mut{color:var(--mut);font-size:13px}.f{font-weight:700}
.okc{color:var(--ok)}.bad{color:var(--red)}
a{color:var(--acc)}#about p{margin:6px 0;font-size:13px}
</style></head><body>
<header><svg class="logo" xmlns="http://www.w3.org/2000/svg" viewBox="25 15 890 985" role="img" aria-label="APRS LoRa+"><rect x="25" y="15" width="890" height="985" rx="60" fill="#000000"/><g id="ondas" fill="#19AE19" stroke="#19AE19" stroke-width="18" stroke-linejoin="round"><path d="M57.4 229.9 A551 551 0 0 1 881 229.9 L864.5 244.1 A529.2 529.2 0 0 0 73.9 244.1 Z"/><path d="M146.4 301.4 A437 437 0 0 1 792 301.4 L776.1 315.4 A415.8 415.8 0 0 0 162.3 315.4 Z"/><path d="M240.1 368.5 A322.9 322.9 0 0 1 698.3 368.5 L682.4 383.5 A301 301 0 0 0 256 383.5 Z"/></g><path id="pin" fill="#109110" stroke="#109110" stroke-width="18" stroke-linejoin="round" d="M179.3 421.3 A302 302 0 0 0 170.1 563.9 C173.6 544.2 161.1 718.2 467.5 975.3 C773.9 718.2 761.4 544.2 764.9 563.9 A302 302 0 0 0 756.3 423.1 L706.9 454.7 A246 246 0 1 1 228.6 452.8 Z"/><g id="lmas" fill="#FFFFFF" stroke="#FFFFFF" stroke-width="9" stroke-linejoin="round"><path d="M350.7 397.9 L340.5 411.5 L340.5 623.8 L380.6 655.5 L547.7 655.5 L598.5 623.5 L598.5 615.5 L399.4 613.5 L386.5 591.2 L386.5 387.1 Z"/><path d="M554.7 385.8 L546.2 390.7 L540.1 446.8 L483.4 457.9 L478.9 471.5 L482.7 477.8 L534.8 483.9 L546.8 543.9 L565.9 546.3 L574.7 540.4 L579.9 489.3 L637.4 477.2 L641.1 463.9 L635.5 456.3 L585.3 451.1 L572.2 389.8 Z"/></g></svg><span class="brand"><span class="brand-name">APRS LoRa+</span><small class="brand-sub">Rotor <span class="ver" id="ver"></span></small></span><span class="right" id="hdr"></span></header>
<main>
<div class="c"><h3>Posición</h3>
<canvas id="cmp" width="260" height="260"></canvas>
<div class="row" style="justify-content:space-around"><div>AZ <span class="big" id="az">--</span>°</div><div id="elbox">EL <span class="big" id="el">--</span>°</div></div>
<div class="row mut" style="justify-content:space-around"><span id="tgt"></span><span id="st"></span></div>
<div class="row mut" id="aprs"></div></div>

<div class="c"><h3>Mando</h3>
<div class="row"><input id="gaz" type="number" step="0.1" placeholder="AZ"><input id="gel" type="number" step="0.1" placeholder="EL"><button onclick="go()">Ir</button></div>
<div class="row"><button data-m="ccw">⟲ CCW</button><button data-m="cw">CW ⟳</button><button data-m="up">▲ UP</button><button data-m="down">▼ DOWN</button></div>
<div class="row"><button class="stop" onclick="cmd({c:'stop'})">STOP</button></div>
<div class="row"><button onclick="cmd({c:'park'})">Park</button><button onclick="cmd({c:'clear'})">Borrar fallo</button></div>
<p class="mut">Mantén pulsado CW / CCW / UP / DOWN para mover a mano.</p></div>

<div class="c"><h3>Calibración (control directo)</h3>
<div class="row mut">Lectura en bruto: AZ <b id="raz">-</b> · EL <b id="rel">-</b> <span id="ru"></span></div>
<div class="row"><select id="cax"><option value="az">Acimut</option><option value="el">Elevación</option></select>
<input id="cdeg" type="number" step="0.1" placeholder="grados"></div>
<div class="row"><button onclick="cal('a')">Punto A aquí</button><button onclick="cal('b')">Punto B aquí</button><button onclick="here()">Corregir desfase</button></div>
<p class="mut">Lleva el rotor a un tope (p. ej. 0°) y pulsa «Punto A»; luego al otro (p. ej. 450°) y «Punto B». «Corregir desfase» hace que la posición actual valga los grados escritos.</p></div>

<div class="c" style="grid-column:1/-1"><h3>Configuración</h3>
<div class="row"><button onclick="loadCfg()">Leer</button><button onclick="saveCfg()">Guardar y reiniciar</button><a href="/update"><button>Actualizar firmware</button></a></div>
<textarea id="cfg" spellcheck="false"></textarea>
<p class="mut">Referencia de campos en docs/CONFIG.md del repositorio. Las contraseñas aparecen; no compartas esta página.</p>
<div class="row mut" id="info"></div></div>
<div class="c" style="grid-column:1/-1" id="about"><h3>Acerca de</h3>
<p class="mut" id="buildInfo"></p>
<p>APRS LoRa+ Rotor - by <b>Manuel EA7JRS</b> &middot; &copy; 2026 Manuel - EA7JRS</p>
<p><b>APRS LoRa+</b> &mdash; EA7JRS: <a href="https://github.com/EA7JRS/APRS-LoRa-Plus" target="_blank" rel="noopener">github.com/EA7JRS/APRS-LoRa-Plus</a> (tracker, app Android, rotor y gateway).</p>
<p class="mut">Compatible con Hamlib (rotctld), Yaesu GS-232A/B, SPID Rot2Prog y EasyComm II. Librerías: NimBLE-Arduino, RadioLib, ArduinoJson; en la Raspberry Pi, rotctld de Hamlib. Licencia GPL-3.0-or-later.</p></div>
</main>
<script>
const $=id=>document.getElementById(id);let pos={},lim={maxAz:360};
async function api(lines){const r=await fetch('/api',{method:'POST',body:lines.map(o=>JSON.stringify(o)).join('\n')+'\n'});if(!r.ok)throw r.status;return r.json()}
async function cmd(o){try{const r=await api([o]);const e=r.find(x=>x.e=='err');if(e)alert(e.c+': '+e.m)}catch(e){}}
function go(){const a=parseFloat($('gaz').value),e=parseFloat($('gel').value);if(isNaN(a))return;const o={c:'pos',az:a};if(!isNaN(e))o.el=e;cmd(o)}
function cal(p){const d=parseFloat($('cdeg').value);if(isNaN(d))return alert('grados');cmd({c:'cal',axis:$('cax').value,pt:p,deg:d})}
function here(){const d=parseFloat($('cdeg').value);if(isNaN(d))return alert('grados');cmd({c:'here',axis:$('cax').value,deg:d})}
document.querySelectorAll('[data-m]').forEach(b=>{const s=()=>cmd({c:'move',d:b.dataset.m}),t=()=>cmd({c:'stop'});
b.addEventListener('pointerdown',s);b.addEventListener('pointerup',t);b.addEventListener('pointerleave',e=>{if(e.buttons)t()})});
function draw(){const c=$('cmp'),x=c.getContext('2d'),r=110,cx=130,cy=130;x.clearRect(0,0,260,260);
x.strokeStyle='#33475c';x.lineWidth=2;x.beginPath();x.arc(cx,cy,r,0,7);x.stroke();x.fillStyle='#8b98a5';x.font='13px sans-serif';x.textAlign='center';
[['N',0],['E',90],['S',180],['W',270]].forEach(([t,a])=>{const k=a*Math.PI/180;x.fillText(t,cx+Math.sin(k)*(r+12),cy-Math.cos(k)*(r+12)+5)});
for(let a=0;a<360;a+=30){const k=a*Math.PI/180;x.beginPath();x.moveTo(cx+Math.sin(k)*(r-8),cy-Math.cos(k)*(r-8));x.lineTo(cx+Math.sin(k)*r,cy-Math.cos(k)*r);x.stroke()}
const line=(a,col,w)=>{if(a==null)return;const k=a*Math.PI/180;x.strokeStyle=col;x.lineWidth=w;x.beginPath();x.moveTo(cx,cy);x.lineTo(cx+Math.sin(k)*(r-14),cy-Math.cos(k)*(r-14));x.stroke()};
line(pos.taz!=null?pos.taz%360:null,'#4aa3ff',3);line(pos.az,'#ff5d5d',5)}
async function poll(){try{const r=await api([{c:'get'}]);pos=r[0];$('az').textContent=pos.az.toFixed(1);
if(pos.el!=null){$('el').textContent=pos.el.toFixed(1)}else $('elbox').style.display='none';
$('tgt').textContent=pos.taz!=null?('→ '+pos.taz.toFixed(1)+(pos.tel!=null?' / '+pos.tel.toFixed(1):'')):'';
$('st').innerHTML=(pos.mv?'moviendo · ':'')+'<span class="f '+(pos.f=='OK'?'okc':'bad')+'">'+pos.f+'</span>'+(pos.maz!=null&&pos.maz>=360?' · solape':'');
$('aprs').textContent=pos.aprs?('APRS: '+pos.aprs):'';draw()}catch(e){$('st').textContent='sin conexión'}}
async function raw(){try{const r=await api([{c:'raw'}]);$('raz').textContent=r[0].az!=null?r[0].az.toFixed(0):'-';$('rel').textContent=r[0].el!=null?r[0].el.toFixed(0):'-';$('ru').textContent=r[0].unit||''}catch(e){}}
async function loadCfg(){const r=await api([{c:'cfgget'}]);$('cfg').value=JSON.stringify(JSON.parse(r.map(x=>x.d).join('')),null,1)}
async function saveCfg(){let t;try{t=JSON.stringify(JSON.parse($('cfg').value))}catch(e){return alert('JSON no válido')}
const n=Math.ceil(t.length/180),L=[];for(let i=0;i<n;i++)L.push({c:'cfgset',i,n,d:t.substr(i*180,180)});
const r=await api(L);const e=r.find(x=>x.e=='err');alert(e?('Error: '+e.m):'Guardado. Reiniciando...')}
async function info(){try{const r=await api([{c:'info'}]);const i=r[0];lim=i.lim;$('ver').textContent=i.ver;$('buildInfo').textContent=`Rotor ${i.ver} · ${i.board} (${i.chip})`;$('hdr').textContent=i.name+' · '+i.call;
$('info').textContent=`${i.board} (${i.chip}) · mando: ${i.drv} · IP ${i.ip} · rotctld ${i.rotctld||'off'} · rango AZ ${i.lim.minAz}–${i.lim.maxAz}, EL ${i.lim.minEl}–${i.lim.maxEl}`}catch(e){}}
info();poll();setInterval(poll,1000);setInterval(raw,2000);
</script></body></html>)HTML";
