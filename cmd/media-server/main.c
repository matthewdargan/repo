#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>

#include "base/inc.h"
#include "http/inc.h"
#include "base/inc.c"
#include "http/inc.c"

#include "player.js.inc"

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

typedef struct WatchProgress WatchProgress;
struct WatchProgress
{
  String8 file_path;
  f64 position_seconds;
  u64 last_watched_timestamp;
  String8 subtitle_lang;
  String8 audio_lang;
  WatchProgress *next;
};

typedef struct WatchProgressTable WatchProgressTable;
struct WatchProgressTable
{
  Mutex mutex;
  Arena *arena;
  WatchProgress **buckets;
  u64 bucket_count;
};

typedef struct StreamMeta StreamMeta;
struct StreamMeta
{
  u32 stream_idx;
  u8 is_audio;
  String8 lang;
  String8 metadata;
};

typedef struct StreamMetaArray StreamMetaArray;
struct StreamMetaArray
{
  StreamMeta *items;
  u64 count;
};

////////////////////////////////
//~ Globals

global String8 media_root_path = {0};
global String8 cache_root_path = {0};
global String8 state_root_path = {0};
global WP_Pool *worker_pool = 0;
global WP_Pool *transmux_pool = 0;
global TransmuxQueue *transmux_queue = 0;
global WatchProgressTable *watch_progress = 0;

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
send_response(OS_Handle socket, Arena *arena, String8 content_type, String8 body)
{
  HTTP_Response *res = http_response_alloc(arena, HTTP_Status_200_OK);
  http_header_add(arena, &res->headers, str8_lit("Content-Type"), content_type);
  http_header_add(arena, &res->headers, str8_lit("Access-Control-Allow-Origin"), str8_lit("*"));
  http_header_add(arena, &res->headers, str8_lit("Content-Length"),
                  str8_from_u64(arena, body.size, 10, 0, 0));
  res->body = body;
  socket_write_all(socket, http_response_serialize(arena, res));
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

internal StreamMetaArray
parse_stream_metadata(Arena *arena, String8 cache_dir)
{
  String8 streams_path = str8f(arena, "%S/streams.txt", cache_dir);
  String8 content = read_small_file(arena, streams_path, KB(64));

  StreamMeta *items = push_array(arena, StreamMeta, 32);
  u64 count = 0;

  if(content.size > 0)
  {
    String8List lines = str8_split(arena, content, (u8 *)"\n", 1, 0);
    for(String8Node *n = lines.first; n != 0; n = n->next)
    {
      String8 line = str8_skip_chop_whitespace(n->string);
      if(line.size == 0) continue;

      String8List fields = str8_split(arena, line, (u8 *)"\t", 1, 0);
      if(fields.node_count >= 4)
      {
        String8Node *field = fields.first;
        u32 stream_idx = (u32)u64_from_str8(field->string, 10);
        field = field->next;
        String8 type = field->string;
        field = field->next;
        String8 lang = field->string;
        field = field->next;
        String8 metadata = field->string;

        if(count < 32)
        {
          items[count].stream_idx = stream_idx;
          items[count].is_audio = str8_match(type, str8_lit("audio"), 0);
          items[count].lang = lang;
          items[count].metadata = metadata;
          count++;
        }
      }
    }
  }

  StreamMetaArray result = {items, count};
  return result;
}

////////////////////////////////
//~ Manifest

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
  String8 manifest_path = str8f(scratch.arena, "%S/manifest.mpd.tmp", cache_dir);
  String8 manifest = read_small_file(scratch.arena, manifest_path, MB(1));
  if(manifest.size == 0) { scratch_end(scratch); return; }

  StreamMetaArray streams = parse_stream_metadata(scratch.arena, cache_dir);

  String8 dynamic_attrs[] = {
    str8_lit("minimumUpdatePeriod"),
    str8_lit("suggestedPresentationDelay"),
    str8_lit("availabilityStartTime"),
    str8_lit("publishTime"),
  };

  u8 *out = push_array(scratch.arena, u8, manifest.size * 3);
  u64 out_pos = 0;
  u64 in_pos = 0;

  for(;;)
  {
    u64 newline = str8_find_needle(manifest, in_pos, str8_lit("\n"), 0);
    if(newline >= manifest.size) break;

    String8 line = str8_substr(manifest, rng_1u64(in_pos, newline));
    b32 has_dynamic_attrs = line_contains_any(line, dynamic_attrs, ArrayCount(dynamic_attrs));
    b32 has_type_attr = str8_find_needle(line, 0, str8_lit("type="), 0) < line.size;

    if(!has_dynamic_attrs || has_type_attr)
    {
      b32 is_audio_adaptation = (str8_find_needle(line, 0, str8_lit("<AdaptationSet"), 0) < line.size &&
                                  str8_find_needle(line, 0, str8_lit("contentType=\"audio\""), 0) < line.size);
      u32 audio_stream_id = 0;

      if(is_audio_adaptation)
      {
        u64 id_pos = str8_find_needle(line, 0, str8_lit("id=\""), 0);
        if(id_pos < line.size)
        {
          u64 id_start = id_pos + 4;
          u64 id_end = id_start;
          while(id_end < line.size && line.str[id_end] >= '0' && line.str[id_end] <= '9')
          {
            id_end += 1;
          }
          if(id_end > id_start)
          {
            String8 id_str = str8_substr(line, rng_1u64(id_start, id_end));
            audio_stream_id = (u32)u64_from_str8(id_str, 10);
          }
        }
      }

      for(u64 i = 0; i < line.size; i += 1)
      {
        if(i + 14 <= line.size && MemoryMatch(line.str + i, "type=\"dynamic\"", 14))
        {
          MemoryCopy(out + out_pos, "type=\"static\" ", 14);
          out_pos += 14;
          i += 13;
        }
        else if(i + 13 <= line.size && MemoryMatch(line.str + i, "codecs=\"hev1\"", 13))
        {
          MemoryCopy(out + out_pos, "codecs=\"hev1.1.6.L93.B0\"", 24);
          out_pos += 24;
          i += 12;
        }
        else if(i + 13 <= line.size && MemoryMatch(line.str + i, "codecs=\"hvc1\"", 13))
        {
          MemoryCopy(out + out_pos, "codecs=\"hvc1.1.6.L93.B0\"", 24);
          out_pos += 24;
          i += 12;
        }
        else
        {
          out[out_pos++] = line.str[i];
        }
      }
      out[out_pos++] = '\n';

      if(is_audio_adaptation && audio_stream_id > 0)
      {
        for(u64 i = 0; i < streams.count; i += 1)
        {
          StreamMeta *s = &streams.items[i];
          if(s->is_audio && s->stream_idx == audio_stream_id)
          {
            String8 lang_upper = upper_from_str8(scratch.arena, s->lang);
            String8 label = str8f(scratch.arena, "%S - %S", lang_upper, s->metadata);
            String8 label_line = str8f(scratch.arena, "\t\t\t<Label>%S</Label>\n", label);
            MemoryCopy(out + out_pos, label_line.str, label_line.size);
            out_pos += label_line.size;
            break;
          }
        }
      }
    }
    in_pos = newline + 1;
  }

  if(in_pos < manifest.size)
  {
    u64 remaining = manifest.size - in_pos;
    MemoryCopy(out + out_pos, manifest.str + in_pos, remaining);
    out_pos += remaining;
  }

  String8 path = str8_copy(scratch.arena, manifest_path);
  int fd = open((char *)path.str, O_WRONLY | O_TRUNC);
  if(fd >= 0)
  {
    write(fd, out, out_pos);
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

  String8 in_path = str8_copy(temp.arena, input_path);
  String8 out_path = str8_copy(temp.arena, output_path);

  AVFormatContext *in_ctx = NULL;
  AVCodecContext *dec_ctx = NULL;
  AVFormatContext *out_ctx = NULL;
  AVCodecContext *enc_ctx = NULL;
  AVPacket *pkt = NULL;
  AVPacket *enc_pkt = NULL;

  if(avformat_open_input(&in_ctx, (char *)in_path.str, NULL, NULL) != 0) goto cleanup;
  if(avformat_find_stream_info(in_ctx, NULL) != 0) goto cleanup;

  AVStream *in_stream = in_ctx->streams[stream_idx];
  const AVCodec *dec = avcodec_find_decoder(in_stream->codecpar->codec_id);
  if(dec == 0) goto cleanup;

  dec_ctx = avcodec_alloc_context3(dec);
  if(dec_ctx == 0) goto cleanup;
  if(avcodec_parameters_to_context(dec_ctx, in_stream->codecpar) != 0) goto cleanup;
  if(avcodec_open2(dec_ctx, dec, NULL) != 0) goto cleanup;

  const AVCodec *enc = avcodec_find_encoder_by_name("webvtt");
  if(enc == 0) goto cleanup;

  enc_ctx = avcodec_alloc_context3(enc);
  if(enc_ctx == 0) goto cleanup;

  enc_ctx->time_base = in_stream->time_base;
  const char *webvtt_header = "WEBVTT\n\n";
  enc_ctx->subtitle_header_size = strlen(webvtt_header);
  enc_ctx->subtitle_header = av_malloc(enc_ctx->subtitle_header_size + 1);
  if(enc_ctx->subtitle_header)
  {
    MemoryCopy(enc_ctx->subtitle_header, webvtt_header, enc_ctx->subtitle_header_size + 1);
  }

  if(avcodec_open2(enc_ctx, enc, NULL) != 0) goto cleanup;
  if(avformat_alloc_output_context2(&out_ctx, NULL, "webvtt", (char *)out_path.str) != 0) goto cleanup;

  AVStream *out_stream = avformat_new_stream(out_ctx, NULL);
  if(out_stream == 0) goto cleanup;
  if(avcodec_parameters_from_context(out_stream->codecpar, enc_ctx) != 0) goto cleanup;

  out_stream->time_base = in_stream->time_base;

  if(avio_open(&out_ctx->pb, (char *)out_path.str, AVIO_FLAG_WRITE) != 0) goto cleanup;
  if(avformat_write_header(out_ctx, NULL) != 0) goto cleanup;

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

cleanup:
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

  String8 path = str8_copy(temp.arena, file_path);

  AVFormatContext *fmt = NULL;
  if(avformat_open_input(&fmt, (char *)path.str, NULL, NULL) != 0)
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

  String8List metadata_lines = {0};

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
    AVDictionaryEntry *lang_entry = av_dict_get(stream->metadata, "language", NULL, 0);
    if(lang_entry != 0 && lang_entry->value != 0)
    {
      lang = str8_cstring(lang_entry->value);
    }

    String8 title = str8_zero();
    AVDictionaryEntry *title_entry = av_dict_get(stream->metadata, "title", NULL, 0);
    if(title_entry != 0 && title_entry->value != 0)
    {
      title = str8_cstring(title_entry->value);
    }

    String8 metadata = title.size > 0 ? title : str8_lit("-");
    str8_list_pushf(temp.arena, &metadata_lines, "%u\tsubtitle\t%S\t%S\n", i, lang, metadata);

    String8 vtt_path = str8f(temp.arena, "%S/subtitle.%u.%S.vtt", cache_dir, i, lang);
    extract_subtitle_stream(file_path, i, lang, vtt_path);
  }

  if(metadata_lines.node_count > 0)
  {
    String8 metadata_content = str8_list_join(temp.arena, metadata_lines, 0);
    String8 streams_path = str8f(temp.arena, "%S/streams.txt", cache_dir);
    OS_Handle file = os_file_open(OS_AccessFlag_Write | OS_AccessFlag_Truncate, streams_path);
    if(file.u64[0] != 0)
    {
      os_file_write(file, rng_1u64(0, metadata_content.size), metadata_content.str);
      os_file_close(file);
    }
  }

  avformat_close_input(&fmt);
  scratch_end(temp);
}

