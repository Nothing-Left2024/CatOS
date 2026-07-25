// 自绘 SVG 图标系统（无外部引用）
// 统一线性风格，16x16 viewBox，使用 currentColor
const Icons = (() => {
  const svg = (paths, size = 16, vb = '0 0 16 16') =>
    `<svg viewBox="${vb}" width="${size}" height="${size}" fill="currentColor" xmlns="http://www.w3.org/2000/svg">${paths}</svg>`;

  const set = {
    brand: svg('<path d="M3 1h18l-1.6 18L12 21 4.6 19z" fill="#e34f26"/><path d="M12 19.5V2.5h8.4L19 17.5z" fill="#ef652a"/><path d="M7 6h10M7.5 9H17M16.5 12.5L12 14l-4.5-1.5-.4-3.5" stroke="#fff" stroke-width="1" fill="none" stroke-linecap="round" stroke-linejoin="round"/>', 24, '0 0 24 24'),
    html5: svg('<path d="M3 1h18l-1.6 18L12 21 4.6 19z" fill="#e34f26"/><path d="M12 19.5V2.5h8.4L19 17.5z" fill="#ef652a"/><path d="M7 6h10M7.5 9H17M16.5 12.5L12 14l-4.5-1.5-.4-3.5" stroke="#fff" stroke-width="1" fill="none" stroke-linecap="round" stroke-linejoin="round"/>', 24, '0 0 24 24'),
    container: svg('<rect x="2" y="3" width="12" height="10" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.4"/><rect x="2" y="3" width="12" height="3" fill="currentColor" opacity=".4"/>'),
    text: svg('<path d="M3 4h10M8 4v9" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>'),
    media: svg('<rect x="2" y="3" width="12" height="10" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.4"/><circle cx="6" cy="7" r="1.2" fill="currentColor"/><path d="M3 12l3-3 2 2 3-4 2 5z" fill="currentColor"/>'),
    form: svg('<rect x="2" y="4" width="12" height="8" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M4 8h6" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>'),
    list: svg('<circle cx="3.5" cy="4" r="1" fill="currentColor"/><circle cx="3.5" cy="8" r="1" fill="currentColor"/><circle cx="3.5" cy="12" r="1" fill="currentColor"/><path d="M6 4h7M6 8h7M6 12h7" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>'),
    table: svg('<rect x="2" y="3" width="12" height="10" rx="1" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M2 7h12M2 10h12M6 3v10M10 3v10" stroke="currentColor" stroke-width="1.2"/>'),
    style: svg('<path d="M3 13l2-7h6l2 7z" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linejoin="round"/><circle cx="8" cy="3" r="1.2" fill="currentColor"/>'),
    selector: svg('<path d="M4 4l8 8M4 12l8-8" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/><circle cx="8" cy="8" r="2" fill="none" stroke="currentColor" stroke-width="1.3"/>'),
    // 标签类
    div: svg('<rect x="2" y="2" width="12" height="12" rx="2" fill="none" stroke="currentColor" stroke-width="1.4"/><rect x="5" y="5" width="6" height="6" fill="currentColor" opacity=".35"/>'),
    section: svg('<rect x="2" y="2" width="12" height="12" rx="2" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M2 6h12" stroke="currentColor" stroke-width="1.4"/>'),
    p: svg('<path d="M3 3h7a3 3 0 010 6H5v4H3z" fill="currentColor"/>'),
    span: svg('<rect x="3" y="6" width="10" height="4" rx="1" fill="currentColor"/>'),
    h: svg('<path d="M3 3v10M3 8h7M10 3v10" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/>'),
    a: svg('<path d="M6 10a3 3 0 010-4l2-2a3 3 0 014 4l-1 1" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/><path d="M10 6a3 3 0 010 4l-2 2a3 3 0 01-4-4l1-1" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>'),
    img: svg('<rect x="2" y="3" width="12" height="10" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.4"/><circle cx="6" cy="7" r="1.2" fill="currentColor"/><path d="M3 12l3-3 2 2 3-4 2 5z" fill="currentColor"/>'),
    input: svg('<rect x="2" y="5" width="12" height="6" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M5 8h2" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>'),
    button: svg('<rect x="2" y="5" width="12" height="6" rx="3" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M6 8h4" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>'),
    ul: svg('<circle cx="3.5" cy="5" r="1" fill="currentColor"/><circle cx="3.5" cy="9" r="1" fill="currentColor"/><circle cx="3.5" cy="13" r="1" fill="currentColor"/><path d="M6 5h7M6 9h7M6 13h7" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>'),
    li: svg('<circle cx="4" cy="8" r="1.5" fill="currentColor"/><path d="M7 5h6M7 8h6M7 11h6" stroke="currentColor" stroke-width="1.3" stroke-linecap="round"/>'),
    // 样式积木
    width: svg('<path d="M2 8h12M2 5v6M14 5v6" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>'),
    height: svg('<path d="M8 2v12M5 2h6M5 14h6" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>'),
    color: svg('<circle cx="8" cy="8" r="5" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M8 3a5 5 0 000 10z" fill="currentColor"/>'),
    bg: svg('<rect x="2" y="3" width="12" height="10" rx="1.5" fill="currentColor" opacity=".35"/><rect x="2" y="3" width="12" height="10" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.4"/>'),
    margin: svg('<rect x="2" y="2" width="12" height="12" fill="none" stroke="currentColor" stroke-width="1.4" stroke-dasharray="2 1"/><rect x="5" y="5" width="6" height="6" fill="currentColor" opacity=".35"/>'),
    padding: svg('<rect x="2" y="2" width="12" height="12" rx="1" fill="none" stroke="currentColor" stroke-width="1.4"/><rect x="5" y="5" width="6" height="6" rx="1" fill="currentColor" opacity=".35"/>'),
    font: svg('<path d="M3 13l3-10h4l3 10M4.5 9h7" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" fill="none" stroke-linejoin="round"/>'),
    opacity: svg('<circle cx="8" cy="8" r="5" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M8 3a5 5 0 000 10z" fill="currentColor"/>'),
    hover: svg('<rect x="2" y="2" width="12" height="12" rx="2" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M5 10l2-2 1 1 3-3" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'),
    access: svg('<circle cx="8" cy="4" r="1.6" fill="currentColor"/><path d="M4 7h8M8 8v4M5 12l3-2 3 2" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" fill="none" stroke-linejoin="round"/>'),
    border: svg('<rect x="3" y="3" width="10" height="10" rx="1" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="1.5 1.5"/>'),
    radius: svg('<path d="M4 13a9 9 0 019-9" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>'),
    display: svg('<rect x="2" y="3" width="6" height="4" rx="1" fill="currentColor" opacity=".4"/><rect x="9" y="3" width="5" height="4" rx="1" fill="currentColor" opacity=".4"/><rect x="2" y="9" width="12" height="4" rx="1" fill="currentColor" opacity=".4"/>'),
    position: svg('<rect x="3" y="3" width="10" height="10" rx="1" fill="none" stroke="currentColor" stroke-width="1.3"/><rect x="6" y="6" width="4" height="4" fill="currentColor"/>'),
    textContent: svg('<path d="M3 4h10M3 7h8M3 10h10M3 13h6" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>'),
    href: svg('<path d="M6 6h5v4H6z" fill="none" stroke="currentColor" stroke-width="1.3"/><path d="M6 8H3v5h5v-3" fill="none" stroke="currentColor" stroke-width="1.4"/>'),
    src: svg('<rect x="2" y="3" width="9" height="10" rx="1" fill="none" stroke="currentColor" stroke-width="1.3"/><path d="M13 5l-2 2 2 2" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round"/>'),
    alt: svg('<path d="M3 3h10v10H3z" fill="none" stroke="currentColor" stroke-width="1.3"/><path d="M5 11l2-3 2 2 2-3" stroke="currentColor" stroke-width="1.3" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'),
    tag: svg('<path d="M4 5L1 8l3 3M12 5l3 3-3 3M9 3l-2 10" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'),
    group: svg('<circle cx="5" cy="6" r="2" fill="currentColor" opacity=".5"/><circle cx="11" cy="6" r="2" fill="currentColor"/><circle cx="8" cy="11" r="2" fill="currentColor" opacity=".7"/>'),
    name: svg('<path d="M3 8h10M3 8a2 2 0 002-2V4M3 8a2 2 0 002 2v2" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>'),
    refresh: svg('<path d="M12 4a5 5 0 102 4" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/><path d="M12 2v3h3" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'),
    export: svg('<path d="M8 2v8M5 6l3 3 3-3M3 13h10" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'),
    undo: svg('<path d="M6 4L3 7l3 3" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/><path d="M3 7h7a3 3 0 010 6H7" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>'),
    redo: svg('<path d="M10 4l3 3-3 3" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/><path d="M13 7H6a3 3 0 000 6h3" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>'),
    trash: svg('<path d="M3 5h10M6 5V3h4v2M5 5l1 9h4l1-9" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'),
    close: svg('<path d="M4 4l8 8M12 4l-8 8" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/>'),
    // ===== JavaScript 积木图标 =====
    event: svg('<path d="M8 2v3M8 2a6 6 0 100 12A6 6 0 008 2zM2 8h12" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round"/><circle cx="8" cy="8" r="1.5" fill="currentColor"/>'),
    control: svg('<path d="M3 4h10v8H3z" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M6 8h4M8 6v4" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>'),
	// Made by Nothing-Left2024 Apache 2.0 Licence
    variable: svg('<rect x="3" y="4" width="10" height="8" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M5 8h6" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>'),
    func: svg('<path d="M3 3v10M13 3v10M3 8h10" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/><circle cx="8" cy="8" r="1.5" fill="currentColor"/>'),
    log: svg('<rect x="2" y="3" width="12" height="10" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M4 7l2 2 2-2M4 11h4M9 7h3M9 11h3" stroke="currentColor" stroke-width="1.3" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'),
    alert: svg('<path d="M8 2L2 13h12L8 2z" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linejoin="round"/><path d="M8 7v3M8 11.5v.5" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>'),
    dom: svg('<rect x="2" y="3" width="12" height="10" rx="1" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M2 6h12M6 6v7" stroke="currentColor" stroke-width="1.3"/>'),
    math: svg('<path d="M3 8h10M8 3v10" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/><path d="M11 11l2 2M11 5l2-2M3 11l-1 1M3 5L2 4" stroke="currentColor" stroke-width="1.3" stroke-linecap="round"/>'),
    timer: svg('<circle cx="8" cy="9" r="5" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M8 9V6M8 9l2 1M6 2h4" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" fill="none"/>'),
    cond: svg('<path d="M5 3l5 5-5 5" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/><path d="M11 3v10" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>'),
    loop: svg('<path d="M3 8a5 5 0 015-5 5 5 0 015 5 5 5 0 01-5 5" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/><path d="M11 11l2 2-2 2" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>'),
    returnIcon: svg('<path d="M2 8h8M7 5l3 3-3 3" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/><path d="M12 4v8" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>'),
    assign: svg('<path d="M3 8h7M7 5l3 3-3 3" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>'),
    textIcon: svg('<path d="M3 4h10M8 4v9" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>'),
    attrIcon: svg('<rect x="3" y="3" width="10" height="10" rx="1" fill="none" stroke="currentColor" stroke-width="1.3"/><path d="M5 8h6" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>'),
    string: svg('<path d="M5 3L3 6v4l2 3M11 3l2 3-2 3M7 8h2" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'),
    number: svg('<path d="M3 5h10M3 11h10M6 3l-1 10M11 3l-1 10" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round"/>'),
  };

  return {
    get(name, size){ return set[name] ? set[name] : set.tag; },
    set,
    inject(el, name){ if(el) el.innerHTML = this.get(name); }
  };
})();
