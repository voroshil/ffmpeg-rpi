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
#include "internal.h"

typedef struct {
    struct openmax_context openmax_ctx;
    enum AVPixelFormat pix_fmt;
} OpenMAXDecoderContext;

static enum AVPixelFormat get_format(struct AVCodecContext *avctx,
        const enum AVPixelFormat *fmt)
{
    av_log(avctx, AV_LOG_DEBUG, "get_format called\n");
//    return AV_PIX_FMT_YUV420P;
    return AV_PIX_FMT_OPENMAX_VLD;
}

static int openmax_decode(AVCodecContext *avctx,
        void *data, int *got_frame, AVPacket *avpkt)
{
    OpenMAXDecoderContext *ctx = avctx->priv_data;
    AVFrame *pic = data;
    int ret;

    ctx->openmax_ctx.frame = pic;
    if (!avctx->hwaccel)
	avctx->hwaccel = ff_find_hwaccel(avctx->codec->id, AV_PIX_FMT_OPENMAX_VLD);
    av_log(avctx, AV_LOG_DEBUG, "hw=%p\n", avctx->hwaccel);
    if (!avctx->hwaccel)
    {
	*got_frame = 0;
	return -1;
    }
    avctx->hwaccel->decode_slice(avctx, avpkt->data, avpkt->size);
    if (avctx->hwaccel->end_frame(avctx) <0){
	*got_frame = 0;
	ret = -1;
    }else{
	*got_frame = 1;
        pic->format = ctx->pix_fmt;
	ret = ctx->openmax_ctx.frame_size;
        avctx->pix_fmt = PIX_FMT_YUV420P;
    }
    return ret;
}
static av_cold int openmax_close(AVCodecContext *avctx)
{
    OpenMAXDecoderContext *ctx = avctx->priv_data;
    /* release buffers and decoder */
    ff_openmax_destroy_decoder(avctx, &ctx->openmax_ctx);
    /* close H.264 decoder */
    return 0;
}
static av_cold int openmax_init(AVCodecContext *avctx)
{
    OpenMAXDecoderContext *ctx = avctx->priv_data;
    struct openmax_context *openmax_ctx = &ctx->openmax_ctx;
    int status;

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
    .pix_fmts       = (const enum AVPixelFormat[]){AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUYV422, AV_PIX_FMT_NONE},
};
