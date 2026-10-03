# Compilation du chip MAX30102 en WebAssembly (d'apres le depot d'exemple "inverter-chip" de Wokwi)
SOURCES = max30102.chip.c
TARGET  = max30102.chip.wasm

all: $(TARGET)

$(TARGET): $(SOURCES) wokwi-api.h
	clang --target=wasm32-unknown-wasi --sysroot /opt/wasi-libc -nostartfiles \
	  -Wl,--import-memory -Wl,--export-table -Wl,--no-entry -Werror -o $(TARGET) $(SOURCES)
