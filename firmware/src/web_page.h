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
/* Aspecto claro / oscuro / automatico (el del sistema), como las demas WebUI de EA7JRS; se guarda en el navegador */
:root{--bg:#f6f7f9;--card:#ffffff;--fg:#1b1f24;--mut:#5b6670;--acc:#0b6bcb;--red:#cf222e;--ok:#1a7f37;--btn:#eef1f4;--bd:#d8dde3;--inp:#ffffff}
:root[data-tema="oscuro"]{--bg:#10151c;--card:#19212b;--fg:#e6edf3;--mut:#8b98a5;--acc:#4aa3ff;--red:#ff5d5d;--ok:#3fb950;--btn:#263241;--bd:#33475c;--inp:#0d1218}
@media(prefers-color-scheme:dark){:root:not([data-tema="claro"]){--bg:#10151c;--card:#19212b;--fg:#e6edf3;--mut:#8b98a5;--acc:#4aa3ff;--red:#ff5d5d;--ok:#3fb950;--btn:#263241;--bd:#33475c;--inp:#0d1218}}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);color:var(--fg)}
/* Cabecera como la del tracker APRS LoRa+: logo, "APRS LoRa+" y debajo "Rotor" con la version */
header{background:#131a22;border-bottom:2px solid transparent;border-image:linear-gradient(90deg,#3ce68a,#28b5e1) 1;height:56px;display:flex;align-items:center;gap:10px;padding:0 16px;position:sticky;top:0;z-index:10}
header .logo{height:40px;width:40px;flex-shrink:0;border-radius:8px}
header .brand{font-weight:600;color:#fff;font-size:1.1rem;display:flex;flex-direction:column;line-height:1.1}
header .brand-sub{font-weight:400;font-size:.68rem;opacity:.95;letter-spacing:.02em;color:#28b5e1}
header .right{margin-left:auto;display:flex;align-items:center;gap:10px;color:#fff;font-size:12px;text-align:right}
header .right #hdr{opacity:.85}
.thm{position:relative}
.thm>button{background:transparent;border:1px solid rgba(255,255,255,.3);color:#fff;border-radius:8px;padding:6px 10px;font-size:13px;cursor:pointer}
.thm .menu{display:none;position:absolute;right:0;top:calc(100% + 6px);min-width:12rem;background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:4px;box-shadow:0 10px 30px rgba(0,0,0,.35);z-index:20}
.thm .menu.open{display:block}
.thm .menu button{display:block;width:100%;text-align:left;background:transparent;border:0;border-radius:7px;color:var(--fg);padding:8px 10px;font-size:14px;cursor:pointer}
.thm .menu button:hover{background:var(--btn)}
.thm .menu button.on{box-shadow:inset 3px 0 0 var(--acc)}
.thm .menu small{color:var(--mut);margin-left:6px}
main{max-width:980px;margin:auto;padding:12px;display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(300px,1fr))}
.c{background:var(--card);border-radius:10px;padding:14px}h3{margin:0 0 10px;font-size:15px;color:var(--mut);font-weight:600}
.big{font-size:34px;font-variant-numeric:tabular-nums}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:6px 0}
button{background:var(--btn);color:var(--fg);border:1px solid var(--bd);border-radius:8px;padding:10px 14px;font-size:15px;cursor:pointer}
button:active{background:var(--acc);color:#fff}button.stop{background:var(--red);border-color:var(--red);color:#fff;font-weight:700;flex:1}
input,select,textarea{background:var(--inp);color:var(--fg);border:1px solid var(--bd);border-radius:6px;padding:8px;font-size:15px}
input{width:90px}textarea{width:100%;height:320px;font-family:monospace;font-size:12px}
canvas{display:block;margin:auto;max-width:100%}.mut{color:var(--mut);font-size:13px}.f{font-weight:700}
.okc{color:var(--ok)}.bad{color:var(--red)}
a{color:var(--acc)}.btn-donate{display:inline-flex;align-items:center;gap:8px;background:#0070e0;color:#fff!important;border-radius:999px;padding:8px 18px;font-weight:600;text-decoration:none;font-size:14px}.btn-donate:hover{background:#003087;color:#fff}#about p{margin:6px 0;font-size:13px}
</style><script>
/* Aspecto: se aplica antes de pintar para que no haya parpadeo. Automático = día / noche por el orto y el ocaso
   en la posición de la estación (como sat-aprs); sin posición todavía, claro de 7 a 19 h local. */
(function(){var M=['sistema','claro','oscuro'];var P=null;
function md(){var m=null;try{m=localStorage.getItem('rotor-tema')}catch(e){}return M.indexOf(m)>=0?m:'sistema'}
function sunUp(lat,lon,now){var r=Math.PI/180,y=now.getUTCFullYear(),n=(Date.UTC(y,now.getUTCMonth(),now.getUTCDate())-Date.UTC(y,0,0))/864e5,dec=23.44*Math.sin(r*(360/365)*(n+284)),h=now.getUTCHours()+now.getUTCMinutes()/60,H=15*((h+lon/15)-12);return Math.asin(Math.sin(r*lat)*Math.sin(r*dec)+Math.cos(r*lat)*Math.cos(r*dec)*Math.cos(r*H))/r>-0.833}
function autoTheme(){if(P)return sunUp(P[0],P[1],new Date())?'claro':'oscuro';var h=new Date().getHours();return (h>=7&&h<19)?'claro':'oscuro'}
function ef(m){m=m||md();return m==='sistema'?autoTheme():m}
function ap(){var t=ef();document.documentElement.setAttribute('data-tema',t);return t}
window.TEMA={M:M,modo:md,poner:function(m){try{localStorage.setItem('rotor-tema',m)}catch(e){}return ap()},aplicar:ap,pos:function(la,lo){if(la!=null&&lo!=null){P=[la,lo];if(md()==='sistema')ap()}}};
try{ap()}catch(e){}
setInterval(function(){if(md()==='sistema')ap()},60000);
})();
</script></head><body>
<header><img class="logo" src="data:image/png;base64,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" alt="APRS LoRa+" width="40" height="40"><span class="brand"><span class="brand-name">APRS LoRa+</span><small class="brand-sub">Rotor <span class="ver" id="ver"></span></small></span><div class="right"><span id="hdr"></span><div class="thm"><button id="themeBtn" type="button" aria-haspopup="true" aria-expanded="false" title="Aspecto">◐</button><div class="menu" id="themeMenu" role="menu"><button type="button" role="menuitemradio" data-m="sistema">Automático<small>día / noche</small></button><button type="button" role="menuitemradio" data-m="claro">Claro</button><button type="button" role="menuitemradio" data-m="oscuro">Oscuro</button></div></div><div class="thm"><button id="cfgBtn" type="button" aria-haspopup="true" aria-expanded="false" title="Configuración" aria-label="Configuración"><svg viewBox="0 0 24 24" width="18" height="18" fill="currentColor" aria-hidden="true" style="vertical-align:middle"><path d="M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58a.49.49 0 0 0 .12-.61l-1.92-3.32a.488.488 0 0 0-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54a.484.484 0 0 0-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58a.49.49 0 0 0-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"/></svg></button><div class="menu" id="cfgMenu" role="menu"><label style="display:block;padding:4px 10px;font-size:12px;color:var(--mut)">Coordenadas<select id="selCoord" style="width:100%;margin-top:4px"><option value="dec">Decimal</option><option value="dms">GMS</option><option value="ddm">GM (náutico)</option></select></label><label style="display:block;padding:4px 10px;font-size:12px;color:var(--mut)">Unidades<select id="selUnits" style="width:100%;margin-top:4px"><option value="metric">Métrico</option><option value="imperial">Imperial</option><option value="nautical">Náutico</option></select></label><div style="border-top:1px solid var(--bd);margin:4px 0"></div><button type="button" data-adv="calCard">Calibración</button><button type="button" data-adv="cfgCard">Configuración</button><button type="button" data-adv="about">Acerca de</button></div></div></div></header>
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
<div class="row"><button onclick="cmd({c:'track',t:'sun'})">☀ Sol</button><button onclick="cmd({c:'track',t:'moon'})">☾ Luna</button><button id="satBtn" onclick="satPick()">🛰 SAT</button><button onclick="cmd({c:'track',t:'off'})">Dejar de seguir</button></div>
<p class="mut">Mantén pulsado CW / CCW / UP / DOWN para mover a mano.</p></div>

<div id="rightcol" style="display:flex;flex-direction:column;gap:12px">
<div class="c" id="gpsc" style="display:none">
<div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:8px"><h3 style="margin:0">GPS</h3><span id="gstat" class="mut"></span></div>
<div id="gpos" style="font-size:22px;font-variant-numeric:tabular-nums">—</div>
<div class="mut" id="gmeta" style="margin-top:4px"></div></div>

<div class="c" id="trkc" style="display:none">
<div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:6px"><h3 style="margin:0">Objetivo</h3><span id="trkName" style="font-size:18px">—</span></div>
<div class="row" style="justify-content:space-around;margin:4px 0"><div class="mut">AZ <span style="font-size:24px;color:var(--fg);font-variant-numeric:tabular-nums" id="trkAz">--</span>°</div><div class="mut">EL <span style="font-size:24px;color:var(--fg);font-variant-numeric:tabular-nums" id="trkEl">--</span>°</div></div>
<div class="mut" id="trkInfo" style="font-size:13px"></div></div>
</div>

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

<div class="c adv" style="display:none" id="calCard"><h3>Calibración (control directo)</h3>
<div class="row mut">Lectura en bruto: AZ <b id="raz">-</b> · EL <b id="rel">-</b> <span id="ru"></span></div>
<div class="row"><select id="cax"><option value="az">Acimut</option><option value="el">Elevación</option></select>
<input id="cdeg" type="number" step="0.1" placeholder="grados"></div>
<div class="row"><button onclick="cal('a')">Punto A aquí</button><button onclick="cal('b')">Punto B aquí</button><button onclick="here()">Corregir desfase</button></div>
<p class="mut">Lleva el rotor a un tope (p. ej. 0°) y pulsa «Punto A»; luego al otro (p. ej. 450°) y «Punto B». «Corregir desfase» hace que la posición actual valga los grados escritos.</p></div>

<div class="c adv" style="grid-column:1/-1;display:none" id="cfgCard"><h3>Configuración</h3>
<div class="row"><button onclick="loadCfg()">Leer</button><button onclick="saveCfg()">Guardar y reiniciar</button><a href="/update"><button>Actualizar firmware</button></a></div>
<textarea id="cfg" spellcheck="false"></textarea>
<p class="mut">Referencia de campos en docs/CONFIG.md del repositorio. Las contraseñas aparecen; no compartas esta página.</p>
<div class="row mut" id="info"></div><div class="row mut" id="sdrow" style="display:none"></div></div>
<div class="c adv" style="grid-column:1/-1;display:none" id="about"><h3>Acerca de</h3>
<p class="mut" id="buildInfo"></p>
<p>APRS LoRa+ Rotor &middot; &copy; 2026 Manuel - EA7JRS</p>
<p><b>APRS LoRa+</b> &mdash; EA7JRS: <a href="https://ea7jrs.github.io/APRS-LoRa-Plus/" target="_blank" rel="noopener">ea7jrs.github.io/APRS-LoRa-Plus</a> (instalador y proyecto: tracker, app Android, rotor, gateway y display).</p>
<p><a class="btn-donate" href="https://paypal.me/ea7jrs" target="_blank" rel="noopener" title="paypal.me/ea7jrs"><svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M15.607 4.653H8.941L6.645 19.251H1.82L4.862 0h7.995c3.754 0 6.375 2.294 6.473 5.513-.648-.478-2.105-.86-3.722-.86m6.57 5.546c0 3.41-3.01 6.853-6.958 6.853h-2.493L11.595 24H6.74l1.845-11.538h3.592c4.208 0 7.346-3.634 7.153-6.949a5.24 5.24 0 0 1 2.848 4.686M9.653 5.546h6.408c.907 0 1.942.222 2.363.541-.195 2.741-2.655 5.483-6.441 5.483H8.714Z"/></svg>Donate · PayPal.me</a></p>
<p class="mut">Compatible con Hamlib (rotctld, rigctld), Yaesu GS-232A/B, SPID Rot2Prog y EasyComm II; CAT: Icom CI-V, Yaesu, Kenwood / Elecraft, SDRconnect (SDRplay) y rigctl. Librerías: NimBLE-Arduino, RadioLib, ArduinoJson; en la Raspberry Pi, rotctld de Hamlib. Licencia GPL-3.0-or-later.</p></div>
</main>
<div id="satModal" style="display:none;position:fixed;inset:0;background:rgba(0,0,0,.55);z-index:50;align-items:center;justify-content:center;padding:16px">
<div style="background:var(--card);border:1px solid var(--bd);border-radius:12px;max-width:430px;width:100%;max-height:82vh;display:flex;flex-direction:column">
<div style="display:flex;align-items:center;justify-content:space-between;padding:12px 16px;border-bottom:1px solid var(--bd)"><b>Elegir satélite</b><button onclick="satClose()" style="padding:4px 10px">×</button></div>
<div style="padding:10px 16px 6px"><input id="satSearch" placeholder="Buscar…" oninput="satRender()" style="width:100%"></div>
<div style="padding:0 16px 8px"><div id="satChips" style="display:flex;gap:6px;flex-wrap:wrap"></div></div>
<div id="satList" style="overflow:auto;padding:0 8px 6px"></div>
<div style="display:flex;justify-content:space-between;align-items:center;gap:8px;padding:10px 16px;border-top:1px solid var(--bd)"><span id="satMeta" class="mut" style="font-size:12px"></span><button onclick="satUpdate()" style="padding:6px 10px">Actualizar TLE</button></div>
</div></div>
<script>
const $=id=>document.getElementById(id);let pos={},lim={maxAz:360};
// Preferencias de vista (guardadas en el navegador, como el aspecto): formato de coordenadas y unidades.
function gP(k,d){try{var v=localStorage.getItem(k);if(v)return v}catch(e){}return d}
function coordPref(){return gP('rotor-coord','dec')}
function unitPref(){return gP('rotor-units','metric')}
function z2(n){n=''+n;return n.length<2?'0'+n:n}
function dms(v,P,N,dw){var h=v<0?N:P;v=Math.abs(v);var d=Math.floor(v),m=(v-d)*60,s=Math.round((m-Math.floor(m))*60);m=Math.floor(m);if(s==60){s=0;m++}if(m==60){m=0;d++}d=''+d;while(d.length<dw)d='0'+d;return d+'°'+z2(m)+"'"+z2(s)+'"'+h}
function ddm(v,P,N,dw){var h=v<0?N:P;v=Math.abs(v);var d=Math.floor(v),m=(v-d)*60;d=''+d;while(d.length<dw)d='0'+d;return d+'°'+(m<10?'0':'')+m.toFixed(3)+"'"+h}
function fmtLL(la,lo){var f=coordPref();if(f=='dms')return dms(la,'N','S',2)+' '+dms(lo,'E','W',3);if(f=='ddm')return ddm(la,'N','S',2)+' '+ddm(lo,'E','W',3);return la.toFixed(5)+', '+lo.toFixed(5)}
function fmtAlt(m){return unitPref()=='metric'?Math.round(m)+' m':Math.round(m*3.28084)+' ft'}
function fmtDist(km){var u=unitPref();if(u=='imperial')return (km*0.621371).toFixed(km<16?2:1)+' mi';if(u=='nautical')return (km*0.539957).toFixed(km<16?2:1)+' NM';return km<1?Math.round(km*1000)+' m':km.toFixed(km<16?2:1)+' km'}
// Locator Maidenhead de 8 caracteres (subcuadrícula extendida), calculado de lat/lon.
function loc8(la,lo){lo+=180;la+=90;var A=65,a=97,s='';s+=String.fromCharCode(A+Math.floor(lo/20));s+=String.fromCharCode(A+Math.floor(la/10));s+=''+Math.floor((lo%20)/2);s+=''+Math.floor(la%10);s+=String.fromCharCode(a+Math.floor((lo%2)*12));s+=String.fromCharCode(a+Math.floor((la%1)*24));s+=''+Math.floor((lo%2)*12%1*10);s+=''+Math.floor((la%1)*24%1*10);return s}
async function api(lines){const r=await fetch('/api',{method:'POST',body:lines.map(o=>JSON.stringify(o)).join('\n')+'\n'});if(!r.ok)throw r.status;return r.json()}
async function cmd(o){try{const r=await api([o]);const e=r.find(x=>x.e=='err');if(e)alert(e.c+': '+e.m)}catch(e){}}
// La columna derecha (GPS + Objetivo) se apila y se oculta si no hay ninguno, para alinearse con Posición y Mando.
function rcSync(){var g=$('gpsc'),t=$('trkc'),r=$('rightcol');if(r)r.style.display=((g&&g.style.display!=='none')||(t&&t.style.display!=='none'))?'flex':'none'}
// Selector de satelite: lista del API (nombre, az/el, proximo paso), buscar, elegir (track sat) y actualizar TLE.
var SATS=[],SATF=[],SATGROUPS=[],SATGRP='',SATACT=false;
function sEsc(s){return (s+'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}
function satHM(ts){var d=new Date(ts*1000);return ('0'+d.getUTCHours()).slice(-2)+':'+('0'+d.getUTCMinutes()).slice(-2)+'Z'}
function satPick(){SATGRP='';SATACT=false;$('satSearch').value='';$('satModal').style.display='flex';satLoad()}
function satClose(){$('satModal').style.display='none'}
async function satLoad(){try{var o=(await api([{c:'sats',grp:SATGRP||undefined,act:SATACT}]))[0]||{};SATS=o.list||[];if(o.groups)SATGROUPS=o.groups;satMeta(o);satChips(o);satRender()}catch(e){$('satList').innerHTML='<p class="bad" style="padding:12px">Satélites no disponibles.</p>'}}
function satMeta(o){var m=$('satMeta');if(o.haspos===false){m.innerHTML='<span class="bad">sin posición propia (GPS o red)</span>';return}var age=o.updated?Math.round((Date.now()/1000-o.updated)/3600):null;var act=o.amsat?(' · '+(o.nactive||0)+' activos (AMSAT)'):'';m.textContent=(o.n||0)+' sat · TLE '+(o.updated?('hace '+age+' h'):'sin descargar')+act}
function satChipHtml(txt,on,js){return '<span onclick="'+js+'" style="cursor:pointer;font-size:12px;border-radius:20px;padding:4px 11px;'+(on?'color:#fff;background:var(--acc)':'color:var(--fg);background:var(--btn)')+'">'+txt+'</span>'}
function satChips(o){var h=satChipHtml('Todos',SATGRP===''&&!SATACT,"satCat('')");for(var i=0;i<SATGROUPS.length;i++){var g=SATGROUPS[i];h+=satChipHtml(sEsc(g[1]),SATGRP===g[0],"satCat('"+g[0]+"')")}if(o.amsat!==false)h+=satChipHtml('Activos',SATACT,'satAct()');$('satChips').innerHTML=h}
function satCat(g){SATGRP=(SATGRP===g?'':g);satLoad()}
function satAct(){SATACT=!SATACT;satLoad()}
function satRender(){var q=($('satSearch').value||'').toUpperCase();SATF=SATS.filter(function(s){return s.name.toUpperCase().indexOf(q)>=0});var h='';for(var i=0;i<SATF.length;i++){var s=SATF[i];var when=s.up?'<span class="okc">ahora</span>':(s.aos?satHM(s.aos):'—');var mx=s.maxel!=null?' · máx '+s.maxel+'°':'';var dot=s.act?'<span class="okc">● </span>':'';h+='<div onclick="satSel('+i+')" style="display:flex;justify-content:space-between;align-items:center;gap:10px;padding:9px 8px;border-top:1px solid var(--bd);cursor:pointer"><span>'+dot+sEsc(s.name)+'</span><span class="mut" style="font-size:12px;white-space:nowrap">'+when+mx+'</span></div>'}$('satList').innerHTML=h||'<p class="mut" style="padding:12px">Sin satélites. Prueba otra categoría o «Actualizar TLE».</p>'}
async function satSel(i){var s=SATF[i];if(!s)return;satClose();await cmd({c:'track',t:'sat',id:s.name});poll()}
async function satUpdate(){$('satMeta').textContent='Actualizando TLE…';await cmd({c:'tle'});setTimeout(satLoad,5000)}
function go(){const a=parseFloat($('gaz').value),e=parseFloat($('gel').value);if(isNaN(a))return;const o={c:'pos',az:a};if(!isNaN(e))o.el=e;cmd(o)}
function cal(p){const d=parseFloat($('cdeg').value);if(isNaN(d))return alert('grados');cmd({c:'cal',axis:$('cax').value,pt:p,deg:d})}
function here(){const d=parseFloat($('cdeg').value);if(isNaN(d))return alert('grados');cmd({c:'here',axis:$('cax').value,deg:d})}
document.querySelectorAll('[data-m]').forEach(b=>{const s=()=>cmd({c:'move',d:b.dataset.m}),t=()=>cmd({c:'stop'});
b.addEventListener('pointerdown',s);b.addEventListener('pointerup',t);b.addEventListener('pointerleave',e=>{if(e.buttons)t()})});
// Esfera con el estilo de sat-aprs, la app y el Display: graduada cada 5°, números cada 30, N roja, objetivo ámbar,
// aguja azul con contrapeso que gira por el lado corto, topes y solape sobre el borde
let acc=null,shown=null,raf=0;
function draw(){if(pos.az!=null){acc=acc==null?pos.az:acc+((((pos.az-acc)%360)+540)%360)-180;if(shown==null)shown=acc}if(!raf)raf=requestAnimationFrame(frame)}
function frame(){raf=0;if(acc!=null){const d=acc-shown;shown+=Math.abs(d)<.05?d:d*.12;if(Math.abs(acc-shown)>.05)raf=requestAnimationFrame(frame)}paint()}
function paint(){const c=$('cmp'),q=window.devicePixelRatio||1,W=260;if(c.width!=W*q){c.width=c.height=W*q;c.style.width=c.style.height=W+'px'}
const x=c.getContext('2d');x.setTransform(q,0,0,q,0,0);x.clearRect(0,0,W,W);const C=W/2,s=(W/2-4)/106;
const P=(a,r,sd=0)=>{const k=a*Math.PI/180,ux=Math.sin(k),uy=-Math.cos(k);return[C+(r*ux-sd*uy)*s,C+(r*uy+sd*ux)*s]};
const L=(a,r0,r1,w,col,dash)=>{const[p,q2]=[P(a,r0),P(a,r1)];x.strokeStyle=col;x.lineWidth=w*s;x.lineCap=dash?'butt':'round';x.setLineDash(dash?dash.map(v=>v*s):[]);x.beginPath();x.moveTo(...p);x.lineTo(...q2);x.stroke();x.setLineDash([])};
const T=(pts,fill,stroke,w)=>{x.beginPath();x.moveTo(...pts[0]);pts.slice(1).forEach(p=>x.lineTo(...p));x.closePath();if(fill){x.fillStyle=fill;x.fill()}if(stroke){x.strokeStyle=stroke;x.lineWidth=w*s;x.lineJoin='round';x.stroke()}};
const g=x.createRadialGradient(C,C-.16*106*s,0,C,C-.16*106*s,138*s);g.addColorStop(0,'#16283a');g.addColorStop(1,'#060b14');
x.fillStyle=g;x.beginPath();x.arc(C,C,106*s,0,7);x.fill();x.strokeStyle='#334155';x.lineWidth=2*s;x.stroke();
x.strokeStyle='#1e293b';x.lineWidth=1*s;x.beginPath();x.arc(C,C,62*s,0,7);x.stroke();
const arc=(a0,a1,col)=>{x.strokeStyle=col;x.lineWidth=4*s;x.lineCap='butt';x.beginPath();x.arc(C,C,104*s,(a0-90)*Math.PI/180,(a1-90)*Math.PI/180);x.stroke()};
if(lim.maxAz-lim.minAz<360)arc(lim.maxAz,lim.minAz+360,'rgba(248,113,113,.7)');if(lim.maxAz>360)arc(0,lim.maxAz-360,'#ff9800');
for(let a=0;a<360;a+=5){const M=a%30==0,m=a%10==0;L(a,M?90:m?94:97,103,M?2:1,M?'#e2e8f0':m?'#94a3b8':'#475569')}
x.textAlign='center';x.textBaseline='middle';
for(let a=0;a<360;a+=30){const k={0:'N',90:'E',180:'S',270:'W'}[a];const[px,py]=P(a,k?76:78);x.font=k?`700 ${17*s}px sans-serif`:`500 ${11*s}px ui-monospace,monospace`;x.fillStyle=a==0?'#f87171':k?'#f1f5f9':'#94a3b8';x.fillText(k||a,px,py)}
if(pos.taz!=null){const t=pos.taz%360;L(t,14,88,1.5,'#fbbf24',[3,5]);T([P(t,91),P(t,105,-7),P(t,105,7)],'#fbbf24','#060b14',1)}
if(shown!=null){const a=shown;T([P(a,0,-5),P(a,-34),P(a,0,5)],'#475569');
const[l0,r0]=[P(a,0,-7),P(a,0,7)],gr=x.createLinearGradient(...l0,...r0);gr.addColorStop(0,'#7dd3fc');gr.addColorStop(.5,'#38bdf8');gr.addColorStop(1,'#0284c7');
T([l0,P(a,86),r0],gr,'#e0f2fe',.6);x.globalAlpha=.6;L(a,6,80,.8,'#e0f2fe');x.globalAlpha=1}
x.fillStyle='#0f172a';x.beginPath();x.arc(C,C,9*s,0,7);x.fill();x.strokeStyle='#7dd3fc';x.lineWidth=2*s;x.stroke();x.fillStyle='#7dd3fc';x.beginPath();x.arc(C,C,3*s,0,7);x.fill()}
async function poll(){try{const r=await api([{c:'get'}]);pos=r[0];$('az').textContent=pos.az.toFixed(1);
if(pos.el!=null){$('el').textContent=pos.el.toFixed(1)}else $('elbox').style.display='none';
$('tgt').textContent=pos.taz!=null?('→ '+pos.taz.toFixed(1)+(pos.tel!=null?' / '+pos.tel.toFixed(1):'')):'';
$('st').innerHTML=(pos.mv?'moviendo · ':'')+'<span class="f '+(pos.f=='OK'?'okc':'bad')+'">'+pos.f+'</span>'+(pos.maz!=null&&pos.maz>=360?' · solape':'')+(pos.wind&&pos.wind.ws!=null?' · viento '+pos.wind.ws+(pos.wind.wg!=null?'/'+pos.wind.wg:'')+' m/s'+(pos.wind.bf!=null?' F'+pos.wind.bf:'')+(pos.wind.lk?' (aparcado)':''):'')+(pos.trk?' · '+(pos.trk.t=='sun'?'☀ sol ':'☾ luna ')+(pos.trk.below?'bajo el horizonte ('+pos.trk.el.toFixed(0)+'°)':pos.trk.az.toFixed(1)+' / '+pos.trk.el.toFixed(1)):'');
var T=pos.trk;if(T){$('trkc').style.display='';var NM={sun:'☀ Sol',moon:'☾ Luna',p2p:'Estación (P2P)',sat:'🛰 '+(T.name||'SAT')};$('trkName').textContent=NM[T.t]||T.t;$('trkAz').textContent=T.az!=null?T.az.toFixed(1):'--';$('trkEl').textContent=T.el!=null?T.el.toFixed(1):'--';var ex=(T.km!=null?' · '+fmtDist(T.km):'');if(T.t=='sat'){ex+=(T.maxel!=null?' · máx '+T.maxel+'°':'')+(T.los?' · LOS '+T.los:'')}$('trkInfo').innerHTML=T.below?('<span class="bad">bajo el horizonte, esperando</span>'):('<span class="okc">siguiendo</span>'+ex);}else $('trkc').style.display='none';rcSync();
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
var g=i.gps;if(i.caps&&i.caps.gps&&g){$('gpsc').style.display='';var fx=g.fix>=3?'3D':g.fix>=2?'2D':'sin fix',ok=g.fix>=2;$('gstat').className='mut '+(ok?'okc':'bad');$('gstat').textContent='● '+fx+(g.pps?' · PPS':'')+' · '+(g.sats!=null?g.sats:'?')+'/'+(g.seen!=null?g.seen:'?')+' sat';if(g.lat!=null){$('gpos').textContent=fmtLL(g.lat,g.lon);$('gmeta').innerHTML=(g.alt!=null?fmtAlt(g.alt)+' · ':'')+'<span class="f" style="color:var(--acc)">'+loc8(g.lat,g.lon)+'</span>';if(window.TEMA)TEMA.pos(g.lat,g.lon);}else{$('gpos').textContent='sin posición';$('gmeta').textContent='';}}else $('gpsc').style.display='none';rcSync();
if(i.cat&&!cat)catInit();$('info').textContent=`${i.board} (${i.chip}) · mando: ${i.drv} · IP ${i.ip} · rotctld ${i.rotctld||'off'} · rigctld ${i.rigctld||'off'} · rango AZ ${i.lim.minAz}–${i.lim.maxAz}, EL ${i.lim.minEl}–${i.lim.maxEl}`;if(i.caps&&i.caps.sd&&i.sd&&i.sd.on){const r=$('sdrow');r.style.display='';r.innerHTML=i.sd.ok?`microSD: ${i.sd.freeMB} MB libres · ${i.sd.lines} líneas · <a href="/sd" target="_blank">registro de movimientos</a>`:'microSD: sin tarjeta'}}catch(e){}}
function hdrMenu(btnId,menuId){var b=$(btnId),m=$(menuId);if(!b)return null;
b.addEventListener('click',function(e){e.stopPropagation();var open=m.classList.contains('open');document.querySelectorAll('.thm .menu.open').forEach(function(x){x.classList.remove('open')});document.querySelectorAll('.thm>button').forEach(function(x){x.setAttribute('aria-expanded','false')});if(!open){m.classList.add('open');b.setAttribute('aria-expanded','true')}});
return {b:b,m:m}}
function themeInit(){
var L={sistema:'Auto',claro:'Claro',oscuro:'Oscuro'};
var t=hdrMenu('themeBtn','themeMenu');
if(t){var p=function(){var k=TEMA.modo();t.b.textContent='◐ '+L[k];t.m.querySelectorAll('button[data-m]').forEach(function(x){x.classList.toggle('on',x.dataset.m===k)})};
t.m.addEventListener('click',function(e){var x=e.target.closest('button[data-m]');if(!x)return;TEMA.poner(x.dataset.m);p();t.m.classList.remove('open');t.b.setAttribute('aria-expanded','false')});p()}
var c=hdrMenu('cfgBtn','cfgMenu');
if(c){var sc=$('selCoord'),su=$('selUnits');
if(sc){sc.value=coordPref();sc.addEventListener('change',function(){try{localStorage.setItem('rotor-coord',sc.value)}catch(e){}info();poll()})}
if(su){su.value=unitPref();su.addEventListener('change',function(){try{localStorage.setItem('rotor-units',su.value)}catch(e){}info();poll()})}
c.m.querySelectorAll('button[data-adv]').forEach(function(btn){btn.addEventListener('click',function(){var id=btn.dataset.adv;document.querySelectorAll('.adv').forEach(function(x){x.style.display=x.id===id?'':'none'});c.m.classList.remove('open');c.b.setAttribute('aria-expanded','false');if(id==='cfgCard')loadCfg();var card=$(id);if(card)setTimeout(function(){card.scrollIntoView({behavior:'smooth',block:'start'})},30)})})}
document.addEventListener('click',function(e){if(e.target.closest('.thm'))return;document.querySelectorAll('.thm .menu.open').forEach(function(x){x.classList.remove('open')});document.querySelectorAll('.thm>button').forEach(function(x){x.setAttribute('aria-expanded','false')})});
}
themeInit();info();poll();setInterval(poll,1000);setInterval(info,5000);setInterval(raw,2000);
</script></body></html>)HTML";
