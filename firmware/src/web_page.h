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
header{padding:12px 16px;background:var(--card);display:flex;justify-content:space-between;align-items:center}
header b{font-size:18px}header span{color:var(--mut);font-size:13px}
main{max-width:980px;margin:auto;padding:12px;display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(300px,1fr))}
.c{background:var(--card);border-radius:10px;padding:14px}h3{margin:0 0 10px;font-size:15px;color:var(--mut);font-weight:600}
.big{font-size:34px;font-variant-numeric:tabular-nums}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:6px 0}
button{background:#263241;color:var(--fg);border:1px solid #33475c;border-radius:8px;padding:10px 14px;font-size:15px;cursor:pointer}
button:active{background:var(--acc)}button.stop{background:var(--red);border-color:var(--red);font-weight:700;flex:1}
input,select,textarea{background:#0d1218;color:var(--fg);border:1px solid #33475c;border-radius:6px;padding:8px;font-size:15px}
input{width:90px}textarea{width:100%;height:320px;font-family:monospace;font-size:12px}
canvas{display:block;margin:auto;max-width:100%}.mut{color:var(--mut);font-size:13px}.f{font-weight:700}
.okc{color:var(--ok)}.bad{color:var(--red)}
</style></head><body>
<header><b>APRS LoRa+ Rotor</b><span id="hdr"></span></header>
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
async function info(){try{const r=await api([{c:'info'}]);const i=r[0];lim=i.lim;$('hdr').textContent=i.name+' · '+i.call+' · v'+i.ver;
$('info').textContent=`${i.board} (${i.chip}) · mando: ${i.drv} · IP ${i.ip} · rotctld ${i.rotctld||'off'} · rango AZ ${i.lim.minAz}–${i.lim.maxAz}, EL ${i.lim.minEl}–${i.lim.maxEl}`}catch(e){}}
info();poll();setInterval(poll,1000);setInterval(raw,2000);
</script></body></html>)HTML";
