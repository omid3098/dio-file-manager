(() => {
  'use strict';
  const originals = [
    {name:'Screenshot 2026-11-16 221403.png',size:426081,date:'Yesterday, 22:14',suggestion:'hosting-payment-confirmation.png'},
    {name:'scan0001.pdf',size:682144,date:'Yesterday, 18:32',suggestion:'invoice-11-2026-acme-hosting.pdf'},
    {name:'IMG_4821.jpg',size:3891224,date:'Nov 15, 2026',suggestion:'kitchen-sink-model-label.jpg'},
    {name:'download (2).pdf',size:328904,date:'Nov 14, 2026',suggestion:'residence-permit-checklist-2026.pdf'},
    {name:'photos (3).zip',size:48234670,date:'Nov 14, 2026',suggestion:'berlin-weekend-11-2026.zip'},
    {name:'اسکن۰۰۰۲.pdf',size:1491608,date:'Nov 12, 2026',suggestion:'قرارداد-اجاره-تهران-۲۰۲۵.pdf'},
    {name:'invoice-11-2026.pdf',size:492310,date:'Nov 11, 2026'},
    {name:'قرارداد اجاره.pdf',size:1048576,date:'Nov 10, 2026'},
    {name:'setup-x64.exe',size:18345160,date:'Nov 10, 2026'}
  ].map(f => ({kind:'file',...f}));
  const host = document.querySelector('#shell');
  const shell = DioShell.create(host, {path:'Home\\Downloads', files:originals});
  const win = shell.element;
  const area = host.querySelector('.dio-files');
  const status = host.querySelector('.dio-status');
  const viewButton = host.querySelector('[data-action="view"]');
  let phase = 'idle', step = 1, drafts = new Map(), editor = null, workTimer, f2At = 0;
  let enabled = true;
  try { enabled = localStorage.getItem('ghost-names-demo-enabled') !== 'false'; } catch (_) {}
  const fa = value => /[\u0600-\u06ff]/.test(value);
  const unclear = file => /^(Screenshot |IMG_|scan\d|download(?: |\.)|photos \(\d|اسکن[\d۰-۹])/i.test(file.name);
  const eligible = () => shell.selected.filter(unclear);
  const hints = [
    '',
    'شش فایل انتخاب شدن؛ روی «Name by content» بزن. خوندن محتوا اینجا شبیه‌سازی می‌شه.',
    'اسم‌های خاکستری از محتوا اومدن؛ روی اسم پیشنهادیِ فایل اول کلیک کن.',
    'این اسم رو می‌تونی عوض کنی؛ برای نگه‌داشتنش Enter رو بزن.',
    'همهٔ اسم‌ها رو نگاه کن؛ با Tab یا Enter یه‌جا قبولشون کن.',
    'شش فایل یه‌جا اسم گرفتن؛ روی «Undo» بزن تا همه برگردن.',
    'حالا برای خاموش‌کردن پیشنهادها، گزینه‌های نما رو باز کن.',
    'تیک «Suggest names for unclear files» رو بردار تا دکمه دیگه نیاد.',
    'اسم‌ها رو دیدی، یه‌جا پذیرفتی و برگردوندی؛ پیشنهادها هم خاموش شدن.'
  ];
  function guide(next) {
    step = next;
    shell.setHint(hints[step], step, 8);
    host.querySelector('.dio-step').textContent = `${String(step).replace(/\d/g,d=>'۰۱۲۳۴۵۶۷۸۹'[d])} از ۸`;
    if (step === 8) {
      const restart = document.createElement('button');
      restart.id = 'restart'; restart.className = 'gn-restart'; restart.textContent = 'از اول';
      restart.onclick = reset;
      host.querySelector('.dio-try-strip p').append(restart);
    }
    mark();
  }
  function mark() {
    host.querySelectorAll('.gn-follow').forEach(el => el.classList.remove('gn-follow'));
    const target = {
      1: '#name-content', 2: '[data-ghost-index="0"]', 3: '#ghost-editor',
      4: '#accept-names', 5: '.dio-toast-action', 6: '[data-action="view"]',
      7: '#suggestion-option'
    }[step];
    if (target) host.querySelector(target)?.classList.add('gn-follow');
  }
  function pill() {
    status.replaceChildren();
    status.hidden = !shell.selected.length;
    const label = document.createElement('span'); label.className = 'gn-pill-text';
    label.textContent = `${shell.selected.length} items selected (${DioShell.formatSize(shell.selected.reduce((a,f)=>a+f.size,0))})`;
    status.append(label);
    if (phase === 'reading') {
      const progress = document.createElement('span'); progress.className = 'gn-name-button';
      progress.innerHTML = '<i class="gn-spinner"></i>Reading contents · Codex'; status.append(progress);
    } else if (phase === 'review') {
      const accept = document.createElement('button'); accept.id = 'accept-names'; accept.className = 'gn-name-button gn-accept';
      accept.innerHTML = '<kbd>Tab</kbd> Accept all'; accept.onclick = acceptAll;
      const cancel = document.createElement('button'); cancel.className = 'gn-discard'; cancel.textContent = 'Esc · Discard'; cancel.onclick = discard;
      status.append(accept,cancel);
    } else if (enabled && eligible().length >= 2) {
      const divider = document.createElement('span'); divider.className = 'gn-pill-divider';
      const button = document.createElement('button'); button.id = 'name-content'; button.className = 'gn-name-button';
      button.innerHTML = DioShell.icon('edit',16) + 'Name by content'; button.onclick = suggest;
      status.append(divider,button);
    }
    mark();
  }
  function decorate() {
    area.querySelectorAll('[data-name]').forEach(row => {
      const name = row.dataset.name;
      const label = row.querySelector('.dio-file-name');
      if (!label) return;
      let stack = label.closest('.gn-stack');
      if (!stack) { stack = document.createElement('span'); stack.className = 'gn-stack'; label.replaceWith(stack); stack.append(label); }
      stack.querySelectorAll('.gn-ghost,.gn-editor,.gn-error').forEach(el=>el.remove());
      if (!drafts.has(name)) return;
      if (editor === name) {
        const input = document.createElement('input'); input.id = 'ghost-editor'; input.className = 'gn-editor';
        input.value = drafts.get(name); input.dir = fa(name)?'rtl':'ltr'; input.setAttribute('aria-label','Suggested file name');
        input.onclick = event => {event.stopPropagation();input.select();};
        input.onpointerdown = event => {event.preventDefault();event.stopPropagation();input.focus();input.select();};
        input.oninput = () => { input.setCustomValidity(''); stack.querySelector('.gn-error')?.remove(); };
        stack.append(input);
      } else {
        const ghost = document.createElement('button'); ghost.type = 'button'; ghost.className = 'gn-ghost';
        ghost.dir = fa(name)?'rtl':'ltr';
        ghost.dataset.ghostIndex = originals.findIndex(f=>f.name===name);
        if (phase === 'reading') { const spinner = document.createElement('i'); spinner.className = 'gn-spinner'; ghost.append(spinner,document.createTextNode('Reading contents…')); ghost.disabled = true; }
        else { ghost.textContent = drafts.get(name); ghost.setAttribute('aria-label',`Edit suggested name: ${drafts.get(name)}`); ghost.onclick = event=>{event.stopPropagation();edit(name);}; }
        stack.append(ghost);
      }
    });
    mark();
  }
  function suggest() {
    if (!enabled || eligible().length < 2 || phase !== 'idle') return;
    shell.closePopup(); phase = 'reading';
    drafts = new Map(eligible().map(f=>[f.name,f.suggestion]));
    decorate(); pill();
    workTimer = setTimeout(()=>{phase='review';decorate();pill();guide(2);area.focus({preventScroll:true});},1100);
  }
  function edit(name) {
    if (phase !== 'review') return;
    if (editor && !saveEditor()) return;
    editor = name; decorate();
    if (step <= 3) guide(3);
    const input = host.querySelector('#ghost-editor'); input.focus(); input.select();
  }
  function saveEditor() {
    if (!editor) return true;
    const input = host.querySelector('#ghost-editor');
    const value = input.value.trim();
    const extension = editor.slice(editor.lastIndexOf('.'));
    const others = shell.files.filter(f=>!drafts.has(f.name)).map(f=>f.name.toLowerCase());
    const otherDrafts = [...drafts].filter(([name])=>name!==editor).map(([,name])=>name.toLowerCase());
    const invalid = !value || /[<>:"/\\|?*\u0000-\u001f]/.test(value) || /[. ]$/.test(value) || !value.endsWith(extension) || [...others,...otherDrafts].includes(value.toLowerCase());
    if (invalid) {
      let error = input.parentNode.querySelector('.gn-error');
      if (!error) {error=document.createElement('span');error.className='gn-error';input.after(error);}
      error.textContent = `Use a unique file name and keep ${extension}.`; input.focus();return false;
    }
    drafts.set(editor,value); editor=null;decorate();return true;
  }
  function acceptAll() {
    if (phase !== 'review' || !saveEditor()) return;
    const before = shell.files.map(f=>({...f})), selectedBefore = shell.selected.map(f=>f.name);
    const changed = [...drafts.values()];
    const count = drafts.size;
    const next = shell.files.map(f=>({...f,name:drafts.get(f.name)||f.name}));
    phase='idle';drafts.clear();shell.setFiles(next);shell.select(changed);decorate();pill();
    shell.toast(`Renamed ${count} files ·`,{duration:0,onAction:()=>{
      shell.setFiles(before);shell.select(selectedBefore);decorate();pill();guide(6);
    }});
    guide(5);
  }
  function discard() {
    clearTimeout(workTimer);editor=null;drafts.clear();phase='idle';decorate();pill();guide(1);
  }
  function option() {
    const popup = host.querySelector('.dio-popover');
    if (!popup || popup.querySelector('.gn-option')) return;
    const group = document.createElement('div');group.className='gn-option';
    const label = document.createElement('label');label.id='suggestion-option';
    const checkbox = document.createElement('input');checkbox.type='checkbox';checkbox.checked=enabled;checkbox.id='suggest-names';
    checkbox.onchange=()=>{
      enabled=checkbox.checked;
      try {localStorage.setItem('ghost-names-demo-enabled',String(enabled));}catch (_) {}
      if (!enabled) {clearTimeout(workTimer);phase='idle';editor=null;drafts.clear();decorate();}
      pill();if(step>=6&&!enabled) guide(8);
    };
    label.append(checkbox,document.createTextNode('Suggest names for unclear files'));group.append(label);popup.append(group);
    popup.style.left = `${Math.min(parseFloat(popup.style.left),win.clientWidth-popup.offsetWidth-8)}px`;
    if(step===6) guide(7);else mark();
  }
  host.addEventListener('click',event=>{
    if(event.target.closest('[data-action="view"]')) queueMicrotask(option);
  });
  host.addEventListener('dio:select',()=>{
    if (drafts.size && [...drafts.keys()].some(name=>!shell.selected.some(f=>f.name===name))) {
      clearTimeout(workTimer);phase='idle';drafts.clear();editor=null;
    }
    decorate();pill();
  });
  host.addEventListener('keydown',event=>{
    if(event.isComposing) return;
    if(event.key==='F2' && enabled && eligible().length>=2) {
      event.preventDefault();event.stopImmediatePropagation();
      const now=performance.now();
      if(f2At){suggest();f2At=0;}else f2At=now;
      return;
    }
    if(phase!=='review'&&phase!=='reading') return;
    if(event.key==='Escape') {event.preventDefault();event.stopImmediatePropagation();discard();return;}
    if(phase!=='review') return;
    if(editor && event.key==='Enter') {
      event.preventDefault();event.stopImmediatePropagation();
      if(saveEditor()){pill();guide(4);area.focus({preventScroll:true});}return;
    }
    if(event.key==='Tab'||event.key==='Enter') {event.preventDefault();event.stopImmediatePropagation();acceptAll();}
  },true);
  new MutationObserver(()=>{decorate();pill();}).observe(area,{childList:true});
  function reset() {
    clearTimeout(workTimer);phase='idle';drafts.clear();editor=null;f2At=0;enabled=true;
    try {localStorage.setItem('ghost-names-demo-enabled','true');}catch (_) {}
    shell.closePopup();host.querySelector('.dio-toast .dio-icon-button')?.click();
    shell.setFiles(originals);shell.setView('details');shell.select(originals.slice(0,6).map(f=>f.name));guide(1);area.focus({preventScroll:true});
  }
  shell.setView('details');
  shell.select(originals.slice(0,6).map(f=>f.name));
  if(!enabled) {enabled=true;try{localStorage.setItem('ghost-names-demo-enabled','true');}catch (_) {}}
  pill();guide(1);
})();
