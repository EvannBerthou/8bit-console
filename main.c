#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <stdbool.h>
#include "include/raylib.h"

#define ABORT(x) do { fprintf(stderr, "ABORT: %s:%d: PC=%x "x"\n", __FILE__, __LINE__, v->pc); exit(1); } while(0)

#define CPU_MEMORY ((1 << 16) - 1) 
#define GPU_MEMORY (128 * 64)
#define REG_COUNT 8

#define OPCODE_MASK (uint8_t)0xF
#define MODE_MASK   (uint8_t)0xF << 4

#define MAKE_INST(opcode, mode) ((opcode) & OPCODE_MASK) | (((mode) & MODE_MASK) << 4)

#define BASE_STACK_ADDR CPU_MEMORY

#define FLAG_Z_OFFSET 0
#define FLAG_C_OFFSET 1 
#define FLAG_N_OFFSET 2

#define FLAG(f) (1 << FLAG_##f##_OFFSET)

#define ZERO(c)  (((c) & FLAG(Z)) >> 0)
#define CARRY(c) ((c) & FLAG(C)) >> 1
#define NEG(c)   (((c) & FLAG(Z)) >> 2)

#define HASCARRY(n, o)  (((n) + (o)) > 0xFF)
#define ISZERO(n)               (((n) == 0))
#define ISNEG(n)                ((n & (1 << 7)) >> 7)

#define SETFLAG(vm, f, x) do { vm->flags = (vm->flags & ~(f)) | x << f; } while(0)

// Background:
// (17 * 9) * 3 = 459 bytes
// IDX-X-Y
// Sprites:
// 40 * 4 = 160 bytes
// IDX-X-Y-FLAG?
// Total:
// 459 + 160 = 619 bytes

// Memory Mapping
// 0x0000 - 0x3FFF -> Fixed Memory Bank (16Kb)
// 0x4000 - 0x7FFF -> Memory Bank from Bank Pointer (16Kb)
// 0x8000 - 0x80FF -> System I/O (256 bytes)
//   - 0x8000 -> Trigger GPU Refresh
// 0x8100 - 0xA0FF -> RAM (8Kb)
// 0xA100 - 0xD0FF -> Tile Map Bank (512 Tiles of 24 bytes each = 12Kb)
// 0xD100 - 0xD36B -> GPU (619 bytes)
// 0xD200 - 0xFFFF -> TODO (12Kb restant)

typedef struct {
    uint8_t memory[CPU_MEMORY];
    uint8_t regs[REG_COUNT];
    uint16_t sp;
    uint16_t pc;
    uint8_t flags;

    uint8_t gpu_memory[GPU_MEMORY];
    uint16_t gpu_pointer;
} vm;

typedef struct {
    uint16_t entrypoint;
    uint8_t game_name[16];
    uint8_t rom_bank_count;
    uint8_t video_bank_count;
} game_header;

typedef struct {
    game_header header;
} cartdridge;

void dump(vm *v) {
    printf("PC=%d\n", v->pc);
    printf("SP TOP=%x\n", v->memory[v->sp+1]);
    printf("FLAG=%x\n", v->flags);
    for (uint8_t i = 0; i < 20; i++) printf("%x ", v->memory[i]);
    printf("\n");
    for (uint8_t i = 0; i < 20; i++) printf("%c ", isprint(v->memory[i]) ? v->memory[i] : ' ');
    printf("\n");
    for (uint8_t i = 0; i < REG_COUNT; i++)  printf("%x ", v->regs[i]);
    printf("\n");
    for (uint8_t i = 0; i < REG_COUNT; i++)  printf("%d ", v->regs[i]);
    printf("\n");
}

void load_tile_map(vm *v) {
    v->memory[0xA100 + 0] = 0xDB;
    v->memory[0xA100 + 1] = 0xB6;
    v->memory[0xA100 + 2] = 0x6D;
    for (int i = 0; i < 6 * 3; i += 3) {
        v->memory[0xA100 + 3 + i] = 0x03;
        v->memory[0xA100 + 4 + i] = rand();
        v->memory[0xA100 + 5 + i] = 0x60;
    }
    v->memory[0xA100 + 21] = 0xDB;
    v->memory[0xA100 + 22] = 0xB6;
    v->memory[0xA100 + 23] = 0x6D;


    for (uint16_t i = 0; i < 17 * 9; i++) {
        v->memory[0xD100 + i * 3 + 0] = 0;
        v->memory[0xD100 + i * 3 + 1] = i % 17;
        v->memory[0xD100 + i * 3 + 2] = i / 17;
    }
}

