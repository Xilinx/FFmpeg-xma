/*
 * Copyright (c) 2018 Xilinx 
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
 * video ABR Scaler with Xilinx Media Accelerator
 */

#include <stdio.h>
#include <xma.h>
#include <xmaplugin.h>

#include "libavutil/attributes.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

//MAX_OUTS is dependant on corresponding XCLBIN used and can have 4 or 8.
#define MAX_OUTS 4

static int xma_filter_frame(AVFilterLink *link, AVFrame *frame);

typedef struct AbrScalerContext {
    const AVClass    *class;
    int               nb_outputs;
    int               out_width[MAX_OUTS];
    int               out_height[MAX_OUTS];

    int              *copyOutLink;
    int               flush;
    int               send_status;	
    int               frames_out;

    XmaScalerSession *session; 
} AbrScalerContext;

static int output_config_props(AVFilterLink *outlink);

static av_cold int scale_xma_init(AVFilterContext *ctx)
{
    int               i = 0;
    AbrScalerContext *s = ctx->priv;
    s->frames_out       = 0;

    for (i = 0; i < s->nb_outputs; i++) {
        char name[32];
        AVFilterPad pad = { 0 };

        snprintf(name, sizeof(name), "output%d", i);
        pad.type = ctx->filter->inputs[0].type;
        pad.name = av_strdup(name);
        if (!pad.name)
            return AVERROR(ENOMEM);	
        pad.config_props = output_config_props;
        ff_insert_outpad(ctx, i, &pad);
    }

    return 0;
}

static av_cold void scale_xma_uninit(AVFilterContext *ctx)
{
    int               i = 0;
    AbrScalerContext *s = ctx->priv;

    for (i = 0; i < ctx->nb_outputs; i++)    
        av_freep(&ctx->output_pads[i].name);
    
    if (s->session)
        xma_scaler_session_destroy(s->session);

}

int output_config_props(AVFilterLink *outlink)
{
    AVFilterContext     *ctx         = outlink->src;
    AbrScalerContext    *s           = ctx->priv;
    const int            outlink_idx = FF_OUTLINK_IDX(outlink);
    AVFilterLink        *out         = outlink->src->outputs[outlink_idx];

    out->w = s->out_width[outlink_idx];
    out->h = s->out_height[outlink_idx];	   
    outlink->sample_aspect_ratio= (AVRational) {1, 1};

    return 0;
}

static int xma_config_props(AVFilterLink *outlink)
{
    AVFilterContext     *ctx    = outlink->dst;
    AVFilterLink        *inlink = outlink->dst->inputs[0];
    XmaScalerProperties  props;
    AbrScalerContext    *s      = ctx->priv;
    int n                       = 0;

    props.hwscaler_type = XMA_POLYPHASE_SCALER_TYPE;
    strcpy(props.hwvendor_string, "Xilinx");
    props.num_outputs   = s->nb_outputs;


    props.input.format = XMA_YUV420_FMT_TYPE;
    props.input.width  = inlink->w;
    props.input.height = inlink->h;
    props.input.stride = inlink->w; 


    props.output[0].format = XMA_YUV420_FMT_TYPE;
    props.output[0].bits_per_pixel = 8;
    props.output[0].width  = s->out_width[0];
    props.output[0].height = s->out_height[0];
    props.output[0].stride = s->out_width[0];
    props.output[0].coeffLoad = 0;
	
	
    props.output[1].format = XMA_YUV420_FMT_TYPE;
    props.output[1].bits_per_pixel = 8;
    props.output[1].width  = s->out_width[1];
    props.output[1].height = s->out_height[1];
    props.output[1].stride = s->out_width[1];
    props.output[1].coeffLoad = 0;
    
    props.output[2].format = XMA_YUV420_FMT_TYPE;
    props.output[2].bits_per_pixel = 8;
    props.output[2].width  = s->out_width[2];
    props.output[2].height = s->out_height[2];
    props.output[2].stride = s->out_width[2];
    props.output[2].coeffLoad = 0;

    props.output[3].format = XMA_YUV420_FMT_TYPE;
    props.output[3].bits_per_pixel = 8;
    props.output[3].width  = s->out_width[3];
    props.output[3].height = s->out_height[3];
    props.output[3].stride = s->out_width[3];
    props.output[3].coeffLoad = 0;
   
#if MAX_OUTS ==8
    props.output[4].format = XMA_YUV420_FMT_TYPE;
    props.output[4].bits_per_pixel = 8;
    props.output[4].width  = s->out_width[4];
    props.output[4].height = s->out_height[4];
    props.output[4].stride = s->out_width[4];
    props.output[4].coeffLoad = 0;

    props.output[5].format = XMA_YUV420_FMT_TYPE;
    props.output[5].bits_per_pixel = 8;
    props.output[5].width  = s->out_width[5];
    props.output[5].height = s->out_height[5];
    props.output[5].stride = s->out_width[5];
    props.output[5].coeffLoad = 0;

    props.output[6].format = XMA_YUV420_FMT_TYPE;
    props.output[6].bits_per_pixel = 8;
    props.output[6].width  = s->out_width[6];
    props.output[6].height = s->out_height[6];
    props.output[6].stride = s->out_width[6];
    props.output[6].coeffLoad = 0;

    props.output[7].format = XMA_YUV420_FMT_TYPE;
    props.output[7].bits_per_pixel = 8;
    props.output[7].width  = s->out_width[7];
    props.output[7].height = s->out_height[7];
    props.output[7].stride = s->out_width[7];
    props.output[7].coeffLoad = 0;	
#endif

 //When coeffLoad is set to 2, app expects a FilterCoeff.txt to load coefficients from 
    for (n=0; n<MAX_OUTS;n++)
    {
      if (props.output[n].coeffLoad==2) 
      {
        sprintf(props.input.coeffFile, "FilterCoeff.txt");
        break;
      }        
    }
	
    s->session = xma_scaler_session_create(&props);
    if (!s->session)
    {
        printf("ERROR:session creation failed.\n");
        return -1;
    }

    return 0;
}