////////////////////////////////
//~ DASH

typedef struct StreamTranscodeInfo StreamTranscodeInfo;
struct StreamTranscodeInfo
{
  b32 needs_transcode;
  AVCodecContext *dec_ctx;
  AVCodecContext *enc_ctx;
  SwrContext *swr_ctx;
  AVFrame *dec_frame;
  AVFrame *enc_frame;
  s64 next_pts;
};

internal b32
audio_codec_needs_transcode(enum AVCodecID codec_id)
{
  return (codec_id == AV_CODEC_ID_AC3 ||
          codec_id == AV_CODEC_ID_EAC3 ||
          codec_id == AV_CODEC_ID_DTS ||
          codec_id == AV_CODEC_ID_TRUEHD ||
          codec_id == AV_CODEC_ID_MLP ||
          codec_id == AV_CODEC_ID_PCM_S16LE ||
          codec_id == AV_CODEC_ID_PCM_S24LE ||
          codec_id == AV_CODEC_ID_PCM_S32LE ||
          codec_id == AV_CODEC_ID_PCM_S16BE ||
          codec_id == AV_CODEC_ID_PCM_S24BE ||
          codec_id == AV_CODEC_ID_PCM_S32BE);
}

internal b32
generate_dash(String8 input_path, String8 output_path)
{
  Temp temp = scratch_begin(0, 0);

  String8 in_path = str8_copy(temp.arena, input_path);
  String8 out_path = str8_copy(temp.arena, output_path);

  AVFormatContext *in_ctx = NULL;
  AVFormatContext *out_ctx = NULL;
  AVDictionary *opts = NULL;
  AVPacket *pkt = NULL;
  AVPacket *enc_pkt = NULL;
  b32 success = 0;

  if(avformat_open_input(&in_ctx, (char *)in_path.str, NULL, NULL) != 0) goto cleanup;
  if(avformat_find_stream_info(in_ctx, NULL) != 0) goto cleanup;
  if(avformat_alloc_output_context2(&out_ctx, NULL, "dash", (char *)out_path.str) != 0) goto cleanup;

  int *stream_map = push_array(temp.arena, int, in_ctx->nb_streams);
  StreamTranscodeInfo *transcode_info = push_array(temp.arena, StreamTranscodeInfo, in_ctx->nb_streams);
  String8List audio_metadata = {0};
  for(u32 i = 0; i < in_ctx->nb_streams; i += 1)
  {
    stream_map[i] = -1;
    MemoryZeroStruct(&transcode_info[i]);
  }

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
    if(out_stream == 0) goto cleanup;

    b32 needs_transcode = (codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                           audio_codec_needs_transcode(codecpar->codec_id));

    if(needs_transcode)
    {
      log_infof("media-server: stream %u (audio/%s) - transcoding to AAC\n",
                i, avcodec_get_name(codecpar->codec_id));

      const AVCodec *decoder = avcodec_find_decoder(codecpar->codec_id);
      if(decoder == 0) goto cleanup;

      AVCodecContext *dec_ctx = avcodec_alloc_context3(decoder);
      if(dec_ctx == 0) goto cleanup;
      if(avcodec_parameters_to_context(dec_ctx, codecpar) != 0) goto cleanup;
      if(avcodec_open2(dec_ctx, decoder, NULL) != 0) goto cleanup;

      const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
      if(encoder == 0) goto cleanup;

      AVCodecContext *enc_ctx = avcodec_alloc_context3(encoder);
      if(enc_ctx == 0) goto cleanup;

      enc_ctx->sample_rate = dec_ctx->sample_rate;
      enc_ctx->ch_layout = dec_ctx->ch_layout;
      enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
      enc_ctx->time_base = (AVRational){1, dec_ctx->sample_rate};

      // Set bitrate based on channel count
      if(enc_ctx->ch_layout.nb_channels <= 2)
      {
        enc_ctx->bit_rate = 192000;
      }
      else if(enc_ctx->ch_layout.nb_channels <= 6)
      {
        enc_ctx->bit_rate = 384000;
      }
      else
      {
        enc_ctx->bit_rate = 512000;
      }

      if(avcodec_open2(enc_ctx, encoder, NULL) != 0) goto cleanup;
      if(avcodec_parameters_from_context(out_stream->codecpar, enc_ctx) != 0) goto cleanup;

      out_stream->time_base = enc_ctx->time_base;

      // Set up audio resampler for format/frame size conversion
      SwrContext *swr_ctx = swr_alloc();
      if(swr_ctx == 0) goto cleanup;

      av_opt_set_chlayout(swr_ctx, "in_chlayout", &dec_ctx->ch_layout, 0);
      av_opt_set_chlayout(swr_ctx, "out_chlayout", &enc_ctx->ch_layout, 0);
      av_opt_set_int(swr_ctx, "in_sample_rate", dec_ctx->sample_rate, 0);
      av_opt_set_int(swr_ctx, "out_sample_rate", enc_ctx->sample_rate, 0);
      av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", dec_ctx->sample_fmt, 0);
      av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", enc_ctx->sample_fmt, 0);

      if(swr_init(swr_ctx) != 0) goto cleanup;

      AVFrame *dec_frame = av_frame_alloc();
      AVFrame *enc_frame = av_frame_alloc();
      if(dec_frame == 0 || enc_frame == 0) goto cleanup;

      enc_frame->format = enc_ctx->sample_fmt;
      enc_frame->ch_layout = enc_ctx->ch_layout;
      enc_frame->sample_rate = enc_ctx->sample_rate;
      enc_frame->nb_samples = enc_ctx->frame_size;
      if(av_frame_get_buffer(enc_frame, 0) != 0) goto cleanup;

      transcode_info[i].needs_transcode = 1;
      transcode_info[i].dec_ctx = dec_ctx;
      transcode_info[i].enc_ctx = enc_ctx;
      transcode_info[i].swr_ctx = swr_ctx;
      transcode_info[i].dec_frame = dec_frame;
      transcode_info[i].enc_frame = enc_frame;
      transcode_info[i].next_pts = 0;
    }
    else
    {
      if(codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
      {
        log_infof("media-server: stream %u (video/%s) - copying\n",
                  i, avcodec_get_name(codecpar->codec_id));
      }
      else
      {
        log_infof("media-server: stream %u (audio/%s) - copying\n",
                  i, avcodec_get_name(codecpar->codec_id));
      }

      if(avcodec_parameters_copy(out_stream->codecpar, codecpar) != 0) goto cleanup;
      out_stream->codecpar->codec_tag = 0;
      out_stream->time_base = in_stream->time_base;
    }

    av_dict_copy(&out_stream->metadata, in_stream->metadata, 0);

    if(codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
    {
      String8 lang = str8_lit("und");
      AVDictionaryEntry *lang_entry = av_dict_get(in_stream->metadata, "language", NULL, 0);
      if(lang_entry != 0 && lang_entry->value != 0)
      {
        lang = str8_cstring(lang_entry->value);
      }

      String8 title = str8_zero();
      AVDictionaryEntry *title_entry = av_dict_get(in_stream->metadata, "title", NULL, 0);
      if(title_entry != 0 && title_entry->value != 0)
      {
        title = str8_cstring(title_entry->value);
      }

      String8 channels_str = str8_zero();
      int ch_count = needs_transcode ? transcode_info[i].enc_ctx->ch_layout.nb_channels : codecpar->ch_layout.nb_channels;
      if(ch_count == 1) channels_str = str8_lit("Mono");
      else if(ch_count == 2) channels_str = str8_lit("Stereo");
      else if(ch_count == 6) channels_str = str8_lit("5.1");
      else if(ch_count == 8) channels_str = str8_lit("7.1");
      else channels_str = str8f(temp.arena, "%dch", ch_count);

      String8 metadata = str8_zero();
      if(title.size > 0)
      {
        metadata = str8f(temp.arena, "%S (%S)", channels_str, title);
      }
      else
      {
        metadata = channels_str;
      }

      str8_list_pushf(temp.arena, &audio_metadata, "%u\taudio\t%S\t%S\n", stream_idx, lang, metadata);
    }

    stream_map[i] = stream_idx;
    stream_idx += 1;
  }

  av_dict_set(&opts, "seg_duration", "4", 0);
  av_dict_set(&opts, "use_timeline", "1", 0);
  av_dict_set(&opts, "use_template", "1", 0);
  av_dict_set(&opts, "single_file", "0", 0);
  av_dict_set(&opts, "streaming", "0", 0);

  if(avformat_write_header(out_ctx, &opts) != 0) goto cleanup;
  av_dict_free(&opts);
  opts = NULL;

  pkt = av_packet_alloc();
  enc_pkt = av_packet_alloc();
  if(pkt != 0 && enc_pkt != 0)
  {
    while(av_read_frame(in_ctx, pkt) >= 0)
    {
      if(stream_map[pkt->stream_index] >= 0)
      {
        u32 in_idx = pkt->stream_index;
        u32 out_idx = stream_map[in_idx];
        AVStream *in_stream = in_ctx->streams[in_idx];
        AVStream *out_stream = out_ctx->streams[out_idx];

        if(transcode_info[in_idx].needs_transcode)
        {
          AVCodecContext *dec_ctx = transcode_info[in_idx].dec_ctx;
          AVCodecContext *enc_ctx = transcode_info[in_idx].enc_ctx;
          SwrContext *swr_ctx = transcode_info[in_idx].swr_ctx;
          AVFrame *dec_frame = transcode_info[in_idx].dec_frame;
          AVFrame *enc_frame = transcode_info[in_idx].enc_frame;
          s64 *next_pts = &transcode_info[in_idx].next_pts;

          if(avcodec_send_packet(dec_ctx, pkt) >= 0)
          {
            while(avcodec_receive_frame(dec_ctx, dec_frame) >= 0)
            {
              // Convert samples to resampler, may not produce output immediately
              swr_convert(swr_ctx, NULL, 0,
                         (const uint8_t **)dec_frame->data, dec_frame->nb_samples);

              // Pull out as many complete frames as available
              while(swr_get_out_samples(swr_ctx, 0) >= enc_ctx->frame_size)
              {
                int samples = swr_convert(swr_ctx,
                                         enc_frame->data, enc_frame->nb_samples,
                                         NULL, 0);

                if(samples > 0)
                {
                  enc_frame->pts = *next_pts;
                  *next_pts += samples;

                  if(avcodec_send_frame(enc_ctx, enc_frame) >= 0)
                  {
                    while(avcodec_receive_packet(enc_ctx, enc_pkt) >= 0)
                    {
                      enc_pkt->stream_index = out_idx;
                      av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_stream->time_base);
                      enc_pkt->pos = -1;

                      if(av_interleaved_write_frame(out_ctx, enc_pkt) != 0) break;
                      av_packet_unref(enc_pkt);
                    }
                  }
                }
                else
                {
                  break;
                }
              }
              av_frame_unref(dec_frame);
            }
          }
        }
        else
        {
          pkt->stream_index = out_idx;
          av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);
          pkt->pos = -1;

          if(av_interleaved_write_frame(out_ctx, pkt) != 0) break;
        }
      }
      av_packet_unref(pkt);
    }

    for(u32 i = 0; i < in_ctx->nb_streams; i += 1)
    {
      if(transcode_info[i].needs_transcode && stream_map[i] >= 0)
      {
        AVCodecContext *enc_ctx = transcode_info[i].enc_ctx;
        SwrContext *swr_ctx = transcode_info[i].swr_ctx;
        AVFrame *enc_frame = transcode_info[i].enc_frame;
        s64 *next_pts = &transcode_info[i].next_pts;
        u32 out_idx = stream_map[i];
        AVStream *out_stream = out_ctx->streams[out_idx];

        while(swr_get_out_samples(swr_ctx, 0) > 0)
        {
          int samples = swr_convert(swr_ctx,
                                   enc_frame->data, enc_frame->nb_samples,
                                   NULL, 0);
          if(samples > 0)
          {
            enc_frame->pts = *next_pts;
            *next_pts += samples;

            if(avcodec_send_frame(enc_ctx, enc_frame) >= 0)
            {
              while(avcodec_receive_packet(enc_ctx, enc_pkt) >= 0)
              {
                enc_pkt->stream_index = out_idx;
                av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_stream->time_base);
                enc_pkt->pos = -1;

                av_interleaved_write_frame(out_ctx, enc_pkt);
                av_packet_unref(enc_pkt);
              }
            }
          }
          else
          {
            break;
          }
        }

        avcodec_send_frame(enc_ctx, NULL);
        while(avcodec_receive_packet(enc_ctx, enc_pkt) >= 0)
        {
          enc_pkt->stream_index = out_idx;
          av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_stream->time_base);
          enc_pkt->pos = -1;

          av_interleaved_write_frame(out_ctx, enc_pkt);
          av_packet_unref(enc_pkt);
        }
      }
    }

    av_write_trailer(out_ctx);
    success = 1;

    if(audio_metadata.node_count > 0 && success)
    {
      String8 cache_dir = str8_chop_last_slash(output_path);
      String8 streams_path = str8f(temp.arena, "%S/streams.txt", cache_dir);
      String8 audio_meta_content = str8_list_join(temp.arena, audio_metadata, 0);

      // Append to streams file (subtitle metadata may have been written first)
      OS_Handle meta_file = os_file_open(OS_AccessFlag_Write | OS_AccessFlag_Append, streams_path);
      if(meta_file.u64[0] != 0)
      {
        os_file_write(meta_file, rng_1u64(0, audio_meta_content.size), audio_meta_content.str);
        os_file_close(meta_file);
      }
    }
  }

