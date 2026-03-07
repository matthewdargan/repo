#include <sys/wait.h>
#include <locale.h>
#include <ncurses.h>

#include "base/inc.h"
#include "base/inc.c"

////////////////////////////////
//~ Types

typedef enum PlayerMode PlayerMode;
enum PlayerMode
{
  PlayerMode_Home,
  PlayerMode_Browsing,
  PlayerMode_Playing,
};

#define IS_BROWSE_MODE(mode) ((mode) == PlayerMode_Home || (mode) == PlayerMode_Browsing)

typedef struct FileEntry FileEntry;
struct FileEntry
{
  String8 name;
  b32     is_directory;
  b32     is_watched;
  f64     last_watched_pos;
  f64     duration;
};

typedef struct WatchProgress WatchProgress;
struct WatchProgress
{
  WatchProgress *next;
  String8        file_path;
  f64            position_seconds;
  f64            duration_seconds;
  u64            last_watched_timestamp;
  b32            is_watched;
  u32            _pad0;
  String8        subtitle_lang;
  String8        audio_lang;
};

typedef struct WatchProgressTable WatchProgressTable;
struct WatchProgressTable
{
  Arena          *arena;
  WatchProgress **buckets;
  u64             bucket_count;
};

internal b32
is_watched(WatchProgress *wp)
{
  return wp->is_watched;
}

typedef struct MPVClient MPVClient;
struct MPVClient
{
  int     socket_fd;
  int     child_pid;
  Arena  *arena;
  u64     request_id;
  b32     running;
  String8 ipc_path;
};

typedef struct Terminal Terminal;
struct Terminal
{
  WINDOW *win;
  u32     height;
  u32     width;
};

typedef struct ContinueWatchingEntry ContinueWatchingEntry;
struct ContinueWatchingEntry
{
  String8 file_path;
  String8 display_name;
  f64     position_seconds;
  u64     last_watched_timestamp;
};

typedef struct PlayerState PlayerState;
struct PlayerState
{
  Arena     *perm_arena;

  PlayerMode mode;
  b32        quit;
  b32        needs_render;

  String8    current_dir;
  String8    current_file;
  FileEntry *files;
  u64        file_count;
  u64        selected_index;
  u64        scroll_offset;
  u64        current_root_index;

  ContinueWatchingEntry *continue_watching;
  u64                    continue_watching_count;

  MPVClient     *mpv;
  Terminal      *term;
  WatchProgress *current_watch_progress;

  u64 last_position_save;
  f64 last_known_position;
  f64 last_known_duration;
};

////////////////////////////////
//~ Globals

typedef struct MediaRoot MediaRoot;
struct MediaRoot
{
  String8 path;
  String8 name;
};

global MediaRoot          *g_roots          = 0;
global u64                 g_root_count     = 0;
global String8             g_state_dir_path = {0};
global WatchProgressTable *g_watch_progress = 0;

////////////////////////////////
//~ Watch Progress Table

internal u64
hash_string(String8 str)
{
  u64 hash = 5381;
  for(u64 i = 0; i < str.size; i += 1) { hash = ((hash << 5) + hash) + str.str[i]; }
  return hash;
}

internal WatchProgressTable *
watch_progress_table_alloc(Arena *arena, u64 bucket_count)
{
  WatchProgressTable *table = push_array(arena, WatchProgressTable, 1);
  table->arena              = arena;
  table->bucket_count       = bucket_count;
  table->buckets            = push_array(arena, WatchProgress *, bucket_count);
  return table;
}

internal WatchProgress *
watch_progress_find(WatchProgressTable *table, String8 file_path)
{
  u64 hash = hash_string(file_path);
  u64 idx  = hash % table->bucket_count;

  for(WatchProgress *wp = table->buckets[idx]; wp != 0; wp = wp->next)
  {
    if(str8_match(wp->file_path, file_path, 0)) { return wp; }
  }

  return 0;
}

internal WatchProgress *
watch_progress_update(WatchProgressTable *table, String8 file_path, f64 position, f64 duration)
{
  u64 hash = hash_string(file_path);
  u64 idx  = hash % table->bucket_count;

  WatchProgress *wp = 0;
  for(WatchProgress *it = table->buckets[idx]; it != 0; it = it->next)
  {
    if(str8_match(it->file_path, file_path, 0)) { wp = it; break; }
  }

  if(wp == 0)
  {
    wp                  = push_array(table->arena, WatchProgress, 1);
    wp->file_path       = str8_copy(table->arena, file_path);
    wp->next            = table->buckets[idx];
    table->buckets[idx] = wp;
  }

  wp->position_seconds       = position;
  wp->duration_seconds       = duration;
  wp->last_watched_timestamp = os_now_microseconds() / Million(1);

  if(duration > 0.0 && (position / duration) > 0.90) { wp->is_watched = 1; }

  return wp;
}

