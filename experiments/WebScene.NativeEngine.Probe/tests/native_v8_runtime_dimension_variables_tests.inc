void test_dimension_custom_property_recascade(webscene_engine* engine)
{
    resize(engine, 1280, 720, 1);
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<style id="sheet">:root{--size:200px;--color:#112233}.grid{display:grid;grid-template-columns:100px minmax(100px,1fr) var(--size)}.wide{width:var(--size)}.fixed{width:90px!important}.child{width:50%}</style><div class="grid"><div></div><div></div><div id="panel"><div class="child" id="child"></div></div></div><div id="box" class="wide"></div>';
          const root=document.documentElement, panel=document.getElementById('panel');
          const box=document.getElementById('box'), sheet=document.getElementById('sheet');
          const width=e=>Math.round(e.getBoundingClientRect().width);
          const check=(v,m)=>{if(!v)throw Error(m);};
          root.style.setProperty('--size','300px');
          check(width(panel)===300 && width(document.getElementById('child'))===150,'grid descendants');
          check(width(box)===300,'direct width');
          root.style.setProperty('--size','310px');
          box.className='wide fixed';
          check(width(box)===90 && width(panel)===310,'ancestor request absorbs descendant mutation');
          for(let i=0;i<160;i++)root.style.setProperty('--size',(200+i)+'px');
          check(width(panel)===359,'repeated variable writes');
          box.className='wide fixed';
          root.style.setProperty('--size','280px');
          check(width(box)===90 && width(panel)===280,'pending class and priority');
          root.style.removeProperty('--size');
          check(width(panel)===200,'removal restores stylesheet value');
          root.style.setProperty('--size','260px');
          root.style.setProperty('--size','');
          check(width(panel)===200,'empty setter restores stylesheet value');
          sheet.textContent += '.alias{--alias:var(--size);width:var(--alias)}';
          box.className='alias';
          root.style.setProperty('--size','270px');
          check(width(box)===270,'stylesheet alias fallback');
          root.style.setProperty('--font','17px');
          sheet.textContent += '.alias{font-size:var(--font)}';
          box.innerHTML='<span id="text">text</span>';
          root.style.setProperty('--font','23px');
          check(getComputedStyle(document.getElementById('text')).fontSize==='23px','inherited property fallback');
          sheet.textContent += '.alias{width:var(--missing,var(--size))}';
          root.style.setProperty('--size','290px');
          check(width(box)===290,'nested variable fallback');
          root.style.setProperty('--size','invalid');
          check(Number.isFinite(width(panel)),'invalid value remains safe');
          root.style.setProperty('--size','240px');
          check(width(panel)===240 && width(box)===240,'valid after invalid');
          sheet.textContent += '[style*="310px"] .alias{width:77px!important}';
          root.style.setProperty('--size','310px');
          check(width(box)===77,'style attribute selector fallback');
          root.style.removeProperty('--size');
          root.style.removeProperty('--font');
          document.body.innerHTML='<div id="ancestor"><div id="scope"><div id="leaf"></div></div></div>';
          const ancestor=document.getElementById('ancestor'), local=document.getElementById('scope'), leaf=document.getElementById('leaf');
          local.style.setProperty('--alias','var(--local-size)');
          leaf.style.width='var(--alias)';
          local.style.setProperty('--local-size','130px');
          check(width(leaf)===130,'inherited inline alias');
          local.style.setProperty('--local-size','160px');
          check(width(leaf)===160,'inherited inline alias update');
          leaf.style.width='var(--local-size)';
          local.style.setProperty('--local-size','180px');
          check(width(leaf)===180,'inline dimension');
          const shadow=local.attachShadow({mode:'open'});
          shadow.innerHTML='<style>.shadow{width:var(--local-size)}</style><div class="shadow" id="shadow"></div>';
          local.style.setProperty('--local-size','210px');
          check(width(shadow.querySelector('#shadow'))===210,'shadow fallback');
          return true;
        })()
    )JS", "dimension-custom-property-recascade.js");
    require(result == "true", "dimension custom property recascade failed: " + result);
}


void test_dimension_custom_property_inheritance(webscene_engine* engine)
{
    resize(engine, 1280, 720, 1);
    const auto result = evaluate(engine, R"JS(
        (() => {
          const failures=[];
          const root=document.documentElement;
          for(const inline of [false,true])for(const fallback of [false,true])for(const levels of [1,3])for(const operation of ['set','remove','empty']){
            root.style.removeProperty('--size');
            if(operation!=='set')root.style.setProperty('--size','200px');
            const declarations=fallback?'width:var(--missing,inherit);height:var(--missing,inherit)':'width:inherit;height:inherit';
            let children='';
            for(let i=0;i<levels;i++)children+='<div class="child" id="child'+i+'"'+(inline?' style="'+declarations+'"':'')+'>';
            children+='</div>'.repeat(levels);
            document.body.innerHTML='<style>:root{--size:100px}.parent{width:var(--size);height:var(--size)}'+(inline?'':'.child{'+declarations+'}')+'</style><div class="parent" id="parent">'+children+'</div>';
            const nodes=[document.getElementById('parent'),...Array.from({length:levels},(_,i)=>document.getElementById('child'+i))];
            const geometry=()=>nodes.map(n=>{const r=n.getBoundingClientRect();return [r.width,r.height]});
            const initial=operation==='set'?100:200;
            const before=geometry();
            if(before.some(r=>r[0]!==initial||r[1]!==initial))failures.push({inline,fallback,levels,operation,phase:'before',before});
            if(operation==='set')root.style.setProperty('--size','200px');
            else if(operation==='remove')root.style.removeProperty('--size');
            else root.style.setProperty('--size','');
            const expected=operation==='set'?200:100;
            const after=geometry();
            if(after.some(r=>r[0]!==expected||r[1]!==expected))failures.push({inline,fallback,levels,operation,phase:'after',after});
          }
          root.style.removeProperty('--size');
          document.body.innerHTML='<style>:root{--size:100px}.parent{width:var(--size);height:var(--size)}.child{width:77px!important}</style><div class="parent"><div class="child" id="priority" style="width:inherit;height:inherit"></div></div>';
          document.getElementById('priority').getBoundingClientRect();
          root.style.setProperty('--size','200px');
          const priority=document.getElementById('priority').getBoundingClientRect();
          if(priority.width!==77||priority.height!==200)failures.push({phase:'inline-priority',width:priority.width,height:priority.height});
          root.style.removeProperty('--size');
          return failures;
        })()
    )JS", "dimension-custom-property-inheritance.js");
    require(result == "[]", "dimension inheritance regression: " + result);
}
