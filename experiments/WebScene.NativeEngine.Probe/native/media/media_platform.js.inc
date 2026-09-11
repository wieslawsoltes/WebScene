R"MEDIAJS(
(()=>{
'use strict';
const native=globalThis.__websceneMedia;delete globalThis.__websceneMedia;
const states=new WeakMap(),errors=new WeakMap();
function MediaError(){throw new TypeError('Illegal constructor');}
for(const name of ['code','message'])Object.defineProperty(MediaError.prototype,name,{get(){const value=errors.get(this);if(!value)throw new TypeError('Illegal MediaError receiver');return value[name];}});
for(const [name,value]of Object.entries({MEDIA_ERR_ABORTED:1,MEDIA_ERR_NETWORK:2,MEDIA_ERR_DECODE:3,MEDIA_ERR_SRC_NOT_SUPPORTED:4})){Object.defineProperty(MediaError,name,{value});Object.defineProperty(MediaError.prototype,name,{value});}
function mediaError(code,message){const error=Object.create(MediaError.prototype);errors.set(error,{code,message});return error;}

const fail=(message,name='InvalidStateError')=>new DOMException(message,name);
function state(el){
 if(!el||!['VIDEO','AUDIO'].includes(el.tagName))throw new TypeError('Illegal media receiver');
 let s=states.get(el);if(!s){native('brand',el);s={generation:0,readyState:0,networkState:0,duration:NaN,time:0,paused:true,ended:false,seeking:false,rate:1,defaultRate:1,volume:1,muted:el.hasAttribute('muted'),error:null,width:0,height:0,pending:[],played:[],playStart:0,currentSrc:'',frame:0,lastTick:0};states.set(el,s);}
 return s;
}
function emit(el,name){const generation=state(el).generation;setTimeout(()=>{if(state(el).generation===generation)el.dispatchEvent(new Event(name));},0);}
function recordPlayed(s,time){
 if(s.paused||time<=s.playStart)return;
 const ranges=[...s.played,[s.playStart,time]].sort((a,b)=>a[0]-b[0]);s.played=[];
 for(const range of ranges){const last=s.played[s.played.length-1];if(last&&range[0]<=last[1])last[1]=Math.max(last[1],range[1]);else s.played.push(range);}
}
function rejectPlay(s,error){for(const p of s.pending.splice(0))p.reject(error);}
function sync(el,s){native('state',el,s.time,s.rate,!s.paused,s.volume,s.muted);}
function finishPlay(el,s){
 if(s.paused||s.readyState<2||s.playTask)return;
 s.playTask=true;const generation=s.generation;
 setTimeout(()=>{s.playTask=false;if(s.generation!==generation||s.paused||s.readyState<2)return;s.playStart=s.time;try{sync(el,s);}catch(error){s.paused=true;native('state',el,s.time,s.rate,false,s.volume,s.muted);s.error=mediaError(3,String(error.message||error));rejectPlay(s,fail(s.error.message,'NotSupportedError'));emit(el,'error');return;}el.dispatchEvent(new Event('playing'));for(const p of s.pending.splice(0))p.resolve();tick(el,s);},0);
}
function tick(el,s){
 if(s.frame||s.paused||s.readyState<2)return;
 s.lastTick=performance.now();
 s.frame=requestAnimationFrame(now=>{
  s.frame=0;if(s.paused)return;
  s.time=Math.min(s.duration,native('time',el)??s.time);
  if(now-(s.lastEvent||0)>250){s.lastEvent=now;emit(el,'timeupdate');}
  recordPlayed(s,s.time);
  if(s.time>=s.duration){if(el.loop){s.time=0;s.playStart=0;sync(el,s);native('seek',el,0);}else{s.paused=true;s.ended=true;sync(el,s);emit(el,'timeupdate');emit(el,'pause');emit(el,'ended');return;}}
  tick(el,s);
 });
}
function signature(el){return [el.getAttribute('src'),...Array.from(el.querySelectorAll('source'),s=>s.getAttribute('src')+';'+s.getAttribute('type'))].join('|');}
function load(el,select=true){
 const s=state(el),old=s.networkState;s.sourceSignature=signature(el);const generation=++s.generation;
 s.playTask=false;s.abort?.abort();s.abort=new AbortController();native('release',el);
 if(s.frame)cancelAnimationFrame(s.frame);s.frame=0;
 rejectPlay(s,fail('Media load interrupted playback','AbortError'));
 Object.assign(s,{readyState:0,networkState:0,duration:NaN,time:0,paused:true,ended:false,seeking:false,error:null,width:0,height:0,currentSrc:'',played:[],playStart:0});
 if(old){emit(el,'abort');emit(el,'emptied');}
 if(!select){s.sourceSignature=undefined;return;}
 s.rate=s.defaultRate;
 const src=el.getAttribute('src')||Array.from(el.querySelectorAll('source')).find(source=>!source.getAttribute('type')||el.canPlayType(source.getAttribute('type')))?.getAttribute('src');if(!src)return;
 const url=new URL(src,document.baseURI).href;s.currentSrc=url;s.networkState=2;
 emit(el,'loadstart');
 const deliver=function(info){
   if(s.generation!==generation)return;
   if(info.error){s.error=mediaError(3,info.error);s.networkState=3;rejectPlay(s,fail(info.error,'NotSupportedError'));emit(el,'error');return;}
   if(!info.ready)return;
   if(s.buffering!==!!info.buffering){s.buffering=!!info.buffering;if(!s.paused)emit(el,s.buffering?'waiting':'playing');}
   const first=s.readyState===0;Object.assign(s,{duration:info.duration,width:info.width,height:info.height,networkState:1});
   if(first){s.readyState=1;emit(el,'loadedmetadata');s.readyState=2;emit(el,'loadeddata');s.readyState=4;emit(el,'progress');emit(el,'canplay');emit(el,'canplaythrough');}
   if(s.seeking&&!info.seeking){s.seeking=false;emit(el,'timeupdate');emit(el,'seeked');}
   if(first){finishPlay(el,s);if(el.autoplay)el.play().catch(()=>{});}
 };
 const failed=error=>{if(s.generation!==generation)return;s.error=mediaError(4,String(error.message||error));s.networkState=3;rejectPlay(s,fail(s.error.message,'NotSupportedError'));emit(el,'error');};
 if(native('streamAllowed',el,url)){
  try{native('loadURL',el,url,'',deliver);}catch(error){failed(error);}
  return;
 }
 fetch(url,{signal:s.abort.signal}).then(r=>{if(!r.ok)throw Error('Media resource failed: '+r.status);return r.arrayBuffer();}).then(bytes=>{
  if(s.generation!==generation)return;
  const ext=new URL(url).pathname.match(/\.[^.\/]+$/)?.[0]?.toLowerCase()|| (el.tagName==='VIDEO'?'.mp4':'.wav');
  native('load',el,bytes,ext,deliver);
 }).catch(failed);
}
function HTMLMediaElement(){throw new TypeError('Illegal constructor');}
Object.setPrototypeOf(HTMLMediaElement.prototype,HTMLElement.prototype);Object.setPrototypeOf(HTMLMediaElement,HTMLElement);
function HTMLVideoElement(){throw new TypeError('Illegal constructor');}
function HTMLAudioElement(){throw new TypeError('Illegal constructor');}
Object.setPrototypeOf(HTMLVideoElement,HTMLMediaElement);Object.setPrototypeOf(HTMLAudioElement,HTMLMediaElement);Object.setPrototypeOf(HTMLVideoElement.prototype,HTMLMediaElement.prototype);Object.setPrototypeOf(HTMLAudioElement.prototype,HTMLMediaElement.prototype);
const proto=HTMLMediaElement.prototype;
proto.setAttribute=function(name,value){HTMLElement.prototype.setAttribute.call(this,name,value);if(String(name).toLowerCase()==='src')load(this);};
proto.removeAttribute=function(name){HTMLElement.prototype.removeAttribute.call(this,name);if(String(name).toLowerCase()==='src')load(this);};
const prop=(name,get,set)=>Object.defineProperty(proto,name,{get,set,enumerable:true,configurable:true});
for(const name of ['readyState','networkState','duration','paused','ended','seeking','error','currentSrc'])prop(name,function(){return state(this)[name];});
prop('src',function(){const v=this.getAttribute('src');return v?new URL(v,document.baseURI).href:'';},function(v){this.setAttribute('src',String(v));});
prop('currentTime',function(){const s=state(this);const time=s.paused?s.time:(native('time',this)??s.time);return Number.isFinite(s.duration)?Math.min(s.duration,time):time;},function(v){const s=state(this);v=Number(v);if(!Number.isFinite(v))throw new TypeError('Invalid currentTime');recordPlayed(s,this.currentTime);s.time=Math.max(0,Number.isFinite(s.duration)?Math.min(v,s.duration):v);s.playStart=s.time;s.ended=false;sync(this,s);if(s.readyState){s.seeking=true;emit(this,'seeking');native('seek',this,s.time);}});
prop('playbackRate',function(){return state(this).rate;},function(v){v=Number(v);if(!Number.isFinite(v)||v<0||v>16)throw fail('Unsupported playback rate','NotSupportedError');const s=state(this);s.time=this.currentTime;s.rate=v;sync(this,s);emit(this,'ratechange');});
prop('defaultPlaybackRate',function(){return state(this).defaultRate;},function(v){v=Number(v);if(!Number.isFinite(v)||v<0||v>16)throw fail('Unsupported playback rate','NotSupportedError');state(this).defaultRate=v;emit(this,'ratechange');});
prop('volume',function(){return state(this).volume;},function(v){v=Number(v);if(!Number.isFinite(v)||v<0||v>1)throw fail('Volume out of range','IndexSizeError');const s=state(this);s.time=this.currentTime;s.volume=v;sync(this,s);emit(this,'volumechange');});
prop('muted',function(){return state(this).muted;},function(v){const s=state(this);s.time=this.currentTime;s.muted=!!v;sync(this,s);emit(this,'volumechange');});
for(const name of ['autoplay','loop','defaultMuted','playsInline']){const attr={defaultMuted:'muted',playsInline:'playsinline'}[name]||name;prop(name,function(){return this.hasAttribute(attr);},function(v){if(v)this.setAttribute(attr,'');else this.removeAttribute(attr);});}
for(const name of ['preload','crossOrigin']){const attr=name.toLowerCase();prop(name,function(){return this.getAttribute(attr)||'';},function(v){this.setAttribute(attr,String(v));});}
for(const name of ['videoWidth','videoHeight'])Object.defineProperty(HTMLVideoElement.prototype,name,{get(){return state(this)[name==='videoWidth'?'width':'height'];},configurable:true});
proto.load=function(){load(this);};proto.pause=function(){const s=state(this);if(!s.paused){s.time=this.currentTime;recordPlayed(s,s.time);s.paused=true;sync(this,s);emit(this,'timeupdate');emit(this,'pause');}rejectPlay(s,fail('Playback paused','AbortError'));};
proto.play=function(){const s=state(this);if(!s.currentSrc)load(this);if(!s.currentSrc)return Promise.reject(fail('No media source','NotSupportedError'));if(s.error)return Promise.reject(fail(s.error.message,'NotSupportedError'));if(s.ended){s.time=0;s.ended=false;native('seek',this,0);}const changed=s.paused;s.paused=false;const result=new Promise((resolve,reject)=>s.pending.push({resolve,reject}));if(changed)emit(this,'play');if(s.readyState>=2)finishPlay(this,s);return result;};
proto.canPlayType=function(type){type=String(type).toLowerCase();if(this.tagName==='VIDEO')return !native('videoSupported',this)?'':/^(video\/(mp4|quicktime))([; ]|$)/.test(type)?'maybe':'';return /^(audio\/(wav|wave|x-wav|mpeg|flac|x-flac))([; ]|$)/.test(type)?'maybe':'';};
for(const [name,value]of Object.entries({HAVE_NOTHING:0,HAVE_METADATA:1,HAVE_CURRENT_DATA:2,HAVE_FUTURE_DATA:3,HAVE_ENOUGH_DATA:4,NETWORK_EMPTY:0,NETWORK_IDLE:1,NETWORK_LOADING:2,NETWORK_NO_SOURCE:3})){Object.defineProperty(proto,name,{value,enumerable:true});Object.defineProperty(HTMLMediaElement,name,{value,enumerable:true});}
const timeRanges=new WeakMap();
function TimeRanges(){throw new TypeError('Illegal constructor');}
Object.defineProperty(TimeRanges.prototype,'length',{get(){return timeRanges.get(this).length;}});
for(const [method,index]of [['start',0],['end',1]])TimeRanges.prototype[method]=function(i){i=Number(i)>>>0;const ranges=timeRanges.get(this);if(i>=ranges.length)throw fail('TimeRanges index','IndexSizeError');return ranges[i][index];};
for(const name of ['buffered','seekable','played'])prop(name,function(){const s=state(this);if(name==='played')recordPlayed(s,this.currentTime);const ranges=name==='played'?s.played.map(r=>r.slice()):s.readyState&&s.duration>0?[[0,s.duration]]:[];const result=Object.create(TimeRanges.prototype);timeRanges.set(result,ranges);return result;});
Object.assign(globalThis,{MediaError,TimeRanges,HTMLMediaElement,HTMLVideoElement,HTMLAudioElement,Audio:function(src){const el=document.createElement('audio');el.preload='auto';if(src!==undefined)el.src=src;return el;}});
for(const ctor of [HTMLMediaElement,HTMLVideoElement,HTMLAudioElement,MediaError,TimeRanges])Object.defineProperty(ctor.prototype,Symbol.toStringTag,{value:ctor.name,configurable:true});
globalThis.Audio.prototype=HTMLAudioElement.prototype;
function initialize(el,connected){
 const s=state(el);if(s.connected&&!connected)load(el,false);s.connected=connected;
 if(!connected||s.initializeQueued||s.sourceSignature===signature(el))return;
 s.initializeQueued=true;setTimeout(()=>{s.initializeQueued=false;if(s.sourceSignature!==signature(el)&&(el.hasAttribute('src')||el.querySelector('source')||s.currentSrc))load(el);},0);
}
initialize.videoPrototype=HTMLVideoElement.prototype;initialize.audioPrototype=HTMLAudioElement.prototype;
globalThis.__websceneInitializeMedia=initialize;

})();
)MEDIAJS"
