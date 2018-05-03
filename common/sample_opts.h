#ifndef __SAMPLE_OPTS_H__
#define __SAMPLE_OPTS_H__

/* isp configuration */
#define ISP_FORMAT FORMAT_1080P25
#define ISP_INIT_WIDTH 1920
#define ISP_INIT_HEIGHT 1088

/* dsp configuration */
#define VIDEO_INPUT_WIDTH 1920
#define VIDEO_INPUT_HEIGHT 1080

/* channel 0 configuration */
#define CH0_WIDTH 1920
#define CH0_HEIGHT 1080
#define CH0_BIT_RATE (512*1024)
#define CH0_FRAME_COUNT 25
#define CH0_FRAME_TIME 1

/* channel 1 configuration */
#define CH1_WIDTH 352
#define CH1_HEIGHT 288
#define CH1_BIT_RATE (512*1024)
#define CH1_FRAME_COUNT 25
#define CH1_FRAME_TIME 1

#define JPEG_INIT_WIDTH 1920
#define JPEG_INIT_HEIGHT 1088

#endif
