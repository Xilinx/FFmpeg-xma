/*
* Copyright (c) 2018 VYUSync Inc
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


/*------------------------------------------------------------------------------------------------------------------------------------------
  
------------------------------------------------------------------------------------------------------------------------------------------*/
typedef struct vyu_h264_dec_ctx {
    const AVClass                            *class;
    char                                      dec_params_name[14][100];
    XmaParameter                              dec_params[14];
    XmaDecoderSession                        *dec_session;
    uint32_t                                  kernel_version;
    uint32_t                                  plugin_version;
    uint32_t                                  ch_index;
    uint32_t                                  log_level;
    uint32_t                                  ref_only;
    uint32_t                                  intra_only;
    char                                     *pixel_format;
    uint32_t                                  max_krnl_enables;
    uint64_t                                  inp_buf_bytes_full;
    uint64_t                                  inp_buf_bytes_free;
    uint32_t                                  comn_yuv_buf_rdsz_luma;
    uint32_t                                  comn_yuv_buf_rdsz_cbcr;
    uint64_t                                  krnl_yuv_buf_addr_luma;
    uint64_t                                  krnl_yuv_buf_addr_chcb;
    uint64_t                                  krnl_yuv_buf_addr_chcr;
    unsigned int                              null_sent;
    XmaFrameProperties                        fprops;
    XmaFrame                                 *xmaFrame;
    int                                       is_mp4_wrapped;
    int                                       nalu_length_size;
} vyu_h264_dec_ctx;


/*------------------------------------------------------------------------------------------------------------------------------------------
  
------------------------------------------------------------------------------------------------------------------------------------------*/
#define OFFSET(x) offsetof(vyu_h264_dec_ctx, x)

#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM


static const AVOption options[] = {
    { "pixelFormat",    "Output Pixel Format",                              OFFSET(pixel_format),     AV_OPT_TYPE_STRING, { .str = "yuv420p" }, 0,          0, VD, "pixelFormat"    },
    { "intraOnly",      "Decode Intra Slices Only",                         OFFSET(intra_only),       AV_OPT_TYPE_BOOL,   { .i64 = 0 },         0,          1, VD, "intraOnly"      },
    { "refOnly",        "Decode Reference Slices Only",                     OFFSET(ref_only),         AV_OPT_TYPE_BOOL,   { .i64 = 0 },         0,          1, VD, "refOnly"        },
    { "logLevel",       "Level for debug messages",                         OFFSET(log_level),        AV_OPT_TYPE_INT,    { .i64 = 0 },         0,         63, VD, "logLevel"       },
    { "maxKrnlEnables", "Maximum number of times kernel should be enabled", OFFSET(max_krnl_enables), AV_OPT_TYPE_INT,    { .i64 = 0 },         0, 2147483647, VD, "maxKrnlEnables" },
    { NULL },
};


/*------------------------------------------------------------------------------------------------------------------------------------------
  
------------------------------------------------------------------------------------------------------------------------------------------*/
static av_cold int vyusynch264_decode_close (
    AVCodecContext                           *avctx
)
{
    vyu_h264_dec_ctx                         *ctx                                      =                         avctx->priv_data;
    
    xma_frame_free(ctx->xmaFrame);
    
    xma_dec_session_destroy(ctx->dec_session);
    
    return 0;
}


