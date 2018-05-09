#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>

#include "sample_common_isp.h"
#include "dsp/fh_vpu_mpi.h"
#include "isp/isp_common.h"
#include "isp/isp_api.h"
#include "isp/isp_enum.h"
#include "sample_opts.h"

#define ERR_CNT	5

FH_SINT32 g_isp_format = ISP_FORMAT;
struct isp_sensor_if *g_sensor;
FH_SINT32 g_isp_init_width = ISP_INIT_WIDTH;
FH_SINT32 g_isp_init_height = ISP_INIT_HEIGHT;


void get_program_path(char *prog_path)
{
	FH_SINT8 cmdline[256];
	FH_SINT8 path[256];
	FH_SINT8 *dname;
	FILE *proc_file;
	pid_t pid = getpid();

	sprintf(cmdline, "/proc/%i/cmdline", pid);
	proc_file = fopen(cmdline, "r");
	fgets(path, 256, proc_file);
	fclose(proc_file);
	dname = dirname(path);
	strcpy(prog_path, dname);
}

FH_UINT32 sample_isp_change_fps(void)
{
	FH_UINT32 fps;

	switch(g_isp_format)
	{
	case  FORMAT_720P25:
		g_isp_format = FORMAT_720P30;
		fps = 30;
		break;
	case FORMAT_720P30:
		g_isp_format = FORMAT_720P25;
		fps = 25;
		break;
	case FORMAT_960P25:
		g_isp_format = FORMAT_960P30;
		fps = 30;
		break;
	case FORMAT_960P30:
		g_isp_format = FORMAT_960P25;
		fps = 25;
		break;
	case FORMAT_1080P25:
		g_isp_format = FORMAT_1080P30;
		fps = 30;
		break;
	case FORMAT_1080P30:
		g_isp_format = FORMAT_1080P25;
		fps = 25;
		break;
	}

	API_ISP_SetSensorFmt(g_isp_format);

	return fps;
}

void isp_vpu_reconfig(void)
{
	FH_VPU_SIZE vpu_size;
	ISP_VI_ATTR_S isp_vi;

	FH_VPSS_Reset();
	API_ISP_Pause();
	API_ISP_Resume();

	FH_VPSS_GetViAttr(&vpu_size);
	API_ISP_GetViAttr(&isp_vi);
	if (vpu_size.vi_size.u32Width != isp_vi.u16PicWidth
		|| vpu_size.vi_size.u32Height != isp_vi.u16PicHeight)
	{
		vpu_size.vi_size.u32Width = isp_vi.u16PicWidth;
		vpu_size.vi_size.u32Height = isp_vi.u16PicHeight;
		FH_VPSS_SetViAttr(&vpu_size);
	}

	API_ISP_KickStart();
}

int sample_isp_init(void)
{
	FH_SINT32 ret;
	FH_UINT32 param_addr,param_size;
	FH_SINT8 *filename = SENSOR_HEX_FILE(SENSOR);

	ret = API_ISP_MemInit(g_isp_init_width, g_isp_init_height);
	if (ret) {
		printf ("Error: API_ISP_MemInit failed with %d\n", ret);
		return ret;
	}
	ret = API_ISP_GetBinAddr(&param_addr, &param_size);
	if (ret) {
		printf("Error: API_ISP_GetBinAddr failed with %d\n", ret);
		return ret;
	}

	g_sensor = Sensor_Create();
	if (g_sensor == NULL) {
		printf("Error: sensor create error\n");
		return -1;
	}

	API_ISP_SensorRegCb(0, g_sensor);
	API_ISP_SensorInit();
	API_ISP_SetSensorFmt(g_isp_format);

	ret = API_ISP_Init();
	if (ret) {
		printf("Error: API_ISP_Init failed with %d\n", ret);
		return ret;
	}
	FILE *param_file;
	FH_SINT8 path[256];
	get_program_path(path);
	strcat(path, "/");
	strcat(path, filename);
	param_file = fopen(path, "rb");
	if (param_file == NULL) {
		printf("Error: open %s failed!\n", path);
		return -2;
	}
	FH_SINT8 isp_param_buff[param_size];
	fread(isp_param_buff, 1, param_size, param_file);
	API_ISP_LoadIspParam(isp_param_buff);
	fclose(param_file);

	return 0;
}


void *sample_isp_proc(void *arg)
{
	FH_SINT32 ret;
	FH_SINT32 err_cnt = 0;
	FH_SINT32 *cancel = (FH_SINT32 *)arg;

	while (!*cancel) {
		ret = API_ISP_Run();
		if (ret) {
			err_cnt++;
			ret = API_ISP_DetectPicSize();
			if (ret && err_cnt > ERR_CNT) {
				isp_vpu_reconfig();
				err_cnt = 0;
			}
		}
		usleep(10000);
	}

	API_ISP_Exit();
	return NULL;
}
