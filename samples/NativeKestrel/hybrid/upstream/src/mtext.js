/* Kestrel CAD — rich, native MTEXT. Original MIT implementation.
 * Logical content is authoritative. Composition caches are disposable; no HTML
 * or imported field expression is ever evaluated. See docs/MTEXT.md.
 */
(function(root){
'use strict';
const K=root.Kestrel,F=K.Fonts,U=K.Unicode,G=K.Geo,{V,M}=K.Math;
const LIMITS=Object.freeze({characters:50000,depth:32,items:20000,lines:4096,columns:32});
const number=(v,label,min,max)=>{if(typeof v!=='number'||!Number.isFinite(v)||v<min||v>max)throw Error('Invalid MTEXT '+label+'.');return v;};
const copy=v=>JSON.parse(JSON.stringify(v));
const point=p=>K.Production.point(p);
const defaultParagraph=()=>({align:'left',left:0,right:0,indent:0,tabs:[],before:0,after:0});
const plain=s=>String(s).replace(/\\U\+([0-9a-f]{4})/gi,(_,n)=>String.fromCharCode(parseInt(n,16))).replace(/%%d/gi,'°').replace(/%%p/gi,'±').replace(/%%c/gi,'⌀');
function parse(text,base={}){
 if(typeof text!=='string'||text.length>LIMITS.characters)throw Error('MTEXT content limit: '+LIMITS.characters+' characters.');
 const paragraphs=[],warnings=new Set(),stack=[];let paragraph={...defaultParagraph(),...(base.paragraph||{})},style={height:base.height||1,widthFactor:base.widthFactor??1,oblique:base.oblique||0,tracking:1,font:base.font||'',bold:false,italic:false,underline:false,overline:false,strike:false,alignment:0},items=[],buffer='',columnBreak=false,total=0;
 const emit=(kind,value)=>{if(++total>LIMITS.items)throw Error('MTEXT item limit exceeded.');items.push({kind,...value,style:{...style}});};
 const flush=()=>{if(buffer){emit('text',{text:buffer});buffer='';}};
 const finish=()=>{flush();paragraphs.push({...copy(paragraph),items,columnBreak});items=[];columnBreak=false;};
 const setting=(cmd,arg)=>{
  const value=Number(arg.replace(/x$/i,'')),relative=/x$/i.test(arg);
  if(['H','W','T'].includes(cmd)){if(!Number.isFinite(value)||value<=0||value>10000){warnings.add('Ignored invalid \\'+cmd+' value.');return;}const key=cmd==='H'?'height':cmd==='W'?'widthFactor':'tracking';style[key]=relative?style[key]*value:value;number(style[key],cmd,.000001,1e9);return;}
  if(cmd==='Q'){if(Number.isFinite(value)&&Math.abs(value)<=85)style.oblique=value;else warnings.add('Invalid oblique angle.');return;}
  if(cmd==='A'){style.alignment=[0,1,2].includes(value)?value:0;return;}
  if(cmd==='C'){if(Number.isInteger(value)&&value>=0&&value<=256)style.color=value===0||value===256?null:K.Exchange.aciColor(value);else warnings.add('Invalid indexed MTEXT color.');return;}
  if(cmd==='c'){if(Number.isInteger(value)&&value>=0&&value<=0xffffff)style.color='#'+value.toString(16).padStart(6,'0');else warnings.add('Invalid true MTEXT color.');return;}
  if(cmd==='f'||cmd==='F'){const [font,...parts]=arg.split('|');if(font.length>255||/[\x00-\x1f]/.test(font))throw Error('Invalid inline font reference.');style.font=font||style.font;for(const token of parts){if(/^b[01]$/.test(token))style.bold=token==='b1';else if(/^i[01]$/.test(token))style.italic=token==='i1';else if(!/^[cp]\d+$/.test(token))warnings.add('Unknown inline font option: '+token);}return;}
  if(cmd==='p'){
   const args=arg.split(',');let tabs=false;for(let part of args){if(part==='x'){paragraph=defaultParagraph();continue;}if(part.startsWith('x'))part=part.slice(1);if(!part)continue;
    if(/^q[lcjdr]$/.test(part)){paragraph.align=({l:'left',c:'center',r:'right',j:'justify',d:'distribute'})[part[1]];tabs=false;}
    else if(/^[ilrab]-?(?:\d|\.)/.test(part)){const v=Number(part.slice(1));if(!Number.isFinite(v)||Math.abs(v)>10000){warnings.add('Invalid paragraph distance.');continue;}paragraph[({i:'indent',l:'left',r:'right',a:'after',b:'before'})[part[0]]]=v;tabs=false;}
    else if(part==='t'||part.startsWith('t')){paragraph.tabs=[];tabs=true;const v=Number(part.slice(1));if(part.length>1&&Number.isFinite(v))paragraph.tabs.push(v);}
    else if(tabs&&Number.isFinite(Number(part)))paragraph.tabs.push(Number(part));
    else warnings.add('Unsupported paragraph option: '+part);
   }paragraph.tabs=[...new Set(paragraph.tabs.filter(x=>x>0&&x<1e6))].sort((a,b)=>a-b);return;
  }
 };
 for(let i=0;i<text.length;){const ch=text[i++];
  if(ch==='{'||ch==='}'){flush();if(ch==='{'){if(stack.length>=LIMITS.depth)throw Error('MTEXT formatting nesting limit.');stack.push({style:{...style},paragraph:copy(paragraph)});}else if(stack.length){const saved=stack.pop();style=saved.style;paragraph=saved.paragraph;}else warnings.add('Unmatched closing formatting brace.');continue;}
  if(ch==='\n'||ch==='\r'){if(ch==='\r'&&text[i]==='\n')i++;finish();continue;}
  if(ch==='%'&&text[i]==='<'){flush();const end=text.indexOf('>%',i+1);if(end>=0){buffer='%'+text.slice(i,end+2);i=end+2;flush();warnings.add('Fields are retained as literal content; no external expression is executed.');continue;}}
  if(ch==='%'&&text[i]==='%'&&/[dpc]/i.test(text[i+1]||'')){buffer+=({d:'°',p:'±',c:'⌀'})[text[i+1].toLowerCase()];i+=2;continue;}
  if(ch==='\t'){flush();emit('tab',{});continue;}
  if(ch!=='\\'){buffer+=ch;continue;}
  if(i===text.length){buffer+='\\';break;}const cmd=text[i++];
  if(['\\','{','}'].includes(cmd)){buffer+=cmd;continue;}
  if(cmd==='U'&&text[i]==='+'&&/^[0-9a-f]{4}/i.test(text.slice(i+1))){buffer+=String.fromCharCode(parseInt(text.slice(i+1,i+5),16));i+=5;continue;}
  if(cmd==='~'){buffer+='\u00a0';continue;}
  flush();
  if(cmd==='P'||cmd==='N'){finish();columnBreak=cmd==='N';continue;}
  if('LlOoKk'.includes(cmd)){style[({L:'underline',l:'underline',O:'overline',o:'overline',K:'strike',k:'strike'})[cmd]]=cmd===cmd.toUpperCase();continue;}
  if('HWTAQCcfFSp'.includes(cmd)){
   let arg='',ended=false;for(;i<text.length;){const q=text[i++];if(q===';'){ended=true;break;}if(q==='\\'&&cmd==='S'&&i<text.length){arg+='\\'+text[i++];}else arg+=q;}
   if(!ended)warnings.add('Unterminated \\'+cmd+' control retained in source.');
   if(cmd==='S'){const m=arg.match(/^(.*?)(?<!\\)([\/#^])(.*)$/s);if(m){const unescape=s=>s.replace(/\\([\\;#^/])/g,'$1');emit('stack',{top:plain(unescape(m[1])),bottom:plain(unescape(m[3].replace(/^ /,''))),divider:m[2]});}else emit('text',{text:arg});}
   else setting(cmd,arg);continue;
  }
  buffer+='\\'+cmd;warnings.add('Unknown MTEXT control \\'+cmd+' displayed literally.');
 }
 finish();if(stack.length)warnings.add('Unclosed formatting group.');return {paragraphs,warnings:[...warnings]};
}
function validate(e){
 if(e.type!=='MTEXT')return;
 point(e.position);number(e.height,'height',1e-6,1e8);number(e.width??0,'width',0,1e10);
 if(!Number.isInteger(e.attachment??1)||(e.attachment??1)<1||(e.attachment??1)>9)throw Error('MTEXT attachment must be 1–9.');
 number(e.spacingFactor??1,'spacing factor',.25,4);if(![1,2].includes(e.spacingStyle??1))throw Error('Invalid MTEXT line spacing style.');
 if(!['auto','ltr','rtl'].includes(e.textDirection||'auto'))throw Error('Invalid MTEXT paragraph direction.');
 if(e.background){number(e.background.padding??.2,'background padding',0,10);if(!/^#[0-9a-f]{6}$/i.test(e.background.color||'#ffffff'))throw Error('Invalid MTEXT background color.');}
 const axes=localAxes(e);if(V.len(V.cross(axes.x,axes.y))<1e-10)throw Error('MTEXT text plane is degenerate.');
 if(e.columns){const c=e.columns;number(c.count,'column count',1,LIMITS.columns);if(!Number.isInteger(c.count))throw Error('Column count must be integral.');number(c.width,'column width',1e-6,1e9);number(c.gutter??0,'column gutter',0,1e9);number(c.height??0,'column height',0,1e10);if(c.autoHeight!==undefined&&typeof c.autoHeight!=='boolean')throw Error('Invalid column height mode.');if(c.reverse!==undefined&&typeof c.reverse!=='boolean')throw Error('Invalid column flow.');}
 if(e.drawingDirection!=null&&![1,3,5].includes(e.drawingDirection))throw Error('Invalid MTEXT drawing direction.');
 if(e.drawingDirection===3)throw Error('Top-to-bottom MTEXT needs a vertical composition engine; retain the original source instead.');
 parse(e.text,{height:e.height});
}
function localAxes(e){const ax=G.textAxes(e);return {x:e.textAxisX?point(e.textAxisX):ax.x,y:e.textAxisY?point(e.textAxisY):ax.y};}
const glyphs=typeof Intl.Segmenter==='function'?new Intl.Segmenter(undefined,{granularity:'grapheme'}):null;
const graphemes=s=>glyphs?Array.from(glyphs.segment(s),v=>v.segment):Array.from(s);
function compose(e,doc){
 validate(e);const props=F.properties(e,doc),parsed=parse(e.text,{...props,height:e.height}),warnings=new Set(parsed.warnings),allLines=[],h=e.height;
 const maxWidth=e.columns?.width||e.width||1e12,measureCache=new Map();
 const measure=(text,style)=>{const key=JSON.stringify([text,style]);if(measureCache.has(key))return measureCache.get(key);const result=F.measure(text,style,doc);if(measureCache.size<20000)measureCache.set(key,result.width);return result.width;};
 let cpBudget=0;
 for(const para of parsed.paragraphs){
  const units=[];let logical='',cpOffset=0;
  for(const item of para.items){
   if(item.kind==='text'){
    // Word-safe wrapping with grapheme-safe emergency breaks. NBSP stays glued.
    const words=item.text.match(/[^ \t\r\n\u200b]+|[ \t]+|\u200b/g)||[];
    for(const word of words){const chunks=/[\u3000-\u9fff\uac00-\ud7af]/u.test(word)||measure(word,item.style)>maxWidth?graphemes(word):[word];
     for(const text of chunks){const cp=Array.from(text);if((cpBudget+=cp.length)>LIMITS.characters)throw Error('MTEXT glyph limit.');units.push({kind:'text',text,style:item.style,start:cpOffset,count:cp.length,width:measure(text,item.style),space:/^[ \t\u200b]+$/.test(text)});logical+=text;cpOffset+=cp.length;}
    }
   }else{const text=item.kind==='tab'?'\t':'\ufffc',small={...item.style,height:item.style.height*.7};units.push({...item,start:cpOffset,count:1,width:item.kind==='stack'?Math.max(measure(item.top,small),measure(item.bottom,small))+item.style.height*.25:0});logical+=text;cpOffset++;}
  }
  const cps=Array.from(logical,c=>c.codePointAt(0)),bidi=U.runBidi(cps,e.textDirection||'auto',{mirror:false,singleParagraph:true}),types=U.classify(cps),survivors=new Set(bidi.map);
  // Split mixed-direction words at resolved run boundaries without splitting graphemes.
  const resolved=[];
  for(const u of units){if(u.kind!=='text'){resolved.push({...u,level:bidi.levels[u.start]||0});continue;}let at=u.start,text='',begin=at,level=bidi.levels[at]||0;
   const flush=()=>{if(text)resolved.push({...u,start:begin,count:at-begin,text,level,width:measure(text,u.style)});text='';begin=at;};
   for(const g of graphemes(u.text)){const n=Array.from(g).length,next=bidi.levels[at]||0;if(next!==level){flush();level=next;}if(survivors.has(at)&&!/^[\u061c\u200e\u200f\u202a-\u202e\u2066-\u2069]$/.test(g))text+=g;at+=n;}flush();
  }
  let first=true,current=[],width=0;const available=()=>Math.max(h*.1,maxWidth-(para.left+para.right+(first?para.indent:0))*h);
  const flush=(last=false)=>{while(current.at(-1)?.space){width-=current.pop().width;}
   const maxHeight=Math.max(h,...current.map(v=>v.style.height*(v.kind==='stack'?1.7:1))),lineHeight=e.spacingStyle===2?h*5/3*(e.spacingFactor||1):Math.max(h*5/3*(e.spacingFactor||1),maxHeight*1.15);
   const begin=current[0]?.start||0,end=current.length?current.at(-1).start+current.at(-1).count:begin,levels=bidi.levels.slice(begin,end),removed=Uint8Array.from(cps.slice(begin,end),(_,i)=>survivors.has(begin+i)?0:1);
   U.applyL1(types.slice(begin,end),levels,removed,bidi.direction==='rtl'?1:0);
   const order=U.reorderIndices(Uint8Array.from(current,u=>levels[u.start-begin]??u.level),new Uint8Array(current.length));
   const ordered=order.map(i=>current[i]);
   const merged=[];for(const u of ordered){const last=merged.at(-1),back=u.level%2===1;if(last&&u.kind==='text'&&last.kind==='text'&&last.level===u.level&&JSON.stringify(last.style)===JSON.stringify(u.style)&&(back?u.start+u.count===last.start:last.start+last.count===u.start)){
     last.text=back?u.text+last.text:last.text+u.text;last.start=Math.min(u.start,last.start);last.count+=u.count;last.width=measure(last.text,last.style);
    }else merged.push({...u});}
   const used=merged.reduce((n,u)=>n+u.width,0),room=available(),extra=Math.max(0,room-used),align=para.align;
   const gaps=ordered.filter(u=>u.space).length;let x=(para.left+(first?para.indent:0))*h+(align==='right'?extra:align==='center'?extra/2:0),advanceExtra=!last&&align==='justify'&&gaps?extra/gaps:!last&&align==='distribute'&&merged.length>1?extra/(merged.length-1):0;
   // Justification keeps whitespace as separate boxes instead of modifying glyph widths.
   const placed=advanceExtra?ordered:merged;let out=[];
   for(const u of placed){out.push({...u,x});x+=u.width+(advanceExtra&&(align==='distribute'||u.space)?advanceExtra:0);}
   allLines.push({items:out,height:lineHeight,inkHeight:maxHeight,width:x,paragraph:para,first,last,direction:bidi.direction,columnBreak:para.columnBreak&&first,before:first?para.before*h:0,after:last?para.after*h:0});
   if(allLines.length>LIMITS.lines)throw Error('MTEXT line limit.');current=[];width=0;first=false;
  };
  for(const item of resolved){let u={...item};if(u.kind==='tab'){const origin=(para.left+(first?para.indent:0))*h,next=para.tabs.map(t=>t*h).find(t=>t>origin+width+1e-8)??(Math.floor((origin+width)/(h*4))+1)*h*4;u.width=next-origin-width;}
   if(width+u.width>available()+1e-9&&current.length){flush();if(u.space)continue;if(u.kind==='tab')u.width=h*4;}
   current.push(u);width+=u.width;
  }flush(true);
 }
 const columns=e.columns||{count:1,width:maxWidth,gutter:0,height:0},count=columns.count||1;
 let limit=columns.height||Infinity;
 const partition=cap=>{const out=[[]];let y=0;for(const line of allLines){if((line.columnBreak||y+line.before+line.height>cap+1e-8)&&out.at(-1).length&&out.length<count){out.push([]);y=0;}out.at(-1).push(line);y+=line.before+line.height+line.after;}return out;};
 if(count>1&&(columns.autoHeight!==false||!columns.height)){let lo=Math.max(...allLines.map(l=>l.height)),hi=allLines.reduce((n,l)=>n+l.height+l.before+l.after,0);for(let i=0;i<32;i++){const mid=(lo+hi)/2,parts=partition(mid),lastHeight=parts.at(-1).reduce((n,l)=>n+l.height+l.before+l.after,0);if(lastHeight>mid)lo=mid;else hi=mid;}limit=hi;}
 const parts=partition(limit),actualWidth=e.columns?count*(columns.width||maxWidth)+(count-1)*(columns.gutter||0):e.width||Math.max(h,...allLines.map(l=>l.width));
 let actualHeight=0;const runs=[],lines=[];
 parts.forEach((part,col)=>{let y=0;for(const line of part){y+=line.before;const base=y+line.inkHeight,dx=(columns.reverse?count-1-col:col)*((columns.width||maxWidth)+(columns.gutter||0));
  for(const item of line.items){if(item.kind==='tab')continue;const s=item.style,x=dx+item.x,baseline=base-(s.alignment===2?line.inkHeight-s.height:s.alignment===1?(line.inkHeight-s.height)/2:0);
   if(item.kind==='stack'){const st={...s,height:s.height*.7},tw=measure(item.top,st),bw=measure(item.bottom,st);runs.push({text:item.top,x:x+(item.width-tw)/2,y:baseline-s.height*.75,style:st,direction:line.direction},{text:item.bottom,x:x+(item.width-bw)/2,y:baseline+s.height*.15,style:st,direction:line.direction});if(item.divider!=='^')lines.push({a:[x,baseline-s.height*.5],b:[x+item.width,baseline-s.height*(item.divider==='#'?1:.5)],color:s.color});}
   else {runs.push({text:item.text,x,y:baseline,style:s,direction:item.level%2?'rtl':'ltr',width:item.width});for(const [flag,at]of [['underline',.12],['overline',-1.08],['strike',-.42]])if(s[flag])lines.push({a:[x,baseline+at*s.height],b:[x+item.width,baseline+at*s.height],color:s.color});}
  }line.column=col;line.y=y;y+=line.height+line.after;}actualHeight=Math.max(actualHeight,y);});
 if(Number.isFinite(limit)&&actualHeight>limit+1e-6)warnings.add('Text exceeds the configured column height; overflow remains visible.');
 if(e.columns)warnings.add('Multi-column DXF output requires explicit evaluated text explosion; native saving retains columns.');
 const a=(e.attachment||1)-1,offset=[-(a%3)*actualWidth/2,-Math.floor(a/3)*actualHeight/2],ax=localAxes(e);
 const world=(x,y)=>V.add(e.position,V.add(V.mul(ax.x,x+offset[0]),V.mul(ax.y,-y-offset[1])));
 const quads=parts.map((part,i)=>{const x=(columns.reverse?count-1-i:i)*((columns.width||maxWidth)+(columns.gutter||0)),w=e.columns?columns.width:actualWidth,hh=part.reduce((n,l)=>n+l.height+l.before+l.after,0);return [[x,0],[x+w,0],[x+w,hh],[x,hh]].map(p=>world(...p));});
 return {runs,lines,allLines,width:actualWidth,height:actualHeight,offset,quads,warnings:[...warnings],world,axes:ax};
}
function geometry(e,doc){const composition=compose(e,doc),points=composition.quads.flat();composition.prepared=composition.runs.map(run=>runGeometry(e,composition,run,doc));for(const g of composition.prepared)points.push(...g.points);composition.warnings=[...new Set([...composition.warnings,...composition.prepared.flatMap(g=>g.warnings)])];return {segments:[],wireSegments:[],triangles:[],texts:[{...e,composition}],snaps:[{point:e.position,type:'insertion'}],points};}
function runGeometry(e,c,run,doc){const s=run.style,n=V.norm(V.cross(c.axes.x,c.axes.y)),x=V.norm(c.axes.x),sx=V.len(c.axes.x),y=V.norm(V.cross(n,x)),sy=V.dot(c.axes.y,y),shear=V.dot(c.axes.y,x),oblique=Math.atan(Math.tan(s.oblique*Math.PI/180)+shear/(sx*s.widthFactor))*180/Math.PI;
 const font=F.resource(s.font);let text=run.text;
 if(font?.kind==='stroke'&&run.direction==='rtl')text=U.render(text,{direction:'rtl'});
 return F.layout({type:'TEXT',text,font:s.font,textStyle:e.textStyle,position:c.world(run.x,run.y),height:s.height*sy,widthFactor:s.widthFactor*sx/sy,oblique,direction:x,normal:n,align:'left',tracking:s.tracking,bold:s.bold,italic:s.italic,textDirection:run.direction},doc);
}
function draw(ctx,e,project,color,alpha,theme='dark',selected=false){const c=e.composition,doc=K.Production.owners.get(e);if(!c)return;
 const p=project(e.position);if(p[2]<0||p[2]>1)return;ctx.save();ctx.globalAlpha=alpha;
 if(e.background){const padding=(e.background.padding??.2)*e.height,q=[[-padding,-padding],[c.width+padding,-padding],[c.width+padding,c.height+padding],[-padding,c.height+padding]].map(v=>project(c.world(...v)));ctx.beginPath();q.forEach((v,i)=>i?ctx.lineTo(v[0],v[1]):ctx.moveTo(v[0],v[1]));ctx.closePath();ctx.fillStyle=e.background.useWindow?(theme==='dark'?'#17212b':'#f8fafc'):e.background.color||'#ffffff';ctx.fill();}
 for(const [index,run] of c.runs.entries()){const ink=selected?color:run.style.color||color,g=c.prepared?.[index]||runGeometry(e,c,run,doc);for(const t of g.texts)F.drawText(ctx,t,project,ink,alpha);if(g.segments.length){ctx.beginPath();for(const s of g.segments){const a=project(s[0]),b=project(s[1]);ctx.moveTo(a[0],a[1]);ctx.lineTo(b[0],b[1]);}ctx.lineWidth=1;ctx.strokeStyle=ink;ctx.stroke();}}
 for(const l of c.lines){const a=project(c.world(...l.a)),b=project(c.world(...l.b));ctx.beginPath();ctx.moveTo(a[0],a[1]);ctx.lineTo(b[0],b[1]);ctx.lineWidth=1;ctx.strokeStyle=selected?color:l.color||color;ctx.stroke();}ctx.restore();
}
function svg(e,project,color,escape,mono=false){const c=e.composition,doc=K.Production.owners.get(e),out=[];if(!c)return '';
 const polygon=pts=>pts.map(p=>project(p).slice(0,2).join(',')).join(' ');
 if(e.background){const pad=(e.background.padding??.2)*e.height,q=[[-pad,-pad],[c.width+pad,-pad],[c.width+pad,c.height+pad],[-pad,c.height+pad]].map(p=>c.world(...p));out.push(`<polygon points="${polygon(q)}" fill="${e.background.useWindow?'#ffffff':e.background.color||'#ffffff'}" stroke="none"/>`);}
 for(const [index,run] of c.runs.entries()){const g=c.prepared?.[index]||runGeometry(e,c,run,doc),ink=mono?color:run.style.color||color;for(const t of g.texts)out.push(F.svgText(t,project,ink,escape));for(const s of g.segments)out.push(`<path d="M${project(s[0]).slice(0,2).join(',')}L${project(s[1]).slice(0,2).join(',')}" stroke="${ink}" fill="none"/>`);}
 for(const l of c.lines)out.push(`<path d="M${project(c.world(...l.a)).slice(0,2).join(',')}L${project(c.world(...l.b)).slice(0,2).join(',')}" stroke="${mono?color:l.color||color}" fill="none"/>`);return out.join('');
}
function fromDXF(r,{num,str,values,pt,normal}){
 const start=r.pairs.findIndex(p=>p[0]===100&&p[1].trim()==='AcDbMText'),rich={pairs:start<0?r.pairs:r.pairs.slice(start+1)};
 const n=normal(r),height=Math.max(1e-6,num(r,40,2.5)),out={type:'MTEXT',text:(values(r,3).join('')+(r.pairs.find(p=>p[0]===1)?.[1]||'')).replace(/\\\\|\\U\+([0-9a-f]{4})/gi,(s,n)=>n?String.fromCharCode(parseInt(n,16)):s),position:pt(r),height,width:Math.max(0,num(r,41)),attachment:num(r,71,1),spacingStyle:num(r,73,1),spacingFactor:num(r,44,1),normal:n,textStyle:plain(str(r,7,'STANDARD')),drawingDirection:num(r,72,1)};
 const direction=r.pairs.some(p=>p[0]===11)?pt(r,11):null;
 const lastDirection=r.pairs.findLastIndex(p=>p[0]===11),lastRotation=r.pairs.findLastIndex(p=>p[0]===50);if(direction&&lastDirection>lastRotation)out.direction=direction;else out.rotation=num(r,50)*Math.PI/180;
 if(num(r,90)&3)out.background={color:rich.pairs.some(p=>p[0]===421||p[0]===420)?'#'+(num(rich,421,num(rich,420))&0xffffff).toString(16).padStart(6,'0'):K.Exchange.aciColor(num(r,63,7)),padding:Math.max(0,num(r,45,1.5)-1),useWindow:!!(num(r,90)&2)};
 if(num(r,75)&&num(r,76)>1)out.columns={count:num(r,76),width:num(r,48,num(r,41)),gutter:num(r,49),height:num(r,46,0),autoHeight:!!num(r,79,1),reverse:!!num(r,78)};
 return out;
}
function dxfPairs(e){
 validate(e);if(e.columns?.count>1)throw Error('Export multi-column MTEXT with Explode to text first. Native saving preserves columns.');
 const ax=localAxes(e),sx=V.len(ax.x),sy=V.len(ax.y);if(Math.abs(sx-sy)>1e-7*Math.max(sx,sy)||Math.abs(V.dot(ax.x,ax.y))>1e-7*sx*sy)throw Error('Sheared/nonuniform MTEXT needs evaluated text export; keep the native project.');
 const pairs=[],put=(c,v)=>pairs.push([c,v]),point=(c,p)=>p.forEach((v,i)=>put(c+i*10,v));
 put(100,'AcDbMText');point(10,e.position);point(210,V.norm(V.cross(ax.x,ax.y)));point(11,V.norm(ax.x));put(40,e.height*sy);put(41,(e.width||0)*sx);put(71,e.attachment||1);put(72,1);put(7,e.textStyle||'STANDARD');put(73,e.spacingStyle||1);put(44,e.spacingFactor||1);
 let text=e.text;if(Math.abs(sy-1)>1e-12)text=text.replace(/\\H((?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?);/gi,(_,n)=>'\\H'+(Number(n)*sy)+';');
 text=text.replace(/\r?\n/g,'\\P').replace(/[^\x20-\x7e]/g,c=>'\\U+'+c.charCodeAt(0).toString(16).toUpperCase().padStart(4,'0'));
 while(text.length>=250){put(3,text.slice(0,250));text=text.slice(250);}put(1,text);
 if(e.background){put(90,e.background.useWindow?3:1);put(45,1+(e.background.padding??.2));put(63,7);if(!e.background.useWindow)put(421,parseInt((e.background.color||'#ffffff').slice(1),16));}
 return pairs;
}
function explode(doc,ids){const added=[];doc.transaction('Explode rich text',()=>{for(const id of ids){const e=doc.byId.get(id);if(!e||e.type!=='MTEXT'||!doc.editable(e))throw Error('Select editable MTEXT.');const c=compose(e,doc),base={layer:e.layer,color:e.color,linetype:e.linetype};
 for(const run of c.runs){const r=runGeometry(e,c,run,doc);for(const t of r.texts){const v={...base,...t,color:run.style.color||e.color};delete v.composition;added.push(doc.add(v).id);}for(const points of r.segments)added.push(doc.add('LINE',{...base,color:run.style.color||e.color,points}).id);}
 for(const l of c.lines)added.push(doc.add('LINE',{...base,color:l.color||e.color,points:[c.world(...l.a),c.world(...l.b)]}).id);doc.remove([id]);}doc.selection=new Set(added);});return added;}
const originalGeometry=G.geometry;G.geometry=function(e,tolerance){return e.type==='MTEXT'?geometry(e,K.Production.owners.get(e)):originalGeometry(e,tolerance);};
const transform=G.transform;G.transform=function(e,m){if(e.type!=='MTEXT')return transform(e,m);const a=localAxes(e),out=copy(e);out.position=M.point(m,e.position);out.textAxisX=M.point(m,a.x,0);out.textAxisY=M.point(m,a.y,0);out.normal=V.norm(V.cross(out.textAxisX,out.textAxisY));out.direction=V.norm(out.textAxisX);validate(out);return out;};
const entityValidator=K.Production.validateEntity;K.Production.validateEntity=function(e){entityValidator(e);validate(e);};
const fontReport=F.report;F.report=function(doc){const warnings=fontReport(doc);for(const {e}of K.Production.renderEntities(doc))if(e.type==='MTEXT')warnings.push(...geometry(e,doc).texts[0].composition.warnings);return [...new Set(warnings)];};
K.MText={limits:LIMITS,parse,plain,validate,compose,geometry,draw,svg,fromDXF,dxfPairs,explode,localAxes};
})(globalThis);
