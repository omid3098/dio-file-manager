/* A week of measurements is simulated. Nothing reads or changes the disk. */
(() => {
  const G = 1073741824;
  const file = (name, size, date, added = false) => ({name, kind: 'file', size: Math.round(size * G), date, added});
  const downloads = [
    file('photos.zip', 2.6, 'Nov 15, 2026', true),
    file('setup-x64.exe', 1.6, 'Nov 13, 2026', true),
    file('invoice-11-2026.pdf', .0004, 'Nov 6, 2026'),
    file('قرارداد اجاره.pdf', .0021, 'Oct 28, 2026'),
    file('Screenshot 2026-11-16 221403.png', .0036, 'Nov 16, 2026', true)
  ];
  const temp = [file('shader-cache.bin', 5.4, 'Nov 15, 2026', true), file('build-cache.7z', 3.7, 'Nov 12, 2026', true), file('installer.log', .001, 'Oct 28, 2026')];
  const build = [file('game-win64.zip', 4.2, 'Nov 16, 2026', true), file('textures.pak', 1.8, 'Nov 14, 2026', true), file('hero_albedo_v08.png', .032, 'Oct 30, 2026'), file('rock_01.fbx', .018, 'Oct 28, 2026')];
  const driveData = {
    C: {name: 'Local Disk (C:)', used: 87.5, week: 18.4, month: 31.2, capacity: 128, free: '16 GB free of 128 GB',
      rows: [{name:'AppData\\Local\\Temp', week:9.1, month:15.8, path:'C:\\Users\\Alex\\AppData\\Local\\Temp', files:temp}, {name:'Downloads', week:4.2, month:10.4, path:'C:\\Users\\Alex\\Downloads', files:downloads}, {name:'AppData\\Local\\Packages', week:5.7, month:8.6, path:'C:\\Users\\Alex\\AppData\\Local\\Packages', files:[file('app-cache.dat', 5.7, 'Nov 15, 2026', true)]}],
      shrunk: {name:'Windows\\SoftwareDistribution', week: -.6, month: -3.6, path:'C:\\Windows\\SoftwareDistribution', files:[file('ReportingEvents.log', .001, 'Nov 16, 2026')]}
    },
    E: {name:'Projects (E:)', used:39.25, week:8.2, month:24.6, capacity:2000, free:'1.2 TB free of 2 TB',
      rows:[{name:'w\\game-tools\\build', week:6, month:18, path:'E:\\w\\game-tools\\build', files:build}, {name:'w\\game-tools\\.agent-work', week:2.8, month:7.4, path:'E:\\w\\game-tools\\.agent-work', files:[file('test-captures.zip', 2.8, 'Nov 16, 2026', true)]}],
      shrunk: {name:'w\\website\\build', week:-.6, month:-.8, path:'E:\\w\\website\\build', files:[file('index.html', .00001, 'Nov 16, 2026')]}
    },
    D: {name:'Shared (D:)', used:44, week:0, capacity:1000, free:'560 GB free of 1 TB'}
  };
  const host = document.querySelector('#shell');
  const shell = DioShell.create(host, {path:'C:\\Users\\Alex\\Downloads', files:downloads, addedFolders:['E:\\w', 'Papers'], drives:[driveData.C, driveData.E, driveData.D]});
  const icon = DioShell.icon;
  let step = 1, period = 'week', growthOn = {C:true,E:true}, context = null;
  const hints = [
    'فرض کن یه هفته گذشته؛ روی قسمت روشنِ نوار C: مکث کن.',
    'این مقدارِ اضافه‌شده‌ست؛ روی همون قسمت روشن کلیک کن.',
    'حالا Month رو بزن تا رشدِ یه ماه رو ببینی.',
    'برای برگشت به هفت روز اخیر، Week رو بزن.',
    'روی Downloads کلیک کن تا فایل‌های تازه رو ببینی.',
    'فایل‌های تازه انتخاب شدن؛ حالا Other Locations رو بزن.',
    'اینجا هم رشد رو می‌بینی؛ روی قسمت روشنِ نوار E: کلیک کن.',
    'روی w\\game-tools\\build کلیک کن تا فایل‌های حجیمش رو ببینی.',
    'حالا روی درایو C: توی نوار کناری کلیک راست کن.',
    'برای خاموش کردن این نشونه روی C:، تیک Show growth رو بردار.',
    'دیدی کدوم پوشه‌ها بزرگ شدن و فایل‌های تازه کجان.'
  ];
  function hint(n) {
    step = n;
    shell.setHint(hints[n-1], n, hints.length);
    host.querySelectorAll('.drive').forEach(el=>el.classList.toggle('guided', n <= 2 && el.dataset.letter==='C'));
    if(n === 11) {
      const restart = document.createElement('button'); restart.className='restart'; restart.id='restart'; restart.textContent='از اول'; restart.onclick=reset;
      host.querySelector('.dio-try-strip p').append(restart);
    }
  }
  function next(from) { if(step === from) hint(from + 1); }
  function hideTip() { host.querySelector('.growth-tip')?.remove(); }
  function segment(letter, location=false) {
    const d = driveData[letter], b = document.createElement('button');
    b.className='growth-segment'; b.dataset.growth=letter; b.id=(location?'location-':'sidebar-')+letter;
    b.setAttribute('aria-label',`${letter}: +${d.week.toFixed(1)} GB this week`);
    b.style.left=`${d.used-d.week/d.capacity*100}%`; b.style.width=`${d.week/d.capacity*100}%`;
    b.onmouseenter = () => {
      if(host.querySelector('.growth-popup'))return;
      hideTip(); const tip = document.createElement('div'); tip.className='growth-tip'; tip.id='growth-tooltip'; tip.setAttribute('role','tooltip');
      tip.textContent=`+${d.week.toFixed(1)} GB this week`;
      const r=b.getBoundingClientRect(), w=shell.element.getBoundingClientRect();
      tip.style.left=`${r.left-w.left-40}px`; tip.style.top=`${r.top-w.top-40}px`; shell.element.append(tip);
      if(letter==='C')next(1);
    };
    b.onmouseleave=hideTip;
    b.onfocus=b.onmouseenter; b.onblur=hideTip;
    b.onclick = e => {e.stopPropagation(); hideTip(); period='week'; openGrowth(letter,b); if(letter==='C')next(2); if(letter==='E'&&location)next(7);};
    return b;
  }
  function decorateDrives() {
    for(const [letter,d] of Object.entries(driveData)) {
      const nav=host.querySelector(`.dio-sidebar [data-folder="${d.name}"]`);
      nav.classList.add('drive'); nav.dataset.letter=letter; nav.id='drive-'+letter;
      if(growthOn[letter] && d.week) {
        const amount=document.createElement('span'); amount.className='drive-growth-label'; amount.textContent=`+${d.week.toFixed(1)} GB`;
        nav.querySelector('.dio-nav-label').insertBefore(amount,nav.querySelector('.dio-drive-meter'));
        nav.querySelector('.dio-drive-meter').append(segment(letter));
      }
      nav.classList.toggle('guided',step<=2&&letter==='C');
    }
  }
  function openGrowth(letter, anchor) {
    const d=driveData[letter], body=document.createElement('div');
    const r=anchor.getBoundingClientRect();
    const pop=shell.popover({x:anchor.closest('.dio-sidebar')?host.querySelector('.dio-sidebar').getBoundingClientRect().right+8:r.right+20,y:r.top-320},{title:`What grew on ${letter}:`,content:body});
    pop.classList.add('growth-popup'); pop.id='growth-popover';
    function draw() {
      const total=d[period], largest=Math.max(...d.rows.map(row=>row[period]));
      body.innerHTML=`<div class="period-switch" aria-label="Growth period"><button id="period-week" aria-pressed="${period==='week'}">Week</button><button id="period-month" aria-pressed="${period==='month'}">Month</button></div><div class="growth-headline"><span class="growth-total">+${total.toFixed(1)} GB</span><span class="growth-period">this ${period}</span></div><div class="growth-dates">${period==='week'?'Nov 10 – 17, 2026':'Oct 17 – Nov 17, 2026'} · net change</div><div class="growth-rows"></div><div class="shrunk"><span>Also shrank</span></div><div class="growth-footer">${icon('drive',14)}Folder sizes kept on this device</div>`;
      ['week','month'].forEach(p=>{body.querySelector('#period-'+p).onclick=()=>{period=p; draw(); if(p==='month')next(3); if(p==='week')next(4);};});
      [...d.rows].sort((a,b)=>b[period]-a[period]).forEach(row=>addRow(row,body.querySelector('.growth-rows'),largest));
      addRow(d.shrunk,body.querySelector('.shrunk'),0);
      function addRow(row,parent,max) {
        const b=document.createElement('button'); b.className='growth-row'; b.dataset.growthFolder=row.name;
        if(row.name==='Downloads')b.id='open-downloads'; if(row.name==='w\\game-tools\\build')b.id='open-build';
        b.innerHTML=`${icon('folder')}<span class="growth-path">${row.name}</span><span class="growth-amount">${row[period]>0?'+':'−'}${Math.abs(row[period]).toFixed(1)} GB</span>${max?`<span class="tiny-track"><i style="--size:${row[period]/max*100}%"></i></span>`:''}`;
        b.onclick=()=>{openFolder(row); if(row.name==='Downloads')next(5); if(row.name==='w\\game-tools\\build')next(8);}; parent.append(b);
      }
    }
    draw(); hideTip(); body.querySelector('#period-week').focus({preventScroll:true});
  }
  function openFolder(row) {
    const nextContext = {period, names: row.files.filter(f=>f.added || (period==='month' && Date.parse(f.date)>=Date.parse('Oct 17, 2026'))).map(f=>f.name)};
    shell.setFolder(row.path,[...row.files].sort((a,b)=>b.size-a.size)); context=nextContext;
    shell.setView('details'); shell.select(context.names); decorateContext(); host.querySelector('.dio-files').focus({preventScroll:true});
  }
  function decorateContext() {
    const area=host.querySelector('.dio-files'); area.querySelector('.growth-context')?.remove();
    if(!context)return;
    const bar=document.createElement('div'); bar.className='growth-context'; bar.id='growth-context';
    bar.innerHTML=`${icon('clock')}<strong>Added this ${context.period}</strong><span>· ${shell.selected.length} selected</span><span>· largest first</span><button class="context-clear" aria-label="Clear growth selection">Clear</button>`;
    bar.querySelector('button').onclick=()=>{context=null; shell.select([]);bar.remove();}; area.prepend(bar);
    const size=area.querySelector('[data-sort="size"]'); if(size)size.innerHTML='Size '+icon('down',12);
  }
  function showLocations() {
    const area=host.querySelector('.dio-files'); area.innerHTML='<section class="locations"><h2>On This Computer</h2></section>';
    const container=area.querySelector('.locations');
    for(const letter of ['C','E','D']) {
      const d=driveData[letter], row=document.createElement('div'); row.className='location'; row.dataset.driveMenu=letter;
      row.innerHTML=`${icon('drive',32)}<div><div class="location-title"><button class="location-name">${d.name}</button>${growthOn[letter]&&d.week?`<span class="location-growth">+${d.week.toFixed(1)} GB this week</span>`:''}</div><div class="location-track" style="--used:${d.used}%"><div class="location-fill"></div></div><div class="location-free">${d.free}</div></div>`;
      row.querySelector('.location-name').onclick=()=>shell.setFolder(d.name);
      if(growthOn[letter]&&d.week)row.querySelector('.location-track').append(segment(letter,true)); container.append(row);
    }
  }
  function driveMenu(letter,x,y) {
    hideTip(); const d=driveData[letter];
    const menu=shell.openMenu(x,y,[{label:'Open',icon:'open',action:()=>shell.setFolder(d.name)},'separator',{label:'Show growth',icon:growthOn[letter]?'check':'',action:()=>{
      growthOn[letter]=!growthOn[letter];
      host.querySelectorAll('.drive-growth-label,.dio-sidebar .growth-segment').forEach(el=>el.remove()); decorateDrives();
      if(shell.folder==='Other Locations')showLocations();
      shell.toast(`Growth ${growthOn[letter]?'shown':'hidden'} on ${letter}:`,{action:'Undo',onAction:()=>{growthOn[letter]=!growthOn[letter];host.querySelectorAll('.drive-growth-label,.dio-sidebar .growth-segment').forEach(el=>el.remove());decorateDrives();if(shell.folder==='Other Locations')showLocations();}});
      if(letter==='C'&&!growthOn.C)next(10);
    }}]);
    menu.querySelectorAll('button')[1].id='toggle-growth'; menu.querySelectorAll('button')[1].setAttribute('role','menuitemcheckbox');menu.querySelectorAll('button')[1].setAttribute('aria-checked',!!growthOn[letter]);
    if(letter==='C')next(9);
  }
  // Capture the drive menu before the shared shell's generic sidebar menu.
  host.addEventListener('contextmenu',e=>{
    const row=e.target.closest('[data-letter],[data-drive-menu]'); if(!row)return;
    e.preventDefault();e.stopImmediatePropagation();driveMenu(row.dataset.letter||row.dataset.driveMenu,e.clientX,e.clientY);
  },true);
  host.addEventListener('keydown',e=>{
    if(e.shiftKey&&e.key==='F10') {const row=e.target.closest('[data-letter]');if(row){e.preventDefault();e.stopImmediatePropagation();const r=row.getBoundingClientRect();driveMenu(row.dataset.letter,r.right,r.top);}}
  },true);
  host.addEventListener('dio:folder',e=>{
    hideTip(); decorateDrives(); context=null;
    if(e.detail.path==='Other Locations'){showLocations();next(6);}
  });
  host.addEventListener('dio:select',()=>{
    if(!context)return;
    // The shared shell can redraw after fonts load or a sort. Keep period context.
    queueMicrotask(()=>{if(context)decorateContext();});
  });
  function reset() {
    growthOn={C:true,E:true}; period='week';context=null; hideTip(); step=1;
    host.querySelector('.dio-toast [aria-label="Dismiss notification"]')?.click();
    shell.setFolder('C:\\Users\\Alex\\Downloads',downloads); shell.setView('details');hint(1);
  }
  shell.registerFolder('Downloads',downloads);
  shell.registerFolder(driveData.C.name,[{name:'Users',kind:'folder',size:85*G,date:'Today'},{name:'Windows',kind:'folder',size:27*G,date:'Today'}]);
  shell.registerFolder(driveData.E.name,[{name:'w',kind:'folder',size:520*G,date:'Today'},{name:'Art',kind:'folder',size:265*G,date:'Yesterday'}]);
  shell.setView('details');decorateDrives();hint(1);
})();
