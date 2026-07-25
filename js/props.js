// 右键上下文菜单：基础属性 / 标签属性 / 样式 / 悬浮样式 / 操作
const Props = (() => {
  let current = null; // { id, page }

  const backdrop = () => document.getElementById('ctxBackdrop');
  const menu = () => document.getElementById('ctxMenu');

  function open(block, page, x, y){
    current = { id: block.id, page };
    // 打开时快照一次，作为本次编辑的撤销点
    Store.snapshot();
    render();
    const m = menu();
    m.style.left = '0px';
    m.style.top = '0px';
    backdrop().classList.remove('hidden');
    // 定位并约束到视口
    const rect = m.getBoundingClientRect();
    const vw = window.innerWidth, vh = window.innerHeight;
    let left = Math.min(x, vw - rect.width - 8);
    let top = Math.min(y, vh - rect.height - 8);
    left = Math.max(8, left);
    top = Math.max(8, top);
    m.style.left = left + 'px';
    m.style.top = top + 'px';
  }
  function close(){
    backdrop().classList.add('hidden');
    current = null;
  }
  function getBlock(){
    if(!current) return null;
    const r = Store.findBlockAny(current.id);
    return r ? r.block : null;
  }

  // 工作区空白处右键：显示通用菜单（粘贴/撤销/重做/清空）
  function openWorkspace(page, x, y){
    current = { id: null, page };
    const m = menu();
    m.innerHTML = '';
    const pageNames = { html: 'HTML 编辑', css: 'CSS 编辑', js: 'JS 编辑' };
    const pageIcons = { html: 'container', css: 'style', js: 'event' };
    // 标题栏
    const def = { label: pageNames[page] || '工作区', icon: pageIcons[page] || 'container', color: 'var(--accent)' };
    const headerEl = document.createElement('div');
    headerEl.className = 'ctx-header';
    const icon = document.createElement('span');
    icon.className = 'ctx-header-icon';
    icon.style.background = 'var(--accent)';
    icon.innerHTML = Icons.get(def.icon);
    const name = document.createElement('span');
    name.className = 'ctx-header-name';
    name.textContent = def.label;
    headerEl.appendChild(icon);
    headerEl.appendChild(name);
    m.appendChild(headerEl);
    // 内容区：提示卡片
    const body = document.createElement('div');
    body.className = 'ctx-body';
    const tip = document.createElement('div');
    tip.className = 'ctx-tip-card';
    tip.textContent = '工作区操作';
    body.appendChild(tip);
    m.appendChild(body);
    // 底部操作栏
    const ops = document.createElement('div');
    ops.className = 'ctx-footer';
    const hasClipboard = clipboardAvailable();
    ops.appendChild(actBtn('粘贴', () => { pasteFromMenu(page); close(); }, false, !hasClipboard));
    ops.appendChild(actBtn('撤销', () => { Store.undo(); close(); }, false, !Store.canUndo()));
    ops.appendChild(actBtn('重做', () => { Store.redo(); close(); }, false, !Store.canRedo()));
    ops.appendChild(actBtn('清空', () => { Store.clearPage(page); close(); }, true));
    m.appendChild(ops);
    // 定位
    m.style.left = '0px';
    m.style.top = '0px';
    backdrop().classList.remove('hidden');
    const rect = m.getBoundingClientRect();
    const vw = window.innerWidth, vh = window.innerHeight;
    let left = Math.min(x, vw - rect.width - 8);
    let top = Math.min(y, vh - rect.height - 8);
    left = Math.max(8, left);
    top = Math.max(8, top);
    m.style.left = left + 'px';
    m.style.top = top + 'px';
  }

  // 粘贴（从 app.js 的 clipboard 变量读取，通过自定义事件协调）
  function pasteFromMenu(page){
    document.dispatchEvent(new CustomEvent('htmlmaker:paste', { detail: { page } }));
  }
  function clipboardAvailable(){
    // 询问 app.js 是否有剪贴板内容
    let has = false;
    document.dispatchEvent(new CustomEvent('htmlmaker:clipboard-query', { detail: { result: v => has = v } }));
    return has;
  }

  function render(){
    const block = getBlock();
    const m = menu();
    m.innerHTML = '';
    if(!block){ close(); return; }
    const def = Blocks.get(block.def);

    // 顶部标题栏：色块图标 + 积木名称
    m.appendChild(header(def));

    // 内容区
    const body = document.createElement('div');
    body.className = 'ctx-body';

    // 属性积木：仅提供提示，操作在底部
    if(def.type === 'property'){
      body.appendChild(tipCard('属性积木的值在积木内直接输入，可通过底部操作复制或删除'));
    } else if(def.type === 'tag'){
      body.appendChild(section('基础属性', () => {
        const rows = [];
        rows.push(row('名称(id)', input(block.props.name||'', v => Store.setProp(block.id,'name',v,current.page), '如 header1')));
        rows.push(row('组(class)', input(block.props.group||'', v => Store.setProp(block.id,'group',v,current.page), '如 card')));
        rows.push(row('可见性', select(['可见','隐藏'], block.props.visible===false?'隐藏':'可见', v => Store.setProp(block.id,'visible', v!=='隐藏', current.page))));
        return rows;
      }));
      if(def.extraAttrs && def.extraAttrs.length){
        body.appendChild(section('标签属性', () => {
          return def.extraAttrs.map(a => {
            const cur = (block.props.extra||{})[a.name] || '';
            if(a.type === 'select'){
              return row(a.label, select(a.options, cur, v => Store.setExtra(block.id, a.name, v, current.page)));
            }
            return row(a.label, input(cur, v => Store.setExtra(block.id, a.name, v, current.page), a.label));
          });
        }));
      }
    } else if(def.type === 'selector'){
      body.appendChild(tipCard('样式编辑：从左侧拖入「样式属性」积木到此处'));
    } else if(def.type === 'js'){
      body.appendChild(tipCard(def.close
        ? '代码块：可拖入其它 JS 积木作为子语句（参数在积木内输入）'
        : '单行语句：参数在积木内输入'));
    }
    m.appendChild(body);

    // 底部操作栏
    const ops = document.createElement('div');
    ops.className = 'ctx-footer';
    ops.appendChild(actBtn('复制', () => { copyBlock(); }));
    ops.appendChild(actBtn('原地复制', () => { const nid = Store.duplicateBlock(block.id, current.page); if(nid) Engine.setSelected(nid); close(); }));
    ops.appendChild(actBtn('删除', () => { Store.deleteBlock(block.id, current.page); close(); }, true));
    m.appendChild(ops);
  }

  // 顶部标题栏：色块图标 + 积木名称
  function header(def){
    const h = document.createElement('div');
    h.className = 'ctx-header';
    const icon = document.createElement('span');
    icon.className = 'ctx-header-icon';
    icon.style.background = Blocks.colorOf(def);
    if(def.icon) icon.innerHTML = Icons.get(def.icon);
    const name = document.createElement('span');
    name.className = 'ctx-header-name';
    name.textContent = def.label;
    h.appendChild(icon);
    h.appendChild(name);
    return h;
  }

  // 提示卡片（用于 selector / js 等无表单的菜单）
  function tipCard(text){
    const c = document.createElement('div');
    c.className = 'ctx-tip-card';
    c.textContent = text;
    return c;
  }

  function styleRows(block, isHover){
    // 样式编辑已迁移至 CSS 页样式属性积木，此处保留空函数以兼容旧调用
    return [];
  }

  function copyBlock(){
    const b = getBlock();
    if(!b) return;
    // 复制到剪贴板（由 app.js 监听 clipboard 变量）—— 这里用自定义事件
    document.dispatchEvent(new CustomEvent('htmlmaker:copy', { detail: { id: b.id, page: current.page } }));
    close();
  }

  // ---- 构造元素 ----
  function section(title, fill){
    const s = document.createElement('div');
    s.className = 'ctx-section';
    const t = document.createElement('div');
    t.className = 'ctx-title';
    t.textContent = title;
    s.appendChild(t);
    const body = document.createElement('div');
    body.className = 'ctx-section-body';
    s.appendChild(body);
    const nodes = fill();
	// Made by Nothing-Left2024 Apache 2.0 Licence
    if(Array.isArray(nodes)) nodes.forEach(n => body.appendChild(n));
    return s;
  }
  function row(label, control){
    const r = document.createElement('div');
    r.className = 'ctx-row';
    const l = document.createElement('label');
    l.textContent = label;
    r.appendChild(l);
    r.appendChild(control);
    // 阻止点击行内控件时关闭
    r.addEventListener('pointerdown', e => e.stopPropagation());
    return r;
  }
  function input(value, onInput, ph){
    const i = document.createElement('input');
    i.type = 'text';
    i.value = value;
    if(ph) i.placeholder = ph;
    i.addEventListener('input', () => onInput(i.value));
    return i;
  }
  function select(options, val, onChange){
    const s = document.createElement('select');
    options.forEach(o => { const op = document.createElement('option'); op.value = o; op.textContent = o || '—'; if(o === val) op.selected = true; s.appendChild(op); });
    s.addEventListener('change', () => onChange(s.value));
    return s;
  }
  function colorInput(value, onInput){
    const i = document.createElement('input');
    i.type = 'color';
    i.value = value || '#000000';
    i.style.width = '40px';
    i.style.height = '26px';
    i.style.padding = '0';
    i.addEventListener('input', () => onInput(i.value));
    return i;
  }
  function sizeInput(cur, onChange){
    const wrap = document.createElement('div');
    wrap.style.display = 'flex';
    wrap.style.gap = '4px';
    wrap.style.alignItems = 'center';
    const v = document.createElement('input');
    v.type = 'text';
    v.value = (cur && cur.value) || '';
    v.style.width = '50px';
    v.placeholder = '值';
    const unit = document.createElement('select');
    ['px','%','vw','vh','em','rem','auto'].forEach(o => { const op = document.createElement('option'); op.value = o; op.textContent = o; if(cur && cur.unit === o) op.selected = true; unit.appendChild(op); });
    const emit = () => onChange({ value: v.value, unit: unit.value });
    v.addEventListener('input', emit);
    unit.addEventListener('change', emit);
    wrap.appendChild(v);
    wrap.appendChild(unit);
    return wrap;
  }
  function actBtn(label, onClick, danger){
    const b = document.createElement('button');
    b.className = 'ctx-act' + (danger ? ' danger' : '');
    b.textContent = label;
    b.addEventListener('pointerdown', e => e.stopPropagation());
    b.addEventListener('click', e => { e.stopPropagation(); onClick(); });
    return b;
  }

  function init(){
    backdrop().addEventListener('pointerdown', e => {
      if(e.target === backdrop()) close();
    });
    // 菜单内点击不冒泡到 backdrop
    menu().addEventListener('pointerdown', e => e.stopPropagation());
  }

  return { open, openWorkspace, close, init };
})();
