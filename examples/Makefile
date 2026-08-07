# override, so that "make CFLAGS=-O2" adds to these rather than dropping the
# standard and the warnings the CI gate depends on.
override CFLAGS += -std=c11 -Wall -Wextra
LDLIBS := -pthread -lm

# The ABA example claims a job with a 16-byte compare-and-swap. On x86-64 that
# is cmpxchg16b, which -mcx16 enables; the flag does not exist elsewhere. Ask
# the compiler rather than uname, so a cross build tests the target it is
# actually producing code for instead of the machine running make.
ABA_MACROS := $(shell $(CC) $(CFLAGS) $(CPPFLAGS) -dM -E -x c /dev/null 2>/dev/null)
ifeq ($(ABA_MACROS),)
    # Say so rather than quietly building without cmpxchg16b, which turns into
    # an undefined reference to __atomic_compare_exchange_16 at link time.
    $(warning cannot query $(CC) for its target; building without -mcx16)
endif
ABA_CFLAGS := $(if $(findstring __x86_64__,$(ABA_MACROS)),-mcx16)

# -mcx16 does not make libatomic unnecessary: some GCC versions still route
# 16-byte atomic load and store through it, which is the link failure the
# manuscript footnotes. Other toolchains inline everything and ship no
# libatomic at all, so probe rather than guess. The probe carries the same
# flags as the ABA build, otherwise it answers for the wrong target.
ABA_LDLIBS := $(shell printf 'int main(void){return 0;}' | \
                $(CC) $(CFLAGS) $(CPPFLAGS) $(ABA_CFLAGS) $(LDFLAGS) -x c - \
                $(LDLIBS) -latomic -o /dev/null 2>/dev/null && echo -latomic)

BINS := rmw_example rmw_example_aba simple_aba_example

# make compares timestamps, so "make CFLAGS=-O2" against an up-to-date tree
# would rebuild nothing and check would then assert on binaries built with
# other flags. Depend on a stamp whose name encodes the toolchain: change any
# of it and the stamp simply does not exist, which make repairs before the
# binaries in the same run. A stamp with a fixed name and rewritten contents
# does not work here, because make 3.81 stats prerequisites before running
# their recipes and would apply the change one invocation late.
#
# The compiler's own version banner is in the signature because upgrading the
# toolchain behind an unchanged CC would otherwise leave check validating
# binaries the previous compiler produced.
CC_ID := $(shell $(CC) --version 2>/dev/null | head -1)
TOOLCHAIN := $(CC)|$(CC_ID)|$(CFLAGS)|$(CPPFLAGS)|$(LDFLAGS)|$(LDLIBS)|$(ABA_CFLAGS)|$(ABA_LDLIBS)

# A flag value may contain a single quote, which would otherwise close the
# quoting below and hand the shell something other than the signature. Escape
# it the usual way before interpolating.
sq := '
TOOLCHAIN_SH := $(subst $(sq),$(sq)\$(sq)$(sq),$(TOOLCHAIN))
STAMP := .toolchain-$(shell printf '%s' '$(TOOLCHAIN_SH)' | cksum | cut -d' ' -f1)

all: $(BINS)

$(STAMP):
	@rm -f .toolchain*
	@touch $@

rmw_example_aba: rmw_example_aba.c $(STAMP)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(ABA_CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS) $(ABA_LDLIBS)

%: %.c $(STAMP)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(BINS) .toolchain*

# The manuscript quotes this output verbatim, so gate on the text and not
# just the exit status: otherwise the book and its own programs can drift
# apart with CI staying green.
PI_LINE := PI calculated with 100 terms: 3.141592653589793
ABA_LINES := A: v = 42/B: v = 47/B: v = 42/A: v = 52/

check: all
	@for p in rmw_example rmw_example_aba; do \
	    out=$$(./$$p) || exit 1; \
	    [ "$$out" = "$(PI_LINE)" ] || { \
	        echo "$$p: expected '$(PI_LINE)', got '$$out'"; exit 1; }; \
	    echo "$$p: $$out"; \
	done
	@raw=$$(./simple_aba_example) || exit 1; \
	out=$$(printf '%s\n' "$$raw" | tr '\n' '/'); \
	[ "$$out" = "$(ABA_LINES)" ] || { \
	    echo "simple_aba_example: expected '$(ABA_LINES)', got '$$out'"; exit 1; }; \
	echo "simple_aba_example: $$out"

# Pinned to match .ci/check-format.sh: clang-format releases disagree about
# this style, and these files are printed verbatim in the book, so a reformat
# under a different version would churn the typeset listings.
CLANG_FORMAT ?= clang-format-20

format:
	$(CLANG_FORMAT) -i --style=file *.c

.PHONY: all clean check format
