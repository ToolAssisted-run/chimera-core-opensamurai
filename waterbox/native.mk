# native.mk - the native reference: the same OpenSamurai and core sources as
# guest.mk, built for the host, plus the two harnesses (run-native drives the
# exports directly; run-wbx drives core.wbx through the miniBox host exactly as
# the frontend does). Objects land in build/native.
#
# Usage: make -f native.mk -j$(nproc) [MB=<miniBox checkout>]

.DEFAULT_GOAL := all
include sources.mk

B := $(ROOT)/build/native
MBINCS := -Inative-shim -I$(MB)/source/guest/include -I$(MB)/extern/jsmn

SAM_CFLAGS := $(SAM_CFLAGS_COMMON) -w
CORE_CFLAGS := $(SAM_CFLAGS_COMMON) $(MBINCS) -I. -I$(ROOT)/build/munt-config -I$(MUNT) -Wall -Wno-unused-function
MUNT_CXXFLAGS := $(MUNT_CXXFLAGS_COMMON) -w

$(call flags_stamp,$(B),$(SAM_CFLAGS) | $(CORE_CFLAGS) | $(MUNT_CXXFLAGS))

SAM_OBJS := $(patsubst $(SAM)/%.c,$(B)/sam/%.o,$(SAM_SRCS))
MUNT_OBJS := $(patsubst $(MUNT)/%.cpp,$(B)/munt/%.o,$(MUNT_SRCS))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_NAMES)))

all: $(B)/run-native $(B)/run-wbx

$(B)/sam/%.o: $(SAM)/%.c $(PATCH_STAMP) $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(SAM_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(PATCH_STAMP) $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(CORE_CFLAGS) -c -o $@ $<

$(B)/munt/%.o: $(MUNT)/%.cpp $(MUNT_CONFIG) $(B)/flags
	@mkdir -p $(dir $@)
	g++ $(MUNT_CXXFLAGS) -c -o $@ $<

$(B)/core/run-native.o: run-native.c gate-harness.h samurai-driver.h $(B)/flags
	@mkdir -p $(dir $@)
	gcc -O2 -Wall -DGATE_NATIVE -I. -c -o $@ $<

$(B)/run-native: $(CORE_OBJS) $(SAM_OBJS) $(MUNT_OBJS) $(B)/core/run-native.o
	g++ -o $@ $^ $(WRAP_FLAGS) -lm

# run-wbx links the miniBox host library
MBHOST := $(MB)/build/meson-linux/source/host
$(B)/run-wbx: run-wbx.c gate-harness.h samurai-driver.h $(B)/flags
	gcc -O2 -Wall -I. -I$(MB)/source/host -o $@ run-wbx.c $(MBHOST)/libminiboxhost.so -Wl,-rpath,$(MBHOST)

clean:
	rm -rf $(B)

.PHONY: all clean
