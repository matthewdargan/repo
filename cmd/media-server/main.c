#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>

#include "base/inc.h"
#include "http/inc.h"
#include "base/inc.c"
#include "http/inc.c"

#include <dirent.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>

////////////////////////////////
//~ Types

typedef struct DashCache DashCache;
struct DashCache
{
  Arena *arena;
  String8 file_path;
  String8 cache_dir;
  u64 duration_us;
  u64 last_access_us;
  DashCache *hash_next;
  DashCache *lru_next;
  DashCache *lru_prev;
};

typedef struct DashCacheTable DashCacheTable;
struct DashCacheTable
{
  Mutex mutex;
  DashCache **hash_table;
  u64 hash_table_size;
  String8 cache_root;
  DashCache *lru_head;
  DashCache *lru_tail;
  u64 cache_count;
  u64 max_cache_count;
};

////////////////////////////////
//~ Globals

global DashCacheTable *dash_cache = 0;
global String8 media_root_path = {0};
global String8 cache_root_path = {0};
global WP_Pool *worker_pool = 0;

////////////////////////////////
//~ Helpers

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

internal void
socket_write_all(OS_Handle socket, String8 data)
{
  int fd = (int)socket.u64[0];
  for(u64 written = 0; written < data.size;)
  {
    ssize_t result = write(fd, data.str + written, data.size - written);
    if(result > 0) { written += result; }
    else if(errno == EINTR) { continue; }
    else { break; }
  }
}

internal void
send_error_response(OS_Handle socket, HTTP_Status status, String8 message)
{
  Temp scratch = scratch_begin(0, 0);
  HTTP_Response *res = http_response_alloc(scratch.arena, status);
  http_header_add(scratch.arena, &res->headers, str8_lit("Content-Type"), str8_lit("text/plain"));
  http_header_add(scratch.arena, &res->headers, str8_lit("Connection"), str8_lit("close"));
  res->body = (message.size > 0) ? message : res->status_text;
  http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"),
                  str8_from_u64(scratch.arena, res->body.size, 10, 0, 0));
  socket_write_all(socket, http_response_serialize(scratch.arena, res));
  scratch_end(scratch);
}

////////////////////////////////
//~ Forward Declarations

internal void postprocess_manifest(String8 cache_dir);
internal void cache_lru_touch(DashCache *cache);
internal void cache_lru_evict_if_needed(void);

////////////////////////////////
//~ LRU Cache Management

internal void
cache_lru_remove(DashCache *cache)
{
  if(cache->lru_prev) cache->lru_prev->lru_next = cache->lru_next;
  else dash_cache->lru_head = cache->lru_next;

  if(cache->lru_next) cache->lru_next->lru_prev = cache->lru_prev;
  else dash_cache->lru_tail = cache->lru_prev;

  cache->lru_prev = 0;
  cache->lru_next = 0;
}

internal void
cache_lru_add_to_head(DashCache *cache)
{
  cache->lru_next = dash_cache->lru_head;
  cache->lru_prev = 0;

  if(dash_cache->lru_head) dash_cache->lru_head->lru_prev = cache;
  else dash_cache->lru_tail = cache;

  dash_cache->lru_head = cache;
}

internal void
cache_lru_touch(DashCache *cache)
{
  if(cache == dash_cache->lru_head) return;
  cache_lru_remove(cache);
  cache_lru_add_to_head(cache);
}

internal void
cache_lru_evict_if_needed(void)
{
  while(dash_cache->cache_count >= dash_cache->max_cache_count && dash_cache->lru_tail)
  {
    DashCache *evict = dash_cache->lru_tail;
    u64 hash = hash_string(evict->file_path);
    u64 bucket = hash % dash_cache->hash_table_size;

    DashCache **prev_ptr = &dash_cache->hash_table[bucket];
    for(DashCache *c = *prev_ptr; c != 0; c = c->hash_next)
    {
      if(c == evict)
      {
        *prev_ptr = c->hash_next;
        break;
      }
      prev_ptr = &c->hash_next;
    }

    cache_lru_remove(evict);
    dash_cache->cache_count -= 1;

    log_infof("media-server: LRU evicted %S (cache: %llu/%llu)\n",
              evict->file_path, dash_cache->cache_count, dash_cache->max_cache_count);

    arena_release(evict->arena);
  }
}

////////////////////////////////
//~ Media Inspection (libav)

internal u64
probe_duration(Arena *arena, String8 file_path)
{
  Temp scratch = scratch_begin(&arena, 1);
  char *path_cstr = (char *)push_array(scratch.arena, u8, file_path.size + 1);
  MemoryCopy(path_cstr, file_path.str, file_path.size);
  path_cstr[file_path.size] = 0;

  AVFormatContext *fmt_ctx = NULL;
  if(avformat_open_input(&fmt_ctx, path_cstr, NULL, NULL) < 0)
  {
    scratch_end(scratch);
    return 0;
  }

  if(avformat_find_stream_info(fmt_ctx, NULL) < 0)
  {
    avformat_close_input(&fmt_ctx);
    scratch_end(scratch);
    return 0;
  }

  u64 duration_us = (u64)fmt_ctx->duration;
  f64 duration_seconds = (f64)duration_us / 1000000.0;

  log_infof("media-server: detected duration %.2fs (%lluµs) for %S\n",
            duration_seconds, duration_us, file_path);

  avformat_close_input(&fmt_ctx);
  scratch_end(scratch);
  return duration_us;
}