/*------------------------------------------------------------------------------------------------------------------------------------------
  
------------------------------------------------------------------------------------------------------------------------------------------*/
static av_cold int vyusynch264_decode_init (
    AVCodecContext                           *avctx
)
{
    /*------------------------------------------------------------------------------------------------------------------------------------------
      
    ------------------------------------------------------------------------------------------------------------------------------------------*/
    vyu_h264_dec_ctx                         *ctx                                      =                         avctx->priv_data;
    XmaDecoderProperties                      dec_props;
    unsigned char                            *mp4_ctx_pointer;
    unsigned char                            *mp4_ctx_end;
    unsigned char                            *param_set_buffer;
    uint32_t                                  param_set_offset;
    uint8_t                                   param_set_type;
    uint8_t                                   param_set_num_val;
    uint8_t                                   param_set_num_cnt;
    uint16_t                                  param_set_size_val;
    uint16_t                                  param_set_size_cnt;
    XmaDataBuffer                            *buf;
    int                                       data_used                                =                                        0;
    
    
    /*------------------------------------------------------------------------------------------------------------------------------------------
      
    ------------------------------------------------------------------------------------------------------------------------------------------*/
    ctx->fprops.width      =    avctx->width;
    ctx->fprops.height     =    avctx->height;
    
    switch (avctx->pix_fmt)
    {
        case AV_PIX_FMT_YUV420P      :  ctx->fprops.format = XMA_YUV420_FMT_TYPE;  ctx->fprops.bits_per_pixel =  8;  break;
        case AV_PIX_FMT_YUV422P      :  ctx->fprops.format = XMA_YUV422_FMT_TYPE;  ctx->fprops.bits_per_pixel =  8;  break;
        case AV_PIX_FMT_YUV444P      :  ctx->fprops.format = XMA_YUV444_FMT_TYPE;  ctx->fprops.bits_per_pixel =  8;  break;
        case AV_PIX_FMT_YUV420P16LE  :  ctx->fprops.format = XMA_YUV420_FMT_TYPE;  ctx->fprops.bits_per_pixel = 16;  break;
        case AV_PIX_FMT_YUV422P16LE  :  ctx->fprops.format = XMA_YUV422_FMT_TYPE;  ctx->fprops.bits_per_pixel = 16;  break;
        case AV_PIX_FMT_YUV444P16LE  :  ctx->fprops.format = XMA_YUV444_FMT_TYPE;  ctx->fprops.bits_per_pixel = 16;  break;
    }
    
    ctx->xmaFrame = xma_frame_alloc(&ctx->fprops);
    
    
    /*------------------------------------------------------------------------------------------------------------------------------------------
        create decoder sesion and set the configuraiton
    ------------------------------------------------------------------------------------------------------------------------------------------*/
    ctx->ch_index    =    0;
    
    strcpy(ctx->dec_params_name[ 0],         "kernel_version");    ctx->dec_params[ 0].name    =    ctx->dec_params_name[ 0];    ctx->dec_params[ 0].type    =    XMA_UINT32;    ctx->dec_params[ 0].length    =    sizeof(ctx->kernel_version        );    ctx->dec_params[ 0].value    =    &ctx->kernel_version;
    strcpy(ctx->dec_params_name[ 1],         "plugin_version");    ctx->dec_params[ 1].name    =    ctx->dec_params_name[ 1];    ctx->dec_params[ 1].type    =    XMA_UINT32;    ctx->dec_params[ 1].length    =    sizeof(ctx->plugin_version        );    ctx->dec_params[ 1].value    =    &ctx->plugin_version;
    strcpy(ctx->dec_params_name[ 2],               "ch_index");    ctx->dec_params[ 2].name    =    ctx->dec_params_name[ 2];    ctx->dec_params[ 2].type    =    XMA_UINT32;    ctx->dec_params[ 2].length    =    sizeof(ctx->ch_index              );    ctx->dec_params[ 2].value    =    &ctx->ch_index;
    strcpy(ctx->dec_params_name[ 3],              "log_level");    ctx->dec_params[ 3].name    =    ctx->dec_params_name[ 3];    ctx->dec_params[ 3].type    =    XMA_UINT32;    ctx->dec_params[ 3].length    =    sizeof(ctx->log_level             );    ctx->dec_params[ 3].value    =    &ctx->log_level;
    strcpy(ctx->dec_params_name[ 4],               "ref_only");    ctx->dec_params[ 4].name    =    ctx->dec_params_name[ 4];    ctx->dec_params[ 4].type    =    XMA_UINT32;    ctx->dec_params[ 4].length    =    sizeof(ctx->ref_only              );    ctx->dec_params[ 4].value    =    &ctx->ref_only;
    strcpy(ctx->dec_params_name[ 5],           "pixel_format");    ctx->dec_params[ 5].name    =    ctx->dec_params_name[ 5];    ctx->dec_params[ 5].type    =    XMA_STRING;    ctx->dec_params[ 5].length    =    sizeof(ctx->pixel_format          );    ctx->dec_params[ 5].value    =     ctx->pixel_format;
    strcpy(ctx->dec_params_name[ 6],       "max_krnl_enables");    ctx->dec_params[ 6].name    =    ctx->dec_params_name[ 6];    ctx->dec_params[ 6].type    =    XMA_UINT32;    ctx->dec_params[ 6].length    =    sizeof(ctx->max_krnl_enables      );    ctx->dec_params[ 6].value    =    &ctx->max_krnl_enables;
    strcpy(ctx->dec_params_name[ 7],     "inp_buf_bytes_full");    ctx->dec_params[ 7].name    =    ctx->dec_params_name[ 7];    ctx->dec_params[ 7].type    =    XMA_UINT64;    ctx->dec_params[ 7].length    =    sizeof(ctx->inp_buf_bytes_full    );    ctx->dec_params[ 7].value    =    &ctx->inp_buf_bytes_full;
    strcpy(ctx->dec_params_name[ 8],     "inp_buf_bytes_free");    ctx->dec_params[ 8].name    =    ctx->dec_params_name[ 8];    ctx->dec_params[ 8].type    =    XMA_UINT64;    ctx->dec_params[ 8].length    =    sizeof(ctx->inp_buf_bytes_free    );    ctx->dec_params[ 8].value    =    &ctx->inp_buf_bytes_free;
    strcpy(ctx->dec_params_name[ 9], "comn_yuv_buf_rdsz_luma");    ctx->dec_params[ 9].name    =    ctx->dec_params_name[ 9];    ctx->dec_params[ 9].type    =    XMA_UINT32;    ctx->dec_params[ 9].length    =    sizeof(ctx->comn_yuv_buf_rdsz_luma);    ctx->dec_params[ 9].value    =    &ctx->comn_yuv_buf_rdsz_luma;
    strcpy(ctx->dec_params_name[10], "comn_yuv_buf_rdsz_cbcr");    ctx->dec_params[10].name    =    ctx->dec_params_name[10];    ctx->dec_params[10].type    =    XMA_UINT32;    ctx->dec_params[10].length    =    sizeof(ctx->comn_yuv_buf_rdsz_cbcr);    ctx->dec_params[10].value    =    &ctx->comn_yuv_buf_rdsz_cbcr;
    strcpy(ctx->dec_params_name[11], "krnl_yuv_buf_addr_luma");    ctx->dec_params[11].name    =    ctx->dec_params_name[11];    ctx->dec_params[11].type    =    XMA_UINT64;    ctx->dec_params[11].length    =    sizeof(ctx->krnl_yuv_buf_addr_luma);    ctx->dec_params[11].value    =    &ctx->krnl_yuv_buf_addr_luma;
    strcpy(ctx->dec_params_name[12], "krnl_yuv_buf_addr_chcb");    ctx->dec_params[12].name    =    ctx->dec_params_name[12];    ctx->dec_params[12].type    =    XMA_UINT64;    ctx->dec_params[12].length    =    sizeof(ctx->krnl_yuv_buf_addr_chcb);    ctx->dec_params[12].value    =    &ctx->krnl_yuv_buf_addr_chcb;
    strcpy(ctx->dec_params_name[13], "krnl_yuv_buf_addr_chcr");    ctx->dec_params[13].name    =    ctx->dec_params_name[13];    ctx->dec_params[13].type    =    XMA_UINT64;    ctx->dec_params[13].length    =    sizeof(ctx->krnl_yuv_buf_addr_chcr);    ctx->dec_params[13].value    =    &ctx->krnl_yuv_buf_addr_chcr;
    
    
    strcpy(dec_props.hwvendor_string, "vyusync");
    dec_props.hwdecoder_type    =    XMA_H264_DECODER_TYPE;
    dec_props.intraOnly         =    ctx->intra_only;
    dec_props.params            =    ctx->dec_params;
    dec_props.param_cnt         =    14;
    
    ctx->dec_session            =    xma_dec_session_create(&dec_props);
    if (!ctx->dec_session) {
        printf("ERROR: Unable to allocate VYUSync H.264 decoder session.\n");
        return -1;
    }
    
    ctx->null_sent              =    0;
    ctx->is_mp4_wrapped         =    0;
    ctx->nalu_length_size       =    4;
    
    
    /*------------------------------------------------------------------------------------------------------------------------------------------
        decode SPS and PPS if in MP4 wrapper
    ------------------------------------------------------------------------------------------------------------------------------------------*/
    if ((avctx->extradata != NULL) && (avctx->extradata_size >= 7))
    {
        mp4_ctx_pointer    =    avctx->extradata;
        mp4_ctx_end        =    mp4_ctx_pointer + avctx->extradata_size;
        
        if (mp4_ctx_pointer[0] == 0x01)
        {
            //---- allocate buffer to store sps and pps.
            //  Minimum number of bytes present in avctx->extradata for each sps/pps = 4 (2 for length, 1 for nal_unit_type and minimum of 1 for nal_unit_data)
            //  Four bytes of start code are added per parameter set. Therefore, in the worst case each 4-byte parameter set can result in 8-bytes of data.
            param_set_buffer    =    malloc(avctx->extradata_size * 2);
            param_set_offset    =    0;
            
            ctx->is_mp4_wrapped    =    1;
            
            //---- read number of bytes used to encode nalu length
            mp4_ctx_pointer        += 4;
            ctx->nalu_length_size  = (mp4_ctx_pointer[0] & 0x03) + 1;
            mp4_ctx_pointer        += 1;
            
            //---- parse SPS and then PPS
            for (param_set_type = 0; param_set_type < 2; param_set_type++)
            {
                if (mp4_ctx_pointer < mp4_ctx_end)
                {
                    //---- read number of parameter sets (SPS is limited to 31)
                    param_set_num_val    =    mp4_ctx_pointer[0];
                    if (param_set_type == 0)
                        param_set_num_val    =    (param_set_num_val & 0x1F);
                    mp4_ctx_pointer++;
                    
                    //---- loop through all parameters sets of one type (either SPS or PPS)
                    for (param_set_num_cnt = 0; param_set_num_cnt < param_set_num_val; param_set_num_cnt++)
                    {
                        if ((mp4_ctx_pointer + 1) < mp4_ctx_end)
                        {
                            //---- read number of bytes in parameter set
                            param_set_size_val    =    ((mp4_ctx_pointer[0] << 8) | mp4_ctx_pointer[1]);
                            mp4_ctx_pointer    +=    2;
                            
                            //---- create the parameter set data to be transmitted to the decoder
                            if ((mp4_ctx_pointer + param_set_size_val) <= mp4_ctx_end)
                            {
                                param_set_buffer[param_set_offset++]    =    0x00;
                                param_set_buffer[param_set_offset++]    =    0x00;
                                param_set_buffer[param_set_offset++]    =    0x00;
                                param_set_buffer[param_set_offset++]    =    0x01;
                                
                                for (param_set_size_cnt = 0; param_set_size_cnt < param_set_size_val; param_set_size_cnt++)
                                    param_set_buffer[param_set_offset++]    =    mp4_ctx_pointer[param_set_size_cnt];
                                
                                mp4_ctx_pointer    +=    param_set_size_val;
                            }
                        }
                    }
                }
            }
            
            //---- transmit the parameter sets to the decoder
            buf    =    xma_data_from_buffer_clone(param_set_buffer, param_set_offset);
            
            xma_dec_session_send_data(ctx->dec_session, buf, &data_used);
            
            xma_data_buffer_free(buf);
            free (param_set_buffer);
        }
    }
    
    
    /*------------------------------------------------------------------------------------------------------------------------------------------
      
    ------------------------------------------------------------------------------------------------------------------------------------------*/
    return 0;
}


