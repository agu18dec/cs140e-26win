# Makefile for 00-hello: a simple example showing how to link against <libpi.a>
#
# This is similar to 2-gpio/code/Makefile but adds:
#  1. the ability to grab <libpi.a> and link against it
#  2. automatically rebuild <libpi> if there were changes.
#  3. automatically run the binary after building
#
# Read top to bottom; every variable is used later in a rule.

# 1) What are we building?
# The program for this directory: just <hello.c>
PROG_SRC = hello.c

# Make `make` less chatty.
MAKEFLAGS += --no-print-directory
# MAKEFLAGS += --warn-undefined-variables

#############################################################
# 2) Get the standard definitions for different make variables.
#
# First, check that <CS140E_2026_PATH> is set (should point to the
# root of your cs140e-26win repo).

ifndef CS140E_2026_PATH
$(error CS140E_2026_PATH is not set: this should contain the absolute path to where this directory is.  Define it in your shell's initialiation.  For example, .tcshrc for tcsh or .bashrc for bash)
endif

# Now include the standard definitions from <libpi/defs.mk>
# This defines: <LPI> (path to <libpi.a>), <MEMMAP>, <START>, <CFLAGS>,
# and all the cross-compilation tools (<CC>, <LD>, <OCP>, <OD>, etc.)
include $(CS140E_2026_PATH)/libpi/defs.mk
#############################################################

# 3) Dependencies: recompile everything if any of these change.
# <LPP> is defined above by <defs.mk> and points to the libpi directory.
DEPS := ./Makefile  $(LPP)/include/rpi.h

# 4) Object files.
# We compile <libpi/staff-start.S> into the current directory
# (see the rule at the end of the makefile for how).
objs := staff-start.o

# 5) The stripped binaries we make: these can be run using the bootloader.
# For each X.c in <PROG_SRC>, we build X.bin
progs   := $(PROG_SRC:.c=.bin)

# 6) Fake targets for each binary used to automatically run.
# For each X.c in <PROG_SRC>, we make a fake target X.run
run_all    := $(PROG_SRC:.c=.run)

# 7) Disable built-in suffix rules (we define everything explicitly).
.SUFFIXES:

#####################################################################
# Below are the recipes to build the binaries (and libpi).
# 

# 8) The default target: build all <.bin> files and all <.run> targets.
all:  $(progs) $(run_all)

# 9) The rule for X.run: depends on X.bin and runs pi-install.
# Each fake target foo.run depends on foo.bin
#   - "$*" is the shared name: for target <hello.run>, "$* == hello"
$(run_all): %.run: %.bin
	pi-install ./$*.bin

# 10) Always attempt to remake <libpi.a> (let its Makefile decide
# if it needs rebuilding).
# <LPI> is the path to <libpi.a> (defined above by <defs.mk>)
# <LPP> is the path to the libpi directory (defined above by <defs.mk>)
$(LPI): .ALWAYS
	@make -C $(LPP) $(LPI)

# <.ALWAYS> is a phony target that never exists, so anything
# depending on it will always be rebuilt (forces the libpi check)
.ALWAYS:

# 11) The rule for building a <.bin>.  We incidentally also
# produce a <.elf> and <.list> (disassembled elf).
#
# To cut down on rules we have one pattern rule to generate:
#   - <.elf>:  the linked program
#   - <.list>: disassembled version of <.elf> so we can see the machine code
#   - <.bin>:  stripped binary, this is what pi-install runs
# Recall:
#  - "$*" is the shared name: for target hello.bin ("$* == hello")
#  - "$<" is the first dependency ("hello.o")
#  - <$(LPI)> is <libpi.a> (pulled in at link time)
$(progs): %.bin: %.o $(objs)  $(DEPS)  $(MEMMAP)  $(LPI)
	$(LD) $(LDFLAGS) $(objs) $< -T $(MEMMAP) -o $*.elf  $(LPI)
	$(OD) -D  $*.elf > $*.list
	$(OCP) $*.elf -O binary $@

# 12) The rule for building a <.o> from a <.c> C file.
# The rule states that every <.o> will get remade if its
# <.c> (same name) or any of the <DEPS> changes (is newer than the <.o>)
%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

# 13) Special rule for staff-start.o
# We hardcode this to compile <libpi/staff-start.S> into the
# current directory to make things easier.
# <START> is defined above by <defs.mk> and points to staff-start.o
# Note:
#  - $< is the <.S> file (<libpi/staff-start.S>)
#  - <CPP_ASFLAGS> allows us to use C preprocessor macros in assembly
#    (defined by <defs.mk>)
staff-start.o: $(START:.o=.S) $(DEPS)
	$(CC) -c $(CPP_ASFLAGS) $< -o $@

# 14) "make clean": Delete all generated files (not sources).
clean:
	rm -f *.bin *.list *.elf *.o Makefile.bak *~

# 15) tell `make` these are fake targets (do not generate a file)
# to reduce some confusing errors.
.PHONY: all clean $(run_all) .ALWAYS

# 16) Keep all <.o> files around (don't delete as intermediate files).
.PRECIOUS: %.o
