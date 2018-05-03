#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include "types/type_def.h"
#include "dsp/fh_system_mpi.h"
#include "dsp/fh_vpu_mpi.h"
#include "sample_common_isp.h"
#include "libvlcview.h"
#include "sample_opts.h"
#include "dsp/fh_venc_mpi.h"
static FH_SINT32 g_exit = 0;
static pthread_t g_thread_isp = 0;
static pthread_t g_thread_stream = 0;

#define CHANNEL_COUNT       2

void sample_mjpeg_exit(void)
{
    if (g_thread_stream != 0)
        pthread_join(g_thread_stream, NULL);
    if (g_thread_isp != 0)
        pthread_join(g_thread_isp, NULL);

    vlcview_mjpeg_stop_server();

    FH_SYS_Exit();
}

void sample_mjpeg_handle_sig(FH_SINT32 signo)
{
    printf("Caught %d, program exit abnormally\n", signo);
    g_exit = 1;
    sample_mjpeg_exit();
    exit(EXIT_FAILURE);
}

void *sample_mjpeg_get_stream_proc(void *arg)
{
    FH_SINT32 ret, i;
    FH_VENC_STREAM stream;
    unsigned int chan,len;
    unsigned char *start;
    while (!g_exit)
    {
        ret = FH_VENC_GetStream_Block(FH_STREAM_MJPEG, &stream);
        if (ret == RETURN_OK)
        {
            chan  = stream.mjpeg_stream.chan;
            start = stream.mjpeg_stream.start;
            len   = stream.mjpeg_stream.length;
            vlcview_mjpeg_stream_pack(start, len);
            FH_VENC_ReleaseStream(chan);
        }
    }
    return NULL;
}

void usage(char *program_name)
{
    fprintf(stderr, "\nUsage:  %s  [port number (optional, default 8080)]\n\n", program_name);
    fprintf(stderr, "Example:\n");
    fprintf(stderr, "    %s\n", program_name);
    fprintf(stderr, "    %s 8081\n", program_name);
}