internal void
watch_progress_mark_watched(WatchProgressTable *table, String8 file_path)
{
  u64 hash = hash_string(file_path);
  u64 idx  = hash % table->bucket_count;

  WatchProgress *wp = 0;
  for(WatchProgress *it = table->buckets[idx]; it != 0; it = it->next)
  {
    if(str8_match(it->file_path, file_path, 0)) { wp = it; break; }
  }

  if(wp == 0)
  {
    wp                  = push_array(table->arena, WatchProgress, 1);
    wp->file_path       = str8_copy(table->arena, file_path);
    wp->next            = table->buckets[idx];
    table->buckets[idx] = wp;
  }

  wp->is_watched             = 1;
  wp->last_watched_timestamp = os_now_microseconds() / Million(1);
}

internal void
watch_progress_load(WatchProgressTable *table, String8 state_file_path)
{
  Temp scratch = scratch_begin(&table->arena, 1);

  OS_Handle file = os_file_open(OS_AccessFlag_Read, state_file_path);
  if(file.u64[0] == 0)
  {
    scratch_end(scratch);
    return;
  }

  OS_FileProperties props = os_properties_from_file(file);
  u8 *buffer              = push_array(scratch.arena, u8, props.size);
  os_file_read(file, rng_1u64(0, props.size), buffer);
  String8 contents        = str8(buffer, props.size);
  os_file_close(file);

  String8List lines = str8_split(scratch.arena, contents, (u8 *)"\n", 1, 0);
  for(String8Node *node = lines.first; node != 0; node = node->next)
  {
    String8 line = str8_skip_chop_whitespace(node->string);
    if(line.size == 0 || line.str[0] == '#') { continue; }

    String8List parts = str8_split(scratch.arena, line, (u8 *)"\t", 1, 0);
    if(parts.node_count != 7) { continue; }

    String8Node *n0 = parts.first;
    String8Node *n1 = n0->next;
    String8Node *n2 = n1->next;
    String8Node *n3 = n2->next;
    String8Node *n4 = n3->next;
    String8Node *n5 = n4->next;
    String8Node *n6 = n5->next;

    String8 file_path     = n0->string;
    f64     position      = f64_from_str8(n1->string);
    u64     timestamp     = u64_from_str8(n2->string, 10);
    String8 subtitle_lang = str8_match(n3->string, str8_lit("-"), 0) ? str8_zero() : n3->string;
    String8 audio_lang    = str8_match(n4->string, str8_lit("-"), 0) ? str8_zero() : n4->string;
    f64     duration      = f64_from_str8(n5->string);
    b32     watched       = (u64_from_str8(n6->string, 10) != 0);

    u64 hash = hash_string(file_path);
    u64 idx  = hash % table->bucket_count;

    WatchProgress *wp          = push_array(table->arena, WatchProgress, 1);
    wp->file_path              = str8_copy(table->arena, file_path);
    wp->position_seconds       = position;
    wp->last_watched_timestamp = timestamp;
    wp->subtitle_lang          = str8_copy(table->arena, subtitle_lang);
    wp->audio_lang             = str8_copy(table->arena, audio_lang);
    wp->duration_seconds       = duration;
    wp->is_watched             = watched;
    wp->next                   = table->buckets[idx];
    table->buckets[idx]        = wp;
  }

  scratch_end(scratch);
}


internal void
watch_progress_save(WatchProgressTable *table, String8 state_file_path)
{
  Temp        scratch = scratch_begin(&table->arena, 1);
  String8List lines   = {0};

  for(u64 i = 0; i < table->bucket_count; i += 1)
  {
    for(WatchProgress *wp = table->buckets[i]; wp != 0; wp = wp->next)
    {
      String8 sub  = wp->subtitle_lang.size > 0 ? wp->subtitle_lang : str8_lit("-");
      String8 aud  = wp->audio_lang.size > 0 ? wp->audio_lang : str8_lit("-");
      String8 line = str8f(scratch.arena, "%S\t%.2f\t%llu\t%S\t%S\t%.2f\t%d\n",
                           wp->file_path, wp->position_seconds, wp->last_watched_timestamp, sub, aud, wp->duration_seconds, wp->is_watched);
      str8_list_push(scratch.arena, &lines, line);
    }
  }

  String8 contents = str8_list_join(scratch.arena, lines, 0);

  OS_Handle file = os_file_open(OS_AccessFlag_Write | OS_AccessFlag_Truncate, state_file_path);
  if(file.u64[0] == 0)
  {
    log_errorf("media-player: failed to open state file for writing: %S\n", state_file_path);
    scratch_end(scratch);
    return;
  }

  os_file_write(file, rng_1u64(0, contents.size), contents.str);
  os_file_close(file);

  scratch_end(scratch);
}


//~ Continue Watching - Incremental Updates

internal void
continue_watching_update_entry(PlayerState *state, String8 file_path, f64 position, u64 timestamp, b32 is_watched)
{
  u64 entry_idx = state->continue_watching_count;
  for(u64 i = 0; i < state->continue_watching_count; i += 1)
  {
    if(str8_match(state->continue_watching[i].file_path, file_path, 0))
    {
      entry_idx = i;
      break;
    }
  }

  if(is_watched || position <= 1.0)
  {
    if(entry_idx < state->continue_watching_count)
    {
      state->continue_watching[entry_idx] = state->continue_watching[state->continue_watching_count - 1];
      state->continue_watching_count -= 1;
    }
    return;
  }

  if(entry_idx == state->continue_watching_count)
  {
    if(state->continue_watching_count >= 10) { return; }
    state->continue_watching_count += 1;
    state->continue_watching[entry_idx].file_path = str8_copy(state->perm_arena, file_path);
  }

  state->continue_watching[entry_idx].position_seconds       = position;
  state->continue_watching[entry_idx].last_watched_timestamp = timestamp;
  state->continue_watching[entry_idx].display_name           = str8_skip_last_slash(file_path);
}

