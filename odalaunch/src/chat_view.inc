// The global-chat scrollback page (HTML/CSS/JS), included as a raw string by
// chat_tab.cpp. Kept in its own file so the markup can grow without bloating the
// C++. It is compiled in (no runtime file dependency) and loaded via SetPage.
//
// Contract with chat_tab.cpp:
//   - @BG@ / @FG@ are replaced with the system window/text colours before load.
//   - setMessages(arr) is called with [{id,user,sub,text,ts,blocked,deleted}].
//   - All chat text is written with textContent / text nodes (never innerHTML),
//     so untrusted chat can't inject markup. Auto-detected http(s) URLs render
//     as non-navigating spans; clicking one (or right-clicking a username) posts
//     a tab-delimited command to the "oda" script message handler:
//         "open\t<url>"   -> host opens the URL in the system browser
//         "menu\t<sub>"   -> host shows the Friend/Block menu for that subject
//     We deliberately avoid real <a href> links: clicking one navigates the
//     webview away and unloads this page. Edge backend only; where window.oda is
//     absent (legacy IE fallback) links/menus are simply inert.
R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta http-equiv="X-UA-Compatible" content="IE=edge">
<style>
  html,body{margin:0;padding:0;height:100%;}
  body{font-family:'Segoe UI','Segoe UI Emoji','Segoe UI Symbol',sans-serif;font-size:13px;background:@BG@;color:@FG@;
       padding:4px 6px;box-sizing:border-box;overflow-y:auto;overflow-x:hidden;}
  .msg{margin:1px 0;line-height:1.35;word-wrap:break-word;overflow-wrap:anywhere;}
  .user{font-weight:bold;}
  .ts{opacity:0.55;}
  .sys{opacity:0.6;font-style:italic;}
  .blocked{color:#b06a6a;font-style:italic;cursor:pointer;}
  .hide{color:#b06a6a;font-style:italic;cursor:pointer;opacity:0.8;}
  .link{color:#4aa3df;text-decoration:underline;cursor:pointer;}
</style></head>
<body><div id="log"></div>
<script>
  var current = [];
  var revealed = {};
  // Append `text` to `parent`, turning http(s) URLs into links and everything
  // else into plain text nodes (so nothing in the message can inject markup).
  function appendLinkified(parent, text){
    var re=/(https?:\/\/[^\s]+)/g;
    var last=0, mm;
    while((mm=re.exec(text))!==null){
      if(mm.index>last) parent.appendChild(document.createTextNode(text.slice(last, mm.index)));
      var url=mm[0], trail='';
      // Don't swallow trailing sentence punctuation into the link.
      while(url.length>1 && /[.,!?:;)\]}'"]$/.test(url)){ trail=url.charAt(url.length-1)+trail; url=url.slice(0,-1); }
      // A real <a href> click makes the webview navigate away and unload this
      // page, so render a non-navigating span and let the host open the URL in
      // the system browser instead. (closure captures this iteration's url.)
      var a=document.createElement('span'); a.className='link'; a.textContent=url;
      (function(u){ a.onclick=function(){ if(window.oda){ window.oda.postMessage('open\t'+u); } return false; }; })(url);
      parent.appendChild(a);
      if(trail) parent.appendChild(document.createTextNode(trail));
      last=mm.index+mm[0].length;
    }
    if(last<text.length) parent.appendChild(document.createTextNode(text.slice(last)));
  }
  function buildMsg(m){
    var div = document.createElement('div');
    if(m.deleted){ div.className='msg sys'; div.textContent='[message removed by a moderator]'; return div; }
    div.className='msg';
    if(m.ts){ var s=document.createElement('span'); s.className='ts'; s.textContent='['+m.ts+'] '; div.appendChild(s); }
    if(m.blocked && !revealed[m.id]){
      div.className='msg blocked';
      var b=document.createElement('span'); b.textContent='[blocked user — click to show]'; div.appendChild(b);
      div.onclick=function(){ revealed[m.id]=true; rerender(true); };
      return div;
    }
    var u=document.createElement('span'); u.className='user'; u.textContent=m.user+': '; div.appendChild(u);
    if(m.sub){ u.oncontextmenu=function(e){ if(window.oda){ window.oda.postMessage('menu\t'+m.sub); } if(e&&e.preventDefault){ e.preventDefault(); } return false; }; }
    var t=document.createElement('span'); appendLinkified(t, m.text); div.appendChild(t);
    if(m.blocked){
      // Revealed blocked message: offer a link to collapse it again.
      var h=document.createElement('span'); h.className='hide'; h.textContent=' [hide]';
      h.onclick=function(){ revealed[m.id]=false; rerender(true); };
      div.appendChild(h);
    }
    return div;
  }
  function getScrollTop(){
    return window.pageYOffset || document.documentElement.scrollTop || document.body.scrollTop || 0;
  }
  function setScrollTop(y){
    // The scroll container differs between backends, so set every candidate;
    // the non-scrolling ones clamp harmlessly.
    window.scrollTo(0, y);
    document.documentElement.scrollTop = y;
    document.body.scrollTop = y;
  }
  // keepScroll: hold the current position (e.g. reveal/hide a blocked message)
  // instead of jumping to the bottom (the default, for newly arrived messages).
  function rerender(keepScroll){
    var log=document.getElementById('log');
    var prev=getScrollTop();
    log.innerHTML='';
    for(var i=0;i<current.length;i++) log.appendChild(buildMsg(current[i]));
    setScrollTop(keepScroll ? prev : document.body.scrollHeight);
  }
  function setMessages(arr){ current=arr; rerender(false); }
</script></body></html>
)HTML"
