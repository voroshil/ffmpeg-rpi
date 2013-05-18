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

#define VIDEO_ENCODE_INPUT_PORT  201
#define VIDEO_ENCODE_OUTPUT_PORT 201
#define VIDEO_DECODE_INPUT_PORT  130
#define VIDEO_DECODE_OUTPUT_PORT 131

static int openmax_set_encoder_bitrate(OMX_VIDEO_PARAM_BITRATETYPE *pbitrateType, COMPONENT_T *video_encode)
{
   // set current bitrate to 1Mbit
   memset(pbitrateType, 0, sizeof(OMX_VIDEO_PARAM_BITRATETYPE));
   pbitrateType->nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
   pbitrateType->nVersion.nVersion = OMX_VERSION;
   pbitrateType->eControlRate = OMX_Video_ControlRateVariable;
   pbitrateType->nTargetBitrate = 1000000;
   pbitrateType->nPortIndex = VIDEO_ENCODE_OUTPUT_PORT;
   r = OMX_SetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoBitrate, pbitrateType);
   if (r != OMX_ErrorNone) {
      av_log(avctx, AV_LOG_ERROR, "OMX_SetParameter() for bitrate for video_encode port 201 failed with %x!\n",r);
      return -1;
   }

   // get current bitrate
   memset(pbitrateType, 0, sizeof(OMX_VIDEO_PARAM_BITRATETYPE));
   pbitrateType->nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
   pbitrateType->nVersion.nVersion = OMX_VERSION;
   pbitrateType->nPortIndex = VIDEO_ENCODE_OUTPUT_PORT;

   if (OMX_GetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoBitrate,pbitrateType) != OMX_ErrorNone) {
      av_log(avctx, AV_LOG_ERROR, "OMX_GetParameter() for video_encode for bitrate port 201 failed!\n");
      return -1;
   }
}

