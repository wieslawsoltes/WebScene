// Native template values carry structure, never an HTML string to parse.
(function(root) {
  'use strict';
  const brand=Symbol('compiled-template');
  const recipe=(id,values)=>({[brand]:true,id,values,toString(){throw Error('Compiled template coerced to a string; migrate this HTML producer');}});
  const isRecipe=value=>Boolean(value&&value[brand]);
  const decode=value=>String(value).replace(/&(?:amp|lt|gt|quot|apos|#39|#x27);/g,
    entity=>({'&amp;':'&','&lt;':'<','&gt;':'>','&quot;':'"','&apos;':"'",'&#39;':"'",'&#x27;':"'"}[entity]));
  function materialize(value) {
    if (value && typeof value.nodeType==='number') return value;
    if (!isRecipe(value)) {
      if (/<\/?[a-zA-Z][\w:-]*(?:\s[^<>]*)?\/?>/.test(String(value)))
        throw Error('Uncompiled HTML producer reached native templates');
      return document.createTextNode(decode(value));
    }
    if (value.id===null) {
      const fragment=document.createDocumentFragment();
      for(const part of value.values) fragment.appendChild(materialize(part));
      return fragment;
    }
    return document.createCompiledTemplateParts(value.id,value.values.map(part=> {
      if(isRecipe(part))return materialize(part);
      if (/<\/?[a-zA-Z][\w:-]*(?:\s[^<>]*)?\/?>/.test(String(part)))
        throw Error('Uncompiled HTML producer used as a template argument');
      return decode(part);
    }));
  }
  root.__nativeUI={
    template:recipe,
    add(a,b){return isRecipe(a)||isRecipe(b)?recipe(null,[a,b]):a+b;},
    join(values,separator=',') {
      if(!isRecipe(separator)&&!Array.prototype.some.call(values,isRecipe))return values.join(separator);
      const parts=[];
      for(let i=0;i<values.length;i++) { if(i)parts.push(separator);parts.push(values[i]??''); }
      return recipe(null,parts);
    },
    set(element,value){const node=materialize(value);element.replaceChildren(node);return value;},
    setDocument(doc,value){return this.set(doc.body,value);},
    materialize
  };
})(globalThis);
