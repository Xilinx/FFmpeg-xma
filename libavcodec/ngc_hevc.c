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
#include "xmaapi.h"



#define SDXL
#define NUM_REF_FRAMES_NGC 40
typedef struct ngc265_encoder
{

    unsigned int      m_nFrameNum;
    unsigned int      m_nOutFrameNum;
    XmaEncoderSession *m_pEnc_session;
}ngc265_encoder;


typedef struct ngc265Context {
    const AVClass *class;

    ngc265_encoder encoder;
    AVFrame        *tmp_frame;
    //ngc265_param   *params;
    int paramSet;
    int nFrameNum;
    unsigned int FixedQP;
    unsigned int Intra_Period;
    unsigned int fps;
    unsigned int bitrateKbps;
    unsigned int rc_lookahead;
    unsigned int aq_mode;
    unsigned int temp_aq_gain;
    unsigned int spat_aq_gain;
    int qp_offset_I;
    int qp_offset_B0;
    int qp_offset_B1;
    int qp_offset_B2;
    unsigned int idr_period;
} ngc265Context;



#define OFFSET(x) offsetof(ngc265Context, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    //{ "qp",          "Constant quantization parameter rate control method",OFFSET(cqp),        AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, INT_MAX, VE },
    { "aq-mode",       "AQ method",                                OFFSET(aq_mode),       AV_OPT_TYPE_INT,    { .i64 = 1 }, 0, 1, VE, "aq_mode"},
    { "aq-temp-gain", "Temporal AQ strength. Reduces blocking and blurring in flat and textured areas.", OFFSET(temp_aq_gain), AV_OPT_TYPE_INT, {.i64 = 100}, 50, 200, VE,"temp_aq_gain"},
    { "aq-spat-gain", "Spatial AQ strength. Reduces blocking and blurring in flat and textured areas.", OFFSET(spat_aq_gain), AV_OPT_TYPE_INT, {.i64 = 100}, 50, 200, VE,"spat_aq_gain"},
    { "rc-lookahead",  "Number of frames to look ahead for frametype and ratecontrol", OFFSET(rc_lookahead), AV_OPT_TYPE_INT, { .i64 = 30 }, 8, 64, VE },
    { "qp-offset-I", "Offset for QP of I Frames",OFFSET(qp_offset_I),AV_OPT_TYPE_INT,   { .i64 = -4 }, -10, 10, VE,"QP_I_Offset" },
    { "qp-offset-B0", "Offset for QP of B0 Frames",OFFSET(qp_offset_B0),AV_OPT_TYPE_INT,   { .i64 = 1 }, -10, 10, VE,"QP_B0_Offset" },
    { "qp-offset-B1", "Offset for QP of B1 Frames",OFFSET(qp_offset_B1),AV_OPT_TYPE_INT,   { .i64 = 2 }, -10, 10, VE,"QP_B1_Offset" },
    { "qp-offset-B2", "Offset for QP of B2 Frames",OFFSET(qp_offset_B2),AV_OPT_TYPE_INT,   { .i64 = 2 }, -10, 10, VE,"QP_B2_Offset" },
    //{ "deblock",       "Loop filter parameters, in <alpha:beta> form.",   OFFSET(deblock),       AV_OPT_TYPE_STRING, { 0 },  0, 0, VE},
    //{ "ngc265-opts",  "Override the x264 configuration using a :-separated list of key=value parameters", OFFSET(ngc265_opts), AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE },
    { "idr-period",       "IDR Period",                                OFFSET(idr_period),       AV_OPT_TYPE_INT,    { .i64 = 0 }, 0, INT_MAX, VE, "idr_period"},
    { NULL },
};



static av_cold int ngc265_encode_close(AVCodecContext *avctx)
{
    ngc265Context *ctx = avctx->priv_data;
    printf("ngc close\n");
    av_frame_free(&ctx->tmp_frame);
    return 0;
}


