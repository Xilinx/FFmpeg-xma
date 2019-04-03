/*
 * Copyright (c) 2019 Xilinx 
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3.0 of the License, or (at your option) any later version.
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

/**
 * @file
 * Xilinx XMA Copy Kernel Encoder
 */
#include "libavutil/internal.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "internal.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <xma.h>

#define NUM_BUFFERS 2  //Number of frames pipelined
typedef struct XlnxCopyContext {
    const AVClass     *class;
    XmaEncoderSession *m_pEnc_session;
    uint32_t          in_frame;
    uint32_t          out_frame;
    uint32_t          fixed_qp;
    uint32_t          gop_size;
    uint32_t          bitrate;
    uint32_t          idr_period;
    uint32_t          fps;
    uint64_t          pts[NUM_BUFFERS]; 
    uint64_t          dts[NUM_BUFFERS]; 
} XlnxCopyContext;

#define OFFSET(x) offsetof(XlnxCopyContext, x)

static const AVOption options[] = {
    { NULL },
};

static av_cold int xlnx_copy_encode_init(AVCodecContext *avctx)
{
    XlnxCopyContext *ctx = avctx->priv_data;
    XmaEncoderProperties enc_props;
    av_log(NULL, AV_LOG_INFO, "INFO: Initializing Xilinx Copy Encoder: avctx->encoder= 0x%x\n", (unsigned int)ctx);

    ctx->fixed_qp = 0;
    ctx->bitrate = 0;
    ctx->gop_size = 0;
    ctx->idr_period = 0;
	
    if (avctx->bit_rate > 0)     
       ctx->bitrate = avctx->bit_rate;    
    else	
       ctx->fixed_qp = avctx->global_quality;	

    if (avctx->gop_size > 0) 
       ctx->gop_size = avctx->gop_size;

    if (avctx->time_base.den > 0) {
        int fpsNum = avctx->time_base.den;
        int fpsDenom = avctx->time_base.num * avctx->ticks_per_frame;
        int fps = fpsNum/fpsDenom;
        ctx->fps = fps;
    }
	
    enc_props.hwencoder_type = XMA_COPY_ENCODER_TYPE;
    strcpy(enc_props.hwvendor_string, "Xilinx");
    enc_props.format = XMA_YUV420_FMT_TYPE;
    enc_props.bits_per_pixel = 8;
    enc_props.width = avctx->width;
    enc_props.height = avctx->height;
    enc_props.framerate.numerator = ctx->fps;
    enc_props.framerate.denominator = 1;
    enc_props.bitrate = ctx->bitrate;
    enc_props.qp = ctx->fixed_qp;
    enc_props.gop_size = ctx->gop_size;
    enc_props.idr_interval = ctx->idr_period;

    av_log(NULL, AV_LOG_INFO, "INFO: Creating encoder session\n");
    if (NULL == (ctx->m_pEnc_session = xma_enc_session_create(&enc_props)))
    {
       av_log(NULL, AV_LOG_ERROR, "ERROR: Encoder channel request denied for format = %d, bpp = %d, WxH = %d x %d; Check maximum channel capacity supported for the encoder\n", 
              enc_props.format, enc_props.bits_per_pixel, enc_props.width, enc_props.height);
       return -1;
    }
    return 0;
}

