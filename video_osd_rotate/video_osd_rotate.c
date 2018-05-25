#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>

#include "types/type_def.h"
#include "dsp/fh_system_mpi.h"
#include "dsp/fh_vpu_mpi.h"
#include "dsp/fh_venc_mpi.h"
#include "dsp/fh_jpeg_mpi.h"
#include "dsp_ext/FHAdv_OSD_mpi.h"
#include "sample_common_isp.h"
#include "libvlcview.h"
#include "dbi/dbi_over_tcp.h"
#include "config_1080p25.h"
#include "osd_fontlib.h"


#define ENABLE_OSD
#define VIDEO_ROTATE

#define LOGO_ALPHA_MAX  255
#define LOGO_MAX_WIDTH  64
#define LOGO_MAX_HEIGHT 64

static FH_SINT32 g_exit = 0;
FH_SINT32 i_count = 1;
static pthread_t g_thread_isp = 0;
static pthread_t g_thread_osd = 0;
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

FH_SINT32 load_font_lib(FHT_OSD_FontType_e type, unsigned char *font_array, int font_array_size)
{
    FH_SINT32 ret;
    FHT_OSD_FontLib_t font_lib;

    font_lib.pLibData = font_array;
    font_lib.fontSize = font_array_size;

    ret = FHAdv_Osd_LoadFontLib(type, &font_lib);
    if (ret != 0) {
        printf("Error: FHAdv_Osd_LoadFontLib failed, ret=%d\n", ret);
        return ret;
    }

    return 0;
}


//# 对主码流设置90/180/270/360度旋转（若使用vlc无法实时看到旋转效果，更新vlc版本）

void *set_osd(void *arg)
{
    
    FH_SINT32 ret;
	FHT_OSD_CONFIG_t osd_config;

	ret = FHAdv_Osd_Init(VIDEO_INPUT_WIDTH, VIDEO_INPUT_HEIGHT); // same with channel width and height;
	if (ret != RETURN_OK) {
		printf("Error: FHAdv_Osd_Init failed with %d\n", ret);
		return;
	}

    if (0 != load_font_lib(FHEN_FONT_TYPE_ASC, asc16, sizeof(asc16))){
        printf("set asc lib failed\r\n");
        return NULL;
    }

    if (0 != load_font_lib(FHEN_FONT_TYPE_CHINESE, gb2312, sizeof(gb2312))){
        printf("set chiese lib failed\r\n");
        return NULL;
    }

    memset(&osd_config, 0, sizeof(osd_config));

    osd_config.osdColor.norcolor.fRed = 255;
    osd_config.osdColor.norcolor.fGreen = 255;
    osd_config.osdColor.norcolor.fBlue = 255;
    osd_config.osdColor.norcolor.fAlpha = 128;
    osd_config.osdColor.invcolor.fRed = 0;
    osd_config.osdColor.invcolor.fGreen = 0;
    osd_config.osdColor.invcolor.fGreen = 0;
    osd_config.osdColor.invcolor.fAlpha = 128;

    osd_config.timeOsdEnable = 1;
    osd_config.timeOsdPosition.pos_x = 0;
    osd_config.timeOsdPosition.pos_y = 0;
    osd_config.timeOsdFormat = 9;
    osd_config.timeOsdNorm = 0;
    osd_config.weekFlag = 1;

    osd_config.text01Enable = 1;
    osd_config.sttext01Position.pos_x = 0;
    osd_config.sttext01Position.pos_y = 64;
    strcpy(osd_config.text01Info, "channel 0");

    osd_config.text02Enable = 1;
    osd_config.sttext02Position.pos_x = 0;
    osd_config.sttext02Position.pos_y = 128;
    strcpy(osd_config.text02Info, "OSD Demo");
    
    osd_config.osdSize.width = 32;
    osd_config.osdSize.height = 32;

   	ret = FHAdv_Osd_SetText(&osd_config);
	if (ret != RETURN_OK) {
		printf("Error: FHAdv_Osd_SetText failed with %d\n", ret);
	}

	char str[256];
    FH_CHN_STATUS chnstat;

    //FHAdv_Osd_SetTextRotate(1, 0);
    while (!g_exit)
    {
        FH_VENC_GetChnStatus(0, &chnstat);
        snprintf(str, 256, "bitrate: %.2f kbps", (float)chnstat.bps / 1000);

        strcpy(osd_config.text02Info, str);
        FHAdv_Osd_Ex_SetText(0, &osd_config);
        usleep(1000000);
    }
    return NULL;
}


