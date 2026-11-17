(() => {
  'use strict';
  const GB=1073741824;
  const tasks=[
    {name:'thumbnail-fix',kind:'folder',size:1.1*GB,date:'Nov 8, 22:14',verdict:'safe',reason:'Branch merged; no work left'},
    {name:'image-tests',kind:'folder',size:.8*GB,date:'Nov 8, 22:36',verdict:'safe',reason:'Test captures; originals are in src'},
    {name:'preview-cache',kind:'folder',size:.6*GB,date:'Nov 8, 21:02',verdict:'safe',reason:'Generated previews; can be rebuilt'},
    {name:'export-check',kind:'folder',size:.4*GB,date:'Nov 7, 16:40',verdict:'safe',reason:'Export verified; source is saved'},
    {name:'nav-spacing',kind:'folder',size:.2*GB,date:'Nov 6, 12:18',verdict:'safe',reason:'Branch merged; worktree is clean'},
    {name:'old-logs',kind:'folder',size:.1*GB,date:'Nov 5, 10:24',verdict:'safe',reason:'Completed sessions; no open handles'},
    {name:'search-agent',kind:'folder',size:.18*GB,date:'Today, 09:12',verdict:'keep',reason:'Codex is still writing here'},
    {name:'طرح تازه',kind:'folder',size:.02*GB,date:'Yesterday, 18:31',verdict:'unsure',reason:'Uncommitted work; no session record'}
  ];
  const rootFiles=[
    {name:'.agent-work',kind:'folder',size:3.4*GB,date:'Today, 09:12',files:tasks},
    {name:'.git',kind:'folder',size:22*1048576,date:'Today, 09:10'},
    {name:'src',kind:'folder',size:144*1048576,date:'Yesterday, 22:14'},
    {name:'build',kind:'folder',size:308*1048576,date:'Yesterday, 22:38'},
    {name:'README.md',kind:'file',size:4296,date:'Nov 12, 16:20'},
    {name:'package.json',kind:'file',size:1760,date:'Nov 12, 16:20'},
    {name:'قرارداد اجاره.pdf',kind:'file',size:524288,date:'Nov 10, 11:48'}
  ];
  const shell=DioShell.create('#shell',{path:'E:\\w\\website',files:rootFiles,addedFolders:['website','game-tools'],drives:[{name:'Local Disk (C:)',used:94},{name:'Local Disk (E:)',used:62}]});
  const host=document.querySelector('#shell');
  const status=()=>host.querySelector('.dio-status');
  let step=1, enabled=true, offer=false, selectionKey='', selectionTimer, workTimer, guided=false, batch=false;
  const hints=[
    'فرض کن چند ثانیه روی .agent-work مکث کردی؛ حالا روی «Why is this here?» کلیک کن.',
    'این پوشه کارِ ناتموم هم داره؛ روی «Review 8 folders» کلیک کن تا داخلش رو ببینی.',
    'فرض کن روی این ۸ پوشه هم مکث کردی؛ دوباره «Why is this here?» رو بزن.',
    'ایجنت دلیل هر حکم رو گفته؛ روی «Trash the 6 safe ones» کلیک کن.',
    'فقط ۶ پوشهٔ امن رفتن توی سطل؛ روی «Undo» کلیک کن تا برگردن.',
    'هر ۸ پوشه برگشتن؛ دوباره «Why is this here?» رو باز کن.',
    'برای خاموش کردن این پیشنهاد، کلید «Don’t offer this» رو بزن.',
    'دیدی چی ساخته شده، کدوم هنوز لازمه و چی رو می‌شه با خیال راحت پاک کرد.'
  ];
  function guide(next=step) {
    step=next;
    host.querySelectorAll('.guided').forEach(el=>el.classList.remove('guided'));
    shell.setHint(hints[step-1],step,hints.length);
    host.querySelector('#restart')?.remove();
    if(step===8) {
      const restart=document.createElement('button');restart.id='restart';restart.className='restart';restart.textContent='از اول';restart.onclick=reset;
      host.querySelector('.dio-try-strip p').append(restart);
      return;
    }
    const targets={1:'#why-link',2:'#review-folders',3:'#why-link',4:'#trash-safe',5:'.dio-toast-action',6:'#why-link',7:'#offer-switch'};
    host.querySelector(targets[step])?.classList.add('guided');
  }
  function enhance() {
    const el=status();
    if(!shell.selected.length)return;
    const summary=document.createElement('span');summary.textContent=el.textContent;el.replaceChildren(summary);
    if(!offer||!enabled)return;
    const link=document.createElement('button');link.id='why-link';link.className='why-link';
    link.innerHTML='Why is this here?<span class="key-cap" aria-hidden="true">?</span>';
    link.setAttribute('aria-keyshortcuts','?');link.onclick=openWhy;el.append(link);
    guide();
  }
  function refreshOffer() { shell.select(shell.selected.map(f=>f.name)); }
  host.addEventListener('dio:select',e=>{
    const key=e.detail.map(f=>f.name).join('|');
    if(key!==selectionKey) {
      selectionKey=key;offer=false;clearTimeout(selectionTimer);
      if(key&&enabled)selectionTimer=setTimeout(()=>{offer=true;refreshOffer();},guided?600:4000);
    }
    enhance();
  });
  function badge(type) { const text={safe:'Safe to trash',keep:'Keep',unsure:'Not sure'};return `<span class="verdict ${type}">${DioShell.icon(type==='safe'?'check':type==='keep'?'info':'info',14)}${text[type]}</span>`; }
  function openWhy() {
    if(!enabled||!shell.selected.length)return;
    clearTimeout(workTimer);
    const items=shell.selected;
    const body=document.createElement('div');
    body.innerHTML=`<header class="why-head"><h2>Why is this here?</h2><button id="offer-switch" class="offer-switch" role="switch" aria-checked="false" aria-label="Don't offer this"><span>Don't offer this</span><span class="switch-track"><i></i></span></button></header><div id="why-answer" aria-live="polite"><div class="working"><span class="working-dot"></span>Claude Code is checking contents, git and session logs…</div></div><footer class="why-foot">${DioShell.icon('terminal',14)}<span>Claude Code · your agent · on this PC</span></footer>`;
    const popup=shell.popover(status(),{title:'',content:body});popup.classList.add('why-pop');popup.setAttribute('aria-label','Why is this here?');
    body.querySelector('#offer-switch').onclick=()=>{
      enabled=false;offer=false;clearTimeout(selectionTimer);shell.closePopup();refreshOffer();
      shell.toast('“Why is this here?” offers are off',{duration:0,onAction:()=>{enabled=true;offer=true;refreshOffer();shell.toast('Offers are on again',{action:null,duration:2500});}});
      guide(8);
    };
    const nextStep=batch?(step===6?7:4):2;
    workTimer=setTimeout(()=>{
      if(!popup.isConnected)return;
      const answer=body.querySelector('#why-answer');
      if(batch) {
        const rows=items.filter(f=>f.verdict);
        answer.innerHTML=`<p class="batch-title">${rows.length} folders in .agent-work</p><ul class="batch-list">${rows.map(f=>`<li class="batch-row"><div><div class="batch-name">${DioShell.icon('folder',16)}<span class="${/[\u0600-\u06ff]/.test(f.name)?'fa-file':''}" dir="auto">${f.name}</span></div><div class="batch-reason">${f.reason}</div></div><span class="batch-size">${f.size<.1*GB?DioShell.formatSize(f.size):(f.size/GB).toFixed(1)+' GB'}</span>${badge(f.verdict)}</li>`).join('')}</ul>${rows.some(f=>f.verdict==='safe')?'<button id="trash-safe" class="dio-primary">Trash the 6 safe ones · 3.2 GB</button>':''}`;
        answer.querySelector('#trash-safe')?.addEventListener('click',trashSafe);
      } else {
        const file=items[0];
        const isWork=file.name==='.agent-work';
        answer.innerHTML=`<div class="why-subject">${DioShell.icon(file.kind==='folder'?'folder':'file',28)}<div><div class="subject-name">${file.name}</div><div class="quiet">${isWork?'8 work folders · 3.4 GB':'Selected item'}</div></div></div><dl class="facts"><dt>Made by</dt><dd>${isWork?'Claude Code · yesterday, 22:14<small>Thumbnail fix, plus 7 other tasks</small>':'No matching session record'}</dd><dt>Used by</dt><dd>${isWork?'6 branches merged; one task still running<small>Checked contents, git worktrees and local session logs</small>':'No reliable usage evidence'}</dd></dl><div class="decision">${badge('unsure')}<span class="decision-reason">${isWork?'Some work inside is still active.':'Not enough evidence to delete it.'}</span></div>${isWork?'<button id="review-folders" class="dio-primary">Review 8 folders</button>':''}`;
        answer.querySelector('#review-folders')?.addEventListener('click',()=>{
          shell.closePopup();batch=true;guided=true;shell.setFolder('E:\\w\\website\\.agent-work',tasks);shell.setView('details');shell.select(tasks.map(f=>f.name));clearTimeout(selectionTimer);offer=true;refreshOffer();guide(3);
        });
      }
      guide(nextStep);
    },650);
  }
  function trashSafe() {
    const before=[...shell.files];const safe=shell.selected.filter(f=>f.verdict==='safe');const names=new Set(safe.map(f=>f.name));
    console.assert(safe.length===6&&Math.abs(safe.reduce((sum,f)=>sum+f.size,0)/GB-3.2)<.00001,'Only the six safe folders total 3.2 GB');
    shell.closePopup();shell.setFiles(before.filter(f=>!names.has(f.name)));shell.select([]);
    console.assert(shell.files.some(f=>f.verdict==='keep')&&shell.files.some(f=>f.verdict==='unsure'),'Active and unknown work must remain');
    shell.toast('6 folders moved to Trash · 3.2 GB',{duration:0,onAction:()=>{
      shell.setFiles(before);shell.select(before.map(f=>f.name));offer=true;refreshOffer();guide(6);
      console.assert(shell.files.length===8,'Undo must restore all eight folders');
    }});
    guide(5);
  }
  // Doubt signals reuse the shell's normal Properties and Delete/Undo actions.
  host.addEventListener('click',e=>{
    if(e.target.closest('.dio-menu-item')?.textContent.includes('Properties')){offer=true;refreshOffer();}
  },true);
  document.addEventListener('keydown',e=>{
    if(e.target.matches('input,textarea'))return;
    if(e.key==='?'&&enabled&&shell.selected.length){e.preventDefault();e.stopImmediatePropagation();openWhy();}
    if(e.altKey&&e.key==='Enter'&&shell.selected.length){e.preventDefault();e.stopImmediatePropagation();offer=true;refreshOffer();shell.dialog({title:shell.selected[0].name,message:'Folder · '+DioShell.formatSize(shell.selected[0].size),actions:[{label:'Close'}]});}
    if(e.ctrlKey&&e.key.toLowerCase()==='z'&&enabled)setTimeout(()=>{offer=true;refreshOffer();},0);
  },true);
  host.addEventListener('click',e=>{if(e.target.closest('.dio-toast-action')&&enabled)setTimeout(()=>{offer=true;refreshOffer();},0);},true);
  function reset() {
    clearTimeout(selectionTimer);clearTimeout(workTimer);shell.closePopup();
    host.querySelector('.dio-toast .dio-icon-button')?.click();
    enabled=true;offer=false;guided=false;batch=false;step=1;selectionKey='';
    shell.setFolder('E:\\w\\website',rootFiles);shell.setView('grid');
    // Restore the shell's medium icon size after coming back from Details.
    host.querySelector('[data-action="view"]').click();
    const slider=host.querySelector('.dio-slider');slider.value=3;slider.dispatchEvent(new Event('input',{bubbles:true}));shell.closePopup();
    shell.select('.agent-work');
    clearTimeout(selectionTimer);offer=true;refreshOffer();guide(1);
  }
  reset();
})();