internal void
extract_subtitle_stream(String8 input_path, u32 stream_idx, String8 lang, String8 output_path)
{
  Temp temp = scratch_begin(0, 0);

  char in_cstr[4096];
  MemoryCopy(in_cstr, input_path.str, Min(input_path.size, sizeof(in_cstr) - 1));
  in_cstr[Min(input_path.size, sizeof(in_cstr) - 1)] = 0;

  char out_cstr[4096];
  MemoryCopy(out_cstr, output_path.str, Min(output_path.size, sizeof(out_cstr) - 1));
  out_cstr[Min(output_path.size, sizeof(out_cstr) - 1)] = 0;

  AVFormatContext *in_ctx = NULL;
  AVCodecContext *dec_ctx = NULL;
  AVFormatContext *out_ctx = NULL;
  const AVCodec *enc = NULL;
  AVCodecContext *enc_ctx = NULL;

  if(avformat_open_input(&in_ctx, in_cstr, NULL, NULL) != 0) goto cleanup;
  if(avformat_find_stream_info(in_ctx, NULL) != 0) goto cleanup;

  AVStream *in_stream = in_ctx->streams[stream_idx];

  const AVCodec *dec = avcodec_find_decoder(in_stream->codecpar->codec_id);
  if(!dec) goto cleanup;

  dec_ctx = avcodec_alloc_context3(dec);
  if(!dec_ctx) goto cleanup;

  if(avcodec_parameters_to_context(dec_ctx, in_stream->codecpar) != 0) goto cleanup;
  if(avcodec_open2(dec_ctx, dec, NULL) != 0) goto cleanup;

  enc = avcodec_find_encoder_by_name("webvtt");
  if(!enc) goto cleanup;

  enc_ctx = avcodec_alloc_context3(enc);
  if(!enc_ctx) goto cleanup;

  enc_ctx->time_base = in_stream->time_base;

  const char *webvtt_header = "WEBVTT\n\n";
  enc_ctx->subtitle_header_size = strlen(webvtt_header);
  enc_ctx->subtitle_header = av_malloc(enc_ctx->subtitle_header_size + 1);
  if(enc_ctx->subtitle_header)
  {
    MemoryCopy(enc_ctx->subtitle_header, webvtt_header, enc_ctx->subtitle_header_size + 1);
  }

  if(avcodec_open2(enc_ctx, enc, NULL) != 0) goto cleanup;

  if(avformat_alloc_output_context2(&out_ctx, NULL, "webvtt", out_cstr) != 0) goto cleanup;

  AVStream *out_stream = avformat_new_stream(out_ctx, NULL);
  if(!out_stream) goto cleanup;
  if(avcodec_parameters_from_context(out_stream->codecpar, enc_ctx) != 0) goto cleanup;
  out_stream->time_base = in_stream->time_base;

  if(avio_open(&out_ctx->pb, out_cstr, AVIO_FLAG_WRITE) != 0) goto cleanup;
  if(avformat_write_header(out_ctx, NULL) != 0) goto cleanup;

  AVPacket *pkt = av_packet_alloc();
  AVPacket *enc_pkt = av_packet_alloc();
  AVSubtitle sub;

  while(av_read_frame(in_ctx, pkt) >= 0)
  {
    if(pkt->stream_index == (int)stream_idx)
    {
      int got_sub = 0;
      if(avcodec_decode_subtitle2(dec_ctx, &sub, &got_sub, pkt) >= 0 && got_sub)
      {
        b32 has_ass_subtitle = 0;
        for(u32 i = 0; i < sub.num_rects; i++)
        {
          if(sub.rects[i]->type == SUBTITLE_ASS)
          {
            has_ass_subtitle = 1;
            break;
          }
        }

        if(has_ass_subtitle)
        {
          u8 buf[4096];
          int size = avcodec_encode_subtitle(enc_ctx, buf, sizeof(buf), &sub);
          if(size > 0)
          {
            enc_pkt->data = buf;
            enc_pkt->size = size;
            enc_pkt->pts = pkt->pts;
            enc_pkt->dts = pkt->dts;
            enc_pkt->duration = pkt->duration;
            enc_pkt->stream_index = 0;
            av_interleaved_write_frame(out_ctx, enc_pkt);
          }
        }
        avsubtitle_free(&sub);
      }
    }
    av_packet_unref(pkt);
  }

  av_packet_free(&enc_pkt);
  av_packet_free(&pkt);
  av_write_trailer(out_ctx);

  log_infof("media-server: extracted subtitle stream %u (lang: %S)\n", stream_idx, lang);

cleanup:
  if(out_ctx)
  {
    if(out_ctx->pb) avio_closep(&out_ctx->pb);
    avformat_free_context(out_ctx);
  }
  if(enc_ctx) avcodec_free_context(&enc_ctx);
  if(dec_ctx) avcodec_free_context(&dec_ctx);
  if(in_ctx) avformat_close_input(&in_ctx);
  scratch_end(temp);
}

internal void
extract_subtitles(String8 file_path, String8 cache_dir)
{
  Temp temp = scratch_begin(0, 0);

  char path_cstr[4096];
  MemoryCopy(path_cstr, file_path.str, Min(file_path.size, sizeof(path_cstr) - 1));
  path_cstr[Min(file_path.size, sizeof(path_cstr) - 1)] = 0;

  AVFormatContext *fmt = NULL;
  if(avformat_open_input(&fmt, path_cstr, NULL, NULL) != 0)
  {
    scratch_end(temp);
    return;
  }

  if(avformat_find_stream_info(fmt, NULL) != 0)
  {
    avformat_close_input(&fmt);
    scratch_end(temp);
    return;
  }

  for(u32 i = 0; i < fmt->nb_streams; i++)
  {
    if(fmt->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) continue;

    if(fmt->streams[i]->codecpar->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE ||
       fmt->streams[i]->codecpar->codec_id == AV_CODEC_ID_DVD_SUBTITLE ||
       fmt->streams[i]->codecpar->codec_id == AV_CODEC_ID_DVB_SUBTITLE)
    {
      continue;
    }

    String8 lang = str8_lit("und");
    AVDictionaryEntry *e = av_dict_get(fmt->streams[i]->metadata, "language", NULL, 0);
    if(e && e->value)
    {
      lang = str8_cstring(e->value);
    }

    String8 vtt_path = str8f(temp.arena, "%S/subtitle.%S.vtt", cache_dir, lang);
    extract_subtitle_stream(file_path, i, lang, vtt_path);
  }

  avformat_close_input(&fmt);
  scratch_end(temp);
}

////////////////////////////////
//~ DASH Generation (libav)