static int xlnx_copy_encode(AVCodecContext *avctx, AVPacket *pkt, const AVFrame *pic, int *got_packet)
{	
    XlnxCopyContext *ctx = avctx->priv_data;
    XmaFrameData f_data;
    XmaFrameProperties f_props;
    XmaFrame *xframe;
    XmaDataBuffer *out_buffer;
	
    int32_t out_size = 0; 
    unsigned int out_packet_size = 0;
    int rc = 0, pip_inIdx=0, pip_outIdx=0;

    *got_packet = 0;
    
    // Clone input AVFrame to XmaFrame
    f_props.format = XMA_YUV420_FMT_TYPE;
    f_props.width = avctx->width;
    f_props.height = avctx->height;
    f_props.bits_per_pixel = 8;
	
    // Allocate ouput data packet
    out_packet_size = f_props.width * f_props.height * 1.5;   // Output is in YUV420p format
    rc = ff_alloc_packet2(avctx, pkt, out_packet_size, out_packet_size);
    if (rc < 0)
    {
       av_log(NULL, AV_LOG_ERROR, "ERROR: Failed to allocate ff_packet\n");
       return rc;
    }

    // Clone output data buffer
    out_buffer = xma_data_from_buffer_clone(pkt->data, out_packet_size);

    if (pic) {
       f_data.data[0] = pic->data[0];
       f_data.data[1] = pic->data[1];
       f_data.data[2] = pic->data[2];
       xframe         = xma_frame_from_buffers_clone(&f_props, &f_data);
       xframe->pts    = pic->pts;      

       if (ctx->in_frame % NUM_BUFFERS == 0)
           pip_inIdx =0;
       else
           pip_inIdx++;
       ctx->pts[pip_inIdx] = pic->pts;           
       ctx->dts[pip_inIdx] = pic->pkt_dts;           
       // Send input frame to copy encoder
       rc = xma_enc_session_send_frame(ctx->m_pEnc_session, xframe);
       if (rc == XMA_SEND_MORE_DATA) {
          av_log(NULL, AV_LOG_INFO, "INFO: Encoder requires more input frames to start processing\n");
          ctx->in_frame++;
          *got_packet = 0;
          return 0;
       }
       else if (rc != 0) {
          av_log(NULL, AV_LOG_ERROR, "ERROR: Failed to send input frame to encoder\n");
          return rc;
       }
       ctx->in_frame++;

       // Receive data from copy encoder
       rc = xma_enc_session_recv_data(ctx->m_pEnc_session, out_buffer, &out_size);
       //av_log(NULL, AV_LOG_INFO, "INFO: Output Size = %d for frame %d \n", out_size, ctx->out_frame);
       if(out_size > 0) {
          if (ctx->out_frame % NUM_BUFFERS == 0)
             pip_outIdx =0;
          else
             pip_outIdx++;

          pkt->pts = ctx->pts[pip_outIdx];           
          pkt->dts = ctx->dts[pip_outIdx];           

          *got_packet = 1;
          ctx->out_frame++;
       }
    } else {
       if (ctx->out_frame >= ctx->in_frame) {
          *got_packet = 0;
          return AVERROR_EOF;
       }
       else {
          f_props.width = 1920;
          f_props.height = 1080;
          f_data.data[0] = NULL;
          f_data.data[1] = NULL;
          f_data.data[2] = NULL;		
       }
       //Receive last output frames from pipeline by sending NULL frames    
       xframe = xma_frame_alloc(&f_props);		
       xframe->data[0].buffer = NULL;
       xframe->data[1].buffer = NULL;
       xframe->data[2].buffer = NULL;			

       rc = xma_enc_session_send_frame(ctx->m_pEnc_session, xframe);
       rc = xma_enc_session_recv_data(ctx->m_pEnc_session, out_buffer, &out_size);

       if(out_size > 0) {
          if (ctx->out_frame % NUM_BUFFERS == 0)
             pip_outIdx =0;
          else
             pip_outIdx++;

          pkt->pts    = ctx->pts[pip_outIdx];           
          pkt->dts    = ctx->dts[pip_outIdx];           

          *got_packet = 1;
          ctx->out_frame++;
       }

       // Release resources
       if(xframe)
          xma_frame_free(xframe);
       if(out_buffer)
          xma_data_buffer_free(out_buffer);

       return 0;	
    }	
  		
	
    // Release resources
    if(xframe)
       xma_frame_free(xframe);
    if(out_buffer)
       xma_data_buffer_free(out_buffer);
	
    return 0;
}

static av_cold int xlnx_copy_encode_close(AVCodecContext *avctx)
{
    int rc = 0;
    XlnxCopyContext *ctx = avctx->priv_data;
    rc = xma_enc_session_destroy(ctx->m_pEnc_session);
    if (rc != 0)
    {
       av_log(NULL, AV_LOG_ERROR, "ERROR: Failed to destroy encoder session\n");
       return rc;		
    }
    av_log(NULL, AV_LOG_INFO, "INFO: Closed Xilinx Copy Encoder session\n");
    return 0;
}

static const enum AVPixelFormat xlnx_copy_csp_eight[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NONE
};

static av_cold void xlnx_copy_encode_init_csp(AVCodec *codec)
{
    codec->pix_fmts = xlnx_copy_csp_eight;
}

static const AVClass class = {
    .class_name = "xlnxcopy",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_xlnx_copy_encoder = {
    .name             = "xlnx_copy_enc",
    .long_name        = NULL_IF_CONFIG_SMALL("Xilinx Example Encoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_XLNX_COPY,
    .init             = xlnx_copy_encode_init,
    .init_static_data = xlnx_copy_encode_init_csp,
    .encode2          = xlnx_copy_encode,
    .close            = xlnx_copy_encode_close,
    .priv_data_size   = sizeof(XlnxCopyContext),
    .priv_class       = &class,
    .capabilities     = CODEC_CAP_DELAY,
};
