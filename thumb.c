#include <libavformat/avformat.h>

#include <libavcodec/avcodec.h>

#include <libavutil/common.h>
#include <libavutil/imgutils.h>

#include <libavfilter/avfiltergraph.h>
#include <libavfilter/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

#include "thumb.h"

typedef struct _ThumbContext {
  AVFormatContext *ctx;
  AVCodecContext *encoder;
  AVStream *stream;
  AVCodec *dcodec;
  AVCodec *ecodec;
  AVFilterGraph *graph;
  AVFilterContext *srcctx;
  AVFilterContext *snkctx;

  double duration;
  long width;
  long height;
  char *codec;

  int index;
} ThumbContext;

static AVPacket *read_next_frame(AVFormatContext *ctx, int idx, AVPacket *pkt)
{
    while (1) {
        int rv = av_read_frame(ctx, pkt);
        if (rv < 0) {
            return NULL;
        }
        if (pkt->stream_index != idx) {
            av_free_packet(pkt);
            continue;
        }
        return pkt;
    }
}

static AVCodecContext* open_encoder(ThumbContext *ctx)
{
  AVCodec *ecodec = avcodec_find_encoder(AV_CODEC_ID_BMP);
  AVCodecContext *ecctx = avcodec_alloc_context3(ecodec);
  if (!ecctx) {
    av_log(NULL, AV_LOG_FATAL, "Failed to get encoder context\n");
    return NULL;
  }
  avcodec_get_context_defaults3(ecctx, NULL);
  ecctx->width = ctx->width;
  ecctx->height = ctx->height;
  ecctx->pix_fmt = avcodec_find_best_pix_fmt_of_list(
          ecodec->pix_fmts,
          ctx->stream->codec->pix_fmt,
          1,
          NULL);
  av_log(NULL, AV_LOG_INFO, "encoder will be %dx%d %x\n", ecctx->width, ecctx->height, ecctx->pix_fmt);
  if (ecctx->pix_fmt < 0) {
    av_log(NULL, AV_LOG_FATAL, "Failed to get encoder pix_fmt\n");
    goto err_encoder;
  }
  if (avcodec_open2(ecctx, ecodec, NULL) < 0) {
    av_log(NULL, AV_LOG_FATAL, "Failed to open encoder\n");
    goto err_encoder;
  }
  ctx->ecodec = ecodec;
  ctx->encoder = ecctx;
  av_log(NULL, AV_LOG_INFO, "encoder OK\n");
  return ecctx;

err_encoder:
  av_freep(&ecctx);
  return NULL;
}

static AVFilterGraph* create_filter_graph(ThumbContext *ctx)
{
  AVStream *st = ctx->stream;
  AVFilterGraph *graph;
  AVFilterContext *srcctx;
  AVFilterContext *snkctx;
  AVFilter *src = avfilter_get_by_name("buffer");
  AVFilter *snk = avfilter_get_by_name("buffersink");
  AVFilterContext *convert;
  AVFilterInOut *outputs = avfilter_inout_alloc();
  AVFilterInOut *inputs  = avfilter_inout_alloc();
  enum AVPixelFormat pix_fmts[] = { ctx->encoder->pix_fmt, AV_PIX_FMT_NONE };
  AVBufferSinkParams *sinkp;
  const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(ctx->encoder->pix_fmt);
  char args[512];
  int rv;

  graph = avfilter_graph_alloc();
  snprintf(
      args, sizeof(args),
      "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
      st->codec->width, st->codec->height, st->codec->pix_fmt,
      st->codec->time_base.num, st->codec->time_base.den,
      st->codec->sample_aspect_ratio.num, st->codec->sample_aspect_ratio.den);
  rv = avfilter_graph_create_filter(&srcctx, src, "in", args, NULL, graph);
  if (rv < 0) {
    av_log(NULL, AV_LOG_FATAL, "Cannot create buffer source\n");
    goto err_graph;
  }

  sinkp = av_buffersink_params_alloc();
  sinkp->pixel_fmts = pix_fmts;
  rv = avfilter_graph_create_filter(&snkctx, snk, "out", NULL, sinkp, graph);
  av_free(sinkp);
  if (rv < 0) {
    av_log(NULL, AV_LOG_FATAL, "Cannot create buffer sink\n");
    goto err_graph;
  }

  /* Endpoints for the filter graph. */
  outputs->name = av_strdup("in");
  outputs->filter_ctx = srcctx;
  outputs->pad_idx = 0;
  outputs->next = NULL;

  inputs->name = av_strdup("out");
  inputs->filter_ctx = snkctx;
  inputs->pad_idx = 0;
  inputs->next = NULL;

  snprintf(args, sizeof(args), "format=%s", desc->name);
  rv = avfilter_graph_parse_ptr(graph, args, &inputs, &outputs, NULL);
  if (rv < 0) {
    av_log(NULL, AV_LOG_FATAL, "Failed to parse graph\n");
    goto err_graph;
  }

  if ((rv = avfilter_graph_config(graph, NULL)) < 0) {
    av_log(NULL, AV_LOG_FATAL, "Failed to parse graph config\n");
    goto err_graph;
  }

  ctx->graph = graph;
  ctx->srcctx = srcctx;
  ctx->snkctx = snkctx;
  return graph;

err_graph:
  avfilter_graph_free(&graph);
  return NULL;
}

