// 积木引擎：渲染调色板、工作区、拖拽/吸附/嵌套、内联文本/选择器输入、右键菜单
const Engine = (() => {
  let drag = null;
  let selectedId = null;
  let currentPage = 'html';

  function el(tag, cls, html){
    const e = document.createElement(tag);
    if(cls) e.className = cls;
    if(html != null) e.innerHTML = html;
    return e;
  }
  function colorOf(def){ return Blocks.colorOf(def); }
  function defOf(block){ return Blocks.get(block.def); }

  // 自定义提示框（不使用 alert）
  function showToast(msg, type){
    type = type || 'warning';
    const container = document.getElementById('toastContainer');
    if(!container) return;
    const toast = el('div', 'toast toast-' + type);
    toast.innerHTML = '<span class="toast-icon">!</span><span class="toast-msg"></span>';
    toast.querySelector('.toast-msg').textContent = msg;
    container.appendChild(toast);
    setTimeout(() => {
      toast.classList.add('toast-out');
      setTimeout(() => toast.remove(), 300);
    }, 3000);
  }

  // 兼容性检查
  // - 属性积木放入标签：检查标签是否支持该属性
  // - JS 积木放入 JS 代码块：仅当父积木 close 非空（即代码块）时可嵌套
  function checkCompatibility(def, target, page){
    if(def.type === 'property'){
      // 样式属性积木：只能放在选择器内
      if(!target || target.parentId == null) return { ok: false, reason: '样式属性积木必须放在选择器内' };
      const parent = findBlockById(target.parentId, page);
      if(!parent) return { ok: false, reason: '找不到父积木' };
      const parentDef = defOf(parent);
      if(!parentDef || parentDef.type !== 'selector') return { ok: false, reason: '样式属性积木必须放在选择器内' };
      return { ok: true };
    }
    if(def.type === 'js'){
      // JS 事件积木只能作为顶级
      if(def.jsType === 'event' && target && target.parentId != null){
        return { ok: false, reason: '事件积木必须放在顶层（不能嵌套）' };
      }
      // 嵌入子积木时，父积木必须是 JS 代码块（close 非空）
      if(target && target.parentId != null){
        const parent = findBlockById(target.parentId, page);
        if(!parent) return { ok: false, reason: '找不到父积木' };
        const parentDef = defOf(parent);
        if(!parentDef || parentDef.type !== 'js') return { ok: false, reason: 'JS 积木必须放在 JS 代码块内' };
        if(!parentDef.close || parentDef.canHaveChildren === false){
          return { ok: false, reason: '该积木为单行语句，不能包含子积木' };
        }
      }
      return { ok: true };
    }
    return { ok: true };
  }

  function sizeShort(s){
    if(!s || !s.value) return '';
    if(s.value === 'auto' || s.unit === 'auto') return 'auto';
    return s.value + (s.unit || 'px');
  }

  // 计算积木标题右侧的属性回显（含样式）
  function propsSummary(block){
    const parts = [];
    const p = block.props || {};
    if(p.name) parts.push('名称:' + p.name);
    if(p.group) parts.push('组:' + p.group);
    if(p.visible === false) parts.push('隐藏');
    const st = block.styles || {};
    if(st.width && st.width.value) parts.push('宽:' + sizeShort(st.width));
    if(st.height && st.height.value) parts.push('高:' + sizeShort(st.height));
    if(st.color) parts.push('颜色:' + st.color);
    if(st.background) parts.push('背景:' + st.background);
    if(st.fontSize && st.fontSize.value) parts.push('字号:' + sizeShort(st.fontSize));
    if(st.margin && st.margin.value) parts.push('边距:' + sizeShort(st.margin));
    if(st.padding && st.padding.value) parts.push('内边距:' + sizeShort(st.padding));
    if(st.opacity) parts.push('透明度:' + st.opacity);
    if(st.border) parts.push('边框:' + st.border);
    if(st.radius && st.radius.value) parts.push('圆角:' + sizeShort(st.radius));
    if(st.display) parts.push('显示:' + st.display);
    if(st.position) parts.push('定位:' + st.position);
    if(p.extra){ for(const k in p.extra){ if(p.extra[k] !== '' && p.extra[k] != null) parts.push(k + ':' + p.extra[k]); } }
    return parts;
  }

  // ---------- 调色板 ----------
  let htmlFilter = '';
  let cssFilter = '';
  let jsFilter = '';
  function getFilter(page){ return page === 'css' ? cssFilter : (page === 'js' ? jsFilter : htmlFilter); }
  function setFilter(page, v){ if(page === 'css') cssFilter = v; else if(page === 'js') jsFilter = v; else htmlFilter = v; }
  function renderPalette(container, categories, page){
    const filter = getFilter(page).toLowerCase().trim();
    container.innerHTML = '';
    const search = el('input', 'pal-search');
    search.type = 'text';
    search.placeholder = '搜索积木…（拖入工作区添加）';
    search.value = getFilter(page);
    search.addEventListener('pointerdown', e => e.stopPropagation());
    search.addEventListener('input', () => {
      setFilter(page, search.value);
      renderPalette(container, categories, page);
      const len = search.value.length; search.setSelectionRange(len, len);
      search.focus();
    });
    container.appendChild(search);

    categories.forEach(cat => {
      const items = filter ? cat.items.filter(def =>
        def.label.toLowerCase().includes(filter) || def.id.toLowerCase().includes(filter)
      ) : cat.items;
      if(!items.length) return;
      const catEl = el('div', 'pal-cat');
      catEl.id = 'cat-' + page + '-' + cat.id;
      catEl.textContent = cat.label;
      container.appendChild(catEl);
      items.forEach(def => {
        const item = el('div', 'pal-item');
        item.style.background = Blocks.colorMap[cat.color] || 'var(--accent)';
        item.innerHTML = Icons.get(def.icon) + '<span>' + def.label + '</span>';
        item.dataset.defId = def.id;
        item.dataset.page = page;
        // 只支持拖拽添加，单击不再添加
        item.addEventListener('pointerdown', e => startPaletteDrag(e, def, page));
        container.appendChild(item);
      });
    });
    if(filter && !container.querySelector('.pal-item')){
      const tip = el('div', 'pal-cat');
      tip.style.textAlign = 'center';
      tip.style.padding = '14px';
      tip.textContent = '无匹配积木';
      container.appendChild(tip);
    }
    // 同步渲染左侧分类竖栏
    renderCatRail(categories, page);
  }

  // 渲染左侧分类竖栏（点击跳转到对应分类标题）
  function renderCatRail(categories, page){
    const rail = document.querySelector('.cat-rail[data-rail-page="' + page + '"]');
    if(!rail) return;
    rail.innerHTML = '';
    const filter = getFilter(page).toLowerCase().trim();
    categories.forEach(cat => {
      // 搜索过滤：若该分类无匹配积木，则隐藏对应竖栏按钮
      const matches = filter
        ? cat.items.filter(def => def.label.toLowerCase().includes(filter) || def.id.toLowerCase().includes(filter))
        : cat.items;
      if(!matches.length) return;
      const btn = el('button', 'cat-rail-btn');
      btn.title = cat.label;                          // 原生 title 作为无障碍兜底
      btn.dataset.catId = cat.id;
      btn.dataset.page = page;
      btn.dataset.tipText = cat.label;                // 自定义悬浮提示文字
      btn.style.setProperty('--cat-color', Blocks.colorMap[cat.color] || 'var(--accent)');
	  // https://github.com/Nothing-Left2024/HtmlMaker Apache 2.0 Licence
      const iconWrap = el('span', 'cat-rail-icon');
      iconWrap.innerHTML = Icons.get(cat.icon) || '';
      btn.appendChild(iconWrap);
      const lbl = el('span', 'cat-rail-text');
      lbl.textContent = cat.label;
      btn.appendChild(lbl);
      btn.addEventListener('click', () => {
        const target = document.getElementById('cat-' + page + '-' + cat.id);
        if(target){
          target.scrollIntoView({ behavior:'smooth', block:'start' });
          // 闪烁高亮目标分类标题
          target.classList.add('pal-cat-flash');
          setTimeout(() => target.classList.remove('pal-cat-flash'), 800);
        }
      });
      rail.appendChild(btn);
    });
  }

  // ---------- 工作区 ----------
  function renderWorkspace(container, page){
    currentPage = page;
    const list = Store.listPage(page);
    container.innerHTML = '';
    if(!list.length){
      const empty = el('div', 'ws-empty');
      empty.textContent = '从左侧拖入积木开始搭建';
      container.appendChild(empty);
      return;
    }
    list.forEach(block => container.appendChild(renderBlock(block, page, true)));
  }

  function renderBlock(block, page, isRoot){
    const def = defOf(block);
    if(!def) return el('div','ws-empty','(未知积木)');

    // 属性积木使用独立渲染器
    if(def.type === 'property'){
      return renderPropertyBlock(block, page);
    }
    // JS 积木使用独立渲染器
    if(def.type === 'js'){
      return renderJsBlock(block, page, isRoot);
    }

    const node = el('div', 'block' + (isRoot ? ' block-root' : ''));
    node.dataset.id = block.id;
    node.dataset.page = page;
    const c = colorOf(def);
    node.style.backgroundColor = c;
    node.style.setProperty('--block-color', c);
    if(selectedId === block.id) node.classList.add('selected');

    // 标签行
    const label = el('div', 'b-label');
    label.innerHTML = Icons.get(def.icon) + '<span>' + def.label + '</span>';
    node.appendChild(label);

    // 选择器值输入（CSS 页）
    if(def.type === 'selector'){
      const inp = el('input', 'b-inline-input');
      inp.value = block.selectorValue || '';
      inp.placeholder = def.selectorKind === 'tag' ? '标签名' : (def.selectorKind === 'group' ? '类名' : 'id');
      inp.addEventListener('pointerdown', e => e.stopPropagation());
      inp.addEventListener('mousedown', e => e.stopPropagation());
      inp.addEventListener('input', () => Store.setSelectorValue(block.id, inp.value, page));
      node.appendChild(inp);
    }

    // 文本输入（文本类标签）
    if(def.type === 'tag' && Blocks.isTextTag(def.tag)){
      const tlab = el('span', 'b-inline-label');
      tlab.textContent = '文本:';
      const inp = el('input', 'b-inline-input');
      inp.value = block.text || '';
      inp.placeholder = '文本内容';
      inp.addEventListener('pointerdown', e => e.stopPropagation());
      inp.addEventListener('mousedown', e => e.stopPropagation());
      inp.addEventListener('input', () => Store.setText(block.id, inp.value, page));
      node.appendChild(tlab);
      node.appendChild(inp);
    }

    // 属性回显
    const summary = propsSummary(block);
    if(summary.length){
      const sp = el('div', 'b-props');
      summary.forEach(s => { const t = el('span'); t.textContent = s; sp.appendChild(t); });
      node.appendChild(sp);
    }

    // 右键 → 上下文菜单
    node.addEventListener('contextmenu', e => {
      e.preventDefault();
      e.stopPropagation();
      setSelectedInternal(block.id);
      Props.open(block, page, e.clientX, e.clientY);
    });

    // 选中（不与输入框冲突）
    node.addEventListener('pointerdown', e => {
      if(e.target.closest('.b-inline-input') || e.target.closest('.b-props')) return;
      e.stopPropagation();
      setSelectedInternal(block.id);
    });

    // 工作区内拖拽
    node.addEventListener('pointerdown', e => {
      if(e.target.closest('.b-inline-input')) return;
      e.stopPropagation();
      startBlockDrag(e, block, page, node);
    });

    // 子积木容器：标签和选择器都可嵌套（选择器放样式属性积木，标签放子标签或属性积木）
    const canHaveChildren = def.type === 'selector' || (def.type === 'tag' && def.canHaveChildren !== false);
    if(canHaveChildren){
      const ch = el('div', 'block-children');
      ch.dataset.parentId = block.id;
      (block.children || []).forEach(c => {
        const cd = defOf(c);
        if(cd) ch.appendChild(renderBlock(c, page, false));
      });
      node.appendChild(ch);
    }
    return node;
  }

  // 属性积木渲染：小尺寸，内联值输入，CSS 页有 :hover 切换
  function renderPropertyBlock(block, page){
    const def = defOf(block);
    const node = el('div', 'block block-prop');
    if(block.isHover) node.classList.add('prop-hover');
    node.dataset.id = block.id;
    node.dataset.page = page;
    node.style.backgroundColor = colorOf(def);
    if(selectedId === block.id) node.classList.add('selected');

    const label = el('div', 'b-label');
    label.innerHTML = Icons.get(def.icon) + '<span>' + def.label + '</span>';
    node.appendChild(label);
	
	// Made by Nothing-Left2024
	
    // 值输入
    const inp = createPropInput(def, block, page);
    if(inp) node.appendChild(inp);

    // :hover 切换（仅 CSS 页）
    if(page === 'css'){
      const hoverBtn = el('button', 'b-hover-btn' + (block.isHover ? ' active' : ''));
      hoverBtn.textContent = '悬';
      hoverBtn.title = '切换 :hover 状态';
      hoverBtn.addEventListener('pointerdown', e => e.stopPropagation());
      hoverBtn.addEventListener('click', e => { e.stopPropagation(); Store.togglePropertyHover(block.id, page); });
      node.appendChild(hoverBtn);
    }

    node.addEventListener('contextmenu', e => {
      e.preventDefault();
      e.stopPropagation();
      setSelectedInternal(block.id);
      Props.open(block, page, e.clientX, e.clientY);
    });

    node.addEventListener('pointerdown', e => {
      if(e.target.closest('.b-inline-input') || e.target.closest('.b-hover-btn') || e.target.tagName === 'SELECT') return;
      e.stopPropagation();
      setSelectedInternal(block.id);
      startBlockDrag(e, block, page, node);
    });
    return node;
  }

  function createPropInput(def, block, page){
    if(def.propType === 'color'){
      const inp = el('input', 'b-inline-input');
      inp.type = 'color';
      inp.value = block.text || '#000000';
      inp.style.width = '36px'; inp.style.height = '24px'; inp.style.padding = '0';
      inp.addEventListener('pointerdown', e => e.stopPropagation());
      inp.addEventListener('mousedown', e => e.stopPropagation());
      inp.addEventListener('input', () => Store.setText(block.id, inp.value, page));
      return inp;
    }
    if(def.propType === 'select'){
      const sel = el('select', 'b-inline-input');
      (def.propOptions || []).forEach(o => {
        const op = el('option'); op.value = o; op.textContent = o || '—';
        if(o === block.text) op.selected = true;
        sel.appendChild(op);
      });
      sel.addEventListener('pointerdown', e => e.stopPropagation());
      sel.addEventListener('mousedown', e => e.stopPropagation());
      sel.addEventListener('change', () => Store.setText(block.id, sel.value, page));
      return sel;
    }
    const inp = el('input', 'b-inline-input');
    inp.value = block.text || '';
    inp.placeholder = def.propType === 'size' ? '如 100px' : '值';
    inp.addEventListener('pointerdown', e => e.stopPropagation());
    inp.addEventListener('mousedown', e => e.stopPropagation());
    inp.addEventListener('input', () => Store.setText(block.id, inp.value, page));
    return inp;
  }

  // ---------- JS 积木渲染 ----------
  // JS 积木：标签 + 参数输入框（按 def.params 定义）+ 可选子积木容器
  function renderJsBlock(block, page, isRoot){
    const def = defOf(block);
    const node = el('div', 'block block-js' + (isRoot ? ' block-root' : ''));
    node.dataset.id = block.id;
    node.dataset.page = page;
    const c = colorOf(def);
    node.style.backgroundColor = c;
    node.style.setProperty('--block-color', c);
    if(selectedId === block.id) node.classList.add('selected');

    // 标签
    const label = el('div', 'b-label');
    label.innerHTML = Icons.get(def.icon) + '<span>' + def.label + '</span>';
    node.appendChild(label);

    // 参数输入框（按 def.params 顺序）
    (def.params || []).forEach(p => {
      const lbl = el('span', 'b-inline-label');
      lbl.textContent = p.label + ':';
      const inp = el('input', 'b-inline-input b-js-param');
      inp.value = (block.params && block.params[p.name]) || '';
      inp.placeholder = p.ph || '';
      inp.dataset.param = p.name;
      inp.addEventListener('pointerdown', e => e.stopPropagation());
      inp.addEventListener('mousedown', e => e.stopPropagation());
      inp.addEventListener('input', () => Store.setJsParam(block.id, p.name, inp.value, page));
      node.appendChild(lbl);
      node.appendChild(inp);
    });

    // 右键菜单
    node.addEventListener('contextmenu', e => {
      e.preventDefault();
      e.stopPropagation();
      setSelectedInternal(block.id);
      Props.open(block, page, e.clientX, e.clientY);
    });

    // 选中
    node.addEventListener('pointerdown', e => {
      if(e.target.closest('.b-inline-input') || e.target.closest('.b-props')) return;
      e.stopPropagation();
      setSelectedInternal(block.id);
    });

    // 工作区内拖拽
    node.addEventListener('pointerdown', e => {
      if(e.target.closest('.b-inline-input')) return;
      e.stopPropagation();
      startBlockDrag(e, block, page, node);
    });

    // 子积木容器（仅当该积木为代码块，即 close 非空且 canHaveChildren !== false）
    const canHaveChildren = def.canHaveChildren !== false && def.close;
    if(canHaveChildren){
      const ch = el('div', 'block-children');
      ch.dataset.parentId = block.id;
      (block.children || []).forEach(c => {
        const cd = defOf(c);
        if(cd) ch.appendChild(renderBlock(c, page, false));
      });
      node.appendChild(ch);
    }
    return node;
  }

  // ---------- 拖拽系统 ----------
  function startPaletteDrag(e, def, page){
    const startX = e.clientX, startY = e.clientY;
    let started = false;
    const move = ev => {
      if(!started && (Math.abs(ev.clientX-startX)>4 || Math.abs(ev.clientY-startY)>4)){
        started = true;
        drag = { kind:'palette', def, page, ghost: buildGhostFromDef(def) };
        document.body.appendChild(drag.ghost);
      }
      if(started){ positionGhost(ev); updateDropTarget(ev); }
    };
    const up = ev => {
      document.removeEventListener('pointermove', move);
      document.removeEventListener('pointerup', up);
      if(started && drag){
        const target = computeDrop(ev, page);
        // 兼容性检查
        const compat = checkCompatibility(def, target, page);
        if(!compat.ok){
          showToast(compat.reason, 'warning');
          cleanupDrag();
          return;
        }
        const b = Store.createBlock(def);
        if(target.parentId == null) Store.addBlock(b, null, page);
        else Store.addBlock(b, target.parentId, page);
        cleanupDrag();
      }
    };
    document.addEventListener('pointermove', move);
    document.addEventListener('pointerup', up);
  }

  function startBlockDrag(e, block, page, node){
    if(drag) return;
    const startX = e.clientX, startY = e.clientY;
    let started = false;
    const move = ev => {
      if(!started && (Math.abs(ev.clientX-startX)>4 || Math.abs(ev.clientY-startY)>4)){
        started = true;
        drag = { kind:'workspace', block, page, ghost: buildGhostFromBlock(block), sourceEl: node };
        document.body.appendChild(drag.ghost);
        node.style.opacity = '.35';
      }
      if(started){ positionGhost(ev); updateDropTarget(ev); }
    };
    const up = ev => {
      document.removeEventListener('pointermove', move);
      document.removeEventListener('pointerup', up);
      if(started && drag){
        if(isOverPalette(ev, page)){
          Store.deleteBlock(block.id, page);
        } else {
          const target = computeDrop(ev, page);
          if(target){
            const blockDef = defOf(block);
            const compat = checkCompatibility(blockDef, target, page);
            if(!compat.ok){
              // 不兼容时仅提示，保留原积木原位（不删除、不移动）
              showToast(compat.reason, 'warning');
            } else {
              Store.moveBlock(block.id, target, page);
            }
          }
        }
        cleanupDrag();
      } else {
        node.style.opacity = '';
      }
    };
    document.addEventListener('pointermove', move);
    document.addEventListener('pointerup', up);
  }

  // 判断是否拖到调色板区域
  function isOverPalette(ev, page){
    if(!drag || !drag.ghost) return false;
    drag.ghost.style.display = 'none';
    const under = document.elementFromPoint(ev.clientX, ev.clientY);
    drag.ghost.style.display = '';
    if(!under) return false;
    const pal = under.closest('.palette');
    if(!pal) return false;
    // 必须是当前页对应的调色板
    const target = page === 'css' ? document.getElementById('paletteCss')
                 : page === 'js' ? document.getElementById('paletteJs')
                 : document.getElementById('paletteHtml');
    return pal === target;
  }

  // 构建拖动 ghost（带旋转效果）
  function buildGhostFromDef(def){
    const g = el('div', 'ghost');
    g.style.backgroundColor = Blocks.colorOf(def);
    g.innerHTML = Icons.get(def.icon) + '<span>' + def.label + '</span>';
    return g;
  }
  function buildGhostFromBlock(block){
    const def = defOf(block);
    const g = el('div', 'ghost');
    g.style.backgroundColor = colorOf(def);
    let html = Icons.get(def.icon) + '<span>' + def.label + '</span>';
    const sum = propsSummary(block);
    if(sum.length){
      html += '<span class="b-props">' + sum.map(s => '<span>' + s + '</span>').join('') + '</span>';
    }
    g.innerHTML = html;
    return g;
  }

  function positionGhost(ev){
    if(!drag) return;
    drag.ghost.style.left = (ev.clientX + 8) + 'px';
    drag.ghost.style.top = (ev.clientY + 8) + 'px';
  }

  function clearDropIndicators(){
    document.querySelectorAll('.drop-preview').forEach(n => n.remove());
    document.querySelectorAll('.drop-inside').forEach(n => n.classList.remove('drop-inside'));
  }

  // 构建拖拽落点预览：半透明的积木样子，让用户直观看到放置后的效果
  function buildDropPreview(){
    const def = drag.def || (drag.block ? defOf(drag.block) : null);
    if(!def) return el('div', 'drop-preview');
    const isProp = def.type === 'property';
    const preview = el('div', 'block drop-preview' + (isProp ? ' block-prop' : ''));
    preview.style.backgroundColor = colorOf(def);
    let html = Icons.get(def.icon) + '<span>' + def.label + '</span>';
    if(drag.block){
      const sum = propsSummary(drag.block);
      if(sum.length){
        html += '<span class="b-props">' + sum.map(s => '<span>' + s + '</span>').join('') + '</span>';
      }
    }
    preview.innerHTML = html;
    return preview;
  }

  function updateDropTarget(ev){
    clearDropIndicators();
    if(!drag) return;
    const wsId = drag.page === 'css' ? '#workspaceCss'
               : drag.page === 'js' ? '#workspaceJs'
               : '#workspaceHtml';
    const ws = document.querySelector(wsId);
    if(!ws) return;
    const elUnder = elementUnderGhost(ev);
    if(!elUnder){ showRootPreview(ws); return; }
    const blockEl = elUnder.closest('.block');
    if(!blockEl || blockEl.dataset.page !== drag.page){ showRootPreview(ws); return; }
    const id = blockEl.dataset.id;
    const block = findBlockById(id, drag.page);
    if(!block){ showRootPreview(ws); return; }
    const def = defOf(block);
    const rect = blockEl.getBoundingClientRect();
    const relY = (ev.clientY - rect.top) / rect.height;
    // 仅 tag 容器、selector 与有子积木的 JS 块可嵌套
    const canChild = def && (
      def.type === 'tag' ||
      def.type === 'selector' ||
      (def.type === 'js' && def.canHaveChildren !== false && def.close)
    );

    if(canChild && relY > 0.35 && relY < 0.75){
      // 工作区拖拽：嵌套到自己内部 = 原位，不显示预览
      if(isOriginalDrop(blockEl, null)){ drag._drop = null; return; }
      blockEl.classList.add('drop-inside');
      // 在子积木容器末尾插入预览
      let ch = blockEl.querySelector(':scope > .block-children');
      if(!ch){
        ch = el('div', 'block-children');
        blockEl.appendChild(ch);
      }
      ch.appendChild(buildDropPreview());
      drag._drop = { parentId: id, index: (block.children||[]).length };
    } else {
      const before = relY <= 0.5;
      // 工作区拖拽：落点为原位（自己或相邻兄弟朝向自己）时不显示预览
      if(isOriginalDrop(blockEl, before)){ drag._drop = null; return; }
      const preview = buildDropPreview();
      if(before) blockEl.parentNode.insertBefore(preview, blockEl);
      else blockEl.parentNode.insertBefore(preview, blockEl.nextSibling);
      drag._drop = computeSiblingDrop(blockEl, before, drag.page);
    }
  }

  // 判断落点是否为被拖拽积木的当前位置（原位）
  // - blockEl 为 null 且嵌套：表示嵌套到自己内部 → 原位
  // - blockEl 为自己（before/after self）→ 原位
  // - blockEl 为相邻兄弟且方向朝向自己 → 原位（拖动后位置不变）
  function isOriginalDrop(blockEl, before){
    if(drag.kind !== 'workspace' || !drag.sourceEl) return false;
    const src = drag.sourceEl;
    if(blockEl === src) return true;                 // 落在自己身上
    if(before === null) return false;                // 嵌套模式仅判断"自己"
    if(before && blockEl.nextElementSibling === src) return true;   // 后一个兄弟的前面 = 自己原位
    if(!before && blockEl.previousElementSibling === src) return true; // 前一个兄弟的后面 = 自己原位
    return false;
  }

  function computeSiblingDrop(blockEl, before, page){
    const parent = blockEl.parentNode;
    const parentId = parent.dataset && parent.dataset.parentId ? parent.dataset.parentId : null;
    const id = blockEl.dataset.id;
    const list = Store.listPage(page);
    const idx = siblingIndex(id, list);
    return { parentId, index: before ? idx : idx + 1 };
  }

  function siblingIndex(id, list){
    function walk(arr){
      for(let i=0;i<arr.length;i++){
        if(arr[i].id === id) return i;
        if(arr[i].children){ const r = walk(arr[i].children); if(r>=0) return r; }
      }
      return -1;
    }
    return walk(list);
  }

  function showRootPreview(ws){
    ws.appendChild(buildDropPreview());
    drag._drop = { parentId: null, index: 9999 };
  }

  function elementUnderGhost(ev){
    if(!drag.ghost) return null;
    drag.ghost.style.display = 'none';
    const under = document.elementFromPoint(ev.clientX, ev.clientY);
    drag.ghost.style.display = '';
    return under;
  }

  function computeDrop(ev, page){
    if(drag && drag._drop){
      const d = drag._drop;
      const list = Store.listPage(page);
      if(d.parentId == null && d.index >= list.length) d.index = list.length;
      return d;
    }
    return null;
  }

  function cleanupDrag(){
    clearDropIndicators();
    if(drag){
      if(drag.ghost) drag.ghost.remove();
      if(drag.sourceEl && drag.kind === 'workspace') drag.sourceEl.style.opacity = '';
    }
    drag = null;
  }

  function findBlockById(id, page){
    const list = Store.listPage(page);
    function walk(arr){
      for(const b of arr){
        if(b.id === id) return b;
        if(b.children){ const r = walk(b.children); if(r) return r; }
      }
      return null;
    }
    return walk(list);
  }

  function isDescendantOf(parentId, ancestorId, page){
    const anc = findBlockById(ancestorId, page);
    if(!anc || !anc.children) return false;
    function walk(arr){ return arr.some(b => b.id === parentId || (b.children && walk(b.children))); }
    return walk(anc.children);
  }

  function setSelectedInternal(id){
    if(selectedId === id) return;
    selectedId = id;
    const wsId = currentPage === 'css' ? '#workspaceCss'
               : currentPage === 'js' ? '#workspaceJs'
               : '#workspaceHtml';
    const ws = document.querySelector(wsId);
    if(ws){
      ws.querySelectorAll('.block.selected').forEach(n => n.classList.remove('selected'));
      if(id){
        const n = ws.querySelector('.block[data-id="' + id + '"]');
        if(n) n.classList.add('selected');
      }
    }
  }
  function clearSelected(){
    if(!selectedId) return;
    selectedId = null;
    document.querySelectorAll('.block.selected').forEach(n => n.classList.remove('selected'));
  }

  return {
    renderPalette,
    renderWorkspace,
    renderBlock,
    getSelected(){ return selectedId; },
    setSelected(id){ setSelectedInternal(id); },
    clearSelected,
    getCurrentPage(){ return currentPage; },
    showToast,
  };
})();
