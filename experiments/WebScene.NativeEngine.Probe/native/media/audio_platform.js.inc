R"AUDIOJS(
(()=>{
'use strict';const native=globalThis.__websceneAudio;delete globalThis.__websceneAudio;
const contexts=new WeakMap(),nodes=new WeakMap(),params=new WeakMap(),buffers=new WeakMap();
const nodeToken={};
const invalid=message=>new DOMException(message,'InvalidStateError');
function ctx(context){const c=contexts.get(context);if(!c)throw new TypeError('Illegal AudioContext receiver');if(c.state==='closed')throw invalid('AudioContext is closed');return c;}
class AudioBuffer {
 constructor(options){const channels=Number(options.numberOfChannels??1),length=Number(options.length),rate=Number(options.sampleRate);if(!Number.isInteger(channels)||channels<1||channels>32||!Number.isInteger(length)||length<1||!Number.isFinite(rate)||rate<8000||rate>192000||channels*length*4>268435456)throw new DOMException('Invalid AudioBuffer size','NotSupportedError');buffers.set(this,{channels:Array.from({length:channels},()=>new Float32Array(length)),rate,length});}
 get sampleRate(){return buffers.get(this).rate;}get length(){return buffers.get(this).length;}get numberOfChannels(){return buffers.get(this).channels.length;}get duration(){return this.length/this.sampleRate;}
 getChannelData(channel){channel=Number(channel);if(!Number.isInteger(channel)||!buffers.get(this).channels[channel])throw new DOMException('Channel out of bounds','IndexSizeError');return buffers.get(this).channels[channel];}
 copyFromChannel(destination,channel,start=0){destination.set(this.getChannelData(channel).subarray(start,start+destination.length));}
 copyToChannel(source,channel,start=0){const target=this.getChannelData(channel);target.set(source.subarray(0,Math.max(0,target.length-start)),start);}
}
class AudioParam {
 constructor(context,node,value=1,token){if(token!==nodeToken)throw new TypeError('Illegal constructor');params.set(this,{context,node,value});}
 get value(){return params.get(this).value;}
 set value(value){const p=params.get(this);value=Number(value);if(!Number.isFinite(value))throw new TypeError('Invalid AudioParam value');native('gain',ctx(p.context).id,p.node,value,p.context.currentTime,0);p.value=value;}
 setTargetAtTime(value,start,timeConstant){const p=params.get(this);value=Number(value);start=Number(start);timeConstant=Number(timeConstant);if(!Number.isFinite(value)||!Number.isFinite(start)||start<0||!Number.isFinite(timeConstant)||timeConstant<0)throw new RangeError('Invalid audio automation');native('gain',ctx(p.context).id,p.node,value,start,timeConstant);return this;}
 setValueAtTime(value,start){return this.setTargetAtTime(value,start,0);}
}
class AudioNode {
 constructor(context,kind,id,token){if(token!==nodeToken)throw new TypeError('Illegal constructor');const c=ctx(context);nodes.set(this,{context,id:id??native('node',c.id,kind)});this.context=context;this.channelCount=2;this.channelCountMode='max';this.channelInterpretation='speakers';}
 connect(destination,output=0,input=0){const a=nodes.get(this),b=nodes.get(destination);if(!a||!b||a.context!==b.context)throw new DOMException('Audio nodes must share a context','InvalidAccessError');if(output!==0||input!==0)throw new DOMException('Invalid audio port','IndexSizeError');native('connect',ctx(a.context).id,a.id,b.id);return destination;}
 disconnect(){const a=nodes.get(this);native('disconnect',ctx(a.context).id,a.id);}
 get numberOfInputs(){return 1;}get numberOfOutputs(){return 1;}
}
class GainNode extends AudioNode {constructor(context){super(context,1,undefined,nodeToken);this.gain=new AudioParam(context,nodes.get(this).id,1,nodeToken);}}
class AnalyserNode extends AudioNode {
 constructor(context){super(context,2,undefined,nodeToken);this._fft=2048;this._smoothing=.8;}
 get fftSize(){return this._fft;}set fftSize(v){if(!Number.isInteger(v)||v<32||v>32768||(v&(v-1)))throw new DOMException('Invalid FFT size','IndexSizeError');this._fft=v;}
 get frequencyBinCount(){return this._fft/2;}
 get smoothingTimeConstant(){return this._smoothing;}set smoothingTimeConstant(v){if(!Number.isFinite(v)||v<0||v>1)throw new DOMException('Invalid smoothing','IndexSizeError');this._smoothing=v;}
 getFloatTimeDomainData(array){if(!(array instanceof Float32Array))throw new TypeError('Float32Array required');const n=nodes.get(this);native('samples',ctx(n.context).id,n.id,array.subarray(0,this.fftSize));}
}
class MediaElementAudioSourceNode extends AudioNode {constructor(context,options){if(!(options?.mediaElement instanceof HTMLMediaElement))throw new TypeError('Media element required');try{native('validateSource',ctx(context).id,options.mediaElement);}catch(error){throw invalid(error.message);}super(context,3,undefined,nodeToken);this.mediaElement=options.mediaElement;native('source',ctx(context).id,nodes.get(this).id,this.mediaElement);}get numberOfInputs(){return 0;}}
const tracks=new WeakMap(),streams=new WeakMap();
const trackToken={};
class MediaStreamTrack extends EventTarget {
 constructor(token,id){super();if(token!==trackToken)throw new TypeError('Illegal constructor');tracks.set(this,{id,enabled:true});native('trackBind',id,this);}
 get id(){return 'webscene-audio-'+tracks.get(this).id;}get kind(){return 'audio';}get label(){return 'WebScene audio mix';}
 get readyState(){return native('trackEnded',tracks.get(this).id)?'ended':'live';}get muted(){return false;}
 get enabled(){return tracks.get(this).enabled;}set enabled(value){const t=tracks.get(this);t.enabled=!!value;native('trackEnabled',t.id,t.enabled);}
 clone(){const result=new MediaStreamTrack(trackToken,native('trackClone',tracks.get(this).id));result.enabled=this.enabled;return result;}
 stop(){native('trackStop',tracks.get(this).id);}
 getSettings(){return {channelCount:2};}
}
let streamId=0;
class MediaStream extends EventTarget {
 constructor(input=[]){super();const items=input instanceof MediaStream?input.getTracks():Array.from(input);if(items.some(t=>!tracks.has(t)))throw new TypeError('MediaStreamTrack required');streams.set(this,{id:'webscene-stream-'+(++streamId),tracks:[...new Set(items)]});}
 get id(){return streams.get(this).id;}get active(){return this.getTracks().some(t=>t.readyState==='live');}
 getTracks(){return streams.get(this).tracks.slice();}getAudioTracks(){return this.getTracks();}getVideoTracks(){return [];}
 getTrackById(id){return this.getTracks().find(t=>t.id===id)||null;}
 addTrack(track){if(!tracks.has(track))throw new TypeError('MediaStreamTrack required');const s=streams.get(this);if(!s.tracks.includes(track))s.tracks.push(track);}
 removeTrack(track){const s=streams.get(this),i=s.tracks.indexOf(track);if(i>=0)s.tracks.splice(i,1);}
 clone(){return new MediaStream(this.getTracks().map(t=>t.clone()));}
}
class MediaStreamAudioDestinationNode extends AudioNode {
 constructor(context){super(context,4,undefined,nodeToken);this.stream=new MediaStream([new MediaStreamTrack(trackToken,native('capture',ctx(context).id,nodes.get(this).id))]);}
 get numberOfOutputs(){return 0;}
}
class AudioContext extends EventTarget {
 constructor(options={}){super();const rate=Number(options.sampleRate??48000);contexts.set(this,{id:native('create',rate),state:'suspended',rate});this.destination=new AudioNode(this,0,0,nodeToken);}
 get sampleRate(){return contexts.get(this).rate;}get currentTime(){const c=contexts.get(this);return c.state==='closed'?(c.closedTime||0):native('time',c.id);}get state(){return contexts.get(this).state;}
 async resume(){const c=ctx(this);native('resume',c.id);if(c.state!=='running'){c.state='running';this.dispatchEvent(new Event('statechange'));}}
 async suspend(){const c=ctx(this);native('suspend',c.id);if(c.state!=='suspended'){c.state='suspended';this.dispatchEvent(new Event('statechange'));}}
 async close(){const c=ctx(this);c.closedTime=this.currentTime;native('close',c.id);c.state='closed';this.dispatchEvent(new Event('statechange'));}
 createGain(){return new GainNode(this);}createAnalyser(){return new AnalyserNode(this);}
 createMediaElementSource(element){return new MediaElementAudioSourceNode(this,{mediaElement:element});}
 createMediaStreamDestination(){return new MediaStreamAudioDestinationNode(this);}
 createBuffer(channels,length,rate){return new AudioBuffer({numberOfChannels:channels,length,sampleRate:rate});}
 decodeAudioData(data,success,error){ctx(this);let pending;try{pending=native('decode',data,this.sampleRate);}catch(e){pending=Promise.reject(e);}const promise=pending.catch(error=>{if(error instanceof TypeError)throw error;throw new DOMException(error.message,'EncodingError');}).then(result=>{const buffer=Object.create(AudioBuffer.prototype);buffers.set(buffer,{channels:result.channels,rate:result.sampleRate,length:result.channels[0].length});return buffer;});if(typeof success==='function'||typeof error==='function')promise.then(success,error);return promise;}
}
for(const ctor of [AudioContext,AudioBuffer,AudioNode,AudioParam,GainNode,AnalyserNode,MediaElementAudioSourceNode,MediaStreamAudioDestinationNode,MediaStream,MediaStreamTrack])Object.defineProperty(ctor.prototype,Symbol.toStringTag,{value:ctor.name,configurable:true});
Object.assign(globalThis,{AudioContext,AudioBuffer,AudioNode,AudioParam,GainNode,AnalyserNode,MediaElementAudioSourceNode,MediaStreamAudioDestinationNode,MediaStream,MediaStreamTrack});
})();
)AUDIOJS"