internal b32
generate_dash(String8 input_path, String8 output_path)
{
  Temp temp = scratch_begin(0, 0);

  char in_cstr[4096];
  MemoryCopy(in_cstr, input_path.str, Min(input_path.size, sizeof(in_cstr) - 1));
  in_cstr[Min(input_path.size, sizeof(in_cstr) - 1)] = 0;

  char out_cstr[4096];
  MemoryCopy(out_cstr, output_path.str, Min(output_path.size, sizeof(out_cstr) - 1));
  out_cstr[Min(output_path.size, sizeof(out_cstr) - 1)] = 0;

  AVFormatContext *in_ctx = NULL;
  AVFormatContext *out_ctx = NULL;
  b32 success = 0;

  if(avformat_open_input(&in_ctx, in_cstr, NULL, NULL) != 0) goto cleanup;
  if(avformat_find_stream_info(in_ctx, NULL) != 0) goto cleanup;

  if(avformat_alloc_output_context2(&out_ctx, NULL, "dash", out_cstr) != 0) goto cleanup;

  int *stream_map = push_array(temp.arena, int, in_ctx->nb_streams);
  for(u32 i = 0; i < in_ctx->nb_streams; i++) stream_map[i] = -1;

  u32 stream_idx = 0;
  for(u32 i = 0; i < in_ctx->nb_streams; i++)
  {
    AVStream *in_stream = in_ctx->streams[i];
    AVCodecParameters *codecpar = in_stream->codecpar;

    if(codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
       codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
    {
      continue;
    }

    AVStream *out_stream = avformat_new_stream(out_ctx, NULL);
    if(!out_stream) goto cleanup;

    if(avcodec_parameters_copy(out_stream->codecpar, codecpar) != 0) goto cleanup;
    out_stream->codecpar->codec_tag = 0;
    out_stream->time_base = in_stream->time_base;
    av_dict_copy(&out_stream->metadata, in_stream->metadata, 0);

    stream_map[i] = stream_idx++;
  }

  AVDictionary *opts = NULL;
  av_dict_set(&opts, "seg_duration", "4", 0);
  av_dict_set(&opts, "use_timeline", "1", 0);
  av_dict_set(&opts, "use_template", "1", 0);
  av_dict_set(&opts, "single_file", "0", 0);
  av_dict_set(&opts, "streaming", "0", 0);

  if(avformat_write_header(out_ctx, &opts) != 0)
  {
    av_dict_free(&opts);
    goto cleanup;
  }
  av_dict_free(&opts);

  AVPacket *pkt = av_packet_alloc();
  while(av_read_frame(in_ctx, pkt) >= 0)
  {
    if(stream_map[pkt->stream_index] >= 0)
    {
      AVStream *in_stream = in_ctx->streams[pkt->stream_index];
      AVStream *out_stream = out_ctx->streams[stream_map[pkt->stream_index]];

      pkt->stream_index = stream_map[pkt->stream_index];
      av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);
      pkt->pos = -1;

      if(av_interleaved_write_frame(out_ctx, pkt) != 0) break;
    }
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);

  av_write_trailer(out_ctx);
  success = 1;

cleanup:
  if(out_ctx)
  {
    if(!(out_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_ctx->pb);
    avformat_free_context(out_ctx);
  }
  if(in_ctx) avformat_close_input(&in_ctx);
  scratch_end(temp);
  return success;
}

internal DashCache *
start_dash_generation(String8 file_path, String8 cache_root)
{
  Arena *cache_arena = arena_alloc();
  Temp scratch = scratch_begin(&cache_arena, 1);
  u64 hash = hash_string(file_path);
  String8 cache_dir = str8f(cache_arena, "%S/%llu", cache_root, hash);

  char *cache_dir_cstr = (char *)push_array(scratch.arena, u8, cache_dir.size + 1);
  MemoryCopy(cache_dir_cstr, cache_dir.str, cache_dir.size);
  cache_dir_cstr[cache_dir.size] = 0;
  mkdir(cache_dir_cstr, 0755);

  u64 duration_us = probe_duration(cache_arena, file_path);
  extract_subtitles(file_path, cache_dir);

  String8 manifest_path = str8f(scratch.arena, "%S/manifest.mpd", cache_dir);

  log_infof("media-server: generating DASH for %S\n", file_path);

  if(!generate_dash(file_path, manifest_path))
  {
    log_errorf("media-server: DASH generation failed for %S\n", file_path);
    scratch_end(scratch);
    return 0;
  }

  log_infof("media-server: DASH generation complete for %S\n", file_path);

  postprocess_manifest(cache_dir);

  DashCache *cache = push_array(cache_arena, DashCache, 1);
  cache->arena = cache_arena;
  cache->file_path = str8_copy(cache_arena, file_path);
  cache->cache_dir = cache_dir;
  cache->duration_us = duration_us;
  cache->last_access_us = os_now_microseconds();
  cache->lru_next = 0;
  cache->lru_prev = 0;

  scratch_end(scratch);
  return cache;
}

internal DashCache *
get_or_create_cache(String8 file_path)
{
  u64 hash = hash_string(file_path);
  u64 bucket = hash % dash_cache->hash_table_size;
  DashCache *result = 0;

  MutexScope(dash_cache->mutex)
  {
    for(DashCache *c = dash_cache->hash_table[bucket]; c != 0; c = c->hash_next)
    {
      if(str8_match(c->file_path, file_path, 0))
      {
        c->last_access_us = os_now_microseconds();
        cache_lru_touch(c);
        result = c;
        break;
      }
    }

    if(result == 0)
    {
      cache_lru_evict_if_needed();

      result = start_dash_generation(file_path, dash_cache->cache_root);
      if(result != 0)
      {
        result->hash_next = dash_cache->hash_table[bucket];
        dash_cache->hash_table[bucket] = result;
        cache_lru_add_to_head(result);
        dash_cache->cache_count += 1;
      }
    }
  }

  return result;
}

internal String8
read_cache_file(Arena *arena, String8 cache_dir, String8 file_name)
{
  Temp scratch = scratch_begin(&arena, 1);
  String8 path = str8f(scratch.arena, "%S/%S", cache_dir, file_name);
  OS_FileProperties props = os_properties_from_file_path(path);

  if(props.size > 0)
  {
    OS_Handle file = os_file_open(OS_AccessFlag_Read, path);
    if(!os_handle_match(file, os_handle_zero()))
    {
      u8 *buffer = push_array(arena, u8, props.size);
      os_file_read(file, rng_1u64(0, props.size), buffer);
      os_file_close(file);
      scratch_end(scratch);
      return str8(buffer, props.size);
    }
  }

  scratch_end(scratch);
  return str8_zero();
}


////////////////////////////////
//~ Manifest Post-Processing

internal String8
str8_replace(Arena *arena, String8 str, String8 old, String8 new)
{
  u64 pos = str8_find_needle(str, 0, old, 0);
  if(pos >= str.size) return str;

  String8 before = str8_prefix(str, pos);
  String8 after = str8_skip(str, pos + old.size);
  String8List parts = {0};
  str8_list_push(arena, &parts, before);
  str8_list_push(arena, &parts, new);
  str8_list_push(arena, &parts, after);
  return str8_list_join(arena, parts, 0);
}

internal b32
line_contains_any(String8 line, String8 *patterns, u64 pattern_count)
{
  for(u64 i = 0; i < pattern_count; i++)
  {
    if(str8_find_needle(line, 0, patterns[i], 0) < line.size) return 1;
  }
  return 0;
}

internal void
postprocess_manifest(String8 cache_dir)
{
  Temp scratch = scratch_begin(0, 0);
  String8 manifest = read_cache_file(scratch.arena, cache_dir, str8_lit("manifest.mpd"));
  if(manifest.size == 0) { scratch_end(scratch); return; }

  String8 dynamic_attrs[] = {
    str8_lit("minimumUpdatePeriod"),
    str8_lit("suggestedPresentationDelay"),
    str8_lit("availabilityStartTime"),
    str8_lit("publishTime"),
  };

  String8List lines = {0};
  u64 pos = 0;
  for(;;)
  {
    u64 newline = str8_find_needle(manifest, pos, str8_lit("\n"), 0);
    if(newline >= manifest.size) break;

    String8 line = str8_substr(manifest, rng_1u64(pos, newline));
    if(!line_contains_any(line, dynamic_attrs, ArrayCount(dynamic_attrs)))
    {
      line = str8_replace(scratch.arena, line, str8_lit("type=\"dynamic\""), str8_lit("type=\"static\""));
      line = str8_replace(scratch.arena, line, str8_lit("codecs=\"hev1\""), str8_lit("codecs=\"hev1.1.6.L93.B0\""));
      line = str8_replace(scratch.arena, line, str8_lit("codecs=\"hvc1\""), str8_lit("codecs=\"hvc1.1.6.L93.B0\""));
      str8_list_push(scratch.arena, &lines, line);
      str8_list_push(scratch.arena, &lines, str8_lit("\n"));
    }
    pos = newline + 1;
  }

  String8 final = str8_list_join(scratch.arena, lines, 0);
  String8 manifest_path = str8f(scratch.arena, "%S/manifest.mpd", cache_dir);
  char *path_cstr = (char *)push_array(scratch.arena, u8, manifest_path.size + 1);
  MemoryCopy(path_cstr, manifest_path.str, manifest_path.size);
  path_cstr[manifest_path.size] = 0;

  int fd = open(path_cstr, O_WRONLY | O_TRUNC);
  if(fd >= 0)
  {
    write(fd, final.str, final.size);
    close(fd);
  }

  scratch_end(scratch);
}

////////////////////////////////
//~ HTTP Handlers

internal String8
inject_duration_into_manifest(Arena *arena, String8 manifest, u64 duration_us)
{
  if(str8_find_needle(manifest, 0, str8_lit("mediaPresentationDuration"), 0) < manifest.size)
  {
    return manifest;
  }

  u64 total_seconds = duration_us / 1000000;
  u64 hours = total_seconds / 3600;
  u64 minutes = (total_seconds % 3600) / 60;
  u64 seconds = total_seconds % 60;
  f64 fractional = (duration_us % 1000000) / 1000000.0;
  String8 duration_str = str8f(arena, "PT%lluH%lluM%.1fS", hours, minutes, (f64)seconds + fractional);

  u64 mpd_pos = str8_find_needle(manifest, 0, str8_lit("<MPD"), 0);
  if(mpd_pos >= manifest.size) return manifest;

  u64 tag_end = str8_find_needle(manifest, mpd_pos, str8_lit(">"), 0);
  if(tag_end >= manifest.size) return manifest;

  String8List parts = {0};
  str8_list_push(arena, &parts, str8_substr(manifest, rng_1u64(0, tag_end)));
  str8_list_push(arena, &parts, str8_lit("\n\tmediaPresentationDuration=\""));
  str8_list_push(arena, &parts, duration_str);
  str8_list_push(arena, &parts, str8_lit("\""));
  str8_list_push(arena, &parts, str8_skip(manifest, tag_end));

  log_infof("media-server: injected duration %S\n", duration_str);
  return str8_list_join(arena, parts, 0);
}

internal void
handle_manifest(OS_Handle socket, String8 file_path)
{
  Temp scratch = scratch_begin(0, 0);
  String8 full_path = str8f(scratch.arena, "%S/%S", media_root_path, file_path);
  DashCache *cache = get_or_create_cache(full_path);

  if(cache == 0)
  {
    send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("File not found"));
    scratch_end(scratch);
    return;
  }

  String8 manifest = read_cache_file(scratch.arena, cache->cache_dir, str8_lit("manifest.mpd"));
  if(manifest.size == 0)
  {
    send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("Manifest not found"));
    scratch_end(scratch);
    return;
  }

  HTTP_Response *res = http_response_alloc(scratch.arena, HTTP_Status_200_OK);
  http_header_add(scratch.arena, &res->headers, str8_lit("Content-Type"), str8_lit("application/dash+xml"));
  http_header_add(scratch.arena, &res->headers, str8_lit("Access-Control-Allow-Origin"), str8_lit("*"));
  http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"),
                  str8_from_u64(scratch.arena, manifest.size, 10, 0, 0));
  res->body = manifest;

  socket_write_all(socket, http_response_serialize(scratch.arena, res));
  log_infof("media-server: served manifest (%llu bytes)\n", manifest.size);

  scratch_end(scratch);
}

