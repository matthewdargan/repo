import ASS from "/js/ass.min.js";
import * as dashjs from "/js/dash.all.js";

export class Player {
    constructor(video_container, stream_url, subtitles) {
        create_player(video_container, stream_url, subtitles);
    }
}

function create_player(base_node, stream_url, subtitles) {
    const video = elem_class("video", "video");
    const ass = document.createElement("div");
    ass.setAttribute("style", "position: absolute; top: 0; left: 0;");
    const player_state = create_player_state();
    const controls = create_controls();
    base_node.append(video, ass, player_state, controls);
    const player = dashjs.MediaPlayer().create();
    player.initialize(video, stream_url, true);
    if (subtitles != null)
        new ASS(subtitles, video, { container: ass, });
    init_player(base_node, video);
}

function create_player_state() {
    const base = elem_class("div", "player-state");
    const inner = elem_class("span", "main-state state-btn");
    const icon = document.createElement("ion-icon");
    icon.setAttribute("name", "play-outline");
    inner.appendChild(icon);
    base.appendChild(inner);
    return base;
}

function create_controls() {
    const base = elem_class("div", "controls");
    const duration = elem_class("div", "duration");
    const current_time = elem_class("div", "current-time");
    const hover_time = elem_class("div", "hover-time");
    const hover_duration = elem_class("span", "hover-duration");
    const buffer = elem_class("div", "buffer");
    hover_time.appendChild(hover_duration);
    duration.append(current_time, hover_time, buffer);
    base.appendChild(duration);
    const left_controls = elem_class("div", "btn-con");
    const play_control = elem_class("span", "play-pause control-btn");
    play_control.append(make_icon("play-outline"));
    const volume_control = create_volume_control();
    const time = create_time();
    left_controls.append(play_control, volume_control, time);
    const right_controls = elem_class("div", "right-controls");
    const speed_control = create_speed_control();
    const fullscreen_control = create_fullscreen_control();
    right_controls.append(speed_control, fullscreen_control);
    const buttons = elem_class("div", "btn-controls");
    buttons.append(left_controls, right_controls);
    base.append(duration, buttons);
    return base;
}

function create_volume_control() {
    const base = elem_class("span", "volume");
    const mute = elem_class("span", "mute-unmute control-btn");
    const mute_icon = make_icon("volume-high-outline");
    mute.append(mute_icon);
    const max_vol = elem_class("div", "max-vol");
    const current_vol = elem_class("div", "current-vol");
    max_vol.append(current_vol);
    base.append(mute, max_vol);
    return base;
}

function create_time() {
    const base = elem_class("span", "time-container");
    const current_duration = elem_class("span", "current-duration");
    current_duration.append(document.createTextNode("00:00"));
    const total_duration = elem_class("span", "total-duration");
    total_duration.append(document.createTextNode("00:00"));
    const divider = document.createElement("span");
    divider.append(document.createTextNode(" / "));
    base.append(current_duration, divider, total_duration);
    return base;
}

function create_speed_control() {
    const base = elem_class("span", "settings control-btn");
    const button = elem_class("span", "setting-btn");
    const icon = make_icon("options-outline");
    button.appendChild(icon);
    const menu = elem_class("ul", "setting-menu");
    [0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]
        .map((val) => {
            const elem = document.createElement("li");
            elem.setAttribute("data-value", val);
            elem.append(document.createTextNode(`${val}x`));
            if (val == 1)
                elem.setAttribute("class", "speed-active");
            return elem;
        })
        .forEach((opt) => menu.append(opt));
    base.append(button, menu);
    return base;
}

function create_fullscreen_control() {
    const fullscreen = elem_class("span", "fullscreen-btn control-btn");
    fullscreen.setAttribute("title", "fullscreen");
    const full = elem_class("span", "full");
    const full_icon = make_icon("scan-outline");
    const contract = elem_class("span", "contract");
    const contract_icon = make_icon("contract-outline");
    full.appendChild(full_icon);
    contract.appendChild(contract_icon);
    fullscreen.appendChild(full);
    fullscreen.appendChild(contract);
    return fullscreen;
}

