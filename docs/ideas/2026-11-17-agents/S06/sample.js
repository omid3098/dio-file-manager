(() => {
  'use strict';
  const folder = 'Documents\\Papers';
  const storageKey = 'dio-about-lines-S06';
  const originals = [
    {name:'bank-statement-10-2026.pdf', size:284216, date:'2 Nov 2026', line:'Bank statement, October 2026, 4 pages'},
    {name:'پاسپورت اسکن.jpg', size:2381440, date:'12 Oct 2026', line:'Passport scan, photo page, expires 2031'},
    {name:'residence-permit-2026.pdf', size:782630, date:'5 Sep 2026', line:'Residence permit, valid until September 2027'},
    {name:'قرارداد اجاره.pdf', size:1842150, date:'18 Mar 2024', line:'Rental contract, signed March 2024, 12 pages'},
    {name:'scan0001.pdf', size:512148, date:'8 Nov 2026', line:'مدرک بیمهٔ درمانی، صادرشده در آبان ۱۴۰۵'},
    {name:'scan0002.pdf', size:673201, date:'10 Nov 2026', line:'Registration certificate, current home address'},
    {name:'نامه اداره.pdf', size:294118, date:'14 Nov 2026', line:'نامهٔ اداره، درخواست کپی قرارداد اجاره'},
    {name:'invoice-11-2026.pdf', size:192834, date:'15 Nov 2026', line:'Electricity bill, November 2026, €84.50'}
  ].map(file => ({...file, kind:'file', revision:1}));
  let records = originals.map(f=>({...f})), cache = {}, enabled = true, ignoredDay = '', stage = 0;
  let mode = 'details', query = '', busy = false, offerVisible = true, offerTimer, timers = [];
  const today = () => new Date().toLocaleDateString('en-CA');
  try {
    const stored = JSON.parse(localStorage.getItem(storageKey) || '{}');
    cache = stored.cache || {}; enabled = stored.enabled !== false; ignoredDay = stored.ignoredDay || '';
  } catch { /* A private browser may deny local storage; the mock still works. */ }
  const upToDate = f => cache[f.name]?.revision === f.revision;
  if (records.every(upToDate)) { stage = 1; offerVisible = false; }
  if (!enabled || ignoredDay === today()) offerVisible = false;
  const shell = DioShell.create('#shell', {path:folder, files:records, addedFolders:['Papers'], onOpen:preview});
  const host = document.querySelector('#shell');
  const fileArea = host.querySelector('.dio-files');
  const content = host.querySelector('.dio-content');
  const viewButton = host.querySelector('[data-action="view"]');
  const pathbar = host.querySelector('.dio-path');
  const pill = document.createElement('div');
  pill.className = 'about-pill'; pill.setAttribute('role','status'); pill.setAttribute('aria-live','polite'); content.append(pill);
  const guides = [
    'فرض کن این سه فایل رو باز کردی و بستی؛ حالا «Tell me what these are» رو بزن.',
    'خط‌ها زیر اسم‌ها اومدن؛ حالا روی ذره‌بین کلیک کن.',
    'توی جست‌وجو بنویس «rental»؛ حالا متن خط‌ها هم پیدا می‌شه.',
    'قرارداد با معنی‌اش پیدا شد؛ روی ضربدر جست‌وجو کلیک کن.',
    'حالا View options رو باز کن تا خط‌ها رو توی نمای آیکن هم ببینی.',
    'روی Grid کلیک کن؛ توضیح هر فایل زیر اسمش می‌مونه.',
    'روی Papers توی نوار مسیر بزن؛ خط‌های ذخیره‌شده همون لحظه برمی‌گردن.',
    'فرض کن یه اسکن تازه رسیده؛ این بار فقط همون فایل رو به ایجنت بسپر.',
    'فقط فایل تازه خونده شد؛ حالا View options رو باز کن.',
    'اگه خط‌ها رو نمی‌خوای، کلید About lines رو خاموش کن.',
    'خط‌ها مخفی شدن؛ برای پاک‌کردن توضیح‌های ذخیره‌شده، Clear about lines رو بزن.',
    'فایل‌ها با یه خط خوانا پیدا شدن؛ اسم و جای هیچ فایلی عوض نشد.'
  ];
  const targets = ['#describe','[data-action="search"]','#about-search','#clear-search','[data-action="view"]','#grid-mode','.dio-crumb:last-child','#describe','[data-action="view"]','#about-toggle','#clear-lines',null];
  function save() {
    // shortcut: browser storage stands in for one local folder file; replace for the native feature.
    try { localStorage.setItem(storageKey,JSON.stringify({cache,enabled,ignoredDay})); } catch {}
  }
  function guide() {
    if(busy) {
      shell.setHint('اینجا خوندن ایجنت شبیه‌سازی می‌شه؛ پنجره همچنان کار می‌کنه.',stage+1,guides.length);
      document.querySelectorAll('.next-target').forEach(el=>el.classList.remove('next-target'));
      pill.classList.add('next-target'); return;
    }
    shell.setHint(guides[stage], stage+1, guides.length);
    document.querySelectorAll('.next-target').forEach(el=>el.classList.remove('next-target'));
    const target = targets[stage] && host.querySelector(targets[stage]);
    target?.classList.add('next-target');
    if (stage === 11) {
      const restart = document.createElement('button'); restart.className='restart'; restart.id='restart'; restart.textContent='از اول';
      restart.onclick=reset; host.querySelector('.dio-try-strip p').append(restart);
    }
  }
  function advance(next) { stage = next; guide(); }
  function matchedText(text) {
    const span=document.createElement('span');
    if (!query) { span.textContent=text; return span; }
    let offset=0, index;
    while ((index=text.toLocaleLowerCase().indexOf(query.toLocaleLowerCase(),offset))!==-1) {
      span.append(document.createTextNode(text.slice(offset,index)));
      const mark=document.createElement('mark');mark.textContent=text.slice(index,index+query.length);span.append(mark);offset=index+query.length;
    }
    span.append(document.createTextNode(text.slice(offset))); return span;
  }
  function decorate() {
    fileArea.querySelectorAll('[data-name]').forEach(row=>{
      const file=records.find(f=>f.name===row.dataset.name); if(!file)return;
      const name=row.querySelector('.dio-file-name'); if(!name)return;
      name.textContent=file.name;
      if (!enabled || !upToDate(file) || row.querySelector('.about-line')) return;
      const line=document.createElement('span');line.className='about-line';line.setAttribute('dir',/[\u0600-\u06ff]/.test(cache[file.name].line)?'rtl':'ltr');
      line.append(matchedText(cache[file.name].line));
      if (row.tagName==='TR') {
        const stack=document.createElement('span');stack.className='name-stack';name.replaceWith(stack);stack.append(name,line);
      } else row.append(line);
      row.dataset.described='true';
    });
  }
  function render(freshName) {
    const shown = records.filter(f=>!query || (f.name+' '+(enabled && upToDate(f)?cache[f.name].line:'')).toLocaleLowerCase().includes(query.toLocaleLowerCase()));
    shell.setFiles(shown); decorate();
    if(freshName) fileArea.querySelector(`[data-name="${CSS.escape(freshName)}"] .about-line`)?.classList.add('fresh');
    drawPill(); guide();
  }
  function drawPill(progress) {
    if(busy && progress===undefined)return;
    pill.hidden=false;
    if (busy) { pill.innerHTML=`<span class="progress-ring" aria-label="Reading files"></span><span>Claude Code · Reading ${progress} of ${timers.length}</span>`;return; }
    const pending=records.filter(f=>!upToDate(f));
    if(enabled && offerVisible && pending.length) {
      pill.innerHTML=`<span class="pill-count">${Object.keys(cache).length ? pending.length+' new file' : records.length+' files'}</span><button id="describe" class="offer-button">${DioShell.icon('file')}<span>${pending.length===1?'Tell me what this is':'Tell me what these are'} · Claude Code</span></button><button id="dismiss-offer" class="pill-dismiss" aria-label="Dismiss offer">${DioShell.icon('close',14)}</button>`;
      pill.querySelector('#describe').onclick=describe;
      pill.querySelector('#dismiss-offer').onclick=dismissOffer;
    } else if(enabled && Object.keys(cache).length) {
      pill.innerHTML=`<span class="pill-success">${DioShell.icon('check')} ${records.filter(upToDate).length} about lines</span><span class="pill-count">Saved locally</span>`;
    } else { const selected=shell.selected; pill.textContent=selected.length ? selected[0].name+' selected' : records.length+' files'; }
  }
  function dismissOffer() {
    offerVisible=false; ignoredDay=today();save();drawPill();
    if(stage===0 || stage===7) {
      shell.setHint('پیشنهاد برای امروز رفت؛ برای دیدن نمونه از اول شروع کن.',stage+1,guides.length);
      host.querySelectorAll('.next-target').forEach(el=>el.classList.remove('next-target'));
      const restart=document.createElement('button');restart.className='restart';restart.id='restart';restart.textContent='از اول';restart.onclick=reset;host.querySelector('.dio-try-strip p').append(restart);
    }
  }
  function describe() {
    clearTimeout(offerTimer); if(busy)return;
    const pending=records.filter(f=>!upToDate(f)); if(!pending.length)return;
    busy=true;timers=new Array(pending.length);drawPill(0);
    document.querySelectorAll('.next-target').forEach(el=>el.classList.remove('next-target'));
    pill.classList.add('next-target');
    shell.setHint('اینجا خوندن ایجنت شبیه‌سازی می‌شه؛ پنجره همچنان کار می‌کنه.',stage+1,guides.length);
    pending.forEach((file,i)=>{timers[i]=setTimeout(()=>{
      cache[file.name]={revision:file.revision,line:file.line};save();render(file.name);drawPill(i+1);
      // Keep the working pill marked while each line arrives.
      if(i<pending.length-1) {document.querySelectorAll('.next-target').forEach(el=>el.classList.remove('next-target'));pill.classList.add('next-target');}
      else {busy=false;offerVisible=false;drawPill();advance(stage===7?8:1);}
    },600+i*220);});
  }
  function searchOpen() {
    shell.closePopup();pathbar.innerHTML='<input id="about-search" class="dio-search" aria-label="Search names and about lines" placeholder="Search names and about lines"><button id="clear-search" class="dio-icon-button" aria-label="Clear search">'+DioShell.icon('close',16)+'</button>';
    const input=pathbar.querySelector('input'); input.value=query;input.focus();
    input.oninput=()=>{query=input.value;render();if(stage===2 && query.toLocaleLowerCase().includes('rental'))advance(3);};
    pathbar.querySelector('#clear-search').onclick=()=>{query='';shell.setFolder(folder,records);render();if(stage===3)advance(4);};
    if(stage===1)advance(2);else guide();
  }
  function showOptions() {
    const body=document.createElement('div');body.className='view-content';
    body.innerHTML=`<div class="view-modes"><button id="details-mode" aria-pressed="${mode==='details'}">${DioShell.icon('list')} Details</button><button id="grid-mode" aria-pressed="${mode==='grid'}">${DioShell.icon('grid')} Grid</button></div><div class="about-option"><div class="option-row"><span>About lines</span><button id="about-toggle" class="toggle" role="switch" aria-label="About lines" aria-checked="${enabled}"><i></i></button></div><span class="view-note">${Object.keys(cache).length} lines saved in this folder</span><button id="clear-lines" class="clear-lines" ${Object.keys(cache).length?'':'disabled'}>${DioShell.icon('trash')} Clear about lines</button></div>`;
    body.querySelectorAll('.view-modes button').forEach(b=>b.onclick=()=>{mode=b.id==='grid-mode'?'grid':'details';shell.setView(mode);shell.closePopup();render();if(stage===5)advance(6);});
    body.querySelector('#about-toggle').onclick=e=>{enabled=!enabled;save();render();e.currentTarget.setAttribute('aria-checked',String(enabled));if(stage===9 && !enabled)advance(10);};
    body.querySelector('#clear-lines').onclick=()=>{
      const previous=cache;cache={};save();shell.closePopup();render();
      shell.toast('About lines cleared. Your files are unchanged.',{action:'Undo',onAction:()=>{cache=previous;save();render();}});
      if(stage===10)advance(11);
    };
    shell.popover(viewButton,{title:'View Options',content:body});
    if(stage===4)advance(5);else if(stage===8)advance(9);else guide();
  }
  function reopen() {
    query='';shell.closePopup();shell.setFolder(folder,records);render();
    if(stage===6) {
      records.push({name:'scan0003.pdf',kind:'file',size:382014,date:'Today, 09:42',revision:1,line:'Rental deposit receipt, €2,100, paid March 2024'});
      offerVisible=true;render();advance(7);
    } else if (!busy && enabled && records.some(f=>!upToDate(f)) && ignoredDay!==today()) {offerVisible=true;drawPill();guide();}
  }
  function preview(file) {
    if(!file)return;
    const popup=shell.dialog({title:file.name,message:'',actions:[{label:'Close',action:()=>{drawPill();guide();}}]});
    const documentPreview=document.createElement('div');documentPreview.className='preview-document';
    const title=document.createElement('strong');title.textContent=file.line.split(',')[0];
    const text=document.createElement('span');text.textContent=file.line;documentPreview.append(title,text);
    popup.querySelector('.dio-dialog p').replaceWith(documentPreview);
  }
  function reset() {
    timers.forEach(clearTimeout);clearTimeout(offerTimer);timers=[];busy=false;query='';cache={};enabled=true;ignoredDay='';stage=0;mode='details';offerVisible=true;
    records=originals.map(f=>({...f}));save();shell.closePopup();content.querySelector('.dio-toast')?.remove();content.classList.remove('has-toast');
    shell.setFolder(folder,records);shell.setView(mode);render();scheduleDismiss();
  }
  function scheduleDismiss() {
    clearTimeout(offerTimer);
    // The guided demo gives the reader time to read before this quiet offer expires.
    offerTimer=setTimeout(()=>{if(stage===0 && !busy && offerVisible)dismissOffer();},45000);
  }
  host.addEventListener('click',e=>{
    if(e.target.closest('[data-action="search"]')) {e.stopImmediatePropagation();searchOpen();}
    else if(e.target.closest('[data-action="view"]')) {e.stopImmediatePropagation();showOptions();}
    else if(e.target.closest('.dio-crumb:last-child')?.textContent==='Papers' || e.target.closest('[data-folder="Papers"]')) {e.stopImmediatePropagation();reopen();}
  },true);
  host.addEventListener('keydown',e=>{if(e.key==='Escape' && query){query='';shell.setFolder(folder,records);render();if(stage===3)advance(4);}},true);
  host.addEventListener('dio:select',()=>{if(!busy){drawPill();guide();}});
  new MutationObserver(()=>{decorate();}).observe(fileArea,{childList:true});
  shell.setView(mode);render();scheduleDismiss();
})();
