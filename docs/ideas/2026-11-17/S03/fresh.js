/* All arrivals, aging and Trash operations are in-memory samples. No disk access. */
(() => {
  'use strict';
  const $ = s => document.querySelector(s);
  const esc = s => String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const icon = DioShell.icon, size = DioShell.formatSize;
  const file = (path, bytes, kind='file') => ({path, size:bytes, kind});
  const mainFiles = [
    file('visual-check',0,'folder'), file('visual-check\\screenshots',0,'folder'),
    file('visual-check\\screenshots\\Screenshot 2026-11-16 221403.png',5280192),
    file('visual-check\\previews\\hero_albedo_v08.png',28148224),
    file('visual-check\\previews\\hero_albedo_v07.png',26962322),
    file('visual-check\\notes\\خلاصهٔ بررسی.txt',3850),
    file('visual-check\\previews',0,'folder'), file('visual-check\\notes',0,'folder'),
    file('visual-check\\logs',0,'folder'), file('visual-check\\frames',0,'folder'),
    file('visual-check\\logs\\build.log',187695),file('visual-check\\logs\\render.log',65324),
    file('visual-check\\report.json',12282),file('visual-check\\capture-settings.json',1583)
  ];
  while (mainFiles.length < 214) mainFiles.push(file(`visual-check\\frames\\hero_${String(mainFiles.length-13).padStart(3,'0')}.png`,5898240+mainFiles.length*1024));
  const initial = () => [
    {id:'game',root:'E:\\w\\game-tools\\.agent-work',age:3,span:'3 min',files:mainFiles.slice()},
    {id:'downloads',root:'Home\\Downloads',age:38,span:'1 min',files:[file('photos.zip',162738176),file('setup-x64.exe',73400320),file('invoice-11-2026.pdf',152129),file('قرارداد اجاره.pdf',524288)]},
    {id:'old',root:'E:\\w\\notes-app\\build',age:23*60+20,span:'2 min',files:[file('notes-app.exe',22020096),file('notes-app.pdb',38797312),file('assets',0,'folder'),file('assets\\icon.png',86172)]}
  ];
  const home = [
    {name:'Desktop',kind:'folder'}, {name:'Documents',kind:'folder'}, {name:'Downloads',kind:'folder'},
    {name:'Music',kind:'folder'}, {name:'Pictures',kind:'folder'}, {name:'Videos',kind:'folder'}
  ];
  let bursts=initial(), trash=[], unread=3, enabled=true, stage=0, selectedFile=-1, focusedBurst='game';
  let shell = DioShell.create('#shell',{path:'Home',files:home,addedFolders:['E:\\w'],drives:[{name:'Local Disk (C:)',used:89},{name:'Local Disk (E:)',used:42}]});
  const host=$('#shell'), content=$('.dio-content');
  const panel=document.createElement('section'); panel.className='fresh-panel'; panel.hidden=true; content.append(panel);
  const action=document.createElement('button'); action.className='try-action'; action.id='try-action'; action.hidden=true;
  $('.dio-try-strip').insertBefore(action,$('.dio-step'));
  const hints=[
    'روی Just Made با نشان ۳ کلیک کن تا ببینی چه چیزهایی تازه ساخته شدن.',
    'روی دستهٔ اول کلیک کن؛ این همه فایل توی سه دقیقه کنار هم ساخته شدن.',
    'روی فایل hero_albedo_v08.png کلیک کن؛ مسیر کاملش رو همین‌جا می‌بینی.',
    'حالا Enter رو بزن تا پوشهٔ واقعی فایل، با خود فایل انتخاب‌شده باز بشه.',
    'فایل سر جاشه. برای جمع‌کردن کل دسته، دوباره روی Just Made کلیک کن.',
    'دستهٔ اول انتخاب شده؛ Delete رو بزن تا همهٔ ۲۱۴ مورد برن توی Trash.',
    'روی Undo بزن تا تمام دسته، سر جای خودش برگرده.',
    'فرض کن یه برنامه باز فایل ساخته؛ با این دکمه رسیدنشون رو شبیه‌سازی کن.',
    'نشان ۱ یعنی یه دستهٔ تازه رسیده. دوباره روی Just Made کلیک کن.',
    'دوازده فایل هم به دستهٔ قبلی اضافه شدن. حالا گذشت یه ساعت رو شبیه‌سازی کن.',
    'دستهٔ قدیمی‌تر از ۲۴ ساعت از فهرست رفت. روی Just Made راست‌کلیک کن.',
    'حالا Hide Just Made رو بزن تا این بخش و ثبت فایل‌های تازه خاموش بشه.',
    'دیدی چی تازه ساخته شده، کجاست و چطور یه‌جا جمعش کنی.'
  ];
  function hint(next=stage) {
    stage=next; shell.setHint(hints[stage],stage+1,hints.length);
    const labels={7:'ساخت فایل‌ها',9:'یه ساعت بعد',12:'از اول'};
    action.hidden=!labels[stage]; action.textContent=labels[stage]||'';
    host.dataset.step=String(stage+1);
  }
  function nav() {
    $('#just-made')?.remove();
    if(enabled) {
      const b=document.createElement('button'); b.id='just-made'; b.className='dio-nav';
      b.setAttribute('aria-label',`Just Made${unread?`, ${unread} new bursts`:''}`);
      if(shell.folder.startsWith('Just Made')) b.setAttribute('aria-current','page');
      b.innerHTML=`${icon('clock')}<span class="dio-nav-label">Just Made</span>${unread?`<span class="dio-badge">${unread}</span>`:''}`;
      $('[data-folder="Recent"]').after(b);
      b.onclick=()=>{unread=0;shell.setFolder('Just Made'); if(stage===0)hint(1); else if(stage===4)hint(5); else if(stage===8)hint(9);};
      b.oncontextmenu=e=>{e.preventDefault();e.stopPropagation();menu(e.clientX,e.clientY);};
      b.onkeydown=e=>{if(e.key==='ContextMenu'||(e.shiftKey&&e.key==='F10')){e.preventDefault();e.stopPropagation();const r=b.getBoundingClientRect();menu(r.right,r.top);}};
    }
    const trashNav=$('[data-folder="Trash"]');
    trashNav.querySelector('.dio-badge')?.remove();
    if(trash.length) trashNav.insertAdjacentHTML('beforeend',`<span class="dio-badge">${trash.length}</span>`);
  }
  function menu(x,y) {
    const el=shell.openMenu(x,y,[{label:'Hide Just Made',icon:'close',action:()=>{
      enabled=false; unread=0; shell.setFolder('Home',home); nav(); if(stage===11)hint(12);
    }}]);
    el.querySelector('button').id='hide-just-made';
    if(stage===10)hint(11);
  }
  const total=b=>b.files.reduce((sum,f)=>sum+f.size,0);
  const age=b=>b.age<60?`${b.age} min ago`:`${Math.floor(b.age/60)} h ago`;
  const relative=f=>f.path.split('\\').map(part=>`<bdi dir="auto">${esc(part)}</bdi>`).join('\\');
  function overview() {
    panel.hidden=false; selectedFile=-1;
    panel.innerHTML=`<header class="fresh-heading"><h1>Just Made</h1><small>Last 24 hours</small></header><div class="burst-list" data-scroll="list" role="listbox" aria-label="Bursts of new items">${bursts.map(b=>`<button id="burst-${b.id}" class="burst" data-burst="${b.id}" role="option" aria-selected="${b.id===focusedBurst && stage>=5}">${icon('folder',28)}<span><span class="burst-path">${esc(b.root)}</span><span class="burst-count">${b.files.length} items · ${b.files.filter(f=>f.kind==='folder').length} folders · made within ${b.span}</span></span><strong class="burst-size">${size(total(b))}</strong><span class="burst-age">${age(b)}</span></button>`).join('') || '<div class="fresh-empty">Nothing made in the last 24 hours.</div>'}</div><footer class="fresh-scope">${icon('folder',14)}<span>Home · E:\\w</span><span>Local · 24-hour history</span></footer>`;
    panel.querySelectorAll('[data-burst]').forEach(el=>{
      el.onclick=()=>{focusedBurst=el.dataset.burst; shell.setFolder('Just Made\\'+focusedBurst);if(stage===1)hint(2);};
      el.onfocus=()=>{focusedBurst=el.dataset.burst;panel.querySelectorAll('[data-burst]').forEach(row=>row.setAttribute('aria-selected',row===el));};
    });
    if(stage>=4) panel.querySelector(`[data-burst="${focusedBurst}"]`)?.focus({preventScroll:true});
  }
  function flat(b) {
    if(!b) {shell.setFolder('Just Made');return;}
    panel.hidden=false;
    panel.innerHTML=`<header class="fresh-heading flat-heading"><h1>${esc(b.root)}</h1><small>${b.files.length} items · ${size(total(b))}</small></header><div class="flat-columns"><span>Relative path</span><span>Size</span><span>Created</span></div><div class="flat-list" role="listbox" aria-label="All items in this burst" data-scroll="list">${b.files.map((f,i)=>`<button class="flat-row" id="flat-${i}" data-file-index="${i}" aria-selected="false" role="option"><span class="relative-path">${icon(f.kind==='folder'?'folder':/png|jpg/.test(f.path)?'image':'file')}<span class="${/[\u0600-\u06ff]/.test(f.path)?'fa':''}" dir="ltr">${relative(f)}</span></span><span class="file-size">${f.kind==='folder'?'—':size(f.size)}</span><span class="file-age">${age(b)}</span></button>`).join('')}</div><footer class="flat-footer"><span>${b.files.length} items · full relative paths</span><span><kbd>Enter</kbd> Open real folder</span></footer>`;
    panel.querySelectorAll('[data-file-index]').forEach(row=>{
      row.onclick=()=>{selectedFile=Number(row.dataset.fileIndex);panel.querySelectorAll('[data-file-index]').forEach(el=>el.setAttribute('aria-selected',el===row)); if(stage===2 && selectedFile===3)hint(3);};
      row.ondblclick=()=>reveal(b,Number(row.dataset.fileIndex));
    });
  }
  function reveal(b,index) {
    const f=b.files[index]; if(!f)return;
    const split=f.path.lastIndexOf('\\');
    const parent=split<0?b.root:b.root+'\\'+f.path.slice(0,split);
    const name=f.path.slice(split+1);
    const siblings=b.files.filter(item=>item.path.slice(0,item.path.lastIndexOf('\\'))===f.path.slice(0,split)).map(item=>({name:item.path.slice(item.path.lastIndexOf('\\')+1),kind:item.kind,size:item.size,date:'Today, 01:16'}));
    shell.setFolder(parent,siblings); shell.setView('details'); shell.select(name);
    $('.dio-files').focus({preventScroll:true});
    if(stage===3)hint(4);
  }
  function trashBurst() {
    const index=bursts.findIndex(b=>b.id===focusedBurst); if(index<0)return;
    const b=bursts[index]; bursts.splice(index,1);trash=b.files.slice();
    overview();nav();
    shell.toast(`${b.files.length} items moved to Trash`,{duration:0,onAction:()=>{
      bursts.splice(index,0,b); trash=[];focusedBurst=b.id;overview();nav();if(stage===6)hint(7);
    }});
    if(stage===5)hint(6);
  }
  host.addEventListener('dio:folder',()=>{
    nav();
    if(enabled && shell.folder==='Just Made')overview();
    else if(enabled && shell.folder.startsWith('Just Made\\'))flat(bursts.find(b=>b.id===shell.folder.split('\\')[1]));
    else {panel.hidden=true; if(shell.folder==='Trash' && trash.length) shell.setFiles(trash.map(f=>({name:f.path,kind:f.kind,size:f.size,date:'Just now'})));}
  });
  // Capture feature keys before the shared shell's ordinary file operations.
  host.addEventListener('keydown',e=>{
    if(panel.hidden || e.target.closest('.dio-menu,.dio-toast'))return;
    const isList=shell.folder==='Just Made';
    if(e.key==='Delete' && isList){e.preventDefault();e.stopImmediatePropagation();trashBurst();}
    else if(e.key==='Enter' && !isList && e.target.closest('.flat-row')){e.preventDefault();e.stopImmediatePropagation();const b=bursts.find(b=>b.id===focusedBurst);reveal(b,Number(e.target.closest('.flat-row').dataset.fileIndex));}
    else if(e.key==='Enter' && isList && e.target.closest('.burst')){e.preventDefault();e.stopImmediatePropagation();e.target.closest('.burst').click();}
    else if(e.key==='Escape' && !isList){e.preventDefault();e.stopImmediatePropagation();shell.setFolder('Just Made');}
  },true);
  action.onclick=()=>{
    if(stage===7) {
      shell.setFolder('Home',home);
      const game=bursts.find(b=>b.id==='game');
      for(let i=1;i<=12;i++)game.files.push(file(`visual-check\\logs\\capture-${String(i).padStart(2,'0')}.log`,12288+i*523));
      game.age=1; game.span='3 min';
      bursts.unshift({id:'website',root:'E:\\w\\website\\build',age:1,span:'1 min',files:Array.from({length:18},(_,i)=>file(i===0?'index.html':`assets\\page-${i}.js`,33540+i*2123))});
      focusedBurst='website'; unread=1;nav();hint(8);
    } else if(stage===9) {
      bursts.forEach(b=>b.age+=60);
      panel.querySelector('#burst-old')?.classList.add('expiring');
      setTimeout(()=>{bursts=bursts.filter(b=>b.age<1440);overview();hint(10);},150);
    } else if(stage===12) location.reload();
  };
  nav(); hint();
})();