int ff_openmax_create_decoder(struct openmax_context *ctx,
                          uint8_t *extradata,
                          int extradata_size);
{
   if ((ctx->client = ilclient_init()) == NULL) {
      return -3;
   }

   if (OMX_Init() != OMX_ErrorNone) {
      ilclient_destroy(ctx->client);
      return -4;
   }
   // create video_encode
   r = ilclient_create_component(client, &video_encode, "video_encode",
				 ILCLIENT_DISABLE_ALL_PORTS |
				 ILCLIENT_ENABLE_INPUT_BUFFERS |
				 ILCLIENT_ENABLE_OUTPUT_BUFFERS);
   if (r != 0) {
      av_log(avctx, AV_LOG_ERROR, "ilclient_create_component() for video_encode failed with %x!\n",r);
      return -1;
   }
   list[0] = video_encode;

   // get current settings of video_encode component from port 200
   memset(&def, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
   def.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
   def.nVersion.nVersion = OMX_VERSION;
   def.nPortIndex = VIDEO_ENCODE_INPUT_PORT;

   if (OMX_GetParameter(ILC_GET_HANDLE(ctx->video_encode), OMX_IndexParamPortDefinition, &def) != OMX_ErrorNone) {
      av_log(avctx, AV_LOG_ERROR, "OMX_GetParameter() for video_encode port 200 failed!\n");
      return -1;
   }

   print_def(def);

   // Port 200: in 1/1 115200 16 enabled,not pop.,not cont. 320x240 320x240 @1966080 20
   def.format.video.nFrameWidth = WIDTH;
   def.format.video.nFrameHeight = HEIGHT;
   def.format.video.xFramerate = 30 << 16;
   def.format.video.nSliceHeight = def.format.video.nFrameHeight;
   def.format.video.nStride = def.format.video.nFrameWidth;
   def.format.video.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;

   print_def(def);

   r = OMX_SetParameter(ILC_GET_HANDLE(ctx->video_encode), OMX_IndexParamPortDefinition, &def);
   if (r != OMX_ErrorNone) {
      av_log(avctx, AV_LOG_ERROR, "OMX_SetParameter() for video_encode port 200 failed with %x!\n",r);
      return -1;
   }

   memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
   format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
   format.nVersion.nVersion = OMX_VERSION;
   format.nPortIndex = VIDEO_ENCODE_OUTPUT_PORT;
   format.eCompressionFormat = OMX_VIDEO_CodingAVC;

   av_log(avctx, AV_LOG_DEBUG, "OMX_SetParameter for video_encode:201...\n");
   r = OMX_SetParameter(ILC_GET_HANDLE(ctx->video_encode), OMX_IndexParamVideoPortFormat, &format);
   if (r != OMX_ErrorNone) {
      av_log(avctx, AV_LOG_ERROR, "OMX_SetParameter() for video_encode port 201 failed with %x!\n", r);
      return -1;
   }

   OMX_VIDEO_PARAM_BITRATETYPE bitrateType;
   if (openmax_set_bitrate(&bitrateType, ctx->video_encode) < 0)
   {
     av_log(avctx, AV_LOG_ERROR, "unable to set bitrate!\n",r);
     return -1;
   }
   av_log(avctx, AV_LOG_VERBOSE, "Current Bitrate=%u\n",bitrateType.nTargetBitrate);

   av_log(avctx, AV_LOG_DEBUG, "encode to idle...\n");
   if (ilclient_change_component_state(ctx->video_encode, OMX_StateIdle) == -1) {
      av_log(avctx, AV_LOG_WARN, "ilclient_change_component_state(video_encode, OMX_StateIdle) failed\n");
   }

   av_log(avctx, AV_LOG_DEBUG, "enabling port buffers for 200...\n");
   if (ilclient_enable_port_buffers(ctx->video_encode, VIDEO_ENCODE_INPUT_PORT, NULL, NULL, NULL) != 0) {
      av_log(avctx, AV_LOG_ERROR, "enabling port buffers for 200 failed!\n");
      return -1;
   }

   av_log(avctx, AV_LOG_DEBUG, "enabling port buffers for 201...\n");
   if (ilclient_enable_port_buffers(ctx->video_encode, VIDEO_ENCODE_OUTPUT_PORT, NULL, NULL, NULL) != 0) {
      av_log(avctx, AV_LOG_ERROR, "enabling port buffers for 201 failed!\n");
      return -1;
   }

   av_log(avctx, AV_LOG_DEBUG, "encode to executing...\n");
   ilclient_change_component_state(ctx->video_encode, OMX_StateExecuting);
}

int ff_openmax_destroy_decoder(AVCodecContext *avctx, struct openmax_context *ctx)
{
   av_log(avctx, AV_LOG_DEBUG, "disabling port buffers for 200...\n");
   ilclient_disable_port_buffers(ctx->video_encode, VIDEO_ENCODE_INPUT_PORT, NULL, NULL, NULL);
   av_log(avctx, AV_LOG_DEBUG, "disabling port buffers for 201...\n");
   ilclient_disable_port_buffers(ctx->video_encode, VIDEO_ENCODER_OUTPUT_PORT, NULL, NULL, NULL);

   ilclient_state_transition(ctx->list, OMX_StateIdle);
   ilclient_state_transition(ctx->list, OMX_StateLoaded);

   ilclient_cleanup_components(ctx->list);

   OMX_Deinit();

   ilclient_destroy(ctx->client);
}
static int openmax_h264_start_frame(AVCodecContext *avctx,
                                av_unused const uint8_t *buffer,
                                av_unused uint32_t size)
{
}
static int openmax_h264_decode_slice(AVCodecContext *avctx,
                                 const uint8_t *buffer,
                                 uint32_t size)
{
}
static int openmax_h264_end_frame(AVCodecContext *avctx)
{
      buf = ilclient_get_input_buffer(video_encode, VIDEO_ENCODE_INPUT_BUFFER, 1);
      if (buf == NULL) {
	 av_lov(avctx, AV_LOG_WARNING, "Unable to get input buffer!\n");
         return -1;
      }

	 /* fill it */
	 generate_test_card(buf->pBuffer, &buf->nFilledLen, framenumber++);

	 if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_encode), buf) !=
	     OMX_ErrorNone) {
	    printf("Error emptying buffer!\n");
	 }

	 out = ilclient_get_output_buffer(video_encode, 201, 1);

	 r = OMX_FillThisBuffer(ILC_GET_HANDLE(video_encode), out);
	 if (r != OMX_ErrorNone) {
	    printf("Error filling buffer: %x\n", r);
	 }

	 if (out != NULL) {
	    if (out->nFlags & OMX_BUFFERFLAG_CODECCONFIG) {
	       int i;
	       for (i = 0; i < out->nFilledLen; i++)
		  printf("%x ", out->pBuffer[i]);
	       printf("\n");
	    }

	    r = fwrite(out->pBuffer, 1, out->nFilledLen, outf);
	    if (r != out->nFilledLen) {
	       printf("fwrite: Error emptying buffer: %d!\n", r);
	    }
	    else {
	       printf("Writing frame %d/%d\n", framenumber, NUMFRAMES);
	    }
	    out->nFilledLen = 0;
	 }
	 else {
	    printf("Not getting it :(\n");
	 }

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