void xma_abrscaler_filter_flush(AVFilterLink *link)
{
    AVFilterContext     *ctx            = link->dst; 	
    AbrScalerContext    *s              = ctx->priv;
    int                  ret            = s->send_status;
    int                  rtt            = -1;
    int                 *outLink        = (int *)link;
    AVFrame             *nframe         = av_frame_alloc();

    nframe->format = XMA_YUV420_FMT_TYPE;
    nframe->width  = 1920;
    nframe->height = 1080;

    rtt =  av_frame_get_buffer(nframe, 32);   
    if (rtt<0) printf("Issue getting av_frame_get_buffer()\n");

    if (outLink == s->copyOutLink)  
    {	   
       s->flush        = 1;
       nframe->data[0] = NULL;
       nframe->data[1] = NULL;
       nframe->data[2] = NULL;
   
       while (ret !=XMA_EOS)
       {	  
         xma_filter_frame(link, nframe);
         ret = s->send_status;
       }
    }

    av_freep(&nframe->data[0]);
    av_freep(&nframe->data[1]);
    av_freep(&nframe->data[2]);
    av_frame_free(&nframe);
}

static int xma_filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext     *ctx           = link->dst;
    AbrScalerContext    *s             = ctx->priv;
    AVFrame             *in_frame      = frame;
    XmaFrame            *xframe        = NULL; 
    int                  ret           = 0;
    int                  i             = 0;

    AVFrame             *a_frame_list[MAX_OUTS];
    XmaFrame            *x_frame_list[MAX_OUTS];
    XmaFrameData         frame_data;
    XmaFrameProperties   frame_props;
	
    s->copyOutLink	= (int*)link;	
	
    // Clone input frame from an AVFrame to an XmaFrame
    frame_props.format = XMA_YUV420_FMT_TYPE;
    frame_props.width = in_frame->width;
    frame_props.height = in_frame->height;
    frame_props.bits_per_pixel = 8;
	
    //set the stride	
    in_frame->linesize[0] = in_frame->width;
    in_frame->linesize[1] = in_frame->width>>1;
    in_frame->linesize[2] = in_frame->width>>1;

    frame_data.data[0] = in_frame->data[0];
    frame_data.data[1] = in_frame->data[1];
    frame_data.data[2] = in_frame->data[2];

    xframe = xma_frame_from_buffers_clone(&frame_props, 
                                          &frame_data);

    xframe->pts = in_frame->pts;
    // Create output frames
    for (i = 0; i < ctx->nb_outputs; i++) 
    {
        XmaFrameProperties fprops;
        XmaFrameData       fdata;

        a_frame_list[i] = ff_get_video_buffer(ctx->outputs[i],
                                              ctx->outputs[i]->w,
                                              ctx->outputs[i]->h); 

        fprops.format = XMA_YUV420_FMT_TYPE;
        fprops.width = ctx->outputs[i]->w;
        fprops.height = ctx->outputs[i]->h;
        fprops.bits_per_pixel = 8;

        fdata.data[0] = a_frame_list[i]->data[0];
        fdata.data[1] = a_frame_list[i]->data[1];
        fdata.data[2] = a_frame_list[i]->data[2];
        
        x_frame_list[i] = xma_frame_from_buffers_clone(&fprops, &fdata);
    } 

    s->send_status = xma_scaler_session_send_frame(s->session, xframe);
	
    if((s->send_status== XMA_SUCCESS) || (s->send_status == XMA_FLUSH_AGAIN)) // only receive output frame after XMA_SUCESS or XMA_FLUSH_AGAIN.
    { 
        xma_scaler_session_recv_frame_list(s->session, x_frame_list);
									
        for (i = 0; i < ctx->nb_outputs; i++) 
        {
           //av_frame_copy_props(a_frame_list[i], in_frame);
           av_frame_copy_props(a_frame_list[i], in_frame);
           a_frame_list[i]->width = ctx->outputs[i]->w;
           a_frame_list[i]->height = ctx->outputs[i]->h;
           a_frame_list[i]->pts = x_frame_list[i]->pts;
		
           //set the stride
           a_frame_list[i]->linesize[0] = a_frame_list[i]->width;
           a_frame_list[i]->linesize[1] = (a_frame_list[i]->width)>>1;
           a_frame_list[i]->linesize[2] = (a_frame_list[i]->width)>>1;
           ret = ff_filter_frame(ctx->outputs[i], a_frame_list[i]);
           if (ret < 0)
           {
               printf("xma_filter_frame: ff_filter_frame failed: ret=%d\n", ret);
               break;
           }
        }
        s->frames_out++;
		
    }
    else // skip ff_filter_frame until output is available to support pipe-lining
        ret = 0;
		
    if (s->flush == 0) 
      av_frame_free(&frame);
	
    return ret;
}

