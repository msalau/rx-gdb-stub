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

GUIDEBUGGER=ddd
GUIDEBUGGERFLAGS=--debugger $(DEBUGGER)

#	-Wredundant-decls \
#	-Wshadow \

CFLAGS=\
	-Ibsp \
	-Os \
	-g2 \
	-Wall \
	-Wextra \
	-Wnested-externs \
	-Wpointer-arith \
	-Wswitch \
	-Wreturn-type \
	-Wstrict-prototypes \
	-Wunused \
	-Wno-main \
	\
	-Wno-strict-aliasing \
	-Wno-unused-but-set-variable \
	-Wno-unused-parameter \
	-Wno-missing-field-initializers \
	-Wno-sign-compare \
	\
	-MMD \
	-mlittle-endian-data \
	-mint-register=0 \
	-ffunction-sections \
	-fdata-sections \
	-std=gnu99 \
	$(END)

ASFLAGS=\
	$(END)

LDFLAGS=\
	-nostartfiles \
	-Wl,--gc-sections \
	-Wl,-Map=$(PROJECT_MAP) \
	-T bsp/RX62N8.ld \
	$(END)

SRC=\
	bsp/isr_vectors.c \
	rx-gdb-stub.o \
	test.o \
	$(END)

OBJ=$(SRC:.c=.o) bsp/crt0.o
DEP=$(OBJ:.o=.d)

all: $(PROJECT_LST) $(PROJECT)

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
