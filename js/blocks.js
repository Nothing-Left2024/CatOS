// 积木定义库：HTML 标签库 + CSS 选择器积木 + 样式 schema
// 样式不再作为子积木，改由右键菜单编辑 block.styles / block.hoverStyles
const Blocks = (() => {

  // ===== HTML 标签定义 =====
  const tagDefs = [
    // 容器
    { id:'div', tag:'div', label:'容器', icon:'div', color:'container' },
    { id:'section', tag:'section', label:'区块', icon:'section', color:'container' },
    { id:'article', tag:'article', label:'文章', icon:'section', color:'container' },
    { id:'header', tag:'header', label:'页头', icon:'section', color:'container' },
    { id:'footer', tag:'footer', label:'页脚', icon:'section', color:'container' },
    { id:'nav', tag:'nav', label:'导航', icon:'section', color:'container' },
    { id:'main', tag:'main', label:'主体', icon:'section', color:'container' },
    // 文本
    { id:'p', tag:'p', label:'段落', icon:'p', color:'text' },
    { id:'span', tag:'span', label:'行内文本', icon:'span', color:'text' },
    { id:'h1', tag:'h1', label:'一级标题', icon:'h', color:'text' },
    { id:'h2', tag:'h2', label:'二级标题', icon:'h', color:'text' },
    { id:'h3', tag:'h3', label:'三级标题', icon:'h', color:'text' },
    { id:'a', tag:'a', label:'链接', icon:'a', color:'text',
      extraAttrs:[{name:'href',label:'链接地址'},{name:'target',label:'打开方式',type:'select',options:['','_blank','_self','_parent']}] },
    { id:'strong', tag:'strong', label:'强调', icon:'text', color:'text' },
    { id:'em', tag:'em', label:'斜体', icon:'text', color:'text' },
    { id:'br', tag:'br', label:'换行', icon:'text', color:'text', canHaveChildren:false, selfClosing:true },
    // 媒体
    { id:'img', tag:'img', label:'图片', icon:'img', color:'media', canHaveChildren:false, selfClosing:true,
      extraAttrs:[{name:'src',label:'图片地址'},{name:'alt',label:'替代文字'}] },
    { id:'video', tag:'video', label:'视频', icon:'media', color:'media',
      extraAttrs:[{name:'src',label:'视频地址'},{name:'controls',label:'控制条',type:'select',options:['','controls']}] },
    // 表单
    { id:'form', tag:'form', label:'表单', icon:'form', color:'form' },
    { id:'input', tag:'input', label:'输入框', icon:'input', color:'form', canHaveChildren:false, selfClosing:true,
      extraAttrs:[{name:'type',label:'类型',type:'select',options:['','text','password','email','number','checkbox','radio','submit','file']},{name:'placeholder',label:'占位符'},{name:'name',label:'名称'}] },
    { id:'button', tag:'button', label:'按钮', icon:'button', color:'form' },
    { id:'label', tag:'label', label:'标签', icon:'form', color:'form' },
    { id:'textarea', tag:'textarea', label:'文本域', icon:'form', color:'form',
      extraAttrs:[{name:'placeholder',label:'占位符'},{name:'rows',label:'行数'}] },
    // 列表
    { id:'ul', tag:'ul', label:'无序列表', icon:'ul', color:'list' },
    { id:'ol', tag:'ol', label:'有序列表', icon:'ul', color:'list' },
    { id:'li', tag:'li', label:'列表项', icon:'li', color:'list' },
    // 表格
    { id:'table', tag:'table', label:'表格', icon:'table', color:'table' },
    { id:'tr', tag:'tr', label:'行', icon:'table', color:'table' },
    { id:'td', tag:'td', label:'单元格', icon:'table', color:'table' },
    { id:'th', tag:'th', label:'表头单元格', icon:'table', color:'table' },
    { id:'thead', tag:'thead', label:'表头', icon:'table', color:'table' },
    { id:'tbody', tag:'tbody', label:'表体', icon:'table', color:'table' },
    { id:'caption', tag:'caption', label:'表格标题', icon:'table', color:'table' },
    // 语义结构
    { id:'aside', tag:'aside', label:'侧栏', icon:'section', color:'container' },
    { id:'figure', tag:'figure', label:'图块', icon:'section', color:'container' },
    { id:'figcaption', tag:'figcaption', label:'图块标题', icon:'text', color:'text' },
    { id:'details', tag:'details', label:'折叠区', icon:'section', color:'container' },
    { id:'summary', tag:'summary', label:'折叠标题', icon:'text', color:'text' },
    // 排版文本
    { id:'blockquote', tag:'blockquote', label:'引用', icon:'text', color:'text' },
    { id:'code', tag:'code', label:'代码', icon:'text', color:'text' },
    { id:'pre', tag:'pre', label:'预格式化', icon:'text', color:'text' },
    { id:'mark', tag:'mark', label:'标记', icon:'text', color:'text' },
    { id:'small', tag:'small', label:'小字', icon:'text', color:'text' },
    { id:'sub', tag:'sub', label:'下标', icon:'text', color:'text' },
    { id:'sup', tag:'sup', label:'上标', icon:'text', color:'text' },
    { id:'time', tag:'time', label:'时间', icon:'text', color:'text',
      extraAttrs:[{name:'datetime',label:'时间值'}] },
    { id:'hr', tag:'hr', label:'分隔线', icon:'text', color:'text', canHaveChildren:false, selfClosing:true },
    // 定义列表
    { id:'dl', tag:'dl', label:'定义列表', icon:'ul', color:'list' },
    { id:'dt', tag:'dt', label:'术语', icon:'li', color:'list' },
    { id:'dd', tag:'dd', label:'描述', icon:'li', color:'list' },
    // 媒体扩展
    { id:'audio', tag:'audio', label:'音频', icon:'media', color:'media',
      extraAttrs:[{name:'src',label:'音频地址'},{name:'controls',label:'控制条',type:'select',options:['','controls']}] },
    { id:'source', tag:'source', label:'媒体源', icon:'media', color:'media', canHaveChildren:false, selfClosing:true,
      extraAttrs:[{name:'src',label:'地址'},{name:'type',label:'类型'}] },
    { id:'iframe', tag:'iframe', label:'内嵌网页', icon:'media', color:'media', canHaveChildren:false,
      extraAttrs:[{name:'src',label:'地址'},{name:'width',label:'宽'},{name:'height',label:'高'}] },
    // 表单扩展
    { id:'select', tag:'select', label:'下拉框', icon:'form', color:'form' },
    { id:'option', tag:'option', label:'选项', icon:'form', color:'form',
      extraAttrs:[{name:'value',label:'值'}] },
    { id:'optgroup', tag:'optgroup', label:'选项组', icon:'form', color:'form',
      extraAttrs:[{name:'label',label:'组名'}] },
    { id:'fieldset', tag:'fieldset', label:'表单分组', icon:'form', color:'form' },
    { id:'legend', tag:'legend', label:'分组标题', icon:'form', color:'form' },
    { id:'datalist', tag:'datalist', label:'候选列表', icon:'form', color:'form' },
    { id:'output', tag:'output', label:'输出', icon:'form', color:'form' },
    { id:'progress', tag:'progress', label:'进度条', icon:'form', color:'form', canHaveChildren:false,
      extraAttrs:[{name:'value',label:'当前值'},{name:'max',label:'最大值'}] },
  ];

  // 可直接编辑文本内容的标签
  const TEXT_TAGS = ['p','span','h1','h2','h3','a','button','strong','em','label','li','textarea',
    'figcaption','summary','blockquote','code','pre','mark','small','sub','sup','time','dt','dd','option','legend','caption','figcaption'];

  // 样式属性 schema（右键菜单与编译共用）
  const STYLE_SCHEMA = [
    { key:'width', label:'宽', type:'size' },
    { key:'height', label:'高', type:'size' },
    { key:'color', label:'文字颜色', type:'color' },
    { key:'background', label:'背景色', type:'color' },
    { key:'fontSize', label:'字号', type:'size' },
    { key:'margin', label:'外边距', type:'size' },
    { key:'padding', label:'内边距', type:'size' },
    { key:'opacity', label:'透明度', type:'text' },
    { key:'border', label:'边框', type:'text' },
    { key:'radius', label:'圆角', type:'size' },
    { key:'display', label:'显示', type:'select', options:['','block','inline','inline-block','flex','grid','none'] },
    { key:'position', label:'定位', type:'select', options:['','static','relative','absolute','fixed','sticky'] },
  ];

  // CSS 选择器起点积木
  const selectorDefs = [
    { id:'selTag', label:'标签', icon:'tag', color:'selector', type:'selector', selectorKind:'tag' },
    { id:'selGroup', label:'组', icon:'group', color:'selector', type:'selector', selectorKind:'group' },
    { id:'selName', label:'名称', icon:'name', color:'selector', type:'selector', selectorKind:'name' },
  ];

  // 样式属性积木（CSS 页：作为选择器的子积木，直接编辑值，不靠右键）
  const stylePropDefs = [
    { id:'propWidth', label:'设置宽', icon:'width', color:'style', type:'property', propKey:'width', propType:'size' },
    { id:'propHeight', label:'设置高', icon:'height', color:'style', type:'property', propKey:'height', propType:'size' },
    { id:'propColor', label:'设置文字颜色', icon:'color', color:'style', type:'property', propKey:'color', propType:'color' },
    { id:'propBackground', label:'设置背景色', icon:'bg', color:'style', type:'property', propKey:'background', propType:'color' },
    { id:'propFontSize', label:'设置字号', icon:'font', color:'style', type:'property', propKey:'fontSize', propType:'size' },
    { id:'propMargin', label:'设置外边距', icon:'margin', color:'style', type:'property', propKey:'margin', propType:'size' },
    { id:'propPadding', label:'设置内边距', icon:'padding', color:'style', type:'property', propKey:'padding', propType:'size' },
    { id:'propOpacity', label:'设置透明度', icon:'opacity', color:'style', type:'property', propKey:'opacity', propType:'text' },
    { id:'propBorder', label:'设置边框', icon:'border', color:'style', type:'property', propKey:'border', propType:'text' },
    { id:'propRadius', label:'设置圆角', icon:'radius', color:'style', type:'property', propKey:'radius', propType:'size' },
    { id:'propDisplay', label:'设置显示', icon:'display', color:'style', type:'property', propKey:'display', propType:'select', propOptions:['block','inline','inline-block','flex','grid','none'] },
    { id:'propPosition', label:'设置定位', icon:'position', color:'style', type:'property', propKey:'position', propType:'select', propOptions:['static','relative','absolute','fixed','sticky'] },
  ];
  // 注：标签属性（src/href/alt/controls/placeholder/type 等）不再作为独立积木，
  // 全部通过标签右键菜单的「标签属性」分区编辑（见 def.extraAttrs）

  // ===== JavaScript 积木定义 =====
  // type:'js'：JS 语句积木；jsType 区分 event/statement/expression
  // params：参数列表，渲染时显示输入框，编译时用 ${name} 模板替换
  // code：编译模板（带 ${param} 占位符）
  // close：如有，表示该积木为代码块（{ ... }），其子积木会被缩进生成
  // canHaveChildren：true 时允许嵌套子积木（默认 true）；设为 false 则为单行语句
  const jsDefs = [
    // 事件（顶级起点）—— 回调用 async 以支持 await 等待语句
    { id:'jsOnLoad', label:'页面加载时', icon:'event', color:'js-event', type:'js', jsType:'event',
      params:[],
      code:'window.addEventListener(\'load\', async () => {', close:'});' },
    { id:'jsOnClick', label:'点击元素时', icon:'event', color:'js-event', type:'js', jsType:'event',
      params:[{name:'selector', label:'选择器', ph:'#btn'}],
      code:'document.querySelector(\'${selector}\').addEventListener(\'click\', async () => {', close:'});' },
    { id:'jsOnInput', label:'输入时', icon:'event', color:'js-event', type:'js', jsType:'event',
      params:[{name:'selector', label:'选择器', ph:'#inp'}],
      code:'document.querySelector(\'${selector}\').addEventListener(\'input\', async () => {', close:'});' },

    // 控制
    { id:'jsIf', label:'如果', icon:'cond', color:'js-control', type:'js', jsType:'statement',
      params:[{name:'cond', label:'条件', ph:'x > 0'}],
      code:'if (${cond}) {', close:'}' },
    { id:'jsIfElse', label:'如果…否则', icon:'cond', color:'js-control', type:'js', jsType:'statement',
      params:[{name:'cond', label:'条件', ph:'x > 0'}],
      code:'if (${cond}) {', close:'} else {\n// 否则分支\n}' },
    { id:'jsFor', label:'循环 N 次', icon:'loop', color:'js-control', type:'js', jsType:'statement',
      params:[{name:'times', label:'次数', ph:'5'},{name:'var', label:'变量', ph:'i'}],
      code:'for (let ${var} = 0; ${var} < ${times}; ${var}++) {', close:'}' },
    { id:'jsWhile', label:'当…循环', icon:'loop', color:'js-control', type:'js', jsType:'statement',
      params:[{name:'cond', label:'条件', ph:'x < 10'}],
      code:'while (${cond}) {', close:'}' },
    { id:'jsBreak', label:'跳出循环', icon:'loop', color:'js-control', type:'js', jsType:'statement', canHaveChildren:false,
      params:[], code:'break;', close:'' },
    { id:'jsContinue', label:'继续下次循环', icon:'loop', color:'js-control', type:'js', jsType:'statement', canHaveChildren:false,
      params:[], code:'continue;', close:'' },
    { id:'jsTry', label:'尝试', icon:'control', color:'js-control', type:'js', jsType:'statement',
      params:[],
      code:'try {', close:'} catch (e) {\nconsole.error(e);\n}' },
    { id:'jsSwitch', label:'判断分支', icon:'cond', color:'js-control', type:'js', jsType:'statement',
      params:[{name:'expr', label:'表达式', ph:'x'}],
      code:'switch (${expr}) {', close:'}' },
    { id:'jsCase', label:'当值为', icon:'cond', color:'js-control', type:'js', jsType:'statement',
      params:[{name:'val', label:'值', ph:'1'}],
      code:'case ${val}:', close:'break;' },
    { id:'jsDefault', label:'默认', icon:'cond', color:'js-control', type:'js', jsType:'statement',
      params:[],
      code:'default:', close:'' },
    { id:'jsForEach', label:'遍历数组', icon:'loop', color:'js-control', type:'js', jsType:'statement',
      params:[{name:'item', label:'项变量', ph:'item'},{name:'arr', label:'数组', ph:'list'}],
      code:'${arr}.forEach((${item}) => {', close:'});' },
    { id:'jsForIn', label:'遍历对象属性', icon:'loop', color:'js-control', type:'js', jsType:'statement',
      params:[{name:'key', label:'键变量', ph:'k'},{name:'obj', label:'对象', ph:'obj'}],
      code:'for (let ${key} in ${obj}) {', close:'}' },
    { id:'jsForOf', label:'遍历可迭代对象', icon:'loop', color:'js-control', type:'js', jsType:'statement',
      params:[{name:'item', label:'项变量', ph:'item'},{name:'iter', label:'可迭代对象', ph:'arr'}],
      code:'for (let ${item} of ${iter}) {', close:'}' },
    { id:'jsTimeout', label:'延时执行', icon:'timer', color:'js-control', type:'js', jsType:'statement',
      params:[{name:'ms', label:'毫秒', ph:'1000'}],
      code:'await new Promise(r => setTimeout(() => {', close:'  r();\n}, ${ms}));' },
    // 等待 N 秒：单行 await 语句，需放在 async 事件回调内才会真正"暂停后续代码"
    { id:'jsWait', label:'等待 N 秒', icon:'timer', color:'js-control', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'seconds', label:'秒', ph:'1'}],
      code:'await new Promise(r => setTimeout(r, ${seconds} * 1000));', close:'' },

    // 变量
    { id:'jsVar', label:'声明变量', icon:'variable', color:'js-var', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'name', label:'名', ph:'x'},{name:'value', label:'值', ph:'0'}],
      code:'let ${name} = ${value};', close:'' },
    { id:'jsConst', label:'声明常量', icon:'variable', color:'js-var', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'name', label:'名', ph:'PI'},{name:'value', label:'值', ph:'3.14'}],
      code:'const ${name} = ${value};', close:'' },
    { id:'jsAssign', label:'赋值', icon:'assign', color:'js-var', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'name', label:'变量', ph:'x'},{name:'value', label:'值', ph:'1'}],
      code:'${name} = ${value};', close:'' },

    // 函数
    { id:'jsFunc', label:'定义函数', icon:'func', color:'js-func', type:'js', jsType:'statement',
      params:[{name:'name', label:'名', ph:'greet'},{name:'args', label:'参数', ph:'a, b'}],
      code:'async function ${name}(${args}) {', close:'}' },
    { id:'jsCall', label:'调用函数', icon:'func', color:'js-func', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'name', label:'名', ph:'greet'},{name:'args', label:'参数', ph:''}],
      code:'${name}(${args});', close:'' },
    { id:'jsReturn', label:'返回', icon:'returnIcon', color:'js-func', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'value', label:'值', ph:'x'}],
      code:'return ${value};', close:'' },

    // DOM 操作
    { id:'jsGetText', label:'获取文本', icon:'textIcon', color:'js-dom', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'var', label:'存入', ph:'t'},{name:'selector', label:'选择器', ph:'#el'}],
      code:'let ${var} = document.querySelector(\'${selector}\').textContent;', close:'' },
    { id:'jsSetText', label:'设置文本', icon:'textIcon', color:'js-dom', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'selector', label:'选择器', ph:'#el'},{name:'value', label:'文本', ph:'hello'}],
      code:'document.querySelector(\'${selector}\').textContent = ${value};', close:'' },
    { id:'jsSetHtml', label:'设置 HTML', icon:'textIcon', color:'js-dom', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'selector', label:'选择器', ph:'#el'},{name:'value', label:'HTML', ph:'<b>x</b>'}],
      code:'document.querySelector(\'${selector}\').innerHTML = ${value};', close:'' },
    { id:'jsSetStyle', label:'设置样式', icon:'style', color:'js-dom', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'selector', label:'选择器', ph:'#el'},{name:'prop', label:'属性', ph:'color'},{name:'value', label:'值', ph:'red'}],
      code:'document.querySelector(\'${selector}\').style.${prop} = ${value};', close:'' },
    { id:'jsAddClass', label:'添加类', icon:'style', color:'js-dom', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'selector', label:'选择器', ph:'#el'},{name:'cls', label:'类名', ph:'active'}],
      code:'document.querySelector(\'${selector}\').classList.add(\'${cls}\');', close:'' },
    { id:'jsRemoveClass', label:'移除类', icon:'style', color:'js-dom', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'selector', label:'选择器', ph:'#el'},{name:'cls', label:'类名', ph:'active'}],
      code:'document.querySelector(\'${selector}\').classList.remove(\'${cls}\');', close:'' },
    { id:'jsSetAttr', label:'设置属性', icon:'attrIcon', color:'js-dom', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'selector', label:'选择器', ph:'#el'},{name:'name', label:'属性', ph:'src'},{name:'value', label:'值', ph:'x.jpg'}],
      code:'document.querySelector(\'${selector}\').setAttribute(\'${name}\', ${value});', close:'' },

    // 输出
    { id:'jsLog', label:'打印日志', icon:'log', color:'js-output', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'value', label:'内容', ph:'hello'}],
      code:'console.log(${value});', close:'' },
    { id:'jsAlert', label:'弹窗提示', icon:'alert', color:'js-output', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'value', label:'内容', ph:'hello'}],
      code:'alert(${value});', close:'' },

    // 运算（结果存入变量）
    { id:'jsAdd', label:'加法', icon:'math', color:'js-math', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'sum'},{name:'a', label:'A', ph:'1'},{name:'b', label:'B', ph:'2'}],
      code:'let ${result} = ${a} + ${b};', close:'' },
    { id:'jsSub', label:'减法', icon:'math', color:'js-math', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'diff'},{name:'a', label:'A', ph:'5'},{name:'b', label:'B', ph:'2'}],
      code:'let ${result} = ${a} - ${b};', close:'' },
    { id:'jsMul', label:'乘法', icon:'math', color:'js-math', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'product'},{name:'a', label:'A', ph:'3'},{name:'b', label:'B', ph:'4'}],
      code:'let ${result} = ${a} * ${b};', close:'' },
    { id:'jsDiv', label:'除法', icon:'math', color:'js-math', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'quotient'},{name:'a', label:'A', ph:'10'},{name:'b', label:'B', ph:'2'}],
      code:'let ${result} = ${a} / ${b};', close:'' },
    { id:'jsMod', label:'取余', icon:'math', color:'js-math', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'remainder'},{name:'a', label:'A', ph:'10'},{name:'b', label:'B', ph:'3'}],
      code:'let ${result} = ${a} % ${b};', close:'' },
    { id:'jsMathMax', label:'最大值', icon:'math', color:'js-math', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'max'},{name:'a', label:'A', ph:'3'},{name:'b', label:'B', ph:'7'}],
      code:'let ${result} = Math.max(${a}, ${b});', close:'' },
    { id:'jsMathMin', label:'最小值', icon:'math', color:'js-math', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'min'},{name:'a', label:'A', ph:'3'},{name:'b', label:'B', ph:'7'}],
      code:'let ${result} = Math.min(${a}, ${b});', close:'' },
    { id:'jsMathRound', label:'四舍五入', icon:'math', color:'js-math', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'r'},{name:'a', label:'数值', ph:'3.14'}],
      code:'let ${result} = Math.round(${a});', close:'' },
    { id:'jsMathFloor', label:'向下取整', icon:'math', color:'js-math', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'f'},{name:'a', label:'数值', ph:'3.9'}],
      code:'let ${result} = Math.floor(${a});', close:'' },
    { id:'jsMathCeil', label:'向上取整', icon:'math', color:'js-math', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'c'},{name:'a', label:'数值', ph:'3.1'}],
      code:'let ${result} = Math.ceil(${a});', close:'' },
    { id:'jsMathRandom', label:'随机数(0-1)', icon:'math', color:'js-math', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'r'}],
      code:'let ${result} = Math.random();', close:'' },
    { id:'jsMathAbs', label:'绝对值', icon:'math', color:'js-math', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'abs'},{name:'a', label:'数值', ph:'-5'}],
      code:'let ${result} = Math.abs(${a});', close:'' },

    // 字符串
    { id:'jsStrLen', label:'字符串长度', icon:'string', color:'js-string', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'len'},{name:'str', label:'字符串', ph:'hello'}],
      code:'let ${result} = ${str}.length;', close:'' },
    { id:'jsStrUpper', label:'转大写', icon:'string', color:'js-string', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'upper'},{name:'str', label:'字符串', ph:'hello'}],
      code:'let ${result} = ${str}.toUpperCase();', close:'' },
    { id:'jsStrLower', label:'转小写', icon:'string', color:'js-string', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'lower'},{name:'str', label:'字符串', ph:'HELLO'}],
      code:'let ${result} = ${str}.toLowerCase();', close:'' },
    { id:'jsStrSub', label:'截取', icon:'string', color:'js-string', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'sub'},{name:'str', label:'字符串', ph:'hello'},{name:'start', label:'开始', ph:'0'},{name:'end', label:'结束', ph:'3'}],
      code:'let ${result} = ${str}.substring(${start}, ${end});', close:'' },
    { id:'jsStrConcat', label:'拼接', icon:'string', color:'js-string', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'joined'},{name:'a', label:'A', ph:'hello '},{name:'b', label:'B', ph:'world'}],
      code:'let ${result} = ${a} + ${b};', close:'' },
    { id:'jsStrReplace', label:'替换', icon:'string', color:'js-string', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'replaced'},{name:'str', label:'字符串', ph:'hello'},{name:'old', label:'旧', ph:'l'},{name:'new', label:'新', ph:'L'}],
      code:'let ${result} = ${str}.replace(${old}, ${new});', close:'' },
    { id:'jsStrTrim', label:'去空格', icon:'string', color:'js-string', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'trimmed'},{name:'str', label:'字符串', ph:'  hi  '}],
      code:'let ${result} = ${str}.trim();', close:'' },
    { id:'jsStrSplit', label:'分割为数组', icon:'string', color:'js-string', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'arr'},{name:'str', label:'字符串', ph:'a,b,c'},{name:'sep', label:'分隔符', ph:','}],
      code:'let ${result} = ${str}.split(${sep});', close:'' },
    { id:'jsStrToNum', label:'字符串转数字', icon:'number', color:'js-string', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'num'},{name:'str', label:'字符串', ph:'123'}],
      code:'let ${result} = Number(${str});', close:'' },
    { id:'jsNumToStr', label:'数字转字符串', icon:'number', color:'js-string', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'str'},{name:'num', label:'数字', ph:'123'}],
      code:'let ${result} = String(${num});', close:'' },

    // 数组操作
    { id:'jsArrNew', label:'创建数组', icon:'list', color:'js-array', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'name', label:'名', ph:'arr'},{name:'items', label:'元素(逗号分隔)', ph:'1, 2, 3'}],
      code:'let ${name} = [${items}];', close:'' },
    { id:'jsArrPush', label:'数组添加元素', icon:'list', color:'js-array', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'arr', label:'数组', ph:'arr'},{name:'item', label:'元素', ph:'x'}],
      code:'${arr}.push(${item});', close:'' },
    { id:'jsArrPop', label:'数组弹出末尾', icon:'list', color:'js-array', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'last'},{name:'arr', label:'数组', ph:'arr'}],
      code:'let ${result} = ${arr}.pop();', close:'' },
    { id:'jsArrLen', label:'数组长度', icon:'list', color:'js-array', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'len'},{name:'arr', label:'数组', ph:'arr'}],
      code:'let ${result} = ${arr}.length;', close:'' },
    { id:'jsArrGet', label:'读取数组元素', icon:'list', color:'js-array', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'v'},{name:'arr', label:'数组', ph:'arr'},{name:'index', label:'下标', ph:'0'}],
      code:'let ${result} = ${arr}[${index}];', close:'' },
    { id:'jsArrSet', label:'设置数组元素', icon:'list', color:'js-array', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'arr', label:'数组', ph:'arr'},{name:'index', label:'下标', ph:'0'},{name:'value', label:'值', ph:'x'}],
      code:'${arr}[${index}] = ${value};', close:'' },
    { id:'jsArrJoin', label:'数组连接为字符串', icon:'string', color:'js-array', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'s'},{name:'arr', label:'数组', ph:'arr'},{name:'sep', label:'分隔符', ph:','}],
      code:'let ${result} = ${arr}.join(${sep});', close:'' },
    { id:'jsArrSort', label:'数组排序', icon:'list', color:'js-array', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'arr', label:'数组', ph:'arr'}],
      code:'${arr}.sort();', close:'' },
    { id:'jsArrReverse', label:'数组反转', icon:'list', color:'js-array', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'arr', label:'数组', ph:'arr'}],
      code:'${arr}.reverse();', close:'' },

    // 对象操作
    { id:'jsObjNew', label:'创建对象', icon:'variable', color:'js-object', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'name', label:'名', ph:'obj'},{name:'props', label:'属性(JSON)', ph:'{"a":1}'}],
      code:'let ${name} = ${props};', close:'' },
    { id:'jsObjGet', label:'读取对象属性', icon:'variable', color:'js-object', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'v'},{name:'obj', label:'对象', ph:'obj'},{name:'key', label:'属性', ph:'a'}],
      code:'let ${result} = ${obj}.${key};', close:'' },
    { id:'jsObjSet', label:'设置对象属性', icon:'assign', color:'js-object', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'obj', label:'对象', ph:'obj'},{name:'key', label:'属性', ph:'a'},{name:'value', label:'值', ph:'1'}],
      code:'${obj}.${key} = ${value};', close:'' },
    { id:'jsObjKeys', label:'对象属性名列表', icon:'list', color:'js-object', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'keys'},{name:'obj', label:'对象', ph:'obj'}],
      code:'let ${result} = Object.keys(${obj});', close:'' },

    // JSON
    { id:'jsJsonParse', label:'JSON 转对象', icon:'variable', color:'js-json', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'obj'},{name:'str', label:'JSON 字符串', ph:'\'{"a":1}\''}],
      code:'let ${result} = JSON.parse(${str});', close:'' },
    { id:'jsJsonStringify', label:'对象转 JSON', icon:'string', color:'js-json', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'str'},{name:'obj', label:'对象', ph:'obj'}],
      code:'let ${result} = JSON.stringify(${obj});', close:'' },

    // 类型转换
    { id:'jsParseInt', label:'字符串转整数', icon:'number', color:'js-convert', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'n'},{name:'str', label:'字符串', ph:'\'42\''}],
      code:'let ${result} = parseInt(${str});', close:'' },
    { id:'jsParseFloat', label:'字符串转小数', icon:'number', color:'js-convert', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'n'},{name:'str', label:'字符串', ph:'\'3.14\''}],
      code:'let ${result} = parseFloat(${str});', close:'' },
    { id:'jsToFixed', label:'保留小数位', icon:'number', color:'js-convert', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'s'},{name:'num', label:'数字', ph:'3.14159'},{name:'digits', label:'位数', ph:'2'}],
      code:'let ${result} = (${num}).toFixed(${digits});', close:'' },
    { id:'jsTypeOf', label:'获取类型', icon:'variable', color:'js-convert', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'t'},{name:'value', label:'值', ph:'x'}],
      code:'let ${result} = typeof ${value};', close:'' },

    // 交互输入
    { id:'jsPrompt', label:'输入框', icon:'alert', color:'js-output', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'name'},{name:'msg', label:'提示', ph:'请输入名字'}],
      code:'let ${result} = prompt(${msg});', close:'' },
    { id:'jsConfirm', label:'确认框', icon:'alert', color:'js-output', type:'js', jsType:'statement', canHaveChildren:false,
      params:[{name:'result', label:'存入', ph:'ok'},{name:'msg', label:'提示', ph:'确定吗？'}],
      code:'let ${result} = confirm(${msg});', close:'' },
  ];

  // ===== 分类（palette）=====
  // HTML 页：标签分类（标签属性全部通过右键菜单编辑，不再单独提供属性积木）
  const htmlCategories = [
    { id:'container', label:'容器', icon:'container', color:'container', items: tagDefs.filter(t=>t.color==='container') },
    { id:'text', label:'文本', icon:'text', color:'text', items: tagDefs.filter(t=>t.color==='text') },
    { id:'media', label:'媒体', icon:'media', color:'media', items: tagDefs.filter(t=>t.color==='media') },
    { id:'form', label:'表单', icon:'form', color:'form', items: tagDefs.filter(t=>t.color==='form') },
    { id:'list', label:'列表', icon:'list', color:'list', items: tagDefs.filter(t=>t.color==='list') },
    { id:'table', label:'表格', icon:'table', color:'table', items: tagDefs.filter(t=>t.color==='table') },
  ];

  // CSS 页：选择器起点 + 样式属性积木（多个顶级块互相独立）
  const cssCategories = [
    { id:'selector', label:'起点（选择器）', icon:'selector', color:'selector', items: selectorDefs },
    { id:'style', label:'样式属性（拖入选择器）', icon:'style', color:'style', items: stylePropDefs },
  ];

  // JS 页：事件起点 + 控制流 + 变量 + 函数 + DOM + 输出
  const jsCategories = [
    { id:'js-event', label:'事件（顶级起点）', icon:'event', color:'js-event', items: jsDefs.filter(d=>d.color==='js-event') },
    { id:'js-control', label:'控制流', icon:'control', color:'js-control', items: jsDefs.filter(d=>d.color==='js-control') },
    { id:'js-var', label:'变量', icon:'variable', color:'js-var', items: jsDefs.filter(d=>d.color==='js-var') },
    { id:'js-func', label:'函数', icon:'func', color:'js-func', items: jsDefs.filter(d=>d.color==='js-func') },
    { id:'js-dom', label:'DOM 操作', icon:'dom', color:'js-dom', items: jsDefs.filter(d=>d.color==='js-dom') },
    { id:'js-output', label:'输出/交互', icon:'log', color:'js-output', items: jsDefs.filter(d=>d.color==='js-output') },
    { id:'js-math', label:'运算', icon:'math', color:'js-math', items: jsDefs.filter(d=>d.color==='js-math') },
    { id:'js-string', label:'字符串', icon:'string', color:'js-string', items: jsDefs.filter(d=>d.color==='js-string') },
    { id:'js-array', label:'数组', icon:'list', color:'js-array', items: jsDefs.filter(d=>d.color==='js-array') },
    { id:'js-object', label:'对象', icon:'variable', color:'js-object', items: jsDefs.filter(d=>d.color==='js-object') },
    { id:'js-json', label:'JSON', icon:'variable', color:'js-json', items: jsDefs.filter(d=>d.color==='js-json') },
    { id:'js-convert', label:'类型转换', icon:'number', color:'js-convert', items: jsDefs.filter(d=>d.color==='js-convert') },
  ];

  const COLOR_MAP = {
    container:'var(--c-container)', text:'var(--c-text)', media:'var(--c-media)',
    form:'var(--c-form)', list:'var(--c-list)', table:'var(--c-table)',
    style:'var(--c-style)', selector:'var(--c-selector)', attr:'var(--c-attr)',
    'js-event':'var(--c-js-event)', 'js-control':'var(--c-js-control)',
    'js-var':'var(--c-js-var)', 'js-func':'var(--c-js-func)',
    'js-dom':'var(--c-js-dom)', 'js-output':'var(--c-js-output)',
    'js-math':'var(--c-js-math)', 'js-string':'var(--c-js-string)',
    'js-array':'var(--c-js-array)', 'js-object':'var(--c-js-object)',
    'js-json':'var(--c-js-json)', 'js-convert':'var(--c-js-convert)',
  };

  const all = {};
  [...tagDefs, ...selectorDefs, ...stylePropDefs, ...jsDefs].forEach(d => all[d.id] = d);
  // 标记 type
  tagDefs.forEach(d => d.type = 'tag');
  selectorDefs.forEach(d => d.type = 'selector');

  return {
    tagDefs, selectorDefs, stylePropDefs, jsDefs, STYLE_SCHEMA, TEXT_TAGS,
    htmlCategories, cssCategories, jsCategories,
    colorMap: COLOR_MAP,
    get(id){ return all[id]; },
    colorOf(def){ return COLOR_MAP[def.color] || 'var(--accent)'; },
    isTextTag(tag){ return TEXT_TAGS.includes(tag); },
  };
})();