internal void
handle_segment(OS_Handle socket, String8 file_path, String8 segment_name)
{
  Temp scratch = scratch_begin(0, 0);
  String8 full_path = str8f(scratch.arena, "%S/%S", media_root_path, file_path);
  DashCache *cache = get_or_create_cache(full_path);

  if(cache == 0)
  {
    send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("File not found"));
    scratch_end(scratch);
    return;
  }

  String8 segment = read_cache_file(scratch.arena, cache->cache_dir, segment_name);
  if(segment.size == 0)
  {
    send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("Segment not found"));
    scratch_end(scratch);
    return;
  }

  HTTP_Response *res = http_response_alloc(scratch.arena, HTTP_Status_200_OK);
  http_header_add(scratch.arena, &res->headers, str8_lit("Content-Type"), str8_lit("video/mp4"));
  http_header_add(scratch.arena, &res->headers, str8_lit("Access-Control-Allow-Origin"), str8_lit("*"));
  http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"),
                  str8_from_u64(scratch.arena, segment.size, 10, 0, 0));
  res->body = segment;

  socket_write_all(socket, http_response_serialize(scratch.arena, res));
  scratch_end(scratch);
}

////////////////////////////////
//~ UI Handlers

internal String8List
find_subtitle_files(Arena *arena, String8 cache_dir)
{
  Temp scratch = scratch_begin(&arena, 1);
  String8List result = {0};
  char *dir_cstr = (char *)push_array(scratch.arena, u8, cache_dir.size + 1);
  MemoryCopy(dir_cstr, cache_dir.str, cache_dir.size);
  dir_cstr[cache_dir.size] = 0;

  DIR *d = opendir(dir_cstr);
  if(d)
  {
    struct dirent *entry;
    while((entry = readdir(d)) != 0)
    {
      String8 name = str8_cstring(entry->d_name);
      if(str8_find_needle(name, 0, str8_lit("subtitle."), 0) == 0 &&
         str8_find_needle(name, 0, str8_lit(".vtt"), 0) == name.size - 4)
      {
        str8_list_push(arena, &result, str8_copy(arena, name));
      }
    }
    closedir(d);
  }

  scratch_end(scratch);
  return result;
}

////////////////////////////////
//~ Entry Point

internal void
handle_connection_task(void *params);

