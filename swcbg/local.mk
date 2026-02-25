# swc: swcbg/local.mk

dir := swcbg

$(dir)_PACKAGES := wayland-client wld libpng
$(dir)_CFLAGS := -I. -Iprotocol
$(dir)_TARGETS := $(dir)/swcbg

$(dir): $(dir)/swcbg

$(dir)/swcbg: $(dir)/main.o protocol/swc-protocol.o
	$(link) $($(dir)_PACKAGE_LIBS) -lpng

$(dir)/main.o: protocol/swc-client-protocol.h

CLEAN_FILES += $(dir)/main.o

include common.mk