function elem_class(ty, cls) {
    const elem = document.createElement(ty);
    elem.setAttribute("class", cls);
    return elem;
}

function make_icon(name) {
    const icon = document.createElement("ion-icon");
    icon.setAttribute("name", name);
    return icon;
}

export function init_player(video_container, video) {
    const fullscreen = document.querySelector(".fullscreen-btn");
    const play_pause = document.querySelector(".play-pause");
    const volume = document.querySelector(".volume");
    const current_time = document.querySelector(".current-time");
    const duration = document.querySelector(".duration");
    const buffer = document.querySelector(".buffer");
    const total_duration = document.querySelector(".total-duration");
    const current_duration = document.querySelector(".current-duration");
    const controls = document.querySelector(".controls");
    const current_vol = document.querySelector(".current-vol");
    const total_vol = document.querySelector(".max-vol");
    const main_state = document.querySelector(".main-state");
    const mute_unmute = document.querySelector(".mute-unmute");
    const hover_time = document.querySelector(".hover-time");
    const hover_duration = document.querySelector(".hover-duration");
    const settings_btn = document.querySelector(".setting-btn");
    const setting_menu = document.querySelector(".setting-menu");
    const speed_buttons = document.querySelectorAll(".setting-menu li");
    video.disablePictureInPicture = true;
    let is_cursor_on_controls = false,
        is_playing = false,
        mousedown_progress = false,
        mousedown_vol = false,
        mouseover_duration = false,
        muted = false,
        timeout,
        touch_client_x = 0,
        touch_past_duration_width = 0,
        touch_start_time = 0,
        volume_val = 1;
    current_vol.style.width = volume_val * 100 + "%";
    video.addEventListener("loadmetadata", can_play_init);
    video.addEventListener("play", play);
    video.addEventListener("pause", pause);
    video.addEventListener("progress", handle_progress);
    video_container.addEventListener("click", toggle_main_state);
    fullscreen.addEventListener("click", toggle_fullscreen);
    video_container.addEventListener("fullscreenchange", () =>
        video_container.classList.toggle("fullscreen", document.fullscreenElement),
    );
    play_pause.addEventListener("click", (e) => {
        if (is_playing)
            pause();
        else
            play();
    });
    duration.addEventListener("click", navigate);
    duration.addEventListener("mousedown", (e) => {
        mousedown_progress = true;
        navigate(e);
    });
    total_vol.addEventListener("mousedown", (e) => {
        mousedown_vol = true;
        handle_volume(e);
    });
    document.addEventListener("mouseup", (_) => {
        mousedown_progress = false;
        mousedown_vol = false;
    });
    document.addEventListener("mousemove", handle_mousemove);
    duration.addEventListener("mouseenter", (_) => {
        mouseover_duration = true;
    });
    duration.addEventListener("mouseleave", (_) => {
        mouseover_duration = false;
        hover_time.style.width = 0;
        hover_duration.innerHTML = "";
    });
    video_container.addEventListener("mouseleave", hide_controls);
    video_container.addEventListener("mousemove", (_) => {
        controls.classList.add("show-controls");
        hide_controls();
    });
    video_container.addEventListener("touchstart", (e) => {
        controls.classList.add("show-controls");
        touch_client_x = e.changedTouches[0].clientX;
        const current_time_rect = current_time.getBoundingClientRect();
        touch_past_duration_width = current_time_rect.width;
        touch_start_time = e.timeStamp;
    });
    video_container.addEventListener("touchend", () => {
        hide_controls();
        touch_client_x = 0;
        touch_past_duration_width = 0;
        touch_start_time = 0;
    });
    video_container.addEventListener("touchmove", touch_navigate);

    function touch_navigate(e) {
        hide_controls();
        if (e.timeStamp - touch_start_time > 500) {
            const duration_rect = duration.getBoundingClientRect();
            const client_x = e.changedTouches[0].clientX;
            const value = Math.min(
                Math.max(
                    0,
                    touch_past_duration_width + (client_x - touch_client_x) * 0.2,
                ),
                duration_rect.width,
            );
            current_time.style.width = value + "px";
            video.currentTime = (value / duration_rect.width) * video.duration;
            current_duration.innerHTML = show_duration(video.currentTime);
        }
    }

    controls.addEventListener("mouseenter", (_) => {
        controls.classList.add("show-controls");
        is_cursor_on_controls = true;
    });
    controls.addEventListener("mouseleave", (_) => {
        is_cursor_on_controls = false;
    });
    main_state.addEventListener("click", toggle_main_state);
    main_state.addEventListener("animationend", handle_main_state_animation_end);
    mute_unmute.addEventListener("click", toggle_mute_unmute);
    mute_unmute.addEventListener("mouseenter", (_) => {
        if (muted) {
            total_vol.classList.remove("show");
        } else {
            total_vol.classList.add("show");
        }
    });
    mute_unmute.addEventListener("mouseleave", (e) => {
        if (e.relatedTarget != volume) {
            total_vol.classList.remove("show");
        }
    });
    settings_btn.addEventListener("click", handle_setting_menu);
    speed_buttons.forEach((btn) => {
        btn.addEventListener("click", handle_playback_rate);
    });
    document.addEventListener("keydown", (e) => {
        const tagName = document.activeElement.tagName.toLowerCase();
        if (tagName === "input") return;
        if (e.key.match(/[0-9]/gi)) {
            video.currentTime = (video.duration / 100) * (parseInt(e.key) * 10);
            current_time.style.width = parseInt(e.key) * 10 + "%";
        }
        switch (e.key.toLowerCase()) {
            case " ":
                if (tagName === "button") return;
                if (is_playing)
                    video.pause();
                else
                    video.play();
                break;
            case "f":
                toggle_fullscreen();
                break;
            case "m":
                toggle_mute_unmute();
                break;
            case "+":
                handle_playback_rate_key("increase");
                break;
            case "-":
                handle_playback_rate_key();
                break;
            default:
                break;
        }
    });

    function can_play_init() {
        total_duration.innerHTML = show_duration(video.duration);
        video.volume = volume_val;
        muted = video.muted;
        if (video.paused) {
            controls.classList.add("show-controls");
            main_state.classList.add("show-state");
            handle_main_state_icon(`<ion-icon name="play-outline"></ion-icon>`);
        }
    }

    function play() {
        video.play();
        is_playing = true;
        play_pause.innerHTML = `<ion-icon name="pause-outline"></ion-icon>`;
        main_state.classList.remove("show-state");
        handle_main_state_icon(`<ion-icon name="pause-outline"></ion-icon>`);
    }

    video.ontimeupdate = handle_progress_bar;

    function handle_progress_bar() {
        current_time.style.width = (video.currentTime / video.duration) * 100 + "%";
        current_duration.innerHTML = show_duration(video.currentTime);
        total_duration.innerHTML = show_duration(video.duration);
    }

    function pause() {
        video.pause();
        is_playing = false;
        play_pause.innerHTML = `<ion-icon name="play-outline"></ion-icon>`;
        controls.classList.add("show-controls");
        main_state.classList.add("show-state");
        handle_main_state_icon(`<ion-icon name="play-outline"></ion-icon>`);
        if (video.ended) {
            current_time.style.width = 100 + "%";
        }
    }

    function navigate(e) {
        const total_duration_rect = duration.getBoundingClientRect();
        const width = Math.min(
            Math.max(0, e.clientX - total_duration_rect.x),
            total_duration_rect.width,
        );
        current_time.style.width = (width / total_duration_rect.width) * 100 + "%";
        video.currentTime = (width / total_duration_rect.width) * video.duration;
    }

    function show_duration(time) {
        const hours = Math.floor(time / 60 ** 2);
        const min = Math.floor((time / 60) % 60);
        const sec = Math.floor(time % 60);
        if (hours > 0) {
            return `${formatter(hours)}:${formatter(min)}:${formatter(sec)}`;
        } else {
            return `${formatter(min)}:${formatter(sec)}`;
        }
    }

    function formatter(number) {
        return new Intl.NumberFormat({}, { minimumIntegerDigits: 2 }).format(
            number,
        );
    }

    function toggle_mute_unmute() {
        if (!muted) {
            video.volume = 0;
            muted = true;
            mute_unmute.innerHTML = `<ion-icon name="volume-mute-outline"></ion-icon>`;
            handle_main_state_icon(
                `<ion-icon name="volume-mute-outline"></ion-icon>`,
            );
            total_vol.classList.remove("show");
        } else {
            video.volume = volume_val;
            muted = false;
            total_vol.classList.add("show");
            handle_main_state_icon(
                `<ion-icon name="volume-high-outline"></ion-icon>`,
            );
            mute_unmute.innerHTML = `<ion-icon name="volume-high-outline"></ion-icon>`;
        }
    }

    function hide_controls() {
        if (timeout)
            clearTimeout(timeout);
        timeout = setTimeout(() => {
            if (is_playing && !is_cursor_on_controls) {
                controls.classList.remove("show-controls");
                setting_menu.classList.remove("show-setting-menu");
            }
        }, 500);
    }

    function toggle_main_state(e) {
        e.stopPropagation();
        if (!e.composedPath().includes(controls)) {
            if (!is_playing)
                play();
            else
                pause();
        }
    }

    function handle_volume(e) {
        const total_vol_rect = total_vol.getBoundingClientRect();
        current_vol.style.width =
            Math.min(
                Math.max(0, e.clientX - total_vol_rect.x),
                total_vol_rect.width,
            ) + "px";
        volume_val = Math.min(
            Math.max(0, (e.clientX - total_vol_rect.x) / total_vol_rect.width),
            1,
        );
        video.volume = volume_val;
    }

    function handle_progress() {
        if (!video.buffered || !video.buffered.length) {
            return;
        }
        const width = (video.buffered.end(0) / video.duration) * 100 + "%";
        buffer.style.width = width;
    }

    function toggle_fullscreen() {
        if (!document.fullscreenElement) {
            video_container.requestFullscreen();
            handle_main_state_icon(`<ion-icon name="scan-outline"></ion-icon>`);
        } else {
            handle_main_state_icon(` <ion-icon name="contract-outline"></ion-icon>`);
            document.exitFullscreen();
        }
    }

    function handle_mousemove(e) {
        if (mousedown_progress) {
            e.preventDefault();
            navigate(e);
        }
        if (mousedown_vol)
            handle_volume(e);
        if (mouseover_duration) {
            const rect = duration.getBoundingClientRect();
            const width = Math.min(Math.max(0, e.clientX - rect.x), rect.width);
            const percent = (width / rect.width) * 100;
            hover_time.style.width = width + "px";
            hover_duration.innerHTML = show_duration(
                (video.duration / 100) * percent,
            );
        }
    }

    function handle_main_state_icon(icon) {
        main_state.classList.add("animate-state");
        main_state.innerHTML = icon;
    }

    function handle_main_state_animation_end() {
        main_state.classList.remove("animate-state");
        if (!is_playing)
            main_state.innerHTML = `<ion-icon name="play-outline"></ion-icon>`;
        if (document.pictureInPictureElement)
            main_state.innerHTML = ` <ion-icon name="tv-outline"></ion-icon>`;
    }

    function handle_setting_menu() {
        setting_menu.classList.toggle("show-setting-menu");
    }

    function handle_playback_rate(e) {
        video.playbackRate = parseFloat(e.target.dataset.value);
        speed_buttons.forEach((btn) => {
            btn.classList.remove("speed-active");
        });
        e.target.classList.add("speed-active");
        setting_menu.classList.remove("show-setting-menu");
    }

    function handle_playback_rate_key(type = "") {
        if (type === "increase" && video.playbackRate < 2) {
            video.playbackRate += 0.25;
        } else if (video.playbackRate > 0.25 && type !== "increase") {
            video.playbackRate -= 0.25;
        }
        handle_main_state_icon(
            `<span style="font-size: 1.4rem">${video.playbackRate}x</span>`,
        );
        speed_buttons.forEach((btn) => {
            btn.classList.remove("speed-active");
            if (btn.dataset.value == video.playbackRate) {
                btn.classList.add("speed-active");
            }
        });
    }
}
