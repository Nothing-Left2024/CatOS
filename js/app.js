// 主入口：接线、事件、页面切换、快捷键
(() => {
  let clipboard = null;

  function $(id){ return document.getElementById(id); }

  // 结构变化 → 重渲染工作区 + 预览 + CSS/JS（palette 只在初始化与搜索时渲染）
  function onChange(){
    Engine.renderWorkspace($('workspaceHtml'), 'html');
    Engine.renderWorkspace($('workspaceCss'), 'css');
    Engine.renderWorkspace($('workspaceJs'), 'js');
    Preview.schedule();
    CssPage.refresh();
    JsPage.refresh();
    updateUndoRedo();
  }
  // 软变化（文本/选择器值/页面标题/JS 参数编辑）→ 仅刷新预览与 CSS/JS 代码
  function softUpdate(){
    Preview.schedule();
    CssPage.refresh();
    JsPage.refresh();
    // 页面标题输入框在工作区外，不会被重渲染影响
  }

  // 子页面顺序，用于判断转场方向
  const PAGE_ORDER = { html:0, css:1, js:2 };
  let activePage = 'html';
  let isAnimating = false;

  function switchPage(page){
    if(page === activePage || isAnimating) return;
    const fromOrder = PAGE_ORDER[activePage];
    const toOrder = PAGE_ORDER[page];
    const direction = toOrder > fromOrder ? 1 : -1;   // 1=进入更靠右的页（新页面从右滑入），-1=从左滑入
    const oldPage = activePage;
    activePage = page;
    isAnimating = true;

    const oldEl = $('page-' + oldPage);
    const newEl = $('page-' + page);

    // 切换顶栏 tab 高亮
    document.querySelectorAll('.page-switch .tab').forEach(t => t.classList.toggle('active', t.dataset.page === page));

    // 新页面：先 display:flex 显示出来，并设置屏外起始位置（无动画）
    newEl.style.setProperty('--enter-from', direction > 0 ? '100%' : '-100%');
    newEl.classList.add('active', 'page-enter-prep');

    // 旧页面：同步设置滑出方向（与新页面对称，都滑 100% 避免视觉拽住感）
    oldEl.style.setProperty('--leave-to', direction > 0 ? '-100%' : '100%');

    // 用双 rAF 替代强制 reflow：第一帧让浏览器接受初始样式，第二帧触发动画
    requestAnimationFrame(() => {
      requestAnimationFrame(() => {
        newEl.classList.remove('page-enter-prep');
        newEl.classList.add('page-enter-active');
        oldEl.classList.add('page-leave-active');
      });
    });

    // 动画结束清理（只监听新页面一个元素，避免双触发）
    const cleanup = () => {
      newEl.classList.remove('page-enter-active');
      oldEl.classList.remove('active', 'page-leave-active');
      oldEl.style.removeProperty('--leave-to');
      newEl.style.removeProperty('--enter-from');
      isAnimating = false;
    };
    newEl.addEventListener('animationend', cleanup, { once:true });
    // 兜底：略大于动画时长，防止事件未触发卡死
    setTimeout(cleanup, 400);
  }

  function updateUndoRedo(){
    $('btnUndo').disabled = !Store.canUndo();
    $('btnRedo').disabled = !Store.canRedo();
    $('btnUndo').style.opacity = Store.canUndo() ? '1' : '.4';
    $('btnRedo').style.opacity = Store.canRedo() ? '1' : '.4';
  }

  function init(){
    Props.init();
    Exporter.init();
    Preview.init();

    // 调色板只渲染一次（搜索框自身负责后续刷新）
    Engine.renderPalette($('paletteHtml'), Blocks.htmlCategories, 'html');
    Engine.renderPalette($('paletteCss'), Blocks.cssCategories, 'css');
    Engine.renderPalette($('paletteJs'), Blocks.jsCategories, 'js');

    Store.load();
    Store.subscribe(onChange);
    Store.subscribeSoft(softUpdate);

    // 页面标题输入
    const titleInput = $('pageTitleInput');
    titleInput.value = Store.getState().meta.name || '';
    titleInput.addEventListener('input', () => Store.setPageTitle(titleInput.value));

    // 顶栏
    document.querySelectorAll('.page-switch .tab').forEach(t => t.addEventListener('click', () => switchPage(t.dataset.page)));
    $('btnUndo').addEventListener('click', () => Store.undo());
    $('btnRedo').addEventListener('click', () => Store.redo());
    $('btnExport').addEventListener('click', () => Exporter.open());
    $('btnCopyHtml').addEventListener('click', () => copyText(Compile.compileFull(), $('btnCopyHtml')));
    $('btnCopyJs').addEventListener('click', () => copyText(Compile.compileJsOnly(), $('btnCopyJs')));
    $('btnPreviewToggle').addEventListener('click', () => Preview.togglePreview());
    $('btnAbout').addEventListener('click', () => openAbout());
    $('aboutClose').addEventListener('click', () => closeAbout());
    $('aboutOverlay').addEventListener('click', e => { if(e.target === $('aboutOverlay')) closeAbout(); });

    // 主题切换
    const themeBtn = $('btnTheme');
    function applyTheme(dark){
      document.body.classList.toggle('dark', dark);
      themeBtn.textContent = dark ? '浅色' : '深色';
      try{ localStorage.setItem('htmlmaker_theme', dark ? 'dark' : 'light'); }catch(e){}
    }
    let initDark = false;
    try{ initDark = localStorage.getItem('htmlmaker_theme') === 'dark'; }catch(e){}
    applyTheme(initDark);
    themeBtn.addEventListener('click', () => applyTheme(!document.body.classList.contains('dark')));

    // 调色板积木悬浮提示（仅左侧调色板区域）
    initPaletteTooltip();

    // 监听右键菜单的复制事件
    document.addEventListener('htmlmaker:copy', e => {
      const { id, page } = e.detail;
      const r = Store.findBlockAny(id);
      if(r) clipboard = Store.cloneBlock(r.block);
    });

    // 剪贴板内容查询（供 Props.openWorkspace 判断粘贴按钮是否可用）
    document.addEventListener('htmlmaker:clipboard-query', e => {
      if(e.detail && typeof e.detail.result === 'function') e.detail.result(!!clipboard);
    });

    // 工作区右键菜单的粘贴操作
    document.addEventListener('htmlmaker:paste', e => {
      if(!clipboard) return;
      const page = (e.detail && e.detail.page) || activePage;
      const copy = Store.cloneBlock(clipboard);
      const newId = Store.pasteBlock(copy, null, page);
      if(newId && page === activePage) Engine.setSelected(newId);
    });

    // 全局右键：拦截浏览器默认菜单，工作区空白处显示自定义菜单
    document.addEventListener('contextmenu', e => {
      // 在积木上：积木自己的 contextmenu 处理器已 open + preventDefault，这里跳过
      if(e.target.closest('.block')) return;
      // 在工作区空白处：显示通用菜单
      const ws = e.target.closest('.workspace');
      if(ws){
        e.preventDefault();
        const page = ws.id === 'workspaceHtml' ? 'html' : (ws.id === 'workspaceCss' ? 'css' : 'js');
        Props.openWorkspace(page, e.clientX, e.clientY);
        return;
      }
      // 其他位置（顶栏/调色板/预览区/弹层等）：仅阻止浏览器默认菜单
      e.preventDefault();
    });

    // 快捷键
    document.addEventListener('keydown', e => {
      if(e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA') return;
      if((e.ctrlKey || e.metaKey) && e.key === 'z'){ e.preventDefault(); Store.undo(); }
      else if((e.ctrlKey || e.metaKey) && (e.key === 'y' || (e.shiftKey && e.key === 'Z'))){ e.preventDefault(); Store.redo(); }
      else if((e.ctrlKey || e.metaKey) && e.key === 's'){ e.preventDefault(); Store.save(); }
      else if((e.ctrlKey || e.metaKey) && e.key === 'c'){
        const id = Engine.getSelected();
        if(id){
          e.preventDefault();
          const r = Store.findBlockAny(id);
          if(r) clipboard = Store.cloneBlock(r.block);
        }
      }
      else if((e.ctrlKey || e.metaKey) && e.key === 'v'){
        if(clipboard){
          e.preventDefault();
          const copy = Store.cloneBlock(clipboard);
          let parentId = null;
          const selId = Engine.getSelected();
            if(selId){
              const r = Store.findBlockAny(selId);
              const sd = r && Blocks.get(r.block.def);
              if(sd && (sd.type === 'tag' || sd.type === 'selector' || sd.type === 'js')) parentId = selId;
            }
          const newId = Store.pasteBlock(copy, parentId, activePage);
          if(newId) Engine.setSelected(newId);
        }
      }
      else if((e.ctrlKey || e.metaKey) && e.key === 'd'){
        const id = Engine.getSelected();
        if(id){ e.preventDefault(); const newId = Store.duplicateBlock(id, activePage); if(newId) Engine.setSelected(newId); }
      }
      else if(e.key === 'Delete' && Engine.getSelected()){
        Store.deleteBlock(Engine.getSelected(), activePage);
      }
      else if(e.key === 'Escape'){
        Props.close();
      }
    });

    // 离开前保存
    window.addEventListener('beforeunload', () => Store.save());

    // 点击工作区空白处取消选中
    document.addEventListener('pointerdown', e => {
      if(e.target.closest('.block') || e.target.closest('#ctxBackdrop') || e.target.closest('.palette') || e.target.closest('#topbar') || e.target.closest('.ws-toolbar')) return;
      Engine.clearSelected();
    });

    // 初始渲染
    onChange();

    // 首次使用 → 显示欢迎界面
    initWelcome();
  }

  // ===== 欢迎界面（首次使用引导） =====
  const WELCOME_KEY = 'htmlmaker_welcomed';
  function initWelcome(){
    let welcomed = false;
    try{ welcomed = localStorage.getItem(WELCOME_KEY) === '1'; }catch(e){}
    if(welcomed) return;

    const overlay = $('welcomeOverlay');
    const steps = overlay.querySelectorAll('.welcome-step');
    const dots = overlay.querySelectorAll('.welcome-dot');
    const progressEl = $('welcomeProgress');
    const prevBtn = $('welcomePrev');
    const nextBtn = $('welcomeNext');
    const startBtn = $('welcomeStart');
    const skipBtn = $('welcomeSkip');
    let current = 0;
    // 步骤 8 为「过渡到实操」步骤：点击 nextBtn 时启动教练模式而非继续翻页
    const COACH_TRIGGER_STEP = 8;

    function show(i){
      current = Math.max(0, Math.min(i, steps.length - 1));
      steps.forEach((s, idx) => s.classList.toggle('hidden', idx !== current));
      dots.forEach((d, idx) => d.classList.toggle('active', idx === current));
      progressEl.textContent = `${current + 1} / ${steps.length}`;
      prevBtn.classList.toggle('hidden', current === 0);
      nextBtn.classList.toggle('hidden', current === steps.length - 1);
      startBtn.classList.toggle('hidden', current !== steps.length - 1);
      // 步骤 8：nextBtn 文案改为「开始实操」；其他步骤恢复「下一步」
      nextBtn.textContent = (current === COACH_TRIGGER_STEP) ? '开始实操' : '下一步';
    }
    function close(){
      try{ localStorage.setItem(WELCOME_KEY, '1'); }catch(e){}
      overlay.classList.add('hidden');
      // 确保完全隐藏（防御性：即使 hidden 类失效也能关闭）
      overlay.style.display = 'none';
    }
    // 仅隐藏卡片（不记录完成状态）—— 教练模式启动时使用
    function hideOnly(){
      overlay.classList.add('hidden');
      overlay.style.display = 'none';
    }
    // 重新显示卡片并跳到指定步骤（教练模式结束后使用）
    function resumeAt(i){
      overlay.classList.remove('hidden');
      overlay.style.display = '';
      overlay.tabIndex = -1;
      overlay.focus();
      show(i);
    }

    nextBtn.addEventListener('click', e => {
      e.stopPropagation();
      if(current === COACH_TRIGGER_STEP){
        // 启动交互式教练模式（不记录完成状态）
        hideOnly();
        startCoach();
        return;
      }
      show(current + 1);
    });
    prevBtn.addEventListener('click', e => { e.stopPropagation(); show(current - 1); });
    startBtn.addEventListener('click', e => { e.stopPropagation(); close(); });
    skipBtn.addEventListener('click', e => { e.stopPropagation(); close(); });
    dots.forEach(d => d.addEventListener('click', e => { e.stopPropagation(); show(parseInt(d.dataset.go, 10)); }));
    // 键盘支持
    overlay.addEventListener('keydown', e => {
      if(e.key === 'ArrowRight'){ e.preventDefault(); show(current + 1); }
      else if(e.key === 'ArrowLeft'){ e.preventDefault(); show(current - 1); }
      else if(e.key === 'Escape'){ e.preventDefault(); close(); }
    });

    show(0);
    overlay.classList.remove('hidden');
    overlay.style.display = '';
    // 让 overlay 可接收键盘事件
    overlay.tabIndex = -1;
    overlay.focus();

    // ===== 交互式教练模式 =====
    // 高亮目标元素 + 浮动提示卡，监听用户实际操作推进步骤
    function startCoach(){
      const coachLayer = $('coachLayer');
      const highlight = $('coachHighlight');
      const card = $('coachCard');
      const stepNumEl = $('coachStepNum');
      const titleEl = $('coachTitle');
      const descEl = $('coachDesc');
      const coachSkipBtn = $('coachSkip');
      const coachNextBtn = $('coachNext');

      // 教练步骤定义：target 是 CSS 选择器，action 决定如何监听完成
      const coachSteps = [
        {
          target: '.page-switch .tab[data-page="css"]',
          title: '切换到 CSS 页',
          desc: '点击顶部的「<b>CSS 编辑</b>」标签，切换到 CSS 编辑页。',
          action: 'click-tab', arg: 'css'
        },
        {
          target: '.page-switch .tab[data-page="js"]',
          title: '切换到 JS 页',
          desc: '很好！现在点击「<b>JS 编辑</b>」标签。',
          action: 'click-tab', arg: 'js'
        },
        {
          target: '.page-switch .tab[data-page="html"]',
          title: '回到 HTML 页',
          desc: '太棒了！最后点击「<b>HTML 编辑</b>」回到 HTML 页。',
          action: 'click-tab', arg: 'html'
        },
        {
          target: '#paletteHtml',
          title: '拖一个积木到工作区',
          desc: '从左侧调色板<b>按住</b>任意积木，<b>拖动</b>到中间工作区。<br/>完成后点击「完成」结束实操。',
          action: 'add-block', isLast: true
        }
      ];

      let coachIdx = 0;
      let unsubStore = null;
      let clickHandler = null;
      let repositionHandler = null;
      let blockAdded = false;

      // 定位高亮框：用目标元素 getBoundingClientRect 设置位置尺寸
      function positionHighlight(targetEl){
        const rect = targetEl.getBoundingClientRect();
        const pad = 4;
        highlight.style.top = (rect.top - pad) + 'px';
        highlight.style.left = (rect.left - pad) + 'px';
        highlight.style.width = (rect.width + pad * 2) + 'px';
        highlight.style.height = (rect.height + pad * 2) + 'px';
      }
      // 定位提示卡：优先放目标下方，其次上方，最后右侧；避免遮挡目标
      function positionCard(targetEl){
        const rect = targetEl.getBoundingClientRect();
        const cardW = Math.min(280, window.innerWidth - 32);
        const cardH = card.offsetHeight || 170;
        const gap = 16;
        let top, left;
        if(rect.bottom + gap + cardH < window.innerHeight){
          top = rect.bottom + gap;                       // 下方
        } else if(rect.top - gap - cardH > 0){
          top = rect.top - gap - cardH;                  // 上方
        } else {
          top = Math.max(16, (window.innerHeight - cardH) / 2);
        }
        if(rect.left + cardW < window.innerWidth){
          left = rect.left;
        } else {
          left = Math.max(16, window.innerWidth - cardW - 16);
        }
        card.style.top = top + 'px';
        card.style.left = left + 'px';
      }

      function cleanupListeners(){
        if(clickHandler){ document.removeEventListener('click', clickHandler, true); clickHandler = null; }
        if(unsubStore){ unsubStore(); unsubStore = null; }
      }

      function showStep(i){
        if(i >= coachSteps.length){ finishCoach(); return; }
        coachIdx = i;
        const step = coachSteps[coachIdx];
        const targetEl = document.querySelector(step.target);
        if(!targetEl){ finishCoach(); return; }

        stepNumEl.textContent = `${coachIdx + 1} / ${coachSteps.length}`;
        titleEl.textContent = step.title;
        descEl.innerHTML = step.desc;
        // 最后一步显示「完成」按钮，其他步隐藏
        coachNextBtn.classList.toggle('hidden', !step.isLast);
        coachNextBtn.textContent = '完成';

        positionHighlight(targetEl);
        positionCard(targetEl);
        coachLayer.classList.remove('hidden');

        cleanupListeners();
        blockAdded = false;

        if(step.action === 'click-tab'){
          // 监听顶栏 tab 点击（capture 阶段，确保先于 switchPage 拿到）
          clickHandler = (e) => {
            const tab = e.target.closest('.page-switch .tab');
            if(tab && tab.dataset.page === step.arg){
              cleanupListeners();
              // 延迟等页面切换动画播完再推进
              setTimeout(() => showStep(coachIdx + 1), 450);
            }
          };
          document.addEventListener('click', clickHandler, true);
        } else if(step.action === 'add-block'){
          // 订阅 store：html 列表长度增加即认为用户成功拖入积木
          // 用 lastLen 而非 initialLen，避免"先删后加"导致检测失效
          let lastLen = Store.getState().html.length;
          unsubStore = Store.subscribe(state => {
            if(state.html.length > lastLen && !blockAdded){
              blockAdded = true;
              descEl.innerHTML = '太棒了！你已成功拖入一个积木。<br/>点击「完成」结束实操，继续查看剩余教程。';
            }
            lastLen = state.html.length;
          });
        }
      }

      function finishCoach(){
        cleanupListeners();
        if(repositionHandler){
          window.removeEventListener('resize', repositionHandler);
          window.removeEventListener('scroll', repositionHandler, true);
          repositionHandler = null;
        }
        coachLayer.classList.add('hidden');
        // 记录已完成引导
        try{ localStorage.setItem(WELCOME_KEY, '1'); }catch(e){}
        // 重新打开欢迎卡片，跳到步骤 9（CSS 样式编辑）继续静态教程
        resumeAt(9);
      }

      function skipCoach(){
        cleanupListeners();
        if(repositionHandler){
          window.removeEventListener('resize', repositionHandler);
          window.removeEventListener('scroll', repositionHandler, true);
          repositionHandler = null;
        }
        coachLayer.classList.add('hidden');
        // 跳过也算完成引导
        try{ localStorage.setItem(WELCOME_KEY, '1'); }catch(e){}
      }

      // 窗口变化/滚动时重新定位高亮框和提示卡
      repositionHandler = () => {
        const step = coachSteps[coachIdx];
        if(!step) return;
        const targetEl = document.querySelector(step.target);
        if(targetEl){
          positionHighlight(targetEl);
          positionCard(targetEl);
        }
      };
      window.addEventListener('resize', repositionHandler);
      window.addEventListener('scroll', repositionHandler, true);

      // 绑定按钮
      coachSkipBtn.onclick = skipCoach;
      coachNextBtn.onclick = () => {
        // 最后一步的「完成」按钮：结束教练模式
        if(coachIdx >= coachSteps.length - 1){
          finishCoach();
        } else {
          showStep(coachIdx + 1);
        }
      };

      // 启动第一步
      showStep(0);
    }
  }

  // ===== 关于模态框 =====
  function openAbout(){
    const iconEl = $('aboutIcon');
    if(iconEl){
      iconEl.innerHTML = '';
      const img = document.createElement('img');
      img.src = 'src/img/html5icon.png';
      img.alt = 'HtmlMaker';
      img.style.width = '64px';
      img.style.height = '64px';
      img.style.objectFit = 'contain';
      iconEl.appendChild(img);
    }
    $('aboutOverlay').classList.remove('hidden');
  }
  function closeAbout(){
    $('aboutOverlay').classList.add('hidden');
  }

  // ===== 调色板积木悬浮提示 =====
  // 根据积木定义生成描述文字
  function describeBlock(def){
    if(!def) return '';
    let desc = def.desc;
    if(!desc){
      if(def.type === 'tag'){
        desc = '<' + def.tag + '> 标签' + (def.canHaveChildren === false ? '（自闭合，无子元素）' : '，可嵌套子标签或属性');
      } else if(def.type === 'selector'){
        const kind = def.selectorKind === 'tag' ? '标签选择器' : (def.selectorKind === 'group' ? '类选择器（.class）' : 'ID 选择器（#id）');
        desc = kind + '，拖入工作区后下方可放样式属性积木';
      } else if(def.type === 'property'){
        desc = '样式属性积木：拖入选择器内，设置 ' + def.propKey;
      } else if(def.type === 'js'){
        if(def.jsType === 'event') desc = '事件起点，必须放在顶层（不能嵌套）';
        else if(def.canHaveChildren === false) desc = '单行语句，参数在积木内输入';
        else desc = '代码块，可嵌套子语句，参数在积木内输入';
      } else {
        desc = def.label;
      }
    }
    return def.label + ' — ' + desc;
  }
  // 绑定调色板悬浮提示（事件委托，对 .pal-item 和 .cat-rail-btn 生效）
  function initPaletteTooltip(){
    const tip = $('blockTip');
    if(!tip) return;
    let tipTimer = null;
    let currentTarget = null;       // 记录当前触发提示的元素，避免在同一元素内重复触发

    // 从触发元素提取提示文字：优先 data-tip-text，否则从积木定义生成
    function getTipText(item){
      if(item.dataset.tipText) return item.dataset.tipText;
      const defId = item.dataset.defId;
      const def = defId && Blocks.get(defId);
      return def ? describeBlock(def) : '';
    }

    document.addEventListener('mouseover', e => {
      const item = e.target.closest && (e.target.closest('.pal-item') || e.target.closest('.cat-rail-btn'));
      if(!item) return;
      if(item === currentTarget) return;              // 同一元素不重复触发
      currentTarget = item;
      const text = getTipText(item);
      if(!text) return;
      clearTimeout(tipTimer);
      tipTimer = setTimeout(() => {
        tip.textContent = text;
        tip.classList.remove('hidden');
        positionTip(e.clientX, e.clientY);
      }, 300);
    });
    document.addEventListener('mousemove', e => {
      if(tip.classList.contains('hidden')) return;
      positionTip(e.clientX, e.clientY);
    });
    document.addEventListener('mouseout', e => {
      const item = e.target.closest && (e.target.closest('.pal-item') || e.target.closest('.cat-rail-btn'));
      if(!item) return;
      const related = e.relatedTarget;
      // 真正离开触发元素（related 不再属于任何 pal-item/cat-rail-btn）才隐藏
      if(!related || !related.closest || (!related.closest('.pal-item') && !related.closest('.cat-rail-btn'))){
        clearTimeout(tipTimer);
        currentTarget = null;
        tip.classList.add('hidden');
      }
    });
    function positionTip(x, y){
      const offset = 14;
      let left = x + offset, top = y + offset;
      const rect = tip.getBoundingClientRect();
      if(left + rect.width > window.innerWidth - 8) left = x - rect.width - offset;
      if(top + rect.height > window.innerHeight - 8) top = y - rect.height - offset;
      tip.style.left = left + 'px';
      tip.style.top = top + 'px';
    }
  }

  function copyText(text, btn){
    if(navigator.clipboard){
      navigator.clipboard.writeText(text).then(()=>{ flashBtn(btn); }, ()=>fallbackCopy(text, btn));
    } else fallbackCopy(text, btn);
  }
  function fallbackCopy(text, btn){
    const ta = document.createElement('textarea');
    ta.value = text; document.body.appendChild(ta); ta.select();
    try{ document.execCommand('copy'); flashBtn(btn); }catch(e){}
    ta.remove();
  }
  function flashBtn(btn){
    if(!btn) return;
    const old = btn.textContent; btn.textContent = '已复制';
    setTimeout(()=>btn.textContent = old, 1200);
  }

  if(document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
