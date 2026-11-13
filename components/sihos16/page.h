// sihos16 — strona konfiguracji liczników (http://<moduł>/sihos16).
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#pragma once

namespace esphome::sihos16 {

static const char PAGE_HTML[] = R"HTML(<!doctype html>
<html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SIHOS16 – liczniki</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mut:#667085;--line:#e4e7ec;--acc:#0b6bcb;--ok:#14804a;--bad:#c4320a}
@media(prefers-color-scheme:dark){:root{--bg:#111418;--card:#1a1f26;--fg:#e6e9ee;--mut:#98a2b3;--line:#2a313b;--acc:#4ea1ff;--ok:#47c27f;--bad:#ff7a59}}
*{box-sizing:border-box}body{margin:0;font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg)}
header{padding:14px 16px;background:var(--card);border-bottom:1px solid var(--line);display:flex;gap:12px;align-items:baseline;flex-wrap:wrap}
header b{font-size:18px}header span{color:var(--mut);font-size:13px}
main{max-width:980px;margin:0 auto;padding:16px}
h2{font-size:16px;margin:22px 0 10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px;margin-bottom:12px}
.meter h3{margin:0 0 4px;font-size:16px}.mut{color:var(--mut);font-size:13px}
.vals{display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:6px 14px;margin:10px 0}
.vals div{display:flex;justify-content:space-between;gap:8px;border-bottom:1px dashed var(--line);padding:2px 0}
.vals b{font-variant-numeric:tabular-nums;white-space:nowrap}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
button{font:inherit;border:1px solid var(--line);background:var(--card);color:var(--fg);border-radius:8px;padding:6px 12px;cursor:pointer}
button.pri{background:var(--acc);border-color:var(--acc);color:#fff}button.del{color:var(--bad)}
button:disabled{opacity:.5;cursor:default}
table{width:100%;border-collapse:collapse;font-size:14px}th,td{text-align:left;padding:6px 8px;border-bottom:1px solid var(--line)}
th{color:var(--mut);font-weight:500}td.num{font-variant-numeric:tabular-nums}
.scroll{overflow-x:auto}
label{display:block;font-size:13px;color:var(--mut);margin:10px 0 4px}
input,select{font:inherit;width:100%;padding:8px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--fg)}
.fields{margin-top:10px}.fields label{display:flex;gap:8px;align-items:center;color:var(--fg);font-size:14px;margin:0;padding:4px 0;border-bottom:1px dashed var(--line)}
.fields input{width:auto}.fields .v{margin-left:auto;font-variant-numeric:tabular-nums;color:var(--mut)}
.ok{color:var(--ok)}.bad{color:var(--bad)}.msg{margin-top:10px}
#form{display:none}.grid2{display:grid;grid-template-columns:1fr 1fr;gap:0 12px}@media(max-width:600px){.grid2{grid-template-columns:1fr}}
</style></head><body>
<header><b id="dname">SIHOS16</b><span>liczniki wM-Bus · SIH Reader 1.6</span><span id="radio"></span></header>
<main>
<div class="card" id="wifi"><h3 style="margin:0">Sieć WiFi</h3><div class="mut" id="wstat">Sprawdzam…</div>
 <div id="wform" style="display:none">
  <div class="grid2">
   <div><label>Sieć</label><select id="w_ssid" onchange="this.dataset.touched=1;wsel()"></select></div>
   <div id="w_other" style="display:none"><label>Nazwa sieci (SSID)</label><input id="w_ssid2" maxlength="32" autocomplete="off"></div>
   <div><label>Hasło</label><input id="w_psk" type="password" autocomplete="new-password" placeholder="puste = sieć otwarta"></div>
  </div>
  <div class="row msg"><button class="pri" onclick="wsave()">Zapisz i połącz</button><button onclick="wclose()" id="wcancel">Anuluj</button><span id="wmsg" class="mut"></span></div>
 </div>
 <div class="row msg" id="wbtn"><button onclick="wopen()">Zmień sieć WiFi</button></div>
</div>
<h2>Moje liczniki</h2><div id="meters"></div>
<div class="card" id="form">
 <h3 id="ftitle" style="margin:0">Dodaj licznik</h3>
 <div class="grid2">
  <div><label>Numer licznika (ID)</label><input id="f_id" placeholder="np. 72157740"></div>
  <div><label>Nazwa w Home Assistant</label><input id="f_name" maxlength="31" placeholder="np. Prąd dom"></div>
  <div><label>Klucz AES (32 znaki hex lub 16 znaków tekstu)</label><input id="f_key" autocomplete="off" placeholder="puste = licznik bez szyfrowania"></div>
  <div><label>Sterownik</label><select id="f_drv"></select></div>
 </div>
 <div class="row msg"><button onclick="preview()">Sprawdź i pokaż pola</button><span id="pmsg" class="mut"></span></div>
 <div class="fields" id="f_fields"></div>
 <div class="row msg"><button class="pri" id="savebtn" onclick="save()">Zapisz i uruchom ponownie</button><button onclick="closeForm()">Anuluj</button><span id="smsg" class="mut"></span></div>
</div>
<div class="row"><button class="pri" onclick="openForm()">+ Dodaj licznik ręcznie</button><span class="mut" id="slots"></span></div>
<h2>Słyszane w okolicy</h2>
<div class="card scroll"><table><thead><tr><th>Producent</th><th>Numer</th><th>Rodzaj</th><th>Sygnał</th><th>Ramek</th><th>Ostatnio</th><th>Stan</th><th></th></tr></thead><tbody id="heard"></tbody></table>
<p class="mut">Lista odświeża się co 5 s. Ramki zaszyfrowane pokazują „brak klucza” — klucz wpisujesz przy dodawaniu.</p></div>
<h2>Diody</h2>
<div class="card" id="leds"><div class="grid2">
 <div id="l_rgbw"><label>Dioda RGB (stan modułu)</label><select id="l_rgb" onchange="lsave()"><option value="1">włączona</option><option value="0">wyłączona</option></select></div>
 <div id="l_brw"><label>Jasność diody RGB: <span id="l_brv"></span>%</label><input type="range" id="l_br" min="1" max="100" oninput="$('l_brv').textContent=this.value" onchange="lsave()"></div>
 <div id="l_blw"><label>Dioda odbioru</label><select id="l_bl" onchange="lsave()"><option value="1">mruga przy ramkach moich liczników</option><option value="2">mruga przy każdej ramce</option><option value="0">wyłączona</option></select></div>
</div><p class="mut">Kolory: <b style="color:#3b82f6">niebieski</b> — hotspot konfiguracji, <b style="color:#ef4444">czerwony</b> — brak WiFi, <b style="color:#eab308">żółty</b> — WiFi bez Home Assistant, <b style="color:#22c55e">zielony</b> — połączono z Home Assistant, biały miga — przytrzymany przycisk BOOT.</p></div>
<h2>Moduł</h2>
<div class="card">
 <div class="mut" id="sysinfo"></div>
 <label>Aktualizacja oprogramowania (plik .bin z ESPHome)</label>
 <div class="row"><input type="file" id="fw" accept=".bin" style="width:auto;flex:1"><button onclick="fwup()">Wgraj</button></div><div class="mut" id="fwmsg"></div>
 <div class="row msg"><button class="del" onclick="freset()">Przywróć ustawienia fabryczne</button><span class="mut">kasuje sieć WiFi i wszystkie liczniki; moduł uruchomi hotspot konfiguracji<span id="btnhint"></span></span></div>
</div>
</main>
<script>
const $=id=>document.getElementById(id);let S=null,edit=null,prev=null,Y=null,wopened=false,lfilled=false;
const esc=s=>String(s??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const ago=s=>s<0?'—':s<60?s+' s temu':s<3600?Math.floor(s/60)+' min temu':Math.floor(s/3600)+' h temu';
const fmt=(v,u)=>v===undefined?'—':(typeof v==='number'?(+v.toFixed(3)).toLocaleString('pl-PL'):esc(v))+(u?' '+esc(u):'');
async function load(){
 if(!$('meters'))return;
 try{S=await (await fetch('/sihos16/api/state')).json();}catch(e){$('radio').textContent='brak połączenia z modułem';return}
 const r=S.radio;$('radio').innerHTML=r.ok?`radio: ${r.frames} ramek · CRC ${r.crc} · urwane ${r.truncated}`:'<span class="bad">CC1101 nie odpowiada</span>';
 $('meters').innerHTML=S.meters.length?S.meters.map(m=>`<div class="card meter"><h3>${esc(m.name)}</h3>
  <div class="mut">${esc(m.mfct)} ${m.id} · ${esc(m.media)} · ${m.driver} · ${m.has_key?'z kluczem':'bez klucza'} · ${m.seen<0?'jeszcze nie odebrano':ago(m.seen)+', '+m.rssi+' dBm, ramek '+m.frames} · <span class="${m.status=='OK'?'ok':'bad'}">${esc(m.status)}</span></div>
  <div class="vals">${m.fields.map(f=>`<div><span>${esc(f.label)}</span><b>${fmt(f.value,f.unit)}</b></div>`).join('')||'<span class="mut">Nie wybrano pól — kliknij Edytuj.</span>'}</div>
  <div class="row"><button onclick="openForm(${m.slot})">Edytuj</button><button class="del" onclick="del(${m.slot})">Usuń</button></div></div>`).join(''):'<div class="card mut">Nie ma jeszcze liczników. Wybierz licznik z listy „Słyszane w okolicy” albo dodaj go ręcznie.</div>';
 $('slots').textContent=`zajęte ${S.meters.length} z ${S.max_meters} miejsc`;
 const h=[...S.heard].sort((a,b)=>b.rssi-a.rssi);
 $('heard').innerHTML=h.map(x=>`<tr><td>${esc(x.mfct)}</td><td class="num">${x.id}</td><td>${esc(x.media)} <span class="mut">${x.driver}</span></td><td class="num">${x.rssi} dBm</td><td class="num">${x.count}</td><td>${ago(x.seen)}</td><td class="${x.status=='OK'?'ok':'mut'}">${esc(x.status)}</td><td><button onclick="addHeard('${x.id}','${x.mfct}')">Dodaj</button></td></tr>`).join('')||'<tr><td colspan="8" class="mut">Jeszcze nic nie odebrano.</td></tr>';
 if(!$('f_drv').options.length)$('f_drv').innerHTML=S.drivers.map(d=>`<option>${d}</option>`).join('');
}
async function sys(){
 if(!$('wstat'))return;
 try{Y=await (await fetch('/sihos16/api/system')).json()}catch(e){return}
 const w=Y.wifi;$('dname').textContent=Y.name;
 $('wstat').innerHTML=w.connected?`Połączono z <b>${esc(w.ssid)}</b> · ${w.rssi} dBm · adres <b>${esc(w.ip)}</b> · http://${esc(Y.host)}.local`
  :(w.ap?'<span class="bad">Moduł nie ma jeszcze sieci WiFi.</span> Wybierz swoją sieć, wpisz hasło i zapisz — dane zostaną w pamięci modułu.':'<span class="bad">Brak połączenia z siecią WiFi.</span>');
 if(!w.connected&&!wopened)wopen();$('wcancel').style.display=w.connected?'':'none';
 const sel=$('w_ssid');if(document.activeElement!==sel){const cur=sel.value;
  const nets=[...Y.scan].sort((a,b)=>b.rssi-a.rssi);
  sel.innerHTML=nets.map(n=>`<option value="${esc(n.ssid)}">${esc(n.ssid)} (${n.rssi} dBm${n.lock?', hasło':''})</option>`).join('')+'<option value="">Inna sieć (wpisz nazwę)…</option>';
  if(sel.dataset.touched&&[...sel.options].some(o=>o.value===cur))sel.value=cur;wsel()}
 const L=Y.leds;$('l_rgbw').style.display=$('l_brw').style.display=Y.has_rgb?'':'none';$('l_blw').style.display=Y.has_blink?'':'none';
 if(!lfilled){lfilled=true;$('l_rgb').value=L.rgb;$('l_br').value=L.brightness;$('l_brv').textContent=L.brightness;$('l_bl').value=L.blink}
 $('btnhint').textContent=Y.has_button?' (albo przytrzymaj przycisk BOOT na module przez 10 s)':'';
 const u=Y.uptime;$('sysinfo').textContent=`${Y.name} · ESPHome ${Y.esphome} · działa ${u<3600?Math.floor(u/60)+' min':Math.floor(u/3600)+' h'}`;
}
function wopen(){wopened=true;$('wform').style.display='block';$('wbtn').style.display='none'}
function wclose(){$('wform').style.display='none';$('wbtn').style.display=''}
function wsel(){$('w_other').style.display=$('w_ssid').value===''?'block':'none'}
async function wsave(){
 const ssid=$('w_ssid').value||$('w_ssid2').value.trim(),psk=$('w_psk').value;
 if(!ssid){$('wmsg').textContent='Wybierz albo wpisz nazwę sieci.';return}
 if(psk&&psk.length<8){$('wmsg').innerHTML='<span class="bad">Hasło WiFi ma co najmniej 8 znaków.</span>';return}
 let r;try{r=await (await fetch('/sihos16/api/wifi',{method:'POST',body:new URLSearchParams({ssid,psk})})).json()}catch(e){r={ok:false,error:'Brak odpowiedzi modułu.'}}
 if(!r.ok){$('wmsg').innerHTML=`<span class="bad">${esc(r.error)}</span>`;return}
 const host=Y?Y.host:'sih-reader';
 document.body.innerHTML=`<main><div class="card"><h3>Zapisano sieć „${esc(ssid)}”</h3><p>Moduł łączy się teraz z Twoją siecią i za chwilę wyłączy swój hotspot.</p>
 <p>Połącz telefon lub komputer z siecią <b>${esc(ssid)}</b> i otwórz <b>http://${esc(host)}.local</b> albo adres modułu odczytany z routera. Home Assistant sam znajdzie nowe urządzenie ESPHome.</p>
 <p class="mut">Jeśli hasło było błędne, moduł po około minucie znowu uruchomi hotspot — połącz się z nim ponownie i popraw dane.</p></div></main>`;
}
async function lsave(){
 await fetch('/sihos16/api/leds',{method:'POST',body:new URLSearchParams({rgb:$('l_rgb').value,brightness:$('l_br').value,blink:$('l_bl').value})});
}
async function fwup(){
 const f=$('fw').files[0];if(!f){$('fwmsg').textContent='Wybierz plik .bin.';return}
 if(!confirm(`Wgrać „${f.name}” (${Math.round(f.size/1024)} kB)? Moduł uruchomi się ponownie.`))return;
 const fd=new FormData();fd.append('update',f,f.name);$('fwmsg').textContent='Wgrywam… nie wyłączaj modułu.';
 try{const r=await fetch('/update',{method:'POST',body:fd});
  if(r.status===404){$('fwmsg').innerHTML='<span class="bad">Ta wersja nie obsługuje aktualizacji ze strony (w YAML brak <i>ota: - platform: web_server</i>).</span>';return}
  if(!r.ok){$('fwmsg').innerHTML=`<span class="bad">Błąd aktualizacji (${r.status}).</span>`;return}
 }catch(e){}
 waitReboot();
}
async function freset(){
 if(!confirm('Przywrócić ustawienia fabryczne? Zostaną skasowane: sieć WiFi i wszystkie liczniki.'))return;
 if(!confirm('Na pewno? Moduł straci połączenie z siecią i uruchomi hotspot konfiguracji.'))return;
 await fetch('/sihos16/api/factory_reset',{method:'POST'}).catch(()=>{});
 document.body.innerHTML='<main><div class="card"><h3>Przywrócono ustawienia fabryczne</h3><p>Moduł uruchamia się ponownie i włączy hotspot konfiguracji (dioda niebieska). Połącz się z nim, aby ustawić sieć WiFi.</p></div></main>';
}
function openForm(slot){
 edit=slot===undefined?null:S.meters.find(m=>m.slot===slot);prev=null;
 $('form').style.display='block';$('ftitle').textContent=edit?'Edytuj licznik':'Dodaj licznik';
 $('f_id').value=edit?edit.id:'';$('f_id').disabled=!!edit;$('f_name').value=edit?edit.name:'';
 $('f_key').value='';$('f_key').placeholder=edit&&edit.has_key?'puste = zostaw obecny klucz':'puste = licznik bez szyfrowania';
 $('f_drv').value=edit?edit.driver:'auto';$('pmsg').textContent='';$('smsg').textContent='';
 $('f_fields').innerHTML=edit?fieldList(edit.fields.map(f=>({...f})),new Set(edit.fields.map(f=>f.key))):'';
 $('form').scrollIntoView({behavior:'smooth'});
 if(edit)preview();
}
function addHeard(id,mfct){openForm();$('f_id').value=id;$('f_id').dataset.mfct=mfct;$('f_name').focus();preview()}
function closeForm(){$('form').style.display='none';edit=null}
function keyArg(){const k=$('f_key').value.trim();return k===''&&edit&&edit.has_key?'*':k}
function fieldList(fields,checked){
 return fields.map(f=>`<label><input type="checkbox" value="${esc(f.key)}" data-text="${f.text?1:0}" ${checked.has(f.key)?'checked':''} onchange="count()"> ${esc(f.label)} <span class="mut">${f.text?'tekst':esc(f.unit)}</span><span class="v">${fmt(f.value,f.text?'':f.unit)}</span></label>`).join('')+'<div class="mut" id="fcount"></div>';
}
function count(){
 const c=[...document.querySelectorAll('#f_fields input:checked')];const n=c.filter(x=>x.dataset.text=='0').length,t=c.length-n;
 const over=n>S.max_num||t>S.max_text;$('fcount').innerHTML=`Wybrane: ${n}/${S.max_num} liczbowych, ${t}/${S.max_text} tekstowych${over?' — <span class="bad">za dużo</span>':''}`;$('savebtn').disabled=over;
}
async function preview(){
 const id=$('f_id').value.trim();if(!id){$('pmsg').textContent='Wpisz numer licznika.';return}
 $('pmsg').textContent='Sprawdzam…';
 const q=new URLSearchParams({id,key:keyArg(),driver:$('f_drv').value});
 const p=await (await fetch('/sihos16/api/preview?'+q)).json();prev=p;
 $('pmsg').innerHTML=`<span class="${p.ok?'ok':'bad'}">${esc(p.status)}</span>`+(p.ok?` · ${esc(p.media)} · sterownik ${p.driver}`:'');
 if(!p.fields.length)return;
 const old=edit?new Set(edit.fields.map(f=>f.key)):null;
 const def=new Set(p.fields.filter(f=>!f.text&&!/_s\d|month|_limit|_max|_min|raw_/.test(f.key)).slice(0,S.max_num).map(f=>f.key));
 $('f_fields').innerHTML=fieldList(p.fields,old&&old.size?old:def);count();
}
async function save(){
 const id=$('f_id').value.trim();if(!id){$('smsg').textContent='Wpisz numer licznika.';return}
 const fields=[...document.querySelectorAll('#f_fields input:checked')].map(x=>x.value).join(',');
 const b=new URLSearchParams({slot:edit?edit.slot:-1,id,mfct:$('f_id').dataset.mfct||'',name:$('f_name').value.trim(),driver:$('f_drv').value,key:keyArg(),fields});
 $('savebtn').disabled=true;$('smsg').textContent='Zapisuję…';
 let r;try{r=await (await fetch('/sihos16/api/save',{method:'POST',body:b})).json()}catch(e){r={ok:false,error:'Brak odpowiedzi modułu — spróbuj ponownie.'}}
 if(!r.ok){$('smsg').innerHTML=`<span class="bad">${esc(r.error)}</span>`;$('savebtn').disabled=false;return}
 waitReboot();
}
async function del(slot){
 const m=S.meters.find(x=>x.slot===slot);if(!confirm(`Usunąć licznik „${m.name}”? Jego encje znikną z Home Assistant.`))return;
 await fetch('/sihos16/api/delete',{method:'POST',body:new URLSearchParams({slot})});waitReboot();
}
function waitReboot(){
 document.body.innerHTML='<main><div class="card"><h3>Zapisano. Moduł uruchamia się ponownie…</h3><p class="mut">Strona odświeży się sama, gdy moduł wróci (ok. 15 s).</p></div></main>';
 setTimeout(function t(){fetch('/sihos16/api/state').then(()=>location.reload()).catch(()=>setTimeout(t,2000))},6000);
}
load();sys();setInterval(()=>{const f=$('form');if(f&&f.style.display!=='block')load();sys()},5000);
</script></body></html>)HTML";

}  // namespace esphome::sihos16
