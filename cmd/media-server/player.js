var statusEl = document.getElementById('status');
var statusText = document.getElementById('status-text');
var playerContainer = document.getElementById('player-container');
var video = document.querySelector('#video');
var player = null;
var manifestUrl = '/media/{{FILE_PARAM}}/manifest.mpd';
var checkInterval = null;

function checkManifest() {
  fetch(manifestUrl, {method: 'HEAD'})
    .then(function(response) {
      if(response.status === 200) {
        clearInterval(checkInterval);
        startPlayer();
      } else if(response.status === 202) {
        statusText.textContent = 'Transmuxing video... (this may take a few minutes)';
      } else {
        statusEl.className = 'error';
        statusText.textContent = 'Error: Manifest not found';
        clearInterval(checkInterval);
      }
    })
    .catch(function() {
      statusEl.className = 'error';
      statusText.textContent = 'Error: Failed to check manifest';
      clearInterval(checkInterval);
    });
}

function startPlayer() {
  statusEl.style.display = 'none';
  playerContainer.className = 'ready';

  fetch('/media/{{FILE_PARAM}}/subtitles.txt')
    .then(function(r) { return r.ok ? r.text() : ''; })
    .then(function(text) {
      var lines = text.split('\n');
      for(var i = 0; i < lines.length; i++) {
        var parts = lines[i].split(' ');
        if(parts.length === 2) {
          var track = document.createElement('track');
          track.kind = 'subtitles';
          track.src = parts[1];
          track.srclang = parts[0];
          track.label = parts[0].toUpperCase();
          video.appendChild(track);
        }
      }
    })
    .catch(function() {})
    .finally(function() { initPlayer(); });
}

function initPlayer() {
  player = dashjs.MediaPlayer().create();

  player.on(dashjs.MediaPlayer.events.ERROR, function(e) {
    if(e.error.code === 10) {
      statusEl.style.display = 'block';
      statusEl.className = 'transmuxing';
      statusText.textContent = 'Manifest incomplete, retrying...';
      playerContainer.className = '';
      setTimeout(checkManifest, 3000);
    }
  });

  player.on(dashjs.MediaPlayer.events.STREAM_INITIALIZED, function() {
    var urlParams = new URLSearchParams(window.location.search);
    var requestedLang = urlParams.get('lang') || 'en';
    var audioTracks = player.getTracksFor('audio');

    for(var i = 0; i < audioTracks.length; i++) {
      if(audioTracks[i].lang === requestedLang) {
        player.setCurrentTrack(audioTracks[i]);
        break;
      }
    }

    setupAudioTracksUI();
  });

  var urlParams = new URLSearchParams(window.location.search);
  var selectedLang = urlParams.get('lang') || 'en';
  player.updateSettings({streaming: {initialSettings: {audio: {lang: selectedLang}}}});
  player.initialize(video, manifestUrl, true);

  var file = urlParams.get('file');
  var savedSubtitle = '';
  var savedAudio = '';
  if(file) {
    fetch('/api/progress?file=' + encodeURIComponent(file))
      .then(function(r) { return r.text(); })
      .then(function(text) {
        var parts = text.split(' ');
        var position = parseFloat(parts[0]);
        savedSubtitle = parts[1] && parts[1] !== '-' ? parts[1] : '';
        savedAudio = parts[2] && parts[2] !== '-' ? parts[2] : '';
        if(position > 5) {
          video.addEventListener('loadedmetadata', function() {
            video.currentTime = position;
          }, {once: true});
        }
      })
      .catch(function() {});
  }

  player.on(dashjs.MediaPlayer.events.STREAM_INITIALIZED, function() {
    if(savedAudio) {
      var audioTracks = player.getTracksFor('audio');
      for(var i = 0; i < audioTracks.length; i++) {
        if(audioTracks[i].lang === savedAudio) {
          player.setCurrentTrack(audioTracks[i]);
          break;
        }
      }
    }
    if(savedSubtitle) {
      for(var i = 0; i < video.textTracks.length; i++) {
        if(video.textTracks[i].language === savedSubtitle) {
          video.textTracks[i].mode = 'showing';
        } else {
          video.textTracks[i].mode = 'hidden';
        }
      }
    }
  });

  var lastSaveTime = 0;
  video.addEventListener('timeupdate', function() {
    var now = Date.now();
    if(now - lastSaveTime > 5000) {
      lastSaveTime = now;
      if(file && video.currentTime > 0) {
        var url = '/api/progress?file=' + encodeURIComponent(file) + '&position=' + video.currentTime;
        var currentAudio = player.getCurrentTrackFor('audio');
        if(!currentAudio || !currentAudio.lang) {
          var audioTracks = player.getTracksFor('audio');
          if(audioTracks.length === 1) currentAudio = audioTracks[0];
        }
        if(currentAudio && currentAudio.lang) {
          url += '&audio=' + encodeURIComponent(currentAudio.lang);
        }
        for(var i = 0; i < video.textTracks.length; i++) {
          if(video.textTracks[i].mode === 'showing') {
            url += '&subtitle=' + encodeURIComponent(video.textTracks[i].language);
            break;
          }
        }
        fetch(url).catch(function() {});
      }
    }
  });
}

function setupAudioTracksUI() {
  var audioTracks = player.getTracksFor('audio');
  if(audioTracks.length <= 1) return;

  video.addEventListener('contextmenu', function(e) {
    if(audioTracks.length > 1) {
      e.preventDefault();
      showAudioMenu(e.clientX, e.clientY);
    }
  });
}

function showAudioMenu(x, y) {
  var existingMenu = document.getElementById('audio-track-menu');
  if(existingMenu) existingMenu.remove();

  var menu = document.createElement('div');
  menu.id = 'audio-track-menu';
  menu.style.cssText = 'position:fixed;left:'+x+'px;top:'+y+'px;background:#1a1a1a;border:1px solid #333;border-radius:4px;padding:4px 0;font-family:sans-serif;font-size:14px;z-index:10000;min-width:150px;box-shadow:0 2px 8px rgba(0,0,0,0.5)';

  var audioTracks = player.getTracksFor('audio');
  var currentTrack = player.getCurrentTrackFor('audio');

  audioTracks.forEach(function(track) {
    var item = document.createElement('div');
    var lang = (track.lang || 'unknown').toUpperCase();
    var label = track.labels && track.labels[0] ? track.labels[0].text : lang;
    var isCurrent = currentTrack && track.index === currentTrack.index;
    item.textContent = (isCurrent ? '✓ ' : '  ') + label;
    item.style.cssText = 'padding:8px 16px;cursor:pointer;color:#ededed';
    item.onmouseover = function() { this.style.background = '#2a2a2a'; };
    item.onmouseout = function() { this.style.background = 'transparent'; };
    item.onclick = function() {
      player.setCurrentTrack(track);
      menu.remove();
    };
    menu.appendChild(item);
  });

  document.body.appendChild(menu);

  var closeMenu = function() {
    menu.remove();
    document.removeEventListener('click', closeMenu);
  };
  setTimeout(function() { document.addEventListener('click', closeMenu); }, 0);
}

checkManifest();
checkInterval = setInterval(checkManifest, 3000);
