# swc: swcshot/local.mk

dir := swcshot

$(dir)_PACKAGES := wayland-client wld
$(dir)_CFLAGS := -I. -Iprotocol
$(dir)_TARGETS := $(dir)/swcshot

$(dir): $(dir)/swcshot

$(dir)/swcshot: $(dir)/swcshot.o protocol/swc-protocol.o
	$(link) $($(dir)_PACKAGE_LIBS)

$(dir)/swcshot.o: protocol/swc-client-protocol.h

CLEAN_FILES += $(dir)/swcshot.o

include common.mk