static av_cold int ngc265_encode_init(AVCodecContext *avctx)
{
    ngc265Context *ctx = avctx->priv_data;
    int err;
    //avctx->coded_frame = av_frame_alloc();
    printf("ngc265_encode_init avctx->encoder= 0x%x\n",(unsigned int)&ctx->encoder);

    ctx->encoder.m_nFrameNum = 0;
    ctx->encoder.m_nOutFrameNum = 0;
    ctx->paramSet = 0;
    ctx->nFrameNum = 0;


    ctx->FixedQP = 35;
    if (avctx->bit_rate > 0) {
      printf("bitrate set as %ld\n",avctx->bit_rate);
      ctx->bitrateKbps = avctx->bit_rate/1000;
    }
    if (avctx->gop_size > 0) {
      printf("gop set as %d\n",avctx->gop_size);
      ctx->Intra_Period = avctx->gop_size;
      printf("gop set as %d\n",ctx->Intra_Period);
    }
    if (avctx->global_quality > 0){
      printf("qp set as %d\n",avctx->global_quality/FF_QP2LAMBDA);
      ctx->FixedQP = avctx->global_quality/FF_QP2LAMBDA;
    }
    printf("fps set as %d/%d\n",avctx->framerate.num,avctx->framerate.den);
    if (avctx->time_base.den > 0) {
        int fpsNum = avctx->time_base.den;
        int fpsDenom = avctx->time_base.num * avctx->ticks_per_frame;
        int fps = fpsNum/fpsDenom;
        printf("fps set as %d/%d=%d\n",avctx->framerate.num,avctx->framerate.den,fps);
        ctx->fps = fps;
        printf("fps set\n");
    }
    if(avctx->max_b_frames == 0)
    {
        printf("No B- frames use MrfMode \n");
    }
    if(ctx->rc_lookahead)
      printf("la=%d\n",ctx->rc_lookahead);
    if(ctx->aq_mode)
      printf("aq=%d\n",ctx->aq_mode);
    if(ctx->idr_period)
      printf("idr=%d\n",ctx->idr_period);
    if(ctx->temp_aq_gain)
      printf("temp_aq_gain = %d\n",ctx->temp_aq_gain);
    if(ctx->spat_aq_gain)
      printf("spat_aq_gain = %d\n",ctx->spat_aq_gain);
    if(ctx->qp_offset_I)
      printf("qp I Offset = %d\n",ctx->qp_offset_I);
    if(ctx->qp_offset_B0)
      printf("qp B0 Offset = %d\n",ctx->qp_offset_B0);
    if(ctx->qp_offset_B1)
      printf("qp B1 Offset = %d\n",ctx->qp_offset_B1);
    if(ctx->qp_offset_B2)
      printf("qp B2 Offset = %d\n",ctx->qp_offset_B2);
    ctx->paramSet = 1;
    ctx->tmp_frame = av_frame_alloc();
    if (!ctx->tmp_frame)
        printf("temp frame alloc failed\n");
    else
    {
      int ret = av_frame_get_buffer(ctx->tmp_frame, 32);
      ctx->tmp_frame->width  = avctx->width;
      ctx->tmp_frame->height = avctx->height;
      ctx->tmp_frame->format = AV_PIX_FMT_YUV420P;
      if (ret < 0)
        printf("tmp frame get buffer failed\n");
      else
      {
        printf("tmp frame get buffer passed linesize = %d,%d and %d\n",ctx->tmp_frame->linesize[0],ctx->tmp_frame->linesize[1],ctx->tmp_frame->linesize[2]);
      }

    }
    printf("width=%d height = %d rc=%d\n",avctx->width,avctx->height,avctx->bit_rate);
    XmaEncoderProperties enc_props;
    enc_props.hwencoder_type = XMA_VP9_ENCODER_TYPE;

    strcpy(enc_props.hwvendor_string, "NGCodec");

    enc_props.format = XMA_YUV420_FMT_TYPE;
    enc_props.bits_per_pixel = 8;
    enc_props.width = avctx->width;
    enc_props.height = avctx->height;
    enc_props.framerate.numerator = ctx->fps;
    enc_props.framerate.denominator = 1;
    enc_props.bitrate = avctx->bit_rate;
    enc_props.qp = ctx->FixedQP;
    enc_props.gop_size = ctx->Intra_Period;
    enc_props.idr_interval = ctx->idr_period;
    enc_props.lookahead_depth = ctx->rc_lookahead;
    enc_props.qp_offset_I = ctx->qp_offset_I;
    enc_props.qp_offset_B0 = ctx->qp_offset_B0;
    enc_props.qp_offset_B1 = ctx->qp_offset_B1;
    enc_props.qp_offset_B2 = ctx->qp_offset_B2;
    enc_props.temp_aq_gain = ctx->temp_aq_gain;
    enc_props.spat_aq_gain = ctx->spat_aq_gain;
    printf("creating enc session\n");
    ctx->encoder.m_pEnc_session = xma_enc_session_create(&enc_props);
    printf("session : %x \n",ctx->encoder.m_pEnc_session);
    return 0;
}


