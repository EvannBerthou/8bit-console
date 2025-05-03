#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "include/raylib.h"

#define ABORT(x) do { fprintf(stderr, "ABORT: %s:%d: PC=%x "x"\n", __FILE__, __LINE__, v->pc); exit(1); } while(0)

#define ASSERT(x) do { if (!(x)) {fprintf(stderr, "ASSERT: %s:%d Error reading\n", __FILE__, __LINE__); exit(1);} } while(0)


#define GPU_MEMORY (128 * 64)
#define REG_COUNT 8

#define OPCODE_MASK (uint8_t)0x1F
#define MODE_MASK   (uint8_t)0xF << 5

#define MAKE_INST(opcode, mode) ((opcode) & OPCODE_MASK) | (((mode) & MODE_MASK) << 5)

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

typedef enum {
    NOOP = 0,
    LDA = 1,
    SAM = 2,
    SAR = 3,
    JMP = 4,
    PSH = 5,
    POP = 6,
    CMP = 7,
    ADD = 8,
    AND = 9,
    OR = 10,
    NOT = 11,
    SHR = 12,
    SHL = 13,
} OPCODE;

// Background:
// (17 * 9) * 3 = 459 bytes
// IDX-X-Y
// Sprites:
// 40 * 4 = 160 bytes
// IDX-X-Y-FLAG?
// Total:
// 459 + 160 = 619 bytes

// Memory Mapping
// TODO: Define Macros for addresses
// 0x0000 - 0x3FFF -> Fixed Memory Bank (16Kb)
// 0x4000 - 0x7FFF -> Memory Bank from Bank Pointer (16Kb)
// 0x8000 - 0x80FF -> System I/O (256 bytes)
//   - 0x8000 -> Trigger GPU Refresh
//   - 0x8001 -> X GPU Scrolling
//   - 0x8002 -> Y GPU Scrolling
//   - 0x8003 -> ROM Bank Pointer
//   - 0x8004 -> Video Bank Pointer
//   - 0x8005 -> Input
// 0x8100 - 0xA0FF -> RAM (8Kb)
// 0xA100 - 0xD0FF -> Tile Map Bank (512 Tiles of 24 bytes each = 12Kb)
//  0xA100 - 0xB8FF -> Background tiles
//  0xB900 - 0xD0FF -> Sprites
// 0xD100 - 0xD36B -> GPU (619 bytes)
//  0xD100 - 0xD2CB -> Background tiles on 3 bytes encoding (idx, x scroll, y scroll)
//  0xD2CC - 0xD36B -> Background tiles on 4 bytes encoding (idx, x scroll, y scroll, flags)
// 0xD36C - 0xD1FF -> Nothing
// 0xD200 - 0xFFFF -> Stack (12Kb) //TODO: May be used by something else later

typedef struct {
    uint16_t entrypoint;
    uint8_t game_name[16];
    uint8_t rom_bank_count;
    uint8_t video_bank_count;
    uint8_t target_fps;
} game_header;

typedef struct {
    game_header header;
    uint8_t *content;
} cartdridge;

typedef struct {
    uint8_t system_io[0xFF];
    uint8_t ram[0x2000];
    uint8_t gpu_tiles[619];
    uint8_t stack[0x3000];

    uint8_t regs[REG_COUNT];
    uint16_t sp;
    uint16_t pc;
    uint8_t flags;

    uint8_t gpu_memory[GPU_MEMORY];
    uint16_t gpu_pointer;

    cartdridge *cart;
} vm;

uint8_t mem_read(vm *v, uint16_t addr);
void mem_write(vm *v, uint16_t addr, uint8_t value);

void dump(vm *v) {
    printf("PC=%d\n", v->pc);
    printf("SP TOP=%x\n", mem_read(v, v->sp+1));
    printf("FLAG=%x\n", v->flags);
    for (uint8_t i = 0; i < 20; i++) printf("%x ", mem_read(v, i));
    printf("\n");
    for (uint8_t i = 0; i < REG_COUNT; i++)  printf("%x ", v->regs[i]);
    printf("\n");
    for (uint8_t i = 0; i < REG_COUNT; i++)  printf("%d ", v->regs[i]);
    printf("\n");
}

