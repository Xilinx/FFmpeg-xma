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
    const AVClass     *class;
    XmaDecoderSession *dec_session;
    unsigned int       intraOnly;
    XmaFrameProperties fprops;
    XmaFrame          *xmaFrame;
    int                latencyFrames;
} vyusynch264Context;

#define OFFSET(x) offsetof(vyusynch264Context, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "intraOnly", "Intra-Only", OFFSET(intraOnly), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VD, "intraOnly"},
    { NULL },
};

static av_cold int vyusynch264_decode_close(AVCodecContext *avctx)
{
    vyusynch264Context *ctx = avctx->priv_data;

    xma_frame_free(ctx->xmaFrame);

    xma_dec_session_destroy(ctx->dec_session);

    return 0;
}

static av_cold int vyusynch264_decode_init(AVCodecContext *avctx)
{
	vyusynch264Context *ctx = avctx->priv_data;
    XmaDecoderProperties dec_props;

    printf("vyusynch264_decode_init avctx->decoder= 0x%x\n",(unsigned int)ctx);

    dec_props.hwdecoder_type = XMA_H264_DECODER_TYPE;
    strcpy(dec_props.hwvendor_string, "vyusync");
    dec_props.intraOnly = ctx->intraOnly;
    ctx->fprops.width = avctx->width;
    ctx->fprops.height = avctx->height;
    switch(avctx->pix_fmt)
    {
    case AV_PIX_FMT_YUV420P:
    	ctx->fprops.format = XMA_YUV420_FMT_TYPE;
        ctx->fprops.bits_per_pixel = 8;
    	break;
    case AV_PIX_FMT_YUV422P:
    	ctx->fprops.format = XMA_YUV422_FMT_TYPE;
        ctx->fprops.bits_per_pixel = 8;
    	break;
    case AV_PIX_FMT_YUV444P:
    	ctx->fprops.format = XMA_YUV444_FMT_TYPE;
        ctx->fprops.bits_per_pixel = 8;
    	break;
    case AV_PIX_FMT_YUV420P16LE:
    	ctx->fprops.format = XMA_YUV420_FMT_TYPE;
        ctx->fprops.bits_per_pixel = 16;
    	break;
    case AV_PIX_FMT_YUV422P16LE:
    	ctx->fprops.format = XMA_YUV422_FMT_TYPE;
        ctx->fprops.bits_per_pixel = 16;
    	break;
    case AV_PIX_FMT_YUV444P16LE:
    	ctx->fprops.format = XMA_YUV444_FMT_TYPE;
        ctx->fprops.bits_per_pixel = 16;
    	break;
    }
	ctx->xmaFrame = xma_frame_alloc(&ctx->fprops);
    ctx->latencyFrames = 0;

    printf("creating dec session\n");
    ctx->dec_session = xma_dec_session_create(&dec_props);
    printf("session : 0x%x \n",(unsigned int)ctx->dec_session);

    return 0;
}



static int vyusynch264_decode(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
	vyusynch264Context *ctx            = avctx->priv_data;
    AVFrame            *pict           = data;
	XmaFrameProperties fprops          = {0};
	unsigned char      *src_data[4]    = {NULL};

	int32_t            data_used       = 0;
    int                rc              = 0;

    if(!avpkt->size)
    {
    	// EOF reached
        rc = xma_dec_session_send_data(ctx->dec_session, NULL, &data_used);
    }else{
		XmaDataBuffer* buf = xma_data_from_buffer_clone(avpkt->data, avpkt->size);
		rc = xma_dec_session_send_data(ctx->dec_session, buf, &data_used);
		xma_data_buffer_free(buf);
		ctx->latencyFrames++;
    }
    if (ctx->latencyFrames > 30)
    	usleep(ctx->latencyFrames * 100);
    rc = xma_dec_session_get_properties(ctx->dec_session, &fprops);
    if (rc == -2)
    {
        *got_frame = 0;
        return 0;
    }
	if (rc != 0)
	{
		if ((data_used == 0) || ((data_used < avpkt->size) && (avpkt->size > 0)))
		{
			while (rc != 0)
			{
				int32_t used = 0;
				if (data_used < avpkt->size)
				{
					XmaDataBuffer* buf = xma_data_from_buffer_clone(avpkt->data + data_used, avpkt->size - data_used);
					rc = xma_dec_session_send_data(ctx->dec_session, buf, &used);
					xma_data_buffer_free(buf);
				}else{
			        rc = xma_dec_session_send_data(ctx->dec_session, NULL, &data_used);
				}
				data_used += used;
				rc = xma_dec_session_get_properties(ctx->dec_session, &fprops);
			    if (rc == -2)
			    {
			        *got_frame = 0;
			        return 0;
			    }
			}
		}else{
			*got_frame = 0;
			return 0;
		}
	}
	rc = xma_dec_session_recv_frame(ctx->dec_session, ctx->xmaFrame);
	if (rc != 0)
	{
		*got_frame = 0;
		return 0;
	}

	src_data[0] = ctx->xmaFrame->data[0].buffer;
	src_data[1] = ctx->xmaFrame->data[1].buffer;
	src_data[2] = ctx->xmaFrame->data[2].buffer;
	src_data[3] = ctx->xmaFrame->data[0].buffer;

    pict->linesize[3] = pict->linesize[0] = fprops.width * ((fprops.bits_per_pixel + 7) >> 3);

    pict->width = fprops.width;
    pict->height = fprops.height;
    pict->format = avctx->pix_fmt;

	switch (fprops.format)
	{
	case XMA_YUV420_FMT_TYPE:
		pict->linesize[1] = pict->linesize[2] = pict->linesize[0] / 2;
		break;
	case XMA_YUV422_FMT_TYPE:
		pict->linesize[1] = pict->linesize[2] = pict->linesize[0] / 2;
		break;
	case XMA_YUV444_FMT_TYPE:
		pict->linesize[1] = pict->linesize[2] = pict->linesize[0];
		break;
	default:
		*got_frame = 0;
		return 0;
	}

    av_frame_get_buffer (pict, 32);
	av_image_copy (pict->data, pict->linesize, (const uint8_t**)src_data, pict->linesize, avctx->pix_fmt, fprops.width, fprops.height);

	*got_frame = 1;
	ctx->latencyFrames--;
	return data_used;
}


static const enum AVPixelFormat vyusynch264_csp_eight[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV444P,
	AV_PIX_FMT_YUV420P16LE,
	AV_PIX_FMT_YUV422P16LE,
	AV_PIX_FMT_YUV444P16LE,
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