internal void
entry_point(CmdLine *cmd_line)
{
  signal(SIGPIPE, SIG_IGN);

  Temp scratch = scratch_begin(0, 0);
  Log *log = log_alloc();
  log_select(log);
  log_scope_begin();
  Arena *arena = arena_alloc();

  media_root_path = cmd_line_string(cmd_line, str8_lit("media-root"));
  if(media_root_path.size == 0) { media_root_path = str8_lit("/tmp/media-server"); }
  cache_root_path = str8_lit("/tmp/media-server-cache");

  dash_cache = push_array(arena, DashCacheTable, 1);
  dash_cache->mutex = mutex_alloc();
  dash_cache->hash_table_size = 256;
  dash_cache->hash_table = push_array(arena, DashCache *, dash_cache->hash_table_size);
  dash_cache->cache_root = cache_root_path;
  dash_cache->lru_head = 0;
  dash_cache->lru_tail = 0;
  dash_cache->cache_count = 0;
  dash_cache->max_cache_count = 16;

  {
    Temp temp = scratch_begin(&arena, 1);
    char *cache_cstr = (char *)push_array(temp.arena, u8, cache_root_path.size + 1);
    MemoryCopy(cache_cstr, cache_root_path.str, cache_root_path.size);
    cache_cstr[cache_root_path.size] = 0;
    mkdir(cache_cstr, 0755);
    scratch_end(temp);
  }

  OS_SystemInfo *sys_info = os_get_system_info();
  u64 worker_count = sys_info->logical_processor_count;
  worker_pool = wp_pool_alloc(arena, worker_count);

  log_infof("media-server: port 8080, media root: %S, %llu workers\n",
            media_root_path, worker_count);

  OS_Handle listen_socket = os_socket_listen_tcp(8080);
  if(os_handle_match(listen_socket, os_handle_zero()))
  {
    log_errorf("media-server: failed to listen on port 8080\n");
    scratch_end(scratch);
    return;
  }

  log_info(str8_lit("media-server: ready\n"));
  log_scope_flush(scratch.arena);

  for(;;)
  {
    OS_Handle client = os_socket_accept(listen_socket);
    if(!os_handle_match(client, os_handle_zero()))
    {
      wp_submit(worker_pool, handle_connection_task, &client, sizeof(client));
    }
  }

  scratch_end(scratch);
}

