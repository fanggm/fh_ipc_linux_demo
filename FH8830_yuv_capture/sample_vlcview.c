#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include "types/type_def.h"
#include "dsp/fh_system_mpi.h"
#include "dsp/fh_vpu_mpi.h"
#include "dsp/fh_venc_mpi.h"
#include "dsp/fh_jpeg_mpi.h"
#include "sample_common_isp.h"
#include "libvlcview.h"
#include "dbi/dbi_over_tcp.h"
#include "config_1080p25.h"

static FH_SINT32 g_exit = 0;
static pthread_t g_thread_isp = 0;
static pthread_t g_thread_stream = 0;
static pthread_t g_thread_dbi = 0;

extern FH_SINT32 g_isp_format;
extern FH_SINT32 g_isp_init_width;
extern FH_SINT32 g_isp_init_height;

void sample_vlcview_exit()
{
	if (g_thread_stream != 0)
		pthread_join(g_thread_stream, NULL);
	if (g_thread_isp != 0)
		pthread_join(g_thread_isp, NULL);

	FH_SYS_Exit();
}

void sample_vlcview_handle_sig(FH_SINT32 signo)
{
	printf("Caught %d, program exit abnormally\n", signo);
	g_exit = 1;
	sample_vlcview_exit();
	exit(EXIT_FAILURE);
}

void *sample_vlcview_get_stream_proc(void *arg)
{
	FH_SINT32 ret, i;
	FH_ENC_STREAM_ELEMENT stream;
	struct vlcview_enc_stream_element stream_element;

	while (!g_exit) {
		do {
			ret = FH_VENC_GetStream(0, &stream);
			if (ret == RETURN_OK) {
				stream_element.frame_type = stream.frame_type;
				stream_element.frame_len = stream.length;
				stream_element.time_stamp = stream.time_stamp;
				stream_element.nalu_count = stream.nalu_cnt;
				for (i = 0; i < stream_element.nalu_count; i++) {
					stream_element.nalu[i].start = stream.nalu[i].start;
					stream_element.nalu[i].len = stream.nalu[i].length;
				}

				vlcview_pes_stream_pack(0, stream_element);
				FH_VENC_ReleaseStream(0);
			}
		} while (ret == RETURN_OK);
		usleep(20000);
	}

	return NULL;
}

FH_BOOL change_vpu_capability()
{
	FILE *proc_file;
	char *cmd = "cap_0_2048_2048";

	proc_file = fopen("/proc/driver/vpu", "w");
	if (proc_file == NULL) {
		perror("fopen");
		return FH_FALSE;
	}
	fwrite(cmd, 1, strlen(cmd), proc_file);
	fclose(proc_file);
	return FH_TRUE;
}

void usage(char *program_name)
{
	fprintf(stderr, "\nUsage:  %s  <VLC IP address>  [port number (optional, default 1234)]\n\n", program_name);
	fprintf(stderr, "Example:\n");
	fprintf(stderr, "    %s 192.168.1.3\n", program_name);
	fprintf(stderr, "    %s 192.168.1.3 2345\n", program_name);
}
//majg add yuv capture test 2018/5/7
#define YUV_CAPTURE
#ifdef  YUV_CAPTURE 

#define CHANNEL_COUNT 2

struct channel_info
{
    FH_UINT32 width;
    FH_UINT32 height;
    FH_UINT8 frame_count;
    FH_UINT8 frame_time;
    FH_UINT32 bps;
};

static struct channel_info g_channel_infos[] = {
    {
        .width = CH0_WIDTH,
        .height = CH0_HEIGHT,
        .frame_count = CH0_FRAME_COUNT,
        .frame_time = CH0_FRAME_TIME,
        .bps = CH0_BIT_RATE
    },
};

void change_8X8_to_16X16(FH_UINT8 *src, FH_UINT8 *dst)
{
    FH_SINT32 h, w, numblock;

    for (numblock = 0; numblock < 4; numblock++)
    {
        for (h = 0; h < 8; h++)
        {
            for (w = 0; w < 8; w++)
            {
                *(dst + numblock / 2 * 8 * 8 * 2 + 2 * 8 * h + numblock % 2 * 8 + w) = *(src + numblock * 8 * 8 + h * 8 + w);
            }
        }
    }
}