void ffthumb_free(ThumbContext** ctx)
{
  ThumbContext *c;
  if (!ctx || !*ctx) {
    return;
  }
  c = *ctx;
  if (c->codec) {
    av_log(NULL, AV_LOG_VERBOSE, "Freeing codec\n");
    av_freep(&c->codec);
  }
  if (c->graph) {
    av_log(NULL, AV_LOG_VERBOSE, "Freeing graph\n");
    avfilter_graph_free(&c->graph);
  }
  if (c->encoder) {
    av_log(NULL, AV_LOG_VERBOSE, "Freeing encoder\n");
    avcodec_close(c->encoder);
    c->encoder = NULL;
  }
  if (c->stream) {
    av_log(NULL, AV_LOG_VERBOSE, "Freeing stream\n");
    avcodec_close(c->stream->codec);
    c->stream = NULL;
  }
  if (c->ctx) {
    av_log(NULL, AV_LOG_VERBOSE, "Freeing input\n");
    avformat_close_input(&c->ctx);
  }

  av_log(NULL, AV_LOG_VERBOSE, "Freeing self\n");
  av_freep(ctx);
}

ThumbContext * ffthumb_create(const char *filename)
{
  ThumbContext *rv = av_malloc(sizeof(ThumbContext));
  AVStream *stream;
  if (!rv) {
    goto err_free;
  }
  memset(rv, 0, sizeof(ThumbContext));

  if (avformat_open_input(&rv->ctx, filename, NULL, NULL) < 0) {
    av_log(NULL, AV_LOG_FATAL, "Cannot open file\n");
    goto err_free;
  }
  if (avformat_find_stream_info(rv->ctx, NULL) < 0) {
    av_log(NULL, AV_LOG_FATAL, "Cannot find stream info\n");
    goto err_free;
  }
  rv->index = av_find_best_stream(
      rv->ctx,
      AVMEDIA_TYPE_VIDEO,
      -1,
      -1,
      &rv->dcodec,
      0);
  if (rv->index < 0) {
    av_log(NULL, AV_LOG_FATAL, "Cannot find video stream\n");
    goto err_free;
  }
  stream = rv->ctx->streams[rv->index];
  stream->codec->workaround_bugs = 1;
  if (avcodec_open2(stream->codec, rv->dcodec, NULL) < 0) {
    av_log(NULL, AV_LOG_FATAL, "Cannot open decoder\n");
    goto err_free;
  }
  rv->stream = stream;
  rv->codec = av_strdup(rv->dcodec->name);
  rv->width = rv->stream->codec->width;
  rv->height = rv->stream->codec->height;
  rv->duration = rv->ctx->duration / (double)AV_TIME_BASE;

  return rv;

err_free:
    ffthumb_free(&rv);
    return NULL;
}

