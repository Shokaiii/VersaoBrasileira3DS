#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
TARGET          := VersaoBrasileira
BUILD           := build
SOURCES         := source/app source/ui source/ui/screens source/store \
                   source/repository source/download source/installer \
                   source/installer/operations source/storage source/platform
INCLUDES        := $(SOURCES) include
ROMFS           := romfs

APP_VERSION     := $(strip $(shell cat $(TOPDIR)/VERSION))
APP_TITLE       := Versão Brasileira
APP_DESCRIPTION := Traduções de fãs para o seu 3DS
APP_AUTHOR      := Versão Brasileira
ICON            := assets/icons/icon.png

#---------------------------------------------------------------------------------
ARCH      := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS    := -g -Wall -Wextra -O2 -mword-relocations -ffunction-sections $(ARCH)
CFLAGS    += $(INCLUDE) -D__3DS__ -DAPP_VERSION=\"$(APP_VERSION)\" -Wno-psabi
CFLAGS    += $(EXTRA_DEFINES)
CXXFLAGS  := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS   := -g $(ARCH)
LDFLAGS   := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
# Todo abort() grava crash.txt antes (source/platform/crash.cpp).
LDFLAGS   += -Wl,--wrap=abort

LIBS      := -lcitro2d -lcitro3d -lcurl -lmbedtls -lmbedx509 \
             -lmbedcrypto -lturbojpeg -larchive -lzstd -llz4 -llzma -lbz2 \
             -lz -lctru -lm
LIBDIRS   := $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

export OFILES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export _3DSXDEPS := $(OUTPUT).smdh
export APP_ICON := $(TOPDIR)/$(ICON)
export _3DSXFLAGS += --smdh=$(OUTPUT).smdh --romfs=$(CURDIR)/$(ROMFS)

# Ferramentas para o .cia (não vêm com o devkitPro): coloque makerom.exe e
# bannertool.exe em tools/ ou no PATH.
MAKEROM    ?= $(if $(wildcard $(TOPDIR)/tools/makerom.exe),$(TOPDIR)/tools/makerom.exe,makerom)
BANNERTOOL ?= $(if $(wildcard $(TOPDIR)/tools/bannertool.exe),$(TOPDIR)/tools/bannertool.exe,bannertool)
VERSION_PARTS := $(subst ., ,$(APP_VERSION))
# A versão do título no .cia tem 16 bits (major 6, minor 6, micro 4: o micro
# vai só até 15). Para "0.1.16" e além caberem, a versão vira um número só,
# MAJOR*16384 + MINOR*1024 + PATCH (até 3.15.1023), dividido de novo nas três
# partes que o makerom pede. O número só cresce, então o sistema sempre aceita
# a versão nova por cima da antiga.
VERSION_MAJOR := $(word 1,$(VERSION_PARTS))
VERSION_MINOR := $(word 2,$(VERSION_PARTS))
VERSION_PATCH := $(word 3,$(VERSION_PARTS))
TITLE_VERSION := $(shell echo $$(( $(VERSION_MAJOR) * 16384 + $(VERSION_MINOR) * 1024 + $(VERSION_PATCH) )))
CIA_MAJOR     := $(shell echo $$(( $(TITLE_VERSION) >> 10 )))
CIA_MINOR     := $(shell echo $$(( ($(TITLE_VERSION) >> 4) & 63 )))
CIA_MICRO     := $(shell echo $$(( $(TITLE_VERSION) & 15 )))
CIA_VERSION_OK := $(shell [ $(VERSION_MAJOR) -le 3 ] && [ $(VERSION_MINOR) -le 15 ] && [ $(VERSION_PATCH) -le 1023 ] && echo yes)

.PHONY: all clean version cia

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf $(TARGET).cia

version:
	@echo $(APP_VERSION)

cia: all
ifneq ($(CIA_VERSION_OK),yes)
	$(error A versao $(APP_VERSION) nao cabe no .cia: o maximo e 3.15.1023)
endif
	@mkdir -p $(BUILD)
	@$(BANNERTOOL) makebanner -i resources/banner.png -a resources/banner.wav -o $(BUILD)/banner.bnr
	@$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i $(ICON) -o $(BUILD)/icon.icn
	@$(MAKEROM) -f cia -o $(TARGET).cia -elf $(TARGET).elf -rsf resources/VersaoBrasileira.rsf -icon $(BUILD)/icon.icn -banner $(BUILD)/banner.bnr -exefslogo -target t -major $(CIA_MAJOR) -minor $(CIA_MINOR) -micro $(CIA_MICRO)
	@echo built ... $(TARGET).cia

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).elf: $(OFILES)

# Uma versão nova recompila tudo, para o número aparecer certo no app.
$(OFILES): $(TOPDIR)/VERSION

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
