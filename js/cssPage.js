// CSS 子页面：右侧 CSS 代码面板刷新
const CssPage = (() => {
  function refresh(){
    const css = Compile.compileCssOnly();
    const cssPanel = document.getElementById('cssCodeCss');
    if(cssPanel) cssPanel.textContent = css || '/* 暂无 CSS */';
  }

  return { refresh };
})();