void yuv_transform(FH_UINT8 *src_y, FH_UINT8 *src_c, FH_UINT8 *dst, FH_UINT32 w, FH_UINT32 h)
{
    FH_SINT32 w_align, h_align;

    FH_SINT32 i, j, numblock;
    FH_UINT8 orderbuf[8*8*4];
    FH_UINT8 *ybuf, *cbuf, *yuvbuf;
    FH_UINT8 *ubuf, *vbuf;

    w_align = (w + 15)&(~15);
    h_align = (h + 15)&(~15);

    //printf("h %d,h_align %d\n", h, h_align);

    ybuf = src_y;
    cbuf = src_c;
    yuvbuf = dst;

    /* Y */
    for (numblock = 0; numblock < (w_align*h_align/16/16); numblock++)
    {
        change_8X8_to_16X16(ybuf + 8*8*4 *numblock, orderbuf);
        for (i = 0; i < 16; i++)
        {
            for (j = 0; j < 16; j++)
            {
                *(yuvbuf + numblock / (w_align / 16) * w * 16 + numblock %(w_align / 16) * 16 + i * w + j) =*(orderbuf + i * 16 +j);
            }
        }
    }

    /* UV */
    ubuf = yuvbuf + w*h;
    vbuf = yuvbuf + + w*h + w*h/4;
    for (numblock = 0; numblock < (w_align*h_align/16/16); numblock++)
    {
        for (i = 0; i < 8; i++)
        {
            for (j = 0; j < 8; j++)
            {
                *(ubuf + numblock  / (w_align / 16) * w / 2 * 8 + numblock %(w_align / 16) * 8 + i * w / 2 + j) = *(cbuf + numblock * 8 * 8 * 2 + i * 8 + j);
                *(vbuf + numblock  / (w_align / 16) * w / 2 * 8 + numblock %(w_align / 16) * 8 + i * w / 2 + j) = *(cbuf + numblock * 8 * 8 * 2 + 8 * 8 + i * 8 + j);
            }
        }
    }
}

void capture_yuv(void)
{
    FILE *yuv_file;
    FH_VPU_STREAM yuv_data;
    FH_UINT8 *dst;
    FH_SINT32 ret;
    FH_SINT32 chan = 0;
    FH_UINT8 *yuv_org;
    int w, h;
    w = g_channel_infos[chan].width;
    h = g_channel_infos[chan].height;

    int h_align = (h + 15)&(~15);

    dst = malloc( w * h_align * 3 / 2);
    printf("capture ch %d,resolution [%d*%d] yuv\n", chan, w , h);

    if (dst == NULL)
    {
        printf("Error: failed to allocate yuv transform buffer\n");
        return;
    }

    ret = FH_VPSS_GetChnFrame(chan, &yuv_data);
    if (ret == RETURN_OK)
    {
        yuv_org = malloc(w * h_align * 3 / 2);
        if(yuv_org == NULL){
            printf("Error: failed to allocate yuv buffer\n");
            return;
        }
        memcpy(yuv_org, yuv_data.yluma.vbase, w * h );
        memcpy(yuv_org+w * h_align ,  yuv_data.chroma.vbase,  w * h/2);

        yuv_transform(yuv_org, yuv_org+w * h_align, dst, w, h);

        yuv_file = fopen("chn.yuv", "wb");
        fwrite(dst, 1, w * h * 3 / 2, yuv_file);
        fclose(yuv_file);
        free(yuv_org);
        printf("GET CHN %d YUV DATA w:%d h:%d to file chn.yuv\n\n", chan, g_channel_infos[chan].width , g_channel_infos[chan].height);
    }
    else
    {
        printf("Error: FH_VPSS_GetChnFrame failed with %d\n\n", ret);
    }
    free(dst);
}
#endif