internal void
continue_watching_sort(PlayerState *state)
{
  for(u64 i = 0; i < state->continue_watching_count; i += 1)
  {
    for(u64 j = i + 1; j < state->continue_watching_count; j += 1)
    {
      if(state->continue_watching[j].last_watched_timestamp > state->continue_watching[i].last_watched_timestamp)
      {
        ContinueWatchingEntry temp = state->continue_watching[i];
        state->continue_watching[i] = state->continue_watching[j];
        state->continue_watching[j] = temp;
      }
    }
  }
}

internal void
load_continue_watching(PlayerState *state, WatchProgressTable *table, u64 max_count)
{
  Temp scratch = scratch_begin(&state->perm_arena, 1);

  ContinueWatchingEntry *temp_entries = push_array(scratch.arena, ContinueWatchingEntry, max_count);
  u64 temp_count = 0;

  for(u64 i = 0; i < table->bucket_count; i += 1)
  {
    for(WatchProgress *wp = table->buckets[i]; wp != 0; wp = wp->next)
    {
      if(temp_count >= max_count) { break; }

      if(wp->position_seconds > 1.0 && !is_watched(wp))
      {
        temp_entries[temp_count].file_path              = wp->file_path;
        temp_entries[temp_count].position_seconds       = wp->position_seconds;
        temp_entries[temp_count].last_watched_timestamp = wp->last_watched_timestamp;
        temp_entries[temp_count].display_name           = str8_skip_last_slash(wp->file_path);
        temp_count += 1;
      }
    }
  }

  state->continue_watching       = push_array(state->perm_arena, ContinueWatchingEntry, temp_count);
  state->continue_watching_count = temp_count;

  for(u64 i = 0; i < temp_count; i += 1)
  {
    state->continue_watching[i].file_path              = str8_copy(state->perm_arena, temp_entries[i].file_path);
    state->continue_watching[i].position_seconds       = temp_entries[i].position_seconds;
    state->continue_watching[i].last_watched_timestamp = temp_entries[i].last_watched_timestamp;
    state->continue_watching[i].display_name           = str8_copy(state->perm_arena, temp_entries[i].display_name);
  }

  continue_watching_sort(state);

  scratch_end(scratch);
}

////////////////////////////////
//~ File Browser

internal int
compare_file_entries(const void *a, const void *b)
{
  FileEntry *fa = (FileEntry *)a;
  FileEntry *fb = (FileEntry *)b;

  if(fa->is_directory && !fb->is_directory) { return -1; }
  if(!fa->is_directory && fb->is_directory) { return 1; }

  u64 min_size = Min(fa->name.size, fb->name.size);
  for(u64 i = 0; i < min_size; i += 1)
  {
    u8 ca = lower_from_char(fa->name.str[i]);
    u8 cb = lower_from_char(fb->name.str[i]);
    if(ca != cb) { return (ca < cb) ? -1 : 1; }
  }

  if(fa->name.size != fb->name.size) { return (fa->name.size < fb->name.size) ? -1 : 1; }
  return 0;
}

internal void
load_directory(PlayerState *state, String8 dir_path)
{
  Temp scratch = scratch_begin(&state->perm_arena, 1);

  char *dir_cstr = (char *)push_array(scratch.arena, u8, dir_path.size + 1);
  MemoryCopy(dir_cstr, dir_path.str, dir_path.size);
  dir_cstr[dir_path.size] = 0;

  DIR *d = opendir(dir_cstr);
  if(d == 0)
  {
    state->file_count = 0;
    state->files      = 0;
    scratch_end(scratch);
    return;
  }

  FileEntry *temp_files = push_array(scratch.arena, FileEntry, 4096);
  u64        temp_count = 0;

  struct dirent *entry;
  for(; (entry = readdir(d)) != 0; )
  {
    String8 name = str8_cstring(entry->d_name);
    if(str8_match(name, str8_lit("."), 0) || str8_match(name, str8_lit(".."), 0)) { continue; }
    if(temp_count >= 4096) { break; }

    name = str8_copy(scratch.arena, name);

    String8 entry_path = str8f(scratch.arena, "%s/%S", dir_cstr, name);
    char   *entry_cstr = (char *)push_array(scratch.arena, u8, entry_path.size + 1);
    MemoryCopy(entry_cstr, entry_path.str, entry_path.size);
    entry_cstr[entry_path.size] = 0;

    struct stat st;
    if(stat(entry_cstr, &st) != 0) { continue; }

    WatchProgress *wp = watch_progress_find(g_watch_progress, entry_path);

    temp_files[temp_count].name             = name;
    temp_files[temp_count].is_directory     = S_ISDIR(st.st_mode);
    temp_files[temp_count].is_watched       = wp ? is_watched(wp) : 0;
    temp_files[temp_count].last_watched_pos = wp ? wp->position_seconds : -1.0;
    temp_files[temp_count].duration         = wp ? wp->duration_seconds : 0.0;
    temp_count += 1;
  }

  closedir(d);

  qsort(temp_files, temp_count, sizeof(FileEntry), compare_file_entries);

  state->files = push_array(state->perm_arena, FileEntry, temp_count);
  state->file_count = temp_count;
  for(u64 i = 0; i < temp_count; i += 1)
  {
    state->files[i].name             = str8_copy(state->perm_arena, temp_files[i].name);
    state->files[i].is_directory     = temp_files[i].is_directory;
    state->files[i].is_watched       = temp_files[i].is_watched;
    state->files[i].last_watched_pos = temp_files[i].last_watched_pos;
    state->files[i].duration         = temp_files[i].duration;
  }

  state->current_dir    = str8_copy(state->perm_arena, dir_path);
  state->selected_index = 0;
  state->scroll_offset  = 0;

  scratch_end(scratch);
}

