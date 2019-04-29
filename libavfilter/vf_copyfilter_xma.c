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
 * Xilinx XMA Copy Kernel Filter
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

#define OFFSET(x) offsetof(XlnxCopyFilterContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM

static int copy_filter_frame(AVFilterLink *link, AVFrame *frame);

typedef struct XlnxCopyFilterContext {
    const AVClass     *class;
    XmaFilterSession  *session;
    int frame_in;
    int frame_out;
    int width;
    int height;
    int *copyOutLink;
    int send_status;
    int flush;
} XlnxCopyFilterContext;

static av_cold int copyfilter_xma_init(AVFilterContext *ctx)
{
    int i = 0;
    XlnxCopyFilterContext *s  = ctx->priv;
    s->frame_in = 0;
    s->frame_out = 0;
    s->flush = 0;

    return 0;
}

static av_cold void copyfilter_xma_uninit(AVFilterContext *ctx)
{
    int rc = 0;
    XlnxCopyFilterContext *s = ctx->priv;
    
    if (s->session)
    {
        rc = xma_filter_session_destroy(s->session);
        if (rc != 0)
        {
           av_log(NULL, AV_LOG_ERROR, "ERROR: Failed to destroy copy filter session\n");
           return rc;       
        }
    }

    av_log(NULL, AV_LOG_INFO, "INFO: Closed Xilinx Copy Filter session\n");
    return 0;
}

static int xma_config_props(AVFilterLink *outlink)
{ 
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    XlnxCopyFilterContext *s = ctx->priv;

    // Setup copy filter input port properties
    XmaFilterPortProperties in_props;
    in_props.format = XMA_YUV420_FMT_TYPE; 
    in_props.bits_per_pixel = 8;
    in_props.width = inlink->w; 
    in_props.height = inlink->h;
    in_props.stride = inlink->w; 

    // Setup copy filter output port properties
    XmaFilterPortProperties out_props;
    out_props.format = XMA_YUV420_FMT_TYPE; 
    out_props.bits_per_pixel = 8;
    out_props.width = inlink->w; 
    out_props.height = inlink->h;
    out_props.stride = inlink->w; 

    s->width  = inlink->w;
    s->height = inlink->h;
    
    // Setup copy filter properties
    XmaFilterProperties filter_props;    
    filter_props.hwfilter_type = XMA_2D_FILTER_TYPE;
    strcpy(filter_props.hwvendor_string, "Xilinx");
    filter_props.input = in_props;
    filter_props.output = out_props;

    // Create copy filter session based on requested properties
    av_log(NULL, AV_LOG_INFO, "INFO: Creating XMA copy filter session\n");
    if (NULL == (s->session = xma_filter_session_create(&filter_props)))
    {
       av_log(NULL, AV_LOG_ERROR, "ERROR: Failed to create filter session\n");
       return -1;
    }

   return 0;
}

// Start video frame processing
static int copy_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    XlnxCopyFilterContext *s = ctx->priv;
    AVFrame *av_frame_in, *av_frame_out;
    XmaFrame *xframe_in, *xframe_out;
    XmaFrameData in_f_data, out_f_data;
    XmaFrameProperties in_f_props, out_f_props;
    
    AVFilterLink *outlink = ctx->outputs[0];
    
    int ret = 0;
    int i = 0;
    
    xframe_in = NULL; 
    xframe_out = NULL;
    av_frame_in = frame;

    s->copyOutLink  = (int*)inlink;

    // Clone input frame from AVFrame to XmaFrame
    in_f_props.format = XMA_YUV420_FMT_TYPE;
    in_f_props.width = av_frame_in->width;
    in_f_props.height = av_frame_in->height;
    in_f_props.bits_per_pixel = 8;
    
    // Set input stride
    av_frame_in->linesize[0] = av_frame_in->width;
    av_frame_in->linesize[1] = av_frame_in->width >> 1;
    av_frame_in->linesize[2] = av_frame_in->width >> 1;
    
    in_f_data.data[0] = av_frame_in->data[0];
    in_f_data.data[1] = av_frame_in->data[1];
    in_f_data.data[2] = av_frame_in->data[2];
    
    xframe_in = xma_frame_from_buffers_clone(&in_f_props, 
                                             &in_f_data);
    av_frame_out = ff_get_video_buffer(outlink, outlink->w, outlink->h);

    out_f_props.format = XMA_YUV420_FMT_TYPE;
    out_f_props.width = outlink->w;
    out_f_props.height = outlink->h;
    out_f_props.bits_per_pixel = 8;                                
    
    out_f_data.data[0] = av_frame_out->data[0];
    out_f_data.data[1] = av_frame_out->data[1];
    out_f_data.data[2] = av_frame_out->data[2];
    
    xframe_out = xma_frame_from_buffers_clone(&out_f_props, &out_f_data);   

    // Send xma input frame to copy filter
    s->send_status = xma_filter_session_send_frame(s->session, xframe_in);    
    if(s->send_status == XMA_SUCCESS)
    { 
        s->frame_in++;
        
        // Receive frame from copy filter
        xma_filter_session_recv_frame(s->session, xframe_out);
                                    
        av_frame_copy_props(av_frame_out, av_frame_in);
        av_frame_out->width = outlink->w;
        av_frame_out->height = outlink->h;
    
        //set the stride
        av_frame_out->linesize[0] = av_frame_out->width;
        av_frame_out->linesize[1] = (av_frame_out->width) >> 1;
        av_frame_out->linesize[2] = (av_frame_out->width) >> 1;  
        s->frame_out++;    
    }
    
    av_frame_free(&frame);
    return (ff_filter_frame(outlink, av_frame_out));

}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_formats[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *pix_fmts;
    int ret;
    
    pix_fmts = ff_make_format_list(pixel_formats);
    
    if ((ret = ff_set_common_formats(ctx, pix_fmts)) < 0)
        return ret;
    
    return 0;
}

static const AVFilterPad avfilter_vf_copy_xma_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = copy_filter_frame,
    },
    { NULL }
};

static const AVOption copyfilter_xma_options[] = {
    { NULL }
};

AVFILTER_DEFINE_CLASS(copyfilter_xma);

static const AVFilterPad avfilter_vf_copy_xma_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = xma_config_props,
    },
    { NULL }
};

AVFilter ff_vf_copyfilter_xma = {
    .name          = "copyfilter_xma",
    .description   = NULL_IF_CONFIG_SMALL("Xilinx Copy Filter for XMA."),
    .priv_size     = sizeof(XlnxCopyFilterContext),
    .priv_class    = &copyfilter_xma_class,
    .init          = copyfilter_xma_init,
    .uninit        = copyfilter_xma_uninit,
    .query_formats = query_formats,
    .inputs        = avfilter_vf_copy_xma_inputs,
    .outputs       = avfilter_vf_copy_xma_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