/*------------------------------------------------------------------------------------------------------------------------------------------
  
------------------------------------------------------------------------------------------------------------------------------------------*/
static int vyusynch264_decode (
    AVCodecContext                           *avctx,
    void                                     *data,
    int                                      *got_frame,
    AVPacket                                 *avpkt
)
{
    /*------------------------------------------------------------------------------------------------------------------------------------------
      
    ------------------------------------------------------------------------------------------------------------------------------------------*/
    vyu_h264_dec_ctx                         *ctx                                      =                         avctx->priv_data;
    AVFrame                                  *pict                                     =                                     data;
    XmaFrameProperties                        fprops                                   =                                      {0};
    unsigned char                            *planar_data[4]                           =                 {NULL, NULL, NULL, NULL};
    unsigned char                            *packed_data[4]                           =                 {NULL, NULL, NULL, NULL};
    int                                       rc                                       =                                        0;
    int                                       data_used                                =                                        0;
    int                                       offset                                   =                                        0;
    unsigned int                              nalu_size                                =                                        0;
    unsigned char                            *nalu_buf;
    XmaDataBuffer                            *buf;
    
    
    /*------------------------------------------------------------------------------------------------------------------------------------------
        transmit encoded data
    ------------------------------------------------------------------------------------------------------------------------------------------*/
    if (avpkt->size != 0)
    {
        if (ctx->is_mp4_wrapped == 1)
        {
            //---- loop through all available nalu
            while (offset < avpkt->size)
            {
                //---- determine the nalu size
                switch (ctx->nalu_length_size)
                {
                    case 1  :  nalu_size    =      (unsigned int) avpkt->data[offset + 0];         break;
                    
                    case 2  :  nalu_size    =      (unsigned int)(avpkt->data[offset + 0] <<  8)
                                                 | (unsigned int)(avpkt->data[offset + 1] <<  0);  break;
                    
                    case 4  :  nalu_size    =      (unsigned int)(avpkt->data[offset + 0] << 24)
                                                 | (unsigned int)(avpkt->data[offset + 1] << 16)
                                                 | (unsigned int)(avpkt->data[offset + 2] <<  8)
                                                 | (unsigned int)(avpkt->data[offset + 3] <<  0);  break;
                }
                offset    +=    ctx->nalu_length_size;
                
                //---- create nalu
                nalu_buf       =    malloc(nalu_size + 4);
                nalu_buf[0]    =    0x00;
                nalu_buf[1]    =    0x00;
                nalu_buf[2]    =    0x00;
                nalu_buf[3]    =    0x01;
                memcpy (nalu_buf + 4, avpkt->data + offset, nalu_size);
                offset    +=    nalu_size;
                
                //---- transmit nalu
                buf    =    xma_data_from_buffer_clone(nalu_buf, nalu_size + 4);
                
                rc = xma_dec_session_send_data(ctx->dec_session, buf, &data_used);
                if (rc != XMA_SUCCESS)
                {
                    *got_frame    =    0;
                    return 0;
                }
                
                //---- cleanup
                xma_data_buffer_free(buf);
                free(nalu_buf);
            }
            
            data_used    =    avpkt->size;
            
        }
        else
        {
            buf    =    xma_data_from_buffer_clone(avpkt->data, avpkt->size);
            
            rc    =    xma_dec_session_send_data(ctx->dec_session, buf, &data_used);
            if (rc != XMA_SUCCESS)
            {
                *got_frame    =    0;
                return 0;
            }
            
            xma_data_buffer_free(buf);
        }
    }
    else
    if (ctx->null_sent == 0)
    {
        rc    =    xma_dec_session_send_data(ctx->dec_session, NULL, &data_used);
        if (rc != XMA_SUCCESS)
        {
            *got_frame    =    0;
            return 0;
        }
        
        ctx->null_sent    =    1;
    }
    
    
    /*------------------------------------------------------------------------------------------------------------------------------------------
        check for decoded images
    ------------------------------------------------------------------------------------------------------------------------------------------*/
    *got_frame    =    0;
    
    do {
        rc    =    xma_dec_session_get_properties(ctx->dec_session, &fprops);
        if (rc == XMA_ERROR)
        {
            *got_frame    =    0;
            return 0;
        }
        
    } while (    ((rc == XMA_SEND_MORE_DATA) && (avpkt->size != 0) && (ctx->inp_buf_bytes_full != 0))
              || ((rc == XMA_SEND_MORE_DATA) && (avpkt->size == 0))
            );
    
    
    /*------------------------------------------------------------------------------------------------------------------------------------------
        read the decoded image
    ------------------------------------------------------------------------------------------------------------------------------------------*/
    if (rc == XMA_SUCCESS)
    {
        rc    =    xma_dec_session_recv_frame(ctx->dec_session, ctx->xmaFrame);
        if (rc != XMA_SUCCESS)
        {
            *got_frame    =    0;
            return 0;
        }
        
        planar_data[0]    =    ctx->xmaFrame->data[0].buffer;
        planar_data[1]    =    ctx->xmaFrame->data[1].buffer;
        planar_data[2]    =    ctx->xmaFrame->data[2].buffer;
        planar_data[3]    =    ctx->xmaFrame->data[0].buffer;
        
        pict->width     =    fprops.width;
        pict->height    =    fprops.height;
        pict->format    =    avctx->pix_fmt;
        
        pict->linesize[0]    =    fprops.width * ((fprops.bits_per_pixel + 7) >> 3);
        pict->linesize[3]    =    pict->linesize[0];
        switch (fprops.format)
        {
            case XMA_YUV420_FMT_TYPE  :  pict->linesize[1] = pict->linesize[0] / 2;  pict->linesize[2] = pict->linesize[1];  break;
            case XMA_YUV422_FMT_TYPE  :  pict->linesize[1] = pict->linesize[0] / 2;  pict->linesize[2] = pict->linesize[1];  break;
            case XMA_YUV444_FMT_TYPE  :  pict->linesize[1] = pict->linesize[0];      pict->linesize[2] = pict->linesize[1];  break;
            default                   :  *got_frame = 0;  return 0;
        }
        
        if (strcmp(ctx->pixel_format, "yuyv422") == 0)
        {
            pict->format    =    AV_PIX_FMT_YUYV422;
            
            pict->linesize[0] = pict->linesize[0] * 2;
            pict->linesize[1] = 0;
            pict->linesize[2] = 0;
            pict->linesize[3] = 0;
            
            packed_data[0] = malloc(fprops.width * fprops.height * 2);
            packed_data[1] = NULL;
            packed_data[2] = NULL;
            packed_data[3] = NULL;
            
            memcpy(packed_data[0],                                                             planar_data[0], ctx->comn_yuv_buf_rdsz_luma);
            memcpy(packed_data[0] + ctx->comn_yuv_buf_rdsz_luma,                               planar_data[1], ctx->comn_yuv_buf_rdsz_cbcr);
            memcpy(packed_data[0] + ctx->comn_yuv_buf_rdsz_luma + ctx->comn_yuv_buf_rdsz_cbcr, planar_data[2], ctx->comn_yuv_buf_rdsz_cbcr);
            
            av_frame_get_buffer (pict, 32);
            av_image_copy (pict->data, pict->linesize, (const uint8_t**)packed_data, pict->linesize, pict->format, fprops.width, fprops.height);
            
            free(packed_data[0]);
        }
        else
        {
            av_frame_get_buffer (pict, 32);
            av_image_copy (pict->data, pict->linesize, (const uint8_t**)planar_data, pict->linesize, pict->format, fprops.width, fprops.height);
        }
        
        *got_frame    =    1;
    }
    
    
    return data_used;
}


