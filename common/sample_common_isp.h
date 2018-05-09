#ifndef SAMPLE_COMMON_ISP_H
#define SAMPLE_COMMON_ISP_H

#include "types/type_def.h"
#define SENSOR_HEX_FILE(name) name"_attr.hex"

int sample_isp_init(void);
FH_UINT32 sample_isp_change_fps();
void *sample_isp_proc(void *arg);

#endif