cleanup:
  for(u32 i = 0; i < in_ctx->nb_streams; i += 1)
  {
    if(transcode_info[i].needs_transcode)
    {
      if(transcode_info[i].enc_frame != 0) av_frame_free(&transcode_info[i].enc_frame);
      if(transcode_info[i].dec_frame != 0) av_frame_free(&transcode_info[i].dec_frame);
      if(transcode_info[i].swr_ctx != 0) swr_free(&transcode_info[i].swr_ctx);
      if(transcode_info[i].enc_ctx != 0) avcodec_free_context(&transcode_info[i].enc_ctx);
      if(transcode_info[i].dec_ctx != 0) avcodec_free_context(&transcode_info[i].dec_ctx);
    }
  }
  if(enc_pkt != 0) av_packet_free(&enc_pkt);
  if(pkt != 0) av_packet_free(&pkt);
  if(opts != 0) av_dict_free(&opts);
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

  String8 manifest_temp = str8f(scratch.arena, "%S/manifest.mpd.tmp", cache_dir);
  String8 manifest_final = str8f(scratch.arena, "%S/manifest.mpd", cache_dir);

  log_infof("media-server: generating DASH for %S\n", file_path);

  if(!generate_dash(file_path, manifest_temp))
  {
    log_errorf("media-server: DASH generation failed for %S\n", file_path);
    scratch_end(scratch);
    return 0;
  }

  log_infof("media-server: DASH generation complete for %S\n", file_path);

  postprocess_manifest(cache_dir);

  char *temp_cstr = (char *)push_array(scratch.arena, u8, manifest_temp.size + 1);
  MemoryCopy(temp_cstr, manifest_temp.str, manifest_temp.size);
  temp_cstr[manifest_temp.size] = 0;

  char *final_cstr = (char *)push_array(scratch.arena, u8, manifest_final.size + 1);
  MemoryCopy(final_cstr, manifest_final.str, manifest_final.size);
  final_cstr[manifest_final.size] = 0;

  rename(temp_cstr, final_cstr);

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
//~ URL Encoding/Decoding

