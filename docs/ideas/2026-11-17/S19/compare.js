/* Local sample fixtures; nothing reads or changes files on disk. */
(() => {
  'use strict';
  const $ = s => document.querySelector(s);
  const icon = DioShell.icon;
  function texture(version) {
    const c=document.createElement('canvas'); c.width=960; c.height=600;
    const x=c.getContext('2d');
    x.fillStyle='#182a30'; x.fillRect(0,0,960,600);
    const panels=[[24,24,420,256],[460,24,476,256],[24,296,280,280],[320,296,296,280],[632,296,304,280]];
    panels.forEach(([a,b,w,h],i)=>{
      x.fillStyle=i%2?'#37545b':'#405e64'; x.fillRect(a,b,w,h);
      x.strokeStyle='#81918d'; x.lineWidth=3; x.strokeRect(a+8,b+8,w-16,h-16);
      x.strokeStyle='#253d45'; x.lineWidth=12; x.beginPath();x.moveTo(a+16,b+h-20);x.lineTo(a+w-20,b+h-20);x.stroke();
      for(const [u,v] of [[a+20,b+20],[a+w-20,b+20],[a+20,b+h-20],[a+w-20,b+h-20]]){
        x.fillStyle='#172b32';x.beginPath();x.arc(u,v,7,0,Math.PI*2);x.fill();
        x.strokeStyle='#a8b4ae';x.lineWidth=2;x.beginPath();x.moveTo(u-3,v);x.lineTo(u+3,v);x.stroke();
      }
    });
    x.fillStyle=version===1?'#b8b9a1':'#d7a65b';
    x.fillRect(52,66,364,36);x.fillRect(52,116,364,12);
    x.fillRect(700,324,36,224);x.fillRect(752,324,12,224);
    x.font='600 64px monospace';x.fillText('07',500,132);
    x.font='18px monospace';x.fillStyle='#9eada7';x.fillText('FIELD / UNIT',504,184);
    x.fillStyle='#14272e';for(let i=0;i<8;i++)x.fillRect(492+i*50,218,28,22);
    x.strokeStyle='#7f918c';x.lineWidth=3;x.beginPath();x.moveTo(78,356);x.lineTo(240,356);x.lineTo(240,506);x.lineTo(78,506);x.closePath();x.stroke();
    x.fillStyle='#9daaa0';x.font='14px monospace';x.fillText('HERO // A',92,390);
    x.fillText('ALBEDO',92,414);
    if(version>1){
      x.fillStyle='#182f35';x.fillRect(354,330,228,208);x.strokeStyle='#9ca899';x.lineWidth=3;x.strokeRect(362,338,212,192);
      x.fillStyle='#d7a65b';x.fillRect(376,354,40,160);x.fillRect(432,354,16,160);
      x.fillStyle='#a8b3a6';x.font='16px monospace';x.fillText('MK.08',466,492);
    }
    if(version===3){x.fillStyle='#d7a65b';x.fillRect(806,324,20,224);}
    let seed=37; const random=()=>{seed=(seed*1664525+1013904223)>>>0;return seed/4294967296;};
    for(let i=0;i<18000;i++){
      const a=random()*960,b=random()*600;
      x.strokeStyle=random()>.45?'#d1c7a51b':'#0b171b35';x.lineWidth=random()*1.5+.5;
      x.beginPath();x.moveTo(a,b);x.lineTo(a+random()*10,b-random()*3);x.stroke();
    }
    return c.toDataURL('image/png');
  }
  const art=[texture(1),texture(2),texture(3)];
  const files=[
    {name:'hero_albedo_v07.png',kind:'file',size:6291456,date:'16 Nov, 14:32',thumbnail:art[0],image:art[0],dimensions:'2048 × 2048'},
    {name:'hero_albedo_v08.png',kind:'file',size:7235174,date:'16 Nov, 15:18',thumbnail:art[1],image:art[1],dimensions:'2048 × 2048'},
    {name:'hero_final_FINAL.psd',kind:'file',size:96468992,date:'16 Nov, 16:04',thumbnail:art[2],image:art[2],dimensions:'2048 × 2048'},
    {name:'rock_01.fbx',kind:'file',size:14994636,date:'15 Nov, 10:08'},
    {name:'پاسپورت اسکن.jpg',kind:'file',size:2516582,date:'12 Nov, 11:24',thumbnail:art[0],image:art[0],dimensions:'2048 × 2048'}
  ];
  const shell=DioShell.create('#shell',{path:'Home\\Pictures\\Textures',files,addedFolders:['Textures'],drives:[{name:'Local Disk (C:)',used:86},{name:'Projects (E:)',used:42}]});
  let phase=1, overlay=null, pair=[], split=50, held=false, blink=false;
  const hints={1:'دو تکسچر انتخاب شدن؛ روی Compare کلیک کن.',2:'خط وسط رو به چپ و راست بکش تا تغییرهای دو نسخه رو ببینی.',3:'Space رو نگه دار و رها کن، یا روی Blink کلیک کن.',4:'حالا Esc رو بزن؛ همون دو فایل انتخاب می‌مونن.',5:'تغییرها رو دیدی و انتخابت سر جاشه. می‌تونی PSD و PNG رو هم امتحان کنی.'};
  function hint(n){phase=n;shell.setHint(hints[n],n,5);$('.dio-try-strip').dataset.phase=n;$('.restart').hidden=n!==5;}
  const restart=document.createElement('button');restart.className='restart';restart.id='restart';restart.textContent='از اول';restart.hidden=true;$('.dio-try-strip').insertBefore(restart,$('.dio-step'));
  function eligible(){return shell.selected.length===2&&shell.selected.every(f=>f.image&&/\.(png|jpe?g|webp|gif|bmp|psd)$/i.test(f.name));}
  function setSplit(value){split=Math.max(0,Math.min(100,value));const stage=$('.compare-stage');stage.style.setProperty('--split',split+'%');$('#divider').setAttribute('aria-valuenow',Math.round(split));$('.split-value').textContent=Math.round(split)+' / '+Math.round(100-split);}
  function showMode(){
    const stage=$('.compare-stage');stage.dataset.mode=held||blink?'blink':'split';stage.dataset.side=held||blink?'b':'a';
    $('#blink').setAttribute('aria-pressed',String(blink));$('#split-mode').setAttribute('aria-pressed',String(!blink));
    $('.space-hint').innerHTML=`<kbd>Space</kbd> ${held?'Release to return':blink?'B · latest version':'Hold to blink'}`;
  }
  function openCompare(){
    if(!eligible())return;
    shell.closePopup();pair=shell.selected.map(f=>f.name);
    const [a,b]=shell.selected; split=50;held=false;blink=false;
    overlay=document.createElement('section');overlay.className='compare-overlay dio-appear';overlay.id='compare';overlay.setAttribute('role','dialog');overlay.setAttribute('aria-modal','true');overlay.setAttribute('aria-label','Compare images');
    overlay.innerHTML=`<div class="compare-top"><h1 class="compare-title">${icon('image',20)} Compare</h1><button id="close-compare" class="compare-close" aria-label="Close comparison"><kbd>Esc</kbd>${icon('close',16)}</button></div><div class="compare-files"></div><div class="compare-stage" data-mode="split"><img class="after" alt=""><img class="before" alt=""><span class="canvas-label label-a">A · earlier</span><span class="canvas-label label-b">B · latest</span><button id="divider" class="divider" role="slider" aria-label="Comparison divider" aria-valuemin="0" aria-valuemax="100" aria-valuenow="50"><span class="divider-knob">${icon('back',16)}${icon('forward',16)}</span></button><span id="reveal-target" class="drag-target"></span></div><div class="compare-bottom"><div class="compare-modes"><button id="split-mode" class="compare-mode" aria-pressed="true">${icon('grid',16)} Split</button><button id="blink" class="compare-mode" aria-pressed="false">${icon('image',16)} Blink</button></div><span class="space-hint"><kbd>Space</kbd> Hold to blink</span><span class="split-value">50 / 50</span></div>`;
    const sizeDelta=(b.size-a.size)/1048576, minutes=(Date.parse('2026-11-'+(b.date.startsWith('16')?'16':'12')+'T'+b.date.slice(-5)+':00')-Date.parse('2026-11-'+(a.date.startsWith('16')?'16':'12')+'T'+a.date.slice(-5)+':00'))/60000;
    [a,b].forEach((f,i)=>{
      const item=document.createElement('div');item.className='compare-file';
      const heading=document.createElement('div');heading.className='file-heading';heading.innerHTML=`<span class="version-letter">${i?'B':'A'}</span>`;
      const name=document.createElement('span');name.textContent=f.name;name.dir='auto';if(/[\u0600-\u06ff]/.test(f.name))name.className='dio-name-fa';heading.append(name);item.append(heading);
      const meta=document.createElement('div');meta.className='file-meta';
      const delta=i?`diff · ${sizeDelta>=0?'+':''}${sizeDelta.toFixed(1)} MB · ${minutes>=0?'+':''}${Math.abs(minutes)>1440?(minutes/1440).toFixed(0)+' days':minutes+' min'}`:'diff · reference';
      const sizes=document.createElement('span');sizes.textContent=DioShell.formatSize(f.size)+' · '+f.date;const badge=document.createElement('span');badge.className='diff-badge';badge.textContent=delta;meta.append(sizes,badge);item.append(meta);
      if(f.name.endsWith('.psd')){const cached=document.createElement('span');cached.className='preview-note';cached.id='cached-preview';cached.textContent='Cached PSD preview · '+f.dimensions;item.append(cached);}
      overlay.querySelector('.compare-files').append(item);
    });
    overlay.querySelector('.before').src=a.image;overlay.querySelector('.before').alt=a.name;
    overlay.querySelector('.after').src=b.image;overlay.querySelector('.after').alt=b.name;
    $('.dio-content').append(overlay);$('.dio-files').inert=true;
    shell.element.querySelectorAll(':scope > :not(.dio-content)').forEach(el=>el.inert=true);
    overlay.addEventListener('keydown',e=>e.stopPropagation());
    setSplit(50);hint(2);
    $('#close-compare').onclick=closeCompare;
    $('#blink').onclick=()=>{blink=!blink;showMode();hint(4);};
    $('#split-mode').onclick=()=>{blink=false;showMode();};
    const divider=$('#divider');
    const move=e=>{const r=$('.compare-stage').getBoundingClientRect();blink=false;showMode();setSplit((e.clientX-r.left)/r.width*100);};
    divider.onpointerdown=e=>{e.preventDefault();divider.setPointerCapture(e.pointerId);divider.focus();move(e);};
    divider.onpointermove=e=>{if(divider.hasPointerCapture(e.pointerId))move(e);};
    divider.onpointerup=e=>{if(divider.hasPointerCapture(e.pointerId))divider.releasePointerCapture(e.pointerId);if(phase===2)hint(3);};
    divider.onkeydown=e=>{if(['ArrowLeft','ArrowRight','Home','End'].includes(e.key)){e.preventDefault();setSplit(e.key==='Home'?0:e.key==='End'?100:split+(e.key==='ArrowLeft'?-5:5));if(phase===2)hint(3);}};
    divider.focus();
  }
  function closeCompare(){if(!overlay)return;overlay.remove();overlay=null;$('.dio-files').inert=false;shell.element.querySelectorAll(':scope > :not(.dio-content)').forEach(el=>el.inert=false);shell.select(pair);$('.dio-files').focus({preventScroll:true});hint(5);held=false;blink=false;}
  function menu(x,y){
    const items=[];
    if(eligible())items.push({label:'Compare',icon:'image',action:openCompare},'separator');
    items.push({label:'Open',icon:'open',action:()=>shell.dialog({title:'Open image',message:'The sample keeps your original files untouched.'})},{label:'Properties',icon:'info',action:()=>shell.dialog({title:'Selection',message:shell.selected.map(f=>f.name+' · '+DioShell.formatSize(f.size)).join(' / ')})});
    const el=shell.openMenu(x,y,items);if(eligible()){const b=el.querySelector('button');b.id='compare-action';b.classList.add('compare-action');}
  }
  $('#shell').addEventListener('contextmenu',e=>{
    const row=e.target.closest('[data-name]');if(!row||overlay)return;e.preventDefault();e.stopImmediatePropagation();if(!shell.selected.some(f=>f.name===row.dataset.name))shell.select(row.dataset.name);menu(e.clientX,e.clientY);
  },true);
  document.addEventListener('keydown',e=>{
    if(overlay){
      if(e.key==='Escape'){e.preventDefault();e.stopImmediatePropagation();closeCompare();return;}
      if(e.code==='Space'){e.preventDefault();e.stopImmediatePropagation();held=true;showMode();return;}
      if(e.key==='Tab'){const controls=[...overlay.querySelectorAll('button')];const n=controls.indexOf(document.activeElement);e.preventDefault();controls[(n+(e.shiftKey?-1:1)+controls.length)%controls.length].focus();return;}
      if(!e.target.closest('.compare-overlay'))e.stopImmediatePropagation();
    } else if(e.shiftKey&&e.key==='F10'&&eligible()){e.preventDefault();e.stopImmediatePropagation();const r=$('.dio-files').getBoundingClientRect();menu(r.x+192,r.y+156);}
  },true);
  document.addEventListener('keyup',e=>{if(overlay&&e.code==='Space'){e.preventDefault();held=false;showMode();if(phase===3)hint(4);}},true);
  window.addEventListener('blur',()=>{if(overlay){held=false;showMode();}});
  function reset(){if(overlay)closeCompare();shell.closePopup();shell.setFolder('Home\\Pictures\\Textures',files);shell.select([files[0].name,files[1].name]);hint(1);const r=$('.dio-files').getBoundingClientRect();menu(r.left+276,r.top+144);}
  restart.onclick=reset;
  // shortcut: PSD decoding is represented by a fixture preview; native implementation uses Windows' cached thumbnail.
  window.BlinkCompare={shell,open:openCompare,close:closeCompare,reset,get phase(){return phase;},get split(){return split;}};
  reset();
})();
