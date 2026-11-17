/* Plain file:// shell. No filesystem access: all operations change the mock's arrays. */
(() => {
  'use strict';
  const paths = {
    menu: '<path d="M4 6h16M4 12h16M4 18h16"/>',
    back: '<path d="m14 6-6 6 6 6"/>', forward: '<path d="m10 6 6 6-6 6"/>',
    down: '<path d="m6 9 6 6 6-6"/>', up: '<path d="m6 15 6-6 6 6"/>',
    search: '<circle cx="11" cy="11" r="7"/><path d="m16 16 4 4"/>',
    grid: '<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M9 3v18M15 3v18M3 9h18M3 15h18"/>',
    list: '<path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01"/>',
    clock: '<circle cx="12" cy="12" r="9"/><path d="M12 6v6l4 2"/>',
    star: '<path d="m12 3 2.8 5.7 6.2.9-4.5 4.4 1.1 6.2-5.6-3-5.6 3 1.1-6.2L3 9.6l6.2-.9Z"/>',
    home: '<path d="m3 10 9-7 9 7v10a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1Z M9 21V12h6v9"/>',
    monitor: '<rect x="3" y="4" width="18" height="13" rx="1"/><path d="M8 21h8M12 17v4"/>',
    file: '<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8Z M14 2v6h6M8 12h8M8 16h6"/>',
    download: '<path d="M12 3v12m-5-5 5 5 5-5M4 15v5h16v-5"/>',
    music: '<path d="M9 18V5l12-2v13M9 9l12-2"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/>',
    image: '<rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="8.5" cy="8.5" r="1.5"/><path d="m21 15-5-5L5 21"/>',
    video: '<rect x="3" y="5" width="12" height="14" rx="2"/><path d="m15 10 6-4v12l-6-4"/>',
    trash: '<path d="M3 6h18M9 6V3h6v3M5 6l1 15h12l1-15M10 10v7M14 10v7"/>',
    folder: '<path d="M3 7V5a2 2 0 0 1 2-2h5l2 3h7a2 2 0 0 1 2 2v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z"/>',
    drive: '<rect x="2" y="3" width="20" height="7" rx="2"/><rect x="2" y="14" width="20" height="7" rx="2"/><path d="M6 6.5h.01M6 17.5h.01"/>',
    plus: '<path d="M12 5v14M5 12h14"/>', close: '<path d="m6 6 12 12M6 18 18 6"/>',
    minimize: '<path d="M5 16h14"/>', maximize: '<rect x="6" y="6" width="12" height="12" rx=".5"/>',
    eject: '<path d="m5 14 7-9 7 9ZM5 19h14"/>',
    open: '<path d="M3 9V6a2 2 0 0 1 2-2h5l2 3h7a2 2 0 0 1 2 2M3 9h18l-3 11H2Z"/>',
    external: '<path d="M15 3h6v6M10 14 21 3M10 3H4a1 1 0 0 0-1 1v16a1 1 0 0 0 1 1h16a1 1 0 0 0 1-1v-6"/>',
    cut: '<circle cx="6" cy="6" r="3"/><circle cx="6" cy="18" r="3"/><path d="m8 8 13 13M8 16 21 3"/>',
    copy: '<rect x="8" y="8" width="13" height="13" rx="2"/><path d="M16 8V3H3v13h5"/>',
    edit: '<path d="m16 3 5 5-12 12-6 1 1-6ZM14 5l5 5"/>',
    link: '<path d="m10 13 4-4M8 16l-2 2a4 4 0 0 1-6-6l4-4a4 4 0 0 1 6 0M16 8l2-2a4 4 0 0 1 6 6l-4 4a4 4 0 0 1-6 0" transform="translate(1 0) scale(.9)"/>',
    info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7h.01"/>',
    check: '<path d="m5 12 4 4L19 6"/>', terminal: '<path d="m4 6 6 6-6 6M12 18h8"/>',
    archive: '<rect x="3" y="3" width="18" height="5" rx="1"/><path d="M5 8v13h14V8M10 12h4"/>',
    sheet: '<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M9 3v18M15 3v18M3 9h18M3 15h18"/>'
  };
  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const icon = (name, size = 16) => `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${paths[name] || paths.file}</svg>`;
  const bytes = n => typeof n !== 'number' ? (n || '') : n < 1024 ? `${n} bytes` : n < 1048576 ? `${(n / 1024).toFixed(1)} kB` : n < 1073741824 ? `${(n / 1048576).toFixed(1)} MB` : `${(n / 1073741824).toFixed(1)} GB`;
  const type = f => {
    const ext = f.name.split('.').pop().toLowerCase();
    if (f.kind === 'folder') return { label:'Folder', icon:'folder', color:'var(--accent)' };
    if (/^(png|jpg|jpeg|webp|gif|svg)$/.test(ext)) return {label:'Image',icon:'image',color:'var(--success)'};
    if (/^(zip|7z|rar)$/.test(ext)) return {label:'Archive',icon:'archive',color:'var(--warn)'};
    if (/^(pdf|doc|docx|pptx)$/.test(ext)) return {label:ext === 'pdf' ? 'PDF Document' : 'Document',icon:'file',color:'var(--danger)'};
    if (/^(xlsx|csv)$/.test(ext)) return {label:'Spreadsheet',icon:'sheet',color:'var(--success)'};
    return {label:ext === 'exe' ? 'Application' : ext === 'fbx' ? '3D Model' : 'File',icon:'file',color:'var(--muted)'};
  };
  function fileIcon(f, small = false) {
    const t = type(f);
    if (small) return `<span class="dio-file-icon" style="color:${t.color}">${icon(t.icon)}</span>`;
    if (f.kind === 'folder') return '<svg class="dio-file-icon" viewBox="0 0 64 64" aria-hidden="true"><path d="M3 16a6 6 0 0 1 6-6h12a6 6 0 0 1 5 3l2 2h27a6 6 0 0 1 6 6v28a6 6 0 0 1-6 6H9a6 6 0 0 1-6-6Z" fill="var(--folder-back)"/><path d="M3 23h58v26a6 6 0 0 1-6 6H9a6 6 0 0 1-6-6Z" fill="var(--accent)"/></svg>';
    if (f.thumbnail) return `<img class="dio-file-icon" src="${esc(f.thumbnail)}" alt="" style="object-fit:contain;border-radius:6px">`;
    const ext = f.name.includes('.') ? f.name.split('.').pop().slice(0,4).toUpperCase() : 'FILE';
    return `<svg class="dio-file-icon" viewBox="0 0 64 64" aria-hidden="true"><path d="M10 3h28l16 16v42H10Z" fill="var(--overlay)" stroke="var(--paper-edge)" stroke-width="2"/><path d="M38 3v16h16Z" fill="${t.color}" fill-opacity=".24" stroke="var(--paper-edge)" stroke-width="1.75"/><path d="M19 22h26" stroke="var(--paper-edge)" stroke-width="2"/><text x="32" y="45" fill="${t.color}" text-anchor="middle" font-family="Inter,sans-serif" font-weight="500" font-size="10">${esc(ext)}</text></svg>`;
  }
  const places = [['Recent','clock'],['Starred','star'],['Home','home'],['Desktop','monitor'],['Documents','file'],['Downloads','download'],['Music','music'],['Pictures','image'],['Videos','video'],['Trash','trash']];
  function create(target, options = {}) {
    const host = typeof target === 'string' ? document.querySelector(target) : target;
    if (!host) throw new Error('DioShell.create needs an existing element.');
    const folders = new Map();
    let current = '', files = [], selection = new Set(), anchor = 0, view = 'grid', zoom = 3;
    let history = [], historyIndex = -1, toastTimer, popup = null, restoreFocus = null, search = '';
    const listeners = new AbortController();
    const signal = listeners.signal;
    host.innerHTML = `<div class="dio-try-strip"><p></p><span class="dio-step"></span></div><section class="dio-window" aria-label="Dio File Manager"><div class="dio-sidebar-header"><button class="dio-icon-button" data-action="main-menu" aria-label="Main menu">${icon('menu')}</button></div><header class="dio-header"><button class="dio-icon-button" data-action="back" aria-label="Back">${icon('back')}</button><button class="dio-icon-button" data-action="forward" aria-label="Forward">${icon('forward')}</button><nav class="dio-path" aria-label="Folder path"></nav><button class="dio-icon-button" data-action="search" aria-label="Search">${icon('search',20)}</button><button class="dio-icon-button dio-view-button" data-action="view" aria-label="View options">${icon('grid')}${icon('down',12)}</button><div class="dio-window-buttons"><button class="dio-window-button" data-action="minimize" aria-label="Minimize">${icon('minimize')}</button><button class="dio-window-button" data-action="maximize" aria-label="Maximize">${icon('maximize')}</button><button class="dio-window-button" data-action="close-window" aria-label="Close window">${icon('close')}</button></div></header><aside class="dio-sidebar" aria-label="Places" data-scroll="list"></aside><main class="dio-content"><div class="dio-files" tabindex="0" aria-label="Files" data-scroll="list"></div><div class="dio-status" role="status" hidden></div></main></section>`;
    const win = host.querySelector('.dio-window'), sidebar = host.querySelector('.dio-sidebar'), content = host.querySelector('.dio-content'), fileArea = host.querySelector('.dio-files'), pathbar = host.querySelector('.dio-path');
    const button = action => host.querySelector(`[data-action="${action}"]`);
    const selected = () => files.filter(f => selection.has(f.name));
    const visibleFiles = () => files.filter(f => f.name.toLocaleLowerCase().includes(search.toLocaleLowerCase()));
    function sidebarRender() {
      const added = (options.addedFolders || ['Reports','Holiday Photos']).map(name => [name,'folder']);
      const drives = options.drives || [{name:'Local Disk (C:)',used:84},{name:'Shared (D:)',used:44},{name:'Local Disk (E:)',used:88},{name:'Local Disk (R:)',used:88}];
      sidebar.innerHTML = [...places,...added].map(([name,i]) => `<button class="dio-nav" data-folder="${esc(name)}" ${current.endsWith(name) ? 'aria-current="page"' : ''}>${icon(i)}<span class="dio-nav-label">${esc(name)}</span>${name === 'Trash' && options.trashCount ? `<span class="dio-badge">${esc(options.trashCount)}</span>` : ''}</button>`).join('') + '<hr>' + drives.map(d => `<div class="dio-nav" data-folder="${esc(d.name)}" role="button" tabindex="0">${icon('drive')}<span class="dio-nav-label">${esc(d.name)}${d.used == null ? '' : `<span class="dio-drive-meter" data-full="${d.used > 90}" style="--used:${Math.max(0,Math.min(100,d.used))}%"><i></i></span>`}</span>${d.removable ? `<button class="dio-eject" aria-label="Eject ${esc(d.name)}">${icon('eject')}</button>` : ''}</div>`).join('') + `<button class="dio-nav" data-folder="Other Locations">${icon('plus')}<span>Other Locations</span></button>`;
    }
    function renderPath() {
      button('back').disabled = historyIndex <= 0;
      button('forward').disabled = historyIndex >= history.length - 1;
      const parts = current.split(/[\\/]/).filter(Boolean);
      pathbar.innerHTML = parts.map((part,i) => `${i ? `<span class="dio-chevron">${icon('forward',12)}</span>` : ''}<button class="dio-crumb" data-path="${esc(parts.slice(0,i+1).join('\\'))}">${i === 0 && part === 'Home' ? icon('home') : ''}${esc(part)}</button>`).join('');
    }
    function renderFiles() {
      const shown = visibleFiles();
      const measure=document.createElement('canvas').getContext('2d');
      const label = f => {
        let text=esc(f.name);
        if(view==='grid') {
          measure.font=`13px ${/[\u0600-\u06ff]/.test(f.name)?'Vazirmatn':'Inter'},sans-serif`;
          const edge=zoom<=3?32+(zoom-2)*32:zoom<=4?64+(zoom-3)*32:96+(zoom-4)*48;
          const cell=Math.max(80,edge+48),available=fileArea.clientWidth-24,columns=Math.max(1,Math.floor(available/cell));
          const width=Math.floor(available/columns)-20;
          const split=value=>{const chars=Array.from(value);let n=1;while(n<chars.length&&measure.measureText(chars.slice(0,n+1).join('')).width<=width)n++;return [chars.slice(0,n).join(''),chars.slice(n).join('')];};
          let value=f.name,lines=split(value),keep=Array.from(f.name).length;
          while(measure.measureText(lines[1]).width>width&&keep>4){keep--;const chars=Array.from(f.name);value=chars.slice(0,Math.ceil(keep/2)).join('')+'…'+chars.slice(-Math.floor(keep/2)).join('');lines=split(value);}
          text=lines.map(esc).filter(Boolean).join('<br>');
        }
        return `<span class="dio-file-name ${/[\u0600-\u06ff]/.test(f.name) ? 'dio-name-fa' : ''}" dir="auto">${text}</span>`;
      };
      if (!shown.length) fileArea.innerHTML = `<div class="dio-empty">${icon(search ? 'search' : 'folder',64)}<span>${search ? 'No Results Found' : 'Folder is Empty'}</span></div>`;
      else if (view === 'details') fileArea.innerHTML = `<table class="dio-list" aria-label="Folder contents"><thead><tr>${['Name','Size','Modified','Type'].map((s,i)=>`<th><button data-sort="${s.toLowerCase()}">${s}${icon(i ? 'down' : 'up',12)}</button></th>`).join('')}</tr></thead><tbody>${shown.map(f=>`<tr data-name="${esc(f.name)}" aria-selected="${selection.has(f.name)}" tabindex="-1"><td><span class="dio-list-name">${fileIcon(f,true)}${label(f)}</span></td><td>${esc(bytes(f.size))}</td><td>${esc(f.date || '')}</td><td>${esc(type(f).label)}</td></tr>`).join('')}</tbody></table>`;
      else if(view === 'list') fileArea.innerHTML = `<div class="dio-columns" role="listbox" aria-label="Folder contents" aria-multiselectable="true">${shown.map(f=>`<button class="dio-compact-item" role="option" data-name="${esc(f.name)}" aria-selected="${selection.has(f.name)}" aria-label="${esc(f.name)}">${fileIcon(f,true)}${label(f)}</button>`).join('')}</div>`;
      else fileArea.innerHTML = `<div class="dio-grid" role="listbox" aria-label="Folder contents" aria-multiselectable="true">${shown.map(f=>`<button class="dio-item" role="option" data-name="${esc(f.name)}" aria-selected="${selection.has(f.name)}" aria-label="${esc(f.name)}"><span class="dio-icon-box">${fileIcon(f)}</span>${label(f)}</button>`).join('')}</div>`;
      fileArea.style.overflowX=view==='list'?'auto':'hidden';
      if (view === 'grid') {
        const edge = zoom <= 3 ? 32 + (zoom-2)*32 : zoom <= 4 ? 64+(zoom-3)*32 : 96+(zoom-4)*48;
        fileArea.querySelectorAll('.dio-file-icon').forEach(el => {el.style.width = `${edge}px`; el.style.height = `${edge}px`;});
        const grid = fileArea.querySelector('.dio-grid');
        if (grid && zoom !== 3) {
          grid.style.gridTemplateColumns = `repeat(auto-fill,minmax(${Math.max(80,edge+48)}px,1fr))`;
          grid.querySelectorAll('.dio-item').forEach(el => {el.style.height=`${Math.max(96,edge+64)}px`; el.querySelector('.dio-icon-box').style.width=`${edge+16}px`; el.querySelector('.dio-icon-box').style.height=`${edge+16}px`;});
        }
      }
      updateSelection();
    }
    function updateSelection() {
      fileArea.querySelectorAll('[data-name]').forEach(el => el.setAttribute('aria-selected',selection.has(el.dataset.name)));
      const status = content.querySelector('.dio-status'), items = selected();
      status.hidden = !items.length;
      const total = items.every(f=>typeof f.size === 'number') ? bytes(items.reduce((n,f)=>n+f.size,0)) : items.length === 1 ? bytes(items[0].size) : '';
      const name = items[0]?.name || '';
      status.textContent = items.length === 1 ? `“${name.length > 64 ? name.slice(0,32)+'…'+name.slice(-24) : name}” selected${total ? ` (${total})` : ''}` : `${items.length} items selected${total ? ` (${total})` : ''}`;
      host.dispatchEvent(new CustomEvent('dio:select',{detail:items}));
    }
    function setFiles(next) {
      if (!Array.isArray(next) || next.some(f=> !f || typeof f.name !== 'string')) throw new TypeError('Files must be an array with a string name on every item.');
      if (new Set(next.map(f=>f.name)).size !== next.length) throw new TypeError('File names must be unique in a folder.');
      files = next.map(f=>({...f})); folders.set(current, files);
      selection = new Set([...selection].filter(n=>files.some(f=>f.name === n)));
      renderFiles();
    }
    function setFolder(path, next, record = true) {
      if (typeof path !== 'string' || !path.trim()) throw new TypeError('Folder path must be a nonempty string.');
      closePopup(); current = path; search = ''; selection.clear();
      if (record && history[historyIndex] !== path) { history = history.slice(0,historyIndex+1); history.push(path); historyIndex++; }
      setFiles(next ?? folders.get(path) ?? []); sidebarRender(); renderPath(); fileArea.scrollTop = 0;
      if(history.length>1){fileArea.classList.remove('dio-appear'); void fileArea.offsetWidth; fileArea.classList.add('dio-appear');}
      host.dispatchEvent(new CustomEvent('dio:folder',{detail:{path,files}}));
    }
    function select(names) { selection = new Set((Array.isArray(names) ? names : [names]).filter(n=>files.some(f=>f.name === n))); updateSelection(); }
    function setView(mode) {
      view = mode === 'details' ? 'details' : mode === 'list' ? 'list' : 'grid'; zoom = view === 'details' ? 0 : view === 'list' ? 1 : Math.max(2,zoom);
      button('view').innerHTML = icon(view === 'grid' ? 'grid':'list')+icon('down',12); renderFiles();
      fileArea.classList.remove('dio-appear'); void fileArea.offsetWidth; fileArea.classList.add('dio-appear');
    }
    function closePopup() { if (popup) {popup.remove(); popup = null; if (restoreFocus?.isConnected) restoreFocus.focus({preventScroll:true}); restoreFocus = null;} }
    function placePopup(el,x,y) {
      const rect = win.getBoundingClientRect();
      el.style.left = `${Math.max(4,Math.min(x-rect.left,rect.width-el.offsetWidth-8))}px`;
      el.style.top = `${Math.max(4,Math.min(y-rect.top,rect.height-el.offsetHeight-8))}px`;
    }
    function openMenu(x,y,items) {
      closePopup(); restoreFocus = document.activeElement; popup = document.createElement('div');
      const menu = popup; menu.className = 'dio-menu dio-appear'; menu.setAttribute('role','menu');
      items.forEach(item=>{
        if (item === 'separator') {menu.append(document.createElement('hr')); return;}
        const b = document.createElement('button'); b.className='dio-menu-item'; b.setAttribute('role','menuitem'); b.disabled=!!item.disabled;
        b.innerHTML = `${icon(item.icon || 'file')}<span>${esc(item.label)}</span>${item.shortcut ? `<span class="dio-menu-shortcut">${esc(item.shortcut)}</span>` : ''}`;
        b.addEventListener('click',()=>{closePopup(); item.action?.();}); menu.append(b);
      });
      win.append(menu); placePopup(menu,x,y); menu.querySelector('button:not(:disabled)')?.focus({preventScroll:true});
      return menu;
    }
    function popover(at, {title,content:body}) {
      closePopup(); restoreFocus=document.activeElement; popup=document.createElement('section');
      popup.className='dio-popover dio-appear'; popup.setAttribute('role','dialog'); popup.setAttribute('aria-label',title || 'Options');
      if (title) {const h=document.createElement('h3'); h.textContent=title; popup.append(h);}
      if (body instanceof Node) popup.append(body); else if (body) popup.append(document.createTextNode(body));
      win.append(popup); const r=at instanceof Element ? at.getBoundingClientRect() : at;
      placePopup(popup,r.x ?? r.left,r.bottom ?? r.y); popup.querySelector('input,button,[tabindex]')?.focus({preventScroll:true}); return popup;
    }
    function dialog({title,message,actions=[{label:'Close'}]}) {
      closePopup(); restoreFocus=document.activeElement; popup=document.createElement('div'); const backdrop=popup;
      backdrop.className='dio-dialog-backdrop'; backdrop.innerHTML=`<section class="dio-dialog dio-appear" role="dialog" aria-modal="true" aria-label="${esc(title)}"><h2>${esc(title)}</h2><p>${esc(message)}</p><div class="dio-dialog-actions"></div></section>`;
      actions.forEach((a,i)=>{const b=document.createElement('button');b.className=i===actions.length-1 ? 'dio-primary':'dio-secondary'; b.textContent=a.label;b.addEventListener('click',()=>{closePopup();a.action?.();});backdrop.querySelector('.dio-dialog-actions').append(b);});
      win.append(backdrop);backdrop.querySelector('button')?.focus({preventScroll:true});return backdrop;
    }
    function dismissToast() {clearTimeout(toastTimer);content.querySelector('.dio-toast')?.remove();content.classList.remove('has-toast');}
    function toast(text,{action='Undo',onAction,duration=6000}={}) {
      dismissToast(); const el=document.createElement('div');el.className='dio-toast dio-appear';el.setAttribute('role','status');
      el.innerHTML=`<span class="dio-toast-text">${esc(text)}</span>${action ? `<button class="dio-toast-action">${esc(action)}</button>` : ''}<button class="dio-icon-button" aria-label="Dismiss notification">${icon('close',12)}</button>`;
      el.querySelector('.dio-toast-action')?.addEventListener('click',()=>{dismissToast();onAction?.();});el.querySelector('.dio-icon-button').addEventListener('click',dismissToast);
      content.append(el);content.classList.add('has-toast');if(duration>0)toastTimer=setTimeout(dismissToast,Math.max(2200,duration));return el;
    }
    function setHint(text,step=1,total=3) {host.querySelector('.dio-try-strip p').textContent=text;host.querySelector('.dio-step').textContent=`${step.toLocaleString('fa-IR')} از ${total.toLocaleString('fa-IR')}`;}
    function rename() {
      const f=selected()[0];if(!f)return;
      const body=document.createElement('div');body.innerHTML=`<input class="dio-field" aria-label="New name" value="${esc(f.name)}"><button class="dio-primary">Rename</button>`;
      const save=()=>{const value=body.querySelector('input').value.trim();if(!value)return;if(files.some(other=>other!==f && other.name===value)){toast('That name is already in use.',{action:null});return;}const old=f.name;f.name=value;select(value);renderFiles();closePopup();toast('Renamed',{onAction:()=>{f.name=old;select(old);renderFiles();}});};
      body.querySelector('button').onclick=save;body.querySelector('input').onkeydown=e=>{if(e.key==='Enter')save();};
      popover(fileArea.querySelector(`[data-name="${CSS.escape(f.name)}"]`),{title:'Rename',content:body});body.querySelector('input').select();
    }
    function trash() {
      const removed=selected();if(!removed.length)return;const before=[...files];const originalFolder=current;
      setFiles(files.filter(f=>!selection.has(f.name)));
      toast(removed.length===1 ? `“${removed[0].name}” moved to Trash` : `${removed.length} items moved to Trash`,{onAction:()=>{folders.set(originalFolder,before);if(current===originalFolder){setFiles(before);select(removed.map(f=>f.name));}}});
    }
    function openFile(f) {
      if (f.kind==='folder') setFolder(current+'\\'+f.name,f.files);
      else if (options.onOpen) options.onOpen(f,api);
      else dialog({title:f.name,message:'This is a browser mock. Files stay on your computer.',actions:[{label:'Close'}]});
    }
    function defaultMenu(x,y,f) {
      if(f) {
        if(!selection.has(f.name))select(f.name);
        const item=(label,i,shortcut,action)=>({label,icon:i,shortcut,action});
        openMenu(x,y,[item('Open','open','Enter',()=>openFile(f)),item('Open With…','external','',()=>dialog({title:'Open With',message:'Choose an application in the real app.'})),...(f.name.endsWith('.lnk')?[item('Open File Location','external','',()=>setFolder('Home\\Documents'))]:[]),'separator',item('Cut','cut','Ctrl X',()=>toast('Ready to move',{action:null})),item('Copy','copy','Ctrl C',()=>toast('Copied',{action:null})),'separator',item('Rename…','edit','F2',rename),item('Move to Trash','trash','Delete',trash),'separator',item('Star','star','',()=>toast('Added to Starred',{onAction:()=>{}})),...(f.kind==='folder'?[item('Add to Sidebar','folder','',()=>{options.addedFolders=[...(options.addedFolders||['Reports','Holiday Photos']),f.name];sidebarRender();toast('Added to Sidebar',{action:null});})]:[]),item('Copy Path','link','',()=>toast('Path copied',{action:null})),item('Properties','info','Alt Enter',()=>dialog({title:f.name,message:`${type(f).label}${f.size ? ' · '+bytes(f.size) : ''}`}))]);
      } else openMenu(x,y,[{label:'New Folder',icon:'folder',action:()=>newFolder()},{label:'Paste',icon:'copy',disabled:true},{label:'Select All',icon:'check',action:()=>select(files.map(f=>f.name))},{label:'Open in Terminal',icon:'terminal',action:()=>toast('Terminal opens here in the real app.',{action:null})},{label:'Properties',icon:'info',action:()=>dialog({title:current.split('\\').pop(),message:`${files.length} items`})}]);
    }
    function newFolder() {
      const body=document.createElement('div');body.innerHTML='<input class="dio-field" aria-label="Folder name" value="New Folder"><button class="dio-primary">Create</button>';
      const save=()=>{const name=body.querySelector('input').value.trim();if(!name)return;if(files.some(f=>f.name===name)){toast('That name is already in use.',{action:null});return;}setFiles([...files,{name,kind:'folder',size:0,date:'Today'}]);closePopup();select(name);};body.querySelector('button').onclick=save;body.querySelector('input').onkeydown=e=>{if(e.key==='Enter')save();};popover(button('view'),{title:'New Folder',content:body});body.querySelector('input').select();
    }
    function viewOptions() {
      const body=document.createElement('div');body.innerHTML=`<div class="dio-toggle-group"><button aria-label="Grid view" aria-pressed="${view==='grid'}">${icon('grid')}Grid</button><button aria-label="Details view" aria-pressed="${view==='details'}">${icon('list')}List</button></div><input class="dio-slider" type="range" min="0" max="5" step="0.01" value="${zoom}" aria-label="View size"><div class="dio-slider-ticks">${'<i></i>'.repeat(6)}</div><p class="dio-mode-label"></p><div class="dio-sort-options"></div>`;
      const toggle=[...body.querySelectorAll('.dio-toggle-group button')];toggle.forEach((b,i)=>b.onclick=()=>{setView(i?'details':'grid');body.querySelector('input').value=zoom;sync();});
      const sync=()=>{toggle.forEach((b,i)=>b.setAttribute('aria-pressed',i?view!=='grid':view==='grid'));body.querySelector('.dio-mode-label').textContent=['Details','List','Small icons','Medium icons','Large icons','Extra large icons'][Math.round(zoom)];};
      body.querySelector('input').oninput=e=>{zoom=Number(e.target.value);view=zoom<.5?'details':zoom<1.5?'list':'grid';button('view').innerHTML=icon(view==='grid'?'grid':'list')+icon('down',12);renderFiles();sync();};
      ['Name','Size','Modified','Type'].forEach(s=>{const b=document.createElement('button');b.className='dio-menu-item';b.textContent='Sort by '+s;b.onclick=()=>sort(s.toLowerCase());body.querySelector('.dio-sort-options').append(b);});
      sync();popover(button('view'),{title:'View Options',content:body});
    }
    function sort(key) {files.sort((a,b)=>Number(b.kind==='folder')-Number(a.kind==='folder')+(a.kind===b.kind ? (key==='size'&&typeof a.size==='number'&&typeof b.size==='number' ? a.size-b.size : String(key==='type'?type(a).label:a[key==='modified'?'date':key]||'').localeCompare(String(key==='type'?type(b).label:b[key==='modified'?'date':key]||''))) : 0));renderFiles();}
    function go(delta) {const index=historyIndex+delta;if(index>=0&&index<history.length){historyIndex=index;setFolder(history[index],undefined,false);}}
    host.addEventListener('click',e=>{
      const nav=e.target.closest('[data-folder]');if(nav){if(e.target.closest('.dio-eject')){toast('Drive ejected',{action:null});return;}setFolder(nav.dataset.folder);return;}
      const crumb=e.target.closest('[data-path]');if(crumb){setFolder(crumb.dataset.path);return;}
      const row=e.target.closest('[data-name]');if(row){const name=row.dataset.name,index=files.findIndex(f=>f.name===name);if(e.shiftKey){const range=files.slice(Math.min(anchor,index),Math.max(anchor,index)+1).map(f=>f.name);selection=new Set(e.ctrlKey?[...selection,...range]:range);}else if(e.ctrlKey||e.metaKey){selection.has(name)?selection.delete(name):selection.add(name);anchor=index;}else{selection=new Set([name]);anchor=index;}updateSelection();fileArea.focus({preventScroll:true});return;}
      const sorter=e.target.closest('[data-sort]');if(sorter){sort(sorter.dataset.sort);return;}
      const action=e.target.closest('[data-action]')?.dataset.action;
      if(action==='back')go(-1);if(action==='forward')go(1);if(action==='view')viewOptions();
      if(action==='search'){pathbar.innerHTML='<input class="dio-search" aria-label="Search current folder" placeholder="Search in this folder">';const input=pathbar.querySelector('input');input.focus();input.addEventListener('input',()=>{search=input.value;renderFiles();});}
      if(action==='main-menu'){const r=button('main-menu').getBoundingClientRect();openMenu(r.left,r.bottom,[{label:'New Folder',icon:'folder',action:newFolder},{label:'Preferences',icon:'grid',action:()=>dialog({title:'Preferences',message:'Use the theme controls below the mock window.'})},{label:'About Dio File Manager',icon:'info',action:()=>dialog({title:'Dio File Manager',message:'A small, fast Windows file manager.'})}]);}
      if(['minimize','maximize','close-window'].includes(action))toast('Window controls are decorative in this mock.',{action:null});
      if(e.target===fileArea||e.target.classList.contains('dio-grid'))select([]);
    },{signal});
    host.addEventListener('dblclick',e=>{const row=e.target.closest('[data-name]');if(row)openFile(files.find(f=>f.name===row.dataset.name));},{signal});
    host.addEventListener('contextmenu',e=>{if(!win.contains(e.target))return;e.preventDefault();const nav=e.target.closest('[data-folder]');if(nav){openMenu(e.clientX,e.clientY,[{label:'Remove from Sidebar',icon:'close',action:()=>{nav.remove();toast('Removed from Sidebar',{onAction:sidebarRender});}}]);return;}const row=e.target.closest('[data-name]');defaultMenu(e.clientX,e.clientY,row?files.find(f=>f.name===row.dataset.name):null);},{signal});
    document.addEventListener('pointerdown',e=>{if(popup&&!popup.contains(e.target)&&!e.target.closest('[data-action="view"]'))closePopup();},{signal});
    host.addEventListener('keydown',e=>{
      if(e.key==='Escape'){if(popup){closePopup();e.preventDefault();return;}search='';renderPath();renderFiles();return;}
      if(popup){const buttons=[...popup.querySelectorAll('button:not(:disabled),input')];const n=buttons.indexOf(document.activeElement);if(e.key==='Tab'){e.preventDefault();buttons[(n+(e.shiftKey?-1:1)+buttons.length)%buttons.length]?.focus();}if(e.key==='ArrowDown'||e.key==='ArrowUp'){e.preventDefault();buttons[(n+(e.key==='ArrowDown'?1:-1)+buttons.length)%buttons.length]?.focus();}return;}
      if(e.target.matches('input,textarea'))return;
      if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='a'){e.preventDefault();select(files.map(f=>f.name));}
      if(e.altKey&&e.key==='ArrowLeft'){e.preventDefault();go(-1);}if(e.altKey&&e.key==='ArrowRight'){e.preventDefault();go(1);}
      if(e.ctrlKey&&e.key==='1'){e.preventDefault();setView('grid');}if(e.ctrlKey&&e.key==='2'){e.preventDefault();setView('details');}
      if(e.key==='Delete'){e.preventDefault();trash();}if(e.key==='F2'){e.preventDefault();rename();}
      if(e.key==='Enter'&&e.target===fileArea&&selected()[0])openFile(selected()[0]);
      if(e.ctrlKey&&e.key.toLowerCase()==='f'){e.preventDefault();button('search').click();}
      if(e.ctrlKey&&e.key.toLowerCase()==='z'){e.preventDefault();content.querySelector('.dio-toast-action')?.click();}
      if(e.shiftKey&&e.key==='F10'){e.preventDefault();const r=fileArea.getBoundingClientRect();defaultMenu(r.left+48,r.top+64,selected()[0]);}
      if(e.target===fileArea&&/^Arrow/.test(e.key)){e.preventDefault();const shown=visibleFiles();if(!shown.length)return;const n=shown.findIndex(f=>selection.has(f.name));const columns=view==='details'?1:Math.max(1,Math.floor((fileArea.clientWidth-24)/112));const delta={ArrowRight:1,ArrowLeft:-1,ArrowDown:columns,ArrowUp:-columns}[e.key];const f=shown[Math.max(0,Math.min(shown.length-1,n<0?0:n+delta))];select(f.name);fileArea.querySelector(`[data-name="${CSS.escape(f.name)}"]`)?.scrollIntoView({block:'nearest'});}
      if((e.key==='Enter'||e.key===' ')&&e.target.matches('[data-folder]')){e.preventDefault();setFolder(e.target.dataset.folder);}
    },{signal});
    const api={setFiles,setFolder,select,setView,openMenu,toast,popover,dialog,closePopup,setHint,setTheme(theme='system'){if(theme==='system')document.documentElement.removeAttribute('data-theme');else document.documentElement.dataset.theme=theme;},icon,registerFolder(path,items){folders.set(path,items);},get files(){return files;},get selected(){return selected();},get folder(){return current;},get element(){return win;},destroy(){listeners.abort();clearTimeout(toastTimer);host.replaceChildren();}};
    setFolder(options.path||'Home\\Downloads',options.files||[]);setHint(options.hint||'روی یه فایل کلیک کن؛ با کلیک راست، کارهای بیشتر رو ببین.',1,3);
    document.fonts.ready.then(()=>{if(host.isConnected&&host.querySelector('.dio-files'))renderFiles();});
    return api;
  }
  window.DioShell={create,icon,fileIcon,formatSize:bytes};
})();