internal u8
hex_to_byte(u8 h)
{
  if(h >= '0' && h <= '9') return h - '0';
  if(h >= 'A' && h <= 'F') return h - 'A' + 10;
  if(h >= 'a' && h <= 'f') return h - 'a' + 10;
  return 0;
}

internal String8
url_decode(Arena *arena, String8 str)
{
  u8 *result = push_array(arena, u8, str.size);
  u64 result_size = 0;

  for(u64 i = 0; i < str.size; i += 1)
  {
    if(str.str[i] == '%' && i + 2 < str.size)
    {
      u8 high = hex_to_byte(str.str[i + 1]);
      u8 low = hex_to_byte(str.str[i + 2]);
      result[result_size++] = (high << 4) | low;
      i += 2;
    }
    else if(str.str[i] == '+')
    {
      result[result_size++] = ' ';
    }
    else
    {
      result[result_size++] = str.str[i];
    }
  }

  return str8(result, result_size);
}

internal String8
url_encode(Arena *arena, String8 str)
{
  String8List parts = {0};
  u8 hex[] = "0123456789ABCDEF";

  for(u64 i = 0; i < str.size; i += 1)
  {
    u8 c = str.str[i];
    // Unreserved characters (RFC 3986)
    if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
       (c >= '0' && c <= '9') || c == '-' || c == '_' ||
       c == '.' || c == '~' || c == '/')
    {
      str8_list_push(arena, &parts, str8(&str.str[i], 1));
    }
    else
    {
      u8 *encoded = push_array(arena, u8, 3);
      encoded[0] = '%';
      encoded[1] = hex[(c >> 4) & 0xF];
      encoded[2] = hex[c & 0xF];
      str8_list_push(arena, &parts, str8(encoded, 3));
    }
  }

  return str8_list_join(arena, parts, 0);
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
handle_player(OS_Handle socket, String8 file_param, Arena *arena)
{
  String8 video_path = str8f(arena, "%S/%S", media_root_path, file_param);
  String8 cache_dir = get_cache_dir(arena, video_path);

  String8 encoded_file = url_encode(arena, file_param);
  String8 placeholder = str8_lit("{{FILE_PARAM}}");
  u64 pos1 = str8_find_needle(player_js_template, 0, placeholder, 0);
  u64 pos2 = str8_find_needle(player_js_template, pos1 + placeholder.size, placeholder, 0);

  String8List html = {0};
  str8_list_push(arena, &html, str8f(arena,
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "  <meta charset=\"UTF-8\">\n"
    "  <title>%S</title>\n"
    "  <meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
    "  <script>\n"
    "@@DASHJS@@\n"
    "  </script>\n"
    "  <style>\n"
    "    * { margin: 0; padding: 0; box-sizing: border-box; }\n"
    "    body {\n"
    "      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;\n"
    "      background: #000;\n"
    "      color: #fff;\n"
    "      line-height: 1.5;\n"
    "    }\n"
    "    header {\n"
    "      background: #111;\n"
    "      border-bottom: 1px solid #333;\n"
    "      padding: 1rem 2rem;\n"
    "      display: flex;\n"
    "      justify-content: space-between;\n"
    "      align-items: center;\n"
    "    }\n"
    "    header h1 {\n"
    "      font-size: 1.25rem;\n"
    "      font-weight: 500;\n"
    "      color: #fff;\n"
    "    }\n"
    "    header .back {\n"
    "      color: #6699cc;\n"
    "      text-decoration: none;\n"
    "      font-size: 0.9rem;\n"
    "    }\n"
    "    header .back:hover {\n"
    "      text-decoration: underline;\n"
    "    }\n"
    "    main {\n"
    "      padding: 2rem;\n"
    "      max-width: 1600px;\n"
    "      margin: 0 auto;\n"
    "    }\n"
    "    .title {\n"
    "      margin-bottom: 1rem;\n"
    "      font-size: 1.1rem;\n"
    "      color: #fff;\n"
    "      word-break: break-word;\n"
    "    }\n"
    "    #status {\n"
    "      background: #1a1a1a;\n"
    "      border: 1px solid #333;\n"
    "      border-radius: 4px;\n"
    "      padding: 1.5rem;\n"
    "      text-align: center;\n"
    "      margin-bottom: 1rem;\n"
    "    }\n"
    "    #status.transmuxing {\n"
    "      border-color: #cc9966;\n"
    "      color: #cc9966;\n"
    "    }\n"
    "    #status.error {\n"
    "      border-color: #cc6666;\n"
    "      color: #cc6666;\n"
    "    }\n"
    "    .spinner {\n"
    "      display: inline-block;\n"
    "      width: 1rem;\n"
    "      height: 1rem;\n"
    "      border: 2px solid #333;\n"
    "      border-top-color: #cc9966;\n"
    "      border-radius: 50%%;\n"
    "      animation: spin 0.8s linear infinite;\n"
    "      margin-right: 0.5rem;\n"
    "      vertical-align: middle;\n"
    "    }\n"
    "    @keyframes spin {\n"
    "      to { transform: rotate(360deg); }\n"
    "    }\n"
    "    #player-container {\n"
    "      display: none;\n"
    "    }\n"
    "    #player-container.ready {\n"
    "      display: block;\n"
    "    }\n"
    "    video {\n"
    "      width: 100%%;\n"
    "      background: #000;\n"
    "      display: block;\n"
    "    }\n"
    "  </style>\n"
    "</head>\n"
    "<body>\n"
    "  <header>\n"
    "    <h1>Media Library</h1>\n"
    "    <a href=\"/\" class=\"back\">\u2190 Back to Library</a>\n"
    "  </header>\n"
    "  <main>\n"
    "    <div class=\"title\">%S</div>\n"
    "    <div id=\"status\" class=\"transmuxing\">\n"
    "      <div class=\"spinner\"></div>\n"
    "      <span id=\"status-text\">Checking manifest...</span>\n"
    "    </div>\n"
    "    <div id=\"player-container\">\n"
    "      <video id=\"video\" controls></video>\n"
    "    </div>\n"
    "  </main>\n"
    "  <script>\n",
    file_param, file_param));

  str8_list_push(arena, &html, str8_prefix(player_js_template, pos1));
  str8_list_push(arena, &html, encoded_file);
  str8_list_push(arena, &html, str8_substr(player_js_template, rng_1u64(pos1 + placeholder.size, pos2)));
  str8_list_push(arena, &html, encoded_file);
  str8_list_push(arena, &html, str8_skip(player_js_template, pos2 + placeholder.size));

  str8_list_push(arena, &html, str8_lit(
    "  </script>\n"
    "</body>\n"
    "</html>\n"));

  send_response(socket, arena, str8_lit("text/html"), str8_list_join(arena, html, 0));
}