void vm_init(vm *v) {
    for (uint16_t i = 0; i < 0x2000; i++)     v->ram[i] = 0;
    for (uint16_t i = 0; i < 0xFF; i++)       v->system_io[i] = 0;
    for (uint16_t i = 0; i < 0x3000; i++)     v->stack[i] = 0;
    for (uint16_t i = 0; i < GPU_MEMORY; i++) v->gpu_memory[i] = 0;
    for (uint16_t i = 0; i < 619; i++)        v->gpu_tiles[i] = 255;
    for (uint8_t i = 0; i < REG_COUNT; i++)   v->regs[i] = 0;
    v->pc = 0;
    v->sp = 0xFFFF;
}

void cart_load(cartdridge *cart, const char *binary) {
    FILE *f = fopen(binary, "rb");
    if (!f) {
        fprintf(stderr, "Can't open file");
        exit(1);
    }

    ASSERT(fread(&cart->header.entrypoint, sizeof(uint16_t), 1, f));
    ASSERT(fread(&cart->header.game_name, sizeof(uint8_t), 16, f));
    ASSERT(fread(&cart->header.rom_bank_count, sizeof(uint8_t), 1, f));
    ASSERT(fread(&cart->header.video_bank_count, sizeof(uint8_t), 1, f));
    ASSERT(fread(&cart->header.target_fps, sizeof(uint8_t), 1, f));

    uint32_t content_size = cart->header.rom_bank_count * (16 * 1024) 
                            + cart->header.video_bank_count * (12 * 1024);
    cart->content = malloc(sizeof(uint8_t) * content_size);
    ASSERT(cart->content != NULL);

    uint8_t buff[16];
    uint16_t ptr = 0;
    uint8_t n = 0;
    while ((n = fread(&buff, sizeof(uint8_t), 16, f))) {
        for (uint8_t i = 0; i < n; i++) {
            cart->content[ptr] = buff[i];
            ptr++;
        }
    }
    fclose(f);
}

//TODO: Bound checking
uint8_t mem_read(vm *v, uint16_t addr) {
    if (addr <= 0x3FFF) return v->cart->content[addr & 0x3FFF];
    //TODO: Based on bank
    if (addr <= 0x7FFF) {
        if (v->cart->header.rom_bank_count == 1) {
            ABORT("There is only one fixed bank.");
        }
        return v->cart->content[addr];
    }
    if (addr <= 0x80FF) return v->system_io[addr & 0xFF];
    if (addr <= 0xA0FF) return v->ram[addr - 0x8100];
    //TODO: Based on bank
    if (addr <= 0xD0FF) return v->cart->content[addr - 0xA100 + 0x4000];
    if (addr <= 0xD36B) return v->gpu_tiles[addr - 0xD100];
    if (addr <= 0xD1FF) ABORT("Unused memory mapping");
    //if (addr <= 0xFFFF) 
    return v->stack[addr - 0xD200];
}

//TODO: Bound checking
void mem_write(vm *v, uint16_t addr, uint8_t value) {
    if (addr <= 0x3FFF) { v->cart->content[addr & 0x3FFF] = value; return; }
    //TODO: Based on bank
    if (addr <= 0x7FFF) {
        if (v->cart->header.rom_bank_count == 1) {
            ABORT("There is only one fixed bank.");
        }
        v->cart->content[addr] = value;
        return;
    }
    if (addr <= 0x80FF) { v->system_io[addr & 0xFF] = value; return; }
    if (addr <= 0xA0FF) { v->ram[addr - 0x8100] = value; return; }
    if (addr <= 0xD0FF) ABORT("TODO: Should we be able to write directly Tile Map Bank?");
    if (addr <= 0xD36B) { v->gpu_tiles[addr - 0xD100] = value; return; }
    if (addr <= 0xD1FF) ABORT("Unused memory mapping");
    //if (addr <= 0xFFFF) 
    v->stack[addr - 0xD200] = value;
}

uint16_t advance_pc(vm *v) { v->pc++; return v->pc; }

