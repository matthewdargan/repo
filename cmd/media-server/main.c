#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include "base/inc.h"
#include "http/inc.h"
#include "base/inc.c"
#include "http/inc.c"

typedef struct CacheInfo CacheInfo;
struct CacheInfo
{
  String8 cache_dir;
};

typedef enum TransmuxState TransmuxState;
enum TransmuxState
{
  TransmuxState_Idle,
  TransmuxState_Queued,
  TransmuxState_Processing,
  TransmuxState_Complete,
  TransmuxState_Failed,
};

typedef struct TransmuxJob TransmuxJob;
struct TransmuxJob
{
  u64 file_hash;
  String8 file_path;
  String8 cache_dir;
  TransmuxState state;
  TransmuxJob *queue_next;
  TransmuxJob *hash_next;
};

typedef struct TransmuxQueue TransmuxQueue;
struct TransmuxQueue
{
  Mutex mutex;
  Arena *arena;
  TransmuxJob *first;
  TransmuxJob *last;
  TransmuxJob **hash_table;
  u64 hash_table_size;
};

////////////////////////////////
//~ Globals

global String8 media_root_path = {0};
global String8 cache_root_path = {0};
global WP_Pool *worker_pool = 0;
global WP_Pool *transmux_pool = 0;
global TransmuxQueue *transmux_queue = 0;

////////////////////////////////
//~ Helpers