static int ngc265_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                const AVFrame *pic, int *got_packet)
{
    ngc265Context *ctx = avctx->priv_data;
    unsigned long out_size,ptr;
    int rc = 0;
    if(!pic)
    {

      printf("got enc EOF\n");
      if(ctx->encoder.m_nOutFrameNum >= ctx->encoder.m_nFrameNum)
      {
        printf("returning EOF\n");
        *got_packet = 0;
        return AVERROR_EOF;
      }

      rc = xma_enc_session_send_frame(ctx->encoder.m_pEnc_session, NULL);

      printf("out num %d in num %d\n",ctx->encoder.m_nOutFrameNum,ctx->encoder.m_nFrameNum);
      char *temp = malloc(avctx->width * avctx->height * (ctx->encoder.m_nOutFrameNum - ctx->encoder.m_nFrameNum));
      unsigned long nSize = 0;
      while(ctx->encoder.m_nOutFrameNum < ctx->encoder.m_nFrameNum)
      {
          out_size = 0;
            XmaDataBuffer *out_buffer = xma_data_from_buffer_clone(temp+nSize, avctx->width * avctx->height);
            do{
              rc = xma_enc_session_recv_data(ctx->encoder.m_pEnc_session, out_buffer, &out_size);
              nSize += out_size;

            }while(out_size == 0);

            ctx->encoder.m_nOutFrameNum++;
      }
      	if(nSize > 0)
        {
      	     printf("nsize=%d\n",nSize);
             int rc = ff_alloc_packet(pkt, nSize);
      	     memcpy(pkt->data,temp,nSize);
      	     free(temp);
                   if (rc < 0) {
                       av_log(avctx, AV_LOG_ERROR, "Error getting output packet.\n");
                       return rc;
              	}
      	    *got_packet = 1;
      	    return 0;
      	}
        else
      	{
             *got_packet = 0;
             return AVERROR_EOF;
      	}
    }

    *got_packet = 0;
    out_size = 0;
    // Create a frame
    XmaFrameProperties fprops;
    fprops.format = XMA_YUV420_FMT_TYPE;
    fprops.width = avctx->width;
    fprops.height = avctx->height;
    fprops.bits_per_pixel = 8;
    XmaFrameData       frame_data;

    frame_data.data[0] = pic->data[0];
    frame_data.data[1] = pic->data[1];
    frame_data.data[2] = pic->data[2];

    // Only send 1 frame for now until the IP is corrected

    XmaFrame *frame = xma_frame_from_buffers_clone(&fprops,&frame_data);

    rc = xma_enc_session_send_frame(ctx->encoder.m_pEnc_session, frame);
    if(ctx->encoder.m_nFrameNum < (NUM_REF_FRAMES_NGC-1))
    {
        ctx->nFrameNum++;
        ctx->encoder.m_nFrameNum++;
        *got_packet = 0;
        //printf("got packet 0\n");
        return 0;
    }
    out_size = 0;
    rc = ff_alloc_packet(pkt, fprops.width * fprops.height);
    XmaDataBuffer *out_buffer = xma_data_from_buffer_clone(pkt->data, fprops.width * fprops.height);
    do{
        rc = xma_enc_session_recv_data(ctx->encoder.m_pEnc_session, out_buffer, &out_size);
    }while(out_size == 0);
    if(out_size > 0)
    {
        int rc = ff_alloc_packet(pkt, out_size);
        if (rc < 0) {
         av_log(avctx, AV_LOG_ERROR, "Error getting output packet.\n");
         return rc;
        }

        pkt->pts = pic->pts;
        pkt->dts = pic->pts;
        *got_packet = 1;
        ctx->encoder.m_nOutFrameNum++;
        if(ctx->encoder.m_nOutFrameNum == 1)
        {
          //looking for start code
          int nMarker = 0 ;
          unsigned char *temp = pkt->data;
          while(!(temp[0]== 0 && temp[1]== 0 && temp[2] == 0 && temp[3] == 1))
          {
            //printf("looking for start code\n");
            temp+=1;
            nMarker+=1;
          }
          memmove(pkt->data,pkt->data+nMarker,out_size - nMarker);
          rc = ff_alloc_packet(pkt, out_size - nMarker);
	     }
    }

    ctx->nFrameNum++;
    ctx->encoder.m_nFrameNum++;

    return 0;
}


static const enum AVPixelFormat ngc265_csp_eight[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NONE
};


static av_cold void ngc265_encode_init_csp(AVCodec *codec)
{
        codec->pix_fmts = ngc265_csp_eight;
}




static const AVClass class = {
    .class_name = "ngc265",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault ngc265_defaults[] = {
    { "b", "0" },
    { NULL },
};

AVCodec ff_ngc_hevc_encoder = {
    .name             = "NGC265",
    .long_name        = NULL_IF_CONFIG_SMALL("NGCodec H.265 / HEVC"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_HEVC,
    .init             = ngc265_encode_init,
    .init_static_data = ngc265_encode_init_csp,
    .encode2          = ngc265_encode_frame,
    .close            = ngc265_encode_close,
    .priv_data_size   = sizeof(ngc265Context),
    .priv_class       = &class,
    .defaults         = ngc265_defaults,
    .capabilities     = CODEC_CAP_DELAY | CODEC_CAP_AUTO_THREADS,
};