void vm_init(vm *v) {
    for (uint16_t i = 0; i < CPU_MEMORY; i++) v->memory[i] = 0;
    for (uint16_t i = 0; i < GPU_MEMORY; i++) v->gpu_memory[i] = 0;
    for (uint8_t i = 0; i < REG_COUNT; i++)   v->regs[i] = 0;
    v->pc = 0;
    v->sp = BASE_STACK_ADDR - 1;
}

void vm_load(vm *v, const char *binary) {
    FILE *f = fopen(binary, "rb");
    if (!f) ABORT("Can't open file");
    uint8_t buff[16];
    uint16_t ptr = 0;
    uint8_t n = 0;
    while ((n = fread(&buff, sizeof(uint8_t), 16, f))) {
        for (uint8_t i = 0; i < n; i++) {
            v->memory[ptr] = buff[i];
            ptr++;
        }
    }
    fclose(f);
    load_tile_map(v);
}

uint16_t advance_pc(vm *v) { v->pc++; return v->pc; }

void jump(vm *v, uint8_t mode) {
        uint16_t addr = v->memory[advance_pc(v)];
        switch (mode) {
            // Always
            case 0: v->pc = addr - 1; break;
            // Equal
            case 1: if(v->flags & FLAG(Z))   { v->pc = addr - 1; } break;
            // Not Equal
            case 2: if(!(v->flags & FLAG(Z))) { v->pc = addr - 1; } break;
            default: printf("%d: ", mode); ABORT("Unknown jump mode");
        }
}

uint16_t fetch_operand(vm *v, uint8_t mode) { 
    switch (mode) {
        // Immediate
        case 0: return v->memory[advance_pc(v)];
        // Memory read
        case 1: {
            uint16_t addr = v->memory[advance_pc(v)];
            return v->memory[addr];
        }
        // Reg
        case 2: {
            if (v->memory[advance_pc(v)] >= REG_COUNT) {
                ABORT("Unknown register");
            }
            return v->regs[v->memory[v->pc]];
        }
        // Fetch 16 bits immediate
        case 3: {
            uint8_t high = v->memory[advance_pc(v)];
            uint8_t low  = v->memory[advance_pc(v)];
            return high << 8 | low;
        }
        // Fetch 16 bits from memory
        case 4: {
            uint8_t high = v->memory[advance_pc(v)];
            uint8_t low  = v->memory[advance_pc(v)];
            uint16_t addr = high << 8 | low;
            return v->memory[addr];
        }
        // Fetch 16 bits from base and offset from reg
        case 5: {
            uint8_t high = v->regs[v->memory[advance_pc(v)]];
            uint8_t low  = v->regs[v->memory[advance_pc(v)]];
            return (high << 8) | low;
        }
        // Retrieve carry
        case 6: return CARRY(v->flags);
        // PC
        case 7: return v->pc;
        // Unknown mode
        default: printf("%x\n", mode); ABORT("Unknown mode"); break;
    }
}

// 0000  0000
// mode  code
// Mode: 0-16
// Code: 0-16