internal u64
hash_string(String8 str)
{
  u64 hash = 5381;
  u8 *data = str.str;
  for(u64 i = 0; i < str.size; i += 1)
  {
    hash = ((hash << 5) + hash) + data[i];
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
//~ Cache

internal String8
get_cache_dir(Arena *arena, String8 file_path)
{
  u64 hash = hash_string(file_path);
  return str8f(arena, "%S/%llu", cache_root_path, hash);
}

internal b32
cache_exists(String8 cache_dir)
{
  Temp scratch = scratch_begin(0, 0);
  String8 manifest_path = str8f(scratch.arena, "%S/manifest.mpd", cache_dir);
  b32 exists = os_file_path_exists(manifest_path);
  scratch_end(scratch);
  return exists;
}

////////////////////////////////
//~ Transmux Queue

internal TransmuxJob *
transmux_find_job(u64 file_hash)
{
  u64 bucket = file_hash % transmux_queue->hash_table_size;
  for(TransmuxJob *job = transmux_queue->hash_table[bucket]; job != 0; job = job->hash_next)
  {
    if(job->file_hash == file_hash)
    {
      return job;
    }
  }
  return 0;
}

internal TransmuxState
transmux_get_state(u64 file_hash)
{
  TransmuxState result = TransmuxState_Idle;
  MutexScope(transmux_queue->mutex)
  {
    TransmuxJob *job = transmux_find_job(file_hash);
    if(job != 0)
    {
      result = job->state;
    }
  }
  return result;
}

internal void
transmux_queue_job(String8 file_path, String8 cache_dir, u64 file_hash)
{
  MutexScope(transmux_queue->mutex)
  {
    TransmuxJob *existing = transmux_find_job(file_hash);
    if(existing != 0)
    {
      TransmuxState state = existing->state;
      if(state == TransmuxState_Complete || state == TransmuxState_Failed)
      {
        existing->state = TransmuxState_Queued;
      }
    }
    else
    {
      TransmuxJob *job = push_array(transmux_queue->arena, TransmuxJob, 1);
      job->file_hash = file_hash;
      job->file_path = str8_copy(transmux_queue->arena, file_path);
      job->cache_dir = str8_copy(transmux_queue->arena, cache_dir);
      job->state = TransmuxState_Queued;
      job->queue_next = 0;
      job->hash_next = 0;

      if(transmux_queue->last != 0)
      {
        transmux_queue->last->queue_next = job;
      }
      else
      {
        transmux_queue->first = job;
      }
      transmux_queue->last = job;

      u64 bucket = file_hash % transmux_queue->hash_table_size;
      job->hash_next = transmux_queue->hash_table[bucket];
      transmux_queue->hash_table[bucket] = job;

      log_infof("media-server: queued transmux for %S\n", file_path);
    }
  }
}

internal TransmuxJob *
transmux_dequeue_job(void)
{
  TransmuxJob *job = 0;
  MutexScope(transmux_queue->mutex)
  {
    for(TransmuxJob *j = transmux_queue->first; j != 0; j = j->queue_next)
    {
      if(j->state == TransmuxState_Queued)
      {
        j->state = TransmuxState_Processing;
        job = j;
        break;
      }
    }
  }
  return job;
}

internal void
transmux_mark_complete(TransmuxJob *job, b32 success)
{
  MutexScope(transmux_queue->mutex)
  {
    job->state = success ? TransmuxState_Complete : TransmuxState_Failed;
  }
}

////////////////////////////////
//~ File I/O

internal void
stream_file_to_socket(OS_Handle socket, String8 file_path, u64 file_size)
{
  Temp scratch = scratch_begin(0, 0);
  OS_Handle file = os_file_open(OS_AccessFlag_Read, file_path);
  if(os_handle_match(file, os_handle_zero()))
  {
    scratch_end(scratch);
    return;
  }

  u8 buffer[KB(64)];
  for(u64 offset = 0; offset < file_size;)
  {
    u64 to_read = Min(sizeof(buffer), file_size - offset);
    u64 did_read = os_file_read(file, rng_1u64(offset, offset + to_read), buffer);
    if(did_read == 0) break;
    socket_write_all(socket, str8(buffer, did_read));
    offset += did_read;
  }

  os_file_close(file);
  scratch_end(scratch);
}

internal String8
read_small_file(Arena *arena, String8 file_path, u64 max_size)
{
  Temp scratch = scratch_begin(&arena, 1);
  OS_FileProperties props = os_properties_from_file_path(file_path);

  if(props.size > 0 && props.size <= max_size)
  {
    OS_Handle file = os_file_open(OS_AccessFlag_Read, file_path);
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
//~ Manifest

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
  for(u64 i = 0; i < pattern_count; i += 1)
  {
    String8 pattern = patterns[i];
    if(str8_find_needle(line, 0, pattern, 0) < line.size) return 1;
  }
  return 0;
}

internal void
postprocess_manifest(String8 cache_dir)
{
  Temp scratch = scratch_begin(0, 0);
  String8 manifest_path = str8f(scratch.arena, "%S/manifest.mpd", cache_dir);
  String8 manifest = read_small_file(scratch.arena, manifest_path, MB(1));
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
//~ Subtitles

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
  AVStream *in_stream = NULL;
  AVStream *out_stream = NULL;
  const AVCodec *dec = NULL;
  AVPacket *pkt = NULL;
  AVPacket *enc_pkt = NULL;

  if(avformat_open_input(&in_ctx, in_cstr, NULL, NULL) != 0) {
    scratch_end(temp);
    return;
  }

  if(avformat_find_stream_info(in_ctx, NULL) != 0) {
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return;
  }

  in_stream = in_ctx->streams[stream_idx];

  dec = avcodec_find_decoder(in_stream->codecpar->codec_id);
  if(dec == 0) {
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return;
  }

  dec_ctx = avcodec_alloc_context3(dec);
  if(dec_ctx == 0) {
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return;
  }

  if(avcodec_parameters_to_context(dec_ctx, in_stream->codecpar) != 0) {
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return;
  }

  if(avcodec_open2(dec_ctx, dec, NULL) != 0) {
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return;
  }

  enc = avcodec_find_encoder_by_name("webvtt");
  if(enc == 0) {
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return;
  }

  enc_ctx = avcodec_alloc_context3(enc);
  if(enc_ctx == 0) {
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return;
  }

  enc_ctx->time_base = in_stream->time_base;

  {
    const char *webvtt_header = "WEBVTT\n\n";
    enc_ctx->subtitle_header_size = strlen(webvtt_header);
    enc_ctx->subtitle_header = av_malloc(enc_ctx->subtitle_header_size + 1);
    if(enc_ctx->subtitle_header)
    {
      MemoryCopy(enc_ctx->subtitle_header, webvtt_header, enc_ctx->subtitle_header_size + 1);
    }
  }

  if(avcodec_open2(enc_ctx, enc, NULL) != 0) {
    avcodec_free_context(&enc_ctx);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return;
  }

  if(avformat_alloc_output_context2(&out_ctx, NULL, "webvtt", out_cstr) != 0) {
    avcodec_free_context(&enc_ctx);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return;
  }

  out_stream = avformat_new_stream(out_ctx, NULL);
  if(out_stream == 0) {
    avformat_free_context(out_ctx);
    avcodec_free_context(&enc_ctx);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return;
  }

  if(avcodec_parameters_from_context(out_stream->codecpar, enc_ctx) != 0) {
    avformat_free_context(out_ctx);
    avcodec_free_context(&enc_ctx);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return;
  }

  out_stream->time_base = in_stream->time_base;

  if(avio_open(&out_ctx->pb, out_cstr, AVIO_FLAG_WRITE) != 0) {
    avformat_free_context(out_ctx);
    avcodec_free_context(&enc_ctx);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return;
  }

  if(avformat_write_header(out_ctx, NULL) != 0) {
    avio_closep(&out_ctx->pb);
    avformat_free_context(out_ctx);
    avcodec_free_context(&enc_ctx);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return;
  }

  pkt = av_packet_alloc();
  enc_pkt = av_packet_alloc();

  if(pkt != 0 && enc_pkt != 0)
  {
    AVSubtitle sub;
    while(av_read_frame(in_ctx, pkt) >= 0)
    {
      if(pkt->stream_index == (int)stream_idx)
      {
        int got_sub = 0;
        if(avcodec_decode_subtitle2(dec_ctx, &sub, &got_sub, pkt) >= 0 && got_sub)
        {
          b32 has_ass_subtitle = 0;
          for(u32 i = 0; i < sub.num_rects; i += 1)
          {
            if(sub.rects[i]->type == SUBTITLE_ASS)
            {
              has_ass_subtitle = 1;
              break;
            }
          }

          if(has_ass_subtitle != 0)
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

    av_write_trailer(out_ctx);
    log_infof("media-server: extracted subtitle stream %u (lang: %S)\n", stream_idx, lang);
  }

  if(enc_pkt != 0) av_packet_free(&enc_pkt);
  if(pkt != 0) av_packet_free(&pkt);
  if(out_ctx != 0)
  {
    if(out_ctx->pb != 0) avio_closep(&out_ctx->pb);
    avformat_free_context(out_ctx);
  }
  if(enc_ctx != 0) avcodec_free_context(&enc_ctx);
  if(dec_ctx != 0) avcodec_free_context(&dec_ctx);
  if(in_ctx != 0) avformat_close_input(&in_ctx);
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

  for(u32 i = 0; i < fmt->nb_streams; i += 1)
  {
    AVStream *stream = fmt->streams[i];
    AVCodecParameters *codecpar = stream->codecpar;

    if(codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) continue;

    if(codecpar->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE ||
       codecpar->codec_id == AV_CODEC_ID_DVD_SUBTITLE ||
       codecpar->codec_id == AV_CODEC_ID_DVB_SUBTITLE)
    {
      continue;
    }

    String8 lang = str8_lit("und");
    AVDictionaryEntry *e = av_dict_get(stream->metadata, "language", NULL, 0);
    if(e != 0 && e->value != 0)
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
//~ DASH

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
  AVDictionary *opts = NULL;
  AVPacket *pkt = NULL;
  b32 success = 0;

  if(avformat_open_input(&in_ctx, in_cstr, NULL, NULL) != 0) {
    scratch_end(temp);
    return 0;
  }

  if(avformat_find_stream_info(in_ctx, NULL) != 0) {
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return 0;
  }

  if(avformat_alloc_output_context2(&out_ctx, NULL, "dash", out_cstr) != 0) {
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return 0;
  }

  int *stream_map = push_array(temp.arena, int, in_ctx->nb_streams);
  for(u32 i = 0; i < in_ctx->nb_streams; i += 1) stream_map[i] = -1;

  u32 stream_idx = 0;
  for(u32 i = 0; i < in_ctx->nb_streams; i += 1)
  {
    AVStream *in_stream = in_ctx->streams[i];
    AVCodecParameters *codecpar = in_stream->codecpar;

    if(codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
       codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
    {
      continue;
    }

    AVStream *out_stream = avformat_new_stream(out_ctx, NULL);
    if(out_stream == 0) {
      avformat_free_context(out_ctx);
      avformat_close_input(&in_ctx);
      scratch_end(temp);
      return 0;
    }

    if(avcodec_parameters_copy(out_stream->codecpar, codecpar) != 0) {
      avformat_free_context(out_ctx);
      avformat_close_input(&in_ctx);
      scratch_end(temp);
      return 0;
    }

    out_stream->codecpar->codec_tag = 0;
    out_stream->time_base = in_stream->time_base;
    av_dict_copy(&out_stream->metadata, in_stream->metadata, 0);

    stream_map[i] = stream_idx;
    stream_idx += 1;
  }

  av_dict_set(&opts, "seg_duration", "4", 0);
  av_dict_set(&opts, "use_timeline", "1", 0);
  av_dict_set(&opts, "use_template", "1", 0);
  av_dict_set(&opts, "single_file", "0", 0);
  av_dict_set(&opts, "streaming", "0", 0);

  if(avformat_write_header(out_ctx, &opts) != 0)
  {
    av_dict_free(&opts);
    avformat_free_context(out_ctx);
    avformat_close_input(&in_ctx);
    scratch_end(temp);
    return 0;
  }
  av_dict_free(&opts);

  pkt = av_packet_alloc();
  if(pkt != 0)
  {
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
  }

  if(out_ctx != 0)
  {
    if(!(out_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_ctx->pb);
    avformat_free_context(out_ctx);
  }
  if(in_ctx != 0) avformat_close_input(&in_ctx);
  scratch_end(temp);
  return success;
}

internal b32
generate_dash_to_cache(String8 file_path, String8 cache_dir, u64 file_hash)
{
  (void)file_hash;
  Temp scratch = scratch_begin(0, 0);

  char *cache_dir_cstr = (char *)push_array(scratch.arena, u8, cache_dir.size + 1);
  MemoryCopy(cache_dir_cstr, cache_dir.str, cache_dir.size);
  cache_dir_cstr[cache_dir.size] = 0;
  mkdir(cache_dir_cstr, 0755);

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

  scratch_end(scratch);
  return 1;
}

internal CacheInfo
get_or_create_cache(Arena *arena, String8 file_path, b32 *out_processing)
{
  CacheInfo result = {0};
  u64 file_hash = hash_string(file_path);
  result.cache_dir = get_cache_dir(arena, file_path);
  *out_processing = 0;

  if(cache_exists(result.cache_dir))
  {
    return result;
  }

  TransmuxState state = transmux_get_state(file_hash);
  if(state == TransmuxState_Processing || state == TransmuxState_Queued)
  {
    *out_processing = 1;
    return result;
  }

  transmux_queue_job(file_path, result.cache_dir, file_hash);
  *out_processing = 1;
  return result;
}

////////////////////////////////
//~ UI

internal String8List
find_subtitle_files(Arena *arena, String8 cache_dir)
{
  Temp scratch = scratch_begin(&arena, 1);
  String8List result = {0};
  char *dir_cstr = (char *)push_array(scratch.arena, u8, cache_dir.size + 1);
  MemoryCopy(dir_cstr, cache_dir.str, cache_dir.size);
  dir_cstr[cache_dir.size] = 0;

  DIR *d = opendir(dir_cstr);
  if(d != 0)
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

internal void
handle_player(OS_Handle socket, String8 file_param)
{
  Temp scratch = scratch_begin(0, 0);

  String8 video_path = str8f(scratch.arena, "%S/%S", media_root_path, file_param);
  u64 hash = hash_string(video_path);
  String8 cache_dir = str8f(scratch.arena, "%S/%llu", cache_root_path, hash);

  String8List subtitle_files = find_subtitle_files(scratch.arena, cache_dir);

  String8List subtitle_json = {0};
  str8_list_push(scratch.arena, &subtitle_json, str8_lit("["));
  b32 first = 1;
  for(String8Node *n = subtitle_files.first; n != 0; n = n->next)
  {
    if(first == 0) str8_list_push(scratch.arena, &subtitle_json, str8_lit(","));
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
    "      setTimeout(populateTrackSelects, 100);\n"
    "    });\n"
    "    function populateTrackSelects() {\n"
    "      try {\n"
    "        log('Populating track selects...');\n"
    "        var videoSelect = document.getElementById('video-select');\n"
    "        var audioSelect = document.getElementById('audio-select');\n"
    "        var subtitleSelect = document.getElementById('subtitle-select');\n"
    "        \n"
    "        var urlParams = new URLSearchParams(window.location.search);\n"
    "        var currentLang = urlParams.get('lang') || 'en';\n"
    "        \n"
    "        videoSelect.innerHTML = '';\n"
    "        var opt = document.createElement('option');\n"
    "        opt.textContent = '1920x1080 HEVC';\n"
    "        videoSelect.appendChild(opt);\n"
    "        \n"
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
    "      e.target.selectedIndex = 0;\n"
    "      log('Video quality switching not supported with codec copy');\n"
    "    });\n"
    "    \n"
    "    document.getElementById('subtitle-select').addEventListener('change', function(e) {\n"
    "      var url = e.target.value;\n"
    "      \n"
    "      for(var i = 0; i < video.textTracks.length; i++) {\n"
    "        video.textTracks[i].mode = 'disabled';\n"
    "      }\n"
    "      \n"
    "      var existingTracks = video.querySelectorAll('track');\n"
    "      existingTracks.forEach(function(track) { track.remove(); });\n"
    "      \n"
    "      if(url) {\n"
    "        var track = document.createElement('track');\n"
    "        track.kind = 'subtitles';\n"
    "        track.src = url;\n"
    "        track.srclang = e.target.options[e.target.selectedIndex].textContent.toLowerCase();\n"
    "        track.label = e.target.options[e.target.selectedIndex].textContent;\n"
    "        track.default = false;\n"
    "        video.appendChild(track);\n"
    "        \n"
    "        track.addEventListener('load', function() {\n"
    "          track.track.mode = 'showing';\n"
    "          log('Enabled subtitles: ' + track.label);\n"
    "        });\n"
    "        track.addEventListener('error', function(e) {\n"
    "          log('Subtitle load error: ' + JSON.stringify(e), true);\n"
    "        });\n"
    "        \n"
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
    "      if(video.videoWidth === 0 && video.videoHeight === 0 && retryCount < 1) {\n"
    "        retryCount++;\n"
    "        log('Video track not loaded - retrying in 2 seconds...');\n"
    "        setTimeout(function() { window.location.reload(); }, 2000);\n"
    "      }\n"
    "    });\n"
    "    video.addEventListener('error', function(e) {\n"
    "      log('Video element error: ' + video.error.message, true);\n"
    "    });\n"
    "    var codecs = [\n"
    "      'video/mp4; codecs=\"avc1.6e0028\"',\n"
    "      'video/mp4; codecs=\"avc1.64001f\"',\n"
    "      'video/mp4; codecs=\"avc1.42E01E\"',\n"
    "      'video/mp4; codecs=\"hev1\"',\n"
    "      'video/mp4; codecs=\"hvc1\"',\n"
    "      'video/mp4; codecs=\"hev1.1.6.L93.B0\"',\n"
    "      'video/mp4; codecs=\"hvc1.1.6.L93.B0\"',\n"
    "      'audio/mp4; codecs=\"mp4a.40.2\"'\n"
    "    ];\n"
    "    codecs.forEach(function(c) {\n"
    "      var supported = MediaSource.isTypeSupported(c);\n"
    "      log('Codec ' + c + ': ' + (supported ? 'SUPPORTED' : 'NOT SUPPORTED'), !supported);\n"
    "    });\n"
    "    var urlParams = new URLSearchParams(window.location.search);\n"
    "    var selectedLang = urlParams.get('lang') || 'en';\n"
    "    log('Pre-selecting audio language: ' + selectedLang);\n"
    "    player.updateSettings({streaming: {initialSettings: {audio: {lang: selectedLang}}}});\n"
    "    log('Initializing player with /media/%S/manifest.mpd');\n"
    "    player.initialize(video, '/media/%S/manifest.mpd', true);\n"
    "    \n"
    "    var videoFile = '%S';\n"
    "    var subtitleFiles = %S;\n"
    "    var loadedSubtitles = [];\n"
    "    subtitleFiles.forEach(function(filename) {\n"
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
  if(dir != 0)
  {
    struct dirent *entry;
    while((entry = readdir(dir)) != 0)
    {
      if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

      String8 name = str8_cstring(entry->d_name);
      String8 entry_path = str8f(scratch.arena, "%S/%S", full_path, name);
      b32 is_dir = os_directory_path_exists(entry_path);

      if(is_dir != 0)
      {
        String8 subdir = (dir_path.size > 0) ? str8f(scratch.arena, "%S/%S", dir_path, name) : name;
        str8_list_pushf(scratch.arena, &html,
          "<li class=\"file-item\"><a href=\"/?dir=%S\">📁 %S/</a></li>\n", subdir, name);
      }
      else
      {
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

////////////////////////////////
//~ HTTP

internal void
handle_manifest(OS_Handle socket, String8 file_path)
{
  Temp scratch = scratch_begin(0, 0);
  String8 full_path = str8f(scratch.arena, "%S/%S", media_root_path, file_path);
  b32 processing = 0;
  CacheInfo cache = get_or_create_cache(scratch.arena, full_path, &processing);

  if(processing != 0)
  {
    send_error_response(socket, HTTP_Status_202_Accepted,
                       str8_lit("Transcoding in progress, please retry in a few seconds"));
    scratch_end(scratch);
    return;
  }

  if(cache.cache_dir.size == 0)
  {
    send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("File not found"));
    scratch_end(scratch);
    return;
  }

  String8 manifest_path = str8f(scratch.arena, "%S/manifest.mpd", cache.cache_dir);
  String8 manifest = read_small_file(scratch.arena, manifest_path, MB(1));
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
  b32 processing = 0;
  CacheInfo cache = get_or_create_cache(scratch.arena, full_path, &processing);

  if(processing != 0)
  {
    send_error_response(socket, HTTP_Status_202_Accepted,
                       str8_lit("Transcoding in progress, please retry in a few seconds"));
    scratch_end(scratch);
    return;
  }

  if(cache.cache_dir.size == 0)
  {
    send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("File not found"));
    scratch_end(scratch);
    return;
  }

  String8 segment_path = str8f(scratch.arena, "%S/%S", cache.cache_dir, segment_name);
  OS_FileProperties props = os_properties_from_file_path(segment_path);
  if(props.size == 0)
  {
    send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("Segment not found"));
    scratch_end(scratch);
    return;
  }

  HTTP_Response *res = http_response_alloc(scratch.arena, HTTP_Status_200_OK);
  http_header_add(scratch.arena, &res->headers, str8_lit("Content-Type"), str8_lit("video/mp4"));
  http_header_add(scratch.arena, &res->headers, str8_lit("Access-Control-Allow-Origin"), str8_lit("*"));
  http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"),
                  str8_from_u64(scratch.arena, props.size, 10, 0, 0));
  res->body = str8_zero();

  socket_write_all(socket, http_response_serialize(scratch.arena, res));
  stream_file_to_socket(socket, segment_path, props.size);

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

    u64 slash_pos = str8_find_needle(media_path, 0, str8_lit("/"), 0);
    if(slash_pos < media_path.size)
    {
      String8 file = str8_prefix(media_path, slash_pos);
      String8 resource = str8_skip(media_path, slash_pos + 1);

      if(str8_match(resource, str8_lit("manifest.mpd"), 0))
      {
        handle_manifest(socket, file);
      }
      else if(str8_find_needle(resource, 0, str8_lit(".vtt"), 0) == resource.size - 4)
      {
        Temp temp = scratch_begin(0, 0);
        String8 file_path = str8f(temp.arena, "%S/%S", media_root_path, file);
        u64 hash = hash_string(file_path);
        String8 cache_dir = str8f(temp.arena, "%S/%llu", cache_root_path, hash);
        String8 vtt_path = str8f(temp.arena, "%S/%S", cache_dir, resource);

        String8 subtitle_content = read_small_file(temp.arena, vtt_path, MB(1));
        if(subtitle_content.size > 0)
        {
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
    else if(str8_find_needle(media_path, 0, str8_lit(".mpd"), 0) == media_path.size - 4)
    {
      handle_manifest(socket, str8_prefix(media_path, media_path.size - 4));
    }
    else if(str8_find_needle(media_path, 0, str8_lit(".vtt"), 0) == media_path.size - 4)
    {
      Temp temp = scratch_begin(0, 0);
      String8 subtitle_path = str8f(temp.arena, "%S/%S", media_root_path, media_path);

      String8 subtitle_content = read_small_file(temp.arena, subtitle_path, MB(1));
      if(subtitle_content.size > 0)
      {
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
//~ Background Worker

internal void
transmux_worker_task(void *params)
{
  (void)params;
  for(;;)
  {
    TransmuxJob *job = transmux_dequeue_job();
    if(job == 0)
    {
      os_sleep_milliseconds(100);
      continue;
    }

    log_infof("media-server: processing transmux for %S\n", job->file_path);

    b32 success = generate_dash_to_cache(job->file_path, job->cache_dir, job->file_hash);
    transmux_mark_complete(job, success);

    if(success != 0)
    {
      log_infof("media-server: transmux complete for %S\n", job->file_path);
    }
    else
    {
      log_errorf("media-server: transmux failed for %S\n", job->file_path);
    }
  }
}

////////////////////////////////
//~ Connection

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

////////////////////////////////
//~ Entry Point

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

  transmux_queue = push_array(arena, TransmuxQueue, 1);
  transmux_queue->mutex = mutex_alloc();
  transmux_queue->arena = arena_alloc();
  transmux_queue->hash_table_size = 256;
  transmux_queue->hash_table = push_array(arena, TransmuxJob *, transmux_queue->hash_table_size);
  transmux_queue->first = 0;
  transmux_queue->last = 0;

  {
    Temp temp = scratch_begin(&arena, 1);
    char *cache_cstr = (char *)push_array(temp.arena, u8, cache_root_path.size + 1);
    MemoryCopy(cache_cstr, cache_root_path.str, cache_root_path.size);
    cache_cstr[cache_root_path.size] = 0;
    mkdir(cache_cstr, 0755);
    scratch_end(temp);
  }

  OS_SystemInfo *sys_info = os_get_system_info();

  u64 logical_cores = sys_info->logical_processor_count;
  if(logical_cores == 0 || logical_cores > 256)
  {
    log_infof("media-server: WARNING: os_get_system_info() returned invalid core count %llu, defaulting to 16\n", logical_cores);
    logical_cores = 16;
  }

  u64 worker_count = 8;
  worker_pool = wp_pool_alloc(arena, worker_count);

  u64 transmux_count = Clamp(2, logical_cores / 8, 8);
  transmux_pool = wp_pool_alloc(arena, transmux_count);
  for(u64 i = 0; i < transmux_count; i += 1)
  {
    wp_submit(transmux_pool, transmux_worker_task, 0, 0);
  }

  log_infof("media-server: port 8080, media root: %S, %llu workers, %llu transmux threads (detected %llu logical cores)\n",
            media_root_path, worker_count, transmux_count, logical_cores);

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
