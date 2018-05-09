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
#include "isp/isp_api.h"
#include "sample_common_isp.h"
#include "sample_opts.h"

//#define  CHANNEL_CHOSE 	2

static	int chan_chose;

struct channel_info {
	FH_UINT32 width;     /* align to 16 */
	FH_UINT32 height;    /* align to 16 */
	FH_UINT32 framerate;
	FH_UINT32 bps;
};

static struct channel_info g_three_channels[] = {
	{
		.width = CH0_WIDTH,
		.height = CH0_HEIGHT+8,
		.framerate = ((CH0_FRAME_TIME<<16) | (CH0_FRAME_COUNT)),
		.bps = CH0_BIT_RATE
	},
	{
		.width = CH1_WIDTH,
		.height = CH1_HEIGHT,
		.framerate = ((CH1_FRAME_TIME<<16) | (CH1_FRAME_COUNT)),
		.bps = CH1_BIT_RATE
	},
	{
		.width = CH2_WIDTH,
		.height = CH2_HEIGHT,
		.framerate = ((CH2_FRAME_TIME<<16) | (CH2_FRAME_COUNT)),
		.bps = CH2_BIT_RATE
	}
};

static FH_SINT32 g_exit = 0;
static pthread_t g_thread_isp = 0;
static pthread_t g_thread_dbi = 0;
static pthread_t g_thread_stream = 0;


void change_8X8_to_16X16(FH_UINT8 *src, FH_UINT8 *dst)
{
	FH_SINT32 h, w, numblock;

	for (numblock = 0; numblock < 4; numblock++) {
		for (h = 0; h < 8; h++) {
			for (w = 0; w < 8; w++) {
				*(dst + numblock / 2 * 8 * 8 * 2 + 2 * 8 * h + numblock % 2 * 8 + w) = *(src + numblock * 8 * 8 + h * 8 + w);
			}
		}
	}
}

void yuv_transform(FH_UINT8 *src_y, FH_UINT8 *src_c, FH_UINT8 *dst, FH_UINT32 w, FH_UINT32 h)
{
	FH_SINT32 w_align, h_align;
	FH_SINT32 w_align32, h_align32;
	FH_SINT32 i, j, numblock;
	FH_UINT8 orderbuf[8*8*4];
	FH_UINT8 *ybuf, *cbuf, *yuvbuf;
	FH_UINT8 *ubuf, *vbuf;

	w_align = (w + 15)&(~15);
	h_align = (h + 15)&(~15);
	w_align32 = (w + 31)&(~31);
	h_align32 = (h + 31)&(~31);

	ybuf = src_y;
	cbuf = src_c;
	yuvbuf = dst;

	// Y
	for (numblock = 0; numblock < (w_align*h_align/16/16); numblock++) {
		change_8X8_to_16X16(ybuf + 8*8*4 *numblock, orderbuf);
		for (i = 0; i < 16; i++) {
			for (j = 0; j < 16; j++) {
				*(yuvbuf + numblock / (w_align / 16) * w * 16 + numblock %( w_align / 16 ) * 16 + i * w + j) =*(orderbuf + i * 16 +j) ;
			}
		}
	}

	// UV
	ubuf = yuvbuf + w*h;
	vbuf = yuvbuf + + w*h + w*h/4;
	for (numblock = 0; numblock < (w_align*h_align/16/16); numblock++) {
		for (i = 0; i < 8; i++) {
			for (j = 0; j < 8; j++) {
				*(ubuf + numblock  / (w_align / 16) * w / 2 * 8 + numblock %( w_align / 16 ) * 8 + i * w / 2 + j) = *(cbuf + numblock * 8 * 8 * 2 + i * 8 + j);
				*(vbuf + numblock  / (w_align / 16) * w / 2 * 8 + numblock %( w_align / 16 ) * 8 + i * w / 2 + j) = *(cbuf + numblock * 8 * 8 * 2 + 8 * 8 + i * 8 + j);
			}
		}
	}
}

void capture_yuv()
{
	FILE *yuv_file;
	FH_VPU_STREAM yuv_data;
	FH_UINT8 *dst;
	FH_SINT32 ret;
	FH_SINT32 chan = chan_chose;

	dst = malloc(g_three_channels[chan].width * g_three_channels[chan].height * 3 / 2);
	if (dst == NULL) {
		printf("Error: failed to allocate yuv transform buffer\n");
		return;
	}

	ret = FH_VPSS_GetChnFrame(chan, &yuv_data);
	if (ret == RETURN_OK) {
		yuv_transform(yuv_data.yluma.vbase, yuv_data.chroma.vbase, dst, g_three_channels[chan].width, g_three_channels[chan].height);
		yuv_file = fopen("chnn.yuv", "wb");
		fwrite(dst, 1, g_three_channels[chan].width * g_three_channels[chan].height * 3 / 2, yuv_file);
		fclose(yuv_file);
		printf("GET CHN :%d YUV DATA w:%d h:%d to file chnn.yuv\n\n",chan, g_three_channels[chan].width , g_three_channels[chan].height);
	} else {
		printf("Error: FH_VPSS_GetChnFrame failed with %d\n\n", ret);
		return;
	}

	free(dst);	
}