////////////////////////////////
//~ MPV IPC Client

internal String8
mpv_ipc_read_response(Arena *arena, int socket_fd)
{
  u8 buffer[4096];
  ssize_t n = recv(socket_fd, buffer, sizeof(buffer), MSG_DONTWAIT);
  if(n <= 0) { return str8_zero(); }

  return str8_copy(arena, str8(buffer, n));
}

internal void
mpv_ipc_send_command(MPVClient *mpv, String8 command)
{
  if(mpv == 0 || mpv->socket_fd < 0) { return; }

  Temp scratch = scratch_begin(&mpv->arena, 1);

  String8 msg = str8f(scratch.arena, "%S\n", command);
  send(mpv->socket_fd, msg.str, msg.size, 0);

  scratch_end(scratch);
}

internal f64
mpv_get_property_f64(MPVClient *mpv, String8 property)
{
  if(mpv == 0 || mpv->socket_fd < 0) { return 0.0; }

  Temp scratch = scratch_begin(&mpv->arena, 1);

  mpv->request_id += 1;
  String8 cmd      = str8f(scratch.arena, "{\"command\":[\"get_property\",\"%S\"],\"request_id\":%llu}",
                           property, mpv->request_id);
  mpv_ipc_send_command(mpv, cmd);

  for(int retry = 0; retry < 10; retry += 1)
  {
    os_sleep_milliseconds(10);
    String8 response = mpv_ipc_read_response(scratch.arena, mpv->socket_fd);
    if(response.size == 0) { continue; }

    String8 needle   = str8_lit("\"data\":");
    u64     data_pos = str8_find_needle(response, 0, needle, 0);
    if(data_pos < response.size)
    {
      String8 after_data = str8_skip(response, data_pos + needle.size);
      f64     value      = f64_from_str8(after_data);
      scratch_end(scratch);
      return value;
    }
  }

  scratch_end(scratch);
  return 0.0;
}

internal b32
mpv_is_running(MPVClient *mpv)
{
  if(mpv == 0 || mpv->child_pid <= 0) { return 0; }

  int status;
  int result = waitpid(mpv->child_pid, &status, WNOHANG);

  return result == 0;
}