void jump(vm *v, uint8_t mode) {
        uint16_t addr = mem_read(v, advance_pc(v));
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
        case 0: return mem_read(v, advance_pc(v));
        // Memory read
        case 1: {
            uint16_t addr = mem_read(v, advance_pc(v));
            return mem_read(v, addr);
        }
        // Reg
        case 2: {
            uint8_t value = mem_read(v, advance_pc(v));
            if (value >= REG_COUNT) {
                ABORT("Unknown register");
            }
            return v->regs[value];
        }
        // Fetch 16 bits immediate
        case 3: {
            uint8_t high = mem_read(v, advance_pc(v));
            uint8_t low  = mem_read(v, advance_pc(v));
            return high << 8 | low;
        }
        // Fetch 16 bits from memory
        case 4: {
            uint8_t high = mem_read(v, advance_pc(v));
            uint8_t low  = mem_read(v, advance_pc(v));
            uint16_t addr = high << 8 | low;
            return mem_read(v, addr);
        }
        // Fetch 16 bits from base and offset from reg
        case 5: {
            uint8_t high = v->regs[mem_read(v, advance_pc(v))];
            uint8_t low  = v->regs[mem_read(v, advance_pc(v))];
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
// Mode: 0-7
// Code: 0-31

void vm_exec_opcode(vm *v) {
    uint8_t value = mem_read(v, v->pc);

    // Halt
    if (value == 0xFF) {
        printf("HALT\n");
        dump(v);
        getc(stdin);
        return;
    }

    uint8_t opcode = value & OPCODE_MASK;
    uint8_t mode   = (value & MODE_MASK) >> 5;

    switch (opcode) {
        // Memory Instructions
        case NOOP: break;
        case LDA: v->regs[0] = fetch_operand(v, mode); break;
        case SAM: mem_write(v, fetch_operand(v, mode), v->regs[0]); break;
        case SAR: v->regs[fetch_operand(v, mode)] = v->regs[0]; break;
        case JMP: jump(v, mode); break;
        case PSH: mem_write(v, v->sp, fetch_operand(v, mode)); v->sp--; break;
        case POP: {
            v->sp++; 
            if (mode == 7) { // PC
                v->pc = mem_read(v, v->sp);
            }
            else {
                v->regs[fetch_operand(v, mode)] = mem_read(v, v->sp); 
            }
            break;
        }
        case CMP: {
             uint8_t operand = fetch_operand(v, mode);
             v->flags = (v->flags & ~FLAG(Z)) | (v->regs[0] == operand);
             break; 
        }
        // Math Instructions
        case ADD: {
            uint16_t operand = fetch_operand(v, mode);
            SETFLAG(v, FLAG(C), HASCARRY(v->regs[0], operand));
            SETFLAG(v, FLAG(C), ISZERO(v->regs[0]));
            v->regs[0] = (v->regs[0] + operand) & 0xFF;
            SETFLAG(v, FLAG(Z), ISZERO(v->regs[0]));
            SETFLAG(v, FLAG(N), ISNEG(v->regs[0]));
            break;
        }
        case AND: {
            v->regs[0] &= fetch_operand(v, mode); 
            SETFLAG(v, FLAG(Z), ISZERO(v->regs[0]));
            SETFLAG(v, FLAG(N), ISNEG(v->regs[0]));
            break;
        }
        case OR: {
            v->regs[0] |= fetch_operand(v, mode); 
            SETFLAG(v, FLAG(Z), ISZERO(v->regs[0]));
            SETFLAG(v, FLAG(N), ISNEG(v->regs[0]));
            break;
        }
        case NOT: {
            v->regs[0] = ~v->regs[0]; 
            SETFLAG(v, FLAG(Z), ISZERO(v->regs[0]));
            SETFLAG(v, FLAG(N), ISNEG(v->regs[0]));
            break;
        }
        case SHR: {
            v->regs[0] = (v->regs[0] >> fetch_operand(v, mode)); 
            SETFLAG(v, FLAG(Z), ISZERO(v->regs[0]));
            SETFLAG(v, FLAG(N), ISNEG(v->regs[0]));
            break;
        }
        case SHL: {
            v->regs[0] = (v ->regs[0] << fetch_operand(v, mode)); 
            SETFLAG(v, FLAG(Z), ISZERO(v->regs[0]));
            SETFLAG(v, FLAG(N), ISNEG(v->regs[0]));
            break;
        }
        // Unknown
        default: ABORT("Unkown upcode"); break;
    }
}

Color colors[] = {
    {0x1D, 0x1D, 0x1D, 0xFF},
    {0xFF, 0xFF, 0xFF, 0xFF},
    {0xF5, 0xE9, 0xBE, 0xFF},
    {0x9A, 0x6A, 0xCB, 0xFF},
    {0x4A, 0x90, 0xB8, 0xFF},
    {0x5C, 0xAD, 0x4A, 0xFF},
    {0xB8, 0x4A, 0x4A, 0xFF},
    {0x7D, 0x7D, 0x7D, 0xFF}
};

const int COLOR_COUNT = sizeof(colors) / sizeof(colors[0]);

void render_game(vm *v) {
    // Render background
    uint8_t x_scrolling = mem_read(v, 0x8001) % 8;
    uint8_t y_scrolling = mem_read(v, 0x8002) / 8;
    
    for (int i = 0; i < (17 * 9) * 3; i += 3) {
        uint8_t tile_index = mem_read(v, 0xD100 + i);
        uint8_t x = mem_read(v, 0xD100 + i + 1);
        uint8_t y = mem_read(v, 0xD100 + i + 2);

        uint16_t tile_addr = 0xA100 + tile_index * 24;

        size_t total_bits = 24 * 8;
        size_t num_windows = total_bits / 3;

        for (size_t i = 0; i < num_windows; i++) {
            uint16_t bit_index = i * 3;
            size_t byte_index = bit_index / 8;
            size_t bit_offset = bit_index % 8;

            uint16_t combined = 0;
            if (byte_index < 24) {
                combined |= mem_read(v, tile_addr + byte_index); 
            }
            if (byte_index + 1 < 24) {
                combined |= ((uint16_t)mem_read(v, tile_addr + byte_index + 1)) << 8;
            }

            uint8_t pixel =  (combined >> bit_offset) & 0x07;
            uint8_t offset_x = i % 8;
            uint8_t offset_y = i / 8;

            uint16_t final_x = x * 8 + offset_x - x_scrolling;
            uint16_t final_y = y * 8 + offset_y - y_scrolling;
            if (final_x >= 128 || final_y >= 64) {
                continue;
            }
            uint16_t gpu_addr = final_x + y * 128 * 8 + (offset_y - y_scrolling) * 128;
            v->gpu_memory[gpu_addr] = pixel;
        }
    }

    for (uint32_t i = 0; i < GPU_MEMORY; i++) {
        int x = (i % 128);
        int y = (i / 128);
        uint8_t value = v->gpu_memory[i];
        DrawRectangle(x * 8, y * 8, 8, 8, colors[value % COLOR_COUNT]);
    }
}

// Inputs are encoding into a single byte
// 0 -> A Button
// 1 -> B Button
// 2 -> Up Pad
// 3 -> Right Pad
// 4 -> Down Pad
// 5 -> Left Pad
// 6 -> Select
// 7 -> Start
void refresh_input(vm *v) {
    uint8_t key_press = 0
        | IsKeyDown(KEY_J) << 0
        | IsKeyDown(KEY_K) << 1
        | IsKeyDown(KEY_W) << 2
        | IsKeyDown(KEY_D) << 3
        | IsKeyDown(KEY_S) << 4
        | IsKeyDown(KEY_A) << 5
        | IsKeyDown(KEY_E) << 7
        | IsKeyDown(KEY_R) << 7;
    mem_write(v, 0x8005, key_press);
}

void vm_run(vm *v) {
    while (!WindowShouldClose()) {
        vm_exec_opcode(v);
        advance_pc(v);
        if (mem_read(v, 0x8000) == 1) {
            mem_write(v, 0x8000, 0);
            for (uint16_t i = 0; i < GPU_MEMORY; i++) v->gpu_memory[i] = 0;
            BeginDrawing();
                ClearBackground(BLACK);
                render_game(v);
                refresh_input(v);
            EndDrawing();
        }
    }
}

int main() {
    InitWindow(1024, 512, "8bit-console");
    SetTargetFPS(60);
    vm v = {};
    cartdridge c = {};
    v.cart = &c;
    vm_init(&v);
    cart_load(&c, "refresh.bin");
    vm_run(&v);
    return 0;
}