/*------------------------------------------------------------------------------------------------------------------------------------------
  
------------------------------------------------------------------------------------------------------------------------------------------*/
static const enum AVPixelFormat vyusynch264_csp[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV420P16LE,
    AV_PIX_FMT_YUV422P16LE,
    AV_PIX_FMT_YUV444P16LE,
    AV_PIX_FMT_NONE
};


/*------------------------------------------------------------------------------------------------------------------------------------------
  
------------------------------------------------------------------------------------------------------------------------------------------*/
static av_cold void vyusynch264_decode_init_csp (
    AVCodec                                  *codec
)
{
    codec->pix_fmts = vyusynch264_csp;
}


/*------------------------------------------------------------------------------------------------------------------------------------------
  
------------------------------------------------------------------------------------------------------------------------------------------*/
static const AVClass class = {
    .class_name    =    "vyuh264",
    .item_name     =    av_default_item_name,
    .option        =    options,
    .version       =    LIBAVUTIL_VERSION_INT,
};


/*------------------------------------------------------------------------------------------------------------------------------------------
  
------------------------------------------------------------------------------------------------------------------------------------------*/
AVCodec ff_vyusync_h264_decoder = {
    .name                =    "VYUH264",
    .long_name           =    NULL_IF_CONFIG_SMALL("VYUsync H264"),
    .type                =    AVMEDIA_TYPE_VIDEO,
    .id                  =    AV_CODEC_ID_H264,
    .init                =    vyusynch264_decode_init,
    .init_static_data    =    vyusynch264_decode_init_csp,
    .decode              =    vyusynch264_decode,
    .close               =    vyusynch264_decode_close,
    .priv_data_size      =    sizeof(vyu_h264_dec_ctx),
    .priv_class          =    &class,
    .capabilities        =    CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
};
