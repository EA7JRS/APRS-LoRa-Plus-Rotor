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
.sm{display:flex;align-items:center;gap:8px;margin:6px 0}.smc{flex:1}.smb,.sml{display:flex}.smb{align-items:flex-end}.smg{display:flex;gap:2px;align-items:flex-end}.smg i{flex:1;height:10px;border-radius:2px}.smg.p i{height:13px}.smt{width:2px;height:16px;margin:0 3px;border-radius:1px;background:var(--fg);opacity:.8}.sml{font:500 10px/1.2 "Barlow Condensed","Roboto Condensed","Arial Narrow",sans-serif;color:#6F8293;padding-top:1px}.sml span{text-align:left}.sml .r span{text-align:right}.smv{font:18px "Share Tech Mono",ui-monospace,monospace;color:#4A5D70}
:root{--bg:#10151c;--card:#19212b;--fg:#e6edf3;--mut:#8b98a5;--acc:#4aa3ff;--red:#ff5d5d;--ok:#3fb950}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);color:var(--fg)}
/* Cabecera como la del tracker APRS LoRa+: logo, "APRS LoRa+" y debajo "Rotor" con la version */
header{background:#b21d1d;border-bottom:1px solid rgba(0,0,0,.25);height:56px;display:flex;align-items:center;gap:10px;padding:0 16px;position:sticky;top:0;z-index:10}
header .logo{height:40px;width:40px;flex-shrink:0;border-radius:8px}
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
<header><img class="logo" src="data:image/png;base64,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" alt="APRS LoRa+" width="40" height="40"><span class="brand"><span class="brand-name">APRS LoRa+</span><small class="brand-sub">Rotor <span class="ver" id="ver"></span></small></span><span class="right" id="hdr"></span></header>
<script>
// S-meter como el de la app Android (Widgets.kt SMeterBar): colores IARU, apagados al 15 %
const SMC=['#4FC3E0','#52D4C0','#5BDFA0','#72E688','#96EA74','#D4E650','#F2DE3E','#FFCF33','#FF9A2E','#FF5A3C','#FF4636','#FF3A33','#FF3030','#FF2A2A','#FF2424'];
function smSet(v){const A=document.getElementById('smA'),B=document.getElementById('smB');if(!A)return;
if(!A.children.length){for(let i=0;i<15;i++){const e=document.createElement('i');e.style.background=SMC[i];(i<9?A:B).appendChild(e);}}
const lit=v<0?0:Math.min(15,v),seg=[...A.children,...B.children];seg.forEach((e,i)=>e.style.opacity=i<lit?1:.15);
const t=document.getElementById('csm');t.textContent=v<0?'S—':(v<=9?'S'+v:'S9+'+(lit-9)*10);t.style.color=lit?SMC[lit-1]:'#4A5D70';}
(function(){var l=document.createElement("link");l.rel="icon";l.href=document.querySelector("header .logo").src;document.head.appendChild(l)})()</script>
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

<div class="c" id="catc" style="display:none"><h3>CAT · emisora <span class="mut" id="crig"></span></h3>
<div class="row"><span class="big" id="cf">---.---.---</span><span class="mut">MHz</span><span id="cm"></span><span id="cp"></span><span id="cl"></span></div>
<div class="sm"><div class="smc"><div class="smb"><div class="smg" id="smA" style="flex:9"></div><div class="smt"></div><div class="smg p" id="smB" style="flex:6"></div></div>
<div class="sml"><div style="flex:9;display:flex"><span style="flex:2">1</span><span style="flex:2">3</span><span style="flex:2">5</span><span style="flex:2">7</span><span style="flex:1;color:#FF9A2E">9</span></div><div style="width:8px"></div><div class="r" style="flex:6;display:flex"><span style="flex:1">+20</span><span style="flex:1">+40</span><span style="flex:1">+60</span></div></div></div>
<span class="smv" id="csm">S—</span></div>
<div class="row mut"><span id="ctx"></span><span id="ctn"></span><span id="ctr"></span></div>
<div class="row"><input id="cfi" type="number" step="0.000001" placeholder="MHz"><button onclick="catF()">Sintonizar</button></div>
<div class="row"><input id="ctone" type="number" step="0.1" min="0" max="254.1" placeholder="CTCSS Hz (0 = no)"><button onclick="cmd({c:'catset',tone:+$('ctone').value||0})">Tono</button></div>
<div class="row"><button onclick="catT(-1)">−</button><select id="cst"><option value="10">10 Hz</option><option value="100">100 Hz</option><option value="1000" selected>1 kHz</option><option value="5000">5 kHz</option><option value="12500">12,5 kHz</option><option value="25000">25 kHz</option><option value="100000">100 kHz</option></select><button onclick="catT(1)">+</button></div>
<div class="row" id="cmodes"></div>
<div class="row"><button id="cvfo" onclick="cmd({c:'catset',vfo:'swap'})">A / B</button><button id="ctrk" onclick="catTrk()">Seguir satélite</button><button id="cptt" class="stop">PTT</button></div>
<p class="mut">El PTT solo emite mientras se mantiene pulsado y si está permitido en la configuración (<code>cat.ptt</code>). El seguimiento con Doppler lo alimenta la app o el tracker (<code>cattrack</code>); GPredict puede usar directamente rigctld.</p></div>

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
<p>APRS LoRa+ Rotor &middot; &copy; 2026 Manuel - EA7JRS</p>
<p><b>APRS LoRa+</b> &mdash; EA7JRS: <a href="https://github.com/EA7JRS/APRS-LoRa-Plus" target="_blank" rel="noopener">github.com/EA7JRS/APRS-LoRa-Plus</a> (tracker, app Android, rotor y gateway).</p>
<p class="mut">Compatible con Hamlib (rotctld, rigctld), Yaesu GS-232A/B, SPID Rot2Prog y EasyComm II; CAT: Icom CI-V, Yaesu, Kenwood / Elecraft, SDRconnect (SDRplay) y rigctl. Librerías: NimBLE-Arduino, RadioLib, ArduinoJson; en la Raspberry Pi, rotctld de Hamlib. Licencia GPL-3.0-or-later.</p></div>
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
$('st').innerHTML=(pos.mv?'moviendo · ':'')+'<span class="f '+(pos.f=='OK'?'okc':'bad')+'">'+pos.f+'</span>'+(pos.maz!=null&&pos.maz>=360?' · solape':'')+(pos.wind&&pos.wind.ws!=null?' · viento '+pos.wind.ws+(pos.wind.wg!=null?'/'+pos.wind.wg:'')+' m/s'+(pos.wind.lk?' (aparcado)':''):'');
$('aprs').textContent=pos.aprs?('APRS: '+pos.aprs):'';draw()}catch(e){$('st').textContent='sin conexión'}}
async function raw(){try{const r=await api([{c:'raw'}]);$('raz').textContent=r[0].az!=null?r[0].az.toFixed(0):'-';$('rel').textContent=r[0].el!=null?r[0].el.toFixed(0):'-';$('ru').textContent=r[0].unit||''}catch(e){}}
async function loadCfg(){const r=await api([{c:'cfgget'}]);$('cfg').value=JSON.stringify(JSON.parse(r.map(x=>x.d).join('')),null,1)}
async function saveCfg(){let t;try{t=JSON.stringify(JSON.parse($('cfg').value))}catch(e){return alert('JSON no válido')}
const n=Math.ceil(t.length/180),L=[];for(let i=0;i<n;i++)L.push({c:'cfgset',i,n,d:t.substr(i*180,180)});
const r=await api(L);const e=r.find(x=>x.e=='err');alert(e?('Error: '+e.m):'Guardado. Reiniciando...')}
let cat=null;const MODES=[['LSB','LSB'],['USB','USB'],['CW','CW'],['FM','FM'],['AM','AM'],['DIG','PKTUSB']];
const fq=f=>{const s=String(Math.round(f||0));return s.replace(/\B(?=(\d{3})+(?!\d))/g,'.')};
function catInit(){$('catc').style.display='';$('cmodes').innerHTML=MODES.map(([l,m])=>`<button data-cm="${m}" onclick="cmd({c:'catset',m:'${m}'})">${l}</button>`).join('');
const p=$('cptt');p.addEventListener('pointerdown',()=>cmd({c:'catset',ptt:true}));p.addEventListener('pointerup',()=>cmd({c:'catset',ptt:false}));p.addEventListener('pointerleave',e=>{if(e.buttons)cmd({c:'catset',ptt:false})});
catPoll();setInterval(catPoll,1000)}
async function catPoll(){try{const r=await api([{c:'cat'}]);cat=r[0];if(cat.e!='cat')return;$('crig').textContent=cat.rig||'';$('cf').textContent=cat.l&&cat.f?fq(cat.f):'---.---.---';
$('cm').textContent=' '+(cat.m||'')+(cat.v?' · VFO '+cat.v:'');$('cp').innerHTML=cat.ptt?' <b class="bad">TX</b>':' RX';$('cl').innerHTML=cat.l?'':' <span class="bad">sin enlace</span>';
smSet(cat.sm);$('ctx').textContent=cat.tf?('↑ '+fq(cat.tf)):'';$('ctn').textContent=cat.tn?((cat.tf?' · ':'')+'CTCSS '+cat.tn.toFixed(1)+' Hz'):'';
const t=cat.tr||{};$('ctr').textContent=t.on&&t.en?(' · siguiendo '+(t.n||'')+' (Doppler '+(t.dop/1000).toFixed(1)+' kHz)'):'';$('ctrk').textContent=t.en?'Seguir: sí':'Seguir: no';
$('cptt').disabled=!cat.pa;document.querySelectorAll('[data-cm]').forEach(b=>b.style.outline=b.dataset.cm==cat.m?'2px solid var(--acc)':'')}catch(e){}}
function catF(){const v=parseFloat($('cfi').value);if(!(v>0))return;cmd({c:'catset',f:Math.round(v*1e6)})}
function catT(n){if(!cat||!cat.f)return;const s=+$('cst').value;cmd({c:'catset',f:(Math.floor(cat.f/s)+n)*s+(n<0&&cat.f%s?s:0)})}
function catTrk(){cmd({c:'cattrack',on:!(cat&&cat.tr&&cat.tr.en)})}
async function info(){try{const r=await api([{c:'info'}]);const i=r[0];lim=i.lim;$('ver').textContent=i.ver;$('buildInfo').textContent=`Rotor ${i.ver} · ${i.board} (${i.chip})`;$('hdr').textContent=i.name+' · '+i.call;
if(i.cat&&!cat)catInit();$('info').textContent=`${i.board} (${i.chip}) · mando: ${i.drv} · IP ${i.ip} · rotctld ${i.rotctld||'off'} · rigctld ${i.rigctld||'off'} · rango AZ ${i.lim.minAz}–${i.lim.maxAz}, EL ${i.lim.minEl}–${i.lim.maxEl}`}catch(e){}}
info();poll();setInterval(poll,1000);setInterval(raw,2000);
</script></body></html>)HTML";