internal void
handle_player(OS_Handle socket, String8 file_param)
{
  Temp scratch = scratch_begin(0, 0);

  // Compute cache directory (same as in start_dash_generation)
  String8 video_path = str8f(scratch.arena, "%S/%S", media_root_path, file_param);
  u64 hash = hash_string(video_path);
  String8 cache_dir = str8f(scratch.arena, "%S/%llu", cache_root_path, hash);

  // Find subtitle files in cache directory
  String8List subtitle_files = find_subtitle_files(scratch.arena, cache_dir);

  // Build JSON array of subtitle files
  String8List subtitle_json = {0};
  str8_list_push(scratch.arena, &subtitle_json, str8_lit("["));
  b32 first = 1;
  for(String8Node *n = subtitle_files.first; n != 0; n = n->next)
  {
    if(!first) str8_list_push(scratch.arena, &subtitle_json, str8_lit(","));
    first = 0;
    str8_list_push(scratch.arena, &subtitle_json, str8_lit("\""));
    str8_list_push(scratch.arena, &subtitle_json, n->string);
    str8_list_push(scratch.arena, &subtitle_json, str8_lit("\""));
  }
  str8_list_push(scratch.arena, &subtitle_json, str8_lit("]"));
  String8 subtitle_json_str = str8_list_join(scratch.arena, subtitle_json, 0);

  String8 html = str8f(scratch.arena,
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "  <meta charset=\"UTF-8\">\n"
    "  <title>Media Player</title>\n"
    "  <script src=\"https://cdn.dashjs.org/latest/dash.all.min.js\"></script>\n"
    "  <style>\n"
    "    * { margin: 0; padding: 0; box-sizing: border-box; }\n"
    "    body { font-family: sans-serif; background: #000; color: #fff; }\n"
    "    .container { max-width: 1200px; margin: 0 auto; padding: 20px; }\n"
    "    video { width: 100%%; background: #000; border-radius: 8px; }\n"
    "    #track-controls { margin-top: 15px; padding: 15px; background: #1a1a1a; border-radius: 4px; display: flex; gap: 20px; flex-wrap: wrap; }\n"
    "    #track-controls label { display: flex; align-items: center; gap: 8px; font-size: 14px; }\n"
    "    #track-controls select { padding: 5px 10px; background: #333; color: #fff; border: 1px solid #555; border-radius: 4px; cursor: pointer; min-width: 150px; }\n"
    "    #track-controls select:hover { background: #444; }\n"
    "    #debug { margin-top: 20px; padding: 10px; background: #222; border-radius: 4px; font-family: monospace; font-size: 12px; }\n"
    "    .error { color: #f44; }\n"
    "    .info { color: #4f4; }\n"
    "  </style>\n"
    "</head>\n"
    "<body>\n"
    "  <div class=\"container\">\n"
    "    <h1>%S</h1>\n"
    "    <video id=\"video\" controls></video>\n"
    "    <div id=\"track-controls\">\n"
    "      <label>🎬 Video: <select id=\"video-select\"></select></label>\n"
    "      <label>🔊 Audio: <select id=\"audio-select\"></select></label>\n"
    "      <label>📝 Subtitles: <select id=\"subtitle-select\"></select></label>\n"
    "    </div>\n"
    "    <div id=\"debug\"></div>\n"
    "  </div>\n"
    "  <script>\n"
    "    var debug = document.getElementById('debug');\n"
    "    function log(msg, isError) {\n"
    "      var div = document.createElement('div');\n"
    "      div.className = isError ? 'error' : 'info';\n"
    "      div.textContent = new Date().toISOString().substr(11,12) + ' ' + msg;\n"
    "      debug.appendChild(div);\n"
    "      console.log(msg);\n"
    "    }\n"
    "    var video = document.querySelector('#video');\n"
    "    var player = dashjs.MediaPlayer().create();\n"
    "    var retryCount = 0;\n"
    "    player.on(dashjs.MediaPlayer.events.ERROR, function(e) {\n"
    "      log('ERROR: ' + JSON.stringify(e.error), true);\n"
    "      // If manifest parsing fails and we haven't retried yet, try again\n"
    "      if(e.error.code === 10 && retryCount < 1) {\n"
    "        retryCount++;\n"
    "        log('Manifest incomplete - will retry in 3 seconds...');\n"
    "        setTimeout(function() { window.location.reload(); }, 3000);\n"
    "      }\n"
    "    });\n"
    "    player.on(dashjs.MediaPlayer.events.PLAYBACK_ERROR, function(e) {\n"
    "      log('PLAYBACK_ERROR: ' + JSON.stringify(e.error), true);\n"
    "    });\n"
    "    player.on(dashjs.MediaPlayer.events.STREAM_INITIALIZED, function() {\n"
    "      log('Stream initialized');\n"
    "      \n"
    "      // Manually select the correct audio track based on URL parameter\n"
    "      var urlParams = new URLSearchParams(window.location.search);\n"
    "      var requestedLang = urlParams.get('lang') || 'en';\n"
    "      var audioTracks = player.getTracksFor('audio');\n"
    "      \n"
    "      for(var i = 0; i < audioTracks.length; i++) {\n"
    "        if(audioTracks[i].lang === requestedLang) {\n"
    "          player.setCurrentTrack(audioTracks[i]);\n"
    "          log('Switched to requested audio track: ' + requestedLang);\n"
    "          break;\n"
    "        }\n"
    "      }\n"
    "      \n"
    "      // Small delay to ensure tracks are fully loaded\n"
    "      setTimeout(populateTrackSelects, 100);\n"
    "    });\n"
    "    function populateTrackSelects() {\n"
    "      try {\n"
    "        log('Populating track selects...');\n"
    "        var videoSelect = document.getElementById('video-select');\n"
    "        var audioSelect = document.getElementById('audio-select');\n"
    "        var subtitleSelect = document.getElementById('subtitle-select');\n"
    "        \n"
    "        // Get current language from URL\n"
    "        var urlParams = new URLSearchParams(window.location.search);\n"
    "        var currentLang = urlParams.get('lang') || 'en';\n"
    "        \n"
    "        // Video track (single quality with codec copy)\n"
    "        videoSelect.innerHTML = '';\n"
    "        var opt = document.createElement('option');\n"
    "        opt.textContent = '1920x1080 HEVC';\n"
    "        videoSelect.appendChild(opt);\n"
    "        \n"
    "        // Audio tracks\n"
    "        var audioTracks = player.getTracksFor('audio');\n"
    "        log('Audio tracks found: ' + audioTracks.length);\n"
    "        audioSelect.innerHTML = '';\n"
    "        \n"
    "        if(audioTracks.length === 0) {\n"
    "          log('WARNING: No audio tracks detected', true);\n"
    "          var opt = document.createElement('option');\n"
    "          opt.textContent = 'Audio (' + currentLang + ')';\n"
    "          audioSelect.appendChild(opt);\n"
    "        } else {\n"
    "          audioTracks.forEach(function(track, i) {\n"
    "            var opt = document.createElement('option');\n"
    "            var lang = track.lang || 'track' + i;\n"
    "            var channels = track.audioChannelConfiguration && track.audioChannelConfiguration[0] ? \n"
    "              (track.audioChannelConfiguration[0].value == 6 ? ' (5.1)' : ' (2.0)') : '';\n"
    "            opt.textContent = lang + channels;\n"
    "            opt.value = lang;\n"
    "            if(lang === currentLang) {\n"
    "              opt.selected = true;\n"
    "              log('Currently playing: ' + lang + channels);\n"
    "            }\n"
    "            audioSelect.appendChild(opt);\n"
    "          });\n"
    "        }\n"
    "        \n"
    "        // External subtitles\n"
    "        subtitleSelect.innerHTML = '';\n"
    "        subtitleSelect.disabled = false;\n"
    "        var noneOpt = document.createElement('option');\n"
    "        noneOpt.textContent = 'None';\n"
    "        noneOpt.value = '';\n"
    "        subtitleSelect.appendChild(noneOpt);\n"
    "        \n"
    "        loadedSubtitles.forEach(function(sub) {\n"
    "          var opt = document.createElement('option');\n"
    "          opt.textContent = sub.lang.toUpperCase();\n"
    "          opt.value = sub.url;\n"
    "          subtitleSelect.appendChild(opt);\n"
    "        });\n"
    "        \n"
    "        if(loadedSubtitles.length === 0) {\n"
    "          var opt = document.createElement('option');\n"
    "          opt.textContent = 'None available';\n"
    "          opt.disabled = true;\n"
    "          subtitleSelect.appendChild(opt);\n"
    "        }\n"
    "        \n"
    "        log('Tracks ready: ' + audioTracks.length + ' audio options');\n"
    "      } catch(e) {\n"
    "        log('ERROR populating tracks: ' + e.message, true);\n"
    "      }\n"
    "    }\n"
    "    // Audio track switching - seamless like subtitles\n"
    "    document.getElementById('audio-select').addEventListener('change', function(e) {\n"
    "      var selectedLang = e.target.value;\n"
    "      if(!selectedLang) return;\n"
    "      \n"
    "      var audioTracks = player.getTracksFor('audio');\n"
    "      for(var i = 0; i < audioTracks.length; i++) {\n"
    "        if(audioTracks[i].lang === selectedLang) {\n"
    "          player.setCurrentTrack(audioTracks[i]);\n"
    "          log('Switched to audio language: ' + selectedLang);\n"
    "          return;\n"
    "        }\n"
    "      }\n"
    "      log('Audio track not found: ' + selectedLang, true);\n"
    "    });\n"
    "    \n"
    "    document.getElementById('video-select').addEventListener('change', function(e) {\n"
    "      e.target.selectedIndex = 0; // Reset selection\n"
    "      log('Video quality switching not supported with codec copy');\n"
    "    });\n"
    "    \n"
    "    document.getElementById('subtitle-select').addEventListener('change', function(e) {\n"
    "      var url = e.target.value;\n"
    "      \n"
    "      // Disable all existing text tracks\n"
    "      for(var i = 0; i < video.textTracks.length; i++) {\n"
    "        video.textTracks[i].mode = 'disabled';\n"
    "      }\n"
    "      \n"
    "      // Remove existing track elements\n"
    "      var existingTracks = video.querySelectorAll('track');\n"
    "      existingTracks.forEach(function(track) { track.remove(); });\n"
    "      \n"
    "      if(url) {\n"
    "        // Add new subtitle track\n"
    "        var track = document.createElement('track');\n"
    "        track.kind = 'subtitles';\n"
    "        track.src = url;\n"
    "        track.srclang = e.target.options[e.target.selectedIndex].textContent.toLowerCase();\n"
    "        track.label = e.target.options[e.target.selectedIndex].textContent;\n"
    "        track.default = false;\n"
    "        video.appendChild(track);\n"
    "        \n"
    "        // Force browser to load and show the track\n"
    "        track.addEventListener('load', function() {\n"
    "          track.track.mode = 'showing';\n"
    "          log('Enabled subtitles: ' + track.label);\n"
    "        });\n"
    "        track.addEventListener('error', function(e) {\n"
    "          log('Subtitle load error: ' + JSON.stringify(e), true);\n"
    "        });\n"
    "        \n"
    "        // Set mode immediately as well (some browsers need both)\n"
    "        track.track.mode = 'showing';\n"
    "      } else {\n"
    "        log('Subtitles disabled');\n"
    "      }\n"
    "    });\n"
    "    player.on(dashjs.MediaPlayer.events.CAN_PLAY, function() {\n"
    "      log('Can play');\n"
    "    });\n"
    "    player.on(dashjs.MediaPlayer.events.PLAYBACK_PLAYING, function() {\n"
    "      log('Playing - video size: ' + video.videoWidth + 'x' + video.videoHeight);\n"
    "    });\n"
    "    player.on(dashjs.MediaPlayer.events.BUFFER_LOADED, function(e) {\n"
    "      log('Buffer loaded: ' + e.mediaType);\n"
    "    });\n"
    "    video.addEventListener('loadedmetadata', function() {\n"
    "      log('Video metadata loaded: ' + video.videoWidth + 'x' + video.videoHeight + ', duration: ' + video.duration);\n"
    "      // If video dimensions are 0x0, manifest might be incomplete - retry once\n"
    "      if(video.videoWidth === 0 && video.videoHeight === 0 && retryCount < 1) {\n"
    "        retryCount++;\n"
    "        log('Video track not loaded - retrying in 2 seconds...');\n"
    "        setTimeout(function() { window.location.reload(); }, 2000);\n"
    "      }\n"
    "    });\n"
    "    video.addEventListener('error', function(e) {\n"
    "      log('Video element error: ' + video.error.message, true);\n"
    "    });\n"
    "    // Check codec support\n"
    "    var codecs = [\n"
    "      'video/mp4; codecs=\"avc1.6e0028\"',  // H.264 High 4.0\n"
    "      'video/mp4; codecs=\"avc1.64001f\"',  // H.264 High 3.1\n"
    "      'video/mp4; codecs=\"avc1.42E01E\"',  // H.264 Baseline 3.0\n"
    "      'video/mp4; codecs=\"hev1\"',         // HEVC (generic)\n"
    "      'video/mp4; codecs=\"hvc1\"',         // HEVC (generic alt)\n"
    "      'video/mp4; codecs=\"hev1.1.6.L93.B0\"',  // HEVC Main\n"
    "      'video/mp4; codecs=\"hvc1.1.6.L93.B0\"',  // HEVC Main (alt)\n"
    "      'audio/mp4; codecs=\"mp4a.40.2\"'    // AAC-LC\n"
    "    ];\n"
    "    codecs.forEach(function(c) {\n"
    "      var supported = MediaSource.isTypeSupported(c);\n"
    "      log('Codec ' + c + ': ' + (supported ? 'SUPPORTED' : 'NOT SUPPORTED'), !supported);\n"
    "    });\n"
    "    // Pre-select audio language from URL parameter\n"
    "    var urlParams = new URLSearchParams(window.location.search);\n"
    "    var selectedLang = urlParams.get('lang') || 'en';\n"
    "    log('Pre-selecting audio language: ' + selectedLang);\n"
    "    // Note: setInitialMediaSettingsFor requires player.updateSettings() instead\n"
    "    player.updateSettings({streaming: {initialSettings: {audio: {lang: selectedLang}}}});\n"
    "    log('Initializing player with /media/%S/manifest.mpd');\n"
    "    player.initialize(video, '/media/%S/manifest.mpd', true);\n"
    "    \n"
    "    // Load external subtitle files\n"
    "    var videoFile = '%S';\n"
    "    var subtitleFiles = %S;\n"
    "    var loadedSubtitles = [];\n"
    "    subtitleFiles.forEach(function(filename) {\n"
    "      // Extract language code: subtitle.eng.vtt -> eng\n"
    "      var parts = filename.split('.');\n"
    "      if(parts.length >= 3) {\n"
    "        var lang = parts[parts.length - 2];\n"
    "        var url = '/media/' + videoFile + '/' + filename;\n"
    "        loadedSubtitles.push({lang: lang, url: url, filename: filename});\n"
    "        log('Found subtitle: ' + lang + ' (' + filename + ')');\n"
    "      }\n"
    "    });\n"
    "  </script>\n"
    "</body>\n"
    "</html>\n",
    file_param, file_param, file_param, file_param, subtitle_json_str);

  HTTP_Response *res = http_response_alloc(scratch.arena, HTTP_Status_200_OK);
  http_header_add(scratch.arena, &res->headers, str8_lit("Content-Type"), str8_lit("text/html"));
  http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"),
                  str8_from_u64(scratch.arena, html.size, 10, 0, 0));
  res->body = html;

  socket_write_all(socket, http_response_serialize(scratch.arena, res));
  scratch_end(scratch);
}

