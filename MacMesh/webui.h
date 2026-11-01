// The web page: live Mac screen with sound (WebSocket on port 81) + setup (ROM, disk, Wi-Fi)
#pragma once
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Mac Mesh</title>
<style>
:root{--bg:#d9d6cf;--panel:#f4f2ec;--ink:#1d1d1b;--mute:#6b6862;--line:#b9b5ac;--accent:#2f5f8a;--bad:#a33}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
header{display:flex;align-items:center;gap:12px;padding:10px 16px;background:var(--ink);color:#eee}
header b{font:600 18px/1 ui-monospace,Menlo,Consolas,monospace;letter-spacing:.5px}
header span{color:#aaa;font-size:13px}#dot{width:10px;height:10px;border-radius:50%;background:#777}
main{max-width:1100px;margin:0 auto;padding:16px;display:grid;gap:16px}
#screenBox{background:#c9c5bb;border:1px solid var(--line);border-radius:14px;padding:14px;display:grid;gap:10px;justify-items:center}
#bezel{background:#ebe7dc;border-radius:10px;padding:18px 18px 26px;box-shadow:inset 0 0 0 1px #cfcabd,0 2px 0 #b3ad9f;width:100%;max-width:1060px}
canvas{display:block;width:100%;image-rendering:pixelated;background:#888;border-radius:3px;outline:none;cursor:none;touch-action:none}
canvas:focus{box-shadow:0 0 0 3px #8fb3d6}
.bar{display:flex;flex-wrap:wrap;gap:8px;align-items:center;justify-content:center}
button,.btn{font:inherit;padding:7px 13px;border:1px solid var(--line);border-radius:8px;background:var(--panel);color:var(--ink);cursor:pointer}
button:hover{border-color:var(--accent)}button.on{background:var(--accent);color:#fff;border-color:var(--accent)}
button.primary{background:var(--accent);color:#fff;border-color:var(--accent)}
.hint{color:var(--mute);font-size:13px;text-align:center}
.grid{display:grid;gap:16px;grid-template-columns:repeat(auto-fit,minmax(300px,1fr))}
section{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:14px 16px}
h2{margin:0 0 8px;font-size:16px}p{margin:6px 0}.mute{color:var(--mute)}.bad{color:var(--bad)}.ok{color:#2d6a2d}
input[type=text],input[type=password],select{font:inherit;padding:6px 8px;border:1px solid var(--line);border-radius:6px;width:100%;background:#fff}
input[type=file]{max-width:100%}label{display:block;margin:6px 0}
dl{display:grid;grid-template-columns:auto 1fr;gap:2px 12px;margin:0}dt{color:var(--mute)}dd{margin:0;word-break:break-word}
#msg{min-height:1.4em}#kbd{position:absolute;left:-9999px;opacity:0}
textarea{width:100%;min-height:70px;font:13px ui-monospace,Menlo,Consolas,monospace;border:1px solid var(--line);border-radius:6px;padding:6px}
</style></head><body>
<header><div id="dot"></div><b>MAC MESH</b><span id="hdr">connecting...</span></header>
<main>
<div id="screenBox">
 <div id="bezel"><canvas id="scr" width="512" height="342" tabindex="0"></canvas></div>
 <div class="bar">
  <button id="bSound">Sound: off</button>
  <button id="bCmd" title="Hold Command for the next key or click">&#8984; Command</button>
  <button id="bOpt" title="Hold Option for the next key or click">&#8997; Option</button>
  <button id="bKbd">Keyboard</button>
  <button id="bFull">Full screen</button>
  <button id="bReset">Restart Mac</button>
 </div>
 <div class="hint">Click the screen to type. Ctrl or Windows key = Command, Alt = Option. Browsers keep Ctrl+W/T/N for themselves, so use the &#8984; button for those.</div>
 <input id="kbd" autocapitalize="off" autocomplete="off" autocorrect="off" spellcheck="false">
</div>
<div class="grid">
<section><h2>Status</h2><dl id="st"></dl><p id="msg"></p></section>
<section><h2>Mac ROM</h2>
 <p class="mute">Your own Mac Plus ROM, version 3 (128 KB, checksum 4D1F8172). It isn't included.</p>
 <p id="romState"></p>
 <input type="file" id="romFile"> <button id="romUp" class="primary">Upload ROM</button>
</section>
<section><h2>Disk</h2>
 <p class="mute">A startup disk image: raw .img/.dsk or DiskCopy 4.2 (System 2&ndash;3.2 fits best). Up to ~5.6 MB.</p>
 <p id="diskState"></p>
 <input type="file" id="diskFile"> <button id="diskUp" class="primary">Upload disk</button>
 <div class="bar" style="justify-content:flex-start;margin-top:8px">
  <a class="btn" id="diskDl" href="/api/disk.img">Download disk</a>
  <button id="diskRO">Write-protect</button><button id="diskEj">Eject</button></div>
</section>
<section><h2>Machine</h2>
 <label>Mac RAM <select id="ram"><option value="0">Auto (biggest that fits)</option><option>256</option><option>128</option></select></label>
 <p class="mute">The Mac Plus needs 128 KB or 256 KB. 256 KB only fits if Wi-Fi leaves enough room; otherwise it uses 128 KB.</p>
 <div class="bar" style="justify-content:flex-start"><button id="ramSet">Apply (restarts the board)</button><button id="turbo">Turbo: off</button></div>
</section>
<section><h2>Type into the Mac</h2>
 <textarea id="txt" placeholder="Text to type on the Mac"></textarea>
 <button id="typeIt">Type it</button>
</section>
<section><h2>Wi-Fi</h2>
 <p id="wifiState" class="mute"></p>
 <button id="scan">Scan</button>
 <label>Network <select id="ssidSel"></select></label>
 <label>or name <input type="text" id="ssid"></label>
 <label>Password <input type="password" id="pass"></label>
 <button id="wifiSave" class="primary">Save &amp; restart</button>
</section>
</div>
</main>
<script>
const $=id=>document.getElementById(id);
const W=512,H=342,cv=$('scr'),cx=cv.getContext('2d'),img=cx.createImageData(W,H);
for(let i=0;i<W*H*4;i+=4){img.data[i]=img.data[i+1]=img.data[i+2]=128;img.data[i+3]=255}
cx.putImageData(img,0,0);
let ws=null,wsOk=false,stickCmd=false,stickOpt=false,lastStatus={};
function msg(t,bad){$('msg').textContent=t;$('msg').className=bad?'bad':'ok'}
async function post(u,body){const r=await fetch(u,{method:'POST',body});const t=await r.text();if(!r.ok)throw new Error(t);return t}

// ---------- screen over WebSocket
function connect(){
 ws=new WebSocket('ws://'+location.hostname+':81/');ws.binaryType='arraybuffer';
 ws.onopen=()=>{wsOk=true;$('dot').style.background='#4c4';send([0x46]);if(audio.on)send([0x61,1])};
 ws.onclose=()=>{wsOk=false;$('dot').style.background='#c44';setTimeout(connect,1500)};
 ws.onmessage=e=>{const d=new Uint8Array(e.data);
  if(d[0]==0x54){const n=(d[1]<<8)|d[2];let p=3,minY=1e9,maxY=-1;
   for(let k=0;k<n;k++){const tx=d[p],ty=d[p+1];p+=2;
    for(let r=0;r<16;r++){const v=(d[p]<<8)|d[p+1];p+=2;const y=ty*16+r;if(y>=H)continue;
     let o=(y*W+tx*16)*4;for(let b=0;b<16;b++){const c=(v&(0x8000>>b))?0:255;img.data[o]=img.data[o+1]=img.data[o+2]=c;o+=4}}
    minY=Math.min(minY,ty*16);maxY=Math.max(maxY,ty*16+16)}
   cx.putImageData(img,0,0,0,minY,W,Math.min(H,maxY)-minY);send([0x41])}
  else if(d[0]==0x53)audio.frame(d)}}
function send(a){if(wsOk&&ws.readyState==1)ws.send(new Uint8Array(a))}
connect();

// ---------- sound: Mac Plus = 370 samples per frame (~22.2 kHz), 8-bit unsigned
const audio={on:false,ctx:null,node:null,
 async start(){
  if(!this.ctx){this.ctx=new (window.AudioContext||window.webkitAudioContext)();
   const src=`class P extends AudioWorkletProcessor{constructor(){super();this.b=new Float32Array(48000);this.r=0;this.w=0;this.n=0;
    this.port.onmessage=e=>{const a=e.data;if(this.n+a.length>this.b.length*0.5){this.r=this.w;this.n=0}
     for(let i=0;i<a.length;i++){this.b[this.w]=a[i];this.w=(this.w+1)%this.b.length}this.n+=a.length}}
    process(i,o){const out=o[0][0];for(let k=0;k<out.length;k++){if(this.n>0){out[k]=this.b[this.r];this.r=(this.r+1)%this.b.length;this.n--}else out[k]=0}
     for(let c=1;c<o[0].length;c++)o[0][c].set(out);return true}}registerProcessor('macsnd',P)`;
   await this.ctx.audioWorklet.addModule(URL.createObjectURL(new Blob([src],{type:'application/javascript'})));
   this.node=new AudioWorkletNode(this.ctx,'macsnd');this.node.connect(this.ctx.destination);this.pos=0;this.last=0}
  await this.ctx.resume();this.on=true;send([0x61,1])},
 stop(){this.on=false;send([0x61,0]);if(this.ctx)this.ctx.suspend()},
 frame(d){if(!this.node)return;const rate=this.ctx.sampleRate,step=22200/rate;
  const silent=d[1]==0xFF,vol=silent?0:d[1]/7*0.6,n=Math.floor((370-this.pos)/step);const o=new Float32Array(Math.max(n,0));
  for(let k=0;k<o.length;k++){const x=this.pos+k*step,i=Math.floor(x),f=x-i;
   if(silent)o[k]=0;else{const a=d[2+i],b=i+1<370?d[2+i+1]:a;o[k]=((a+(b-a)*f)-128)/128*vol}}
  this.pos=this.pos+o.length*step-370;this.node.port.postMessage(o)}};
$('bSound').onclick=async()=>{try{if(audio.on)audio.stop();else await audio.start()}catch(e){msg('Sound needs a newer browser: '+e.message,1)}
 $('bSound').textContent='Sound: '+(audio.on?'on':'off');$('bSound').classList.toggle('on',audio.on)};

// ---------- mouse / touch
let btn=0,mx=0,my=0;
function pos(e){const r=cv.getBoundingClientRect();mx=Math.max(0,Math.min(W-1,Math.floor((e.clientX-r.left)*W/r.width)));my=Math.max(0,Math.min(H-1,Math.floor((e.clientY-r.top)*H/r.height)))}
function sendMouse(){send([0x4D,mx>>8,mx&255,my>>8,my&255,btn])}
let mvTimer=0;
cv.addEventListener('pointermove',e=>{pos(e);if(!mvTimer)mvTimer=setTimeout(()=>{mvTimer=0;sendMouse()},16)});
cv.addEventListener('pointerdown',e=>{cv.focus();cv.setPointerCapture(e.pointerId);pos(e);
 if(stickCmd)keyRaw(0xffeb,1);if(stickOpt)keyRaw(0xffe9,1);btn=1;sendMouse();e.preventDefault()});
cv.addEventListener('pointerup',e=>{pos(e);btn=0;sendMouse();unstick()});
cv.addEventListener('contextmenu',e=>e.preventDefault());

// ---------- keyboard -> X11 keysyms (same as VNC)
const SPECIAL={Backspace:0xff08,Tab:0xff09,Enter:0xff0d,Escape:0xff1b,Delete:0xffff,ArrowLeft:0xff51,ArrowUp:0xff52,ArrowRight:0xff53,ArrowDown:0xff54,
 Shift:0xffe1,Control:0xffe3,Alt:0xffe9,AltGraph:0xfe03,Meta:0xffeb,OS:0xffeb,CapsLock:0xffe5,Clear:0xff0b,NumLock:0xff7f};
const PAD={NumpadEnter:0xff8d,NumpadAdd:0xffab,NumpadSubtract:0xffad,NumpadMultiply:0xffaa,NumpadDivide:0xffaf,NumpadDecimal:0xffae,NumpadEqual:0xffbd};
function keysym(e){if(/^Numpad\d$/.test(e.code))return 0xffb0+ +e.code[6];if(PAD[e.code])return PAD[e.code];
 if(SPECIAL[e.key])return SPECIAL[e.key];if(e.key.length==1){const c=e.key.charCodeAt(0);return c<256?c:0}return 0}
function keyRaw(k,d){send([0x4B,d?1:0,k>>>24,(k>>16)&255,(k>>8)&255,k&255])}
const downKeys={};
function unstick(){if(stickCmd){keyRaw(0xffeb,0);stickCmd=false;$('bCmd').classList.remove('on')}if(stickOpt){keyRaw(0xffe9,0);stickOpt=false;$('bOpt').classList.remove('on')}}
cv.addEventListener('keydown',e=>{const k=keysym(e);if(!k)return;e.preventDefault();
 if(!(e.code in downKeys)){if(stickCmd)keyRaw(0xffeb,1);if(stickOpt)keyRaw(0xffe9,1)}
 downKeys[e.code]=k;keyRaw(k,1)});
cv.addEventListener('keyup',e=>{const k=downKeys[e.code]||keysym(e);if(!k)return;e.preventDefault();delete downKeys[e.code];keyRaw(k,0);
 if(k<0xff00||k>=0xffb0&&k<=0xffb9)unstick()});
cv.addEventListener('blur',()=>{for(const c in downKeys)keyRaw(downKeys[c],0);for(const c in downKeys)delete downKeys[c]});
$('bCmd').onclick=()=>{stickCmd=!stickCmd;$('bCmd').classList.toggle('on',stickCmd);if(!stickCmd)keyRaw(0xffeb,0);cv.focus()};
$('bOpt').onclick=()=>{stickOpt=!stickOpt;$('bOpt').classList.toggle('on',stickOpt);if(!stickOpt)keyRaw(0xffe9,0);cv.focus()};
// phones: a hidden text box brings up the on-screen keyboard; what's typed is sent as text
$('bKbd').onclick=()=>{$('kbd').value='';$('kbd').focus()};
$('kbd').addEventListener('input',e=>{const v=$('kbd').value;if(v){sendText(v);$('kbd').value=''}});
$('kbd').addEventListener('keydown',e=>{const k={Backspace:0xff08,Enter:0xff0d}[e.key];if(k){e.preventDefault();keyRaw(k,1);keyRaw(k,0)}});
function sendText(t){const b=new TextEncoder().encode(t);for(let i=0;i<b.length;i+=1000)send([0x50,...b.slice(i,i+1000)])}
cv.addEventListener('paste',e=>{const t=e.clipboardData.getData('text');if(t){sendText(t);e.preventDefault()}});
$('typeIt').onclick=()=>{sendText($('txt').value);msg('Typing...')};
$('bFull').onclick=()=>{const b=$('bezel');(b.requestFullscreen||b.webkitRequestFullscreen||(()=>{})).call(b);cv.focus()};
$('bReset').onclick=async()=>{if(confirm('Restart the Mac? Unsaved work on the Mac is lost.')){await post('/api/reset');msg('Mac restarted')}};

// ---------- status + settings
function kb(n){return Math.round(n/1024)+' KB'}
async function status(){try{const s=await (await fetch('/api/status')).json();lastStatus=s;
 const rows=[['Mac',s.state],['RAM',s.ram?kb(s.ram):'-'],['Speed',s.speed+'% of a Mac Plus'+(s.turbo?' (turbo)':'')],['Viewers',(s.web?'browser ':'')+(s.vnc?'VNC':'')||'none'],
  ['Address',s.ip+'  (VNC :5900)'],['Free memory',kb(s.heap)+' (largest piece '+kb(s.heapBlock||0)+')']];
 $('st').innerHTML=rows.map(r=>'<dt>'+r[0]+'</dt><dd>'+esc(String(r[1]))+'</dd>').join('');
 $('hdr').textContent=s.state+' · '+(s.ram?kb(s.ram):'');
 $('romState').innerHTML=s.romOk?'<span class="ok">ROM loaded: '+esc(s.romName)+'</span>':'<span class="bad">No ROM yet - upload yours to start the Mac.</span>';
 $('diskState').innerHTML=s.disk?('<b>'+esc(s.diskName||'disk')+'</b> &middot; '+kb(s.disk)+(s.diskInfo?' &middot; '+esc(s.diskInfo):'')+(s.diskRO?' &middot; write-protected':'')):'<span class="mute">No disk - the Mac will show a disk with a question mark.</span>';
 $('diskRO').classList.toggle('on',!!s.diskRO);$('turbo').textContent='Turbo: '+(s.turbo?'on':'off');$('turbo').classList.toggle('on',!!s.turbo);
 $('ram').value=String(s.ramSetting);$('wifiState').textContent=s.mode=='ap'?'Hotspot mode: join MacMesh-Setup, then pick your network.':'Connected to '+s.ssid+' ('+s.rssi+' dBm)';
}catch(e){}}
function esc(t){return t.replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
status();setInterval(status,2500);
async function upload(url,file,label){if(!file){msg('Pick a file first',1);return}
 msg('Uploading '+label+'... (the Mac pauses meanwhile)');const fd=new FormData();fd.append('f',file,file.name);
 try{const t=await post(url,fd);msg(t+' Restarting...');setTimeout(()=>location.reload(),6000)}catch(e){msg(e.message,1)}}
$('romUp').onclick=()=>upload('/api/rom',$('romFile').files[0],'ROM');
$('diskUp').onclick=()=>upload('/api/disk',$('diskFile').files[0],'disk image');
$('diskRO').onclick=async()=>{await post('/api/diskro?on='+(lastStatus.diskRO?0:1));msg('Write-protect changed - restarting the Mac');status()};
$('diskEj').onclick=async()=>{if(confirm('Remove the disk image from the board? Download it first if you want to keep changes.')){await post('/api/eject');msg('Disk removed. Restarting...');setTimeout(()=>location.reload(),6000)}};
$('ramSet').onclick=async()=>{await post('/api/ram?kb='+$('ram').value);msg('Restarting the board...');setTimeout(()=>location.reload(),7000)};
$('turbo').onclick=async()=>{await post('/api/turbo?on='+(lastStatus.turbo?0:1));status()};
$('scan').onclick=async()=>{$('scan').textContent='Scanning...';const l=await (await fetch('/api/scan')).json();
 $('ssidSel').innerHTML='<option value="">(choose)</option>'+l.map(n=>'<option>'+esc(n.s)+'</option>').join('');$('scan').textContent='Scan'};
$('ssidSel').onchange=()=>{$('ssid').value=$('ssidSel').value};
$('wifiSave').onclick=async()=>{const f=new URLSearchParams({ssid:$('ssid').value,pass:$('pass').value});await post('/api/wifi',f);msg('Saved. The board is restarting onto your network - find its new address on the OLED.')};
</script></body></html>)HTML";
