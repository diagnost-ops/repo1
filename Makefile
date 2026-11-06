ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO, e.g.: export DEVKITPRO=/opt/devkitpro")
endif

LIBNX      ?= $(DEVKITPRO)/libnx
TARGET     := joycon_sound_test
APP_TITLE  := Joy-Con Sound Test
APP_AUTHOR := Alexandr
APP_VERSION:= 1.0.0

CC     := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-gcc
ARCH   := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -g -Wall -O2 -ffunction-sections $(ARCH) -D__SWITCH__ -I$(LIBNX)/include
LDFLAGS:= -specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,-Map,$(TARGET).map
LIBS   := -L$(LIBNX)/lib -lnx -lm

.RECIPEPREFIX = >

all: $(TARGET).nro

$(TARGET).elf: source/main.c
> $(CC) $(CFLAGS) $< $(LDFLAGS) $(LIBS) -o $@

$(TARGET).nacp:
> $(DEVKITPRO)/tools/bin/nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(TARGET).nro: $(TARGET).elf $(TARGET).nacp
> $(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=$(TARGET).nacp

clean:
> rm -f $(TARGET).elf $(TARGET).nro $(TARGET).nacp $(TARGET).map

.PHONY: all clean
