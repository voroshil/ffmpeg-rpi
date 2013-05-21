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

#ifdef HAVE_BCM_HOST_H
#include "bcm_host.h"
#endif

#include "internal.h"

#define VIDEO_ENCODE_INPUT_PORT  201
#define VIDEO_ENCODE_OUTPUT_PORT 201
#define VIDEO_DECODE_INPUT_PORT  130
#define VIDEO_DECODE_OUTPUT_PORT 131

static void openmax_vcos_log(const VCOS_LOG_CAT_T *cat, VCOS_LOG_LEVEL_T _level, const char *fmt, va_list args)
{
  av_vlog(NULL, AV_LOG_DEBUG, fmt, args);
  av_log(NULL, AV_LOG_DEBUG, "\n");
}

static void print_port_properties(AVCodecContext *avctx,OMX_PARAM_PORTDEFINITIONTYPE* pdef)
{
  av_log(avctx, AV_LOG_DEBUG, "Port %u: %s %u/%u %u %u %s,%s,%s %ux%u %ux%u @%u %u\n",
      pdef->nPortIndex,
      pdef->eDir == OMX_DirInput ? "in" : "out",
      pdef->nBufferCountActual,
      pdef->nBufferCountMin,
      pdef->nBufferSize,
      pdef->nBufferAlignment,
      pdef->bEnabled ? "enabled" : "disabled",
      pdef->bPopulated ? "populated" : "not pop.",
      pdef->bBuffersContiguous ? "contig." : "not cont.",
      pdef->format.video.nFrameWidth,
      pdef->format.video.nFrameHeight,
      pdef->format.video.nStride,
      pdef->format.video.nSliceHeight,
      pdef->format.video.xFramerate,
      pdef->format.video.eColorFormat);
}

static void dump_port_properties(AVCodecContext *avctx, OMX_HANDLETYPE handle, int nPort)
{
  OMX_PARAM_PORTDEFINITIONTYPE def;
  OMX_ERRORTYPE r;

  memset(&def, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
  def.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
  def.nVersion.nVersion = OMX_VERSION;
  def.nPortIndex = nPort;
  r = OMX_GetParameter(handle, OMX_IndexParamPortDefinition, &def);
  if (r != OMX_ErrorNone) {
    av_log(avctx, AV_LOG_ERROR, "OMX_GetParameter(OMX_IndexParamPortDefinition) for port %d of component %p failed with error 0x%x!\n", def.nPortIndex, handle, r);
    return;
  }
  print_port_properties(avctx, &def);
}

static void port_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data)
{
    AVCodecContext *avctx = userdata;
    struct openmax_context *ctx = avctx->hwaccel_context;
    av_log(avctx, AV_LOG_DEBUG, "port settings changed\n");
    ctx->changed = 1;

   av_log(avctx, AV_LOG_DEBUG, "enabling port buffers for %d...\n", VIDEO_DECODE_OUTPUT_PORT);
   if (ilclient_enable_port_buffers(ctx->video_decode, VIDEO_DECODE_OUTPUT_PORT, NULL, NULL, NULL) != 0) {
      av_log(avctx, AV_LOG_ERROR, "enabling port buffers for %d failed!\n", VIDEO_DECODE_OUTPUT_PORT);
   }
   dump_port_properties(avctx, ILC_GET_HANDLE(comp), data);
    
}
static int openmax_set_decoder_bitrate(COMPONENT_T *video_decode, int nPortIndex, int nTargetBitrate)
{
   OMX_VIDEO_PARAM_BITRATETYPE bitrateType;
   OMX_ERRORTYPE r;

   // set new bitrate
   memset(&bitrateType, 0, sizeof(OMX_VIDEO_PARAM_BITRATETYPE));
   bitrateType.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
   bitrateType.nVersion.nVersion = OMX_VERSION;
   bitrateType.eControlRate = OMX_Video_ControlRateVariable;
   bitrateType.nTargetBitrate = nTargetBitrate;
   bitrateType.nPortIndex = nPortIndex;

   r = OMX_SetParameter(ILC_GET_HANDLE(video_decode), OMX_IndexParamVideoBitrate, &bitrateType);
   if (r != OMX_ErrorNone) {
      return -1;
   }

   // get current bitrate
   memset(&bitrateType, 0, sizeof(OMX_VIDEO_PARAM_BITRATETYPE));
   bitrateType.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
   bitrateType.nVersion.nVersion = OMX_VERSION;
   bitrateType.nPortIndex = nPortIndex;

   if (OMX_GetParameter(ILC_GET_HANDLE(video_decode), OMX_IndexParamVideoBitrate,&bitrateType) != OMX_ErrorNone) {
      return -2;
   }

   return bitrateType.nTargetBitrate;
}