typedef struct EpisodeInfo EpisodeInfo;
struct EpisodeInfo
{
  b32 is_episode;
  u32 season;
  u32 episode;
  String8 dir_path;
  String8 filename;
};

internal EpisodeInfo
parse_episode_info(String8 file_path)
{
  EpisodeInfo info = {0};
  info.dir_path = str8_chop_last_slash(file_path);
  info.filename = str8_skip_last_slash(file_path);

  if(info.filename.size >= 5)
  {
      for(u64 i = 0; i <= info.filename.size - 5; i += 1)
      {
        if((info.filename.str[i] == 'S' || info.filename.str[i] == 's') &&
           char_is_digit(info.filename.str[i + 1], 10) &&
           char_is_digit(info.filename.str[i + 2], 10))
        {
          u64 e_pos = i + 3;
          if(e_pos < info.filename.size &&
             (info.filename.str[e_pos] == 'E' || info.filename.str[e_pos] == 'e'))
          {
            info.season = (info.filename.str[i + 1] - '0') * 10 + (info.filename.str[i + 2] - '0');

            u64 ep_start = e_pos + 1;
            u64 ep_end = ep_start;
            while(ep_end < info.filename.size && char_is_digit(info.filename.str[ep_end], 10))
            {
              ep_end += 1;
            }

            if(ep_end > ep_start)
            {
              String8 ep_str = str8_substr(info.filename, rng_1u64(ep_start, ep_end));
              info.episode = (u32)u64_from_str8(ep_str, 10);
              info.is_episode = 1;
              break;
            }
          }
        }
      }
    }

  return info;
}

internal String8
find_next_episode(Arena *arena, String8 file_path)
{
  String8 result = str8_zero();
  EpisodeInfo current = parse_episode_info(file_path);

  if(!current.is_episode) return result;

  Temp temp = scratch_begin(&arena, 1);
  String8 dir_path = str8f(temp.arena, "%S/%S", media_root_path, current.dir_path);
  String8 dir_path_cstr = str8_copy(temp.arena, dir_path);

  u32 next_episode = current.episode + 1;
  DIR *dir = opendir((char *)dir_path_cstr.str);
  if(dir != 0)
  {
    struct dirent *entry;
    while((entry = readdir(dir)) != 0)
    {
      String8 filename = str8_cstring(entry->d_name);
      String8 full_path = str8f(temp.arena, "%S/%S", current.dir_path, filename);
      EpisodeInfo info = parse_episode_info(full_path);

      if(info.is_episode && info.season == current.season && info.episode == next_episode)
      {
        result = str8_copy(arena, full_path);
        break;
      }
    }
    closedir(dir);
  }

  scratch_end(temp);
  return result;
}

internal WatchProgress **
watch_progress_get_sorted(Arena *arena, u64 *out_count)
{
  Temp temp = scratch_begin(&arena, 1);
  WatchProgress **all_entries = push_array(temp.arena, WatchProgress *, 1024);
  u64 count = 0;

  MutexScope(watch_progress->mutex)
  {
    for(u64 i = 0; i < watch_progress->bucket_count; i += 1)
    {
      for(WatchProgress *entry = watch_progress->buckets[i]; entry != 0; entry = entry->next)
      {
        if(count < 1024)
        {
          all_entries[count++] = entry;
        }
      }
    }
  }

  for(u64 i = 0; i < count; i += 1)
  {
    for(u64 j = i + 1; j < count; j += 1)
    {
      if(all_entries[j]->last_watched_timestamp > all_entries[i]->last_watched_timestamp)
      {
        WatchProgress *tmp = all_entries[i];
        all_entries[i] = all_entries[j];
        all_entries[j] = tmp;
      }
    }
  }

  WatchProgress **result = push_array(arena, WatchProgress *, count);
  MemoryCopy(result, all_entries, sizeof(WatchProgress *) * count);
  *out_count = count;

  scratch_end(temp);
  return result;
}

