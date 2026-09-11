// Build-time migration of HTML-producing expressions. Original sources remain
// browser inputs; the output calls native constructors with structured values.
import ts from '../../../tooling/webscene/node_modules/typescript/lib/typescript.js';
import fs from 'node:fs';
import path from 'node:path';
const [input, output] = process.argv.slice(2);
if (!input || !output) throw Error('Usage: compile-js-templates.mjs input-directory output-directory');
fs.mkdirSync(output, {recursive:true});
const catalog=[], attributeSets=new Set(['selected','disabled','checked','multiple','required','readonly','hidden']);
function normalizeInspector(source) {
  source=source.replace('section = title => `<div class="property-section"><div class="property-section-title">${title}</div>`',
    'section = (title, content) => `<div class="property-section"><div class="property-section-title">${title}</div>${content}</div>`');
  for (const title of ['GENERAL','GEOMETRY']) {
    const begin=source.indexOf(`html += section('${title}');`);
    const end=source.indexOf("html += '</div>';",begin);
    if(begin<0||end<0)throw Error('Inspector source changed; review template grouping');
    const body=source.slice(begin+`html += section('${title}');`.length,end).replaceAll('html +=','sectionBody +=');
    source=source.slice(0,begin)+(title==='GENERAL'?"let sectionBody = '';":"sectionBody = '';")+body+
      `html += section('${title}', sectionBody);`+source.slice(end+"html += '</div>';".length);
  }
  source=source.replace("section('DRAFTING SETTINGS') + input(","section('DRAFTING SETTINGS', input(")
    .replace("read('Grid display', 'Adaptive') + '</div>';","read('Grid display', 'Adaptive'));");
  return source;
}
const html = value => /<\/?[a-zA-Z][\w:-]*(?:\s[^<>]*)?\/?>/.test(value);
const nativeFunctions={
  'fonts.js':['svgText','Fonts'], 'mtext.js':['svg','MText'],
  'exchange.js':['writeSVG','Exchange'], 'production.js':['layoutSVG','Production']
};
function nativeSvgSource(source, filename) {
  const [name,owner]=nativeFunctions[filename];
  const parsed=ts.createSourceFile(filename,source,ts.ScriptTarget.Latest,true,ts.ScriptKind.JS);
  let found;
  const find=node=>{if(ts.isFunctionDeclaration(node)&&node.name?.text===name)found=node;else ts.forEachChild(node,find);};
  find(parsed);if(!found)throw Error(`Missing ${name} SVG producer`);
  let body=found.getText(parsed).replace(`function ${name}(`,`function ${name}Native(`)
    .replaceAll('K.MText.svg(','K.MText.svgNative(').replaceAll('K.Fonts.svgText(','K.Fonts.svgTextNative(').replaceAll('F.svgText(','F.svgTextNative(');
  if(name==='writeSVG'||name==='layoutSVG') {
    const list=name==='writeSVG'?'svg':'out';
    const root=new RegExp(`const ${list} = \\[(`+'`[\\s\\S]*?`'+`)\\];`);
    body=body.replace(root,(_,markup)=>`const ${list}=[]; const wrap = body => ${markup.slice(0,-1)}\${body}</svg>\`;`);
    const group=new RegExp(`${list}\\.push\\((`+'`(?:<g |<defs>)[\\s\\S]*?`'+`)\\);`);
    const match=group.exec(body);if(!match)throw Error('SVG group source changed');
    const begin=match.index+match[0].length,end=body.indexOf(`${list}.push('</g>');`,begin);
    if(end<0)throw Error('SVG group terminator missing');
    const middle=body.slice(begin,end).replaceAll(`${list}.push(`,'group.push(');
    body=body.slice(0,match.index)+`const group=[]; const wrapGroup = body => ${match[1].slice(0,-1)}\${body}</g>\`;`+
      middle+`${list}.push(wrapGroup(group.join('')));`+body.slice(end+`${list}.push('</g>');`.length);
    body=body.replace("svg.push('</svg>');",'').replace('</text></svg>`','</text>`');
    body=body.replace(/return (svg|out)\.join\(([^;]+)\);/, 'return wrap($1.join($2));');
  }
  return {body,assignment:`K.${owner}.${name}Native=${name}Native;`};
}
for (const filename of fs.readdirSync(input).filter(n=>n.endsWith('.js')).sort()) {
  let source=fs.readFileSync(path.join(input,filename),'utf8');
  if(filename==='app.js')source=normalizeInspector(source);
  const original=source;
  let nativeSvg;
  if(nativeFunctions[filename]){nativeSvg=nativeSvgSource(source,filename);source=nativeSvg.body;}
  if(filename==='mtext-ui.js')source=source.replaceAll('T.svg(','T.svgNative(');
  if(filename==='app.js')source=source.replace('const svg = K.Exchange.writeSVG(this.doc, { width: 1680','const svg = K.Exchange.writeSVGNative(this.doc, { width: 1680');
  if(filename==='production-ui.js')source=source.replace('svg=P.layoutSVG(doc,l)','svg=f.mode===\'svg\'?P.layoutSVG(doc,l):P.layoutSVGNative(doc,l)');
  if (!(nativeSvg||filename==='app.js'||filename==='ui.js'||filename.endsWith('-ui.js'))) {
    fs.writeFileSync(path.join(output,filename),source); continue;
  }
  const file=ts.createSourceFile(filename,source,ts.ScriptTarget.Latest,true,ts.ScriptKind.JS);
  const F=ts.factory;
  const call=(name,args)=>F.createCallExpression(F.createPropertyAccessExpression(F.createIdentifier('__nativeUI'),name),undefined,args);
  const transform=context=>{
    const template=(pieces,node)=>{
      if (!html(pieces.filter(p=>typeof p==='string').join(''))) return null;
      const values=[];
      let markup='';
      for(const piece of pieces) {
        if(typeof piece==='string') { markup+=piece; continue; }
        const marker=`__ws_slot_${values.push(ts.visitNode(piece,visit))-1}__`;
        const raw=[...markup.matchAll(/<(textarea|title|style|script)\b[^>]*>/g)].at(-1);
        const inRaw=raw&&markup.lastIndexOf('</'+raw[1])<raw.index;
        markup+=inRaw||markup.lastIndexOf('<')>markup.lastIndexOf('>')?marker:`<!--${marker}-->`;
      }
      const id='t'+catalog.length;
      const loc=file.getLineAndCharacterOfPosition(node.getStart(file));
      catalog.push({id,markup,file:filename,line:loc.line+1,column:loc.character+1});
      return call('template',[F.createStringLiteral(id),F.createArrayLiteralExpression(values)]);
    };
    const flatten=node=>{
      if(ts.isStringLiteralLike(node)) return [node.text];
      if(ts.isTemplateExpression(node)) return [node.head.text,...node.templateSpans.flatMap(s=>[s.expression,s.literal.text])];
      if(ts.isBinaryExpression(node)&&node.operatorToken.kind===ts.SyntaxKind.PlusToken) return [...flatten(node.left),...flatten(node.right)];
      return [node];
    };
    const visit=node=>{
      if(ts.isCallExpression(node)&&ts.isPropertyAccessExpression(node.expression)
          &&ts.isPropertyAccessExpression(node.expression.expression)&&node.expression.expression.name.text==='document') {
        const method=node.expression.name.text;
        if(method==='write')return call('setDocument',[ts.visitNode(node.expression.expression,visit),...node.arguments.map(a=>ts.visitNode(a,visit))]);
        if(method==='open'||method==='close')return F.createVoidZero();
      }
      if(ts.isStringLiteralLike(node) && node.text.includes('=') && /^(?:[\w-]+(?:=(?:"[^"]*"|'[^']*'))?\s*)+$/.test(node.text.trim())) attributeSets.add(node.text.trim());
      if(ts.isBinaryExpression(node)&&node.operatorToken.kind===ts.SyntaxKind.EqualsToken
          &&ts.isPropertyAccessExpression(node.left)&&node.left.name.text==='innerHTML')
        return call('set',[ts.visitNode(node.left.expression,visit),ts.visitNode(node.right,visit)]);
      if(ts.isTemplateExpression(node)||ts.isStringLiteralLike(node)||
          (ts.isBinaryExpression(node)&&node.operatorToken.kind===ts.SyntaxKind.PlusToken)) {
        const result=template(flatten(node),node); if(result)return result;
      }
      if(ts.isBinaryExpression(node)&&node.operatorToken.kind===ts.SyntaxKind.PlusToken)
        return call('add',[ts.visitNode(node.left,visit),ts.visitNode(node.right,visit)]);
      if(ts.isBinaryExpression(node)&&node.operatorToken.kind===ts.SyntaxKind.PlusEqualsToken&&ts.isIdentifier(node.left))
        return F.createAssignment(node.left,call('add',[node.left,ts.visitNode(node.right,visit)]));
      if(ts.isCallExpression(node)&&ts.isPropertyAccessExpression(node.expression)&&node.expression.name.text==='join')
        return call('join',[ts.visitNode(node.expression.expression,visit),...node.arguments.map(a=>ts.visitNode(a,visit))]);
      return ts.visitEachChild(node,visit,context);
    };
    return root=>ts.visitNode(root,visit);
  };
  const result=ts.transform(file,[transform]);
  let emitted=ts.createPrinter().printFile(result.transformed[0]);
  if(nativeSvg){const at=original.lastIndexOf('})(');if(at<0)throw Error('SVG module closure changed');emitted=original.slice(0,at)+emitted+'\n'+nativeSvg.assignment+'\n'+original.slice(at);}
  fs.writeFileSync(path.join(output,filename),emitted);
  result.dispose();
}
// JSON is an audit artifact; markup is consumed only by the build-time compiler.
fs.writeFileSync(path.join(output,'templates.json'),JSON.stringify(catalog,null,2));
const escape=v=>v.replaceAll('&','&amp;').replaceAll('\"','&quot;').replaceAll('<','&lt;');
fs.writeFileSync(path.join(output,'template-attributes.html'),[...attributeSets].map(v=>`<template data-ws-attributes="${escape(v)}"><input ${v}></template>`).join('\n'));
fs.writeFileSync(path.join(output,'templates.html'),catalog.map(t=>`<template id="${t.id}">${/^<(?:path|polygon|polyline|text|g|defs|rect|circle|ellipse|line|tspan)\b/.test(t.markup)?`<svg data-ws-wrapper="true">${t.markup}</svg>`:t.markup}</template>`).join('\n'));
console.log(`${catalog.length} HTML producers extracted`);
