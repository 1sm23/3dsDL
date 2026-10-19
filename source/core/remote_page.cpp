#include "remote.hpp"

// The page a phone or computer sees on the Receive screen's address: a box
// for links, which works without JavaScript, and a file manager for the SD
// card that talks to the JSON API in net/remote_server. Colours follow
// docs/DESIGN.md.

namespace remote {

namespace {

std::string escapeHtml(const std::string &value)
{
    std::string out;
    for (const char c : value) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

const char *const PageStart = R"PAGE(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="icon" href="data:,"><title>3dsDL</title><style>
:root{color-scheme:light;--ink:#615a4b;--soft:#8c8466;--blue:#1c93c2;--panel:#dcd49a;--recess:#d9d3a0;--dots:#b0a66e;--good:#3fa65a;--bad:#e0301a}
*{box-sizing:border-box}
html{background:#f0e694}
body{margin:0;font:16px/1.45 system-ui,-apple-system,sans-serif;background:linear-gradient(#f2ebb2,#f0e694);color:var(--ink);min-height:100vh}
main{max-width:46rem;margin:0 auto;padding:20px 16px 120px}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px}
h1{margin:0;font-size:1.5rem;color:var(--blue)}
.tabs{display:flex;background:var(--recess);border-radius:10px;padding:3px;box-shadow:inset 0 1px 2px #0002}
.tabs a{padding:6px 16px;border-radius:8px;color:var(--ink);text-decoration:none;font-weight:600}
.tabs a.on{background:linear-gradient(#fefcf5,#e0dccb);box-shadow:0 1px 2px #0003;color:var(--blue)}
p{margin:0 0 14px}
.panel{background:var(--panel);border-radius:12px;padding:14px}
textarea,input[type=text]{width:100%;border:1px solid #b3aa83;border-radius:8px;padding:10px;font:15px/1.4 ui-monospace,monospace;background:#fffdf4;color:#141414}
textarea{min-height:9rem;resize:vertical}
button,.btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;padding:9px 14px;border:1px solid #8f8659;border-radius:10px;font:600 15px system-ui,sans-serif;color:#756e60;background:linear-gradient(#fefcf5,#e0dccb);cursor:pointer;white-space:nowrap}
button:active,.btn:active{background:#e0dccb}
button:focus-visible,.btn:focus-within,a:focus-visible{outline:3px solid #3cdc78;outline-offset:1px}
button:disabled{opacity:.5;cursor:default}
.wide{width:100%;margin-top:12px;padding:12px;font-size:17px}
.danger{color:#fff;background:linear-gradient(#ef5a3c,#c8260f);border-color:#9a1d0b}
.note{border-radius:10px;padding:10px 12px;margin-bottom:14px;color:#fff}
.good{background:var(--good)}.bad{background:var(--bad)}
small{display:block;margin-top:14px;color:var(--soft)}
[hidden]{display:none!important}
.bar{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:8px;margin-bottom:10px}
.crumbs{display:flex;flex-wrap:wrap;align-items:center;gap:2px;font-weight:600;min-width:0}
.crumbs a{color:var(--blue);text-decoration:none;padding:2px 4px;border-radius:6px;overflow-wrap:anywhere}
.crumbs a:hover{background:#fff6}
.crumbs span{color:var(--soft)}
.free{color:var(--soft);font-size:14px}
.tools{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:10px}
.tools select{padding:8px;border-radius:10px;border:1px solid #8f8659;background:#fffdf4;color:var(--ink);font:inherit;margin-left:auto}
.selbar{display:flex;flex-wrap:wrap;align-items:center;gap:8px;background:#fff0b4;border:1px solid #e0c060;border-radius:10px;padding:8px 10px;margin-bottom:10px}
.selbar b{margin-right:auto}
.list{list-style:none;margin:0;padding:4px 10px;background:var(--recess);border-radius:12px;box-shadow:inset 0 1px 3px #0002}
.list li{display:flex;align-items:center;gap:10px;padding:8px 2px;background-image:radial-gradient(circle,var(--dots) 1.2px,transparent 1.5px);background-size:8px 3px;background-repeat:repeat-x;background-position:0 100%}
.list li:last-child{background:none}
.list li.on{background-color:#fff0b4;border-radius:8px}
.list input{width:18px;height:18px;accent-color:var(--blue);flex:none}
.ic{flex:none;width:40px;height:40px;border-radius:9px;display:grid;place-items:center;font:800 10px system-ui,sans-serif;color:#fff;letter-spacing:.02em;box-shadow:inset 0 -2px 0 #0002}
.ic svg{width:26px;height:26px}
.k-dir{background:none;box-shadow:none}.k-cia{background:#1c93c2}.k-3dsx{background:#3fa65a}.k-zip{background:#b07a2a}.k-img{background:#c0569a}.k-txt{background:#8c8466}.k-rom{background:#e69614}.k-file{background:#aca27a}
.what{flex:1;min-width:0}
.what a{display:block;color:#141414;text-decoration:none;font-weight:600;overflow-wrap:anywhere}
.what a:hover{color:var(--blue)}
.what span{font-size:13px;color:var(--soft)}
.more{padding:6px 10px;font-size:18px;line-height:1}
.empty{text-align:center;color:var(--soft);padding:28px 8px}
.menu{position:absolute;z-index:20;min-width:190px;background:#fffdf0;border:1px solid #ae8d67;border-radius:10px;box-shadow:0 6px 20px #0003;padding:4px}
.menu button{display:block;width:100%;text-align:left;border:0;background:none;border-radius:6px;padding:9px 12px;color:#141414;font-weight:500}
.menu button:hover{background:#fff0b4}
.menu .red{color:var(--bad)}
.ups{margin-top:16px}
.ups h2{display:flex;align-items:center;justify-content:space-between;font-size:1rem;margin:0 0 8px}
.ups h2 button{padding:4px 10px;font-size:13px}
.ups ul{list-style:none;margin:0;padding:0}
.ups li{display:grid;grid-template-columns:1fr auto;gap:2px 10px;align-items:center;padding:8px 0;border-bottom:1px dotted var(--dots)}
.ups li:last-child{border:0}
.ups .n{font-weight:600;overflow-wrap:anywhere;min-width:0}
.ups .s{font-size:13px;color:var(--soft);grid-column:1}
.ups .s.err{color:var(--bad)}.ups .s.ok{color:var(--good)}
.ups li button{grid-row:1/span 3;grid-column:2;padding:5px 10px;font-size:13px}
.track{grid-column:1;height:10px;border-radius:5px;background:#cfc795;overflow:hidden;box-shadow:inset 0 1px 2px #0002}
.fill{height:100%;width:0;background:linear-gradient(#ffcc22,#ff9900);transition:width .2s}
label.check{display:flex;gap:8px;align-items:center;margin-top:10px;font-size:14px}
.drop{position:fixed;inset:0;z-index:30;display:grid;place-items:center;background:rgba(28,147,194,.18);border:4px dashed var(--blue);font:700 1.3rem system-ui,sans-serif;color:var(--blue);pointer-events:none}
.gate{text-align:center;padding:24px 16px}
.gate h2{margin:0 0 6px;font-size:1.15rem;color:#141414}
.spin{width:28px;height:28px;margin:0 auto 12px;border-radius:50%;border:4px solid #cfc795;border-top-color:var(--blue);animation:s 1s linear infinite}
@keyframes s{to{transform:rotate(1turn)}}
dialog{border:0;border-radius:14px;padding:0;width:min(30rem,calc(100vw - 32px));background:linear-gradient(#fffdf0,#e4e3dd);color:#141414;box-shadow:0 10px 40px #0005}
dialog.big{width:min(52rem,calc(100vw - 32px))}
dialog::backdrop{background:rgba(20,18,8,.48)}
dialog form{padding:18px}
dialog h2{margin:0 0 8px;font-size:1.1rem}
dialog p{white-space:pre-line;overflow-wrap:anywhere}
dialog textarea{min-height:55vh;font-size:13px}
.row{display:flex;justify-content:flex-end;flex-wrap:wrap;gap:8px;margin-top:14px}
.toasts{position:fixed;left:16px;right:16px;bottom:20px;z-index:40;display:flex;flex-direction:column;align-items:center;gap:8px;pointer-events:none}
.toast{max-width:30rem;background:#141414e6;color:#fff;padding:10px 16px;border-radius:10px;font-size:15px}
</style></head><body><main>
<header><h1>3dsDL</h1><nav class="tabs"><a href="#links" id="t-links" class="on">Links</a><a href="#files:%2F" id="t-files">Files</a></nav></header>
<section id="links">
<p>Paste one or more direct download links. They go to the 3DS, which downloads them one after another.</p>
)PAGE";

const char *const PageEnd = R"PAGE(<form method="post" action="/send" class="panel">
<textarea name="links" placeholder="https://..." autofocus required></textarea>
<button class="wide" type="submit">Send to 3DS</button></form>
<small>Only links are sent. Keep 3dsDL on the Receive screen while you send.</small>
</section>
<section id="files" hidden>
<div id="gate" class="panel gate" hidden></div>
<div id="browser" hidden>
<div class="bar"><nav class="crumbs" id="crumbs"></nav><span class="free" id="free"></span></div>
<div class="tools">
<label class="btn">Upload Files<input type="file" id="pick" multiple hidden></label>
<label class="btn">Upload Folder<input type="file" id="pickdir" webkitdirectory multiple hidden></label>
<button id="mkdir">New Folder</button><button id="reload">Refresh</button>
<select id="sort" aria-label="Sort"><option value="name">By name</option><option value="size">By size</option><option value="time">By date</option><option value="type">By type</option></select>
</div>
<div class="selbar" id="selbar" hidden><b id="selcount"></b><button id="selinstall">Install on 3DS</button><button id="selmove">Move</button><button id="seldel" class="danger">Delete</button><button id="selnone">Clear</button></div>
<ul class="list" id="list"></ul>
<div class="ups panel" id="ups" hidden><h2>Uploads<button id="upclear">Clear Finished</button></h2><ul id="uplist"></ul>
<label class="check"><input type="checkbox" id="offer" checked>Offer to install CIAs on the 3DS once they arrive</label></div>
<small>Drop files or folders anywhere on this page to upload them here. One transfer runs at a time.</small>
</div>
</section>
<div class="drop" id="drop" hidden>Drop to upload</div>
<div class="toasts" id="toasts"></div>
<div class="menu" id="menu" hidden></div>
<dialog id="dlg"><form method="dialog"><h2 id="dt"></h2><p id="dp"></p>
<input type="text" id="di" autocomplete="off" spellcheck="false"><textarea id="da" spellcheck="false"></textarea>
<div class="row" id="db"></div></form></dialog>
</main><script>
'use strict';
const $=s=>document.querySelector(s),enc=encodeURIComponent,H={'X-Requested-With':'3dsDL'};
let cwd='/',entries=[],free=-1,sel=new Set(),polling=0;
const join=(d,n)=>d==='/'?'/'+n:d+'/'+n;
const parent=p=>p.slice(0,p.lastIndexOf('/'))||'/';
const base=p=>p.slice(p.lastIndexOf('/')+1);
const ext=n=>{const i=n.lastIndexOf('.');return i>0?n.slice(i+1).toLowerCase():''};
function size(b){if(b<0)return'';const u=['B','KiB','MiB','GiB'];let i=0;while(b>=1024&&i<3){b/=1024;i++}return(i?b.toFixed(b<10?2:1):b)+' '+u[i]}
function when(t){if(!t)return'';const d=new Date(t*1e3),z=n=>String(n).padStart(2,'0');return d.getUTCFullYear()+'-'+z(d.getUTCMonth()+1)+'-'+z(d.getUTCDate())+' '+z(d.getUTCHours())+':'+z(d.getUTCMinutes())}
const KINDS={cia:'cia','3dsx':'3dsx',zip:'zip','7z':'zip',rar:'zip',png:'img',jpg:'img',jpeg:'img',gif:'img',bmp:'img',webp:'img',txt:'txt',ini:'txt',cfg:'txt',json:'txt',xml:'txt',log:'txt',md:'txt',yml:'txt',yaml:'txt',lua:'txt',nds:'rom','3ds':'rom',cci:'rom',firm:'rom',tik:'rom',smdh:'rom',bin:'file'};
const TEXT=new Set(['txt','ini','cfg','json','xml','log','md','yml','yaml','lua','csv','conf','sh','py','js','html','htm','css']);
const VIEW=new Set(['png','jpg','jpeg','gif','bmp','webp','mp3','ogg','wav','mp4','pdf',...TEXT]);
const FOLDER='<svg viewBox="0 0 26 26"><path d="M2 6.5A2.5 2.5 0 0 1 4.5 4h5.6l2.4 2.6h9A2.5 2.5 0 0 1 24 9.1V20a2.5 2.5 0 0 1-2.5 2.5h-17A2.5 2.5 0 0 1 2 20z" fill="#e6b422"/><path d="M2 10h22v10a2.5 2.5 0 0 1-2.5 2.5h-17A2.5 2.5 0 0 1 2 20z" fill="#f7d55c"/></svg>';
function toast(t){const e=document.createElement('div');e.className='toast';e.textContent=t;$('#toasts').append(e);setTimeout(()=>e.remove(),3500)}
function ask(title,text,o={}){return new Promise(res=>{const d=$('#dlg');$('#dt').textContent=title;$('#dp').textContent=text||'';$('#dp').hidden=!text;
const i=$('#di'),a=$('#da');i.hidden=o.input===undefined;a.hidden=o.area===undefined;i.value=o.input||'';a.value=o.area||'';d.classList.toggle('big',o.area!==undefined);
const b=$('#db');b.textContent='';(o.buttons||['Cancel','OK']).forEach((l,n,all)=>{const x=document.createElement('button');x.textContent=l;x.value=String(n);
if(n===all.length-1)x.className=o.danger?'danger':'';b.append(x)});
d.onclose=()=>{const v=d.returnValue===''?-1:+d.returnValue;res(o.input!==undefined?(v>0?i.value:null):o.area!==undefined?(v>0?a.value:null):v)};
d.returnValue='';d.showModal();if(!i.hidden){i.focus();const dot=i.value.lastIndexOf('.');i.setSelectionRange(o.select?0:i.value.lastIndexOf('/')+1,o.select&&dot>0?dot:i.value.length)}})}
async function api(method,url,body){const o={method,headers:{...H}};
if(body){o.headers['Content-Type']='application/x-www-form-urlencoded';o.body=new URLSearchParams(body).toString()}
let r;try{r=await fetch(url,o)}catch(e){throw new Error('Lost the connection to the 3DS. Is 3dsDL still on the Receive screen?')}
let j={};try{j=await r.json()}catch(e){}
if(r.status===403&&j.access){gate(j.access);throw new Error('')}
if(!r.ok)throw new Error(j.error||'The 3DS answered '+r.status+'.');return j}
function fail(e){if(e.message)ask('Something went wrong',e.message,{buttons:['OK']})}
function gate(state){$('#browser').hidden=true;const g=$('#gate');g.hidden=false;
g.innerHTML=state==='pending'?'<div class="spin"></div><h2>Allow this device on the 3DS</h2><p>The console is asking whether this device may see and change the files on its SD card. Choose Allow there.</p>'
:'<h2>Not allowed</h2><p>The 3DS said no. To ask again, leave Receive on the console and open it again.</p>';
clearTimeout(polling);if(state==='pending')polling=setTimeout(()=>load(cwd),1500)}
async function load(path){cwd=path;sel.clear();
try{const j=await api('GET','/api/list?path='+enc(path));entries=j.entries;free=j.free;$('#gate').hidden=true;$('#browser').hidden=false;render()}
catch(e){if(e.message&&path!=='/'){toast(e.message);location.hash='#files:'+enc(parent(path))}else fail(e)}}
function sorted(){const by=$('#sort').value,c=(a,b)=>a.name.localeCompare(b.name,undefined,{numeric:true,sensitivity:'base'});
return entries.slice().sort((a,b)=>a.dir!==b.dir?(a.dir?-1:1):by==='size'&&!a.dir?b.size-a.size||c(a,b):by==='time'?b.time-a.time||c(a,b):by==='type'&&!a.dir?ext(a.name).localeCompare(ext(b.name))||c(a,b):c(a,b))}
function render(){const cr=$('#crumbs');cr.textContent='';let p='';
const crumb=(label,to)=>{const a=document.createElement('a');a.href='#files:'+enc(to);a.textContent=label;cr.append(a)};
crumb('SD','/');cwd.split('/').filter(Boolean).forEach(part=>{p+='/'+part;const s=document.createElement('span');s.textContent='/';cr.append(s);crumb(part,p)});
$('#free').textContent=free>=0?size(free)+' free':'';
const ul=$('#list');ul.textContent='';const list=sorted();
if(!list.length){const li=document.createElement('li');li.className='empty';li.textContent='This folder is empty. Drop files here to upload them.';ul.append(li)}
for(const e of list){const path=join(cwd,e.name),li=document.createElement('li'),k=e.dir?'dir':KINDS[ext(e.name)]||'file';
const box=document.createElement('input');box.type='checkbox';box.checked=sel.has(path);box.setAttribute('aria-label','Select '+e.name);
box.onchange=()=>{box.checked?sel.add(path):sel.delete(path);li.classList.toggle('on',box.checked);selbar()};
const ic=document.createElement('div');ic.className='ic k-'+k;if(e.dir)ic.innerHTML=FOLDER;else ic.textContent=(ext(e.name)||'file').slice(0,4).toUpperCase();
const w=document.createElement('div');w.className='what';const a=document.createElement('a');a.textContent=e.name;
a.href=e.dir?'#files:'+enc(path):'/api/file?download=1&path='+enc(path);
const m=document.createElement('span');m.textContent=[e.dir?'Folder':size(e.size),when(e.time)].filter(Boolean).join(' · ');w.append(a,m);
const b=document.createElement('button');b.className='more';b.textContent='⋯';b.setAttribute('aria-label','Options for '+e.name);b.onclick=ev=>{ev.stopPropagation();menu(b,e,path)};
li.classList.toggle('on',box.checked);li.append(box,ic,w,b);ul.append(li)}selbar()}
function selbar(){$('#selbar').hidden=!sel.size;$('#selcount').textContent=sel.size+' selected';$('#selinstall').hidden=![...sel].some(p=>ext(p)==='cia')}
function menu(anchor,e,path){const m=$('#menu');m.textContent='';const x=ext(e.name);
const add=(label,fn,cls)=>{const b=document.createElement('button');b.textContent=label;if(cls)b.className=cls;b.onclick=()=>{m.hidden=true;fn()};m.append(b)};
if(e.dir)add('Open',()=>location.hash='#files:'+enc(path));
else{add('Download',()=>location.href='/api/file?download=1&path='+enc(path));
if(VIEW.has(x))add('Open in Browser',()=>window.open('/api/file?path='+enc(path),'_blank'));
if(TEXT.has(x)&&e.size<=1048576)add('Edit',()=>edit(path));
if(x==='cia')add('Install on 3DS',()=>install([path]))}
add('Rename',()=>rename(path));add('Move To...',()=>move([path]));add('Delete',()=>remove([path]),'red');
m.hidden=false;const r=anchor.getBoundingClientRect();m.style.top=(scrollY+r.bottom+4)+'px';m.style.left=Math.max(8,Math.min(scrollX+r.right-m.offsetWidth,innerWidth-m.offsetWidth-8))+'px'}
document.addEventListener('click',()=>$('#menu').hidden=true);
async function rename(path){const n=await ask('Rename',null,{input:base(path),select:true,buttons:['Cancel','Rename']});if(!n||n===base(path))return;
try{await api('POST','/api/move',{from:path,to:join(parent(path),n)});load(cwd)}catch(e){fail(e)}}
async function move(paths){const to=await ask('Move '+(paths.length>1?paths.length+' items':'"'+base(paths[0])+'"'),'Type the folder to move to, starting with /.',{input:cwd,buttons:['Cancel','Move']});
if(!to)return;const dest=('/'+to.trim()).replace(/\/+/g,'/').replace(/(.)\/$/,'$1');
try{for(const p of paths)await api('POST','/api/move',{from:p,to:join(dest,base(p))});load(cwd)}catch(e){fail(e);load(cwd)}}
async function remove(paths){const many=paths.length>1;
if(await ask('Delete '+(many?paths.length+' items':'"'+base(paths[0])+'"')+'?','Deleted files and folders cannot be brought back.',{buttons:['Cancel','Delete'],danger:true})!==1)return;
try{for(const p of paths)await api('POST','/api/delete',{path:p});load(cwd)}catch(e){fail(e);load(cwd)}}
async function install(paths){const cias=paths.filter(p=>ext(p)==='cia');if(!cias.length)return;
try{await api('POST','/api/install',{paths:cias.join('\n')});toast('Sent to the 3DS. Choose Install on the console.')}catch(e){fail(e)}}
async function edit(path){let t;try{const r=await fetch('/api/file?path='+enc(path));if(!r.ok)throw new Error('Could not open '+base(path)+'.');t=await r.text()}catch(e){return fail(e)}
const v=await ask(base(path),null,{area:t,buttons:['Cancel','Save']});if(v===null||v===t)return;
try{const r=await fetch('/api/file?replace=1&path='+enc(path),{method:'PUT',headers:H,body:v});if(!r.ok)throw new Error((await r.json().catch(()=>({}))).error||'Not saved.');toast('Saved '+base(path)+'.');load(cwd)}catch(e){fail(e)}}
$('#mkdir').onclick=async()=>{const n=await ask('New Folder',null,{input:'',buttons:['Cancel','Create']});if(!n)return;
try{await api('POST','/api/folder',{path:join(cwd,n.trim())});load(cwd)}catch(e){fail(e)}};
$('#reload').onclick=()=>load(cwd);$('#sort').onchange=render;
$('#selnone').onclick=()=>{sel.clear();render()};$('#seldel').onclick=()=>remove([...sel]);$('#selmove').onclick=()=>move([...sel]);$('#selinstall').onclick=()=>install([...sel]);
// Uploads go one at a time, in the order they were chosen.
const queue=[];let busy=false,arrived=[];
async function plan(files){if(!files.length)return;
const dirs=new Map();for(const f of files){const d=parent(f.path);if(!dirs.has(d))dirs.set(d,null)}
for(const d of dirs.keys()){try{const r=await fetch('/api/list?path='+enc(d));const j=r.ok?await r.json():{entries:[]};dirs.set(d,new Map(j.entries.map(e=>[e.name,e.dir])))}catch(e){dirs.set(d,new Map())}}
const clash=files.filter(f=>dirs.get(parent(f.path)).has(base(f.path)));
if(clash.some(f=>dirs.get(parent(f.path)).get(base(f.path))))return ask('Cannot upload','A folder already has the name '+clash.find(f=>dirs.get(parent(f.path)).get(base(f.path))).path+'.',{buttons:['OK']});
let skip=new Set();if(clash.length){const names=clash.slice(0,5).map(f=>base(f.path)).join('\n')+(clash.length>5?'\n...and '+(clash.length-5)+' more':'');
const c=await ask(clash.length===1?'Replace this file?':'Replace '+clash.length+' files?',names,{buttons:['Cancel','Skip','Replace']});
if(c<1)return;if(c===1)skip=new Set(clash);else clash.forEach(f=>f.replace=true)}
files=files.filter(f=>!skip.has(f));const need=files.reduce((n,f)=>n+f.file.size,0);
if(free>=0&&need>free)return ask('Not enough space','These need '+size(need)+' and the SD card has '+size(free)+' free.',{buttons:['OK']});
const big=files.find(f=>f.file.size>4294967295);if(big)return ask('Too big for the SD card',base(big.path)+' is 4 GiB or more, which the SD card\'s FAT32 cannot hold.',{buttons:['OK']});
$('#ups').hidden=false;for(const f of files){const li=document.createElement('li');li.innerHTML='<span class="n"></span><div class="track"><div class="fill"></div></div><span class="s">Waiting</span><button>Cancel</button>';
li.querySelector('.n').textContent=f.path;f.li=li;f.state='wait';li.querySelector('button').onclick=()=>{if(f.xhr)f.xhr.abort();else{f.state='off';status(f,'Cancelled','err');li.querySelector('button').remove()}};
$('#uplist').prepend(li);queue.push(f)}if(!busy)next()}
function status(f,t,c){const s=f.li.querySelector('.s');s.textContent=t;s.className='s'+(c?' '+c:'')}
const made=new Set();
$('#upclear').onclick=()=>{for(let i=queue.length-1;i>=0;i--)if(queue[i].state==='done'||queue[i].state==='off'){queue[i].li.remove();queue.splice(i,1)}$('#ups').hidden=!queue.length};
async function next(){const f=queue.find(x=>x.state==='wait');
if(!f){busy=false;load(cwd);if($('#offer').checked&&arrived.length)install(arrived);arrived=[];return}
busy=true;f.state='up';const d=parent(f.path);
if(d!==cwd&&!made.has(d)){try{await api('POST','/api/folder',{path:d});made.add(d)}catch(e){status(f,e.message||'Not allowed','err');f.state='off';return next()}}
const x=new XMLHttpRequest(),t0=performance.now(),fill=f.li.querySelector('.fill');f.xhr=x;
x.open('PUT','/api/file?path='+enc(f.path)+(f.replace?'&replace=1':''));x.setRequestHeader('X-Requested-With','3dsDL');
x.upload.onprogress=e=>{if(!e.lengthComputable)return;const s=(performance.now()-t0)/1e3,r=e.loaded/Math.max(s,.001);fill.style.width=(e.total?100*e.loaded/e.total:100)+'%';
status(f,size(e.loaded)+' of '+size(e.total)+(s>1?' · '+size(r)+'/s':''))};
const end=(t,c)=>{f.state='done';status(f,t,c);const b=f.li.querySelector('button');if(b)b.remove();f.xhr=null;next()};
x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}
if(x.status===200){fill.style.width='100%';const s=(performance.now()-t0)/1e3;end('Done, '+size(f.file.size)+(s>1?' at '+size(f.file.size/s)+'/s':''),'ok');if(ext(f.path)==='cia')arrived.push(f.path)}
else{if(x.status===403&&j.access)gate(j.access);end(j.error||'Failed ('+x.status+')','err')}};
x.onerror=()=>end('The connection to the 3DS was lost.','err');x.onabort=()=>end('Cancelled','err');x.send(f.file)}
const pick=list=>plan([...list].map(file=>({file,path:join(cwd,file.webkitRelativePath||file.name)})));
$('#pick').onchange=e=>{pick(e.target.files);e.target.value=''};$('#pickdir').onchange=e=>{pick(e.target.files);e.target.value=''};
function walk(entry,dir,out){return new Promise(res=>{if(entry.isFile)entry.file(file=>{out.push({file,path:join(dir,file.name)});res()},()=>res());
else if(entry.isDirectory){const r=entry.createReader(),all=[];const more=()=>r.readEntries(async got=>{if(got.length){all.push(...got);more()}else{for(const g of all)await walk(g,join(dir,entry.name),out);res()}},()=>res());more()}else res()})}
let depth=0;const filesTab=()=>!$('#files').hidden&&!$('#browser').hidden;
addEventListener('dragenter',e=>{if(filesTab()&&e.dataTransfer.types.includes('Files')){depth++;$('#drop').hidden=false}});
addEventListener('dragleave',()=>{if(--depth<=0){depth=0;$('#drop').hidden=true}});
addEventListener('dragover',e=>{if(filesTab())e.preventDefault()});
addEventListener('drop',async e=>{depth=0;$('#drop').hidden=true;if(!filesTab())return;e.preventDefault();const out=[],items=[...e.dataTransfer.items].map(i=>i.webkitGetAsEntry&&i.webkitGetAsEntry());
if(items.every(Boolean)){for(const it of items)await walk(it,cwd,out);plan(out)}else pick(e.dataTransfer.files)});
function route(){const h=location.hash,files=h.startsWith('#files');$('#links').hidden=files;$('#files').hidden=!files;
$('#t-links').classList.toggle('on',!files);$('#t-files').classList.toggle('on',files);
if(files){let p='/';try{p=decodeURIComponent(h.slice(7))||'/'}catch(e){}$('#t-files').href='#files:'+enc(p);load(p)}}
addEventListener('hashchange',route);route();
</script></body></html>)PAGE";

} // namespace

std::string page(const std::string &message, bool good)
{
    std::string html = PageStart;
    if (!message.empty()) {
        html += std::string("<div class=\"note ") + (good ? "good" : "bad") +
                "\">" + escapeHtml(message) + "</div>\n";
    }
    return html + PageEnd;
}

} // namespace remote
