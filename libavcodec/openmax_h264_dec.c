/*
 * Copyright (c) 2013, Vladimir Voroshilov
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "openmax.h"
#include "h264.h"

extern AVCodec ff_h264_decoder, ff_h264_openmax_decoder;

typedef struct {
    H264Context h264ctx;
    int h264_initialized;
    struct openmax_context openmax_ctx;
    enum AVPixelFormat pix_fmt;
} OpenMAXDecoderContext;

static enum AVPixelFormat get_format(struct AVCodecContext *avctx,
        const enum AVPixelFormat *fmt)
{
    return AV_PIX_FMT_OPENMAX_VLD;
}
#if 0
static void release_buffer(void *opaque, uint8_t *data)
{
    OpenMAXBufferContext *context = opaque;
    CVPixelufferUnlockBaseAddress(context->cv_buffer, 0);
    CVPixelBufferRelease(context->cv_buffer);
    av_free(context);
}

static int get_buffer2(AVCodecContext *avctx, AVFrame *pic, int flag)
{
    VDABufferContext *context = av_mallocz(sizeof(VDABufferContext));
    AVBufferRef *buffer = av_buffer_create(NULL, 0, release_buffer, context, 0);
    if (!context || !buffer) {
        av_free(context);
        return AVERROR(ENOMEM);
    }

    pic->buf[0] = buffer;
    pic->data[0] = (void *)1;
    return 0;
}
#endif
static av_cold int check_format(AVCodecContext *avctx)
{
    AVCodecParserContext *parser;
    uint8_t *pout;
    int psize;
    int index;
    H264Context *h;
    int ret = -1;

    /* init parser & parse file */
    parser = av_parser_init(avctx->codec->id);
    if (!parser) {
        av_log(avctx, AV_LOG_ERROR, "Failed to open H.264 parser.\n");
        goto final;
    }
    parser->flags = PARSER_FLAG_COMPLETE_FRAMES;
    index = av_parser_parse2(parser, avctx, &pout, &psize, NULL, 0, 0, 0, 0);
    if (index < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to parse this file.\n");
        goto release_parser;
    }

    /* check if support */
    h = parser->priv_data;
    switch (h->sps.bit_depth_luma) {
    case 8:
        if (!CHROMA444(h) && !CHROMA422(h)) {
            // only this will H.264 decoder switch to hwaccel
            ret = 0;
            break;
        }
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported file %d.\n", h->sps.bit_depth_luma);
    }

release_parser:
    av_parser_close(parser);

final:
    return ret;
}
static int openmax_decode(AVCodecContext *avctx,
        void *data, int *got_frame, AVPacket *avpkt)
{
#if 0
    OpenMAXDecoderContext *ctx = avctx->priv_data;
    AVFrame *pic = data;
    int ret;

    ret = ff_h264_decoder.decode(avctx, data, got_frame, avpkt);
    if (*got_frame) {
        AVBufferRef *buffer = pic->buf[0];
        VDABufferContext *context = av_buffer_get_opaque(buffer);
        CVPixelBufferRef cv_buffer = (CVPixelBufferRef)pic->data[3];
        CVPixelBufferLockBaseAddress(cv_buffer, 0);
        context->cv_buffer = cv_buffer;
        pic->format = ctx->pix_fmt;
        if (CVPixelBufferIsPlanar(cv_buffer)) {
            int i, count = CVPixelBufferGetPlaneCount(cv_buffer);
            av_assert0(count < 4);
            for (i = 0; i < count; i++) {
                pic->data[i] = CVPixelBufferGetBaseAddressOfPlane(cv_buffer, i);
                pic->linesize[i] = CVPixelBufferGetBytesPerRowOfPlane(cv_buffer, i);
            }
        } else {
            pic->data[0] = CVPixelBufferGetBaseAddress(cv_buffer);
            pic->linesize[0] = CVPixelBufferGetBytesPerRow(cv_buffer);
        }
    }
    avctx->pix_fmt = ctx->pix_fmt;

    return ret;
#endif
    return 0;
}
static av_cold int openmax_close(AVCodecContext *avctx)
{
    OpenMAXDecoderContext *ctx = avctx->priv_data;
    /* release buffers and decoder */
    ff_openmax_destroy_decoder(avctx, &ctx->openmax_ctx);
    /* close H.264 decoder */
    if (ctx->h264_initialized)
        ff_h264_decoder.close(avctx);
    return 0;
}
static av_cold int openmax_init(AVCodecContext *avctx)
{
    OpenMAXDecoderContext *ctx = avctx->priv_data;
    struct openmax_context *openmax_ctx = &ctx->openmax_ctx;
    int status;
    int ret;

    ctx->h264_initialized = 0;

    /* check if OpenMAX supports this file */
    if (check_format(avctx) < 0)
        goto failed;

    /* init vda */
    memset(openmax_ctx, 0, sizeof(struct openmax_context));

    openmax_ctx->width = avctx->width;
    openmax_ctx->height = avctx->height;

    openmax_ctx->openmax_input_format = OMX_VIDEO_CodingAVC;

    ctx->pix_fmt = avctx->get_format(avctx, avctx->codec->pix_fmts);
    switch (ctx->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        openmax_ctx->openmax_output_format = OMX_COLOR_FormatYUV420PackedPlanar;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported pixel format: %d\n", avctx->pix_fmt);
        goto failed;
    }
    status = ff_openmax_create_decoder(avctx, openmax_ctx,
                                   avctx->extradata, avctx->extradata_size);
    if (status < 0) {
        av_log(avctx, AV_LOG_ERROR,
                "Failed to init OpenMAX decoder: %d.\n", status);
        goto failed;
    }
    avctx->hwaccel_context = openmax_ctx;

    /* changes callback functions */
    avctx->get_format = get_format;
//    avctx->get_buffer2 = get_buffer2;
#if FF_API_GET_BUFFER
    // force the old get_buffer to be empty
    avctx->get_buffer = NULL;
#endif

    /* init H.264 decoder */
    ret = ff_h264_decoder.init(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to open H.264 decoder.\n");
        goto failed;
    }
    ctx->h264_initialized = 1;

    return 0;

failed:
    openmax_close(avctx);
    return -1;
}
static void openmax_flush(AVCodecContext *avctx)
{
}
AVCodec ff_h264_openmax_decoder = {
    .name           = "h264_openmax",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(OpenMAXDecoderContext),
    .init           = openmax_init,
    .close          = openmax_close,
    .decode         = openmax_decode,
    .capabilities   = CODEC_CAP_DELAY,
    .flush          = openmax_flush,
    .long_name      = NULL_IF_CONFIG_SMALL("H.264 (OpenMAX acceleration)"),
    .pix_fmts       = (const enum AVPixelFormat[]){AV_PIX_FMT_YUYV422, AV_PIX_FMT_NONE},
};
