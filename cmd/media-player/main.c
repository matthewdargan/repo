#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <ncurses.h>

#include "base/inc.h"
#include "9p/inc.h"
#include "base/inc.c"
#include "9p/inc.c"

////////////////////////////////
//~ Types

typedef enum PlayerMode PlayerMode;
enum PlayerMode
{
  PlayerMode_Home,
  PlayerMode_Shows,
  PlayerMode_Movies,
  PlayerMode_Playing,
};

#define IS_BROWSE_MODE(mode) ((mode) == PlayerMode_Home || (mode) == PlayerMode_Shows || (mode) == PlayerMode_Movies)

typedef struct FileEntry FileEntry;
struct FileEntry
{
  String8 name;
  b32 is_directory;
  u64 size;
  f64 last_watched_pos;
  f64 duration;
  u64 last_watched_time;
  b32 is_watched;
};

typedef struct WatchProgress WatchProgress;
struct WatchProgress
{
  String8 file_path;
  f64 position_seconds;
  u64 last_watched_timestamp;
  String8 subtitle_lang;
  String8 audio_lang;
  f64 duration_seconds;
  b32 is_watched;
  WatchProgress *next;
};

typedef struct WatchProgressTable WatchProgressTable;
struct WatchProgressTable
{
  Arena *arena;
  WatchProgress **buckets;
  u64 bucket_count;
};

internal b32
is_watched(WatchProgress *wp)
{
  if(wp == 0) { return 0; }
  return wp->is_watched;
}

//~ MPV IPC Client
typedef struct MPVClient MPVClient;
struct MPVClient
{
  int socket_fd;
  int child_pid;
  Arena *arena;
  u64 request_id;
  b32 running;
  String8 ipc_path;
};

//~ Terminal (ncurses UI)
typedef struct Terminal Terminal;
struct Terminal
{
  WINDOW *win;
  u32 height;
  u32 width;
};

typedef struct ContinueWatchingEntry ContinueWatchingEntry;
struct ContinueWatchingEntry
{
  String8 file_path;
  String8 display_name;
  f64 position_seconds;
  u64 last_watched_timestamp;
  b32 is_next_episode;
};

typedef struct PlayerState PlayerState;
struct PlayerState
{
  Arena *perm_arena;

  PlayerMode mode;
  b32 quit;
  b32 needs_render;
  b32 continue_watching_dirty;

  String8 current_dir;
  String8 current_file;
  FileEntry *files;
  u64 file_count;
  u64 selected_index;
  u64 scroll_offset;

  ContinueWatchingEntry *continue_watching;
  u64 continue_watching_count;

  MPVClient *mpv;
  Terminal *term;

  u64 last_poll_time;
  u64 last_position_save;
  f64 last_known_position;
  f64 last_known_duration;
};

////////////////////////////////
//~ Globals

global String8 g_media_root_path = {0};
global String8 g_state_dir_path   = {0};
global Client9P *g_9p_client      = 0;
global WatchProgressTable *g_watch_progress = 0;

////////////////////////////////
//~ Watch Progress Table

internal u64
hash_string(String8 str)
{
  u64 hash = 5381;
  for(u64 i = 0; i < str.size; i += 1)
  {
    hash = ((hash << 5) + hash) + str.str[i];
  }
  return hash;
}

internal WatchProgressTable *
watch_progress_table_alloc(Arena *arena, u64 bucket_count)
{
  WatchProgressTable *table = push_array(arena, WatchProgressTable, 1);
  table->arena        = arena;
  table->bucket_count = bucket_count;
  table->buckets      = push_array(arena, WatchProgress *, bucket_count);
  MemoryZero(table->buckets, bucket_count * sizeof(WatchProgress *));
  return table;
}

internal WatchProgress *
watch_progress_find(WatchProgressTable *table, String8 file_path)
{
  if(table == 0 || table->bucket_count == 0) { return 0; }

  u64 hash = hash_string(file_path);
  u64 idx  = hash % table->bucket_count;

  for(WatchProgress *wp = table->buckets[idx]; wp != 0; wp = wp->next)
  {
    if(str8_match(wp->file_path, file_path, 0)) { return wp; }
  }

  return 0;
}

internal void
watch_progress_update(WatchProgressTable *table, String8 file_path, f64 position, f64 duration)
{
  if(table == 0) { return; }

  u64 hash = hash_string(file_path);
  u64 idx  = hash % table->bucket_count;

  WatchProgress *wp = watch_progress_find(table, file_path);
  if(wp == 0)
  {
    wp = push_array(table->arena, WatchProgress, 1);
    wp->file_path     = str8_copy(table->arena, file_path);
    wp->subtitle_lang = str8_zero();
    wp->audio_lang    = str8_zero();
    wp->next          = table->buckets[idx];
    table->buckets[idx] = wp;
  }

  wp->position_seconds       = position;
  wp->duration_seconds       = duration;
  wp->last_watched_timestamp = os_now_microseconds() / Million(1);

  // Auto-mark as watched if position is near end (90%+)
  if(duration > 0.0 && (position / duration) > 0.90)
  {
    wp->is_watched = 1;
  }
}

