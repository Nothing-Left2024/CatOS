// JS 子页面：右侧 JS 代码面板刷新
const JsPage = (() => {
  function refresh(){
    const js = Compile.compileJsOnly();
    const panel = document.getElementById('jsCode');
    if(panel) panel.textContent = js || '// 暂无 JavaScript';
  }

  return { refresh };
})();
