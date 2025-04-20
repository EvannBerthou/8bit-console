from PIL import Image

tiles = [
    [
        [1, 1, 1, 1, 1, 1, 1, 1],
        [1, 2, 2, 2, 2, 2, 2, 1],
        [1, 2, 2, 2, 2, 2, 2, 1],
        [1, 2, 2, 2, 2, 2, 2, 1],
        [1, 2, 2, 2, 2, 2, 2, 1],
        [1, 2, 2, 2, 2, 2, 2, 1],
        [1, 2, 2, 2, 2, 2, 2, 1],
        [1, 1, 1, 1, 1, 1, 1, 1],
    ],
    [
        [3, 3, 3, 3, 3, 3, 3, 3],
        [3, 2, 2, 2, 2, 2, 2, 3],
        [3, 2, 4, 4, 4, 4, 2, 3],
        [3, 2, 4, 5, 5, 4, 2, 3],
        [3, 2, 4, 5, 5, 4, 2, 3],
        [3, 2, 4, 4, 4, 4, 2, 3],
        [3, 2, 2, 2, 2, 2, 2, 3],
        [3, 3, 3, 3, 3, 3, 3, 3],
    ],
]


macros = {}

def parse_macro(lines, start):
    global macros
    macro_name = lines[start][2:].strip()
    if not macro_name:
        raise ValueError("no macro name")
    macros[macro_name] = []
    
    end = start + 1
    while lines[end] != "?-":
        opcode, operand = parse_instruction(lines[end])
        macros[macro_name].extend([opcode, *operand])
        end += 1
    return end

def insert_macro(macro_name, args):
    macro_name = macro_name[1:]
    if macro_name not in macros:
        raise ValueError("Unknown macro")
    insts = []
    length = len(macros[macro_name])
    i = 0
    while i < length:
        opcode = macros[macro_name][i]
        mode = macros[macro_name][i + 1]

        if mode == 0xFF: # We are on an argument
            prefix = macros[macro_name][i + 2]
            arg = args[macros[macro_name][i + 3]]
            s = f"{prefix}{arg}"
            operand = parse_operand(s)
            mode, *value = operand
            opcode = opcode | mode << 4
            insts.extend([opcode, *value])
            i += 4
        elif mode in (6, 7):
            opcode = opcode | mode << 4
            insts.extend([opcode])
            i += 2
        elif mode in (3, 4, 5):
            operand = (macros[macro_name][i + 2], macros[macro_name][i + 3])
            opcode = opcode | mode << 4
            insts.extend([opcode, *operand])
            i += 4
        else:
            operand = macros[macro_name][i + 2]
            opcode = opcode | mode << 4
            insts.extend([opcode, operand])
            i += 3
    return insts

def parse_operand(value):
    prefix = value[0]
    operand = value[1:].split(',')

    if operand[0].startswith("?"): # Only in macros
        return (0xFF, prefix, int(operand[0][1]))

    match [prefix, *operand]:
        case ['@', 'C']: # CARRY FLAG
            return (6,)
        case ['@', 'PC']: # PC
            return (7,)
        case ['$', x]: # Immediate
            return (0, int(x))
        case ['#', x]: # Memory
            return (1, int(x))
        case ['@', x]: # Reg
            return (2, int(x))
        case ['$', x, y]: # IMM_16
            return (3, int(x), int(y))
        case ['#', x, y]: # MEM_16
            return (4, int(x), int(y))
        case ['@', x, y]: # REG_16
            return (5, int(x), int(y))
        case _:
            return [0, value]

def parse_instruction(line):
    try:
        key, value = line.split()
    except ValueError:
        key = line
        match key:
            case "NOOP": 
                opcode = 0
            case "HALT": 
                opcode = 0xFF
        return opcode, None
    operand = []
    match key:
        case "LDA":  
            opcode = 1
            operand = parse_operand(value)
        case "SAM":  
            opcode = 2
            operand = parse_operand(value)
        case "SAR":  
            opcode = 3
            operand = parse_operand(value)
        case "JAL":  
            opcode = 4
            operand = (0, value)
        case "JEQ":  
            opcode = 4
            operand = (1, value)
        case "JNE":  
            opcode = 4
            operand = (2, value)
        case "PSH":  
            opcode = 5
            operand = parse_operand(value)
        case "POP":  
            opcode = 6
            operand = parse_operand(value)
        case "CMP":  
            opcode = 7
            operand = parse_operand(value)
        case "ADD":  
            opcode = 8
            operand = parse_operand(value)
        case "AND":  
            opcode = 9
            operand = parse_operand(value)
        case "OR":   
            opcode = 10
            operand = parse_operand(value)
        case "NOT":  
            opcode = 11
            operand = parse_operand(value)
        case "SHR":  
            opcode = 12
            operand = parse_operand(value)
        case "SHL":  
            opcode = 13
            operand = parse_operand(value)
        case "PXC":  
            opcode = 14
            operand = parse_operand(value)
        case "PXP":  
            opcode = 15
            operand = parse_operand(value)
        case _:
            raise ValueError(f"Unknown opcode: {key}")
    return opcode, operand

def first_pass(lines):
    instructions = []
    labels = {}
    end = None
    for i, line in enumerate(lines):
        if end is not None and i <= end:
            continue

        # Macro definition
        if line.startswith("?+"):
            end = parse_macro(lines, i)
            continue
        
        # Macro call
        if line.startswith("?"): 
            macro_name, *args = line.split()
            insts = insert_macro(macro_name, args)
            instructions.extend(insts)
            continue

        if line.endswith(":"):
            labels[line[:-1]] = len(instructions)
            continue

        opcode, operand = parse_instruction(line)
        if operand:
            mode, *value = operand
            opcode = opcode | mode << 4
            instructions.extend([opcode, *value])
        else:
            instructions.append(opcode)
    return instructions, labels

def main():
    with open("loop.asm", "r") as f:
        lines = [line.strip() for line in f.readlines()]
        lines = [line for line in lines if line and not line.startswith("//")]
    instructions, labels = first_pass(lines)

    zero = 0
    un = 1
    fps = 60
    with open("refresh.bin", "wb") as f:
        # Header
        f.write(zero.to_bytes(2)) # Entrypoint
        f.write("Hello".ljust(16).encode()) # Title
        f.write(un.to_bytes(1)) # rom bank count
        f.write(un.to_bytes(1)) # video_bank_count
        f.write(fps.to_bytes(1)) # target_fps

        for i, instruction in enumerate(instructions):
            if not isinstance(instruction, int): # We are on a label
                if instruction in macros:
                    continue
                instruction = labels[instruction]
            v = instruction.to_bytes(1, signed=instruction < 0, byteorder='little')
            f.write(v)

        for i in range((16 * 1024) - len(instructions)):
            f.write(zero.to_bytes(1))

        # Asset packer

        img = Image.open("sprites.png").convert("RGB")
        pixels = img.load()

        colors = {
            (0, 0, 0): 0,
            (255, 255, 255): 1,
            (255, 255, 0): 2,
            (255, 0, 255): 3,
            (0, 255, 255): 4,
            (0, 255, 0): 5,
            (255, 0, 0): 6,
            (0, 0, 255): 6,
        }

        for j in range(16):
            for i in range(16):
                for y in range(8):
                    res = 0
                    for x in range(8):
                        color = colors[pixels[(i * 8 + x), (j * 8 + y)]]
                        res |= color << (x * 3)
                    v = res.to_bytes(3, signed=False, byteorder='little')
                    f.write(v)

if __name__ == "__main__":
    main()