internal void
watch_progress_mark_watched(WatchProgressTable *table, String8 file_path)
{
  if(table == 0) { return; }

  u64 hash = hash_string(file_path);
  u64 idx  = hash % table->bucket_count;

  WatchProgress *wp = watch_progress_find(table, file_path);
  if(wp == 0)
  {
    wp = push_array(table->arena, WatchProgress, 1);
    wp->file_path     = str8_copy(table->arena, file_path);
    wp->subtitle_lang = str8_zero();
    wp->audio_lang    = str8_zero();
    wp->position_seconds = 0.0;
    wp->duration_seconds = 0.0;
    wp->next          = table->buckets[idx];
    table->buckets[idx] = wp;
  }

  wp->is_watched = 1;
  wp->last_watched_timestamp = os_now_microseconds() / Million(1);
}

internal void
watch_progress_load(WatchProgressTable *table, String8 state_file_path)
{
  if(table == 0) { return; }

  Temp scratch = scratch_begin(&table->arena, 1);

  OS_Handle file = os_file_open(OS_AccessFlag_Read, state_file_path);
  if(file.u64[0] == 0)
  {
    log_infof("media-player: no watch progress file found at %S\n", state_file_path);
    scratch_end(scratch);
    return;
  }

  OS_FileProperties props = os_properties_from_file(file);
  u8 *buffer = push_array(scratch.arena, u8, props.size);
  os_file_read(file, rng_1u64(0, props.size), buffer);
  String8 contents = str8(buffer, props.size);
  os_file_close(file);

  log_infof("media-player: loaded watch progress from %S (%llu bytes)\n", state_file_path, props.size);

  String8List lines = str8_split(scratch.arena, contents, (u8 *)"\n", 1, 0);
  for(String8Node *node = lines.first; node != 0; node = node->next)
  {
    String8 line = str8_skip_chop_whitespace(node->string);
    if(line.size == 0 || line.str[0] == '#') { continue; }

    String8 file_path = {0};
    f64 position = 0.0;
    u64 timestamp = 0;
    String8 subtitle_lang = str8_lit("-");
    String8 audio_lang = str8_lit("-");
    f64 duration = 0.0;
    b32 watched = 0;

    String8List parts = str8_split(scratch.arena, line, (u8 *)"\t", 1, 0);
    if(parts.node_count >= 3)
    {
      String8Node *n = parts.first;
      file_path = str8_skip_chop_whitespace(n->string);
      n = n->next;
      position = f64_from_str8(n->string);
      n = n->next;
      timestamp = u64_from_str8(n->string, 10);

      if(n->next != 0)
      {
        n = n->next;
        String8 sub = str8_skip_chop_whitespace(n->string);
        if(sub.size > 0 && !str8_match(sub, str8_lit("-"), 0))
        {
          subtitle_lang = sub;
        }
      }
      if(n->next != 0)
      {
        n = n->next;
        String8 aud = str8_skip_chop_whitespace(n->string);
        if(aud.size > 0 && !str8_match(aud, str8_lit("-"), 0))
        {
          audio_lang = aud;
        }
      }
      if(n->next != 0)
      {
        n = n->next;
        duration = f64_from_str8(n->string);
      }
      if(n->next != 0)
      {
        n = n->next;
        watched = (u64_from_str8(n->string, 10) != 0);
      }

      WatchProgress *wp = watch_progress_find(table, file_path);
      if(wp == 0)
      {
        u64 hash = hash_string(file_path);
        u64 idx  = hash % table->bucket_count;

        wp = push_array(table->arena, WatchProgress, 1);
        wp->file_path = str8_copy(table->arena, file_path);
        wp->next      = table->buckets[idx];
        table->buckets[idx] = wp;
      }

      wp->position_seconds       = position;
      wp->last_watched_timestamp = timestamp;
      wp->subtitle_lang          = str8_copy(table->arena, subtitle_lang);
      wp->audio_lang             = str8_copy(table->arena, audio_lang);
      wp->duration_seconds       = duration;
      wp->is_watched             = watched;
    }
  }

  scratch_end(scratch);
}

