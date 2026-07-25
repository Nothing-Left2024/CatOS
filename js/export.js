// 导出：完整 HTML / 仅 CSS / 项目 JSON，支持复制与下载
const Exporter = (() => {
  let mode = 'html';

  const overlay = () => document.getElementById('exportOverlay');
  const codeEl = () => document.getElementById('exportCode');

  function open(){
    render();
    overlay().classList.remove('hidden');
  }
  function close(){ overlay().classList.add('hidden'); }

  function currentCode(){
    const s = Store.getState();
    if(mode === 'html') return Compile.compileFull();
    if(mode === 'css') return Compile.compileCssOnly();
    if(mode === 'js') return Compile.compileJsOnly();
    return JSON.stringify(s, null, 2);
  }
  function fileName(){
    const name = (Store.getState().meta.name || '未命名项目').replace(/[\\/:*?"<>|]/g,'_');
    if(mode === 'html') return name + '.html';
    if(mode === 'css') return name + '.css';
    if(mode === 'js') return name + '.js';
    return name + '.json';
  }

  function render(){
    codeEl().textContent = currentCode();
    document.querySelectorAll('.export-tabs .tab').forEach(t => {
      t.classList.toggle('active', t.dataset.exp === mode);
    });
  }

  function copy(){
    const text = currentCode();
    navigator.clipboard.writeText(text).then(()=>{}, ()=>{
      // 后备
      const ta = document.createElement('textarea');
      ta.value = text; document.body.appendChild(ta); ta.select();
      try{ document.execCommand('copy'); }catch(e){}
      ta.remove();
    });
    const btn = document.getElementById('exportCopy');
    const old = btn.textContent; btn.textContent = '已复制';
    setTimeout(()=>btn.textContent = old, 1200);
  }

  function download(){
    const text = currentCode();
    const blob = new Blob([text], {type: 'text/plain;charset=utf-8'});
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url; a.download = fileName();
    document.body.appendChild(a); a.click(); a.remove();
    URL.revokeObjectURL(url);
  }

  function init(){
    document.getElementById('exportClose').addEventListener('click', close);
    document.getElementById('exportCopy').addEventListener('click', copy);
    document.getElementById('exportDownload').addEventListener('click', download);
    document.querySelectorAll('.export-tabs .tab').forEach(t => {
      t.addEventListener('click', () => { mode = t.dataset.exp; render(); });
    });
    overlay().addEventListener('click', e => { if(e.target === overlay()) close(); });

    // 导入项目：点击按钮触发文件选择
    const importBtn = document.getElementById('exportImport');
    const fileInput = document.getElementById('importFileInput');
    if(importBtn && fileInput){
      importBtn.addEventListener('click', () => fileInput.click());
      fileInput.addEventListener('change', handleImportFile);
    }
  }

  // 处理导入文件：读取 → 解析 → 校验 → 替换状态
  function handleImportFile(e){
    const file = e.target.files && e.target.files[0];
    if(!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      try{
        const data = JSON.parse(reader.result);
        // 基础校验：必须包含 html / css / meta 字段
        if(!data || typeof data !== 'object') throw new Error('文件内容不是有效的 JSON 对象');
        if(!Array.isArray(data.html)) throw new Error('缺少 html 字段或格式不正确');
        if(!Array.isArray(data.css)) throw new Error('缺少 css 字段或格式不正确');
        if(!data.meta || typeof data.meta !== 'object') throw new Error('缺少 meta 字段');
        // js 字段可选，但若存在必须是数组
        if(data.js != null && !Array.isArray(data.js)) throw new Error('js 字段格式不正确');
        if(!data.js) data.js = [];
        Store.replaceAll(data);
        if(Engine && Engine.showToast) Engine.showToast('项目导入成功', 'success');
        close();
      }catch(err){
        if(Engine && Engine.showToast) Engine.showToast('导入失败：' + err.message, 'error');
      }
      // 重置 input，便于重复导入同一文件
      e.target.value = '';
    };
    reader.onerror = () => {
      if(Engine && Engine.showToast) Engine.showToast('文件读取失败', 'error');
      e.target.value = '';
    };
    reader.readAsText(file);
  }

  return { open, close, init };
})();