void sample_venc_exit()
{
	if (g_thread_stream != 0)
		pthread_join(g_thread_stream, NULL);
	if (g_thread_dbi != 0)
		pthread_join(g_thread_dbi, NULL);
	if (g_thread_isp != 0)
		pthread_join(g_thread_isp, NULL);

	FH_SYS_Exit();
}

void sample_venc_handle_sig(FH_SINT32 signo)
{
	printf("Caught %d, program exit abnormally\n", signo);
	g_exit = 1;
	sample_venc_exit();
	exit(EXIT_FAILURE);
}

void *sample_venc_get_stream_proc(void *arg)
{
	FH_SINT32 i;
	FH_SINT32 ret;
	FH_ENC_STREAM_ELEMENT stream;
	FILE *stream_files[3];
	char name[32];

	for (i = 0; i < 3; i++) {
		sprintf(name, "CH%d.264", i);
		stream_files[i] = fopen(name, "wb");
		if (stream_files[i] == NULL) {
			printf("Error: failed to create file %s (%s)\n", name, strerror(errno));
			return NULL;
		}
	}

	while (!g_exit) {
		for (i = 0; i < 3; i++) {
			do {
				ret = FH_VENC_GetStream(i, &stream);
				if (ret == RETURN_OK) {
					fwrite(stream.start, 1, stream.length, stream_files[stream.chan]);
					FH_VENC_ReleaseStream(i);
				}
			} while (ret == RETURN_OK);
		}
		usleep(20000);
	}

	return NULL;
}

void print_keyboard_help()
{
	printf("Keyboard function:\n");
	printf("   u    save one frame yuv data to file\n");
}

int main(int argc, char const *argv[])
{
	FH_VPU_SIZE vi_pic;
	FH_VPU_CHN_CONFIG chn_attr;
	FH_CHR_CONFIG cfg_param;
	FH_SINT32 i;
	FH_SINT32 ret;

	signal(SIGINT, sample_venc_handle_sig);
	signal(SIGQUIT, sample_venc_handle_sig);
	signal(SIGKILL, sample_venc_handle_sig);
	signal(SIGTERM, sample_venc_handle_sig);
	
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
	 step  4: create 3 venc channels
	******************************************/
	for (i = 0; i < 3; i++) {
		/******************************************
		 step 4.1 configure vpss channel
		******************************************/
		chn_attr.vpu_chn_size.u32Width = g_three_channels[i].width;
		chn_attr.vpu_chn_size.u32Height = g_three_channels[i].height;
		ret = FH_VPSS_SetChnAttr(i, &chn_attr);
		if (RETURN_OK != ret) {
			printf("Error: FH_VPSS_SetChnAttr failed with %d\n", ret);
			goto err_exit;
		}

		/******************************************
		 step 4.2 open vpss channel
		******************************************/
		ret = FH_VPSS_OpenChn(i);
		if (RETURN_OK != ret) {
			printf("Error: FH_VPSS_OpenChn failed with %d\n", ret);
			goto err_exit;
		}

		/******************************************
		 step 4.3 create venc channel
		******************************************/
		cfg_param.chn_attr.size.u32Width = g_three_channels[i].width;
		cfg_param.chn_attr.size.u32Height = g_three_channels[i].height;
		cfg_param.rc_config.bitrate = g_three_channels[i].bps;
		cfg_param.rc_config.FrameRate.frame_count = (g_three_channels[i].framerate & 0xffff);
		cfg_param.rc_config.FrameRate.frame_time  = (g_three_channels[i].framerate >> 16);
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
		ret = FH_VENC_CreateChn(i, &cfg_param);
		if (RETURN_OK != ret) {
			printf("Error: FH_VENC_CreateChn failed with %d\n", ret);
			goto err_exit;
		}

		/******************************************
		 step 4.4 start venc channel
		******************************************/
		ret = FH_VENC_StartRecvPic(i);
		if (RETURN_OK != ret) {
			printf("Error: FH_VENC_StartRecvPic failed with %d\n", ret);
			goto err_exit;
		}

		/******************************************
		 step 4.5 bind vpss channel with venc channel
		******************************************/
		ret = FH_SYS_BindVpu2Enc(i, i);
		if (RETURN_OK != ret) {
			printf("Error: FH_SYS_BindVpu2Enc failed with %d\n", ret);
			goto err_exit;
		}
	}

	/******************************************
	 step  5: init ISP, and then start ISP process thread
	******************************************/
	if (sample_isp_init() != 0) {
		goto err_exit;
	}
	pthread_create(&g_thread_isp, NULL, sample_isp_proc, &g_exit);

	/******************************************
	 step 6: get stream, then save it to file
	******************************************/
	pthread_create(&g_thread_stream, NULL, sample_venc_get_stream_proc, NULL);

	/******************************************
	 step 7: handle keyboard event
	******************************************/
	
	printf("choose one channel capture yuv\n");
	scanf("%d",&chan_chose);
	if (chan_chose >=3)
	{
	printf("Error: choose channel error\n");
	return ;
	}

	print_keyboard_help();
	while (!g_exit) {
		char key;
			
		key = getchar();
		if (key == 'u') {
			capture_yuv();
			printf("u    save one frame yuv data to file\n");
		}
		sleep(1);
	}

	/******************************************
	 step 8: exit process
	******************************************/
err_exit:
	sample_venc_exit();

	return 0;
}