// 编译器：积木树（新模型）→ HTML + CSS
// CSS 页：选择器积木的子积木为样式属性积木（直接编辑值），不再依赖右键
// HTML 页：标签积木的子积木可为标签属性积木（src/href/...）或子标签
const Compile = (() => {

  // key → css 属性名
  const STYLE_MAP = [
    ['width','width'], ['height','height'], ['color','color'], ['background','background-color'],
    ['fontSize','font-size'], ['margin','margin'], ['padding','padding'], ['opacity','opacity'],
    ['border','border'], ['radius','border-radius'], ['display','display'], ['position','position'],
  ];

  function sizeVal(s){
    if(!s || !s.value) return '';
    if(s.value === 'auto' || s.unit === 'auto') return 'auto';
    return s.value + (s.unit || 'px');
  }

  function stylesToLines(styles){
    const lines = [];
    if(!styles) return lines;
    STYLE_MAP.forEach(([key, css]) => {
      const v = styles[key];
      if(v == null || v === '') return;
      if(typeof v === 'object'){
        const sv = sizeVal(v);
        if(sv) lines.push(css + ': ' + sv + ';');
      } else {
        lines.push(css + ': ' + v + ';');
      }
    });
    return lines;
  }

  // 从子积木中收集样式属性积木 → CSS 行
  function propChildrenToLines(block){
    const lines = [];
    if(!block.children) return lines;
    block.children.forEach(c => {
      if(c.type !== 'property') return;
      const def = Blocks.get(c.def);
      if(!def || def.type !== 'property') return;
      const v = (c.text || '').trim();
      if(!v) return;
      const cssName = cssNameOf(def.propKey);
	  // Made by Nothing-Left2024
	  // Apache 2.0 Licence.
      if(!cssName) return;
      lines.push(cssName + ': ' + v + ';');
    });
    return lines;
  }

  function cssNameOf(propKey){
    const m = STYLE_MAP.find(([k]) => k === propKey);
    return m ? m[1] : null;
  }

  function esc(s){ return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }

  // 生成 HTML 标签字符串
  function genTagHtml(block, indent){
    indent = indent || '';
    const def = Blocks.get(block.def);
    if(!def || def.type !== 'tag') return '';
    const p = block.props || {};
    const attrs = ['data-hm-id="' + block.id + '"'];
    if(p.name) attrs.push('id="' + esc(p.name) + '"');
    if(p.group) attrs.push('class="' + esc(p.group) + '"');
    if(p.extra){ for(const k in p.extra) if(p.extra[k] !== '') attrs.push(k + '="' + esc(p.extra[k]) + '"'); }
    const attrStr = ' ' + attrs.join(' ');

    if(def.selfClosing){
      return indent + '<' + def.tag + attrStr + ' />';
    }
    let inner = esc(block.text || '');
    const kids = (block.children||[]).filter(c => {
      const cd = Blocks.get(c.def);
      return cd && cd.type === 'tag';
    });
    kids.forEach(c => { inner += '\n' + genTagHtml(c, indent + '  '); });
    const nl = inner ? '\n' : '';
    return indent + '<' + def.tag + attrStr + '>' + inner + nl + indent + '</' + def.tag + '>';
  }

  // HTML 页 CSS：每个标签积木一条规则（仅旧版 b.styles 内联样式，新样式请放 CSS 页）
  function genHtmlCss(list){
    let out = '';
    function walk(arr){
      arr.forEach(b => {
        const def = Blocks.get(b.def);
        if(def && def.type === 'tag'){
          const sel = '[data-hm-id="' + b.id + '"]';
          const lines = stylesToLines(b.styles);
          if(b.props && b.props.visible === false) lines.push('display: none;');
          if(lines.length) out += sel + ' {\n  ' + lines.join('\n  ') + '\n}\n';
          const hlines = stylesToLines(b.hoverStyles);
          if(hlines.length) out += sel + ':hover {\n  ' + hlines.join('\n  ') + '\n}\n';
          if(b.children) walk(b.children);
        }
      });
    }
    walk(list);
    return out;
  }

  // CSS 页规则：每个选择器一条，子积木为样式属性积木（按 isHover 分到 :hover 规则）
  function genCssPageRules(list){
    let out = '';
    list.forEach(b => {
      if(b.type !== 'selector') return;
      let sel = '';
      const v = (b.selectorValue || '').trim();
      if(b.selectorKind === 'tag') sel = v;
      else if(b.selectorKind === 'group') sel = '.' + v;
      else if(b.selectorKind === 'name') sel = '#' + v;
      if(!sel) return;
      // 兼容旧数据：b.styles 仍可生效
      const lines = stylesToLines(b.styles);
      const hlines = stylesToLines(b.hoverStyles);
      // 新模型：从子积木收集样式属性积木
      if(b.children){
        b.children.forEach(c => {
          if(c.type !== 'property') return;
          const def = Blocks.get(c.def);
          if(!def || def.type !== 'property') return;
          const val = (c.text || '').trim();
          if(!val) return;
          const cssName = cssNameOf(def.propKey);
          if(!cssName) return;
          if(c.isHover) hlines.push(cssName + ': ' + val + ';');
          else lines.push(cssName + ': ' + val + ';');
        });
      }
      if(lines.length) out += sel + ' {\n  ' + lines.join('\n  ') + '\n}\n';
      if(hlines.length) out += sel + ':hover {\n  ' + hlines.join('\n  ') + '\n}\n';
    });
    return out;
  }

  function compileFull(){
    const s = Store.getState();
    const css = genHtmlCss(s.html) + genCssPageRules(s.css);
    const js = compileJs();
    let body = '';
    s.html.forEach(b => { body += genTagHtml(b) + '\n'; });
    const scriptTag = js ? '\n<script>\n' + js + '\n</script>' : '';
    return '<!DOCTYPE html>\n<html lang="zh-CN">\n<head>\n<meta charset="UTF-8">\n<meta name="viewport" content="width=device-width, initial-scale=1.0">\n<title>'
      + esc(s.meta.name || '') + '</title>\n<style>\n' + css + '</style>\n</head>\n<body>\n' + body + '</body>' + scriptTag + '\n</html>';
  }

  function compileCssOnly(){
    const s = Store.getState();
    return genHtmlCss(s.html) + genCssPageRules(s.css);
  }

  // ===== JavaScript 编译 =====
  // 模板替换：${param} → block.params[param]（字符串值原样插入）
  // 注意：参数值默认当字符串字面量插入。模板里若需要引号包裹，已在 code 模板中写好。
  function fillTemplate(tpl, params){
    if(!tpl) return '';
    return tpl.replace(/\$\{(\w+)\}/g, (m, name) => {
      const v = params && params[name];
      if(v == null || v === '') return ''; // 空值替换为空
      return String(v);
    });
  }

  // 编译单个 JS 积木 → 代码行（带缩进）
  function genJsBlock(block, indent){
    indent = indent || '';
    const def = Blocks.get(block.def);
    if(!def || def.type !== 'js') return '';
    const code = fillTemplate(def.code, block.params);
    let out = indent + code + '\n';
    // 有 close 的代码块：递归子积木后追加 close
    if(def.close){
      (block.children || []).forEach(c => { out += genJsBlock(c, indent + '  '); });
      // close 同样需要模板替换（如 }, ${ms}); 中的 ${ms}）
      const closeCode = fillTemplate(def.close, block.params);
      // close 可能含多行（如 else 分支），按行缩进
      const closeLines = closeCode.split('\n');
      closeLines.forEach((ln, i) => {
        out += indent + ln + (i < closeLines.length - 1 ? '\n' : '');
      });
      out += '\n';
    }
    return out;
  }

  // 编译所有 JS 积木 → 完整 JS 代码
  function compileJs(){
    const s = Store.getState();
    let out = '';
    (s.js || []).forEach(b => { out += genJsBlock(b, ''); });
    return out.trim();
  }

  function compileJsOnly(){
    return compileJs();
  }

  function compileHtmlBody(){
    const s = Store.getState();
    let body = '';
    s.html.forEach(b => { body += genTagHtml(b) + '\n'; });
    return body;
  }

  return { compileFull, compileCssOnly, compileJs, compileJsOnly, compileHtmlBody, genHtmlCss, genCssPageRules };
})();
