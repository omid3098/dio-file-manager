'use strict';
(() => {
  const original = [
    {name:'Screenshot 2026-11-16 221403.png', kind:'file', size:1245184, date:'16 Nov, 22:14'},
    {name:'scan0003.pdf', kind:'file', size:384912, date:'16 Nov, 18:32'},
    {name:'scan0004.pdf', kind:'file', size:612370, date:'16 Nov, 18:34'},
    {name:'IMG_4471.jpg', kind:'file', size:2745832, date:'16 Nov, 14:08'},
    {name:'invoice-11-2026.pdf', kind:'file', size:128409, date:'15 Nov, 09:46'},
    {name:'قرارداد اجاره.pdf', kind:'file', size:742816, date:'14 Nov, 16:20'},
    {name:'پاسپورت اسکن.jpg', kind:'file', size:1286430, date:'14 Nov, 16:18'},
    {name:'setup-x64.exe', kind:'file', size:18559712, date:'12 Nov, 11:02'},
    {name:'photos.zip', kind:'file', size:32788314, date:'11 Nov, 20:15'}
  ];
  const suggestions = {
    'Screenshot 2026-11-16 221403.png':'dio-settings-keys-error 2026-11-16.png',
    'scan0003.pdf':'electricity-bill-10-2026.pdf',
    'scan0004.pdf':'قبض-گاز-۰۸-۱۴۰۵.pdf',
    'IMG_4471.jpg':'residence-permit-2026.jpg'
  };
  const host = document.querySelector('#shell');
  const shell = DioShell.create(host, {path:'Home\\Downloads', files:original,
    addedFolders:['Papers','Projects'], drives:[{name:'Local Disk (C:)',used:84},{name:'Data (E:)',used:38}]});
  const win = shell.element;
  let step = 1, enabled = true, editing = null, timer = null;
  const row = name => host.querySelector(`[data-name="${CSS.escape(name)}"]`);
  const ext = name => name.slice(name.lastIndexOf('.'));
  const stem = name => name.slice(0, name.lastIndexOf('.'));
  const machineName = name => /^(Screenshot[ _-]|IMG[_-]|scan\d|Untitled(?:[ ._-]|$)|[a-f\d]{20,}\.)/i.test(name);
  const hints = [
    '',
    'فرض کن F2 رو زدی و ایجنت عکس رو خونده؛ Tab رو بزن تا اسم خاکستری قبول بشه.',
    'حالا Enter رو بزن تا فایل با همین اسم ذخیره بشه.',
    'برای چند فایل با هم، روی اسم scan0003.pdf کلیک کن.',
    'حالا کادر کنار scan0004.pdf رو هم تیک بزن.',
    'دو اسکن انتخاب شدن؛ F2 رو بزن تا اسم‌های پیشنهادی رو ببینی.',
    'فرض کن ایجنت اسکن‌ها رو خونده؛ اسم‌ها قابل ویرایشن. Rename رو بزن.',
    'هر دو اسم عوض شدن؛ Undo رو بزن تا اسم‌های قبلی برگردن.',
    'برای خاموش کردن پیشنهادها، روی IMG_4471.jpg کلیک کن.',
    'حالا F2 رو بزن تا پیشنهاد اسم این عکس هم بیاد.',
    'روی فلش کوچیک کنار Tab کلیک کن.',
    'روی Stop suggesting names کلیک کن.',
    'اسم‌ها از محتوای فایل اومدن، با Undo برگشتن و پیشنهادها هم خاموش شدن.'
  ];
  function mark(target) {
    host.querySelectorAll('.guide-target').forEach(el=>el.classList.remove('guide-target'));
    if (target) target.classList.add('guide-target');
  }
  function guide(next, override) {
    step=next;
    document.body.dataset.step=String(next);
    shell.setHint(override || hints[step], step, 12);
    win.querySelector('.f2-key')?.remove();
    if (step===1) mark(win.querySelector('#accept-name'));
    if (step===2) mark(win.querySelector('#commit-name'));
    if (step===3) mark(row('scan0003.pdf')?.querySelector('.dio-file-name'));
    if (step===4) mark(row('scan0004.pdf')?.querySelector('.pick'));
    if (step===5 || step===9) {
      const first = row(shell.selected[0]?.name);
      const key = document.createElement('button');
      key.className='key f2-key'; key.id='rename-key'; key.textContent='F2';
      key.setAttribute('aria-label','Rename selected files (F2)');
      key.style.top=`${(first?.getBoundingClientRect().top || win.getBoundingClientRect().top+100)-win.getBoundingClientRect().top+8}px`;
      key.onclick=()=>openRename(); win.append(key); mark(key);
    }
    if (step===6) mark(win.querySelector('#batch-rename'));
    if (step===7) mark(win.querySelector('.dio-toast-action'));
    if (step===8) mark(row('IMG_4471.jpg')?.querySelector('.dio-file-name'));
    if (step===10) mark(win.querySelector('.suggestion-menu'));
    if (step===11) mark(win.querySelector('#stop-suggesting'));
    if (step===12) {
      mark(null);
      const restart=document.createElement('button'); restart.className='restart';restart.id='restart';
      restart.textContent='از اول';restart.onclick=reset;
      host.querySelector('.dio-try-strip p').append(restart);
    }
  }
  function decorate() {
    host.querySelectorAll('[data-name]').forEach(el=>{
      if (el.querySelector('.pick')) return;
      const pick=document.createElement('button');
      pick.className='pick'; pick.dataset.file=el.dataset.name;
      pick.setAttribute('aria-label',`Select ${el.dataset.name}`);
      pick.setAttribute('aria-pressed', String(shell.selected.some(f=>f.name===el.dataset.name)));
      pick.innerHTML=DioShell.icon('check',14);
      el.querySelector('.dio-list-name')?.prepend(pick);
    });
  }
  function syncChecks() {
    host.querySelectorAll('.pick').forEach(p=>p.setAttribute('aria-pressed',String(shell.selected.some(f=>f.name===p.dataset.file))));
  }
  function closeEditor() {
    clearTimeout(timer);
    if (editing?.label) editing.label.hidden=false;
    host.querySelectorAll('.is-editing').forEach(el=>el.classList.remove('is-editing'));
    win.querySelector('.rename-inline')?.remove();
    win.querySelector('.batch')?.remove();
    win.querySelector('.suggest-menu')?.remove();
    editing=null;
  }
  function valid(names, oldNames) {
    if (names.some(name=>!stem(name).trim() || /[<>:"/\\|?*\x00-\x1f]/.test(name) || /[. ]$/.test(stem(name)))) return 'Choose a valid file name.';
    const lower=names.map(n=>n.toLocaleLowerCase());
    if (new Set(lower).size!==lower.length || shell.files.some(f=>!oldNames.includes(f.name) && lower.includes(f.name.toLocaleLowerCase()))) return 'That name is already in use.';
    return '';
  }
  function applyNames(oldNames, newNames, batch=false) {
    const error=valid(newNames,oldNames);
    if (error) {shell.toast(error,{action:null});return;}
    const before=shell.files.map(f=>({...f}));
    closeEditor();
    shell.setFiles(before.map(f=>{const n=oldNames.indexOf(f.name);return n<0 ? f : {...f,name:newNames[n]};}));
    decorate();shell.select(newNames);syncChecks();
    shell.toast(batch?'2 files renamed':'File renamed', {duration:0, onAction:()=>{
      closeEditor();shell.setFiles(before);decorate();shell.select(oldNames);syncChecks();
      if (batch && step===7) guide(8);
    }});
    if (step===2) guide(3);
    else if(batch && step===6) guide(7);
  }
  function sizeInput(input) {
    const canvas=document.createElement('canvas');const c=canvas.getContext('2d');
    c.font='14px Inter';input.style.width=`${Math.ceil(c.measureText(input.value).width)+4}px`;
  }
  function acceptSuggestion() {
    if (!editing?.suggestion) return;
    const input=editing.input;input.value=stem(editing.suggestion);sizeInput(input);
    editing.suggestion=null;win.querySelector('.ghost')?.remove();
    win.querySelector('#accept-name')?.remove();win.querySelector('.suggestion-menu')?.remove();
    const commit=win.querySelector('#commit-name');commit.hidden=false;
    input.focus();input.setSelectionRange(input.value.length,input.value.length);
    if(step===1)guide(2);
  }
  function stopSuggesting() {
    enabled=false;
    if(editing)editing.suggestion=null;
    win.querySelector('.ghost')?.remove(); win.querySelector('.suggest-menu')?.remove();
    win.querySelector('#accept-name')?.remove();win.querySelector('.suggestion-menu')?.remove();
    win.querySelector('#commit-name').hidden=false;
    editing?.input.focus();guide(12);
  }
  function openSuggestionMenu() {
    if(win.querySelector('.suggest-menu')) {win.querySelector('.suggest-menu').remove();return;}
    const anchor=win.querySelector('.suggestion-menu').getBoundingClientRect();
    const b=win.getBoundingClientRect();const menu=document.createElement('div');
    menu.className='suggest-menu dio-appear';menu.setAttribute('role','menu');
    menu.innerHTML=`<button id="stop-suggesting" role="menuitem">${DioShell.icon('close')}Stop suggesting names</button>`;
    menu.style.right='24px';menu.style.top=`${anchor.bottom-b.top+8}px`;
    menu.querySelector('button').onclick=stopSuggesting;
    menu.addEventListener('click',e=>e.stopPropagation());win.append(menu);
    menu.querySelector('button').focus();if(step===10)guide(11);
  }
  function openSingle(file, initial=false) {
    const label=row(file.name)?.querySelector('.dio-file-name');if(!label)return;
    label.hidden=true;
    row(file.name).classList.add('is-editing');
    const field=document.createElement('div');field.className='rename-inline dio-appear';
    field.innerHTML='<input id="name-input" aria-label="New file name"><span class="extension"></span><span class="ghost" hidden></span><span class="rename-keys"><button id="accept-name" class="key" hidden aria-label="Accept suggested name (Tab)">Tab</button><button class="suggestion-menu" hidden aria-label="Name suggestion options"></button><button id="commit-name" class="key" hidden aria-label="Confirm rename (Enter)">Enter</button></span>';
    field.addEventListener('click',e=>e.stopPropagation());
    const input=field.querySelector('input');input.value=stem(file.name);sizeInput(input);
    field.querySelector('.extension').textContent=ext(file.name);
    field.querySelector('.suggestion-menu').innerHTML=DioShell.icon('down',12);
    row(file.name).querySelector('.dio-list-name').append(field);
    editing={oldName:file.name,input,label,suggestion:null};
    input.focus();input.select();
    input.addEventListener('input',()=>{
      clearTimeout(timer);editing.suggestion=null;sizeInput(input);
      field.querySelector('.ghost')?.setAttribute('hidden','');
      field.querySelector('#accept-name')?.setAttribute('hidden','');field.querySelector('.suggestion-menu')?.setAttribute('hidden','');
      field.querySelector('#commit-name').hidden=false;
      if(step===1) guide(2,'اسم خودت رو نوشتی؛ Enter رو بزن تا ذخیره بشه.');
    });
    field.querySelector('#accept-name').onclick=acceptSuggestion;
    field.querySelector('#commit-name').onclick=()=>applyNames([file.name],[input.value+ext(file.name)]);
    field.querySelector('.suggestion-menu').onclick=openSuggestionMenu;
    if(enabled && machineName(file.name) && suggestions[file.name]) {
      timer=setTimeout(()=>{
        if(!editing || editing.oldName!==file.name)return;
        editing.suggestion=suggestions[file.name];
        const ghost=field.querySelector('.ghost');ghost.textContent=editing.suggestion;ghost.hidden=false;
        field.querySelector('#accept-name').hidden=false;field.querySelector('.suggestion-menu').hidden=false;
        if(initial)guide(1);else if(step===9)guide(10);
      }, initial ? 0 : 280);
    } else field.querySelector('#commit-name').hidden=false;
  }
  function openBatch(files) {
    const batch=document.createElement('section');batch.className='batch dio-appear';
    batch.setAttribute('role','dialog');batch.setAttribute('aria-label','Rename selected files');
    batch.innerHTML=`<div class="batch-header"><h2>Rename ${files.length} files</h2><button class="dio-icon-button" id="close-batch" aria-label="Close rename">${DioShell.icon('close')}</button></div><div class="batch-labels"><span>Current name</span><span>Suggested name</span></div><div id="batch-rows"></div><p class="form-error" hidden></p><div class="batch-footer"><span class="agent-source">${DioShell.icon('check',16)}Read by your Codex · Folder naming style</span><div class="batch-actions"><button class="dio-secondary" id="cancel-batch">Cancel</button><button class="dio-primary" id="batch-rename">Rename</button></div></div>`;
    files.forEach((f,i)=>{
      const line=document.createElement('div');line.className='batch-row';
      const old=document.createElement('span');old.className='batch-old';old.textContent=f.name;
      const entry=document.createElement('label');entry.className='batch-entry';
      const input=document.createElement('input');input.id=`batch-name-${i}`;
      input.setAttribute('aria-label',`New name for ${f.name}`);
      const proposed=enabled && machineName(f.name) ? suggestions[f.name] || f.name : f.name;
      input.value=stem(proposed); input.dir=/[\u0600-\u06ff]/.test(proposed)?'rtl':'ltr';
      const suffix=document.createElement('span');suffix.className='batch-extension';suffix.textContent=ext(f.name);
      entry.append(input,suffix);line.append(old,entry);batch.querySelector('#batch-rows').append(line);
    });
    batch.querySelector('#batch-rename').onclick=()=>{
      const names=files.map((f,i)=>batch.querySelector(`#batch-name-${i}`).value+ext(f.name));
      const error=valid(names,files.map(f=>f.name));
      if(error){const p=batch.querySelector('.form-error');p.hidden=false;p.textContent=error;return;}
      applyNames(files.map(f=>f.name),names,true);
    };
    batch.querySelector('#cancel-batch').onclick=closeEditor;batch.querySelector('#close-batch').onclick=closeEditor;
    batch.addEventListener('click',e=>e.stopPropagation());
    win.append(batch);batch.querySelector('input')?.focus();if(step===5)guide(6);
  }
  function openRename() {
    const selected=shell.selected;if(!selected.length)return;
    closeEditor();shell.closePopup();
    if(selected.length>1)openBatch(selected);else openSingle(selected[0]);
  }
  host.addEventListener('click',e=>{
    if(e.target.closest('.rename-inline,.batch,.suggest-menu,.f2-key'))return;
    const pick=e.target.closest('.pick');
    if(pick){
      e.stopImmediatePropagation();closeEditor();
      const names=shell.selected.map(f=>f.name);
      shell.select(names.includes(pick.dataset.file)?names.filter(n=>n!==pick.dataset.file):[...names,pick.dataset.file]);
      syncChecks();
      if(step===4 && pick.dataset.file==='scan0004.pdf')guide(5);
    }
  },true);
  host.addEventListener('click',e=>{
    syncChecks();
    if(step===3 && e.target.closest('[data-name]')?.dataset.name==='scan0003.pdf')guide(4);
    if(step===8 && e.target.closest('[data-name]')?.dataset.name==='IMG_4471.jpg')guide(9);
  });
  document.addEventListener('keydown',e=>{
    if(e.key==='F2') {e.preventDefault();e.stopImmediatePropagation();openRename();return;}
    if(e.key==='Escape' && (editing || win.querySelector('.batch'))) {e.preventDefault();e.stopImmediatePropagation();closeEditor();return;}
    if(editing && !e.isComposing){
      if(e.key==='Tab' && !e.shiftKey && editing.suggestion){e.preventDefault();e.stopImmediatePropagation();acceptSuggestion();}
      else if(e.key==='Enter'){e.preventDefault();e.stopImmediatePropagation();applyNames([editing.oldName],[editing.input.value+ext(editing.oldName)]);}
    }
  },true);
  function reset() {
    closeEditor();enabled=true;step=1;
    host.querySelector('.dio-toast .dio-icon-button')?.click();
    shell.setFiles(original);shell.setView('details');decorate();shell.select(original[0].name);syncChecks();
    openSingle(shell.selected[0],true);guide(1);
  }
  document.fonts.ready.then(reset);
})();
