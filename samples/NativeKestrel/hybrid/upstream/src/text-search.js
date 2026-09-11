/* Literal, Unicode-aware annotation search with format-preserving replacements. */
(function(root){
'use strict';
const K=root.Kestrel;
const richLiteral=value=>String(value).replace(/\r\n?/g,"\n").replace(/[\\{}%]/g,c=>c==="%"?"\\U+0025":"\\"+c).replace(/\n/g,"\\P");
const LIMITS=Object.freeze({query:256,replacement:10000,matches:10000,characters:5000000});
const plans=new WeakMap(),word=/[\p{L}\p{N}\p{M}_]/u;
const own=(o,k)=>Object.prototype.hasOwnProperty.call(o,k);
function checked(s,label,limit){if(typeof s!=='string'||s.length>limit||/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/.test(s))throw Error('Invalid '+label+'.');return s;}
function visibleRaw(raw,rich=false){
 checked(raw,'annotation text',rich?50000:100000);const tokens=[];let text='';
 const emit=(value,start,end,atomic=false)=>{if(!value)return;tokens.push({text:value,start,end,from:text.length,to:text.length+value.length,atomic});text+=value;};
 for(let i=0;i<raw.length;){const start=i,ch=raw[i++];
  if(rich&&(ch==='{'||ch==='}'))continue;
  if(rich&&ch==='%'&&raw[i]==='<'){const end=raw.indexOf('>%',i+1);if(end>=0){i=end+2;emit(raw.slice(start,i),start,i,true);continue;}}
  if(rich&&ch==='%'&&raw[i]==='%'&&/[dpc]/i.test(raw[i+1]||'')){i+=2;emit(({d:'°',p:'±',c:'⌀'})[raw[i-1].toLowerCase()],start,i);continue;}
  if(ch==='\\'&&i<raw.length){const cmd=raw[i++];
   if(rich&&cmd==='U'&&raw[i]==='+'&&/^[0-9a-f]{4}/i.test(raw.slice(i+1))){i+=5;emit(String.fromCharCode(parseInt(raw.slice(i-4,i),16)),start,i);continue;}
   if(rich){
    if(['\\','{','}'].includes(cmd)){emit(cmd,start,i);continue;}
    if(cmd==='~'){emit('\u00a0',start,i);continue;}
    if(cmd==='P'||cmd==='N'){emit('\n',start,i);continue;}
    if('LlOoKk'.includes(cmd))continue;
    if('HWTAQCcfFSp'.includes(cmd)){
     while(i<raw.length){const q=raw[i++];if(q===';')break;if(cmd==='S'&&q==='\\'&&i<raw.length)i++;}
     if(cmd==='S'){const fragment=K.MText.parse(raw.slice(start,i));const value=fragment.paragraphs.flatMap(p=>p.items.map(t=>t.kind==='stack'?t.top+(t.divider==='^'?' ':'/')+t.bottom:t.text||'')).join('\n');emit(value,start,i,true);}
     continue;
    }
   }
   if(rich){emit(raw.slice(start,i),start,i,true);continue;}emit('\\',start,start+1);i=start+1;continue;
  }
  if(ch==='\r'){if(raw[i]==='\n')i++;emit('\n',start,i);continue;}
  if(ch.charCodeAt(0)>=0xd800&&ch.charCodeAt(0)<=0xdbff&&raw.charCodeAt(i)>=0xdc00&&raw.charCodeAt(i)<=0xdfff)i++;
  emit(raw.slice(start,i),start,i);
 }
 return {text,tokens};
}
function span(tokens,start,end){let low=0,high=tokens.length;while(low<high){const middle=(low+high)>>1;if(tokens[middle].to<=start)low=middle+1;else high=middle;}const out=[];for(let i=low;i<tokens.length&&tokens[i].from<end;i++)out.push(tokens[i]);return out;}
const targetKey=t=>t.kind==='text'?'text':t.kind==='cell'?`cell:${t.row}:${t.column}`:`attribute:${t.tag}`;
function rawValue(doc,e,t){if(t.kind==='text')return e.text||'';if(t.kind==='cell')return e.cells[t.row][t.column];const block=doc.production.blocks.find(b=>b.id===e.block),a=block?.attributes?.find(a=>a.tag===t.tag);let value=own(e.attributes||{},t.tag)?e.attributes[t.tag]:a?.value||'';if(block?.dynamic&&!e.fieldBindings?.some(f=>f.target===targetKey(t))){const values=K.DynamicBlocks.resolve(block,e.parameters||{}).values;value=value.replace(/\$\{([A-Za-z][A-Za-z0-9_]*)\}/g,(_,name)=>own(values,name)?String(values[name]):'${'+name+'}');}return value;}
function assign(e,t,v){if(t.kind==='text')e.text=v;else if(t.kind==='cell')e.cells[t.row][t.column]=v;else{e.attributes=e.attributes||{};e.attributes[t.tag]=v;}}
function targets(doc,e){
 if(['TEXT','MTEXT','LEADER','DIMENSION'].includes(e.type))return [{kind:'text'}];
 if(e.type==='TABLE')return e.cells.flatMap((row,r)=>row.map((_,c)=>({kind:'cell',row:r,column:c})));
 if(e.type==='INSERT')return (doc.production.blocks.find(b=>b.id===e.block)?.attributes||[]).map(a=>({kind:'attribute',tag:a.tag}));return [];
}
function reason(doc,e,t){if(!doc.visible(e))return 'Hidden object';if(doc.layer(e).locked)return 'Locked layer';if(e.fieldBindings?.some(f=>f.target===targetKey(t)))return 'Associative field; use FIELD or FIELDREMOVE';if(e.type==='INSERT'&&doc.production.blocks.find(b=>b.id===e.block)?.dynamic)return 'Configurable attribute; edit its template';return '';}
function matchRanges(text,query,options){
 const re=new RegExp(query.replace(/[.*+?^${}()|[\]\\]/g,'\\$&'),'gu'+(options.caseSensitive?'':'i')),out=[];
 for(const m of text.matchAll(re)){
  if(options.wholeWord){const left=Array.from(text.slice(Math.max(0,m.index-2),m.index)).at(-1)||'',right=Array.from(text.slice(m.index+m[0].length,m.index+m[0].length+2))[0]||'';if(word.test(left)||word.test(right))continue;}
  out.push({start:m.index,end:m.index+m[0].length});if(out.length>LIMITS.matches)break;
 }return out;
}
function search(doc,query,options={}){
 checked(query,'search text',LIMITS.query);if(!query)throw Error('Enter text to find.');
 for(const k of Object.keys(options))if(!['caseSensitive','wholeWord','includeHidden','includeLocked','selectionOnly','types'].includes(k))throw Error('Unknown search setting.');
 for(const k of ['caseSensitive','wholeWord','includeHidden','includeLocked','selectionOnly'])if(options[k]!==undefined&&typeof options[k]!=='boolean')throw Error('Invalid search setting.');
 if(options.types!==undefined&&(!Array.isArray(options.types)||options.types.some(t=>!['TEXT','MTEXT','LEADER','DIMENSION','TABLE','INSERT'].includes(t))))throw Error('Invalid annotation type.');
 const rows=[],entries=new Map();let scanned=0,characters=0,truncated=false;
 outer:for(const e of doc.entities){
  if(!options.includeHidden&&!doc.visible(e)||options.includeLocked===false&&doc.layer(e).locked||options.selectionOnly&&!doc.selection.has(e.id)||options.types&&!options.types.includes(e.type))continue;
  for(const t of targets(doc,e)){
   const raw=rawValue(doc,e,t);characters+=raw.length;if(characters>LIMITS.characters){truncated=true;break outer;}scanned++;
   const rich=e.type==='MTEXT',visible=visibleRaw(raw,rich),ranges=matchRanges(visible.text,query,options),readOnly=reason(doc,e,t);
   if(!ranges.length)continue;const key=JSON.stringify([e.id,targetKey(t)]);entries.set(key,{entity:e.id,target:t,raw,visible,rich});
   for(const range of ranges){if(rows.length>=LIMITS.matches){truncated=true;break outer;}const slices=span(visible.tokens,range.start,range.end);
    const fragment=slices.some(t=>t.from<range.start||t.to>range.end);
    rows.push(Object.freeze({index:rows.length,entity:e.id,type:e.type,layer:doc.layer(e).name,target:Object.freeze({...t}),start:range.start,end:range.end,text:visible.text,match:visible.text.slice(range.start,range.end),reason:readOnly||(fragment?'Partial stacked fraction or encoded token; match the complete token':''),key}));
   }
  }
 }
 const plan=Object.freeze({query,revision:doc.revision,scanned,characters,truncated,rows:Object.freeze(rows)});plans.set(plan,{doc,entries});return plan;
}
function patch(entry,ranges,replacement){
 const value=entry.rich?richLiteral(replacement):replacement,edits=[];
 for(const r of ranges){const tokens=span(entry.visible.tokens,r.start,r.end);if(!tokens.length||tokens.some(t=>t.from<r.start||t.to>r.end))throw Error('Cannot replace part of an encoded text token.');let first=true;
  for(const token of tokens){edits.push({start:token.start,end:token.end,text:first?value:''});first=false;}
 }
 edits.sort((a,b)=>a.start-b.start);let previous=0;const chunks=[],limit=entry.rich?50000:100000;let length=entry.raw.length;for(const e of edits){if(e.start<previous)throw Error('Overlapping text replacement.');length+=e.text.length-e.end+e.start;if(length>limit+entry.raw.length)throw Error('Replacement exceeds annotation content limits.');chunks.push(entry.raw.slice(previous,e.start),e.text);previous=e.end;}if(length>limit)throw Error('Replacement exceeds annotation content limits.');chunks.push(entry.raw.slice(previous));const raw=chunks.join('');
 checked(raw,'result text',limit);if(entry.rich){K.MText.parse(raw);let at=0;const expected=[];for(const r of [...ranges].sort((a,b)=>a.start-b.start)){expected.push(entry.visible.text.slice(at,r.start),replacement);at=r.end;}expected.push(entry.visible.text.slice(at));if(visibleRaw(raw,true).text!==expected.join(''))throw Error('Replacement would reinterpret adjacent MTEXT controls. Include the surrounding literal text in the match.');}return raw;
}
function replace(doc,plan,replacement,indices=null){
 checked(replacement,'replacement text',LIMITS.replacement);const state=plans.get(plan);if(!state||state.doc!==doc)throw Error('Search these results in this drawing first.');if(doc.revision!==plan.revision)throw Error('Drawing changed; search again before replacing.');if(plan.truncated&&indices===null)throw Error('Results were truncated; narrow the search before Replace all.');
 if(indices!==null&&(!Array.isArray(indices)||indices.some(i=>!Number.isInteger(i)||i<0||i>=plan.rows.length)||new Set(indices).size!==indices.length))throw Error('Invalid result selection.');
 const chosen=indices===null?plan.rows:indices.map(i=>plan.rows[i]),groups=new Map();let skipped=0;
 for(const row of chosen){const entry=state.entries.get(row.key),e=doc.byId.get(row.entity);if(!e||rawValue(doc,e,row.target)!==entry.raw)throw Error('Text changed; search again before replacing.');if(row.reason||reason(doc,e,row.target)){skipped++;continue;}if(!groups.has(row.key))groups.set(row.key,[]);groups.get(row.key).push(row);}
 const updates=[];let replaced=0;for(const[key,rows]of groups){const entry=state.entries.get(key);updates.push({entry,value:patch(entry,rows,replacement)});replaced+=rows.length;}
 if(updates.length)doc.transaction('Find and replace annotation text',()=>{const objects=new Map();for(const{entry,value}of updates){if(!objects.has(entry.entity))objects.set(entry.entity,K.clone(doc.byId.get(entry.entity)));assign(objects.get(entry.entity),entry.target,value);}for(const[id,e]of objects)doc.replace(id,e);});
 return {replaced,skipped,targets:updates.length};
}
K.TextSearch={limits:LIMITS,visibleRaw,search,replace};
})(typeof window!=='undefined'?window:globalThis);
