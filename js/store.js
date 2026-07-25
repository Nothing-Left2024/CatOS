// 状态管理：新数据模型（styles/hoverStyles/text）+ 撤销重做 + localStorage
// 三页：html / css / js
const Store = (() => {
  const KEY = 'htmlmaker_project_v2'; // v2: 新模型，与旧版不兼容
  let state = createEmpty();
  let undoStack = [];
  let redoStack = [];
  const listeners = new Set();
  const softListeners = new Set();

  function createEmpty(){
    return {
      html: [],
      css: [],
      js: [],
      meta: { name: '未命名页面', created: Date.now() }
    };
  }
  function createEmptyStyles(){
    return {
      width:{value:'',unit:'px'}, height:{value:'',unit:'px'},
      color:'', background:'',
      fontSize:{value:'',unit:'px'}, margin:{value:'',unit:'px'}, padding:{value:'',unit:'px'},
      opacity:'', border:'', radius:{value:'',unit:'px'},
      display:'', position:''
    };
  }

  function genId(){ return 'b' + Math.random().toString(36).slice(2, 9) + Date.now().toString(36).slice(-3); }
  function clone(obj){ return JSON.parse(JSON.stringify(obj)); }

  function emit(){ listeners.forEach(fn => fn(state)); }
  function emitSoft(){ softListeners.forEach(fn => fn(state)); }
  function persist(){ try{ localStorage.setItem(KEY, JSON.stringify(state)); }catch(e){} }

  function snapshot(){
    undoStack.push(clone(state));
    if(undoStack.length > 60) undoStack.shift();
    redoStack = [];
  }
  function after(){ persist(); emit(); }

  function findBlock(id, list){
    list = list || state.html;
    for(const b of list){
      if(b.id === id) return { block: b, parent: list };
      if(b.children && b.children.length){
        const r = findBlock(id, b.children);
        if(r) return r;
      }
    }
    return null;
  }

  function findBlockAny(id){
    let r = findBlock(id, state.html); if(r) return r;
    r = findBlock(id, state.css); if(r) return r;
    r = findBlock(id, state.js); return r;
  }

  // 取得某页的积木列表（兼容缺失 js 字段的旧数据）
  function listPage(page){
    if(page === 'css') return state.css;
    if(page === 'js') return state.js || (state.js = []);
    return state.html;
  }

  // === 公共 API ===
  return {
    getState(){ return state; },
    subscribe(fn){ listeners.add(fn); return () => listeners.delete(fn); },
    subscribeSoft(fn){ softListeners.add(fn); return () => softListeners.delete(fn); },
    genId,
    createEmptyStyles,
    snapshot, // 暴露给右键菜单使用

    createBlock(def){
      if(def.type === 'property'){
        return {
          id: genId(),
          type: 'property',
          def: def.id,
          label: def.label || def.id,
          text: '',
          isHover: false,
          children: undefined
        };
      }
      if(def.type === 'js'){
        return {
          id: genId(),
          type: 'js',
          def: def.id,
          label: def.label || def.id,
          params: {},
          children: def.canHaveChildren === false ? undefined : []
        };
      }
      return {
        id: genId(),
        type: def.type,
        def: def.id,
        tag: def.tag || null,
        label: def.label || def.id,
        props: { name:'', group:'', visible:true, extra:{} },
        styles: createEmptyStyles(),
        hoverStyles: createEmptyStyles(),
        text: '',
        selectorKind: def.selectorKind || null,
        selectorValue: '',
        children: def.canHaveChildren === false ? undefined : []
      };
    },

    // ---- 软编辑（不重渲染工作区，保护输入焦点）----
    setArg(){}, // 兼容旧调用，无操作
    setText(blockId, text, page){
      const list = listPage(page);
      const r = findBlock(blockId, list);
      if(r) r.block.text = text;
      persist(); emitSoft();
    },
    setSelectorValue(blockId, value, page){
      const list = listPage(page);
      const r = findBlock(blockId, list);
      if(r) r.block.selectorValue = value;
      persist(); emitSoft();
    },
    // JS 积木参数编辑（软编辑）
    setJsParam(blockId, paramName, value, page){
      const list = listPage(page);
      const r = findBlock(blockId, list);
      if(r && r.block.type === 'js'){
        if(!r.block.params) r.block.params = {};
        r.block.params[paramName] = value;
      }
      persist(); emitSoft();
    },
    togglePropertyHover(blockId, page){
      const list = listPage(page);
      const r = findBlock(blockId, list);
      if(r && r.block.type === 'property'){
        r.block.isHover = !r.block.isHover;
      }
      persist(); emit();
    },
    setPageTitle(title){
      state.meta.name = title;
      persist(); emitSoft();
    },

    // ---- 硬编辑（重渲染工作区，进入历史）----
    setStyle(blockId, key, value, page, isHover){
      const list = listPage(page);
      const r = findBlock(blockId, list);
      if(r){
        const target = isHover ? r.block.hoverStyles : r.block.styles;
        target[key] = value;
      }
      persist(); emit();
    },
    setProp(blockId, key, value, page){
      const list = listPage(page);
      const r = findBlock(blockId, list);
      if(r) r.block.props[key] = value;
      persist(); emit();
    },
    setExtra(blockId, key, value, page){
      const list = listPage(page);
      const r = findBlock(blockId, list);
      if(r){
        if(!r.block.props.extra) r.block.props.extra = {};
        r.block.props.extra[key] = value;
      }
      persist(); emit();
    },
    setProps(blockId, props, page){
      const list = listPage(page);
      const r = findBlock(blockId, list);
      if(r) r.block.props = props;
      persist(); emit();
    },

    // ---- 结构操作（进入历史）----
    moveBlock(blockId, target, page){
      const list = listPage(page);
      const r = findBlock(blockId, list);
      if(!r) return;
      if(isDescendant(target.parentId, blockId, list)) return;
      const srcParent = r.parent;
      const srcIdx = srcParent.indexOf(r.block);
      const sameParent = (target.parentId == null && srcParent === list)
                     || (target.parentId != null && srcParent === (findBlock(target.parentId, list)||{}).block?.children);
      snapshot();
      const moving = clone(r.block);
      const idx0 = srcParent.indexOf(r.block);
      srcParent.splice(idx0, 1);
      let targetIndex = target.index;
      if(sameParent && srcIdx < targetIndex) targetIndex = Math.max(0, targetIndex - 1);
      if(target.parentId == null){
        const arr = listPage(page);
        arr.splice(Math.min(targetIndex, arr.length), 0, moving);
      } else {
        const pr = findBlock(target.parentId, list);
        if(!pr){ listPage(page).push(moving); }
        else {
          if(!pr.block.children) pr.block.children = [];
          pr.block.children.splice(Math.min(targetIndex, pr.block.children.length), 0, moving);
        }
      }
      function isDescendant(parentId, childId, list){
        if(!parentId) return false;
        const r = findBlock(parentId, list);
        if(!r) return false;
        const walk = (arr)=>arr.some(b=>b.id===childId || (b.children&&walk(b.children)));
        return r.block.children ? walk(r.block.children) : false;
      }
      after();
    },

    addBlock(block, parentId, page){
      snapshot();
      const list = listPage(page);
      if(parentId == null){ list.push(block); }
      else {
        const r = findBlock(parentId, list);
        if(r){ if(!r.block.children) r.block.children = []; r.block.children.push(block); }
        else list.push(block);
      }
      after();
      return block.id;
    },

    deleteBlock(blockId, page){
      snapshot();
      const list = listPage(page);
      const r = findBlock(blockId, list);
      if(r){ const i = r.parent.indexOf(r.block); r.parent.splice(i,1); }
      after();
    },

    cloneBlock(block){
      const c = clone(block);
      function reId(b){ b.id = genId(); if(b.children) b.children.forEach(reId); }
      reId(c);
      return c;
    },
    duplicateBlock(blockId, page){
      const list = listPage(page);
      const r = findBlock(blockId, list);
      if(!r) return null;
      const copy = this.cloneBlock(r.block);
      snapshot();
      const idx = r.parent.indexOf(r.block);
      r.parent.splice(idx + 1, 0, copy);
      after();
      return copy.id;
    },
    pasteBlock(block, parentId, page){
      snapshot();
      const list = listPage(page);
      if(parentId == null){ list.push(block); }
      else {
        const r = findBlock(parentId, list);
        if(r){ if(!r.block.children) r.block.children = []; r.block.children.push(block); }
        else list.push(block);
      }
      after();
      return block.id;
    },

    clearPage(page){
      snapshot();
      const list = listPage(page);
      list.length = 0;
      after();
    },

    findBlockAny,
    listPage,

    undo(){
      if(!undoStack.length) return;
      redoStack.push(clone(state));
      state = undoStack.pop();
      persist(); emit();
    },
    redo(){
      if(!redoStack.length) return;
      undoStack.push(clone(state));
      state = redoStack.pop();
      persist(); emit();
    },
    canUndo(){ return undoStack.length>0; },
    canRedo(){ return redoStack.length>0; },

    load(){
      try{ const s = localStorage.getItem(KEY); if(s){ state = JSON.parse(s); migrate(state); } }catch(e){}
      if(!state.html) state.html = [];
      if(!state.css) state.css = [];
      if(!state.js) state.js = [];
      if(!state.meta) state.meta = { name:'未命名页面', created:Date.now() };
      emit();
    },
    save(){ persist(); },
    reset(){ state = createEmpty(); undoStack=[]; redoStack=[]; persist(); emit(); },
    replaceAll(newState){ snapshot(); state = clone(newState); after(); }
  };

  // 兼容旧数据：若 block 缺少 styles/hoverStyles/text/params，补全
  function migrate(s){
    function fix(b){
      if(b.type === 'property'){
        if(b.isHover == null) b.isHover = false;
        if(b.text == null) b.text = '';
        return;
      }
      if(b.type === 'js'){
        if(!b.params) b.params = {};
        return;
      }
      if(!b.styles) b.styles = createEmptyStyles();
      if(!b.hoverStyles) b.hoverStyles = createEmptyStyles();
      if(b.text == null) b.text = '';
      if(!b.props) b.props = { name:'', group:'', visible:true, extra:{} };
      if(!b.props.extra) b.props.extra = {};
      if(b.children) b.children.forEach(fix);
    }
    (s.html||[]).forEach(fix);
    (s.css||[]).forEach(fix);
    (s.js||[]).forEach(fix);
  }
})();
