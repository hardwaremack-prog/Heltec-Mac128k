// The web page: live Mac screen with sound (WebSocket on port 81) + setup (ROM, disk, Wi-Fi)
#pragma once
#include "heltec_config.h"
#include "macfiles_js.h"   // script URLs carry a version, so a new firmware never runs with an old cached script
#ifdef HELTEC_V4
#define ROM_HELP "Your own ROM: the Mac SE FDHD ROM (256 KB, checksum B306E171) or Mac SE ROM (B2E362A8) makes this a 1 MB Mac SE with an ADB keyboard and mouse, running System 4 to 6 from 800K and 1.44 MB disks. The Mac Plus v3 (4D1F8172) and Mac 128K/512K (28BA61CE) ROMs work too. ROMs aren't included."
#define RAM_OPTIONS "<option>1024</option><option>512</option><option>256</option><option>128</option>"
#define DISK_HINT "With the SE ROM, use System 4.1 to 6.0.8 disks: 800K or 1.44 MB HFS (or 400K MFS with System 3.2 or later)."
#else
#define ROM_HELP "Your own ROM: the Mac Plus v3 ROM (128 KB, checksum 4D1F8172), or the original Mac 128K/512K ROM (64 KB, checksum 28BA61CE), which leaves more room for apps like MacPaint 1.x on this 128K Mac. ROMs aren't included."
#define RAM_OPTIONS "<option>256</option><option>128</option>"
#define DISK_HINT "Early 400K disks (System 1&ndash;3) fit this Mac best."
#endif
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>)HTML" BOARD_NAME R"HTML(</title>
<style>
:root{--bg:#d9d6cf;--panel:#f4f2ec;--ink:#1d1d1b;--mute:#6b6862;--line:#b9b5ac;--accent:#2f5f8a;--bad:#a33}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
header{display:flex;align-items:center;gap:12px;padding:10px 16px;background:var(--ink);color:#eee}
header b{font:600 18px/1 ui-monospace,Menlo,Consolas,monospace;letter-spacing:.5px}
header span{color:#aaa;font-size:13px}#dot{width:10px;height:10px;border-radius:50%;background:#777}
main{max-width:1180px;margin:0 auto;padding:16px;display:grid;gap:16px}
#screenBox{background:#c9c5bb;border:1px solid var(--line);border-radius:14px;padding:14px;display:grid;gap:10px;justify-items:center}
#bezel{background:#ebe7dc;border-radius:10px;padding:18px 18px 26px;box-shadow:inset 0 0 0 1px #cfcabd,0 2px 0 #b3ad9f;width:100%;display:grid;justify-items:center}
#bezel:fullscreen{max-width:none;border-radius:0;align-content:center;background:#111}
canvas{display:block;width:100%;image-rendering:pixelated;image-rendering:crisp-edges;background:#888;outline:none;cursor:none;touch-action:none}
canvas:focus{box-shadow:0 0 0 3px #8fb3d6}
.bar{display:flex;flex-wrap:wrap;gap:8px;align-items:center;justify-content:center}
button,.btn{font:inherit;padding:7px 13px;border:1px solid var(--line);border-radius:8px;background:var(--panel);color:var(--ink);cursor:pointer}
button:hover{border-color:var(--accent)}button.on{background:var(--accent);color:#fff;border-color:var(--accent)}
button.primary{background:var(--accent);color:#fff;border-color:var(--accent)}button.sure{background:var(--bad);color:#fff;border-color:var(--bad)}
.hint{color:var(--mute);font-size:13px;text-align:center}
#prn{display:none;margin:10px auto 0;max-width:640px;background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:10px 14px;font-size:14px}#prn.on{display:block}#prn .job{display:flex;flex-wrap:wrap;gap:8px 10px;align-items:center;padding:4px 0}#prn .job b{flex:1 1 200px}#prn .job+.job{border-top:1px solid var(--line)}
.grid{display:grid;gap:16px;grid-template-columns:repeat(auto-fit,minmax(300px,1fr))}
section{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:14px 16px}
h2{margin:0 0 8px;font-size:16px}p{margin:6px 0}.mute{color:var(--mute)}.bad{color:var(--bad)}.ok{color:#2d6a2d}
input[type=text],input[type=password],select{font:inherit;padding:6px 8px;border:1px solid var(--line);border-radius:6px;width:100%;background:#fff}
input[type=file]{max-width:100%}label{display:block;margin:6px 0}
dl{display:grid;grid-template-columns:auto 1fr;gap:2px 12px;margin:0}dt{color:var(--mute)}dd{margin:0;word-break:break-word}
#msg{min-height:1.4em}#kbd{position:absolute;left:-9999px;opacity:0}
section.wide{grid-column:1/-1}
.disk{display:flex;flex-wrap:wrap;gap:8px 12px;align-items:center;padding:8px 0;border-top:1px solid var(--line)}
.disk .nm{flex:1 1 240px;min-width:0}.disk .nm b{word-break:break-word}.disk.cur .nm b::after{content:'  \2022  in use';color:var(--accent);font-weight:600}
textarea{width:100%;min-height:70px;font:13px ui-monospace,Menlo,Consolas,monospace;border:1px solid var(--line);border-radius:6px;padding:6px}
</style></head><body>
<header><div id="dot"></div><b>)HTML" BOARD_HEADER R"HTML(</b><span id="hdr">connecting...</span></header>
<main>
<div id="screenBox">
 <div id="bezel"><canvas id="scr" width="512" height="342" tabindex="0"></canvas></div>
 <div class="bar">
  <button id="bSound">Sound: off</button>
  <button id="bCmd" title="Hold Command for the next key or click">&#8984; Command</button>
  <button id="bOpt" title="Hold Option for the next key or click">&#8997; Option</button>
  <button id="bKbd">Keyboard</button>
  <button id="bSize" title="Auto picks sharp when it's big enough. Sharp: every Mac pixel is exactly 2x2 or 3x3 screen pixels. Fill: uses all the space, slightly soft.">Pixels: auto</button>
  <button id="bFull">Full screen</button>
  <button id="bReset">Restart Mac</button>
  <button id="bReboot" title="Restarts the whole Heltec board, like pressing RST. The disk is saved first.">Restart board</button>
  <button id="bSleep" title="Puts the Heltec board into deep sleep. Press PRG (or RST) on the board to wake it.">Sleep board</button>
 </div>
 <div class="hint">Click the screen to type. Ctrl or Windows key = Command, Alt = Option. Browsers keep Ctrl+W/T/N for themselves, so use the &#8984; button for those.</div>
 <div id="prn" aria-live="polite"></div>
 <input id="kbd" autocapitalize="off" autocomplete="off" autocorrect="off" spellcheck="false">
</div>
<div class="grid">
<section><h2>Status</h2><dl id="st"></dl><p id="msg"></p></section>
<section><h2>Mac ROM</h2>
 <p class="mute">)HTML" ROM_HELP R"HTML(</p>
 <p id="romState"></p>
 <input type="file" id="romFile"> <button id="romUp" class="primary">Upload ROM</button>
</section>
<section class="wide"><h2>Disks</h2>
 <p class="mute">Your disks are kept on the board. Click <b>Use</b> to start the Mac from one. Add disk images (.img, .dsk, DiskCopy .image) or archives with disks or programs inside (.zip, .7z, .sit incl. StuffIt 5, .hqx, .bin, .gz); archives are unpacked here in your browser. )HTML" DISK_HINT R"HTML(</p>
 <div id="lib"></div>
 <p id="libFree" class="mute"></p>
 <div class="bar" style="justify-content:flex-start"><input type="file" id="diskFile"> <button id="diskUp" class="primary">Add</button>
  <button id="diskRO" title="Stop the Mac from changing the disk in use">Write-protect</button></div>
 <div id="diskPick" hidden><p><b>Disks found. Tick the ones to add:</b></p><div id="diskList"></div>
  <label><input type="checkbox" id="useFirst" checked> <span>Start the Mac from the first ticked disk</span></label>
  <button id="diskPickUp" class="primary">Add ticked disks</button></div>
 <div id="progPick" hidden><p><b>Programs found:</b> <span id="progList"></span></p>
  <p class="mute" id="progNote"></p>
  <button id="progAdd" class="primary">Add to the disk in use</button></div>
</section>
<section><h2>Machine</h2>
 <label>Mac RAM <select id="ram"><option value="0">Auto (biggest that fits)</option>)HTML" RAM_OPTIONS R"HTML(</select></label>
 <p class="mute">On the Heltec V3, 128 KB is what fits next to Wi-Fi (a real Mac 128K). 256 KB is used only if there's room. With the Mac 128K ROM, 128 KB runs 1984 software like MacPaint and MacWrite.</p>
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
<script src=")HTML" SIT_ARSENIC_JS_URL R"HTML("></script>
<script src=")HTML" MACFILES_JS_URL R"HTML("></script>
<script src=")HTML" IMAGEWRITER_JS_URL R"HTML("></script>
<script>
const $=id=>document.getElementById(id);
const W=512,H=342,cv=$('scr'),cx=cv.getContext('2d'),img=cx.createImageData(W,H);
for(let i=0;i<W*H*4;i+=4){img.data[i]=img.data[i+1]=img.data[i+2]=128;img.data[i+3]=255}
cx.putImageData(img,0,0);
let ws=null,wsOk=false,stickCmd=false,stickOpt=false,lastStatus={};
function msg(t,bad){$('msg').textContent=t;$('msg').className=bad?'bad':'ok'}
async function post(u,body){const r=await fetch(u,{method:'POST',body});const t=await r.text();if(!r.ok)throw new Error(t);return t}

// ---------- screen over WebSocket
// A board that restarts or loses power never says goodbye, so the browser can keep a dead
// connection "open" forever (frozen screen). So: after 4 s of quiet send a tiny ping ('p'); if
// nothing comes back within 3 s, reconnect.
let lastRx=0,pingSent=0,reconnectTimer=0;
function connect(){
 clearTimeout(reconnectTimer);
 ws=new WebSocket('ws://'+location.hostname+':81/');ws.binaryType='arraybuffer';
 ws.onopen=()=>{wsOk=true;lastRx=Date.now();pingSent=0;$('dot').style.background='#4c4';send([0x46]);if(audio.on)send([0x61,1]);sendClock()};
 ws.onclose=()=>{wsOk=false;$('dot').style.background='#c44';clearTimeout(reconnectTimer);reconnectTimer=setTimeout(connect,1500)};
 ws.onmessage=e=>{const d=new Uint8Array(e.data);lastRx=Date.now();pingSent=0;if(d[0]==0x70)return;
  if(d[0]==0x54){const n=(d[1]<<8)|d[2];let p=3,minY=1e9,maxY=-1;
   for(let k=0;k<n;k++){const tx=d[p],ty=d[p+1];p+=2;
    for(let r=0;r<16;r++){const v=(d[p]<<8)|d[p+1];p+=2;const y=ty*16+r;if(y>=H)continue;
     let o=(y*W+tx*16)*4;for(let b=0;b<16;b++){const c=(v&(0x8000>>b))?0:255;img.data[o]=img.data[o+1]=img.data[o+2]=c;o+=4}}
    minY=Math.min(minY,ty*16);maxY=Math.max(maxY,ty*16+16)}
   cx.putImageData(img,0,0,0,minY,W,Math.min(H,maxY)-minY);send([0x41])}
  else if(d[0]==0x53)audio.bundle(d)
  else if(d[0]==0x52)printer.data(d.subarray(1))}}
function send(a){if(wsOk&&ws.readyState==1)ws.send(new Uint8Array(a))}
function reconnect(){const old=ws;wsOk=false;if(old){old.onclose=null;old.onmessage=null;try{old.close()}catch(e){}}
 $('dot').style.background='#c44';connect()}
setInterval(()=>{if(!wsOk||document.hidden)return;const now=Date.now();
 if(pingSent&&now-pingSent>3000){reconnect();return}
 if(!pingSent&&now-lastRx>4000){pingSent=now;send([0x70])}},1000);
// Set the Mac's clock to this computer's local time (Mac time counts seconds from 1 Jan 1904)
function sendClock(){const d=new Date(),tz=-d.getTimezoneOffset(),t=Math.floor(d.getTime()/1000)+tz*60+2082844800;
 send([0x43,(t>>>24)&255,(t>>>16)&255,(t>>>8)&255,t&255,(tz>>8)&255,tz&255])}
// ---------- printer: the Mac prints to an ImageWriter; the board passes the bytes on, and
// this page turns each job into a PDF (web/imagewriter.js) once the Mac stops sending
const printer={chunks:[],bytes:0,timer:0,jobs:[],
 data(b){this.chunks.push(b.slice());this.bytes+=b.length;clearTimeout(this.timer);this.timer=setTimeout(()=>this.finish(),5000);this.show()},
 async finish(){const all=new Uint8Array(this.bytes);let o=0;for(const c of this.chunks){all.set(c,o);o+=c.length}
  this.chunks=[];this.bytes=0;
  try{const job=ImageWriter.render(all),d=new Date(),p=n=>String(n).padStart(2,'0');
   const name='Mac printout '+d.getFullYear()+'-'+p(d.getMonth()+1)+'-'+p(d.getDate())+' '+p(d.getHours())+'.'+p(d.getMinutes())+'.'+p(d.getSeconds())+'.pdf';
   const pdf=await ImageWriter.toPDF(job,name.replace(/\.pdf$/,''));
   const url=URL.createObjectURL(new Blob([pdf],{type:'application/pdf'}));
   this.jobs.unshift({name,url,pages:job.pages.length});
   while(this.jobs.length>4)URL.revokeObjectURL(this.jobs.pop().url)}
  catch(e){this.jobs.unshift({error:e.message||String(e)})}
  this.show()},
 show(){const el=$('prn');el.classList.toggle('on',this.bytes>0||this.jobs.length>0);
  let h=this.bytes?'<div class="job"><b>&#128424; Printing... '+Math.round(this.bytes/1024)+' KB received</b></div>':'';
  h+=this.jobs.map((j,i)=>j.error?'<div class="job"><b>Couldn\'t make that printout: '+esc(j.error)+'</b></div>':
   '<div class="job"><b>&#128424; '+esc(j.name.replace(/\.pdf$/,''))+' &middot; '+j.pages+' page'+(j.pages>1?'s':'')+'</b><a class="btn" href="'+j.url+'" target="_blank" rel="noopener">Open PDF</a><a class="btn" href="'+j.url+'" download="'+esc(j.name)+'">Download</a></div>').join('');
  if(this.jobs.length)h+='<div class="job"><span class="mute" style="flex:1">Printed from the Mac (File &gt; Print). PDFs stay here until you close this page.</span><button id="prnClear">Clear</button></div>';
  el.innerHTML=h;const c=$('prnClear');if(c)c.onclick=()=>{for(const j of this.jobs)if(j.url)URL.revokeObjectURL(j.url);this.jobs=[];this.show()}}};
connect();

// ---------- sound: Mac Plus = 370 samples per frame (~22.2 kHz), 8-bit unsigned
const audio={on:false,ctx:null,node:null,
 // AudioWorklet only exists on secure (https/localhost) pages; the board serves plain http, so
 // there we play through a ScriptProcessorNode fed from this ring buffer instead.
 rb:new Float32Array(48000),r:0,w:0,n:0,target:0,playing:false,fade:0,
 async start(){
  if(!this.ctx){this.ctx=new (window.AudioContext||window.webkitAudioContext)();this.pos=0;
   if(this.ctx.audioWorklet&&window.isSecureContext){
    const src=`class P extends AudioWorkletProcessor{constructor(){super();this.b=new Float32Array(48000);this.r=0;this.w=0;this.n=0;
     this.port.onmessage=e=>{const a=e.data;if(this.n+a.length>this.b.length*0.5){this.r=this.w;this.n=0}
      for(let i=0;i<a.length;i++){this.b[this.w]=a[i];this.w=(this.w+1)%this.b.length}this.n+=a.length}}
     process(i,o){const out=o[0][0];for(let k=0;k<out.length;k++){if(this.n>0){out[k]=this.b[this.r];this.r=(this.r+1)%this.b.length;this.n--}else out[k]=0}
      for(let c=1;c<o[0].length;c++)o[0][c].set(out);return true}}registerProcessor('macsnd',P)`;
    await this.ctx.audioWorklet.addModule(URL.createObjectURL(new Blob([src],{type:'application/javascript'})));
    this.node=new AudioWorkletNode(this.ctx,'macsnd');this.worklet=true}
   else{const sp=this.ctx.createScriptProcessor(2048,0,1);
    // Jitter buffer: wait until `target` samples are queued before playing. If Wi-Fi delivers
    // late and it runs dry mid-sound, pause cleanly and grow the cushion (0.1 s at first, up to 0.4 s) instead
    // of crackling; if it builds up, play 2% faster until it's back down.
    sp.onaudioprocess=e=>{const out=e.outputBuffer.getChannelData(0),b=this.rb,L=b.length,rate=this.ctx.sampleRate;
     if(!this.target)this.target=Math.round(rate*0.1);
     if(!this.playing&&this.n>=this.target){this.playing=true;this.fade=0}
     for(let k=0;k<out.length;k++){
      if(this.playing&&this.n>0){let v=b[this.r];this.r=(this.r+1)%L;this.n--;
       if(k%50==0&&this.n>this.target*2+rate*0.1){this.r=(this.r+1)%L;this.n--}
       if(this.fade<128){v*=this.fade/128;this.fade++}
       out[k]=v}
      else{if(this.playing){this.playing=false;
        if(performance.now()-(this.last||0)<120)this.target=Math.min(this.target+Math.round(rate*0.06),Math.round(rate*0.4))}
       out[k]=0}}};
    this.node=sp;this.worklet=false}
   this.node.connect(this.ctx.destination)}
  await this.ctx.resume();this.on=true;send([0x61,1])},
 stop(){this.on=false;send([0x61,0]);if(this.ctx)this.ctx.suspend()},
 push(o){if(this.worklet){this.node.port.postMessage(o);return}
  const b=this.rb,L=b.length;if(this.n+o.length>L*0.5){this.r=this.w;this.n=0}   // too far behind: drop the backlog
  for(let i=0;i<o.length;i++){b[this.w]=o[i];this.w=(this.w+1)%L}this.n+=o.length},
 // A bundle: 'S' n, then n frames of {vol, 370 samples} (vol 0xFF = silent frame, no samples).
 // After a gap (silence isn't sent), start with ~80 ms of quiet so Wi-Fi hiccups don't cut the sound.
 bundle(d){if(!this.node)return;const rate=this.ctx.sampleRate,step=22200/rate,now=performance.now();
  if(now-(this.last||0)>150){this.pos=0;if(this.worklet)this.push(new Float32Array(Math.round(rate*0.08)))}
  this.last=now;let p=2;
  for(let f=0;f<d[1];f++){const v=d[p++],silent=v==0xFF,vol=silent?0:v/7*0.6,s0=p;if(!silent)p+=370;
   const n=Math.floor((370-this.pos)/step),o=new Float32Array(Math.max(n,0));
   for(let k=0;k<o.length;k++){const x=this.pos+k*step,i=Math.floor(x),fr=x-i;
    if(silent)o[k]=0;else{const a=d[s0+i],b=i+1<370?d[s0+i+1]:a;o[k]=((a+(b-a)*fr)-128)/128*vol}}
   this.pos=this.pos+o.length*step-370;this.push(o)}}};
// Sound is on by default (remembered per browser). Browsers only allow sound after a click or key,
// so it starts on the first one anywhere on the page.
let soundWanted=true;try{soundWanted=localStorage.getItem('hmSound')!=='0'}catch(e){}
function soundBtn(){$('bSound').textContent=soundWanted?(audio.on?'Sound: on':'Sound: on (click the screen)'):'Sound: off';$('bSound').classList.toggle('on',soundWanted&&audio.on)}
async function unlockSound(e){if(!soundWanted||audio.on||(e&&e.target&&e.target.id=='bSound'))return;
 try{await audio.start()}catch(err){msg('Couldn\'t start sound: '+err.message,1)}soundBtn()}
['pointerdown','pointerup','keydown','touchend'].forEach(t=>document.addEventListener(t,unlockSound,true));
$('bSound').onclick=async()=>{soundWanted=!soundWanted;try{localStorage.setItem('hmSound',soundWanted?'1':'0')}catch(e){}
 try{if(soundWanted)await audio.start();else audio.stop()}catch(e){msg('Couldn\'t start sound: '+e.message,1)}soundBtn()};
soundBtn();

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
// Sharp pixels: size the screen so each Mac pixel covers a whole number of real screen pixels
// (512 x k device pixels). Any other size makes nearest-neighbour scaling uneven, which looks fuzzy.
let sizeMode='auto';try{sizeMode=localStorage.getItem('mmSize')||'auto'}catch(e){}
function fitScreen(){
 const b=$('bezel'),fs=document.fullscreenElement===b,dpr=window.devicePixelRatio||1;
 const cs=getComputedStyle(b),availW=b.clientWidth-parseFloat(cs.paddingLeft)-parseFloat(cs.paddingRight);
 // (in fullscreen the whole window is available)
 const availH=fs?window.innerHeight-40:Infinity;
 const k=Math.min(Math.floor(availW*dpr/512),Math.floor(availH*dpr/342));
 const fillW=Math.min(availW,availH*512/342),sharpW=512*k/dpr;
 // auto: sharp unless that would waste lots of space; on dense (phone/retina) screens fill looks fine
 const sharp=sizeMode=='sharp'?k>=1:sizeMode=='fill'?false:(k>=1&&dpr<2&&sharpW>=0.7*fillW);
 const w=sharp?sharpW:fillW;cv.style.width=w+'px';cv.style.height=(w*342/512)+'px';
 $('bSize').textContent='Pixels: '+(sizeMode=='auto'?'auto ('+(sharp?'sharp':'fill')+')':sizeMode);$('bSize').classList.toggle('on',sharp)}
$('bSize').onclick=()=>{sizeMode={auto:'sharp',sharp:'fill',fill:'auto'}[sizeMode]||'auto';try{localStorage.setItem('mmSize',sizeMode)}catch(e){}fitScreen()};
window.addEventListener('resize',fitScreen);document.addEventListener('fullscreenchange',fitScreen);
(window.matchMedia?matchMedia('(resolution: '+(window.devicePixelRatio||1)+'dppx)'):null)?.addEventListener?.('change',fitScreen);
fitScreen();
$('bReboot').onclick=async()=>{if(!confirm('Restart the whole Heltec board? The disk is saved first; unsaved work on the Mac is lost.'))return;
 try{await post('/api/reboot')}catch(e){}msg(lastStatus.mode=='ap'?'Restarting the board. Its hotspot is off for a few seconds; if this computer doesn\'t rejoin )HTML" BOARD_AP_SSID R"HTML( by itself, pick it in your Wi-Fi list.':'Restarting the board...');waitForBoard()};
$('bSleep').onclick=async()=>{if(!confirm('Put the Heltec board to sleep? The disk is saved first. Press PRG (or RST) on the board to wake it.'))return;
 try{await post('/api/sleep')}catch(e){}asleep=true;msg('The board is asleep. Press PRG (or RST) on the board to wake it; this page reconnects by itself.');
 await new Promise(r=>setTimeout(r,4000));
 for(;;){try{const r=await fetch('/api/status',{cache:'no-store'});if(r.ok)break}catch(e){}await new Promise(r=>setTimeout(r,2000))}
 asleep=false;reconnect();await status();msg('Awake. Starting the Mac...');setTimeout(()=>msg(''),4000)};
$('bReset').onclick=async()=>{if(confirm('Restart the Mac? Unsaved work on the Mac is lost.')){await post('/api/reset');msg('Mac restarted')}};

// ---------- status + settings
function kb(n){return Math.round(n/1024)+' KB'}
let boardWasDown=false,asleep=false;
async function status(){try{const s=await (await fetch('/api/status',{cache:'no-store'})).json();lastStatus=s;
 if(boardWasDown){boardWasDown=false;reconnect()}
 const rows=[['Mac',s.state],['RAM',s.ram?kb(s.ram):'-'],['Speed',s.speed+'% of real speed'+(s.turbo?' (turbo)':'')],['Viewers',(s.web?'browser ':'')+(s.vnc?'VNC':'')||'none'],
  ['Battery',s.batText||'-'],['Up',s.uptime!==undefined?(s.uptime<120?s.uptime+' s':Math.round(s.uptime/60)+' min')+' · last start: '+s.reset:'-'],['Address',s.ip+'  (VNC :5900)'],['Free memory',kb(s.heap)+' (largest piece '+kb(s.heapBlock||0)+')']];
 $('st').innerHTML=rows.map(r=>'<dt>'+r[0]+'</dt><dd>'+esc(String(r[1]))+'</dd>').join('');
 $('hdr').textContent=s.state+' · '+(s.ram?kb(s.ram):'')+(s.bat>0&&s.bat<=4300?' · battery '+s.batPct+'%':'');
 $('romState').innerHTML=s.romOk?'<span class="ok">ROM loaded: '+esc(s.romName)+'</span>':'<span class="bad">No ROM yet - upload yours to start the Mac.</span>';
 renderLib(s);
 $('diskRO').classList.toggle('on',!!s.diskRO);$('turbo').textContent='Turbo: '+(s.turbo?'on':'off');$('turbo').classList.toggle('on',!!s.turbo);
 $('ram').value=String(s.ramSetting);$('wifiState').textContent=s.mode=='ap'?'Hotspot mode: join )HTML" BOARD_AP_SSID R"HTML(, then pick your network.':'Connected to '+s.ssid+' ('+s.rssi+' dBm)';
}catch(e){boardWasDown=true;if(asleep)$('hdr').textContent='Asleep · press PRG on the board to wake it'}}
// After a restart, don't reload the page (that would lose the sound you switched on with your
// click); wait for the board to answer again. The screen reconnects by itself, and the board
// holds the Mac until it has, so you hear the startup sound.
async function waitForBoard(){const t0=Date.now();await new Promise(r=>setTimeout(r,2500));
 while(Date.now()-t0<60000){try{const r=await fetch('/api/status',{cache:'no-store'});if(r.ok){reconnect();await status();msg('Back. Starting the Mac...');setTimeout(()=>msg(''),4000);return}}catch(e){}
  await new Promise(r=>setTimeout(r,1000))}
 msg('The board hasn\'t come back after a minute. Check the OLED for its address.',1)}
function esc(t){return t.replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
status();setInterval(status,2500);
async function upload(url,file,label,extra){if(!file){msg('Pick a file first',1);return}
 msg('Uploading '+label+'... (the Mac pauses meanwhile)');const fd=new FormData();fd.append('f',file,file.name);
 try{const t=await post(url,fd);msg(t+(extra||'')+' Restarting...');waitForBoard()}catch(e){msg(e.message,1)}}
$('romUp').onclick=()=>upload('/api/rom',$('romFile').files[0],'ROM');
// ---- disk library. Archives are unpacked in the browser (macfiles.js); the board only stores raw disks.
let found=[],progs=[];
let pendingDel=-1,pendingTimer=0;
function renderLib(s){const L=s.lib||[];
 $('lib').innerHTML=L.length?L.map(d=>'<div class="disk'+(d.i==s.active?' cur':'')+'"><div class="nm"><b>'+esc(d.n.replace(/\.img$/i,''))+'</b><br><span class="mute">'+kb(d.s)+(d.d?' &middot; '+esc(d.d):'')+'</span></div>'+
  (d.i==s.active?'':'<button data-use="'+d.i+'" class="primary">Use</button>')+'<a class="btn" href="/api/disk.img?i='+d.i+'">Download</a><button data-del="'+d.i+'"'+(pendingDel==d.i?' class="sure">Sure?':'>Remove')+'</button></div>').join(''):
  '<p class="mute">No disks yet. Add one below.</p>';
 if(L.length)$('lib').innerHTML+='<div class="disk'+(s.active<0?' cur':'')+'"><div class="nm"><b>No disk</b><br><span class="mute">Starts the Mac with an empty drive: it waits at a floppy icon, like a real Mac with no disk in</span></div>'+(s.active<0?'':'<button data-use="-1">Use</button>')+'</div>';
 $('libFree').textContent=kb(s.libFree||0)+' free for more disks'}
$('lib').onclick=async e=>{const u=e.target.dataset.use,d=e.target.dataset.del;
 if(u!==undefined){try{await post('/api/use?i='+u);msg(u=='-1'?'The Mac restarted with no disk.':'The Mac restarted from that disk.');setTimeout(()=>msg(''),4000)}catch(err){msg(err.message,1)}status()}
 if(d!==undefined){clearTimeout(pendingTimer);
  if(pendingDel!=+d){pendingDel=+d;renderLib(lastStatus);pendingTimer=setTimeout(()=>{pendingDel=-1;renderLib(lastStatus)},4000);return}
  pendingDel=-1;const wasActive=+d==lastStatus.active;
  e.target.disabled=true;e.target.textContent='Removing...';
  msg('Removing... the board moves your other disks down to free the space, so this can take a few seconds.');
  try{await post('/api/libdel?i='+d);
   if(wasActive){msg('Removed. The Mac restarted with no disk.');status();return}
   msg('Removed. The space is free again.')}catch(err){msg(err.message,1)}status()}};
function diskLabel(d){const i=d.info;return esc(d.name.split('/').pop())+' &middot; '+Math.round(i.size/1024)+' KB'+(i.fs?' '+i.fs:'')+(i.vol?' &ldquo;'+esc(i.vol)+'&rdquo;':'')+(i.bootable?'':' &middot; not a startup disk')}
function diskWarning(d){const i=d.info;
 if(i.fs=='HFS'&&lastStatus.ram&&lastStatus.ram<512*1024)return ' Note: HFS disks are usually System 4 or later, which needs 512 KB; this Mac has '+kb(lastStatus.ram)+', so it may stop with an out-of-memory error.';
 if(!i.bootable)return ' Note: it isn\'t a startup disk, so the Mac will show the question-mark disk.';return ''}
async function sendDisks(list,useFirst){   // uploads one after another; the board restarts after the last
 let note='';
 for(let k=0;k<list.length;k++){const d=list[k],last=k==list.length-1;
  const name=d.name.split('/').pop().replace(/\.(image|dsk|img|dc42|hfv|disk)$/i,'')+'.img';
  msg('Adding '+name+' ('+(k+1)+' of '+list.length+')...');
  const fd=new FormData();fd.append('f',new File([d.raw],name),name);
  try{await post('/api/disk?activate='+(useFirst&&k==0?1:0)+'&restart='+(last?1:0),fd)}catch(e){msg(e.message,1);return}
  if(useFirst&&k==0)note=diskWarning(d)}
 msg('Added '+list.length+' disk'+(list.length>1?'s':'')+'.'+note+' Restarting...');waitForBoard()}
$('diskUp').onclick=async()=>{const f=$('diskFile').files[0];if(!f){msg('Pick a file first',1);return}
 $('diskPick').hidden=true;$('progPick').hidden=true;msg('Opening '+f.name+'...');
 try{const r=await MacFiles.open(f.name,new Uint8Array(await f.arrayBuffer()));found=r.disks;progs=r.files||[];
  if(!found.length&&!progs.length){let t='No Mac disk or program found in '+f.name+'.';
   if(r.other.length==1&&r.other[0]==f.name)t=f.name+' isn\'t a Mac disk image or an archive )HTML" BOARD_NAME R"HTML( can open.';
   if(r.notes.length)t+=' '+r.notes.join(' ');msg(t,1);return}
  if(found.length==1&&!progs.length){await sendDisks(found,true);return}
  if(found.length){$('diskList').innerHTML=found.map((d,i)=>'<label><input type="checkbox" name="dpick" value="'+i+'" checked> <span>'+diskLabel(d)+'</span></label>').join('');$('diskPick').hidden=false}
  if(progs.length){const cur=(lastStatus.lib||[]).find(d=>d.i==lastStatus.active);
   $('progList').textContent=progs.map(p=>p.name).join(', ');
   $('progNote').textContent=cur?'They\'ll be copied onto "'+cur.n.replace(/\.img$/i,'')+'", the disk in use. It must be a 400K MFS disk with room to spare.':'Pick a disk to use first, then add these to it.';
   $('progAdd').disabled=!cur;$('progPick').hidden=false}
  msg('Found '+[found.length?found.length+' disk'+(found.length>1?'s':''):'',progs.length?progs.length+' program'+(progs.length>1?'s':''):''].filter(Boolean).join(' and ')+'.'+(r.notes.length?' '+r.notes.join(' '):''))}
 catch(e){msg('Couldn\'t open '+f.name+': '+e.message,1)}};
$('diskPickUp').onclick=()=>{const pick=[...document.querySelectorAll('input[name=dpick]:checked')].map(c=>found[+c.value]);
 if(!pick.length){msg('Tick at least one disk',1);return}$('diskPick').hidden=true;sendDisks(pick,$('useFirst').checked)};
$('progAdd').onclick=async()=>{const i=lastStatus.active;if(i<0)return;
 try{msg('Copying the disk from the board...');
  let disk=new Uint8Array(await (await fetch('/api/disk.img?i='+i)).arrayBuffer());const added=[];
  for(const p of progs){const r=MacFiles.addToMFS(disk,p);disk=r.disk;added.push(r.name)}
  const cur=(lastStatus.lib||[]).find(d=>d.i==i),name=cur?cur.n:'disk.img';
  msg('Writing '+added.join(', ')+' to the disk...');
  const fd=new FormData();fd.append('f',new File([disk],name),name);
  await post('/api/disk?replace='+i,fd);$('progPick').hidden=true;
  msg('Added '+added.join(', ')+'. The Mac is restarting; open the disk to find '+(added.length>1?'them':'it')+'.');waitForBoard()}
 catch(e){msg(e.message,1)}};
$('diskRO').onclick=async()=>{await post('/api/diskro?on='+(lastStatus.diskRO?0:1));msg('Write-protect changed; the Mac restarted.');status()};
$('ramSet').onclick=async()=>{await post('/api/ram?kb='+$('ram').value);msg('Restarting the board...');waitForBoard()};
$('turbo').onclick=async()=>{await post('/api/turbo?on='+(lastStatus.turbo?0:1));status()};
$('scan').onclick=async()=>{$('scan').textContent='Scanning...';const l=await (await fetch('/api/scan')).json();
 $('ssidSel').innerHTML='<option value="">(choose)</option>'+l.map(n=>'<option>'+esc(n.s)+'</option>').join('');$('scan').textContent='Scan'};
$('ssidSel').onchange=()=>{$('ssid').value=$('ssidSel').value};
$('wifiSave').onclick=async()=>{const f=new URLSearchParams({ssid:$('ssid').value,pass:$('pass').value});await post('/api/wifi',f);msg('Saved. The board is restarting onto your network - find its new address on the OLED.')};
</script></body></html>)HTML";
