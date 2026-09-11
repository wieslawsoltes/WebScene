/* Kestrel CAD — user-supplied stroke fonts and explicit text style resolution.
 * The font files belong to their owners: no font binary is bundled or serialized.
 * SHX instructions follow Autodesk's published shape specification.
 */
(function(root) {
 'use strict';
 const K=root.Kestrel,{V}=K.Math,G=K.Geo,registry=new Map(),subscribers=new Set();
 const limits=Object.freeze({bytes:16*1024*1024,glyphs:65536,instructions:200000,segments:100000,depth:16});
 const finite=(v,label,min=-1e12,max=1e12)=>{if(typeof v!=='number'||!Number.isFinite(v)||v<min||v>max)throw Error(label+' is outside its range.');return v;};
 let resourceRevision=0;
 const key=name=>String(name||'').split(/[\\/]/).pop().toLowerCase();
 const ascii=bytes=>new TextDecoder('windows-1252').decode(bytes);
 const directions=[[1,0],[1,.5],[1,1],[.5,1],[0,1],[-.5,1],[-1,1],[-1,.5],[-1,0],[-1,-.5],[-1,-1],[-.5,-1],[0,-1],[.5,-1],[1,-1],[1,-.5]];
 class Reader {
  constructor(data,start=0,end=data.length){this.data=data;this.pos=start;this.end=end;}
  u8(){if(this.pos>=this.end)throw Error('Truncated SHX record.');return this.data[this.pos++];}
  u16(){return this.u8()|this.u8()<<8;}
  str(){const start=this.pos;while(this.u8()!==0){}return ascii(this.data.subarray(start,this.pos-1));}
  record(n){if(!Number.isInteger(n)||n<1||this.pos+n>this.end)throw Error('Invalid SHX record size.');const r=new Reader(this.data,this.pos,this.pos+n);this.pos+=n;return r;}
 }
 const signed=b=>b>127?b-256:b;
 function instructions(r,unicode=false) {
  const out=[];let ended=false;
  while(r.pos<r.end){const op=r.u8(),args=[];
   if(op===0){ended=true;out.push({op,args});break;}
   if(op===15)throw Error('Reserved SHX instruction 15.');
   if(op===3||op===4){args.push(r.u8());if(!args[0])throw Error('Zero SHX scale factor.');}
   else if(op===7)args.push(unicode?r.u16():r.u8());
   else if(op===8)args.push(signed(r.u8()),signed(r.u8()));
   else if(op===9||op===13){for(;;){const x=signed(r.u8()),y=signed(r.u8());if(!x&&!y)break;args.push(op===9?[x,y]:[x,y,signed(r.u8())]);}}
   else if(op===10)args.push(r.u8(),r.u8());
   else if(op===11)for(let i=0;i<5;i++)args.push(r.u8());
   else if(op===12)args.push(signed(r.u8()),signed(r.u8()),signed(r.u8()));
   out.push({op,args});if(out.length>8192)throw Error('SHX glyph instruction limit exceeded.');
  }
  if(!ended)throw Error('SHX glyph lacks its end instruction.');
  if(r.pos!==r.end)throw Error('Unexpected bytes after a SHX glyph end.');
  return out;
 }
 function checkGraph(font){const done=new Set();
  function visit(id,stack=[]){if(done.has(id))return;if(stack.includes(id)||stack.length>=limits.depth)throw Error('Recursive or over-deep SHX subshape.');const g=font.glyphs.get(id);if(!g)throw Error('Missing SHX subshape '+id+'.');for(const c of g.codes)if(c.op===7)visit(c.args[0],[...stack,id]);done.add(id);}
  for(const id of font.glyphs.keys())visit(id);
  finite(font.above,'Font cap height',1,65535);finite(font.below,'Font descent',0,65535);
  if(![0,2].includes(font.mode))throw Error('Unsupported SHX writing mode.');
  return font;
 }
 function parseSHX(input){const data=input instanceof Uint8Array?input:new Uint8Array(input);if(data.length>limits.bytes)throw Error('Font exceeds 16 MiB.');
  const signature=ascii(data.subarray(0,40));if(signature.startsWith('AutoCAD-86 bigfont'))throw Error('Big Font SHX is not supported; it is not a Unicode SHX font.');
  const unicode=signature.startsWith('AutoCAD-86 unifont 1.0'),legacy=/^AutoCAD-86 shapes 1\.[01]/.test(signature);
  if(!unicode&&!legacy)throw Error('Unsupported SHX signature. Expected shapes 1.0/1.1 or unifont 1.0.');
  const r=new Reader(data,unicode?24:23);if(r.u8()!==26)throw Error('Invalid SHX signature terminator.');
  const font={kind:'stroke',format:unicode?'SHX Unicode':'SHX shapes',byteLength:data.length,unicode,glyphs:new Map(),cache:new Map()};
  function shape(id,record){if(font.glyphs.has(id))throw Error('Duplicate SHX shape number.');const name=record.str();
   if(id===0&&!unicode){font.name=name;font.above=record.u8();font.below=record.u8();font.mode=record.u8();if(record.u8()!==0)throw Error('Invalid SHX font header.');while(record.pos<record.end)if(record.u8()!==0)throw Error('Unexpected font header bytes.');return;}
   font.glyphs.set(id,{name,codes:instructions(record,unicode)});
  }
  if(unicode){const count=r.u16();r.u16();const header=r.record(r.u16());font.name=header.str();font.above=header.u8();font.below=header.u8();font.mode=header.u8();font.encoding=header.u8();font.embedding=header.u8();if(header.u8()!==0||header.pos!==header.end)throw Error('Invalid Unicode font definition.');
   while(r.pos<r.end){const id=r.u16(),size=r.u16();shape(id,r.record(size));if(font.glyphs.size>=limits.glyphs)throw Error('Too many SHX glyphs.');}
   if(count!==font.glyphs.size+1)throw Error('Unicode SHX record count mismatch.');
  }else{const first=r.u16(),last=r.u16(),count=r.u16();if(!count)throw Error('Empty SHX index.');const entries=[];for(let i=0;i<count;i++)entries.push([r.u16(),r.u16()]);
   if(entries[0][0]!==first||entries.at(-1)[0]!==last)throw Error('SHX index range mismatch.');const ids=new Set();for(const [id,size]of entries){if(ids.has(id))throw Error('Duplicate SHX shape number.');ids.add(id);shape(id,r.record(size));}
   if(r.u8()!==69||r.u8()!==79||r.u8()!==70||r.pos!==r.end)throw Error('Invalid SHX EOF.');
  }
  return checkGraph(font);
 }
 function parseSHP(source){if(typeof source!=='string'||source.length>limits.bytes)throw Error('Invalid SHP text.');
  const clean=source.split(/\r?\n/).map(l=>l.split(';')[0]).join('\n'),records=[...clean.matchAll(/^\s*\*([^,\r\n]+),\s*(\d+),([^\r\n]*)\r?\n([\s\S]*?)(?=^\s*\*|$(?![\s\S]))/gm)];
  if(!records.length)throw Error('No SHP definitions.');
  const font={kind:'stroke',format:'SHP',byteLength:source.length,unicode:records[0][1].trim()==='UNIFONT',glyphs:new Map(),cache:new Map()};
  const value=s=>{s=s.trim();if(!/^[+-]?[0-9a-fA-F]+$/.test(s))throw Error('Invalid SHP integer: '+s);const negative=s.startsWith('-');s=s.replace(/^[+-]/,'');return (negative?-1:1)*parseInt(s,s.length>1&&s[0]==='0'?16:10);};
  for(const match of records){const label=match[1].trim();if(label==='BIGFONT')throw Error('Big Font SHP is not supported.');
   const vals=match[4].replace(/[()\s]/g,'').replace(/,+$/,'').split(',').filter(Boolean).map(value);
   if(label==='UNIFONT'||label==='0'){[font.above,font.below,font.mode]=vals;font.name=match[3].trim();continue;}
   const id=value(label);if(id<0||id>65535||font.glyphs.has(id))throw Error('Invalid or duplicate SHP shape.');
   // Convert the documented source integers to the same checked instruction stream.
   const bytes=[];let i=0;const next=()=>{if(i>=vals.length)throw Error('Truncated SHP instruction.');return vals[i++];};
   const byte=(v,sign=false)=>{if(!Number.isInteger(v)||v<(sign?-127:0)||v>255)throw Error('SHP byte outside range.');bytes.push(v&255);};
   while(i<vals.length){const op=next();byte(op);if(op===7){const sub=next();if(font.unicode){if(sub<0||sub>65535)throw Error('Invalid Unicode subshape.');bytes.push(sub&255,sub>>8);}else byte(sub);}
    else if(op===3||op===4)byte(next());else if(op===8){byte(next(),true);byte(next(),true);}
    else if(op===9||op===13){for(;;){const x=next(),y=next();byte(x,true);byte(y,true);if(!x&&!y)break;if(op===13)byte(next(),true);}}
    else if(op===10||op===11){const n=op===10?2:5;for(let j=0;j<n;j++){const v=next();byte(j===n-1&&v<0?128-v:v);}}
    else if(op===12){byte(next(),true);byte(next(),true);byte(next(),true);}
   }
   if(bytes.length!==Number(match[2]))throw Error('SHP byte count mismatch for '+label+'.');
   font.glyphs.set(id,{name:match[3].trim(),codes:instructions(new Reader(Uint8Array.from(bytes)),font.unicode)});
  }
  return checkGraph(font);
 }
 function glyph(font,code,vertical=false){const id=font.unicode?code:({176:256,177:257,8709:258,8960:258}[code]??code),cacheKey=id+':'+vertical;
  if(font.cache.has(cacheKey))return font.cache.get(cacheKey);if(!font.glyphs.has(id))return null;
  if(vertical&&font.mode!==2)throw Error('This stroke font does not define vertical writing.');
  const state={p:[0,0],scale:1,down:true,stack:[],steps:0,segments:[]};
  function move(p){if(!p.every(Number.isFinite)||Math.max(...p.map(Math.abs))>1e9)throw Error('SHX coordinates exceed safe bounds.');if(state.down&&(p[0]!==state.p[0]||p[1]!==state.p[1])){if(state.segments.length>=limits.segments)throw Error('SHX glyph segment limit.');state.segments.push([state.p.slice(),p.slice()]);}state.p=p;}
  function displacement(x,y){move([state.p[0]+x*state.scale,state.p[1]+y*state.scale]);}
  function arc(radius,start,span){if(!(radius>0)||!Number.isFinite(radius)||Math.abs(span)>Math.PI*2+1e-8)throw Error('Invalid SHX arc.');const center=[state.p[0]-radius*Math.cos(start),state.p[1]-radius*Math.sin(start)];
   const n=Math.max(2,Math.min(720,Math.ceil(Math.abs(span)/(Math.PI/90))));for(let k=1;k<=n;k++){const a=start+span*k/n;move([center[0]+radius*Math.cos(a),center[1]+radius*Math.sin(a)]);}
  }
  function bulge(dx,dy,b){if(!b||!state.down){displacement(dx,dy);return;}const x=dx*state.scale,y=dy*state.scale,bg=b/127;if(!x&&!y)throw Error('Degenerate SHX bulge.');const p=state.p.slice(),c=[p[0]+x/2-y*(1-bg*bg)/(4*bg),p[1]+y/2+x*(1-bg*bg)/(4*bg)];arc(Math.hypot(p[0]-c[0],p[1]-c[1]),Math.atan2(p[1]-c[1],p[0]-c[0]),4*Math.atan(bg));state.p=[p[0]+x,p[1]+y];}
  function render(number,depth=0){if(depth>=limits.depth)throw Error('SHX recursion limit.');state.down=true;let skip=false;
   for(const {op,args:a}of font.glyphs.get(number).codes){if(++state.steps>limits.instructions)throw Error('SHX instruction budget exceeded.');if(op===0)break;if(skip){skip=false;continue;}if(op===14){skip=!vertical;continue;}
    if(op>15){const d=directions[op&15];displacement(d[0]*(op>>4),d[1]*(op>>4));}
    else if(op===1||op===2)state.down=op===1;
    else if(op===3||op===4){state.scale=op===3?state.scale/a[0]:state.scale*a[0];finite(state.scale,'SHX vector scale',1e-12,1e9);}
    else if(op===5){if(state.stack.length>=64)throw Error('SHX stack limit.');state.stack.push(state.p.slice());}
    else if(op===6){if(!state.stack.length)throw Error('SHX stack underflow.');state.p=state.stack.pop();}
    else if(op===7)render(a[0],depth+1);
    else if(op===8)displacement(...a);
    else if(op===9)for(const pair of a)displacement(...pair);
    else if(op===10||op===11){const specs=a.at(-1),startOct=(specs&127)>>4,spanOct=(specs&15)||8,sign=specs&128?-1:1;if(startOct>7||spanOct>8)throw Error('Invalid SHX octants.');const radius=(op===10?a[0]:a[2]*256+a[3])*state.scale;
     const start=(startOct+(op===11?sign*a[0]/256:0))*Math.PI/4;
     const span=sign*(op===10?spanOct:spanOct-1+(a[1]||256)/256-a[0]/256)*Math.PI/4;arc(radius,start,span);
    }else if(op===12)bulge(...a);else if(op===13)for(const triple of a)bulge(...triple);
   }
  }
  render(id);if(state.stack.length)throw Error('Unbalanced SHX location stack.');
  const out={segments:state.segments,advance:state.p.slice()};if((font.cachedSegments||0)+out.segments.length>limits.segments){font.cache.clear();font.cachedSegments=0;}font.cachedSegments=(font.cachedSegments||0)+out.segments.length;font.cache.set(cacheKey,out);return out;
 }
 function defaults(){return [{name:'STANDARD',font:'',height:0,width:1,oblique:0,backwards:false,upsideDown:false,vertical:false}];}
 function styles(doc){return doc?.production?.textstyles||defaults();}
 function style(doc,name='STANDARD'){return styles(doc).find(s=>s.name.toLowerCase()===String(name).toLowerCase())||defaults()[0];}
 function properties(t,doc){const s=style(doc,t.textStyle);return {...t,textStyle:t.textStyle||s.name,font:t.font??s.font??'',widthFactor:t.widthFactor??s.width??1,oblique:t.oblique??s.oblique??0,backwards:t.backwards??!!s.backwards,upsideDown:t.upsideDown??!!s.upsideDown,vertical:t.vertical??!!s.vertical,lineSpacing:t.lineSpacing??1.35};}
 function validate(data){const list=data.production?.textstyles;if(list){if(!Array.isArray(list)||!list.length||list.length>256)throw Error('Invalid text style table.');const seen=new Set();for(const s of list){K.Production.name(s.name);if(seen.has(s.name.toLowerCase()))throw Error('Duplicate text style.');seen.add(s.name.toLowerCase());if(typeof s.font!=='string'||s.font.length>255||/[\x00-\x1f]/.test(s.font))throw Error('Invalid font reference.');finite(s.height??0,'Fixed text height',0,1e9);finite(s.width??1,'Width factor',.001,1000);finite(s.oblique??0,'Oblique angle',-85,85);for(const flag of ['backwards','upsideDown','vertical'])if(s[flag]!==undefined&&typeof s[flag]!=='boolean')throw Error('Invalid text flag.');}if(!seen.has('standard'))throw Error('Keep the STANDARD text style.');if(data.production.currentTextStyle&&!seen.has(data.production.currentTextStyle.toLowerCase()))throw Error('Missing current text style.');}
  for(const e of [...data.entities,...(data.production?.blocks||[]).flatMap(b=>[...b.entities,...b.attributes||[]])]){if(e.textStyle!==undefined&&(typeof e.textStyle!=='string'||e.textStyle.length>255))throw Error('Invalid text style reference.');if(e.widthFactor!==undefined)finite(e.widthFactor,'Text width',.001,1000);if(e.oblique!==undefined)finite(e.oblique,'Text oblique',-85,85);if(e.lineSpacing!==undefined)finite(e.lineSpacing,'Line spacing',.1,10);for(const flag of ['backwards','upsideDown','vertical'])if(e[flag]!==undefined&&typeof e[flag]!=='boolean')throw Error('Invalid text flag.');}
 }
 function register(name,font){if(typeof name!=='string'||!key(name)||name.length>255)throw Error('Invalid font filename.');if(!registry.has(key(name))&&registry.size>=32)throw Error('At most 32 local font resources.');const total=[...registry].filter(([n])=>n!==key(name)).reduce((n,[,f])=>n+(f.byteLength||0),0)+(font.byteLength||0);if(total>64*1024*1024)throw Error('Local fonts exceed 64 MiB.');registry.set(key(name),font);resourceRevision++;for(const fn of subscribers)fn();return font;}
 async function load(name,input){const bytes=input instanceof Uint8Array?input:new Uint8Array(input),suffix=key(name).split('.').pop();if(bytes.length>limits.bytes)throw Error('Font exceeds 16 MiB.');
  if(suffix==='shx')return register(name,parseSHX(bytes));if(suffix==='shp')return register(name,parseSHP(new TextDecoder('utf-8',{fatal:true}).decode(bytes)));
  if(!['ttf','otf','woff','woff2'].includes(suffix)||typeof FontFace==='undefined')throw Error('Select SHX/SHP, or a browser-supported TTF/OTF/WOFF font.');
  const family='KestrelUserFont'+(++load.counter),exportFamily=outlineFamily(bytes)||key(name).replace(/\.[^.]+$/,''),face=new FontFace(family,bytes);await face.load();document.fonts.add(face);const old=registry.get(key(name));if(old?.face)document.fonts.delete(old.face);return register(name,{kind:'outline',byteLength:bytes.length,name:family,family,exportFamily,face,format:suffix.toUpperCase()});
 }load.counter=0;
 function outlineFamily(bytes){
  // sfnt name table only; glyph rasterization/shaping is delegated to FontFace.
  if(bytes.length<12)return null;const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),u16=o=>{if(o+2>v.byteLength)throw Error('Truncated outline font metadata.');return v.getUint16(o);},u32=o=>{if(o+4>v.byteLength)throw Error('Truncated outline font metadata.');return v.getUint32(o);};
  const signature=u32(0);if(signature!==0x10000&&signature!==0x4f54544f)return null;
  const n=u16(4);if(n>512)throw Error('Outline table limit.');let name;
  for(let i=0;i<n;i++){const o=12+i*16;if(u32(o)===0x6e616d65){const start=u32(o+8),len=u32(o+12);if(start+len>bytes.length)throw Error('Invalid outline name table.');name={start,end:start+len};break;}}
  if(!name)return null;const count=u16(name.start+2),strings=name.start+u16(name.start+4);if(count>4096||name.start+6+count*12>name.end)throw Error('Invalid font name records.');const choices=[];
  for(let i=0;i<count;i++){const o=name.start+6+i*12,platform=u16(o),language=u16(o+4),id=u16(o+6),length=u16(o+8),start=strings+u16(o+10);if(![1,16].includes(id)||![0,3].includes(platform))continue;if(start+length>name.end||length%2)throw Error('Invalid font family record.');let text='';for(let j=0;j<length;j+=2)text+=String.fromCharCode(u16(start+j));if(text&&!/[\x00-\x1f]/.test(text))choices.push({text,rank:(id===16?4:0)+(language===0x409?2:0)+(platform===3?1:0)});}
  return choices.sort((a,b)=>b.rank-a.rank)[0]?.text||null;
 }
 let context;
 function metrics(text,font,options={}){if(typeof document!=='undefined'&&!context)context=document.createElement('canvas').getContext('2d');const family=font?.family||'"Segoe UI",Arial,sans-serif';if(!context)return {width:text.length*.66,cap:1,em:1,family};context.font=(options.italic?'italic ':'')+(options.bold?'bold ':'')+'1000px '+family;context.direction=options.textDirection||'ltr';if('letterSpacing' in context)context.letterSpacing='0px';const cap=context.measureText('H').actualBoundingBoxAscent||730;if('letterSpacing' in context)context.letterSpacing=((options.tracking??1)-1)*.2*cap+'px';return {width:context.measureText(text).width/cap,cap:1,em:1000/cap,family};}
 function layout(input,doc){const t=properties(input,doc),font=registry.get(key(t.font))||registry.get(key(t.font)+'.shx'),stroke=font?.kind==='stroke',missing=!!t.font&&!font,placeholder=missing&&(/\.(shx|shp)$/i.test(t.font)||!key(t.font).includes('.')),h=t.height||10,axes=G.textAxes(t),width=t.widthFactor*(t.backwards?-1:1),ys=t.upsideDown?-1:1,slant=Math.tan(t.oblique*Math.PI/180),warnings=new Set(),segments=[],points=[],texts=[];
  const transform=p=>V.add(t.position,V.add(V.mul(axes.x,h*width*(p[0]+slant*p[1])),V.mul(axes.y,h*ys*p[1])));
  if(input.textStyle&&!styles(doc).some(s=>s.name.toLowerCase()===input.textStyle.toLowerCase()))warnings.add('Missing text style: '+input.textStyle);if(style(doc,input.textStyle).bigFont)warnings.add('Big Font dependency is not supported: '+style(doc,input.textStyle).bigFont);if(missing)warnings.add('Missing font: '+t.font);if(t.vertical&&stroke&&font.mode!==2)warnings.add('Font does not support vertical writing: '+t.font);
  const lines=String(t.text||'').split('\n');let count=0;
  for(let lineIndex=0;lineIndex<lines.length;lineIndex++){const line=lines[lineIndex],local=[],vertices=[];let x=0,y=0;
   if(stroke||placeholder){for(const ch of line){const code=ch.codePointAt(0);let g;try{g=stroke?glyph(font,code,t.vertical):null;}catch(error){warnings.add(error.message);g=null;}const actual=!!g;
     if(!g){if(!placeholder)warnings.add('Missing glyph U+'+code.toString(16).toUpperCase()+' in '+t.font);g={segments:ch===' '?[]:[[[0,0],[.6,0]],[[.6,0],[.6,1]],[[.6,1],[0,1]],[[0,1],[0,0]],[[0,0],[.6,1]]],advance:t.vertical?[0,-1.35]:[.8,0]};}
     const unit=actual?1/font.above:1;
     for(const pair of g.segments){if(++count>limits.segments)throw Error('Text stroke segment limit exceeded.');local.push(pair.map(p=>[x+p[0]*unit,y+p[1]*unit]));}
     x+=g.advance[0]*unit+((t.tracking??1)-1)*.2;y=t.vertical?y+g.advance[1]*unit:0;
    }
    const dx=t.align==='center'?-x/2:t.align==='right'?-x:0,dy=t.vertical?0:-lineIndex*t.lineSpacing;
    for(const pair of local)segments.push(pair.map(p=>transform([p[0]+dx+(t.vertical?lineIndex*t.lineSpacing:0),p[1]+dy])));
    const descent=stroke?font.below/font.above:.25;for(const xx of [dx,dx+x])for(const yy of [dy+y-descent,dy+1])vertices.push([xx+(t.vertical?lineIndex*t.lineSpacing:0),yy]);
   }else{const m=metrics(line,font,t),dx=t.align==='center'?-m.width/2:t.align==='right'?-m.width:0,dy=-lineIndex*t.lineSpacing;
    if(t.vertical)warnings.add('Vertical outline text is stacked, not a SHX dual-orientation layout.');
    if(t.vertical){let index=0;for(const ch of line){const cm=metrics(ch,font,t);texts.push({...t,text:ch,position:transform([lineIndex*t.lineSpacing,-index*t.lineSpacing]),fontFamily:m.family,exportFamily:font?.exportFamily,fontEm:m.em,align:'center',vertical:false,fontMissing:missing});index++;}for(const xx of [-.6,.6])for(const yy of [-line.length*t.lineSpacing,1])vertices.push([xx+lineIndex*t.lineSpacing,yy]);}
    else{const resolved={...t,text:line,position:V.add(t.position,V.add(V.mul(axes.x,h*width*slant*dy),V.mul(axes.y,h*ys*dy))),fontFamily:m.family,exportFamily:font?.exportFamily,fontEm:m.em,fontMissing:missing,vertical:false};texts.push(resolved);for(const xx of [dx,dx+m.width])for(const yy of [dy-.25,dy+1])vertices.push([xx,yy]);}
   }
   points.push(...vertices.map(transform));
  }
  points.push(...segments.flat());return {segments,texts,points,warnings:[...warnings]};
 }
 function resource(name){return registry.get(key(name))||registry.get(key(name)+'.shx');}
 function measure(text,input={},doc){const t=properties(input,doc),font=resource(t.font),h=t.height||1;let width=0;
  if(font?.kind==='stroke'){for(const ch of String(text)){const g=glyph(font,ch.codePointAt(0));width+=(g?g.advance[0]/font.above:.8)+((t.tracking??1)-1)*.2;}}
  else if(t.font&&(/\.(shx|shp)$/i.test(t.font)||!key(t.font).includes('.'))&&!font)width=Array.from(text).length*.8;
  else width=metrics(text,font,t).width;
  return {width:Math.max(0,width*h*(t.widthFactor??1)),font};
 }
 function svgText(t,project,color,escape){const h=t.height||10,ax=G.textAxes(t),p=project(t.position),x=project(V.add(t.position,V.mul(ax.x,h))),y=project(V.add(t.position,V.mul(ax.y,h))),w=(t.widthFactor??1)*(t.backwards?-1:1),up=t.upsideDown?-1:1,k=Math.tan((t.oblique||0)*Math.PI/180),a=(x[0]-p[0])*w,b=(x[1]-p[1])*w,c=-(y[0]-p[0])*up-a*k,d=-(y[1]-p[1])*up-b*k;
  return `<text transform="matrix(${[a,b,c,d,...p.slice(0,2)].join(' ')})" x="0" y="0" font-size="${t.fontEm||1}" font-family="${escape(t.exportFamily||t.fontFamily||'Arial,sans-serif')}" font-weight="${t.bold?'bold':'normal'}" font-style="${t.italic?'italic':'normal'}" direction="${t.textDirection==='rtl'?'rtl':'ltr'}" unicode-bidi="isolate" letter-spacing="${((t.tracking??1)-1)*.2}" text-anchor="${t.align==='center'?'middle':(t.align==='right')!==(t.textDirection==='rtl')?'end':'start'}" fill="${color}" stroke="none" data-font="${escape(t.font||'browser default')}">${escape(t.text||'')}</text>`;
 }
 function drawText(ctx,t,project,color,alpha){const h=t.height||10,ax=G.textAxes(t),p=project(t.position),x=project(V.add(t.position,V.mul(ax.x,h))),y=project(V.add(t.position,V.mul(ax.y,h)));if(p[2]<0||p[2]>1)return;
  const w=(t.widthFactor??1)*(t.backwards?-1:1),up=t.upsideDown?-1:1,k=Math.tan((t.oblique||0)*Math.PI/180),a=(x[0]-p[0])*w,b=(x[1]-p[1])*w,c=-(y[0]-p[0])*up-a*k,d=-(y[1]-p[1])*up-b*k;if(Math.abs(a*d-b*c)<.01)return;
  ctx.save();ctx.transform(a,b,c,d,p[0],p[1]);ctx.font=(t.italic?'italic ':'')+(t.bold?'bold ':'')+(t.fontEm||1)+'px '+(t.fontFamily||'Arial,sans-serif');ctx.direction=t.textDirection||'ltr';if('letterSpacing' in ctx)ctx.letterSpacing=((t.tracking??1)-1)*.2+'px';ctx.textAlign=t.align||'left';ctx.textBaseline='alphabetic';ctx.fillStyle=color;ctx.globalAlpha=alpha;ctx.fillText(t.text||'',0,0);ctx.restore();
 }
 function report(doc){const out=new Set();for(const {e}of K.Production.renderEntities(doc)){for(const t of originalGeometry(e).texts||[]){const r=layout({...t,textStyle:t.textStyle||e.textStyle},doc);for(const warning of r.warnings)out.add(warning);}}return [...out];}
 // Keep the complete text-local affine mapping through scale, mirror and shear.
 const originalTransform=G.transform;
 G.transform=function(entity,matrix){if(entity.type!=='TEXT')return originalTransform(entity,matrix);const e=properties(entity,K.Production.owners.get(entity)),ax=G.textAxes(e),tx=K.Math.M.point(matrix,ax.x,0),ty=K.Math.M.point(matrix,ax.y,0),x=V.norm(tx),n=V.norm(V.cross(tx,ty)),y=V.norm(V.cross(n,x)),sx=V.len(tx),sy=V.dot(ty,y);if(sx<1e-12||sy<1e-12)throw Error('Text transform collapses its plane.');const out=originalTransform(e,matrix),wf=e.widthFactor,sign=e.backwards?-1:1,up=e.upsideDown?-1:1;out.direction=x;out.normal=n;out.height=(e.height||10)*sy;out.widthFactor=wf*sx/sy;out.oblique=Math.atan(Math.tan(e.oblique*Math.PI/180)+up*V.dot(ty,x)/(wf*sign*sx))*180/Math.PI;const basis=K.Math.basis(n);out.rotation=Math.atan2(V.dot(x,basis.y),V.dot(x,basis.x));return out;};
 const geometryRevision=new WeakMap(),drawingGeometry=K.Drawing.prototype.geometry;
 K.Drawing.prototype.geometry=function(entity){if(geometryRevision.get(this)!==resourceRevision){this.cache=new WeakMap();geometryRevision.set(this,resourceRevision);}return drawingGeometry.call(this,entity);};

 // Resolve every annotation emitted by any geometry producer, including blocks and tables.
 const originalGeometry=G.geometry;
 G.geometry=function(e,tolerance){const g=originalGeometry(e,tolerance);if(!g.texts?.length)return g;const doc=K.Production.owners.get(e),out={...g,segments:g.segments.slice(),texts:[],points:[...g.segments.flat(),...g.triangles.flatMap(t=>t.points)]};
  for(const t of g.texts){if(t.composition){out.texts.push(t);out.points.push(...t.composition.quads.flat());continue;}const r=layout({...t,textStyle:t.textStyle||e.textStyle},doc);out.segments.push(...r.segments);out.texts.push(...r.texts);out.points.push(...r.points);}return out;};
 const oldValidate=K.Production.validate;K.Production.validate=function(data){oldValidate(data);validate(data);};
 K.Fonts={resource,measure,limits,parseSHX,parseSHP,outlineFamily,glyph,register,load,registry,defaults,styles,style,properties,validate,layout,report,svgText,drawText,subscribe:fn=>{subscribers.add(fn);return()=>subscribers.delete(fn);}};
})(typeof window!=='undefined'?window:globalThis);