int ff_openmax_create_decoder(AVCodecContext *avctx, struct openmax_context *ctx,
                          uint8_t *extradata,
                          int extradata_size)
{
  OMX_ERRORTYPE r;
//  OMX_PARAM_PORTDEFINITIONTYPE def;
  OMX_VIDEO_PARAM_PORTFORMATTYPE format;
  //int bitrate;

#ifdef HAVE_BCM_HOST_H
   bcm_host_init();
#endif


   if ((ctx->client = ilclient_init()) == NULL) {
      return -3;
   }

   vcos_set_vlog_impl(openmax_vcos_log);
   ilclient_set_port_settings_callback(ctx->client, port_callback, avctx);

   r = OMX_Init();
   if (r != OMX_ErrorNone) {
      av_log(avctx, AV_LOG_ERROR, "Error initializing OMX: %x\n", r);
      ilclient_destroy(ctx->client);
      return -4;
   }
   ctx->nInputPortIndex = VIDEO_DECODE_INPUT_PORT;
   ctx->nOutputPortIndex = VIDEO_DECODE_OUTPUT_PORT;
   ctx->changed = 0;
   ctx->first = 1;

   // create video_decode
   r = ilclient_create_component(ctx->client, &ctx->video_decode, "video_decode",
				 ILCLIENT_DISABLE_ALL_PORTS |
				 ILCLIENT_ENABLE_INPUT_BUFFERS |
				 ILCLIENT_ENABLE_OUTPUT_BUFFERS);
   if (r != 0) {
      av_log(avctx, AV_LOG_ERROR, "ilclient_create_component() for video_decode failed with %x!\n",r);
      return -1;
   }
   ctx->list[0] = ctx->video_decode;

   av_log(avctx, AV_LOG_DEBUG, "encode to idle...\n");
   if (ilclient_change_component_state(ctx->video_decode, OMX_StateIdle) == -1) {
      av_log(avctx, AV_LOG_WARNING, "ilclient_change_component_state(video_decode, OMX_StateIdle) failed\n");
   }

   memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
   format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
   format.nVersion.nVersion = OMX_VERSION;
   format.nPortIndex = VIDEO_DECODE_INPUT_PORT;
//   format.xFramerate = (avctx->time_base.den << 16) / (avctx->time_base.num);
//   format.xFramerate /= avctx->ticks_per_frame;
   format.eCompressionFormat = OMX_VIDEO_CodingAVC;

   r = OMX_SetParameter(ILC_GET_HANDLE(ctx->video_decode), OMX_IndexParamVideoPortFormat, &format);
   if (r != OMX_ErrorNone) {
      av_log(avctx, AV_LOG_ERROR, "OMX_SetParameter(OMX_IndexParamVideoPortFormat) for video_decode port %d failed with %x!\n",format.nPortIndex, r);
      return -1;
   }
   dump_port_properties(avctx, ILC_GET_HANDLE(ctx->video_decode), VIDEO_DECODE_INPUT_PORT);

//   def.format.video.nFrameWidth = avctx->width;
//   def.format.video.nFrameHeight = avctx->height;
//   r = OMX_SetParameter(ILC_GET_HANDLE(ctx->video_decode), OMX_IndexParamPortDefinition, &def);
//   if (r != OMX_ErrorNone) {
//      av_log(avctx, AV_LOG_ERROR, "OMX_SetParameter(OMX_IndexParamPortDefinition) for video_decode port %d failed with %x!\n", def.nPortIndex, r);
//      return -1;
//   }
//   print_def(avctx, def);

   // Port 200: in 1/1 115200 16 enabled,not pop.,not cont. 320x240 320x240 @1966080 20
//   memset(&def, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
//   def.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
//   def.nVersion.nVersion = OMX_VERSION;
//   def.nPortIndex = ctx->nOutputPortIndex;
//   av_log(avctx, AV_LOG_DEBUG, "OMX_GetParameter for video_decode:%d...\n", def.nPortIndex);
//   r = OMX_GetParameter(ILC_GET_HANDLE(ctx->video_decode), OMX_IndexParamPortDefinition, &def);
//   if (r != OMX_ErrorNone) {
//      av_log(avctx, AV_LOG_ERROR, "OMX_GetParameter(OMX_IndexParamPortDefinition) for video_encode port %d failed!\n", def.nPortIndex);
//      return -1;
//   }
//   print_def(avctx, def);

//   def.format.video.xFramerate = (avctx->time_base.den << 16) / (avctx->time_base.num);
//   def.format.video.xFramerate /= avctx->ticks_per_frame;
//   def.format.video.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;

//   print_def(avctx, def);

//   av_log(avctx, AV_LOG_DEBUG, "OMX_SetParameter for video_decode:%d...\n", def.nPortIndex);
//   r = OMX_SetParameter(ILC_GET_HANDLE(ctx->video_decode), OMX_IndexParamPortDefinition, &def);
//   if (r != OMX_ErrorNone) {
//      av_log(avctx, AV_LOG_ERROR, "OMX_SetParameter(OMX_IndexParamPortDefinition) for video_decode port %d failed with %x!\n", def.nPortIndex, r);
//      return -1;
//   }



   av_log(avctx, AV_LOG_DEBUG, "enabling port buffers for %d...\n", VIDEO_DECODE_INPUT_PORT);
   if (ilclient_enable_port_buffers(ctx->video_decode, VIDEO_DECODE_INPUT_PORT, NULL, NULL, NULL) != 0) {
      av_log(avctx, AV_LOG_ERROR, "enabling port buffers for %d failed!\n", VIDEO_DECODE_INPUT_PORT);
      return -1;
   }

#if 0
   av_log(avctx, AV_LOG_DEBUG, "enabling port buffers for %d...\n", VIDEO_DECODE_OUTPUT_PORT);
   if (ilclient_enable_port_buffers(ctx->video_decode, VIDEO_DECODE_OUTPUT_PORT, NULL, NULL, NULL) != 0) {
      av_log(avctx, AV_LOG_ERROR, "enabling port buffers for %d failed!\n", VIDEO_DECODE_OUTPUT_PORT);
      return -1;
   }
#endif
   av_log(avctx, AV_LOG_DEBUG, "encode to executing...\n");
   ilclient_change_component_state(ctx->video_decode, OMX_StateExecuting);

   ctx->frame = av_frame_alloc();

   return 0;
}

