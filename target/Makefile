# ==============================================================================
# QNX-ChaosPulse: Target C Microkernel Engine Makefile
# Track: Developer Tooling & Host Tools | BlackBerry QNX Global Hackathon 2026
#
# Builds the native microkernel telemetry streamer and chaos injector daemon.
# Supports both QNX Neutrino SDP 8.0 (qcc) cross-compilation and POSIX fallback (gcc).
# ==============================================================================

.PHONY: all clean qnx posix

RM = rm -f
TARGET_BIN = bin/chaospulse_daemon
TARGET_LIB = bin/libchaospulse.a

SRCS = src/chaospulse_daemon.c \
       src/cycle_telemetry.c \
       src/pulse_interceptor.c \
       src/cpu_storm_generator.c

OBJS = $(SRCS:.c=.o)

# Detect QNX Neutrino 8.0 SDP cross-compilation vs Host POSIX fallback
ifdef QNX_TARGET
	# Native QNX Neutrino SDP 8.0 Toolchain
	QNX_HOST ?= $(QNX_TARGET)/../host/linux/x86_64
	CC = $(QNX_HOST)/usr/bin/qcc -Vgcc_ntox86_64
	CFLAGS = -O3 -Wall -Wextra -D__QNXNTO__ -Iinclude
	LDFLAGS = -M -lsocket
	PLATFORM_DESC = QNX Neutrino RTOS 8.0 SDP
else
	# Host POSIX Fallback (Linux / macOS / Alpine Docker runner)
	CC ?= gcc
	CFLAGS = -O3 -Wall -Wextra -pedantic -std=c11 -pthread -Iinclude \
	         -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_GNU_SOURCE
	LDFLAGS = -pthread -lm
	PLATFORM_DESC = Host POSIX Real-Time Fallback Emulation
endif

all: $(TARGET_BIN)

bin:
	mkdir -p bin

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET_BIN): $(OBJS) | bin
	$(CC) $(OBJS) -o $(TARGET_BIN) $(LDFLAGS)
	@echo "=================================================================="
	@echo " Build Success: $(TARGET_BIN)"
	@echo " Target Platform: $(PLATFORM_DESC)"
	@echo "=================================================================="

qnx:
	$(MAKE) all QNX_TARGET=$(QNX_TARGET)

posix:
	$(MAKE) all

clean:
	$(RM) src/*.o $(TARGET_BIN) $(TARGET_LIB)
	rm -rf bin