void vm_exec_opcode(vm *v) {
    uint8_t value = v->memory[v->pc];

    // Halt
    if (value == 0xFF) {
        printf("HALT\n");
        dump(v);
        getc(stdin);
        return;
    }

    uint8_t opcode = value & OPCODE_MASK;
    uint8_t mode   = (value & MODE_MASK) >> 4;

    switch (opcode) {
        // MEMORY Instructions
        // No-op
        case 0: break;
        // Load acc
        case 1: v->regs[0] = fetch_operand(v, mode); break;
        // Store acc to mem
        case 2: v->memory[fetch_operand(v, mode)] = v->regs[0]; break;
        // Store acc to reg
        case 3: v->regs[fetch_operand(v, mode)] = v->regs[0]; break;
        // Jump
        case 4: jump(v, mode); break;
        // Stack push
        case 5: v->memory[v->sp] = fetch_operand(v, mode); v->sp--; break;
        // Stack pop into reg
        case 6: {
            v->sp++; 
            if (mode == 7) { // PC
                v->pc = v->memory[v->sp];
            }
            else {
                v->regs[fetch_operand(v, mode)] = v->memory[v->sp]; 
            }
            break;
        }
        // Compare
        case 7: {
             uint8_t operand = fetch_operand(v, mode);
             v->flags = (v->flags & ~FLAG(Z)) | (v->regs[0] == operand);
             break; 
        }
        // Math Instructions
        // 8 Add
        case 8: {
            uint16_t operand = fetch_operand(v, mode);
            SETFLAG(v, FLAG(C), HASCARRY(v->regs[0], operand));
            SETFLAG(v, FLAG(C), ISZERO(v->regs[0]));
            v->regs[0] = (v->regs[0] + operand) & 0xFF;
            SETFLAG(v, FLAG(Z), ISZERO(v->regs[0]));
            SETFLAG(v, FLAG(N), ISNEG(v->regs[0]));
            break;
        }
        // 9 AND
        case 9: {
            v->regs[0] &= fetch_operand(v, mode); 
            SETFLAG(v, FLAG(Z), ISZERO(v->regs[0]));
            SETFLAG(v, FLAG(N), ISNEG(v->regs[0]));
            break;
        }
        // 10 OR
        case 10: {
            v->regs[0] |= fetch_operand(v, mode); 
            SETFLAG(v, FLAG(Z), ISZERO(v->regs[0]));
            SETFLAG(v, FLAG(N), ISNEG(v->regs[0]));
            break;
        }
        // 11 NOT
        case 11: {
            v->regs[0] = ~v->regs[0]; 
            SETFLAG(v, FLAG(Z), ISZERO(v->regs[0]));
            SETFLAG(v, FLAG(N), ISNEG(v->regs[0]));
            break;
        }
        // 12 Shift right
        case 12: {
            v->regs[0] = (v->regs[0] >> fetch_operand(v, mode)); 
            SETFLAG(v, FLAG(Z), ISZERO(v->regs[0]));
            SETFLAG(v, FLAG(N), ISNEG(v->regs[0]));
            break;
        }
        // 13 Shift left
        case 13: {
            v->regs[0] = (v ->regs[0] << fetch_operand(v, mode)); 
            SETFLAG(v, FLAG(Z), ISZERO(v->regs[0]));
            SETFLAG(v, FLAG(N), ISNEG(v->regs[0]));
            break;
        }
        // GPU Instructions
        // Set pixel color at pointeur position
        case 14: v->gpu_memory[v->gpu_pointer] = fetch_operand(v, mode); break;
        // Set GPU Pointer
        case 15: v->gpu_pointer = fetch_operand(v, mode); break;
        // Unknown
        default: ABORT("Unkown upcode"); break;
    }
}

Color colors[] = {
    BLACK,
    WHITE,
    YELLOW,
    PURPLE,
    SKYBLUE,
    GREEN,
    RED,
    BLUE,
};

const int COLOR_COUNT = sizeof(colors) / sizeof(colors[0]);

void render_game(vm *v) {
    // Render background
    for (int i = 0; i < (17 * 9) * 3; i += 3) {
        uint8_t tile_index = v->memory[0xD100 + i];
        uint8_t x = v->memory[0xD100 + i + 1];
        uint8_t y = v->memory[0xD100 + i + 2];

        uint8_t *tile = &v->memory[0xA100 + tile_index * 24];

        size_t total_bits = 24 * 8;
        size_t num_windows = total_bits / 3;

        for (size_t i = 0; i < num_windows; i++) {
            uint16_t bit_index = i * 3;
            size_t byte_index = bit_index / 8;
            size_t bit_offset = bit_index % 8;

            // Combine two bytes to ensure enough bits across boundaries
            uint16_t combined = 0;
            if (byte_index < 24) {
                combined |= tile[byte_index];
            }
            if (byte_index + 1 < 24) {
                combined |= ((uint16_t)tile[byte_index + 1]) << 8;
            }

            uint8_t pixel =  (combined >> bit_offset) & 0x07;
            uint8_t offset_x = i % 8;
            uint8_t offset_y = i / 8;
            if (x >= 16 || y >= 8) {
                continue;
            }
            uint16_t gpu_addr = (x * 8 + offset_x) + ((y * 128 * 8) + (offset_y * 128));
            v->gpu_memory[gpu_addr] = pixel;
        }
    }

    for (uint32_t i = 0; i < GPU_MEMORY; i++) {
        int x = (i % 128);
        int y = (i / 128);
        uint8_t value = v->gpu_memory[i];
        DrawRectangle(x * 8, y * 8, 8, 8, colors[value % COLOR_COUNT]);
    }
    //exit(0);
}

void vm_run(vm *v) {
    while (!WindowShouldClose()) {
        vm_exec_opcode(v);
        advance_pc(v);
        if (v->memory[0x8000] == 1) {
            v->memory[0x8000] = 0;
            BeginDrawing();
                ClearBackground(BLACK);
                render_game(v);
            EndDrawing();
        }
    }
}

int main() {
    InitWindow(1024, 512, "OK");
    vm v = {};
    vm_init(&v);
    vm_load(&v, "refresh.bin");
    vm_run(&v);
    return 0;
}
