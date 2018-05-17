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
#include "dsp/fh_bgm_mpi.h"
#include "sample_common_isp.h"
#include "libvlcview.h"
#include "dbi/dbi_over_tcp.h"
#include "sample_opts.h"

#define ALIGNTO(addr, edge)  ((addr + edge - 1) & ~(edge - 1))

static FH_SINT32 g_exit = 0;
static pthread_t g_thread_isp = 0;
static pthread_t g_thread_stream = 0;
static pthread_t g_thread_dbi = 0;

void print_keyboard_help()
{
	printf("Keyboard function:\n");
	printf("   q    quit\n");
	printf("   c    close smart\n");
	printf("   o    enable smart\n");
}

void enable_smart(int enable)
{
    int ret;
    printf("enable smart %d\n", enable);
    FH_SMART_CHR_CONFIG smart_attr;

    if((ret = FH_SMART_ENC_GetChnAttr(0, &smart_attr)) != 0){
        printf("get smart enc attr error,%d\n", ret);
        return;
    }
    ret = FH_VPSS_CloseChn(0);
    if (ret != RETURN_OK)
    {
        printf("Error: FH_VPSS_CloseChn failed with %d\n", ret);
        return;
    }

    if((ret = FH_VENC_StopRecvPic(0))!= 0){
        printf("FH_VENC_StopRecvPic error,%d\n", ret);
        return;
    }
    ret = FH_VPSS_OpenChn(0);
    if (ret != RETURN_OK)
    {
        printf("Error: FH_VPSS_OpenChn failed with %d\n", ret);
        return;
    }
    
    smart_attr.smart_attr.smart_en = enable;
    smart_attr.smart_attr.texture_en = enable;
    smart_attr.smart_attr.backgroudmodel_en = enable;
    smart_attr.smart_attr.mbconsist_en = enable;

    if(!enable)
    {
    	printf("close bgm ...\n");
    	FH_BGM_Disable();
	}
    
    if((ret = FH_SMART_ENC_SetChnAttr(0, &smart_attr)) != 0){
        printf("set smart enc attr error,%d\n", ret);
        return;
    }
    if((ret = FH_VENC_StartRecvPic(0)) != 0){
          printf("FH_VENC_StopRecvPic error,%d\n", ret);
        return;
    }  
    ret = FH_SYS_BindVpu2Enc(0, 0);
    if (ret != RETURN_OK)
    {
        printf("Error: FH_SYS_BindVpu2Enc failed with %d\n", ret);
        return;
    }

}

void sample_smart_enc_exit()
{
	if (g_thread_stream != 0)
		pthread_join(g_thread_stream, NULL);
	if (g_thread_isp != 0)
		pthread_join(g_thread_isp, NULL);

	FH_SYS_Exit();
}

void sample_smart_enc_handle_sig(FH_SINT32 signo)
{
	printf("Caught %d, program exit abnormally\n", signo);
	g_exit = 1;
	sample_smart_enc_exit();
	exit(EXIT_FAILURE);
}

void *sample_smart_enc_get_stream_proc(void *arg)
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

void usage(char *program_name)
{
	fprintf(stderr, "\nUsage:  %s  <VLC IP address>  [port number (optional, default 1234)]\n\n", program_name);
	fprintf(stderr, "Example:\n");
	fprintf(stderr, "    %s 192.168.1.3\n", program_name);
	fprintf(stderr, "    %s 192.168.1.3 2345\n", program_name);
}

