// 预览面板：显示生成的 HTML 代码（可切换为 iframe 渲染预览）+ 防抖刷新
const Preview = (() => {
  let timer = null;
  let el;
  let frameEl;
  let previewMode = false;

  function init(){
    el = document.getElementById('htmlCode');
    frameEl = document.getElementById('previewFrame');
  }
  function refresh(){
    if(!el) return;
    const code = Compile.compileFull();
    el.textContent = code;
    if(previewMode && frameEl){
      frameEl.srcdoc = code;
    }
  }
  function schedule(){
    if(timer) clearTimeout(timer);
    timer = setTimeout(refresh, 150);
  }
  // 切换代码视图 / 渲染预览
  function togglePreview(){
    previewMode = !previewMode;
    if(frameEl) frameEl.classList.toggle('hidden', !previewMode);
    if(el) el.classList.toggle('hidden', previewMode);
    if(previewMode && frameEl) frameEl.srcdoc = Compile.compileFull();
    const btn = document.getElementById('btnPreviewToggle');
    if(btn) btn.textContent = previewMode ? '代码' : '预览';
  }

  return { init, refresh, schedule, togglePreview };
})();
