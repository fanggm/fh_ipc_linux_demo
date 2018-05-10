#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include "types/type_def.h"
#include "dsp/fh_system_mpi.h"
#include "dsp/fh_vpu_mpi.h"
#include "dsp/fh_jpeg_mpi.h"
#include "sample_common_isp.h"
#include "libvlcview.h"
#include "sample_opts.h"

static FH_SINT32 g_exit = 0;
static pthread_t g_thread_isp = 0;
static pthread_t g_thread_stream = 0;


void sample_mjpeg_exit()
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
	FH_JPEG_STREAM_INFO jpeg_info;
	FH_JPEG_CONFIG jpeg_config;

	jpeg_config.QP = 50;	
	jpeg_config.rate = 0x70;
	jpeg_config.rotate = 0;
	FH_JPEG_Setconfig(&jpeg_config);

	while (!g_exit) {
		ret = FH_SYS_BindVpu2Jpeg(1);
		if (ret == 0) {
			do {
				ret = FH_JPEG_Getstream_Block(&jpeg_info);
			} while (ret != 0);

			if (jpeg_info.stream.size != 0) {
				vlcview_mjpeg_stream_pack(jpeg_info.stream.addr, jpeg_info.stream.size);
			} else {
				printf("jpeg stream size is zero\n");
			}
		} else {
			printf("jpeg bind failed %d\n", ret);
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

	if (argc > 1) {
		port = strtol(argv[1], NULL, 0);
		if (port == 0) {
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
	 step  4: configure vpss channel 1
	******************************************/
	chn_attr.vpu_chn_size.u32Width = CH1_WIDTH;
	chn_attr.vpu_chn_size.u32Height = CH1_HEIGHT;
	ret = FH_VPSS_SetChnAttr(1, &chn_attr);
	if (RETURN_OK != ret) {
		printf("Error: FH_VPSS_SetChnAttr failed with %d\n", ret);
		goto err_exit;
	}

	/******************************************
	 step  5: open vpss channel 1
	******************************************/
	
	ret = FH_VPSS_OpenChn(1);
	if (RETURN_OK != ret) {
		printf("Error: FH_VPSS_OpenChn failed with %d\n", ret);
		goto err_exit;
	}

	/******************************************
	 step  6: init ISP, and then start ISP process thread
	******************************************/
	#if 1
	if (sample_isp_init() != 0) {
		goto err_exit;
	}

	pthread_create(&g_thread_isp, NULL, sample_isp_proc, &g_exit);

	/******************************************
	 step  7: init jpeg system
	******************************************/
	
	ret = FH_JPEG_InitMem(JPEG_INIT_WIDTH, JPEG_INIT_HEIGHT);
	if (ret != RETURN_OK) {
		printf("Error: FH_JPEG_InitMem failed with %d\n", ret);
		goto err_exit;
	}
	
	/******************************************
	 step  8: start mjpeg server thread and mjpeg stream thread,
	          the server waits the clients to connect
	******************************************/
	vlcview_mjpeg_start_server(port);
	pthread_create(&g_thread_stream, NULL, sample_mjpeg_get_stream_proc, NULL);
	#endif
	/******************************************
	 step  9: handle keyboard event
	******************************************/
	printf("\nPress Enter key to exit program ...\n");
	getchar();
	g_exit = 1;

	/******************************************
	 step  10: exit process
	******************************************/
err_exit:
	sample_mjpeg_exit();

	return 0;
}