internal MPVClient *
mpv_start(Arena *arena, String8 file_path, f64 start_position)
{
  Temp scratch = scratch_begin(&arena, 1);

  MPVClient *mpv  = push_array(arena, MPVClient, 1);
  MemoryZeroStruct(mpv);
  mpv->arena      = arena;
  mpv->request_id = 0;
  mpv->socket_fd  = -1;

  u64     timestamp = os_now_microseconds();
  mpv->ipc_path     = str8f(arena, "/tmp/mpv-ipc-%llu.sock", timestamp);

  char *file_cstr = (char *)push_array(scratch.arena, u8, file_path.size + 1);
  MemoryCopy(file_cstr, file_path.str, file_path.size);
  file_cstr[file_path.size] = 0;

  char *ipc_cstr = (char *)push_array(scratch.arena, u8, mpv->ipc_path.size + 1);
  MemoryCopy(ipc_cstr, mpv->ipc_path.str, mpv->ipc_path.size);
  ipc_cstr[mpv->ipc_path.size] = 0;

  int pid = fork();
  if(pid == 0)
  {
    char start_pos_str[64];
    char ipc_arg[512];
    char start_arg[128];

    snprintf(ipc_arg, sizeof(ipc_arg), "--input-ipc-server=%s", ipc_cstr);
    snprintf(start_pos_str, sizeof(start_pos_str), "%.2f", start_position);
    snprintf(start_arg, sizeof(start_arg), "--start=%s", start_pos_str);

    if(start_position > 1.0)
    {
      execlp("mpv", "mpv",
             ipc_arg,
             "--fullscreen",
             "--keep-open=yes",
             "--osd-level=1",
             start_arg,
             file_cstr,
             NULL);
    }
    else
    {
      execlp("mpv", "mpv",
             ipc_arg,
             "--fullscreen",
             "--keep-open=yes",
             "--osd-level=1",
             file_cstr,
             NULL);
    }

    exit(1);
  }
  else if(pid > 0)
  {
    mpv->child_pid = pid;
    mpv->running   = 1;

    for(int retry = 0; retry < 100; retry += 1)
    {
      os_sleep_milliseconds(50);

      struct sockaddr_un addr = {0};
      addr.sun_family = AF_UNIX;
      strncpy(addr.sun_path, ipc_cstr, sizeof(addr.sun_path) - 1);

      mpv->socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
      if(mpv->socket_fd >= 0)
      {
        if(connect(mpv->socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
        {
          int flags = fcntl(mpv->socket_fd, F_GETFL, 0);
          fcntl(mpv->socket_fd, F_SETFL, flags | O_NONBLOCK);

          mpv_ipc_send_command(mpv, str8_lit("{\"command\":[\"observe_property\",1,\"time-pos\"]}"));
          mpv_ipc_send_command(mpv, str8_lit("{\"command\":[\"observe_property\",2,\"duration\"]}"));

          scratch_end(scratch);
          return mpv;
        }
        close(mpv->socket_fd);
        mpv->socket_fd = -1;
      }
    }

    log_errorf("media-player: failed to connect to mpv IPC socket after retries\n");
  }
  else
  {
    log_errorf("media-player: fork failed\n");
  }

  scratch_end(scratch);
  return 0;
}

internal void
mpv_stop(MPVClient *mpv)
{
  mpv_ipc_send_command(mpv, str8_lit("{\"command\":[\"quit\"]}"));

  if(mpv->child_pid > 0)
  {
    for(int retry = 0; retry < 20; retry += 1)
    {
      int status;
      int result = waitpid(mpv->child_pid, &status, WNOHANG);
      if(result != 0) { break; }
      os_sleep_milliseconds(50);
    }

    kill(mpv->child_pid, SIGTERM);
  }

  if(mpv->socket_fd >= 0) { close(mpv->socket_fd); }

  Temp  scratch  = scratch_begin(&mpv->arena, 1);
  char *ipc_cstr = (char *)push_array(scratch.arena, u8, mpv->ipc_path.size + 1);
  MemoryCopy(ipc_cstr, mpv->ipc_path.str, mpv->ipc_path.size);
  ipc_cstr[mpv->ipc_path.size] = 0;
  unlink(ipc_cstr);
  scratch_end(scratch);

  mpv->running = 0;
}

////////////////////////////////
//~ Terminal

internal Terminal *
terminal_init(Arena *arena)
{
  Terminal *term = push_array(arena, Terminal, 1);

  setlocale(LC_ALL, "");
  term->win = initscr();
  if(term->win == 0) { return 0; }

  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  timeout(100);

  getmaxyx(stdscr, term->height, term->width);

  if(has_colors())
  {
    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_BLACK, COLOR_WHITE);
    init_pair(3, COLOR_GREEN, COLOR_BLACK);
    init_pair(4, COLOR_CYAN, COLOR_BLACK);
  }

  return term;
}

internal void
terminal_shutdown(Terminal *term)
{
  endwin();
}

internal void
render_home_ncurses(Terminal *term, PlayerState *state)
{
  clear();

  int y        = 0;
  u64 selected = state->selected_index;

  attron(A_BOLD);
  mvprintw(y, 0, "Media Library");
  y += 1;
  attroff(A_BOLD);
  y += 1;

  for(u64 i = 0; i < g_root_count; i += 1)
  {
    if(selected == i) attron(COLOR_PAIR(2));
    mvprintw(y, 2, "> %.*s", (int)g_roots[i].name.size, g_roots[i].name.str);
    y += 1;
    if(selected == i) attroff(COLOR_PAIR(2));
  }
  y += 1;

  if(state->continue_watching_count > 0)
  {
    attron(COLOR_PAIR(4));
    mvprintw(y, 0, "Continue Watching:");
    y += 1;
    attroff(COLOR_PAIR(4));

    for(u64 i = 0; i < state->continue_watching_count; i += 1)
    {
      ContinueWatchingEntry *entry = &state->continue_watching[i];
      u64 item_index = g_root_count + i;

      if(selected == item_index) attron(COLOR_PAIR(2));
      else attron(COLOR_PAIR(3));

      int mins = (int)(entry->position_seconds / 60);
      int secs = (int)(entry->position_seconds) % 60;
      mvprintw(y, 2, "* %.*s [%02d:%02d]", (int)entry->display_name.size, entry->display_name.str, mins, secs);
      y += 1;

      if(selected == item_index) attroff(COLOR_PAIR(2));
      else attroff(COLOR_PAIR(3));
    }
  }

  attron(COLOR_PAIR(4));
  mvprintw(term->height - 1, 0, "↑↓=Navigate  Enter=Select  ESC=Quit");
  attroff(COLOR_PAIR(4));

  refresh();
}

internal void
render_browser_ncurses(Terminal *term, PlayerState *state, String8 header)
{
  clear();

  int y = 0;

  attron(A_BOLD);
  mvprintw(y, 0, "%.*s", (int)header.size, header.str);
  y += 1;
  attroff(A_BOLD);
  y += 1;

  if(state->file_count == 0)
  {
    attron(COLOR_PAIR(4));
    mvprintw(y, 2, "(empty)");
    attroff(COLOR_PAIR(4));
  }
  else
  {
    u64 visible_count = Min(term->height - 4, state->file_count);
    u64 start = state->scroll_offset;
    u64 end = Min(state->file_count, start + visible_count);

    for(u64 i = start; i < end; i += 1)
    {
      FileEntry *entry = &state->files[i];
      b32 selected = (i == state->selected_index);

      if(selected) attron(COLOR_PAIR(2));

      if(entry->is_directory)
      {
        mvprintw(y, 2, "> %.*s/", (int)entry->name.size, entry->name.str);
        y += 1;
      }
      else
      {
        if(entry->is_watched)
        {
          attron(COLOR_PAIR(4));
          mvprintw(y, 2, "✓ %.*s", (int)entry->name.size, entry->name.str);
          y += 1;
          attroff(COLOR_PAIR(4));
        }
        else if(entry->last_watched_pos > 0.0)
        {
          int mins = (int)(entry->last_watched_pos / 60);
          int secs = (int)(entry->last_watched_pos) % 60;
          attron(COLOR_PAIR(3));
          mvprintw(y, 2, "* %.*s [%02d:%02d]", (int)entry->name.size, entry->name.str, mins, secs);
          y += 1;
          attroff(COLOR_PAIR(3));
        }
        else
        {
          mvprintw(y, 2, "  %.*s", (int)entry->name.size, entry->name.str);
          y += 1;
        }
      }

      if(selected) attroff(COLOR_PAIR(2));
    }
  }

  attron(COLOR_PAIR(4));
  mvprintw(term->height - 2, 0, "%lu items", state->file_count);
  mvprintw(term->height - 1, 0, "↑↓=Navigate  Enter=Select  W=Mark Watched  Backspace=Back  ESC=Quit");
  attroff(COLOR_PAIR(4));

  refresh();
}

////////////////////////////////
//~ Forward Declarations

internal b32  start_playback(PlayerState *state, String8 file_path);
internal void stop_playback(PlayerState *state);
internal void update_playback(PlayerState *state);

////////////////////////////////
//~ Input

internal void
handle_browser_input_ncurses(PlayerState *state, int ch)
{
  if(state->mode == PlayerMode_Home)
  {
    u64 max_index = g_root_count + state->continue_watching_count;
    if(max_index > 0) { max_index -= 1; }

    if(ch == KEY_DOWN)
    {
      if(state->selected_index < max_index) { state->selected_index += 1; }
    }
    else if(ch == KEY_UP)
    {
      if(state->selected_index > 0) { state->selected_index -= 1; }
    }
    else if(ch == '\n' || ch == 10)
    {
      if(state->selected_index < g_root_count)
      {
        state->current_root_index = state->selected_index;
        state->mode               = PlayerMode_Browsing;
        load_directory(state, g_roots[state->selected_index].path);
      }
      else
      {
        u64 cw_idx = state->selected_index - g_root_count;
        if(cw_idx < state->continue_watching_count)
        {
          start_playback(state, state->continue_watching[cw_idx].file_path);
        }
      }
    }
  }
  else if(state->mode == PlayerMode_Browsing)
  {
    if(ch == KEY_DOWN)
    {
      if(state->selected_index < state->file_count - 1)
      {
        state->selected_index += 1;
        if(state->selected_index >= state->scroll_offset + 15)
        {
          state->scroll_offset += 1;
        }
      }
    }
    else if(ch == KEY_UP)
    {
      if(state->selected_index > 0)
      {
        state->selected_index -= 1;
        if(state->selected_index < state->scroll_offset)
        {
          state->scroll_offset -= 1;
        }
      }
    }
    else if(ch == '\n' || ch == 10)
    {
      if(state->file_count == 0) { return; }

      FileEntry *entry = &state->files[state->selected_index];
      String8 path = str8f(state->perm_arena, "%S/%S", state->current_dir, entry->name);

      if(entry->is_directory) { load_directory(state, path); }
      else { start_playback(state, path); }
    }
    else if(ch == 'w' || ch == 'W')
    {
      if(state->file_count == 0) { return; }

      FileEntry *entry = &state->files[state->selected_index];
      if(!entry->is_directory)
      {
        String8 file_path  = str8f(state->perm_arena, "%S/%S", state->current_dir, entry->name);
        Temp    scratch    = scratch_begin(&state->perm_arena, 1);
        String8 state_file = str8f(scratch.arena, "%S/watch-progress.txt", g_state_dir_path);

        watch_progress_mark_watched(g_watch_progress, file_path);
        watch_progress_save(g_watch_progress, state_file);
        scratch_end(scratch);

        continue_watching_update_entry(state, file_path, 0.0, 0, 1);
        load_directory(state, state->current_dir);
      }
    }
    else if(ch == KEY_BACKSPACE || ch == 127)
    {
      if(state->current_root_index < g_root_count)
      {
        String8 root_path = g_roots[state->current_root_index].path;
        if(str8_match(state->current_dir, root_path, 0))
        {
          state->mode           = PlayerMode_Home;
          state->selected_index = 0;
        }
        else
        {
          String8 parent = str8_chop_last_slash(state->current_dir);
          load_directory(state, parent);
        }
      }
    }
  }
}

////////////////////////////////
//~ Playback

internal b32
start_playback(PlayerState *state, String8 file_path)
{
  Temp scratch = scratch_begin(&state->perm_arena, 1);

  WatchProgress *wp = watch_progress_find(g_watch_progress, file_path);
  f64 start_pos     = (wp && wp->position_seconds > 1.0) ? wp->position_seconds : 0.0;

  state->mpv = mpv_start(state->perm_arena, file_path, start_pos);
  if(state->mpv == 0)
  {
    log_errorf("media-player: failed to start mpv\n");
    scratch_end(scratch);
    return 0;
  }

  state->current_file           = str8_copy(state->perm_arena, file_path);
  state->current_watch_progress = wp;
  state->last_position_save     = os_now_microseconds();
  state->last_known_position    = start_pos;
  state->last_known_duration    = 0.0;
  state->mode                   = PlayerMode_Playing;

  scratch_end(scratch);
  return 1;
}

internal void
stop_playback(PlayerState *state)
{
  if(state->mpv != 0)
  {
    if(state->current_file.size > 0 && state->last_known_position >= 0.0)
    {
      Temp scratch = scratch_begin(&state->perm_arena, 1);

      WatchProgress *wp = state->current_watch_progress;
      if(wp == 0)
      {
        wp = watch_progress_update(g_watch_progress, state->current_file, state->last_known_position, state->last_known_duration);
      }
      else
      {
        wp->position_seconds       = state->last_known_position;
        wp->duration_seconds       = state->last_known_duration;
        wp->last_watched_timestamp = os_now_microseconds() / Million(1);
        if(wp->duration_seconds > 0.0 && (wp->position_seconds / wp->duration_seconds) > 0.90) { wp->is_watched = 1; }
      }

      String8 state_file = str8f(scratch.arena, "%S/watch-progress.txt", g_state_dir_path);
      watch_progress_save(g_watch_progress, state_file);
      scratch_end(scratch);

      b32 watched = wp ? is_watched(wp) : 0;
      continue_watching_update_entry(state, state->current_file, state->last_known_position, os_now_microseconds() / Million(1), watched);
      continue_watching_sort(state);
    }

    mpv_stop(state->mpv);
    state->mpv = 0;
  }

  state->current_file           = str8_zero();
  state->current_watch_progress = 0;
  state->mode                   = PlayerMode_Home;
  state->selected_index         = 0;
  state->needs_render           = 1;
}

internal void
update_playback(PlayerState *state)
{
  if(state->mpv == 0) { return; }
  if(!mpv_is_running(state->mpv)) { stop_playback(state); return; }

  Temp    scratch  = scratch_begin(&state->perm_arena, 1);
  String8 response = mpv_ipc_read_response(scratch.arena, state->mpv->socket_fd);

  if(response.size > 0)
  {
    for(u64 offset = 0; offset < response.size; )
    {
      u64     newline = str8_find_needle(response, offset, str8_lit("\n"), 0);
      u64     end     = (newline < response.size) ? newline : response.size;
      String8 line    = str8_substr(response, rng_1u64(offset, end));

      u64 prop_change_pos = str8_find_needle(line, 0, str8_lit("\"property-change\""), 0);
      if(prop_change_pos < line.size)
      {
        u64 id_pos   = str8_find_needle(line, 0, str8_lit("\"id\":"), 0);
        u64 data_pos = str8_find_needle(line, 0, str8_lit("\"data\":"), 0);

        if(id_pos < line.size && data_pos < line.size)
        {
          String8 after_id   = str8_skip(line, id_pos + 5);
          String8 after_data = str8_skip(line, data_pos + 7);

          u64 id_end = 0;
          for(; id_end < after_id.size; id_end += 1)
          {
            u8 c = after_id.str[id_end];
            if(c < '0' || c > '9') { break; }
          }
          String8 id_str = str8_prefix(after_id, id_end);

          u64 data_end = 0;
          for(; data_end < after_data.size; data_end += 1)
          {
            u8 c = after_data.str[data_end];
            if(!((c >= '0' && c <= '9') || c == '.' || c == '-')) { break; }
          }
          String8 data_str = str8_prefix(after_data, data_end);

          u64 id    = u64_from_str8(id_str, 10);
          f64 value = f64_from_str8(data_str);

          if(id == 1) { state->last_known_position = value; }
          else if(id == 2) { state->last_known_duration = value; }
        }
      }

      offset = (newline < response.size) ? newline + 1 : response.size;
    }
  }
  scratch_end(scratch);

  u64 now = os_now_microseconds();
  if(now - state->last_position_save > Million(5))
  {
    Temp scratch2 = scratch_begin(&state->perm_arena, 1);

    WatchProgress *wp = state->current_watch_progress;
    if(wp == 0)
    {
      wp = watch_progress_update(g_watch_progress, state->current_file, state->last_known_position, state->last_known_duration);
      state->current_watch_progress = wp;
    }
    else
    {
      wp->position_seconds       = state->last_known_position;
      wp->duration_seconds       = state->last_known_duration;
      wp->last_watched_timestamp = now / Million(1);
      if(wp->duration_seconds > 0.0 && (wp->position_seconds / wp->duration_seconds) > 0.90) { wp->is_watched = 1; }
    }

    String8 state_file = str8f(scratch2.arena, "%S/watch-progress.txt", g_state_dir_path);
    watch_progress_save(g_watch_progress, state_file);
    scratch_end(scratch2);

    b32 watched = wp ? is_watched(wp) : 0;
    continue_watching_update_entry(state, state->current_file, state->last_known_position, now / Million(1), watched);

    state->last_position_save = now;
  }
}

////////////////////////////////
//~ Main Loop

internal void
main_loop(PlayerState *state)
{
  state->needs_render = 1;

  for(; !state->quit; )
  {
    int ch = getch();
    if(ch != ERR && IS_BROWSE_MODE(state->mode))
    {
      if(ch == 27)
      {
        if(state->mode == PlayerMode_Home) { state->quit = 1; }
        else
        {
          state->mode         = PlayerMode_Home;
          state->needs_render = 1;
        }
      }
      else
      {
        handle_browser_input_ncurses(state, ch);
        state->needs_render = 1;
      }
    }

    if(state->mode == PlayerMode_Playing) { update_playback(state); }

    if(IS_BROWSE_MODE(state->mode) && state->needs_render)
    {
      if(state->mode == PlayerMode_Home)
      {
        render_home_ncurses(state->term, state);
      }
      else
      {
        String8 header = state->current_dir;
        render_browser_ncurses(state->term, state, header);
      }

      state->needs_render = 0;
    }
  }
}

////////////////////////////////
//~ Exit/Signal Handlers

internal void
signal_handler(int sig)
{
  if(sig == SIGCHLD || sig == SIGPIPE)
  {
    return;
  }

  _exit(1);
}

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
  signal(SIGTERM, signal_handler);
  signal(SIGHUP, signal_handler);
  signal(SIGINT, signal_handler);
  signal(SIGQUIT, signal_handler);
  signal(SIGCHLD, signal_handler);
  signal(SIGPIPE, signal_handler);
  signal(SIGABRT, signal_handler);
  signal(SIGSEGV, signal_handler);
  signal(SIGILL, signal_handler);
  signal(SIGFPE, signal_handler);

  Temp scratch = scratch_begin(0, 0);
  Log *log     = log_alloc();
  log_select(log);
  log_scope_begin();

  String8 state_dir = cmd_line_string(cmd_line, str8_lit("state-dir"));
  if(state_dir.size == 0) { state_dir = str8_lit("/home/mpd/.local/state"); }
  g_state_dir_path = state_dir;

  Arena *perm_arena = arena_alloc();

  String8List root_strings = cmd_line_strings(cmd_line, str8_lit("root"));
  if(root_strings.node_count == 0)
  {
    str8_list_push(perm_arena, &root_strings, str8_lit("/home/mpd/n/shows"));
    str8_list_push(perm_arena, &root_strings, str8_lit("/home/mpd/n/movies"));
  }

  g_root_count = root_strings.node_count;
  g_roots      = push_array(perm_arena, MediaRoot, g_root_count);

  u64 root_idx = 0;
  for(String8Node *node = root_strings.first; node != 0; node = node->next)
  {
    String8 path = node->string;
    if(path.size > 0 && path.str[path.size - 1] == '/') { path = str8_chop_last_slash(path); }

    g_roots[root_idx].path = str8_copy(perm_arena, path);
    g_roots[root_idx].name = str8_skip_last_slash(path);
    log_infof("media-player: root [%llu]: %S (%S)\n", root_idx, g_roots[root_idx].name, g_roots[root_idx].path);
    root_idx += 1;
  }

  log_infof("media-player: state dir: %S\n", g_state_dir_path);

  OS_Handle state_dir_handle = os_file_open(OS_AccessFlag_Read, g_state_dir_path);
  if(state_dir_handle.u64[0] == 0)
  {
    b32 created = os_make_directory(g_state_dir_path);
    if(!created) { log_errorf("media-player: failed to create state directory\n"); }
  }
  else
  {
    os_file_close(state_dir_handle);
  }

  g_watch_progress   = watch_progress_table_alloc(perm_arena, 2048);
  String8 state_file = str8f(perm_arena, "%S/watch-progress.txt", g_state_dir_path);
  watch_progress_load(g_watch_progress, state_file);

  Terminal *term = terminal_init(perm_arena);
  if(term == 0)
  {
    log_scope_flush(scratch.arena);
    scratch_end(scratch);
    return;
  }

  PlayerState state  = {0};
  state.perm_arena   = perm_arena;
  state.term         = term;
  state.mode         = PlayerMode_Home;
  state.quit         = 0;

  load_continue_watching(&state, g_watch_progress, 10);

  main_loop(&state);

  if(state.mpv != 0) { stop_playback(&state); }

  terminal_shutdown(state.term);

  log_scope_flush(scratch.arena);
  scratch_end(scratch);
}