internal void
handle_directory_listing(OS_Handle socket, String8 dir_path)
{
  Temp scratch = scratch_begin(0, 0);

  String8 full_path = (dir_path.size > 0) ? str8f(scratch.arena, "%S/%S", media_root_path, dir_path) : media_root_path;
  char *path_cstr = (char *)push_array(scratch.arena, u8, full_path.size + 1);
  MemoryCopy(path_cstr, full_path.str, full_path.size);
  path_cstr[full_path.size] = 0;

  String8List html = {0};
  str8_list_push(scratch.arena, &html, str8_lit(
    "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n"
    "<title>Media Library</title>\n"
    "<style>\n"
    "* { margin: 0; padding: 0; box-sizing: border-box; }\n"
    "body { font-family: sans-serif; background: #000; color: #fff; padding: 20px; }\n"
    ".container { max-width: 1200px; margin: 0 auto; }\n"
    "h1 { margin-bottom: 20px; }\n"
    ".file-list { list-style: none; }\n"
    ".file-item { padding: 12px; margin: 4px 0; background: #1a1a1a; border-radius: 6px; }\n"
    ".file-item:hover { background: #2a2a2a; }\n"
    ".file-item a { color: #fff; text-decoration: none; }\n"
    "</style>\n</head>\n<body>\n<div class=\"container\">\n"
    "<h1>Media Library</h1>\n<ul class=\"file-list\">\n"));

  DIR *dir = opendir(path_cstr);
  if(dir)
  {
    struct dirent *entry;
    while((entry = readdir(dir)) != 0)
    {
      if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

      String8 name = str8_cstring(entry->d_name);
      String8 entry_path = str8f(scratch.arena, "%S/%S", full_path, name);
      b32 is_dir = os_directory_path_exists(entry_path);

      if(is_dir)
      {
        String8 subdir = (dir_path.size > 0) ? str8f(scratch.arena, "%S/%S", dir_path, name) : name;
        str8_list_pushf(scratch.arena, &html,
          "<li class=\"file-item\"><a href=\"/?dir=%S\">📁 %S/</a></li>\n", subdir, name);
      }
      else
      {
        // Check if video file
        if(name.size > 4)
        {
          String8 ext = str8_skip(name, name.size - 4);
          if(str8_match(ext, str8_lit(".mkv"), StringMatchFlag_CaseInsensitive) ||
             str8_match(ext, str8_lit(".mp4"), StringMatchFlag_CaseInsensitive) ||
             str8_match(ext, str8_lit(".avi"), StringMatchFlag_CaseInsensitive))
          {
            String8 file = (dir_path.size > 0) ? str8f(scratch.arena, "%S/%S", dir_path, name) : name;
            str8_list_pushf(scratch.arena, &html,
              "<li class=\"file-item\"><a href=\"/player?file=%S\">🎬 %S</a></li>\n", file, name);
          }
        }
      }
    }
    closedir(dir);
  }

  str8_list_push(scratch.arena, &html, str8_lit("</ul>\n</div>\n</body>\n</html>\n"));
  String8 html_str = str8_list_join(scratch.arena, html, 0);

  HTTP_Response *res = http_response_alloc(scratch.arena, HTTP_Status_200_OK);
  http_header_add(scratch.arena, &res->headers, str8_lit("Content-Type"), str8_lit("text/html"));
  http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"),
                  str8_from_u64(scratch.arena, html_str.size, 10, 0, 0));
  res->body = html_str;

  socket_write_all(socket, http_response_serialize(scratch.arena, res));
  scratch_end(scratch);
}