int64_t ffthumb_load_frame(ThumbContext *ctx, double position, char **buffer)
{
  AVFrame *frame = 0;
  AVPacket pkt;
  int64_t timestamp = ctx->ctx->duration * position;
  int64_t bytes = 0;
  int attempts = 0;
  int rv;

  *buffer = NULL;

  /* seek min */
  av_seek_frame(ctx->ctx, -1, timestamp, 0);
  /* seek max */
  if (timestamp > 0) {
    av_seek_frame(ctx->ctx, -1, timestamp, AVSEEK_FLAG_BACKWARD);
  }
  avcodec_flush_buffers(ctx->stream->codec);
  av_log(NULL, AV_LOG_INFO, "seek to %" PRId64 "\n", timestamp);

  frame = avcodec_alloc_frame();
  do {
    while (read_next_frame(ctx->ctx, ctx->index, &pkt)) {
      int fin = 0;
      avcodec_get_frame_defaults(frame);
      int decoded = avcodec_decode_video2(
          ctx->stream->codec,
          frame,
          &fin,
          &pkt);
      if (decoded < 0) {
        av_log(NULL, AV_LOG_FATAL, "Failed to decode\n");
        goto err_frame;
      }
      if (fin) {
        int64_t ts = av_rescale_q(
            timestamp,
            AV_TIME_BASE_Q,
            ctx->stream->time_base);
        frame->pts = av_frame_get_best_effort_timestamp(frame);
        av_log(NULL, AV_LOG_INFO, "got frame @ %dx%d %" PRId64 " %" PRId64 "\n", frame->width, frame->height, frame->pts, ts);
        if (frame->pts >= ts) {
          break;
        }
      }
    }
  } while (!frame->width && attempts++ < 200);
  frame->pts = 0;
  av_log(NULL, AV_LOG_INFO, "frame: %dx%d\n", frame->width, frame->height);
  if (frame->width == 0 || frame->height == 0) {
    av_log(NULL, AV_LOG_FATAL, "Failed to get frame\n");
    goto err_frame;
  }

  if (!ctx->encoder && !open_encoder(ctx)) {
    av_log(NULL, AV_LOG_FATAL, "Failed to open encoder\n");
    goto err_frame;
  }
  if (!ctx->graph && !create_filter_graph(ctx)) {
    av_log(NULL, AV_LOG_FATAL, "Failed to create filter graph\n");
    goto err_frame;
  }

  rv = av_buffersrc_add_frame_flags(
      ctx->srcctx,
      frame,
      AV_BUFFERSRC_FLAG_KEEP_REF);
  if (rv < 0) {
    av_log(NULL, AV_LOG_FATAL, "Failed to add frame to source\n");
    goto err_frame;
  }
  av_frame_unref(frame);
    
  {
    int got_output;
    frame = avcodec_alloc_frame();
    rv = av_buffersink_get_frame(ctx->snkctx, frame);
    if (rv < 0) {
      av_log(NULL, AV_LOG_FATAL, "Failed to get frame from sink\n");
      goto err_frame;
    }
    frame->pts = 0;

    pkt.data = NULL;
    pkt.size = 0;

    rv = avcodec_encode_video2(
        ctx->encoder,
        &pkt,
        frame,
        &got_output);
    if (rv < 0) {
      av_log(NULL, AV_LOG_FATAL, "Failed to encode\n");
      goto err_frame;
    }
    if (!got_output) {
        av_log(NULL, AV_LOG_FATAL, "No output\n");
        goto err_frame;
    }
    *buffer = av_malloc(pkt.size);
    memcpy(*buffer, pkt.data, pkt.size);
    bytes = pkt.size;
    av_free_packet(&pkt);
    av_log(NULL, AV_LOG_INFO, "load complete\n");
  }

err_frame:
  av_frame_free(&frame);
  return bytes;
}

void ffthumb_free_frame(ThumbContext *ctx, char** buffer)
{
  if (!buffer || !*buffer) {
    return;
  }
  av_freep(buffer);
}

const char* ffthumb_get_codec(ThumbContext *ctx)
{
  return ctx->codec;
}

double ffthumb_get_duration(ThumbContext *ctx)
{
  return ctx->duration;
}

long ffthumb_get_width(ThumbContext *ctx)
{
  return ctx->width;
}

long ffthumb_get_height(ThumbContext *ctx)
{
  return ctx->height;
}

static const Thumber thumber = {
  ffthumb_create,
  ffthumb_free,
  ffthumb_load_frame,
  ffthumb_free_frame,
  ffthumb_get_codec,
  ffthumb_get_duration,
  ffthumb_get_width,
  ffthumb_get_height
};

const Thumber* ffthumb_init(int log) {
  switch (log) {
    case 0:
      log = AV_LOG_FATAL;
      break;
    case 1:
      log = AV_LOG_INFO;
      break;
    default:
      log = AV_LOG_DEBUG;
      break;
  }
  av_log_set_level(log);
  avcodec_register_all();
  av_register_all();
  avfilter_register_all();
  return &thumber;
}