void osd_rotate(int osd_rotate)
{
	FHAdv_Osd_Ex_SetTextRotate(0, 1, osd_rotate);
}

void sample_overlay_set_logo()
{
	FH_SINT32 ret;
	FHT_OSD_Logo_t logo_config;
	FILE *logo_file;
	struct stat stat_info;

	logo_config.enable = 1;
	logo_config.alpha = 255;
	logo_config.area.fTopLeftX = 640;
	logo_config.area.fTopLeftY = 360;
	logo_config.area.fWidth = 64;
	logo_config.area.fHeigh = 64;

	logo_config.pData = malloc(LOGO_MAX_WIDTH * LOGO_MAX_HEIGHT * 4);
	if (logo_config.pData == NULL) {
		printf("Error: failed to malloc logo data buffer\n");
		return;
	}

	if (stat("logo.hex", &stat_info) < 0) {
		printf("Error: get filesize of logo.hex failed\n");
		return;
	}

	logo_file = fopen("logo.hex", "rb");
	if (logo_file == NULL) {
		printf("Error: failed to open logo.hex (%s)\n", strerror(errno));
		return;
	}
	fread(logo_config.pData, 1, stat_info.st_size, logo_file);
	fclose(logo_file);

	ret = FHAdv_Osd_SetLogo(&logo_config);
	if (ret != RETURN_OK) {
		printf("Error: FHAdv_Osd_SetLogo failed with %d\n", ret);
	}
}

void print_keyboard_help()
{
	printf("Keyboard function:\n");
	printf("   q    quit\n");
	printf("   r    rotate video \n");
}

void video_rotate()
{
	FH_ROTATE rotate_info;
	FH_SINT32 channel = 0;
	static FH_UINT32 rotate_value = 0;
	FH_SINT32 ret;

	rotate_value = (rotate_value + 1) % 4;	
	rotate_info.enable = 1;
	rotate_info.rotate = rotate_value;
	ret = FH_VENC_SetRotate(channel, &rotate_info);
	if (ret != RETURN_OK) {
		printf("Error: FH_VENC_SetRotate failed with %d\n", ret);
	}

	printf("Rotate to %d degree\n\n", rotate_value * 90);
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

	#ifdef ENABLE_OSD
    printf("create osd thread\r\n");
    
    pthread_create(&g_thread_osd, NULL, set_osd, NULL);
	#endif

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

	#ifdef VIDEO_ROTATE
	print_keyboard_help();



	while (!g_exit) {
		char key;
		
		key = getchar();
		
		if (key == 'q')
		{
			print_keyboard_help();
			g_exit =1;
		}
		else if (key == 'r') {
		
			printf("i_count:%d\n", i_count);
			osd_rotate(i_count);
			video_rotate();
			print_keyboard_help();
			i_count++;
			if(i_count >3)
				i_count = 0;
				
		}
		sleep(1);

	}
	#endif

	/******************************************
	 step  13: handle keyboard event
	******************************************/
	//printf("\nPress Enter key to exit program ...\n");
	//getchar();
	//g_exit = 1;

	/******************************************
	 step  14: exit process
	******************************************/
err_exit:
	sample_vlcview_exit();
	vlcview_pes_uninit();

	return 0;
}

