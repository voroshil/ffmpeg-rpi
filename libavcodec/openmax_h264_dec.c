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

typedef struct {
    H264Context h264ctx;
    int h264_initialized;
    struct openmax_context openmax_ctx;
    enum AVPixelFormat pix_fmt;
} OpenMAXDecoderContext;

static int openmax_decode(AVCodecContext *avctx,
        void *data, int *got_frame, AVPacket *avpkt)
{
}
static av_cold int openmax_close(AVCodecContext *avctx)
{
    VDADecoderContext *ctx = avctx->priv_data;
    /* release buffers and decoder */
    ff_vda_destroy_decoder(&ctx->openmax_ctx);
    /* close H.264 decoder */
    if (ctx->h264_initialized)
        ff_h264_decoder.close(avctx);
    return 0;
}
static av_cold int openmax_init(AVCodecContext *avctx)
{
    OpenMAXDecoderContext *ctx = avctx->priv_data;
    struct openmax_context *openmax_ctx = &ctx->openmax_ctx;
    OSStatus status;
    int ret;

    ctx->h264_initialized = 0;

    /* check if OpenMAX supports this file */
    if (check_format(avctx) < 0)
        goto failed;

    /* init vda */
    memset(openmax_ctx, 0, sizeof(struct vda_context));
    openmax_ctx->width = avctx->width;
    openmax_ctx->height = avctx->height;
    openmax_ctx->format = 'avc1';
    openmax_ctx->use_sync_decoding = 1;
    ctx->pix_fmt = avctx->get_format(avctx, avctx->codec->pix_fmts);
    switch (ctx->pix_fmt) {
    case AV_PIX_FMT_UYVY422:
        openmax_ctx->cv_pix_fmt_type = '2vuy';
        break;
    case AV_PIX_FMT_YUYV422:
        openmax_ctx->cv_pix_fmt_type = 'yuvs';
        break;
    case AV_PIX_FMT_NV12:
        openmax_ctx->cv_pix_fmt_type = '420v';
        break;
    case AV_PIX_FMT_YUV420P:
        openmax_ctx->cv_pix_fmt_type = 'y420';
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported pixel format: %d\n", avctx->pix_fmt);
        goto failed;
    }
    status = ff_openmax_create_decoder(openmax_ctx,
                                   avctx->extradata, avctx->extradata_size);
    if (status != kVDADecoderNoErr) {
        av_log(avctx, AV_LOG_ERROR,
                "Failed to init VDA decoder: %d.\n", status);
        goto failed;
    }
    avctx->hwaccel_context = openmax_ctx;

    /* changes callback functions */
    avctx->get_format = get_format;
    avctx->get_buffer2 = get_buffer2;
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
