const video = document.querySelector('#video');
const status = document.getElementById('status');
const statusText = document.getElementById('status-text');
const playerContainer = document.getElementById('player-container');
const urlParams = new URLSearchParams(window.location.search);
const file = urlParams.get('file');
const manifestUrl = '/media/{{FILE_PARAM}}/manifest.mpd';

let player = null;
let lastSaveTime = 0;

function checkManifest() {
  fetch(manifestUrl, {method: 'HEAD'})
    .then(r => {
      if(r.status === 200) {
        startPlayer();
      } else if(r.status === 202) {
        statusText.textContent = 'Transmuxing video...';
        setTimeout(checkManifest, 3000);
      } else {
        status.className = 'error';
        statusText.textContent = 'Error: Manifest not found';
      }
    })
    .catch(() => {
      status.className = 'error';
      statusText.textContent = 'Error: Failed to check manifest';
    });
}

function startPlayer() {
  status.style.display = 'none';
  playerContainer.className = 'ready';

  fetch('/media/{{FILE_PARAM}}/subtitles.txt')
    .then(r => r.ok ? r.text() : '')
    .then(text => {
      text.split('\n').forEach(line => {
        line = line.trim();
        if(!line) return;

        const parts = line.split('\t');
        if(parts.length >= 3) {
          const track = document.createElement('track');
          track.kind = 'subtitles';
          track.src = parts[2];
          track.srclang = parts[0];
          track.label = parts[1] !== '-' ? parts[0].toUpperCase() + ' - ' + parts[1] : parts[0].toUpperCase();
          video.appendChild(track);
        }
      });
    })
    .catch(() => {});

  player = dashjs.MediaPlayer().create();

  player.on(dashjs.MediaPlayer.events.ERROR, e => {
    if(e.error.code === 10) {
      status.style.display = 'block';
      status.className = 'transmuxing';
      statusText.textContent = 'Manifest incomplete, retrying...';
      playerContainer.className = '';
      setTimeout(checkManifest, 3000);
    }
  });

  player.on(dashjs.MediaPlayer.events.STREAM_INITIALIZED, () => {
    const requestedLang = urlParams.get('lang') || 'en';
    const audioTracks = player.getTracksFor('audio');
    const matchingTrack = audioTracks.find(t => t.lang === requestedLang);
    if(matchingTrack) player.setCurrentTrack(matchingTrack);

    if(file) {
      fetch(`/api/progress?file=${encodeURIComponent(file)}`)
        .then(r => r.text())
        .then(text => {
          const parts = text.split(' ');
          const position = parseFloat(parts[0]);
          const savedSubtitle = parts[1] !== '-' ? parts[1] : '';
          const savedAudio = parts[2] !== '-' ? parts[2] : '';

          if(position > 5) {
            if(video.readyState >= 1) {
              video.currentTime = position;
            } else {
              video.addEventListener('loadedmetadata', () => {
                video.currentTime = position;
              }, {once: true});
            }
          }

          if(savedAudio) {
            const audioTrack = audioTracks.find(t => t.lang === savedAudio);
            if(audioTrack) player.setCurrentTrack(audioTrack);
          }

          if(savedSubtitle) {
            for(let track of video.textTracks) {
              track.mode = track.language === savedSubtitle ? 'showing' : 'hidden';
            }
          }
        })
        .catch(() => {});
    }
  });

  video.addEventListener('timeupdate', () => {
    const now = Date.now();
    if(file && now - lastSaveTime > 5000 && video.currentTime > 0) {
      lastSaveTime = now;
      let url = `/api/progress?file=${encodeURIComponent(file)}&position=${video.currentTime}`;

      const currentAudio = player.getCurrentTrackFor('audio');
      if(currentAudio?.lang) {
        url += `&audio=${encodeURIComponent(currentAudio.lang)}`;
      }

      for(let track of video.textTracks) {
        if(track.mode === 'showing') {
          url += `&subtitle=${encodeURIComponent(track.language)}`;
          break;
        }
      }

      fetch(url).catch(() => {});
    }
  });

  player.updateSettings({streaming: {initialSettings: {audio: {lang: urlParams.get('lang') || 'en'}}}});
  player.initialize(video, manifestUrl, true);

  video.addEventListener('contextmenu', e => {
    const tracks = player.getTracksFor('audio');
    if(tracks.length <= 1) return;

    e.preventDefault();
    const menu = document.createElement('div');
    menu.style.cssText = `position:fixed;left:${e.clientX}px;top:${e.clientY}px;background:#1a1a1a;border:1px solid #333;border-radius:4px;padding:4px 0;font:14px sans-serif;z-index:10000;min-width:150px;box-shadow:0 2px 8px rgba(0,0,0,0.5)`;

    const current = player.getCurrentTrackFor('audio');
    tracks.forEach(t => {
      const item = document.createElement('div');
      const label = t.labels?.[0]?.text || t.lang.toUpperCase();
      const isCurrent = current && t.index === current.index;
      item.textContent = (isCurrent ? '✓ ' : '  ') + label;
      item.style.cssText = 'padding:8px 16px;cursor:pointer;color:#ededed';
      item.onmouseover = () => item.style.background = '#2a2a2a';
      item.onmouseout = () => item.style.background = 'transparent';
      item.onclick = () => {
        player.setCurrentTrack(t);
        menu.remove();
      };
      menu.appendChild(item);
    });

    document.body.appendChild(menu);
    setTimeout(() => {
      const close = () => {
        menu.remove();
        document.removeEventListener('click', close);
      };
      document.addEventListener('click', close);
    }, 0);
  });
}

checkManifest();