internal void
handle_directory_listing(OS_Handle socket, String8 dir_path, Arena *arena)
{
  String8 full_path = (dir_path.size > 0) ? str8f(arena, "%S/%S", media_root_path, dir_path) : media_root_path;
  String8 path = str8_copy(arena, full_path);

  String8List html = {0};
  str8_list_push(arena, &html, str8_lit(
    "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n"
    "<title>Media Library</title>\n"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
    "<style>\n"
    "* { margin: 0; padding: 0; box-sizing: border-box; }\n"
    "body {\n"
    "  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;\n"
    "  background: #000;\n"
    "  color: #fff;\n"
    "  line-height: 1.5;\n"
    "}\n"
    "header {\n"
    "  background: #111;\n"
    "  border-bottom: 1px solid #333;\n"
    "  padding: 1rem 2rem;\n"
    "  display: flex;\n"
    "  justify-content: space-between;\n"
    "  align-items: center;\n"
    "}\n"
    "header h1 {\n"
    "  font-size: 1.25rem;\n"
    "  font-weight: 500;\n"
    "  color: #fff;\n"
    "}\n"
    "header .user-info {\n"
    "  font-size: 0.9rem;\n"
    "  color: #888;\n"
    "}\n"
    "main {\n"
    "  padding: 2rem;\n"
    "  max-width: 1400px;\n"
    "  margin: 0 auto;\n"
    "}\n"
    ".breadcrumb {\n"
    "  margin-bottom: 1.5rem;\n"
    "  font-size: 0.9rem;\n"
    "  color: #888;\n"
    "}\n"
    ".breadcrumb a {\n"
    "  color: #6699cc;\n"
    "  text-decoration: none;\n"
    "}\n"
    ".breadcrumb a:hover {\n"
    "  text-decoration: underline;\n"
    "}\n"
    "table {\n"
    "  width: 100%%;\n"
    "  border-collapse: collapse;\n"
    "}\n"
    "thead {\n"
    "  border-bottom: 1px solid #333;\n"
    "}\n"
    "th {\n"
    "  text-align: left;\n"
    "  padding: 0.75rem 1rem;\n"
    "  font-weight: 500;\n"
    "  font-size: 0.85rem;\n"
    "  color: #888;\n"
    "  text-transform: uppercase;\n"
    "  letter-spacing: 0.05em;\n"
    "}\n"
    "tbody tr {\n"
    "  border-bottom: 1px solid #222;\n"
    "}\n"
    "tbody tr:hover {\n"
    "  background: #111;\n"
    "}\n"
    "td {\n"
    "  padding: 0.75rem 1rem;\n"
    "}\n"
    "td a {\n"
    "  color: #fff;\n"
    "  text-decoration: none;\n"
    "  display: flex;\n"
    "  align-items: center;\n"
    "  gap: 0.5rem;\n"
    "}\n"
    "td a:hover {\n"
    "  color: #6699cc;\n"
    "}\n"
    ".icon {\n"
    "  width: 1.25rem;\n"
    "  text-align: center;\n"
    "  font-size: 1rem;\n"
    "  opacity: 0.6;\n"
    "}\n"
    ".type {\n"
    "  color: #666;\n"
    "  font-size: 0.85rem;\n"
    "  text-transform: uppercase;\n"
    "}\n"
    "</style>\n</head>\n<body>\n"
    "<header>\n"
    "  <h1>Media Library</h1>\n"
    "  <div class=\"user-info\"></div>\n"
    "</header>\n"
    "<main>\n"));

  if(dir_path.size == 0 && watch_progress != 0)
  {
    u64 watch_count = 0;
    WatchProgress **recent = watch_progress_get_sorted(arena, &watch_count);

    if(watch_count > 0)
    {
      str8_list_push(arena, &html, str8_lit(
        "<section style=\"margin-bottom: 2rem;\">\n"
        "<h2 style=\"font-size: 1.1rem; margin-bottom: 1rem; color: #888;\">Continue Watching</h2>\n"));

      u64 show_count = Min(watch_count, 10);
      for(u64 i = 0; i < show_count; i += 1)
      {
        WatchProgress *entry = recent[i];
        String8 encoded_file = url_encode(arena, entry->file_path);

        String8 dir_path = str8_chop_last_slash(entry->file_path);
        String8 filename = str8_skip_last_slash(entry->file_path);

        String8 card_html = str8f(arena,
          "<div style=\"background: #111; padding: 0.75rem 1rem; margin-bottom: 0.5rem; border-radius: 4px;\">\n"
          "  <div style=\"display: flex; justify-content: space-between; align-items: center;\">\n"
          "    <div style=\"flex: 1;\">\n"
          "      <div style=\"font-size: 0.95rem; margin-bottom: 0.25rem;\">%S</div>\n"
          "      <div style=\"font-size: 0.8rem; color: #666;\">%S</div>\n"
          "    </div>\n"
          "    <div style=\"display: flex; gap: 0.5rem;\">\n"
          "      <a href=\"/player?file=%S\" style=\"background: #6699cc; color: #fff; padding: 0.5rem 1rem; border-radius: 4px; text-decoration: none; font-size: 0.85rem;\">Resume</a>\n",
          filename, dir_path, encoded_file);
        str8_list_push(arena, &html, card_html);

        String8 next_episode = find_next_episode(arena, entry->file_path);
        if(next_episode.size > 0)
        {
          String8 encoded_next = url_encode(arena, next_episode);
          str8_list_pushf(arena, &html,
            "      <a href=\"/player?file=%S\" style=\"background: #333; color: #fff; padding: 0.5rem 1rem; border-radius: 4px; text-decoration: none; font-size: 0.85rem;\">Next Episode</a>\n",
            encoded_next);
        }

        str8_list_pushf(arena, &html,
          "      <button onclick=\"deleteProgress('%S')\" style=\"background: #333; color: #d33; padding: 0.25rem 0.5rem; border: none; border-radius: 2px; cursor: pointer; font-size: 1rem; line-height: 1;\" title=\"Remove from list\">\u00D7</button>\n"
          "    </div>\n"
          "  </div>\n"
          "</div>\n",
          encoded_file);
      }

      str8_list_push(arena, &html, str8_lit("</section>\n"));
    }
  }

  str8_list_push(arena, &html, str8_lit(
    "<table>\n"
    "<thead><tr><th>Name</th><th>Type</th></tr></thead>\n"
    "<tbody>\n"));

  DIR *dir = opendir((char *)path.str);
  if(dir != 0)
  {
    struct dirent *entry;
    while((entry = readdir(dir)) != 0)
    {
      if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

      String8 name = str8_cstring(entry->d_name);
      String8 entry_path = str8f(arena, "%S/%S", full_path, name);
      b32 is_dir = os_directory_path_exists(entry_path);

      if(is_dir != 0)
      {
        String8 subdir = (dir_path.size > 0) ? str8f(arena, "%S/%S", dir_path, name) : name;
        String8 encoded_subdir = url_encode(arena, subdir);
        str8_list_pushf(arena, &html,
          "<tr><td><a href=\"/?dir=%S\"><span class=\"icon\">\u25B8</span>%S/</a></td><td class=\"type\">folder</td></tr>\n",
          encoded_subdir, name);
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
            String8 file = (dir_path.size > 0) ? str8f(arena, "%S/%S", dir_path, name) : name;
            String8 encoded_file = url_encode(arena, file);
            String8 ext_upper = str8_skip(name, name.size - 3);
            str8_list_pushf(arena, &html,
              "<tr><td><a href=\"/player?file=%S\"><span class=\"icon\">\u25B6</span>%S</a></td><td class=\"type\">%S</td></tr>\n",
              encoded_file, name, ext_upper);
          }
        }
      }
    }
    closedir(dir);
  }

  str8_list_push(arena, &html, str8_lit(
    "</tbody>\n</table>\n</main>\n"
    "<script>\n"
    "function deleteProgress(file) {\n"
    "  if(confirm('Remove this item from Continue Watching?')) {\n"
    "    fetch('/api/progress?file=' + file + '&delete=1')\n"
    "      .then(function() { location.reload(); })\n"
    "      .catch(function() { alert('Failed to delete'); });\n"
    "  }\n"
    "}\n"
    "</script>\n"
    "</body>\n</html>\n"));
  String8 html_str = str8_list_join(arena, html, 0);
  send_response(socket, arena, str8_lit("text/html"), html_str);
}

////////////////////////////////
//~ Watch Progress

internal void
watch_progress_load(void)
{
  Temp scratch = scratch_begin(0, 0);
  String8 progress_path = str8f(scratch.arena, "%S/watch-progress.txt", state_root_path);
  String8 content = read_small_file(scratch.arena, progress_path, MB(1));

  if(content.size > 0)
  {
    String8List lines = str8_split(scratch.arena, content, (u8 *)"\n", 1, 0);
    for(String8Node *n = lines.first; n != 0; n = n->next)
    {
      String8 line = str8_skip_chop_whitespace(n->string);
      if(line.size == 0) continue;

      String8List fields = str8_split(scratch.arena, line, (u8 *)"\t", 1, 0);
      if(fields.node_count >= 5)
      {
        String8Node *field_node = fields.first;
        String8 file = field_node->string; field_node = field_node->next;
        String8 pos_str = field_node->string; field_node = field_node->next;
        String8 time_str = field_node->string; field_node = field_node->next;
        String8 sub_str = field_node->string; field_node = field_node->next;
        String8 aud_str = field_node->string;

        f64 position = f64_from_str8(pos_str);
        u64 timestamp = u64_from_str8(time_str, 10);
        String8 subtitle_lang = str8_match(sub_str, str8_lit("-"), 0) ? str8_zero() : sub_str;
        String8 audio_lang = str8_match(aud_str, str8_lit("-"), 0) ? str8_zero() : aud_str;

        u64 hash = hash_string(file) % watch_progress->bucket_count;
        WatchProgress *entry = push_array(watch_progress->arena, WatchProgress, 1);
        entry->file_path = str8_copy(watch_progress->arena, file);
        entry->position_seconds = position;
        entry->last_watched_timestamp = timestamp;
        entry->subtitle_lang = str8_copy(watch_progress->arena, subtitle_lang);
        entry->audio_lang = str8_copy(watch_progress->arena, audio_lang);
        entry->next = watch_progress->buckets[hash];
        watch_progress->buckets[hash] = entry;
      }
    }
  }
  scratch_end(scratch);
}