internal void
handle_http_request(HTTP_Request *req, OS_Handle socket)
{
  if(str8_match(req->path, str8_lit("/"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    String8 dir_param = str8_zero();
    if(req->query.size > 0)
    {
      String8List parts = str8_split(scratch.arena, req->query, (u8 *)"&", 1, 0);
      for(String8Node *n = parts.first; n != 0; n = n->next)
      {
        u64 eq = str8_find_needle(n->string, 0, str8_lit("="), 0);
        if(eq < n->string.size && str8_match(str8_prefix(n->string, eq), str8_lit("dir"), 0))
        {
          dir_param = str8_skip(n->string, eq + 1);
          break;
        }
      }
    }
    handle_directory_listing(socket, dir_param);
    scratch_end(scratch);
  }
  else if(str8_match(req->path, str8_lit("/player"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    String8 file_param = str8_zero();
    if(req->query.size > 0)
    {
      String8List parts = str8_split(scratch.arena, req->query, (u8 *)"&", 1, 0);
      for(String8Node *n = parts.first; n != 0; n = n->next)
      {
        u64 eq = str8_find_needle(n->string, 0, str8_lit("="), 0);
        if(eq < n->string.size && str8_match(str8_prefix(n->string, eq), str8_lit("file"), 0))
        {
          file_param = str8_skip(n->string, eq + 1);
          break;
        }
      }
    }
    if(file_param.size > 0)
    {
      handle_player(socket, file_param);
    }
    else
    {
      send_error_response(socket, HTTP_Status_400_BadRequest, str8_lit("Missing file parameter"));
    }
    scratch_end(scratch);
  }
  else if(str8_match(str8_prefix(req->path, 7), str8_lit("/media/"), 0))
  {
    String8 media_path = str8_skip(req->path, 7);

    // Check if this is a path with slash (directory-style URL)
    u64 slash_pos = str8_find_needle(media_path, 0, str8_lit("/"), 0);
    if(slash_pos < media_path.size)
    {
      // /media/file.mkv/manifest.mpd, /media/file.mkv/init-stream0.m4s, or /media/file.mkv/subtitle.eng.vtt
      String8 file = str8_prefix(media_path, slash_pos);
      String8 resource = str8_skip(media_path, slash_pos + 1);

      if(str8_match(resource, str8_lit("manifest.mpd"), 0))
      {
        handle_manifest(socket, file);
      }
      else if(str8_find_needle(resource, 0, str8_lit(".vtt"), 0) == resource.size - 4)
      {
        // Subtitle file from cache: /media/file.mkv/subtitle.eng.vtt
        Temp temp = scratch_begin(0, 0);
        String8 file_path = str8f(temp.arena, "%S/%S", media_root_path, file);
        u64 hash = hash_string(file_path);
        String8 cache_dir = str8f(temp.arena, "%S/%llu", cache_root_path, hash);
        String8 vtt_path = str8f(temp.arena, "%S/%S", cache_dir, resource);

        OS_FileProperties props = os_properties_from_file_path(vtt_path);
        if(props.size > 0)
        {
          u8 *buffer = push_array(temp.arena, u8, props.size);
          OS_Handle file_handle = os_file_open(OS_AccessFlag_Read, vtt_path);
          os_file_read(file_handle, rng_1u64(0, props.size), buffer);
          os_file_close(file_handle);
          String8 subtitle_content = str8(buffer, props.size);

          HTTP_Response *res = http_response_alloc(temp.arena, HTTP_Status_200_OK);
          http_header_add(temp.arena, &res->headers, str8_lit("Content-Type"), str8_lit("text/vtt; charset=utf-8"));
          http_header_add(temp.arena, &res->headers, str8_lit("Access-Control-Allow-Origin"), str8_lit("*"));
          http_header_add(temp.arena, &res->headers, str8_lit("Content-Length"),
                          str8_from_u64(temp.arena, subtitle_content.size, 10, 0, 0));
          res->body = subtitle_content;
          socket_write_all(socket, http_response_serialize(temp.arena, res));
        }
        else
        {
          send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("Subtitle not found"));
        }
        scratch_end(temp);
      }
      else
      {
        handle_segment(socket, file, resource);
      }
    }
    // Legacy: /media/file.mkv.mpd (redirect to proper URL)
    else if(str8_find_needle(media_path, 0, str8_lit(".mpd"), 0) == media_path.size - 4)
    {
      handle_manifest(socket, str8_prefix(media_path, media_path.size - 4));
    }
    // Subtitle files: /media/video.en.vtt
    else if(str8_find_needle(media_path, 0, str8_lit(".vtt"), 0) == media_path.size - 4)
    {
      Temp temp = scratch_begin(0, 0);
      String8 subtitle_path = str8f(temp.arena, "%S/%S", media_root_path, media_path);

      OS_FileProperties props = os_properties_from_file_path(subtitle_path);
      if(props.size > 0)
      {
        u8 *buffer = push_array(temp.arena, u8, props.size);
        OS_Handle file = os_file_open(OS_AccessFlag_Read, subtitle_path);
        os_file_read(file, rng_1u64(0, props.size), buffer);
        os_file_close(file);
        String8 subtitle_content = str8(buffer, props.size);

        HTTP_Response *res = http_response_alloc(temp.arena, HTTP_Status_200_OK);
        http_header_add(temp.arena, &res->headers, str8_lit("Content-Type"), str8_lit("text/vtt; charset=utf-8"));
        http_header_add(temp.arena, &res->headers, str8_lit("Access-Control-Allow-Origin"), str8_lit("*"));
        http_header_add(temp.arena, &res->headers, str8_lit("Content-Length"),
                        str8_from_u64(temp.arena, subtitle_content.size, 10, 0, 0));
        res->body = subtitle_content;
        socket_write_all(socket, http_response_serialize(temp.arena, res));
      }
      else
      {
        send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("Subtitle not found"));
      }

      scratch_end(temp);
    }
    else
    {
      send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("Not found"));
    }
  }
  else
  {
    send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("Not found"));
  }
}

////////////////////////////////
//~ Connection Handling

internal void
handle_http_request(HTTP_Request *req, OS_Handle socket);

internal void
handle_connection(OS_Handle socket)
{
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = arena_alloc();

  u8 buffer[KB(16)];
  ssize_t n = 0;
  for(;;)
  {
    n = read((int)socket.u64[0], buffer, sizeof(buffer));
    if(n >= 0) break;
    else if(errno == EINTR) continue;
    else break;
  }

  if(n > 0)
  {
    HTTP_Request *req = http_request_parse(arena, str8(buffer, (u64)n));
    if(req->method != HTTP_Method_Unknown && req->path.size > 0)
    {
      handle_http_request(req, socket);
    }
    else
    {
      send_error_response(socket, HTTP_Status_400_BadRequest, str8_lit("Invalid request"));
    }
  }

  os_file_close(socket);
  arena_release(arena);
  scratch_end(scratch);
}

internal void
handle_connection_task(void *params)
{
  OS_Handle socket = *(OS_Handle *)params;
  handle_connection(socket);
}