int main(int argc, char const *argv[])
{
    FH_VPU_SIZE vi_pic;
    FH_VPU_CHN_CONFIG chn_attr;
    FH_SINT32 ret;
    int port = -1;
    FH_SINT32 i ;
    FH_VENC_CHN_CAP cfg_vencmem;
    FH_VENC_CHN_CONFIG cfg_param;
    if (argc > 1)
    {
        port = strtol(argv[1], NULL, 0);
        if (port == 0)
        {
            printf("Error: invalid port number\n");
            return -2;
        }
    }
    if (port == -1)
        port = 8080;

    signal(SIGINT, sample_mjpeg_handle_sig);
    signal(SIGQUIT, sample_mjpeg_handle_sig);
    signal(SIGKILL, sample_mjpeg_handle_sig);
    signal(SIGTERM, sample_mjpeg_handle_sig);

    /******************************************
     step  1: init media platform
    ******************************************/
    ret = FH_SYS_Init();
    if (ret != RETURN_OK)
    {
        printf("Error: FH_SYS_Init failed with %d\n", ret);
        goto err_exit;
    }

    /******************************************
     step  2: set vpss resolution
    ******************************************/
    vi_pic.vi_size.u32Width = VIDEO_INPUT_WIDTH;
    vi_pic.vi_size.u32Height = VIDEO_INPUT_HEIGHT;
    ret = FH_VPSS_SetViAttr(&vi_pic);
    if (ret != RETURN_OK)
    {
        printf("Error: FH_VPSS_SetViAttr failed with %d\n", ret);
        goto err_exit;
    }

    /******************************************
     step  3: start vpss
    ******************************************/
    ret = FH_VPSS_Enable(0);
    if (ret != RETURN_OK)
    {
        printf("Error: FH_VPSS_Enable failed with %d\n", ret);
        goto err_exit;
    }

    /******************************************
     step  4: configure vpss channel 0
    ******************************************/
 //   for (i = 0; i < CHANNEL_COUNT; i++)
 //   {
    chn_attr.vpu_chn_size.u32Width = CH1_WIDTH;
    chn_attr.vpu_chn_size.u32Height = CH1_HEIGHT;
    ret = FH_VPSS_SetChnAttr(1, &chn_attr);
    if (ret != RETURN_OK)
    {
        printf("Error: FH_VPSS_SetChnAttr failed with %d\n", ret);
        goto err_exit;
    }

    /******************************************
     step  5: open vpss channel 0
    ******************************************/
    ret = FH_VPSS_OpenChn(1);
    if (ret != RETURN_OK)
    {
        printf("Error: FH_VPSS_OpenChn failed with %d\n", ret);
        goto err_exit;
    }

    /******************************************
     step  6: init ISP, and then start ISP process thread
    ******************************************/
    if (sample_isp_init() != 0)
    {
        goto err_exit;
    }
    pthread_create(&g_thread_isp, NULL, sample_isp_proc, &g_exit);

    /*****************************************
     * step 7: venc set chan attr.
     *****************************************/
    /****************************************
     * 7.1 create venc channel 0 with MJPEG type
     ****************************************/
    cfg_vencmem.support_type       = FH_MJPEG;
    cfg_vencmem.max_size.u32Width  = CH1_WIDTH;
    cfg_vencmem.max_size.u32Height = CH1_HEIGHT;
    ret = FH_VENC_CreateChn(1, &cfg_vencmem);
    if (ret != RETURN_OK)
    {
        printf("Error: FH_VENC_CreateChn failed with %d\n", ret);
        goto err_exit;
    }
    /****************************************
    *  7.2 venc set chnattr 0
    *****************************************/
    cfg_param.chn_attr.enc_type = FH_MJPEG;
    cfg_param.chn_attr.mjpeg_attr.pic_size.u32Width     = CH1_WIDTH;
    cfg_param.chn_attr.mjpeg_attr.pic_size.u32Height    = CH1_HEIGHT;
    cfg_param.chn_attr.mjpeg_attr.rotate                = 0;
    cfg_param.chn_attr.mjpeg_attr.encode_speed          = 4;/* 0-9 */

    cfg_param.rc_attr.rc_type                           = FH_RC_MJPEG_FIXQP;
    cfg_param.rc_attr.mjpeg_fixqp.qp                    = 50;
    cfg_param.rc_attr.mjpeg_fixqp.FrameRate.frame_count = CH1_FRAME_COUNT;
    cfg_param.rc_attr.mjpeg_fixqp.FrameRate.frame_time  = CH1_FRAME_TIME;
    ret = FH_VENC_SetChnAttr(1, &cfg_param);
    if (ret != RETURN_OK)
    {
        printf("Error: FH_VENC_SetChnAttr failed with %d\n", ret);
        goto err_exit;
    }
    /******************************************
     step  8: start venc channel 0
    ******************************************/
    ret = FH_VENC_StartRecvPic(1);
    if (ret != RETURN_OK)
    {
        printf("Error: FH_VENC_StartRecvPic failed with %d\n", ret);
        goto err_exit;
    }

    /******************************************
     step  9: bind vpss channel 0 with venc channel 0
    ******************************************/
    ret = FH_SYS_BindVpu2Enc(1, 1);
    if (ret != RETURN_OK)
    {
        printf("Error: FH_SYS_BindVpu2Enc failed with %d\n", ret);
        goto err_exit;
    }
//}
    /******************************************
      start mjpeg server thread and mjpeg stream thread,
      the server waits the clients to connect,
      client uses http://<IP>:8080/?action=stream
      to view MJPEG stream
    ******************************************/
    vlcview_mjpeg_start_server(port);
    pthread_create(&g_thread_stream, NULL, sample_mjpeg_get_stream_proc, NULL);
    

    /******************************************
      handle keyboard event
    ******************************************/
    printf("\nPress Enter key to exit program ...\n");
    getchar();
    g_exit = 1;

err_exit:
    sample_mjpeg_exit();

    return 0;
}
