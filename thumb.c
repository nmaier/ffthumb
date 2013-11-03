#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

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

unsigned long create_thumb(const char *filename, double position, double *duration, long* width, long* height, char** codec, char** buffer)
{
    AVFormatContext *ctx = NULL;
    AVCodecContext *ecctx = NULL;
    AVStream *st;
    AVCodec *dcodec;
    AVCodec *ecodec;
    AVFrame *frame = NULL;
    AVPacket pkt;
    AVFilterGraph *graph;
    AVFilterContext *srcctx;
    AVFilterContext *snkctx;

    int idx;
    size_t bytes = 0;
    int rv = -1;
    int kattempts = 0;
    int64_t timestamp;

    if (avformat_open_input(&ctx, filename, NULL, NULL) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Cannot open file\n");
        return 0;
    }
    if (avformat_find_stream_info(ctx, NULL) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Cannot find stream info\n");
        goto close;
    }
    *duration = (double)ctx->duration / AV_TIME_BASE;

    for (idx = 0; idx < ctx->nb_streams; ++idx) {
        st = ctx->streams[idx];
        if (st->codec->codec_type != AVMEDIA_TYPE_VIDEO) {
            continue;
        }
        dcodec = avcodec_find_decoder(st->codec->codec_id);
        if (!dcodec) {
            continue;
        }
        st->codec->workaround_bugs = 1;
        if (avcodec_open2(st->codec, dcodec, NULL) < 0) {
            dcodec = NULL;
            continue;
        }
        break;
    }
    if (!dcodec) {
        av_log(NULL, AV_LOG_FATAL, "Couldn't find a stream\n");
        goto close;
    }
    *codec = av_strdup(dcodec->name);

    {
        timestamp = ctx->duration * position;
        av_seek_frame(ctx, -1, timestamp, 0);
        if (timestamp > 0) {
          av_seek_frame(ctx, -1, timestamp, AVSEEK_FLAG_BACKWARD);
        }
        av_log(NULL, AV_LOG_INFO, "seek to %" PRId64 "\n", timestamp);
    }

seek:
    kattempts = 0;
    do {
        if (frame) {
          av_log(NULL, AV_LOG_WARNING, "freeing frame\n");
          av_frame_free(&frame);
        }
        if (!frame) {
          av_log(NULL, AV_LOG_WARNING, "alloc frame\n");
          frame = avcodec_alloc_frame();
        }
        while (read_next_frame(ctx, idx, &pkt)) {
            int fin = 0;
            avcodec_get_frame_defaults(frame);
            int decoded = avcodec_decode_video2(st->codec, frame, &fin, &pkt);
            if (decoded < 0) {
                av_log(NULL, AV_LOG_FATAL, "Failed to decode\n");
                goto frame;
            }
            if (fin) {
                int64_t ts = av_rescale_q(timestamp, AV_TIME_BASE_Q, st->time_base);
                frame->pts = av_frame_get_best_effort_timestamp(frame);
                av_log(NULL, AV_LOG_ERROR, "got frame @ %dx%d %" PRId64 " %" PRId64 "\n", frame->width, frame->height, frame->pts, ts);
                if (frame->pts >= ts) {
                  break;
                }
            }
        }
        av_log(NULL, AV_LOG_ERROR, "still going @ %d\n", kattempts);
    } while (!frame->width && kattempts++ < 200);
    frame->pts = 0;
    av_log(NULL, AV_LOG_INFO, "frame: %dx%d\n", frame->width, frame->height);
    if (frame->width == 0 || frame->height == 0) {
      av_frame_free(&frame);
      av_freep(&frame);
      av_seek_frame(ctx, -1, timestamp, AVSEEK_FLAG_BACKWARD);
      av_log(NULL, AV_LOG_WARNING, "reseeking %.3f\n", position);
      goto seek;
    }

    ecodec = avcodec_find_encoder(AV_CODEC_ID_BMP);
    ecctx = avcodec_alloc_context3(ecodec);
    if (!ecctx) {
        av_log(NULL, AV_LOG_FATAL, "Failed to get encoder context\n");
        goto unref_frame;
    }
    avcodec_get_context_defaults3(ecctx, NULL);
    *width = ecctx->width = st->codec->width;
    *height = ecctx->height = st->codec->height;
    ecctx->pix_fmt = avcodec_find_best_pix_fmt_of_list(
            ecodec->pix_fmts,
            st->codec->pix_fmt,
            1,
            NULL);
    if (avcodec_open2(ecctx, ecodec, NULL) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Failed to open encoder\n");
        goto encoder;
    }

    {
        AVFilter *src = avfilter_get_by_name("buffer");
        AVFilter *snk = avfilter_get_by_name("buffersink");
        AVFilterContext *convert;
        AVFilterInOut *outputs = avfilter_inout_alloc();
        AVFilterInOut *inputs  = avfilter_inout_alloc();
        enum AVPixelFormat pix_fmts[] = { ecctx->pix_fmt, AV_PIX_FMT_NONE };
        AVBufferSinkParams *sinkp;
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(ecctx->pix_fmt);
        char args[512];

        graph = avfilter_graph_alloc();
        snprintf(args, sizeof(args),
                "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                st->codec->width, st->codec->height, st->codec->pix_fmt,
                st->codec->time_base.num, st->codec->time_base.den,
                st->codec->sample_aspect_ratio.num, st->codec->sample_aspect_ratio.den);
        rv = avfilter_graph_create_filter(&srcctx, src, "in", args, NULL, graph);
        if (rv < 0) {
            av_log(NULL, AV_LOG_FATAL, "Cannot create buffer source\n");
            goto fgraph;
        }

        sinkp = av_buffersink_params_alloc();
        sinkp->pixel_fmts = pix_fmts;
        rv = avfilter_graph_create_filter(&snkctx, snk, "out", NULL, sinkp, graph);
        av_free(sinkp);
        if (rv < 0) {
            av_log(NULL, AV_LOG_FATAL, "Cannot create buffer sink\n");
            goto fgraph;
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
            goto fgraph;
        }

        if ((rv = avfilter_graph_config(graph, NULL)) < 0) {
            av_log(NULL, AV_LOG_FATAL, "Failed to parse graph config\n");
            goto fgraph;
        }
    }
    if (av_buffersrc_add_frame_flags(srcctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Failed to add frame to source\n");
        goto fgraph;
    }
    av_frame_unref(frame);

    while (1) {
        int got_output;
        frame = avcodec_alloc_frame();
        rv = av_buffersink_get_frame(snkctx, frame);
        if (rv < 0) {
            av_log(NULL, AV_LOG_FATAL, "Failed to get frame from sink\n");
            goto fgraph;
        }
        frame->pts = 0;

        pkt.data = NULL;
        pkt.size = 0;

        rv = avcodec_encode_video2(ecctx, &pkt, frame, &got_output);
        if (rv < 0) {
            av_log(NULL, AV_LOG_FATAL, "Failed to encode\n");
            goto fgraph;
        }
        if (!got_output) {
            av_log(NULL, AV_LOG_FATAL, "No output\n");
            goto fgraph;
        }
        *buffer = av_malloc(pkt.size);
        memcpy(*buffer, pkt.data, pkt.size);
        bytes = pkt.size;
        av_free_packet(&pkt);
        break;
    }

fgraph:
    avcodec_close(ecctx);
    avfilter_graph_free(&graph);

encoder:
    av_free(ecctx);

unref_frame:
    //av_frame_unref(frame);

frame:
    avcodec_free_frame(&frame);

codec:
    avcodec_close(st->codec);

close:
    avformat_close_input(&ctx);
    return bytes;
}

void free_thumb(char** p) {
    av_freep(p);
}

#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason,  LPVOID lpvReserved)
{
  if (fdwReason == DLL_PROCESS_ATTACH) {
    av_log_set_level(AV_LOG_FATAL);
    avcodec_register_all();
    av_register_all();
    avfilter_register_all();
  }
  return TRUE;
}