static int xma_query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_formats[] = {
        AV_PIX_FMT_YUV420P, 
        AV_PIX_FMT_NONE,
    };

    AVFilterFormats *pix_fmts;
    int              ret;

    pix_fmts = ff_make_format_list(pixel_formats);

    if ((ret = ff_set_common_formats(ctx, pix_fmts)) < 0)
        return ret;

    return 0;
}

#define OFFSET(x) offsetof(AbrScalerContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM)
static const AVOption options[] = {
    { "outputs", "set number of outputs", OFFSET(nb_outputs), AV_OPT_TYPE_INT, { .i64 = 4 }, 1, INT_MAX, FLAGS },
    { "out_1_width", "set width of output 1", OFFSET(out_width[0]), AV_OPT_TYPE_INT, { .i64 = 1280 }, 424, INT_MAX, FLAGS },
    { "out_1_height", "set height of output 1", OFFSET(out_height[0]), AV_OPT_TYPE_INT, { .i64 = 720 }, 240, INT_MAX, FLAGS },
    { "out_2_width", "set width of output 2", OFFSET(out_width[1]), AV_OPT_TYPE_INT, { .i64 = 852 }, 424, INT_MAX, FLAGS },
    { "out_2_height", "set height of output 2", OFFSET(out_height[1]), AV_OPT_TYPE_INT, { .i64 = 480 }, 240, INT_MAX, FLAGS },
    { "out_3_width", "set width of output 3", OFFSET(out_width[2]), AV_OPT_TYPE_INT, { .i64 = 640 }, 424, INT_MAX, FLAGS },
    { "out_3_height", "set height of output 3", OFFSET(out_height[2]), AV_OPT_TYPE_INT, { .i64 = 360 }, 240, INT_MAX, FLAGS },
    { "out_4_width", "set width of output 4", OFFSET(out_width[3]), AV_OPT_TYPE_INT, { .i64 = 424 }, 424, INT_MAX, FLAGS },
    { "out_4_height", "set height of output 4", OFFSET(out_height[3]), AV_OPT_TYPE_INT, { .i64 = 240 }, 240, INT_MAX, FLAGS },
#if MAX_OUTS ==8
    { "out_5_width", "set width of output 5", OFFSET(out_width[4]), AV_OPT_TYPE_INT, { .i64 = 424 }, 424, INT_MAX, FLAGS },
    { "out_5_height", "set height of output 5", OFFSET(out_height[4]), AV_OPT_TYPE_INT, { .i64 = 240 }, 240, INT_MAX, FLAGS },
    { "out_6_width", "set width of output 6", OFFSET(out_width[5]), AV_OPT_TYPE_INT, { .i64 = 424 }, 424, INT_MAX, FLAGS },
    { "out_6_height", "set height of output 6", OFFSET(out_height[5]), AV_OPT_TYPE_INT, { .i64 = 240 }, 240, INT_MAX, FLAGS },
    { "out_7_width", "set width of output 7", OFFSET(out_width[6]), AV_OPT_TYPE_INT, { .i64 = 424 }, 424, INT_MAX, FLAGS },
    { "out_7_height", "set height of output 7", OFFSET(out_height[6]), AV_OPT_TYPE_INT, { .i64 = 240 }, 240, INT_MAX, FLAGS },
    { "out_8_width", "set width of output 8", OFFSET(out_width[7]), AV_OPT_TYPE_INT, { .i64 = 424 }, 424, INT_MAX, FLAGS },
    { "out_8_height", "set height of output 8", OFFSET(out_height[7]), AV_OPT_TYPE_INT, { .i64 = 240 }, 240, INT_MAX, FLAGS },	
#endif
    { NULL }
};

#define scale_xma_options options
AVFILTER_DEFINE_CLASS(scale_xma);

static const AVFilterPad avfilter_vf_scale_xma_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = xma_filter_frame,
        .config_props = xma_config_props,
    },
    { NULL }
};

AVFilter ff_vf_scale_xma = {
    .name          = "scale_xma",
    .description   = NULL_IF_CONFIG_SMALL("Xilinx ABR Scaler for XMA."),
    .priv_size     = sizeof(AbrScalerContext),
    .priv_class    = &scale_xma_class,
    .query_formats = xma_query_formats,
    .init          = scale_xma_init,
    .uninit        = scale_xma_uninit,
    .inputs        = avfilter_vf_scale_xma_inputs,
    .outputs       = NULL,
    .flags         = AVFILTER_FLAG_DYNAMIC_OUTPUTS,
};
