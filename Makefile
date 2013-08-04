PROJECT=test.elf
PROJECT_MAP=$(PROJECT:.elf=.map)
PROJECT_LST=$(PROJECT:.elf=.lst)
PROJECT_HEX=$(PROJECT:.elf=.hex)

CC=rx-elf-gcc
AS=rx-elf-gcc
LD=rx-elf-gcc
SIZE=rx-elf-size
OBJDUMP=rx-elf-objdump
OBJCOPY=rx-elf-objcopy
DEBUGGER=rx-elf-gdb
FLASH_TOOL=rxusb
LINT=splint

GUIDEBUGGER=ddd
GUIDEBUGGERFLAGS=--debugger $(DEBUGGER)

INCLUDE=\
	-I. \
	-Ibsp \
	$(END)

CFLAGS=\
	$(INCLUDE) \
	-Os \
	-g2 \
	-Wall \
	-Wextra \
	-Wdouble-promotion \
	-Wnested-externs \
	-Wpointer-arith \
	-Wswitch \
	-Wreturn-type \
	-Wstrict-prototypes \
	-Wunused \
	-Wno-main \
	-Wcast-qual \
	-Wcast-align \
	-Wwrite-strings \
	-Wshadow \
	-Wmissing-declarations \
	-Wmissing-prototypes \
	-Wredundant-decls \
	-Wsuggest-attribute=const \
	-Wsuggest-attribute=pure \
	-Wsuggest-attribute=noreturn \
	\
	-Wno-unused-parameter \
	\
	-MMD \
	-mlittle-endian-data \
	-mint-register=0 \
	-ffunction-sections \
	-fdata-sections \
	-std=gnu89 \
	$(END)

ASFLAGS=\
	-MMD \
	$(END)

LDFLAGS=\
	-Wl,--gc-sections \
	-Wl,-Map=$(PROJECT_MAP) \
	-T bsp/RX62N8.ld \
	$(END)

CPATH=`$(CC) -print-file-name=include`

LINTFLAGS=\
	-linelen 10000 \
	+quiet \
	+stats \
	+show-summary \
	$(INCLUDE) \
	+boolint \
	+charint \
	+slashslashcomment \
	+declundef \
	+relax-types \
	+nullptrarith \
	+sizeoftype \
	+looploopbreak \
	+loopswitchbreak \
	+evalorderuncon \
	+bitwisesigned \
	+matchanyintegral \
	-D'__RX_LITTLE_ENDIAN__' \
	-D'__volatile__=' \
	-D'__asm__(a)=' \
	$(END)

SRC=\
	bsp/isr_vectors.c \
	rx-gdb-stub.c \
	test.c \
	$(END)

OBJ=$(SRC:.c=.o)
DEP=$(OBJ:.o=.d)

all: $(PROJECT_LST) $(PROJECT)

check: $(SRC)
	@echo -e "\tLINT\t*.c"
	@CPATH=$(CPATH) $(LINT) $(LINTFLAGS) $^

$(PROJECT): $(OBJ)
	@echo -e "\tLD\t"$@
	@$(LD) $(LDFLAGS) -o $@ $^
	@echo -e "\tSIZE\t"$@
	@$(SIZE) $@

%.o: %.c
	@echo -e "\tCC\t"$@
	@$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	@echo -e "\tAS\t"$@
	@$(AS) $(ASFLAGS) -c -o $@ $<

%.lst: %.elf
	@echo -e "\tOBJDUMP\t"$@
	@$(OBJDUMP) -DS $^ > $@

%.lst: %.o
	@echo -e "\tOBJDUMP\t"$@
	@$(OBJDUMP) -DS $^ > $@

%.hex: %.elf
	@echo -e "\tOBJCOPY\t"$@
	@$(OBJCOPY) -Oihex $^ $@

flash: $(PROJECT)
	@$(FLASH_TOOL) -v $<

debug: $(PROJECT)
	@$(DEBUGGER) $<

guidebug: $(PROJECT)
	$(GUIDEBUGGER) $(GUIDEBUGGERFLAGS) $<

clean:
	@rm -f $(OBJ) $(DEP) $(PROJECT) $(PROJECT_MAP) $(PROJECT_LST) $(PROJECT_HEX)

-include $(DEP)
