/*
 * Copyright (c) 2018 Xilinx 
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

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct AbrScalerContext {
    const AVClass    *class;
    int               nb_outputs;
    int               out_1_width;
    int               out_1_height;
    int               out_2_width;
    int               out_2_height;
    int               out_3_width;
    int               out_3_height;
    int               out_4_width;
    int               out_4_height;
    XmaScalerSession *session; 
} AbrScalerContext;

static int output_config_props(AVFilterLink *outlink);

static av_cold int scale_xma_init(AVFilterContext *ctx)
{
    int i;
    AbrScalerContext *s = ctx->priv;

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
    int              i;
    AbrScalerContext *s = ctx->priv;

    for (i = 0; i < ctx->nb_outputs; i++)
    {
        av_freep(&ctx->output_pads[i].name);
        //printf("****pad name=%s\n",ctx->output_pads[i].name);
    }
    if (s->session)
        xma_scaler_session_destroy(s->session);

}

int output_config_props(AVFilterLink *outlink)
{
    AVFilterContext     *ctx    = outlink->src;
    AbrScalerContext    *s      = ctx->priv;
    const int outlink_idx = FF_OUTLINK_IDX(outlink);
    AVFilterLink        *out    = outlink->src->outputs[outlink_idx];

    switch (outlink_idx)
    {
        case 0:
           out->w = s->out_1_width;
           out->h = s->out_1_height;	   
           outlink->sample_aspect_ratio= (AVRational) {1, 1};
        break;
        case 1:
           out->w = s->out_2_width;
           out->h = s->out_2_height;   
           outlink->sample_aspect_ratio= (AVRational) {1, 1};
        break;
        case 2:
           out->w = s->out_3_width;
           out->h = s->out_3_height;	   
           outlink->sample_aspect_ratio= (AVRational) {1, 1};
        break;
        case 3:
           out->w = s->out_4_width;
           out->h = s->out_4_height;
           outlink->sample_aspect_ratio= (AVRational) {1, 1};
	break;
        default:
            return -1;
    }
    printf("out->w = %d\n", out->w);
    printf("out->h = %d\n", out->h);	

    return 0;
}

static int xma_config_props(AVFilterLink *outlink)
{
    AVFilterContext     *ctx    = outlink->dst;
    AVFilterLink        *inlink = outlink->dst->inputs[0];
    XmaScalerProperties  props;
    AbrScalerContext    *s      = ctx->priv;

    props.hwscaler_type = XMA_POLYPHASE_SCALER_TYPE;
    strcpy(props.hwvendor_string, "Xilinx");
    props.num_outputs = s->nb_outputs;

    props.input.format = XMA_YUV420_FMT_TYPE;
    props.input.width = inlink->w;
    props.input.height = inlink->h;
    props.input.stride = inlink->w; 

    printf("nb_outputs=%d\n", s->nb_outputs);
    printf("out_1_w=%d\n", s->out_1_width);
    printf("out_1_h=%d\n", s->out_1_height);
    printf("out_2_w=%d\n", s->out_2_width);
    printf("out_2_h=%d\n", s->out_2_height);
    printf("out_3_w=%d\n", s->out_3_width);
    printf("out_3_h=%d\n", s->out_3_height);
    printf("out_4_w=%d\n", s->out_4_width);
    printf("out_4_h=%d\n", s->out_4_height);
	
    props.output[0].format = XMA_YUV420_FMT_TYPE;
    props.output[0].bits_per_pixel = 8;
    props.output[0].width = s->out_1_width;
    props.output[0].height = s->out_1_height;
    props.output[0].stride = s->out_1_width;
    props.output[0].filter_idx = 0;
    props.output[0].coeffLoad = 0;
	
    props.output[1].format = XMA_YUV420_FMT_TYPE;
    props.output[1].bits_per_pixel = 8;
    props.output[1].width = s->out_2_width;
    props.output[1].height = s->out_2_height;
    props.output[1].stride = s->out_2_width;
    props.output[1].filter_idx = 1;
    props.output[1].coeffLoad = 0;	

    props.output[2].format = XMA_YUV420_FMT_TYPE;
    props.output[2].bits_per_pixel = 8;
    props.output[2].width = s->out_3_width;
    props.output[2].height = s->out_3_height;
    props.output[2].stride = s->out_3_width;
    props.output[2].filter_idx = 2;
    props.output[2].coeffLoad = 0;	

    props.output[3].format = XMA_YUV420_FMT_TYPE;
    props.output[3].bits_per_pixel = 8;
    props.output[3].width = s->out_4_width;
    props.output[3].height = s->out_4_height;
    props.output[3].stride = s->out_4_width;
    props.output[3].filter_idx = 3;
    props.output[3].coeffLoad = 0;	
	
    //When coeffLoad is set to 2, app expects a FilterCoeff.txt to load coefficients from 
    if ((props.output[0].coeffLoad==2) || (props.output[1].coeffLoad==2) || (props.output[2].coeffLoad==2) || (props.output[3].coeffLoad==2))
    {
        sprintf(props.input.coeffFile, "FilterCoeff.txt");
    }
	
    s->session = xma_scaler_session_create(&props);
    if (!s->session)
    {
        printf("ERROR:session creation failed.\n");
        return -1;
    }

    return 0;
}

static int xma_filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext     *ctx            = link->dst;
    AbrScalerContext    *s              = ctx->priv;
    int8_t               frame_id       = s->session->first_frame;	
	
    AVFrame             *in_frame       = frame;
    XmaFrame            *xframe         = NULL; 
    int                  ret            = AVERROR_EOF;
	
    AVFrame             *a_frame_list[4];
    XmaFrame            *x_frame_list[4];
    XmaFrameData         frame_data;
    XmaFrameProperties   frame_props;
    int                  i;

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
        
    xma_scaler_session_send_frame(s->session, xframe);
    if(frame_id> 1){ // only read output frame after 3rd frame.
       xma_scaler_session_recv_frame_list(s->session, x_frame_list);

       for (i = 0; i < ctx->nb_outputs; i++) 
       {
           av_frame_copy_props(a_frame_list[i], in_frame);
           a_frame_list[i]->width = ctx->outputs[i]->w;
           a_frame_list[i]->height = ctx->outputs[i]->h;
		
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
    }
    else // first 2 frames are not valid data due to pipe-linining
        ret = 0;

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
    { "out_1_width", "set width of output 1", OFFSET(out_1_width), AV_OPT_TYPE_INT, { .i64 = 1280 }, 256, INT_MAX, FLAGS },
    { "out_1_height", "set height of output 1", OFFSET(out_1_height), AV_OPT_TYPE_INT, { .i64 = 720 }, 144, INT_MAX, FLAGS },
    { "out_2_width", "set width of output 2", OFFSET(out_2_width), AV_OPT_TYPE_INT, { .i64 = 852 }, 256, INT_MAX, FLAGS },
    { "out_2_height", "set height of output 2", OFFSET(out_2_height), AV_OPT_TYPE_INT, { .i64 = 480 }, 144, INT_MAX, FLAGS },
    { "out_3_width", "set width of output 3", OFFSET(out_3_width), AV_OPT_TYPE_INT, { .i64 = 640 }, 256, INT_MAX, FLAGS },
    { "out_3_height", "set height of output 3", OFFSET(out_3_height), AV_OPT_TYPE_INT, { .i64 = 360 }, 144, INT_MAX, FLAGS },
    { "out_4_width", "set width of output 4", OFFSET(out_4_width), AV_OPT_TYPE_INT, { .i64 = 256 }, 256, INT_MAX, FLAGS },
    { "out_4_height", "set height of output 4", OFFSET(out_4_height), AV_OPT_TYPE_INT, { .i64 = 144 }, 144, INT_MAX, FLAGS },
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