int ff_openmax_destroy_decoder(AVCodecContext *avctx, struct openmax_context *ctx)
{
   if (ctx->video_decode) {
   av_log(avctx, AV_LOG_DEBUG, "disabling port buffers for %d...\n", VIDEO_DECODE_INPUT_PORT);
   ilclient_disable_port_buffers(ctx->video_decode, VIDEO_DECODE_INPUT_PORT, NULL, NULL, NULL);
   av_log(avctx, AV_LOG_DEBUG, "disabling port buffers for %d...\n", VIDEO_DECODE_OUTPUT_PORT);
   ilclient_disable_port_buffers(ctx->video_decode, VIDEO_DECODE_OUTPUT_PORT, NULL, NULL, NULL);
   }
   if (ctx->list){
   ilclient_state_transition(ctx->list, OMX_StateIdle);
   ilclient_state_transition(ctx->list, OMX_StateLoaded);

   ilclient_cleanup_components(ctx->list);
   }
   OMX_Deinit();
   if (ctx->client) {
   ilclient_destroy(ctx->client);
   }
#ifdef HAVE_BCM_HOST_H
   bcm_host_deinit();
#endif

  return 0;
}
static int openmax_h264_start_frame(AVCodecContext *avctx,
                            av_unused const uint8_t *buffer,
                                av_unused uint32_t size)
{
    return 0;
}