internal void
watch_progress_save(WatchProgressTable *table, String8 state_file_path)
{
  if(table == 0) { return; }

  Temp scratch = scratch_begin(&table->arena, 1);

  String8List lines = {0};
  str8_list_push(scratch.arena, &lines, str8_lit("# Media Player Watch State\n"));
  str8_list_push(scratch.arena, &lines, str8_lit("# Format: file_path\\tposition\\ttimestamp\\tsub\\taud\\tduration\\twatched\n"));

  for(u64 i = 0; i < table->bucket_count; i += 1)
  {
    for(WatchProgress *wp = table->buckets[i]; wp != 0; wp = wp->next)
    {
      String8 sub = wp->subtitle_lang.size > 0 ? wp->subtitle_lang : str8_lit("-");
      String8 aud = wp->audio_lang.size > 0 ? wp->audio_lang : str8_lit("-");
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

  log_infof("media-player: saved watch progress to %S (%llu bytes)\n", state_file_path, contents.size);

  scratch_end(scratch);
}

internal String8
filename_from_path(String8 path)
{
  for(u64 i = path.size; i > 0; i -= 1)
  {
    if(path.str[i - 1] == '/')
    {
      return str8(path.str + i, path.size - i);
    }
  }
  return path;
}

typedef struct EpisodeInfo EpisodeInfo;
struct EpisodeInfo
{
  b32 is_episode;
  u32 season;
  u32 episode;
  String8 show_path;
};

internal EpisodeInfo
parse_episode_info(String8 file_path)
{
  EpisodeInfo info = {0};

  String8 filename = filename_from_path(file_path);

  // Look for SxxExx pattern in filename
  for(u64 i = 0; i < filename.size - 5; i += 1)
  {
    if((filename.str[i] == 'S' || filename.str[i] == 's') &&
       (filename.str[i + 3] == 'E' || filename.str[i + 3] == 'e'))
    {
      u8 s1 = filename.str[i + 1];
      u8 s2 = filename.str[i + 2];
      u8 e1 = filename.str[i + 4];
      u8 e2 = filename.str[i + 5];

      if(s1 >= '0' && s1 <= '9' && s2 >= '0' && s2 <= '9' &&
         e1 >= '0' && e1 <= '9' && e2 >= '0' && e2 <= '9')
      {
        info.is_episode = 1;
        info.season = (s1 - '0') * 10 + (s2 - '0');
        info.episode = (e1 - '0') * 10 + (e2 - '0');

        // Extract show path (everything up to /Season XX/)
        String8 dir = str8_chop_last_slash(file_path);
        info.show_path = str8_chop_last_slash(dir);
        break;
      }
    }
  }

  return info;
}

internal void
load_continue_watching(PlayerState *state, WatchProgressTable *table, u64 max_count)
{
  if(table == 0) { return; }

  Temp scratch = scratch_begin(&state->perm_arena, 1);

  ContinueWatchingEntry *temp_entries = push_array(scratch.arena, ContinueWatchingEntry, max_count * 2);
  u64 temp_count = 0;

  // Collect in-progress items
  for(u64 i = 0; i < table->bucket_count; i += 1)
  {
    for(WatchProgress *wp = table->buckets[i]; wp != 0; wp = wp->next)
    {
      if(wp->position_seconds > 1.0 && !is_watched(wp))
      {
        if(temp_count >= max_count * 2) { break; }
        temp_entries[temp_count].file_path            = wp->file_path;
        temp_entries[temp_count].position_seconds     = wp->position_seconds;
        temp_entries[temp_count].last_watched_timestamp = wp->last_watched_timestamp;
        temp_entries[temp_count].is_next_episode      = 0;
        temp_entries[temp_count].display_name         = filename_from_path(wp->file_path);
        temp_count += 1;
      }
    }
  }

  // Find next episodes for recently watched shows
  for(u64 i = 0; i < table->bucket_count; i += 1)
  {
    for(WatchProgress *wp = table->buckets[i]; wp != 0; wp = wp->next)
    {
      if(is_watched(wp))
      {
        EpisodeInfo ep = parse_episode_info(wp->file_path);
        if(ep.is_episode)
        {
          // Build next episode pattern
          String8 next_pattern = str8f(scratch.arena, "S%02dE%02d", ep.season, ep.episode + 1);

          // Check if next episode exists in watch progress
          b32 found_next = 0;
          for(u64 j = 0; j < table->bucket_count; j += 1)
          {
            for(WatchProgress *next_wp = table->buckets[j]; next_wp != 0; next_wp = next_wp->next)
            {
              String8 next_filename = filename_from_path(next_wp->file_path);
              b32 contains_pattern = 0;
              for(u64 k = 0; k + next_pattern.size <= next_filename.size; k += 1)
              {
                if(MemoryCompare(next_filename.str + k, next_pattern.str, next_pattern.size) == 0)
                {
                  contains_pattern = 1;
                  break;
                }
              }
              if(contains_pattern)
              {
                found_next = 1;
                break;
              }
            }
            if(found_next) { break; }
          }

          if(!found_next)
          {
            // Next episode hasn't been started - check if file exists
            String8 full_path = str8f(scratch.arena, "%S/%S", g_media_root_path, wp->file_path);
            String8 full_dir = str8_chop_last_slash(full_path);

            char *dir_cstr = (char *)push_array(scratch.arena, u8, full_dir.size + 1);
            MemoryCopy(dir_cstr, full_dir.str, full_dir.size);
            dir_cstr[full_dir.size] = 0;

            DIR *d = opendir(dir_cstr);
            if(d != 0)
            {
              struct dirent *entry;
              while((entry = readdir(d)) != 0)
              {
                String8 name = str8_cstring(entry->d_name);
                b32 contains_pattern = 0;
                for(u64 k = 0; k + next_pattern.size <= name.size; k += 1)
                {
                  if(MemoryCompare(name.str + k, next_pattern.str, next_pattern.size) == 0)
                  {
                    contains_pattern = 1;
                    break;
                  }
                }
                if(contains_pattern)
                {
                  String8 next_file_path = str8_chop_last_slash(wp->file_path);
                  next_file_path = str8f(scratch.arena, "%S/%S", next_file_path, name);

                  if(temp_count >= max_count * 2) { break; }
                  temp_entries[temp_count].file_path            = next_file_path;
                  temp_entries[temp_count].position_seconds     = 0.0;
                  temp_entries[temp_count].last_watched_timestamp = wp->last_watched_timestamp;
                  temp_entries[temp_count].is_next_episode      = 1;
                  temp_entries[temp_count].display_name         = name;
                  temp_count += 1;
                  break;
                }
              }
              closedir(d);
            }
          }
        }
      }
    }
  }

  // Sort by last watched timestamp (most recent first)
  for(u64 i = 0; i < temp_count; i += 1)
  {
    for(u64 j = i + 1; j < temp_count; j += 1)
    {
      if(temp_entries[j].last_watched_timestamp > temp_entries[i].last_watched_timestamp)
      {
        ContinueWatchingEntry temp = temp_entries[i];
        temp_entries[i] = temp_entries[j];
        temp_entries[j] = temp;
      }
    }
  }

  // Remove duplicates (prefer in-progress over next episode)
  u64 unique_count = 0;
  for(u64 i = 0; i < temp_count; i += 1)
  {
    b32 is_duplicate = 0;
    for(u64 j = 0; j < unique_count; j += 1)
    {
      if(str8_match(temp_entries[i].file_path, temp_entries[j].file_path, 0))
      {
        is_duplicate = 1;
        break;
      }
    }
    if(!is_duplicate && unique_count < max_count)
    {
      temp_entries[unique_count] = temp_entries[i];
      unique_count += 1;
    }
  }

  // Copy to permanent storage
  state->continue_watching = push_array(state->perm_arena, ContinueWatchingEntry, unique_count);
  state->continue_watching_count = unique_count;

  for(u64 i = 0; i < unique_count; i += 1)
  {
    state->continue_watching[i].file_path            = str8_copy(state->perm_arena, temp_entries[i].file_path);
    state->continue_watching[i].position_seconds     = temp_entries[i].position_seconds;
    state->continue_watching[i].last_watched_timestamp = temp_entries[i].last_watched_timestamp;
    state->continue_watching[i].is_next_episode      = temp_entries[i].is_next_episode;
    state->continue_watching[i].display_name         = str8_copy(state->perm_arena, temp_entries[i].display_name);
  }

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

  for(u64 i = 0; i < Min(fa->name.size, fb->name.size); i += 1)
  {
    u8 ca = fa->name.str[i];
    u8 cb = fb->name.str[i];
    if(ca >= 'A' && ca <= 'Z') { ca += 32; }
    if(cb >= 'A' && cb <= 'Z') { cb += 32; }
    if(ca < cb) { return -1; }
    if(ca > cb) { return 1; }
  }

  if(fa->name.size < fb->name.size) { return -1; }
  if(fa->name.size > fb->name.size) { return 1; }
  return 0;
}

internal void
load_directory(PlayerState *state, String8 dir_path)
{
  Temp scratch = scratch_begin(&state->perm_arena, 1);

  String8 full_path = dir_path.size > 0
    ? str8f(scratch.arena, "%S/%S", g_media_root_path, dir_path)
    : g_media_root_path;

  log_infof("media-player: loading directory: %S\n", full_path);

  char *dir_cstr = (char *)push_array(scratch.arena, u8, full_path.size + 1);
  MemoryCopy(dir_cstr, full_path.str, full_path.size);
  dir_cstr[full_path.size] = 0;

  DIR *d = opendir(dir_cstr);
  if(d == 0)
  {
    log_errorf("media-player: failed to open directory: %S\n", full_path);
    state->file_count = 0;
    state->files = 0;
    scratch_end(scratch);
    return;
  }

  FileEntry *temp_files = push_array(scratch.arena, FileEntry, 1024);
  u64 temp_count = 0;

  struct dirent *entry;
  for(; (entry = readdir(d)) != 0; )
  {
    String8 name = str8_cstring(entry->d_name);
    name = str8_copy(scratch.arena, name);  // Copy immediately - d_name is only valid during readdir
    if(str8_match(name, str8_lit("."), 0)) { continue; }
    if(str8_match(name, str8_lit(".."), 0)) { continue; }

    if(temp_count >= 1024) { break; }

    String8 entry_path = str8f(scratch.arena, "%s/%S", dir_cstr, name);
    char *entry_cstr = (char *)push_array(scratch.arena, u8, entry_path.size + 1);
    MemoryCopy(entry_cstr, entry_path.str, entry_path.size);
    entry_cstr[entry_path.size] = 0;

    struct stat st;
    if(stat(entry_cstr, &st) != 0) { continue; }

    b32 is_dir = S_ISDIR(st.st_mode);
    u64 size = st.st_size;

    String8 rel_path = dir_path.size > 0
      ? str8f(scratch.arena, "%S/%S", dir_path, name)
      : name;

    WatchProgress *wp = watch_progress_find(g_watch_progress, rel_path);

    temp_files[temp_count].name              = name;
    temp_files[temp_count].is_directory      = is_dir;
    temp_files[temp_count].size              = size;
    temp_files[temp_count].last_watched_pos  = wp ? wp->position_seconds : -1.0;
    temp_files[temp_count].duration          = wp ? wp->duration_seconds : 0.0;
    temp_files[temp_count].last_watched_time = wp ? wp->last_watched_timestamp : 0;
    temp_files[temp_count].is_watched        = is_watched(wp);
    temp_count += 1;
  }

  closedir(d);

  qsort(temp_files, temp_count, sizeof(FileEntry), compare_file_entries);

  state->files = push_array(state->perm_arena, FileEntry, temp_count);
  state->file_count = temp_count;
  for(u64 i = 0; i < temp_count; i += 1)
  {
    state->files[i].name              = str8_copy(state->perm_arena, temp_files[i].name);
    state->files[i].is_directory      = temp_files[i].is_directory;
    state->files[i].size              = temp_files[i].size;
    state->files[i].last_watched_pos  = temp_files[i].last_watched_pos;
    state->files[i].duration          = temp_files[i].duration;
    state->files[i].last_watched_time = temp_files[i].last_watched_time;
    state->files[i].is_watched        = temp_files[i].is_watched;
  }

  state->current_dir = str8_copy(state->perm_arena, dir_path);
  state->selected_index = 0;
  state->scroll_offset = 0;

  log_infof("media-player: loaded %lu entries\n", state->file_count);

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
  String8 cmd = str8f(scratch.arena, "{\"command\":[\"get_property\",\"%S\"],\"request_id\":%llu}",
                      property, mpv->request_id);
  mpv_ipc_send_command(mpv, cmd);

  // Read response (simple blocking read with timeout)
  for(int retry = 0; retry < 10; retry += 1)
  {
    os_sleep_milliseconds(10);
    String8 response = mpv_ipc_read_response(scratch.arena, mpv->socket_fd);
    if(response.size == 0) { continue; }

    // Log response for debugging (only first time per property)
    static u64 log_count = 0;
    if(log_count < 2)
    {
      log_infof("media-player: mpv IPC response for %S: %S\n", property, response);
      log_count += 1;
    }

    // Very simple JSON parsing - just look for "data":<number>
    String8 needle = str8_lit("\"data\":");
    u64 data_pos = str8_find_needle(response, 0, needle, 0);
    if(data_pos < response.size)
    {
      String8 after_data = str8_skip(response, data_pos + needle.size);
      f64 value = f64_from_str8(after_data);
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

  MPVClient *mpv = push_array(arena, MPVClient, 1);
  MemoryZeroStruct(mpv);
  mpv->arena      = arena;
  mpv->request_id = 0;
  mpv->socket_fd  = -1;

  // Create unique IPC socket path
  u64 timestamp = os_now_microseconds();
  mpv->ipc_path = str8f(arena, "/tmp/mpv-ipc-%llu.sock", timestamp);

  char *file_cstr = (char *)push_array(scratch.arena, u8, file_path.size + 1);
  MemoryCopy(file_cstr, file_path.str, file_path.size);
  file_cstr[file_path.size] = 0;

  char *ipc_cstr = (char *)push_array(scratch.arena, u8, mpv->ipc_path.size + 1);
  MemoryCopy(ipc_cstr, mpv->ipc_path.str, mpv->ipc_path.size);
  ipc_cstr[mpv->ipc_path.size] = 0;

  // Fork and exec mpv
  int pid = fork();
  if(pid == 0)
  {
    // Child process - exec mpv
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

    // If exec fails
    exit(1);
  }
  else if(pid > 0)
  {
    mpv->child_pid = pid;
    mpv->running   = 1;

    log_infof("media-player: launched mpv (pid=%d) with IPC socket: %S\n", pid, mpv->ipc_path);

    // Wait for IPC socket to be created
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
          // Set non-blocking
          int flags = fcntl(mpv->socket_fd, F_GETFL, 0);
          fcntl(mpv->socket_fd, F_SETFL, flags | O_NONBLOCK);

          log_infof("media-player: connected to mpv IPC socket\n");

          // Enable property observation for time-pos and duration
          mpv_ipc_send_command(mpv, str8_lit("{\"command\":[\"observe_property\",1,\"time-pos\"]}"));
          mpv_ipc_send_command(mpv, str8_lit("{\"command\":[\"observe_property\",2,\"duration\"]}"));
          log_infof("media-player: enabled property observation for time-pos and duration\n");

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
  if(mpv == 0) { return; }

  // Send quit command
  mpv_ipc_send_command(mpv, str8_lit("{\"command\":[\"quit\"]}"));

  // Wait for process to exit
  if(mpv->child_pid > 0)
  {
    for(int retry = 0; retry < 20; retry += 1)
    {
      int status;
      int result = waitpid(mpv->child_pid, &status, WNOHANG);
      if(result != 0) { break; }
      os_sleep_milliseconds(50);
    }

    // Force kill if still running
    kill(mpv->child_pid, SIGTERM);
  }

  if(mpv->socket_fd >= 0) { close(mpv->socket_fd); }

  // Clean up IPC socket file
  Temp scratch = scratch_begin(&mpv->arena, 1);
  char *ipc_cstr = (char *)push_array(scratch.arena, u8, mpv->ipc_path.size + 1);
  MemoryCopy(ipc_cstr, mpv->ipc_path.str, mpv->ipc_path.size);
  ipc_cstr[mpv->ipc_path.size] = 0;
  unlink(ipc_cstr);
  scratch_end(scratch);

  mpv->running = 0;
  log_info(str8_lit("media-player: mpv stopped\n"));
}

////////////////////////////////
//~ Terminal UI (ncurses replacement for SDL)

internal Terminal *
terminal_init(Arena *arena)
{
  Terminal *term = push_array(arena, Terminal, 1);

  setlocale(LC_ALL, "");  // Enable UTF-8 support

  term->win = initscr();
  if(term->win == 0)
  {
    return 0;
  }

  cbreak();              // Disable line buffering
  noecho();              // Don't echo input
  keypad(stdscr, TRUE);  // Enable arrow keys
  curs_set(0);           // Hide cursor
  timeout(16);           // Non-blocking with 16ms timeout (~60fps)

  getmaxyx(stdscr, term->height, term->width);

  // Enable colors
  if(has_colors())
  {
    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLACK);   // Normal
    init_pair(2, COLOR_BLACK, COLOR_WHITE);   // Selected
    init_pair(3, COLOR_GREEN, COLOR_BLACK);   // In-progress
    init_pair(4, COLOR_CYAN, COLOR_BLACK);    // Dim
  }

  log_info(str8_lit("media-player: terminal UI initialized\n"));
  return term;
}

internal void
terminal_shutdown(Terminal *term)
{
  if(term == 0) { return; }
  endwin();
}

internal void
render_home_ncurses(Terminal *term, PlayerState *state)
{
  clear();

  int y = 0;
  u64 selected = state->selected_index;

  // Header
  attron(A_BOLD);
  mvprintw(y++, 0, "Home");
  attroff(A_BOLD);
  y++;

  // Categories
  if(selected == 0) attron(COLOR_PAIR(2));
  mvprintw(y++, 2, "> Shows");
  if(selected == 0) attroff(COLOR_PAIR(2));

  if(selected == 1) attron(COLOR_PAIR(2));
  mvprintw(y++, 2, "> Movies");
  if(selected == 1) attroff(COLOR_PAIR(2));
  y++;

  // Continue Watching
  if(state->continue_watching_count > 0)
  {
    attron(COLOR_PAIR(4));
    mvprintw(y++, 0, "Continue Watching:");
    attroff(COLOR_PAIR(4));

    for(u64 i = 0; i < state->continue_watching_count; i++)
    {
      ContinueWatchingEntry *entry = &state->continue_watching[i];

      if(selected == (2 + i)) attron(COLOR_PAIR(2));
      else attron(COLOR_PAIR(3));

      if(entry->is_next_episode)
      {
        mvprintw(y++, 2, "> Next: %.*s", (int)entry->display_name.size, entry->display_name.str);
      }
      else
      {
        int mins = (int)(entry->position_seconds / 60);
        int secs = (int)(entry->position_seconds) % 60;
        mvprintw(y++, 2, "* %.*s [%02d:%02d]", (int)entry->display_name.size, entry->display_name.str, mins, secs);
      }

      if(selected == (2 + i)) attroff(COLOR_PAIR(2));
      else attroff(COLOR_PAIR(3));
    }
  }

  // Controls
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

  // Header
  attron(A_BOLD);
  mvprintw(y++, 0, "%.*s", (int)header.size, header.str);
  attroff(A_BOLD);
  y++;

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

    for(u64 i = start; i < end; i++)
    {
      FileEntry *entry = &state->files[i];
      b32 selected = (i == state->selected_index);

      if(selected) attron(COLOR_PAIR(2));

      if(entry->is_directory)
      {
        mvprintw(y++, 2, "> %.*s/", (int)entry->name.size, entry->name.str);
      }
      else
      {
        if(entry->is_watched)
        {
          attron(COLOR_PAIR(4));
          mvprintw(y++, 2, "✓ %.*s", (int)entry->name.size, entry->name.str);
          attroff(COLOR_PAIR(4));
        }
        else if(entry->last_watched_pos > 0.0)
        {
          int mins = (int)(entry->last_watched_pos / 60);
          int secs = (int)(entry->last_watched_pos) % 60;
          attron(COLOR_PAIR(3));
          mvprintw(y++, 2, "* %.*s [%02d:%02d]", (int)entry->name.size, entry->name.str, mins, secs);
          attroff(COLOR_PAIR(3));
        }
        else
        {
          mvprintw(y++, 2, "  %.*s", (int)entry->name.size, entry->name.str);
        }
      }

      if(selected) attroff(COLOR_PAIR(2));
    }
  }

  // Info
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
    u64 max_index = 2 + state->continue_watching_count - 1;

    if(ch == KEY_DOWN)
    {
      if(state->selected_index < max_index)
      {
        state->selected_index += 1;
      }
    }
    else if(ch == KEY_UP)
    {
      if(state->selected_index > 0)
      {
        state->selected_index -= 1;
      }
    }
    else if(ch == '\n' || ch == 10)
    {
      if(state->selected_index == 0)
      {
        state->mode = PlayerMode_Shows;
        load_directory(state, str8_lit("shows"));
      }
      else if(state->selected_index == 1)
      {
        state->mode = PlayerMode_Movies;
        load_directory(state, str8_lit("movies"));
      }
      else if(state->selected_index >= 2)
      {
        u64 cw_idx = state->selected_index - 2;
        if(cw_idx < state->continue_watching_count)
        {
          start_playback(state, state->continue_watching[cw_idx].file_path);
        }
      }
    }
  }
  else if(state->mode == PlayerMode_Shows || state->mode == PlayerMode_Movies)
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
      if(entry->is_directory)
      {
        String8 new_dir = state->current_dir.size > 0
          ? str8f(state->perm_arena, "%S/%S", state->current_dir, entry->name)
          : str8_copy(state->perm_arena, entry->name);
        load_directory(state, new_dir);
      }
      else
      {
        String8 file_path = state->current_dir.size > 0
          ? str8f(state->perm_arena, "%S/%S", state->current_dir, entry->name)
          : str8_copy(state->perm_arena, entry->name);
        start_playback(state, file_path);
      }
    }
    else if(ch == 'w' || ch == 'W')
    {
      if(state->file_count == 0) { return; }

      FileEntry *entry = &state->files[state->selected_index];
      if(!entry->is_directory)
      {
        String8 file_path = state->current_dir.size > 0
          ? str8f(state->perm_arena, "%S/%S", state->current_dir, entry->name)
          : str8_copy(state->perm_arena, entry->name);

        watch_progress_mark_watched(g_watch_progress, file_path);

        Temp scratch = scratch_begin(&state->perm_arena, 1);
        String8 state_file = str8f(scratch.arena, "%S/watch-state.txt", g_state_dir_path);
        watch_progress_save(g_watch_progress, state_file);
        scratch_end(scratch);

        state->continue_watching_dirty = 1; // Mark for refresh
        load_directory(state, state->current_dir);
      }
    }
    else if(ch == KEY_BACKSPACE || ch == 127)
    {
      String8 category = state->mode == PlayerMode_Shows ? str8_lit("shows") : str8_lit("movies");

      if(str8_match(state->current_dir, category, 0))
      {
        state->mode = PlayerMode_Home;
        load_continue_watching(state, g_watch_progress, 10);
        state->selected_index = 0;
      }
      else if(state->current_dir.size > 0)
      {
        String8 parent = str8_chop_last_slash(state->current_dir);

        if(parent.size == 0 || str8_match(parent, category, 0))
        {
          load_directory(state, category);
        }
        else
        {
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

  // Load saved watch state
  WatchProgress *wp = watch_progress_find(g_watch_progress, file_path);
  f64 start_pos = (wp && wp->position_seconds > 1.0) ? wp->position_seconds : 0.0;

  String8 full_path = str8f(scratch.arena, "%S/%S", g_media_root_path, file_path);
  log_infof("media-player: starting playback: %S (resume from %.1fs)\n", full_path, start_pos);

  state->mpv = mpv_start(state->perm_arena, full_path, start_pos);
  if(state->mpv == 0)
  {
    log_errorf("media-player: failed to start mpv\n");
    scratch_end(scratch);
    return 0;
  }

  state->current_file        = str8_copy(state->perm_arena, file_path);
  state->last_poll_time      = os_now_microseconds();
  state->last_position_save  = os_now_microseconds();
  state->last_known_position = start_pos;
  state->last_known_duration = 0.0;
  state->mode                = PlayerMode_Playing;

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
      watch_progress_update(g_watch_progress, state->current_file, state->last_known_position, state->last_known_duration);
      String8 state_file = str8f(scratch.arena, "%S/watch-state.txt", g_state_dir_path);
      watch_progress_save(g_watch_progress, state_file);
      scratch_end(scratch);
    }

    mpv_stop(state->mpv);
    state->mpv = 0;
  }

  state->current_file            = str8_zero();
  state->mode                    = PlayerMode_Home;
  state->selected_index          = 0;
  state->needs_render            = 1;
  state->continue_watching_dirty = 1; // Refresh Continue Watching
}

internal void
update_playback(PlayerState *state)
{
  if(state->mpv == 0) { return; }

  b32 is_running = mpv_is_running(state->mpv);
  if(!is_running)
  {
    stop_playback(state);
    return;
  }

  // Read and process property-change events from mpv (event-driven, not polling)
  Temp scratch = scratch_begin(&state->perm_arena, 1);
  String8 response = mpv_ipc_read_response(scratch.arena, state->mpv->socket_fd);
  if(response.size > 0)
  {
    // Parse multiple JSON events separated by newlines
    u64 offset = 0;
    while(offset < response.size)
    {
      u64 newline = str8_find_needle(response, offset, str8_lit("\n"), 0);
      u64 end = (newline < response.size) ? newline : response.size;
      String8 line = str8_substr(response, rng_1u64(offset, end));

      // Look for property-change events: {"event":"property-change","id":1,"data":123.45}
      if(str8_find_needle(line, 0, str8_lit("\"property-change\""), 0) < line.size)
      {
        // Check if it's time-pos (id:1) or duration (id:2)
        b32 is_time_pos = str8_find_needle(line, 0, str8_lit("\"id\":1"), 0) < line.size;
        b32 is_duration = str8_find_needle(line, 0, str8_lit("\"id\":2"), 0) < line.size;

        if(is_time_pos || is_duration)
        {
          // Extract "data":<number>
          u64 data_pos = str8_find_needle(line, 0, str8_lit("\"data\":"), 0);
          if(data_pos < line.size)
          {
            String8 after_data = str8_skip(line, data_pos + 7);
            f64 value = f64_from_str8(after_data);

            if(is_time_pos && value > 0.0)
            {
              state->last_known_position = value;
            }
            else if(is_duration && value > 0.0)
            {
              state->last_known_duration = value;
            }
          }
        }
      }

      offset = (newline < response.size) ? newline + 1 : response.size;
    }
  }
  scratch_end(scratch);

  // Save progress every 5 seconds
  u64 now = os_now_microseconds();
  if(now - state->last_position_save > Million(5))
  {
    Temp scratch2 = scratch_begin(&state->perm_arena, 1);
    watch_progress_update(g_watch_progress, state->current_file, state->last_known_position, state->last_known_duration);
    String8 state_file = str8f(scratch2.arena, "%S/watch-state.txt", g_state_dir_path);
    watch_progress_save(g_watch_progress, state_file);
    scratch_end(scratch2);

    state->last_position_save = now;
    state->continue_watching_dirty = 1; // Mark for refresh
  }
}

////////////////////////////////
//~ Main Loop

internal void
main_loop(PlayerState *state)
{
  state->needs_render = 1; // Initial render
  state->continue_watching_dirty = 1; // Initial load

  while(!state->quit)
  {
    // Handle input (ncurses with 16ms timeout)
    int ch = getch();
    if(ch != ERR && IS_BROWSE_MODE(state->mode))
    {
      if(ch == 27) // ESC
      {
        if(state->mode == PlayerMode_Home)
        {
          state->quit = 1;
        }
        else
        {
          state->mode = PlayerMode_Home;
          state->needs_render = 1;
        }
      }
      else
      {
        handle_browser_input_ncurses(state, ch);
        state->needs_render = 1;
      }
    }

    // Update playback
    if(state->mode == PlayerMode_Playing)
    {
      update_playback(state);
    }

    // Reload Continue Watching if dirty (cached when clean)
    if(state->mode == PlayerMode_Home && state->continue_watching_dirty)
    {
      load_continue_watching(state, g_watch_progress, 10);
      state->continue_watching_dirty = 0;
      state->needs_render = 1;
    }

    // Render only when state changes (eliminates flicker)
    if(IS_BROWSE_MODE(state->mode) && state->needs_render)
    {
      if(state->mode == PlayerMode_Home)
      {
        render_home_ncurses(state->term, state);
      }
      else if(state->mode == PlayerMode_Shows)
      {
        String8 header = state->current_dir.size > 0
          ? state->current_dir
          : str8_lit("Shows");
        render_browser_ncurses(state->term, state, header);
      }
      else if(state->mode == PlayerMode_Movies)
      {
        String8 header = state->current_dir.size > 0
          ? state->current_dir
          : str8_lit("Movies");
        render_browser_ncurses(state->term, state, header);
      }

      state->needs_render = 0;
    }
  }
}

////////////////////////////////
//~ Exit/Signal Handlers

internal void
atexit_handler(void)
{
}

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
  // Install exit and signal handlers to debug unexpected termination
  atexit(atexit_handler);
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

  String8 media_root = cmd_line_string(cmd_line, str8_lit("media-root"));
  String8 state_dir  = cmd_line_string(cmd_line, str8_lit("state-dir"));
  b32 windowed       = cmd_line_has_flag(cmd_line, str8_lit("windowed"));

  if(media_root.size == 0) { media_root = str8_lit("/home/mpd/n/media"); }
  if(state_dir.size == 0)  { state_dir = str8_lit("/home/mpd/.local/state/media-player"); }

  if(media_root.size > 0 && media_root.str[media_root.size - 1] == '/')
  {
    media_root = str8_chop_last_slash(media_root);
  }

  g_media_root_path = media_root;
  g_state_dir_path  = state_dir;

  log_infof("media-player: media root: %S\n", g_media_root_path);
  log_infof("media-player: state dir: %S\n", g_state_dir_path);

  Arena *perm_arena = arena_alloc();

  // Create state directory if it doesn't exist
  OS_Handle state_dir_handle = os_file_open(OS_AccessFlag_Read, g_state_dir_path);
  if(state_dir_handle.u64[0] == 0)
  {
    log_infof("media-player: creating state directory: %S\n", g_state_dir_path);
    b32 created = os_make_directory(g_state_dir_path);
    if(!created)
    {
      log_errorf("media-player: failed to create state directory\n");
    }
  }
  else
  {
    os_file_close(state_dir_handle);
  }

  // Initialize watch progress table
  g_watch_progress = watch_progress_table_alloc(perm_arena, 256);
  String8 state_file = str8f(perm_arena, "%S/watch-state.txt", g_state_dir_path);
  watch_progress_load(g_watch_progress, state_file);

  Terminal *term = terminal_init(perm_arena);
  if(term == 0)
  {
    log_scope_flush(scratch.arena);
    scratch_end(scratch);
    return;
  }

  PlayerState state = {0};
  state.perm_arena = perm_arena;
  state.term = term;
  state.mode = PlayerMode_Home;
  state.quit = 0;

  load_continue_watching(&state, g_watch_progress, 10);

  main_loop(&state);

  if(state.mpv != 0)
  {
    stop_playback(&state);
  }

  terminal_shutdown(state.term);

  log_scope_flush(scratch.arena);
  scratch_end(scratch);
}
