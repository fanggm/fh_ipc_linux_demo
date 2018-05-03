include ../Rules.make

LIBS = $(SDK_LIBS) -lpthread -lvlcview -lrt -lmipi

ifeq ($(DEBUG),0)
  CFLAGS 	+= -O2
else
  CFLAGS 	+= -O0 -g3
endif

BIN = sample_mjpeg

SRCS = $(wildcard *.c) $(wildcard ../common/*.c)
OBJS	= $(SRCS:%.c=%.o)
DEPENDS	= $(subst .o,.d,$(OBJS))

all: $(BIN) install

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

install:
	-mkdir -p $(INSTALL_DIR)
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