int main(int argc, char const *argv[])
{
	FH_VPU_SIZE vi_pic;
	FH_VPU_CHN_CONFIG chn_attr;
	FH_CHR_CONFIG cfg_param;
	FH_SINT32 ret;
	int port = -1;
	FH_SMART_CHR_CONFIG smart_cfg;
	unsigned int w, h;
	FH_SIZE picsize;

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

	signal(SIGINT, sample_smart_enc_handle_sig);
	signal(SIGQUIT, sample_smart_enc_handle_sig);
	signal(SIGKILL, sample_smart_enc_handle_sig);
	signal(SIGTERM, sample_smart_enc_handle_sig);
	
	/******************************************
	 step  1: init media platform
	******************************************/
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
	 step  6: create smart venc channel 0
	******************************************/
	smart_cfg.chn_attr.profile = FH_PRO_MAIN;
	smart_cfg.chn_attr.i_frame_intterval = 100;
	smart_cfg.chn_attr.size.u32Width = CH0_WIDTH;
	smart_cfg.chn_attr.size.u32Height = CH0_HEIGHT;
	smart_cfg.rc_config.bitrate = CH0_BIT_RATE;
	smart_cfg.init_qp = 35;
	smart_cfg.rc_config.ImaxQP = 42;
	smart_cfg.rc_config.IminQP = 30;
	smart_cfg.rc_config.PmaxQP = 42;
	smart_cfg.rc_config.PminQP = 30;
	smart_cfg.rc_config.RClevel = FH_RC_LOW;
	smart_cfg.rc_config.RCmode                = FH_RC_CBR;
	smart_cfg.rc_config.max_delay             = 8;
	smart_cfg.rc_config.FrameRate.frame_count = CH0_FRAME_COUNT;
	smart_cfg.rc_config.FrameRate.frame_time  = CH0_FRAME_TIME;

	smart_cfg.smart_attr.smart_en             = FH_TRUE;
	smart_cfg.smart_attr.texture_en           = FH_TRUE;
	smart_cfg.smart_attr.backgroudmodel_en    = FH_TRUE;
	smart_cfg.smart_attr.mbconsist_en         = FH_FALSE;

	smart_cfg.smart_attr.gop_th.GOP_TH_NUM           = 4;
	smart_cfg.smart_attr.gop_th.TH_VAL[0]            = 8;
	smart_cfg.smart_attr.gop_th.TH_VAL[1]            = 15;
	smart_cfg.smart_attr.gop_th.TH_VAL[2]            = 25;
	smart_cfg.smart_attr.gop_th.TH_VAL[3]            = 35;
	smart_cfg.smart_attr.gop_th.MIN_GOP[0]            = 380;
	smart_cfg.smart_attr.gop_th.MIN_GOP[1]            = 330;
	smart_cfg.smart_attr.gop_th.MIN_GOP[2]            = 270;
	smart_cfg.smart_attr.gop_th.MIN_GOP[3]            = 220;
	smart_cfg.smart_attr.gop_th.MIN_GOP[4]            = 160;

	ret = FH_SMART_ENC_CreateChn(0, &smart_cfg);
	if (RETURN_OK != ret) {
		printf("Error: FH_SMART_ENC_CreateChn failed with %d\n", ret);
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
	 step  9: init BGM
	******************************************/
	w = ALIGNTO(CH0_WIDTH, 16) / 8;
	h = ALIGNTO(CH0_HEIGHT, 16) / 8;
	ret = FH_BGM_InitMem(w,h);
	if (RETURN_OK != ret) {
		printf("Error: FH_BGM_InitMem failed with %d\n", ret);
		goto err_exit;
	}

	picsize.u32Width = w;
	picsize.u32Height = h;
	ret = FH_BGM_SetConfig(&picsize);
	if (RETURN_OK != ret) {
		printf("Error: FH_BGM_SetConfig failed with %d\n", ret);
		goto err_exit;
	}

	ret = FH_BGM_Enable();
	if (RETURN_OK != ret) {
		printf("Error: FH_BGM_Enable failed with %d\n", ret);
		goto err_exit;
	}

	ret = FH_SYS_BindVpu2Bgm();
	if (RETURN_OK != ret) {
		printf("Error: FH_BGM_Enable failed with %d\n", ret);
		goto err_exit;
	}

	/******************************************
	 step  10: init ISP, and then start ISP process thread
	******************************************/
	if (sample_isp_init() != 0) {
		goto err_exit;
	}
	pthread_create(&g_thread_isp, NULL, sample_isp_proc, &g_exit);

	/******************************************
	 step  11: start debug interface thread
	******************************************/
	struct dbi_tcp_config tcp_conf;
	tcp_conf.cancel = &g_exit;
	tcp_conf.port = 8888;
	pthread_create(&g_thread_dbi, NULL, tcp_dbi_thread, &tcp_conf);

	/******************************************
	 step  12: int vlcview lib
	******************************************/
	ret = vlcview_pes_init();
	if (0 != ret) {
		printf("Error: vlcview_pes_init failed with %d\n", ret);
		goto err_exit;
	}

	/******************************************
	 step  13: get stream, pack as PES stream and then send to vlc
	******************************************/
	pthread_create(&g_thread_stream, NULL, sample_smart_enc_get_stream_proc, NULL);
	vlcview_pes_send_to_vlc(0, (char *)argv[1], port);

	/******************************************
	 step  14: handle keyboard event
	******************************************/
	//printf("\nPress Enter key to exit program ...\n");
	//getchar();
	//g_exit = 1;
	print_keyboard_help();
	while(!g_exit)
	{
		char key;
		key = getchar();

		if(key == 'q')
			{
				g_exit = 1;
				printf("quit program...\n");
			}
			else if(key == 'c')
			{
		
				enable_smart(0);
				printf("close smart ok\n");
			}
			else if(key == 'o')
			{
				
				enable_smart(1);
				printf("enble smart ok\n");
			}
			sleep(1);
	}

	/******************************************
	 step  15: exit process
	******************************************/
err_exit:
	sample_smart_enc_exit();
	vlcview_pes_uninit();

	return 0;
}