internal void
watch_progress_save(void)
{
  Temp scratch = scratch_begin(0, 0);
  String8 progress_path = str8f(scratch.arena, "%S/watch-progress.txt", state_root_path);
  String8List lines = {0};

  MutexScope(watch_progress->mutex)
  {
    for(u64 i = 0; i < watch_progress->bucket_count; i += 1)
    {
      for(WatchProgress *entry = watch_progress->buckets[i]; entry != 0; entry = entry->next)
      {
        String8 sub = entry->subtitle_lang.size > 0 ? entry->subtitle_lang : str8_lit("-");
        String8 aud = entry->audio_lang.size > 0 ? entry->audio_lang : str8_lit("-");
        str8_list_pushf(scratch.arena, &lines, "%S\t%.2f\t%llu\t%S\t%S\n",
                       entry->file_path, entry->position_seconds, entry->last_watched_timestamp, sub, aud);
      }
    }
  }

  String8 content = str8_list_join(scratch.arena, lines, 0);
  OS_Handle file = os_file_open(OS_AccessFlag_Write | OS_AccessFlag_Truncate, progress_path);
  if(file.u64[0] != 0)
  {
    os_file_write(file, rng_1u64(0, content.size), content.str);
    os_file_close(file);
  }
  scratch_end(scratch);
}

internal WatchProgress *
watch_progress_find(String8 file)
{
  WatchProgress *result = 0;
  u64 hash = hash_string(file) % watch_progress->bucket_count;

  MutexScope(watch_progress->mutex)
  {
    for(WatchProgress *entry = watch_progress->buckets[hash]; entry != 0; entry = entry->next)
    {
      if(str8_match(entry->file_path, file, 0))
      {
        result = entry;
        break;
      }
    }
  }
  return result;
}

internal void
watch_progress_delete(String8 file)
{
  u64 hash = hash_string(file) % watch_progress->bucket_count;

  MutexScope(watch_progress->mutex)
  {
    WatchProgress **prev_ptr = &watch_progress->buckets[hash];
    WatchProgress *entry = *prev_ptr;
    while(entry != 0)
    {
      if(str8_match(entry->file_path, file, 0))
      {
        *prev_ptr = entry->next;
        break;
      }
      prev_ptr = &entry->next;
      entry = entry->next;
    }
  }

  watch_progress_save();
}

internal void
watch_progress_set(String8 file, f64 position, String8 subtitle_lang, String8 audio_lang)
{
  Temp scratch = scratch_begin(0, 0);
  EpisodeInfo current_ep = parse_episode_info(file);
  u64 hash = hash_string(file) % watch_progress->bucket_count;

  MutexScope(watch_progress->mutex)
  {
    WatchProgress *entry = 0;
    for(WatchProgress *e = watch_progress->buckets[hash]; e != 0; e = e->next)
    {
      if(str8_match(e->file_path, file, 0))
      {
        entry = e;
        break;
      }
    }

    if(entry == 0)
    {
      entry = push_array(watch_progress->arena, WatchProgress, 1);
      entry->file_path = str8_copy(watch_progress->arena, file);
      entry->next = watch_progress->buckets[hash];
      watch_progress->buckets[hash] = entry;
    }

    entry->position_seconds = position;
    entry->last_watched_timestamp = os_now_microseconds() / 1000000;
    entry->subtitle_lang = str8_copy(watch_progress->arena, subtitle_lang);
    entry->audio_lang = str8_copy(watch_progress->arena, audio_lang);

    // Clean up earlier episodes in the same season
    if(current_ep.is_episode)
    {
      for(u64 i = 0; i < watch_progress->bucket_count; i += 1)
      {
        WatchProgress **prev_ptr = &watch_progress->buckets[i];
        WatchProgress *e = *prev_ptr;
        while(e != 0)
        {
          EpisodeInfo other_ep = parse_episode_info(e->file_path);
          b32 should_remove = (other_ep.is_episode &&
                               str8_match(other_ep.dir_path, current_ep.dir_path, 0) &&
                               other_ep.season == current_ep.season &&
                               other_ep.episode < current_ep.episode);
          if(should_remove)
          {
            *prev_ptr = e->next;
            e = *prev_ptr;
          }
          else
          {
            prev_ptr = &e->next;
            e = e->next;
          }
        }
      }
    }
  }

  scratch_end(scratch);
  watch_progress_save();
}

////////////////////////////////
//~ HTTP

internal void
handle_manifest(OS_Handle socket, String8 file_path, Arena *arena)
{
  String8 full_path = str8f(arena, "%S/%S", media_root_path, file_path);
  b32 processing = 0;
  CacheInfo cache = get_or_create_cache(arena, full_path, &processing);

  if(processing != 0)
  {
    send_error_response(socket, HTTP_Status_202_Accepted,
                       str8_lit("Transcoding in progress, please retry in a few seconds"));
    return;
  }

  if(cache.cache_dir.size == 0)
  {
    send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("File not found"));
    return;
  }

  String8 manifest_path = str8f(arena, "%S/manifest.mpd", cache.cache_dir);
  String8 manifest = read_small_file(arena, manifest_path, MB(1));
  if(manifest.size == 0)
  {
    send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("Manifest not found"));
    return;
  }

  send_response(socket, arena, str8_lit("application/dash+xml"), manifest);
  log_infof("media-server: served manifest (%llu bytes)\n", manifest.size);
}

internal void
handle_segment(OS_Handle socket, String8 file_path, String8 segment_name, Arena *arena)
{
  String8 full_path = str8f(arena, "%S/%S", media_root_path, file_path);
  b32 processing = 0;
  CacheInfo cache = get_or_create_cache(arena, full_path, &processing);

  if(processing != 0)
  {
    send_error_response(socket, HTTP_Status_202_Accepted,
                       str8_lit("Transcoding in progress, please retry in a few seconds"));
    return;
  }

  if(cache.cache_dir.size == 0)
  {
    send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("File not found"));
    return;
  }

  String8 segment_path = str8f(arena, "%S/%S", cache.cache_dir, segment_name);
  OS_FileProperties props = os_properties_from_file_path(segment_path);
  if(props.size == 0)
  {
    send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("Segment not found"));
    return;
  }

  HTTP_Response *res = http_response_alloc(arena, HTTP_Status_200_OK);
  http_header_add(arena, &res->headers, str8_lit("Content-Type"), str8_lit("video/mp4"));
  http_header_add(arena, &res->headers, str8_lit("Access-Control-Allow-Origin"), str8_lit("*"));
  http_header_add(arena, &res->headers, str8_lit("Content-Length"),
                  str8_from_u64(arena, props.size, 10, 0, 0));
  res->body = str8_zero();
  socket_write_all(socket, http_response_serialize(arena, res));
  stream_file_to_socket(socket, segment_path, props.size);
}

internal String8
get_query_param(Arena *arena, String8 query, String8 key)
{
  String8 result = str8_zero();
  if(query.size > 0)
  {
    String8List parts = str8_split(arena, query, (u8 *)"&", 1, 0);
    for(String8Node *n = parts.first; n != 0; n = n->next)
    {
      u64 eq = str8_find_needle(n->string, 0, str8_lit("="), 0);
      if(eq < n->string.size && str8_match(str8_prefix(n->string, eq), key, 0))
      {
        result = url_decode(arena, str8_skip(n->string, eq + 1));
        break;
      }
    }
  }
  return result;
}

