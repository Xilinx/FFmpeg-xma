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

#define SDXL

typedef struct vyusynch264Context {
    const AVClass *class;

    XmaDecoderSession *m_pDec_session;
    int32_t m_iInputFrames;
    int32_t m_iOutputFrames;
} vyusynch264Context;

#define OFFSET(x) offsetof(vyusynch264Context, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { NULL },
};

static av_cold int vyusynch264_decode_close(AVCodecContext *avctx)
{
    vyusynch264Context *ctx = avctx->priv_data;

    xma_dec_session_destroy(ctx->m_pDec_session);

    return 0;
}

static av_cold int vyusynch264_decode_init(AVCodecContext *avctx)
{
	vyusynch264Context *ctx = avctx->priv_data;
    XmaDecoderProperties dec_props;

    printf("vyusynch264_decode_init avctx->decoder= 0x%x\n",(unsigned int)ctx);

    dec_props.hwdecoder_type = XMA_H264_DECODER_TYPE;
    strcpy(dec_props.hwvendor_string, "vyusync");

    printf("creating dec session\n");
    ctx->m_pDec_session = xma_dec_session_create(&dec_props);
    printf("session : 0x%x \n",(unsigned int)ctx->m_pDec_session);
    ctx->m_iInputFrames = 0;
    ctx->m_iOutputFrames = 0;

    return 0;
}



static int vyusynch264_decode(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
	vyusynch264Context *ctx            = avctx->priv_data;
    AVFrame            *pict           = data;
	XmaFrameProperties fprops          = {0};
	XmaFrame           *xmaFrame;
	unsigned char      *src_data[4]    = {NULL};

	int32_t            data_used       = 0;
    int                rc              = 0;

    if(!avpkt->size)
    {
    	// EOF reached
        rc = xma_dec_session_send_data(ctx->m_pDec_session, NULL, &data_used);
    }else{
    	XmaDataBuffer* buf = xma_data_from_buffer_clone(avpkt->data, avpkt->size);
        rc = xma_dec_session_send_data(ctx->m_pDec_session, buf, &data_used);
        ctx->m_iInputFrames++;
    }
    if (ctx->m_iOutputFrames >= ctx->m_iInputFrames)
    {
        *got_frame = 0;
        return 0;
    }
    rc = xma_dec_session_get_properties(ctx->m_pDec_session, &fprops);
	if (rc != 0)
	{
		if ((ctx->m_iOutputFrames < ctx->m_iInputFrames) && (data_used == 0))
		{
			while (rc != 0)
			{
		        rc = xma_dec_session_send_data(ctx->m_pDec_session, NULL, &data_used);
				rc = xma_dec_session_get_properties(ctx->m_pDec_session, &fprops);
			}
		}else{
			*got_frame = 0;
			return 0;
		}
	}
	xmaFrame = xma_frame_alloc(&fprops);
	rc = xma_dec_session_recv_frame(ctx->m_pDec_session, xmaFrame);
	if (rc != 0)
	{
		xma_frame_free(xmaFrame);
		*got_frame = 0;
		return 0;
	}

	src_data[0] = xmaFrame->data[0].buffer;
	src_data[1] = xmaFrame->data[1].buffer;
	src_data[2] = xmaFrame->data[2].buffer;
	src_data[3] = xmaFrame->data[0].buffer;
	pict->linesize[3] = pict->linesize[0] = fprops.width * ((fprops.bits_per_pixel + 7) >> 3);

    pict->width = fprops.width;
    pict->height = fprops.height;

	switch (fprops.format)
	{
	case XMA_YUV420_FMT_TYPE:
		pict->linesize[1] = pict->linesize[2] = pict->linesize[0] / 2;
	    pict->format = AV_PIX_FMT_YUV420P;
		break;
	case XMA_YUV422_FMT_TYPE:
		pict->linesize[1] = pict->linesize[2] = pict->linesize[0] / 2;
		pict->format = AV_PIX_FMT_YUV422P;
		break;
	case XMA_YUV444_FMT_TYPE:
		pict->linesize[1] = pict->linesize[2] = pict->linesize[0];
		pict->format = AV_PIX_FMT_YUV444P;
		break;
	default:
		xma_frame_free(xmaFrame);
		*got_frame = 0;
		return 0;
	}

    av_frame_get_buffer (pict, 32);
	av_image_copy (pict->data, pict->linesize, (const uint8_t**)src_data, pict->linesize, pict->format, fprops.width, fprops.height);

	xma_frame_free(xmaFrame);
	*got_frame = 1;
	ctx->m_iOutputFrames++;
	return data_used;
}


static const enum AVPixelFormat vyusynch264_csp_eight[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_NONE
};


static av_cold void vyusynch264_decode_init_csp(AVCodec *codec)
{
        codec->pix_fmts = vyusynch264_csp_eight;
}




static const AVClass class = {
    .class_name = "vyuh264",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_vyusync_h264_decoder = {
    .name             = "VYUH264",
    .long_name        = NULL_IF_CONFIG_SMALL("VYUSync H264"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_H264,
    .init             = vyusynch264_decode_init,
    .init_static_data = vyusynch264_decode_init_csp,
    .decode           = vyusynch264_decode,
    .close            = vyusynch264_decode_close,
    .priv_data_size   = sizeof(vyusynch264Context),
    .priv_class       = &class,
    .capabilities     = CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
};