int main(int argc, char const *argv[])
{
	FH_VPU_SIZE vi_pic;
	FH_VPU_CHN_CONFIG chn_attr;
	FH_CHR_CONFIG cfg_param;
	FH_SINT32 ret;
	int port = -1;

	if (argc < 2) {
		usage(argv[0]);
		return -1;
	}
	if (argc > 2) {
		port = strtol(argv[2], NULL, 0);
		if (port == 0) {
			printf("Error: invalid port number\n");
			return -2;
		}
	}

	if (port == -1)
		port = 1234;

	signal(SIGINT, sample_vlcview_handle_sig);
	signal(SIGQUIT, sample_vlcview_handle_sig);
	signal(SIGKILL, sample_vlcview_handle_sig);
	signal(SIGTERM, sample_vlcview_handle_sig);
	
	/******************************************
	 step  1: init media platform
	******************************************/
	if (change_vpu_capability() == FH_FALSE)
		goto err_exit;

	ret = FH_SYS_Init();
	if (RETURN_OK != ret) {
		printf("Error: FH_SYS_Init failed with %d\n", ret);
		goto err_exit;
	}

	/******************************************
	 step  2: set vpss resolution
	******************************************/
	vi_pic.vi_size.u32Width = VIDEO_INPUT_WIDTH;
	vi_pic.vi_size.u32Height = VIDEO_INPUT_HEIGHT;
	ret = FH_VPSS_SetViAttr(&vi_pic);
	if (RETURN_OK != ret) {
		printf("Error: FH_VPSS_SetViAttr failed with %d\n", ret);
		goto err_exit;
	}

	/******************************************
	 step  3: start vpss
	******************************************/
	ret = FH_VPSS_Enable(0);
	if (RETURN_OK != ret) {
		printf("Error: FH_VPSS_Enable failed with %d\n", ret);
		goto err_exit;
	}

	/******************************************
	 step  4: configure vpss channel 0
	******************************************/
	chn_attr.vpu_chn_size.u32Width = CH0_WIDTH;
	chn_attr.vpu_chn_size.u32Height = CH0_HEIGHT;
	ret = FH_VPSS_SetChnAttr(0, &chn_attr);
	if (RETURN_OK != ret) {
		printf("Error: FH_VPSS_SetChnAttr failed with %d\n", ret);
		goto err_exit;
	}

	/******************************************
	 step  5: open vpss channel 0
	******************************************/
	ret = FH_VPSS_OpenChn(0);
	if (RETURN_OK != ret) {
		printf("Error: FH_VPSS_OpenChn failed with %d\n", ret);
		goto err_exit;
	}

	/******************************************
	 step  6: create venc channel 0
	******************************************/
	cfg_param.chn_attr.size.u32Width = CH0_WIDTH;
	cfg_param.chn_attr.size.u32Height = CH0_HEIGHT;
	cfg_param.rc_config.bitrate = CH0_BIT_RATE;
	cfg_param.rc_config.FrameRate.frame_count = CH0_FRAME_COUNT;
	cfg_param.rc_config.FrameRate.frame_time  = CH0_FRAME_TIME;
	cfg_param.chn_attr.profile = FH_PRO_MAIN;
	cfg_param.chn_attr.i_frame_intterval = 30;
	cfg_param.init_qp = 28;
	cfg_param.rc_config.ImaxQP = 40;
	cfg_param.rc_config.IminQP = 20;
	cfg_param.rc_config.PmaxQP = 40;
	cfg_param.rc_config.PminQP = 20;
	cfg_param.rc_config.RClevel = FH_RC_LOW;
	cfg_param.rc_config.RCmode = FH_RC_FIXEQP;
	cfg_param.rc_config.max_delay = 8;
	ret = FH_VENC_CreateChn(0, &cfg_param);
	if (RETURN_OK != ret) {
		printf("Error: FH_VENC_CreateChn failed with %d\n", ret);
		goto err_exit;
	}

	/******************************************
	 step  7: start venc channel 0
	******************************************/
	ret = FH_VENC_StartRecvPic(0);
	if (RETURN_OK != ret) {
		printf("Error: FH_VENC_StartRecvPic failed with %d\n", ret);
		goto err_exit;
	}

	/******************************************
	 step  8: bind vpss channel 0 with venc channel 0
	******************************************/
	ret = FH_SYS_BindVpu2Enc(0, 0);
	if (RETURN_OK != ret) {
		printf("Error: FH_SYS_BindVpu2Enc failed with %d\n", ret);
		goto err_exit;
	}

	/******************************************
	 step  9: init ISP, and then start ISP process thread
	******************************************/
	g_isp_format = ISP_FORMAT;
	g_isp_init_width = ISP_INIT_WIDTH;
	g_isp_init_height = ISP_INIT_HEIGHT;
	if (sample_isp_init() != 0) {
		goto err_exit;
	}
	pthread_create(&g_thread_isp, NULL, sample_isp_proc, &g_exit);

	/******************************************
	 step  10: start debug interface thread
	******************************************/
	struct dbi_tcp_config tcp_conf;
	tcp_conf.cancel = &g_exit;
	tcp_conf.port = 8888;
	pthread_create(&g_thread_dbi, NULL, tcp_dbi_thread, &tcp_conf);

	/******************************************
	 step  11: int vlcview lib
	******************************************/
	ret = vlcview_pes_init();
	if (0 != ret) {
		printf("Error: vlcview_pes_init failed with %d\n", ret);
		goto err_exit;
	}

	/******************************************
	 step  12: get stream, pack as PES stream and then send to vlc
	******************************************/
	pthread_create(&g_thread_stream, NULL, sample_vlcview_get_stream_proc, NULL);
	vlcview_pes_send_to_vlc(0, (char *)argv[1], port);

	//test capture yuv 
    //majg add 2018/5/7
    #ifdef YUV_CAPTURE
    capture_yuv(); 
    #endif  
	/******************************************
	 step  13: handle keyboard event
	******************************************/
	printf("\nPress Enter key to exit program ...\n");
	getchar();
	g_exit = 1;

	/******************************************
	 step  14: exit process
	******************************************/
err_exit:
	sample_vlcview_exit();
	vlcview_pes_uninit();

	return 0;
}