static int openmax_h264_decode_slice(AVCodecContext *avctx,
                                 const uint8_t *buffer,
                                 uint32_t size)
{
  int copied;
  int data_length = size;
  struct openmax_context *ctx = avctx->hwaccel_context;
  OMX_BUFFERHEADERTYPE *buf;

  av_dlog(avctx, "openmax_decode_slice %p (size=%d)!\n", buffer, size);
  if (!size && ctx->changed) {
  ctx->changed = 0;
  av_log(avctx, AV_LOG_VERBOSE, "openmax_decode_slice %p (size=%d)!\n", buffer, size);
    OMX_BUFFERHEADERTYPE *buf;

    buf = ilclient_get_input_buffer(ctx->video_decode, VIDEO_DECODE_INPUT_PORT, 1);
    if (buf) {
      buf->nFilledLen = 0;
      buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;
      
      OMX_EmptyThisBuffer(ILC_GET_HANDLE(ctx->video_decode), buf);
    }
    return -1;
  }
  for(copied = 0; copied < size; copied += data_length) {
    buf = ilclient_get_input_buffer(ctx->video_decode, VIDEO_DECODE_INPUT_PORT, 1);
    if (buf == NULL) {
      av_log(avctx, AV_LOG_ERROR, "Unable to get input buffer!\n");
      return -1;
    }
    data_length = FFMIN(size - copied, buf->nAllocLen);
    /* fill it */
    memcpy(buf->pBuffer, buffer + copied, data_length);
    buf->nFilledLen = data_length;
    buf->nOffset = 0;

    if (ctx->first) {
        buf->nFlags = OMX_BUFFERFLAG_STARTTIME;
        ctx->first = 0;
    } else {
        buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
    }

    if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(ctx->video_decode), buf) != OMX_ErrorNone) {
       av_log(avctx, AV_LOG_ERROR, "Error emptying buffer!\n");
       return -1;
    }
  }
  return 0;
}

static int openmax_h264_end_frame(AVCodecContext *avctx)
{
    struct openmax_context *ctx = avctx->hwaccel_context;
    OMX_BUFFERHEADERTYPE *out;
    OMX_ERRORTYPE r;
    AVFrame *frame                      = ctx->frame;

    av_dlog(avctx, "openmax_end_frame!\n");
   if (!ctx->changed)
    return -1;
    out = ilclient_get_output_buffer(ctx->video_decode, VIDEO_DECODE_OUTPUT_PORT, 0);
    if (!out) {
        av_log(avctx, AV_LOG_ERROR, "Not getting it :(\n");
        return -1;
    }
    av_dlog(avctx, "openmax_end_frame. pre fill\n");
    r = OMX_FillThisBuffer(ILC_GET_HANDLE(ctx->video_decode), out);
    if (r != OMX_ErrorNone) {
        av_log(avctx, AV_LOG_ERROR, "Error filling buffer: %x\n", r);
        return -1;
    }
    av_dlog(avctx, "openmax_end_frame. post fill %x\n", out->nFlags);
    if (out->nFlags & OMX_BUFFERFLAG_EOS)
    {
	return -1;
    }

    {
        frame->format = ctx->pix_fmt;
	av_frame_unref(frame);
	int ret = ff_reget_buffer(avctx, frame);
	if (ret < 0)
	  return -1;
        memcpy(frame->data[0], out->pBuffer, out->nFilledLen);
        ctx->frame_size = out->nFilledLen;
        out->nFilledLen = 0;
    }
    av_dlog(avctx, "openmax_end_frame. returning 0\n");
    return 0;
}
AVHWAccel ff_h264_openmax_hwaccel = {
    .name           = "h264_openmax",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .pix_fmt        = AV_PIX_FMT_OPENMAX_VLD,
    .start_frame    = openmax_h264_start_frame,
    .decode_slice   = openmax_h264_decode_slice,
    .end_frame      = openmax_h264_end_frame,
};