internal void
handle_http_request(HTTP_Request *req, OS_Handle socket, Arena *arena)
{
  if(str8_match(req->path, str8_lit("/"), 0))
  {
    String8 dir_param = get_query_param(arena, req->query, str8_lit("dir"));
    handle_directory_listing(socket, dir_param, arena);
  }
  else if(str8_match(req->path, str8_lit("/player"), 0))
  {
    String8 file_param = get_query_param(arena, req->query, str8_lit("file"));
    if(file_param.size > 0)
    {
      handle_player(socket, file_param, arena);
    }
    else
    {
      send_error_response(socket, HTTP_Status_400_BadRequest, str8_lit("Missing file parameter"));
    }
  }
  else if(str8_match(req->path, str8_lit("/api/progress"), 0))
  {
    String8 file_param = get_query_param(arena, req->query, str8_lit("file"));
    String8 pos_param = get_query_param(arena, req->query, str8_lit("position"));
    String8 sub_param = get_query_param(arena, req->query, str8_lit("subtitle"));
    String8 aud_param = get_query_param(arena, req->query, str8_lit("audio"));
    String8 delete_param = get_query_param(arena, req->query, str8_lit("delete"));

    if(file_param.size > 0 && delete_param.size > 0)
    {
      watch_progress_delete(file_param);
      send_response(socket, arena, str8_lit("text/plain"), str8_lit("ok"));
    }
    else if(file_param.size > 0 && pos_param.size > 0)
    {
      f64 position = f64_from_str8(pos_param);
      watch_progress_set(file_param, position, sub_param, aud_param);
      send_response(socket, arena, str8_lit("text/plain"), str8_lit("ok"));
    }
    else if(file_param.size > 0)
    {
      WatchProgress *progress = watch_progress_find(file_param);
      f64 position = progress ? progress->position_seconds : 0.0;
      String8 subtitle = progress && progress->subtitle_lang.size > 0 ? progress->subtitle_lang : str8_lit("-");
      String8 audio = progress && progress->audio_lang.size > 0 ? progress->audio_lang : str8_lit("-");
      String8 response = str8f(arena, "%.2f %S %S", position, subtitle, audio);
      send_response(socket, arena, str8_lit("text/plain"), response);
    }
    else
    {
      send_error_response(socket, HTTP_Status_400_BadRequest, str8_lit("Missing file parameter"));
    }
  }
  else if(str8_match(str8_prefix(req->path, 7), str8_lit("/media/"), 0))
  {
    String8 media_path = str8_skip(req->path, 7);

    String8 file_part = str8_chop_last_slash(media_path);
    String8 resource = str8_skip_last_slash(media_path);

    if(file_part.size > 0)
    {
      String8 file = url_decode(arena, file_part);

      if(str8_match(resource, str8_lit("manifest.mpd"), 0))
      {
        handle_manifest(socket, file, arena);
      }
      else if(str8_match(resource, str8_lit("subtitles.txt"), 0))
      {
        String8 file_path = str8f(arena, "%S/%S", media_root_path, file);
        String8 cache_dir = get_cache_dir(arena, file_path);
        String8List subtitle_files = find_subtitle_files(arena, cache_dir);

        StreamMetaArray streams = parse_stream_metadata(arena, cache_dir);

        String8List lines = {0};
        for(String8Node *n = subtitle_files.first; n != 0; n = n->next)
        {
          String8 filename = n->string;
          u64 first_dot = str8_find_needle(filename, 0, str8_lit("."), 0);
          if(first_dot < filename.size)
          {
            u64 second_dot = str8_find_needle(filename, first_dot + 1, str8_lit("."), 0);
            if(second_dot < filename.size)
            {
              String8 index_str = str8_substr(filename, rng_1u64(first_dot + 1, second_dot));
              u32 index = (u32)u64_from_str8(index_str, 10);

              String8 lang = str8_lit("und");
              String8 title = str8_zero();
              for(u64 i = 0; i < streams.count; i += 1)
              {
                StreamMeta *s = &streams.items[i];
                if(!s->is_audio && s->stream_idx == index)
                {
                  lang = s->lang;
                  title = s->metadata;
                  break;
                }
              }

              if(str8_match(lang, str8_lit("und"), 0))
              {
                u64 third_dot = str8_find_needle(filename, second_dot + 1, str8_lit("."), 0);
                lang = str8_substr(filename, rng_1u64(second_dot + 1, third_dot));
              }

              String8 encoded_file = url_encode(arena, file);
              String8 url = str8f(arena, "/media/%S/%S", encoded_file, filename);

              String8 title_field = title.size > 0 ? title : str8_lit("-");
              str8_list_pushf(arena, &lines, "%S\t%S\t%S\n", lang, title_field, url);
            }
          }
        }
        String8 text = str8_list_join(arena, lines, 0);
        send_response(socket, arena, str8_lit("text/plain; charset=utf-8"), text);
      }
      else if(str8_find_needle(resource, 0, str8_lit(".vtt"), 0) == resource.size - 4)
      {
        String8 file_path = str8f(arena, "%S/%S", media_root_path, file);
        String8 cache_dir = get_cache_dir(arena, file_path);
        String8 vtt_path = str8f(arena, "%S/%S", cache_dir, resource);

        String8 subtitle_content = read_small_file(arena, vtt_path, MB(1));
        if(subtitle_content.size > 0)
        {
          send_response(socket, arena, str8_lit("text/vtt; charset=utf-8"), subtitle_content);
        }
        else
        {
          send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("Subtitle not found"));
        }
      }
      else
      {
        handle_segment(socket, file, resource, arena);
      }
    }
    else if(str8_find_needle(media_path, 0, str8_lit(".mpd"), 0) == media_path.size - 4)
    {
      handle_manifest(socket, str8_prefix(media_path, media_path.size - 4), arena);
    }
    else if(str8_find_needle(media_path, 0, str8_lit(".vtt"), 0) == media_path.size - 4)
    {
      String8 subtitle_path = str8f(arena, "%S/%S", media_root_path, media_path);

      String8 subtitle_content = read_small_file(arena, subtitle_path, MB(1));
      if(subtitle_content.size > 0)
      {
        send_response(socket, arena, str8_lit("text/vtt; charset=utf-8"), subtitle_content);
      }
      else
      {
        send_error_response(socket, HTTP_Status_404_NotFound, str8_lit("Subtitle not found"));
      }
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

    b32 success = generate_dash_to_cache(job->file_path, job->cache_dir, job->file_hash);
    transmux_mark_complete(job, success);
  }
}

////////////////////////////////
//~ Connection

internal void
handle_connection(OS_Handle socket)
{
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
      handle_http_request(req, socket, arena);
    }
    else
    {
      send_error_response(socket, HTTP_Status_400_BadRequest, str8_lit("Invalid request"));
    }
  }

  os_file_close(socket);
  arena_release(arena);
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
  cache_root_path = cmd_line_string(cmd_line, str8_lit("cache-root"));
  if(cache_root_path.size == 0) { cache_root_path = str8_lit("/tmp/media-server-cache"); }
  state_root_path = cmd_line_string(cmd_line, str8_lit("state-root"));
  if(state_root_path.size == 0) { state_root_path = str8_lit("/tmp/media-server-state"); }

  transmux_queue = push_array(arena, TransmuxQueue, 1);
  transmux_queue->mutex = mutex_alloc();
  transmux_queue->arena = arena_alloc();
  transmux_queue->hash_table_size = 256;
  transmux_queue->hash_table = push_array(arena, TransmuxJob *, transmux_queue->hash_table_size);
  transmux_queue->first = 0;
  transmux_queue->last = 0;

  {
    Temp temp = scratch_begin(&arena, 1);
    String8 cache_path = str8_copy(temp.arena, cache_root_path);
    mkdir((char *)cache_path.str, 0755);
    scratch_end(temp);
  }

  {
    Temp temp = scratch_begin(&arena, 1);
    String8 state_path = str8_copy(temp.arena, state_root_path);
    mkdir((char *)state_path.str, 0755);
    scratch_end(temp);
  }

  watch_progress = push_array(arena, WatchProgressTable, 1);
  watch_progress->mutex = mutex_alloc();
  watch_progress->arena = arena_alloc();
  watch_progress->bucket_count = 256;
  watch_progress->buckets = push_array(arena, WatchProgress *, watch_progress->bucket_count);
  watch_progress_load();

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
