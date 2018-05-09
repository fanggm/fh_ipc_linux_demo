demo目录结构
=============
sample
    |-- common                        # 公共目录，存放通用代码，通常是将各个示例代码都会用到的代码片段封装到这里;
    |   |-- sample_common_isp.h       # 目前是将ISP相关的代码放在这里
    |   `-- sample_common_isp.c
    |-- mjpeg                         # 广播MJPEG码流到客户端，客户端可以通过VLC或浏览器（Firefox、Chrome）观看
    |   |-- Makefile
    |   `-- sample_mjpeg.c
    |-- vlcview                       # 发送码流到VLC客户端，可以实时观看图像
    |   |-- Makefile
    |   `-- sample_vlcview.c
    |-- venc                          # 编码API示例
    |   |-- Makefile
    |   `-- sample_venc.c                
    |-- overlay                       # OSD、LOGO、MASK API示例
    |   |-- Makefile
    |   `-- sample_overlay.c 
    |-- audio                         # 音频API示例
    |   |-- Makefile
    |   `-- sample_audio.c 
    |-- motion_detect                 # 移动侦测API示例
    |   |-- Makefile
    |   `-- sample_md.c
    |-- cover_detect                  # 遮挡侦测API示例
    |   |-- Makefile
    |   `-- sample_cd.c
    |-- gpio                          # GPIO示例
    |   |-- Makefile
    |   `-- sample_gpio.c
    |-- smart_enc                     # 智能编码示例
    |   |-- Makefile
    |   `-- sample_gpio.c
    |-- face_detect                   # 人脸检测示例
    |   |-- Makefile
    |   `-- sample_gpio.c
    |-- Makefile                      # 总控makefile,会将所有子目录编译一遍
    `-- Rules.make                    # 编译所依赖的变量定义集中在这个文件里，包括交叉编译器、SDK头文件路径、SDK链接库路径、sample安装路径

编译
=============
可以在demo目录这一层执行make，也可以单独进入一个demo子目录执行make

   $make

如何添加demo
=============
1.创建一个新的目录，在其中添加.h和.c文件，内容可以参照venc/sample_venc.c
2.在第1步创建的目录下新建文本文件，命名为Makefile，将以下内容复制到文件中，注意修改CFLAGS、LDFLAGS、LIBS、BIN、SRCS变量的值

--------- 从下面一行开始复制 --------------
include ../Rules.make

LIBS = $(SDK_LIBS) -lpthread

ifeq ($(DEBUG),0)
  CFLAGS 	+= -O2
else
  CFLAGS 	+= -O0 -g3
endif

BIN = sample_test

SRCS = $(wildcard *.c) $(wildcard ../common/*.c)
OBJS	= $(SRCS:%.c=%.o)
DEPENDS	= $(subst .o,.d,$(OBJS))

all: $(BIN) install

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

install:
	mkdir -p $(INSTALL_DIR)
	-cp -a $(BIN) $(INSTALL_DIR)

clean:
	rm -f $(BIN)
	rm -f *.o *.d
	rm -f ../common/*.o ../common/*.d

ifneq "$(MAKECMDGOALS)" "clean"
  -include $(DEPENDS)
endif

%.o: %.c
	$(CC) $(CFLAGS) -c -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"

.PHONY: clean
--------- 复制截止到上面一行 --------------

3.进入第1步创建的目录，执行make即可进行编译：

   $ make

4.在demo主目录下，执行make，会自动编译所有样例程序

