/*
 * OpenMAX HW acceleration
 *
 * copyright (c) 2013 Vladimir Voroshilor
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

#ifndef AVCODEC_OPENMAX_H
#define AVCODEC_OPENMAX_H

#include <stdint.h>
#include "libavcodec/version.h"
#include "avcodec.h"

#define OMX_SKIP64BIT
#define USE_VCHIQ_ARM
#define USE_EXTERNAL_LIBBCM_HOST
#define USE_EXTERNAL_OMX
#define HAVE_LIBBCM_HOST
#define HAVE_LIBOPENMAX=2
#define OMX


#undef NDEBUG
#include "IL/OMX_Video.h"
#include "ilclient.h"

struct openmax_context {
   ILCLIENT_T *client;
   COMPONENT_T *list[5];
   COMPONENT_T *video_decode;
   int width;
   int height;
   int pix_fmt;
   OMX_VIDEO_CODINGTYPE openmax_input_format;
   OMX_COLOR_FORMATTYPE openmax_output_format;
   int nInputPortIndex;
   int nOutputPortIndex;
   int changed;
   AVFrame *frame;
   int frame_size;
   uint8_t packet[16<<10];
   int packet_size;
   int first;
};

/** Create the video decoder. */
int ff_openmax_create_decoder(AVCodecContext * avctx, struct openmax_context *openmax_ctx,
                          uint8_t *extradata,
                          int extradata_size);

/** Destroy the video decoder. */
int ff_openmax_destroy_decoder(AVCodecContext *avctx, struct openmax_context *openmax_ctx);

#endif /* AVCODEC_OPENMAX_H */
