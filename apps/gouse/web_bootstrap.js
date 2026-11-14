/* Small DOM/Canvas host layer for QuickJS. Unsupported graphics contexts return null. */
(() => {
'use strict';
const call = globalThis.__bridge; delete globalThis.__bridge;
const cache = new Map();
class Event {
  constructor(type, init = {}) { this.type=type; Object.assign(this,init); this.defaultPrevented=false; this.cancelBubble=false; }
  preventDefault() { this.defaultPrevented=true; }
  stopPropagation() { this.cancelBubble=true; }
}
class EventTarget {
  constructor() { this._listeners=new Map(); }
  addEventListener(t,f,options) { if(typeof f!=='function') return; const a=this._listeners.get(t)||[]; if(!a.some(x=>x.f===f)) a.push({f,once:!!(options&&options.once)}); this._listeners.set(t,a); }
  removeEventListener(t,f) { this._listeners.set(t,(this._listeners.get(t)||[]).filter(x=>x.f!==f)); }
  dispatchEvent(e) {
    if(!e.target) e.target=this; e.currentTarget=this;
    for(const l of [...(this._listeners.get(e.type)||[])]) { if(l.once) this.removeEventListener(e.type,l.f); l.f.call(this,e); }
    const f=this['on'+e.type]; if(typeof f==='function' && f.call(this,e)===false) e.preventDefault();
    return !e.defaultPrevented;
  }
}
function wrap(id) { if(!id) return null; if(!cache.has(id)) cache.set(id,new Element(id)); return cache.get(id); }
const cssName = name => name.replace(/[A-Z]/g,c=>'-'+c.toLowerCase());
class Element extends EventTarget {
  constructor(id) {
    super(); this._id=id;
    const read=()=>Object.fromEntries((this.getAttribute('style')||'').split(';').filter(x=>x.includes(':')).map(x=>{const i=x.indexOf(':');return [x.slice(0,i).trim(),x.slice(i+1).trim()];}));
    const set=(k,v)=>{const a=read();a[cssName(k)]=String(v);this.setAttribute('style',Object.entries(a).map(([k,v])=>k+':'+v).join(';'));};
    this.style=new Proxy({}, {get:(_,k)=> k==='setProperty' ? set : k==='cssText' ? this.getAttribute('style')||'' : read()[cssName(String(k))]||'',set:(_,k,v)=>{if(k==='cssText') this.setAttribute('style',v);else set(String(k),v);return true;}});
    this.classList={add:(...a)=>{this.className=[...new Set(this.className.split(/\s+/).concat(a))].join(' ').trim();},remove:(...a)=>{this.className=this.className.split(/\s+/).filter(x=>!a.includes(x)).join(' ');},contains:a=>this.className.split(/\s+/).includes(a),toggle:a=>{const had=this.classList.contains(a);this.classList[had?'remove':'add'](a);return !had;}};
  }
  getAttribute(k) { return call('get',this._id,String(k)); }
  setAttribute(k,v) { call('set',this._id,String(k),String(v)); }
  appendChild(n) { call('append',this._id,n._id); return n; }
  append(...nodes) { for(const n of nodes) this.appendChild(typeof n==='string'?document.createTextNode(n):n); }
  removeChild(n) { if(n.parentNode!==this) throw new Error('Not a child'); n.remove(); return n; }
  remove() { call('remove',this._id); }
  insertBefore(n,ref) { call('append',this._id,n._id,ref?ref._id:0); return n; }
  replaceChild(n,old) { this.insertBefore(n,old); old.remove(); return old; }
  prepend(...nodes) { const first=this.firstChild; for(const n of nodes) this.insertBefore(typeof n==='string'?document.createTextNode(n):n,first); }
  hasChildNodes() { return !!this.firstChild; }
  contains(n) { for(;n;n=n.parentNode) if(n===this) return true; return false; }
  get parentNode() { return wrap(call('get',this._id,'parentNode')); }
  get parentElement() { const p=this.parentNode; return p&&p.nodeType===1?p:null; }
  get firstChild() { return wrap(call('get',this._id,'firstChild')); }
  get lastChild() { return wrap(call('get',this._id,'lastChild')); }
  get nextSibling() { return wrap(call('get',this._id,'nextSibling')); }
  get previousSibling() { return wrap(call('get',this._id,'previousSibling')); }
  get childNodes() { return call('children',this._id,'',true).map(wrap); }
  get children() { return call('children',this._id).map(wrap); }
  get firstElementChild() { return this.children[0]||null; }
  get lastElementChild() { const c=this.children; return c[c.length-1]||null; }
  get childElementCount() { return this.children.length; }
  get nextElementSibling() { let n=this.nextSibling; while(n&&n.nodeType!==1) n=n.nextSibling; return n; }
  get previousElementSibling() { let n=this.previousSibling; while(n&&n.nodeType!==1) n=n.previousSibling; return n; }
  get isConnected() { return document.documentElement.contains(this); }
  hasAttribute(k) { return this.getAttribute(k)!==null; }
  removeAttribute(k) { this.setAttribute(k,''); }
  toggleAttribute(k,force) { const on=force===undefined?!this.hasAttribute(k):!!force; if(on) this.setAttribute(k,''); else this.removeAttribute(k); return on; }
  get dataset() { const el=this; return new Proxy({},{get:(_,k)=>{const v=el.getAttribute('data-'+cssName(String(k)));return v===null?undefined:v;},set:(_,k,v)=>{el.setAttribute('data-'+cssName(String(k)),v);return true;}}); }
  matches(s) { const p=this.parentNode; return !!p&&p.querySelectorAll(s).includes(this); }
  closest(s) { for(let n=this;n&&n.nodeType===1;n=n.parentNode) if(n.matches(s)) return n; return null; }
  cloneNode() { const n=document.createElement(this.nodeName); return n; }
  insertAdjacentElement(where,n) {
    const p=this.parentNode;
    switch(String(where).toLowerCase()) {
      case 'beforebegin': if(p) p.insertBefore(n,this); break;
      case 'afterbegin': this.insertBefore(n,this.firstChild); break;
      case 'beforeend': this.appendChild(n); break;
      case 'afterend': if(p) p.insertBefore(n,this.nextSibling); break;
    }
    return n;
  }
  insertAdjacentHTML(where,html) { const t=document.createElement('div'); t.innerHTML=html; for(const n of t.childNodes) this.insertAdjacentElement(where==='afterend'||where==='afterbegin'?where:where,n); }
  insertAdjacentText(where,text) { this.insertAdjacentElement(where,document.createTextNode(text)); }
  before(...nodes) { for(const n of nodes) this.insertAdjacentElement('beforebegin',typeof n==='string'?document.createTextNode(n):n); }
  after(...nodes) { for(const n of nodes.reverse()) this.insertAdjacentElement('afterend',typeof n==='string'?document.createTextNode(n):n); }
  replaceWith(...nodes) { this.before(...nodes); this.remove(); }
  replaceChildren(...nodes) { for(const c of this.childNodes) c.remove(); this.append(...nodes); }
  get localName() { return this.nodeName; }
  get namespaceURI() { return 'http://www.w3.org/1999/xhtml'; }
  get attributes() { return []; }
  focus() {} blur() {} click() { this.dispatchEvent(new Event('click')); } scrollIntoView() {}
  get offsetWidth() { return this.clientWidth; } get offsetHeight() { return this.clientHeight; }
  get tagName() { return this.nodeName.toUpperCase(); }
  get ownerDocument() { return document; }
  getBoundingClientRect() { return {x:0,y:0,left:0,top:0,right:this.clientWidth,bottom:this.clientHeight,width:this.clientWidth,height:this.clientHeight}; }
  // Matched in C with the style engine's selectors.
  querySelectorAll(s) { return call('query',this._id,String(s)).map(wrap); }
  querySelector(s) { return wrap(call('query',this._id,String(s),true)); }
  getElementsByTagName(s) { return this.querySelectorAll(s); }
  getElementsByClassName(s) { const k=String(s).trim().split(/\s+/).filter(Boolean); return k.length?this.querySelectorAll(k.map(x=>'.'+x).join('')):[]; }
  getContext(type) { if(this.nodeName!=='canvas'||type!=='2d') return null; if(!this._context) this._context=new Canvas2D(this); return this._context; }
}
for(const k of ['id','className','textContent','innerHTML','value','src','href','type','name','rel','lang','title','alt','placeholder','action','method','target','htmlFor','dir','role','content','charset']) {
  const name=k==='className'?'class':k==='htmlFor'?'for':k;
  Object.defineProperty(Element.prototype,k,{get(){return call('get',this._id,name)||'';},set(v){call('set',this._id,name,String(v));}});
}
for(const k of ['width','height']) Object.defineProperty(Element.prototype,k,{get(){const v=this.getAttribute(k);return v===null?(k==='width'?300:150):Number(v);},set(v){this.setAttribute(k,Math.max(0,Math.floor(Number(v)||0)));if(this._context)this._context._call('resize');}});
for(const k of ['nodeType','nodeName','clientWidth','clientHeight']) Object.defineProperty(Element.prototype,k,{get(){return call('get',this._id,k);}});
class Canvas2D {
  constructor(canvas) { this.canvas=canvas; this._call('init'); }
  _call(op,...args) { return call('canvas',this.canvas._id,op,...args); }
}
for(const k of ['fillStyle','strokeStyle','globalAlpha','lineWidth','globalCompositeOperation']) Object.defineProperty(Canvas2D.prototype,k,{get(){return this._call('get',k);},set(v){this._call('set',k,v);}});
for(const k of ['save','restore','setTransform','resetTransform','transform','translate','scale','rotate','clearRect','fillRect','strokeRect','beginPath','moveTo','lineTo','closePath','fill','stroke','arc','rect','setLineDash']) Canvas2D.prototype[k]=function(...a){return this._call(k,...a);};
// Explicit errors for APIs whose pixel semantics are not implemented.
for(const k of ['clip','drawImage','createPattern','createLinearGradient','createRadialGradient','fillText','strokeText','putImageData','getImageData','bezierCurveTo','quadraticCurveTo']) Canvas2D.prototype[k]=function(){throw new Error('Canvas2D.'+k+' is not supported yet');};
const doc=new EventTarget();
Object.defineProperties(doc,{body:{get:()=>wrap(call('body'))},head:{get:()=>wrap(call('head'))},documentElement:{get:()=>wrap(call('root')).children.find(n=>n.nodeName==='html')||wrap(call('root'))},title:{get:()=>call('title'),set:v=>call('title',0,String(v))}});
doc.createElement=name=>wrap(call('create',0,String(name).toLowerCase()));
doc.createElementNS=(_,name)=>doc.createElement(name);
doc.createTextNode=text=>{const n=wrap(call('create',0,'#text'));n.textContent=text;return n;};
doc.querySelectorAll=s=>call('query',0,String(s)).map(wrap);
doc.querySelector=s=>wrap(call('query',0,String(s),true));
doc.getElementById=id=>wrap(call('byid',0,String(id)));
doc.getElementsByTagName=s=>doc.querySelectorAll(s);
doc.getElementsByClassName=s=>wrap(call('root')).getElementsByClassName(s);
doc.getElementsByName=s=>doc.querySelectorAll('[name="'+String(s).replace(/"/g,'\\"')+'"]');
doc.createDocumentFragment=()=>doc.createElement('div');
doc.createComment=()=>doc.createTextNode('');
doc.hasFocus=()=>true;
doc.readyState='loading';
// Cookies of scripts stay in this page (the browser's cookie jar is for HTTP).
const cookies=new Map();
Object.defineProperty(doc,'cookie',{get:()=>[...cookies].map(([k,v])=>k+'='+v).join('; '),set:v=>{const p=String(v).split(';')[0],i=p.indexOf('=');if(i>0)cookies.set(p.slice(0,i).trim(),p.slice(i+1).trim());}});
globalThis.document=doc;
globalThis.window=globalThis; globalThis.self=globalThis;
globalThis.Event=Event; globalThis.EventTarget=EventTarget; globalThis.HTMLElement=Element;
const win=new EventTarget();
for(const k of ['addEventListener','removeEventListener','dispatchEvent']) globalThis[k]=win[k].bind(win);
globalThis.navigator={userAgent:'Mozilla/5.0 (Barnix; i686) GouseWeb/2.0 QuickJS',platform:'Barnix',language:'ru-RU',languages:['ru-RU','ru','en'],
  hardwareConcurrency:1,cookieEnabled:true,onLine:true,maxTouchPoints:0,vendor:'',product:'Gecko',appName:'Netscape',appVersion:'5.0',doNotTrack:null,
  sendBeacon:()=>true,clipboard:{writeText:async()=>{},readText:async()=>''},serviceWorker:undefined,mediaDevices:undefined};
for(const [k,s] of [['scripts','script'],['forms','form'],['images','img'],['links','a[href]'],['styleSheets','style,link[rel~=stylesheet]']])
  Object.defineProperty(doc,k,{get:()=>doc.querySelectorAll(s)});
globalThis.Image=function(w,h){const n=doc.createElement('img');if(w!==undefined)n.width=w;if(h!==undefined)n.height=h;return n;};
globalThis.Audio=function(){const n=doc.createElement('audio');n.play=()=>Promise.reject(new Error('Audio is not supported'));n.pause=()=>{};return n;};
const href=call('url'), m=/^([a-z][a-z0-9+.-]*:)(?:\/\/([^\/?#:]*)(?::(\d+))?)?([^?#]*)(\?[^#]*)?(#.*)?$/i.exec(href)||[];
globalThis.location={href,protocol:m[1]||'',hostname:m[2]||'',port:m[3]||'',host:(m[2]||'')+(m[3]?':'+m[3]:''),pathname:m[4]||'/',search:m[5]||'',hash:m[6]||'',
  origin:(m[1]||'')+'//'+(m[2]||'')+(m[3]?':'+m[3]:''),assign(){},replace(){},reload(){},toString(){return href;}};
doc.location=location; doc.URL=href; doc.documentURI=href; doc.referrer=''; doc.domain=location.hostname;
doc.visibilityState='visible'; doc.hidden=false; doc.characterSet='UTF-8'; doc.compatMode='CSS1Compat';
class Storage { constructor(){this._m=new Map();} get length(){return this._m.size;} key(i){return [...this._m.keys()][i]??null;}
  getItem(k){k=String(k);return this._m.has(k)?this._m.get(k):null;} setItem(k,v){this._m.set(String(k),String(v));}
  removeItem(k){this._m.delete(String(k));} clear(){this._m.clear();} }
globalThis.localStorage=new Storage(); globalThis.sessionStorage=new Storage();
const mediaMatches=q=>{const min=/min-width:\s*(\d+)/.exec(q),max=/max-width:\s*(\d+)/.exec(q);return !!(min||max)&&(!min||innerWidth>=Number(min[1]))&&(!max||innerWidth<=Number(max[1]));};
globalThis.matchMedia=q=>({matches:mediaMatches(String(q)),media:String(q),
  addListener(){},removeListener(){},addEventListener(){},removeEventListener(){},onchange:null});
globalThis.getComputedStyle=el=>el.style;
globalThis.scrollTo=globalThis.scrollBy=globalThis.scroll=()=>{}; globalThis.scrollX=globalThis.scrollY=globalThis.pageXOffset=globalThis.pageYOffset=0;
globalThis.screen={width:1024,height:768,availWidth:1024,availHeight:768,colorDepth:24};
globalThis.history={length:1,state:null,pushState(){},replaceState(){},back(){},forward(){},go(){}};
globalThis.Node=Element; Object.assign(Element,{ELEMENT_NODE:1,TEXT_NODE:3,COMMENT_NODE:8,DOCUMENT_NODE:9,DOCUMENT_FRAGMENT_NODE:11});
globalThis.Element=Element;
// instanceof checks of frameworks: every element is an Element; text nodes have nodeType 3.
const kind=test=>({[Symbol.hasInstance]:x=>!!x&&x instanceof Element&&test(x)});
globalThis.Text=kind(x=>x.nodeType===3); globalThis.CharacterData=globalThis.Text; globalThis.Comment=kind(()=>false);
for(const name of ['HTMLDivElement','HTMLSpanElement','HTMLAnchorElement','HTMLImageElement','HTMLInputElement','HTMLButtonElement','HTMLFormElement',
  'HTMLSelectElement','HTMLTextAreaElement','HTMLScriptElement','HTMLStyleElement','HTMLLinkElement','HTMLIFrameElement','HTMLCanvasElement',
  'HTMLVideoElement','HTMLMediaElement','HTMLTemplateElement','HTMLBodyElement','HTMLHtmlElement','HTMLHeadElement','HTMLUnknownElement','SVGElement'])
  globalThis[name]=kind(x=>x.nodeType===1);
globalThis.DocumentFragment=globalThis.ShadowRoot=kind(()=>false);
globalThis.Document=globalThis.HTMLDocument={[Symbol.hasInstance]:x=>x===doc};
globalThis.Window={[Symbol.hasInstance]:x=>x===globalThis};
globalThis.CustomEvent=class extends Event { constructor(t,i={}){super(t,i);this.detail=i.detail??null;} };
globalThis.MutationObserver=globalThis.ResizeObserver=globalThis.IntersectionObserver=class { observe(){} unobserve(){} disconnect(){} takeRecords(){return [];} };
globalThis.innerWidth=640;globalThis.innerHeight=480;globalThis.devicePixelRatio=1;
globalThis.performance={now:()=>call('now')};
globalThis.console=Object.fromEntries(['log','warn','error','info','debug'].map(k=>[k,(...a)=>call('log',0,a.map(String).join(' '))]));
let next=1, timers=new Map(), frames=new Map();
// The host calls __webTick only when the earliest timer or a frame is due ('wake').
function timer(f,delay,repeat,args) { const id=next++,at=performance.now()+Math.max(0,Number(delay)||0);timers.set(id,{f,at,delay:Math.max(4,Number(delay)||0),repeat,args});call('wake',0,at);return id; }
globalThis.setTimeout=(f,d,...a)=>timer(f,d,false,a);
globalThis.setInterval=(f,d,...a)=>timer(f,d,true,a);
globalThis.clearTimeout=globalThis.clearInterval=id=>timers.delete(id);
globalThis.requestAnimationFrame=f=>{call('wake',0,0);const id=next++;frames.set(id,f);return id;};
globalThis.cancelAnimationFrame=id=>frames.delete(id);
globalThis.queueMicrotask=f=>{if(typeof f!=='function')throw new TypeError('Callback must be a function');Promise.resolve().then(f);};
globalThis.fetch=async url=>{const r=call('fetch',0,String(url&&url.url||url));return {ok:r.status>=200&&r.status<300,status:r.status,url:r.url,headers:{get:()=>null},text:async()=>r.body,json:async()=>JSON.parse(r.body)};};
globalThis.XMLHttpRequest=class extends EventTarget {
  constructor(){super();this.readyState=0;this.status=0;this.responseText='';this.response='';this.responseType='';}
  open(method,url){this._url=String(url);this.readyState=1;}
  setRequestHeader(){} getResponseHeader(){return null;} getAllResponseHeaders(){return '';} abort(){}
  send(){const done=(type)=>{this.readyState=4;const e=new Event(type);this.dispatchEvent(new Event('readystatechange'));this.dispatchEvent(e);this.dispatchEvent(new Event('loadend'));};
    setTimeout(()=>{try{const r=call('fetch',0,this._url);this.status=r.status;this.responseURL=r.url;this.responseText=r.body;this.response=this.responseType==='json'?JSON.parse(r.body):r.body;done('load');}catch(e){done('error');}},0);}
};
for(const t of ['readystatechange','load','error','loadend']) Object.defineProperty(XMLHttpRequest.prototype,'on'+t,{get(){return this['_on'+t]||null;},set(f){this['_on'+t]=f;},configurable:true});
globalThis.__webTick=(now,w,h)=>{
  if(innerWidth!==w||innerHeight!==h) {globalThis.innerWidth=w;globalThis.innerHeight=h;win.dispatchEvent(new Event('resize'));if(typeof globalThis.onresize==='function')globalThis.onresize(new Event('resize'));}
  const pending=[...frames.keys()];
  for(const [id,t] of [...timers]) if(timers.has(id)&&now>=t.at) {if(t.repeat)t.at=now+t.delay;else timers.delete(id);if(typeof t.f==='function')t.f(...t.args);else (0,eval)(String(t.f));}
  for(const id of pending) {const f=frames.get(id);if(!f)continue;frames.delete(id);f(now);}
  let wake=frames.size?0:Infinity;
  if(!frames.size) for(const t of timers.values()) if(t.at<wake) wake=t.at;
  call('work',frames.size+timers.size>0,wake===Infinity?0:wake);
};
globalThis.__webEvent=(id,type,x,y,key)=>{
  const names={37:'ArrowLeft',38:'ArrowUp',39:'ArrowRight',40:'ArrowDown',32:' ',13:'Enter',27:'Escape'};
  const e=new Event(type,{clientX:x,clientY:y,offsetX:x,offsetY:y,key:names[key]||String.fromCharCode(key),keyCode:key,which:key,button:0,bubbles:true});
  let n=wrap(id);e.target=n;for(;n&&!e.cancelBubble;n=n.parentNode)n.dispatchEvent(e);
  if(!e.cancelBubble)doc.dispatchEvent(e);if(!e.cancelBubble)win.dispatchEvent(e);
  const f=globalThis['on'+type];if(!e.cancelBubble&&typeof f==='function')f(e);
  return e.defaultPrevented;
};
globalThis.__webLoaded=()=>{
  // Inline attributes execute only in response to the corresponding event.
  const kinds=['click','keydown','keyup','mousedown','mouseup','mousemove'];
  for(const n of doc.querySelectorAll(kinds.map(t=>'[on'+t+']').join(','))) for(const t of kinds) {const source=n.getAttribute('on'+t);if(source)n['on'+t]=new Function('event',source);}
  doc.readyState='interactive';doc.dispatchEvent(new Event('DOMContentLoaded'));win.dispatchEvent(new Event('DOMContentLoaded'));
  doc.readyState='complete';win.dispatchEvent(new Event('load'));if(typeof globalThis.onload==='function')globalThis.onload(new Event('load'));
};
})();
