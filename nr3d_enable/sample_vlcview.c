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
#include "isp/isp_common.h"

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


// close IPF module must set after second frame
// you can call this function in sample_isp_proc(void *arg)
extern void nr3d_enable()
{
	// software & hardware shut down nr3d

	ISP_NR3D_CFG nr3dCfg;
	FH_UINT32 u32RegData;

    memset(&nr3dCfg, 0, sizeof(nr3dCfg));
    API_ISP_GetNR3D(&nr3dCfg);
    nr3dCfg.bNR3DEn = 0;
    API_ISP_SetNR3D(&nr3dCfg);

    API_ISP_GetIspReg(0xea000010, &u32RegData);
    printf("\n%x\n", u32RegData);
    u32RegData |= 0x00800000;
    API_ISP_SetIspReg(0xea000010, u32RegData);
    printf("\n%x\n", u32RegData);

}

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
