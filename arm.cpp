#include <cinttypes>
#include <cassert>
#include <Windows.h>

extern bool bDone;
extern uint8_t *pMemory;
extern uint32_t programSize;
extern volatile bool bGPIOInitialized;
extern volatile bool bLEDOn;

uint32_t programEnd;
uint32_t memoryEnd;

// Registers
#define SP_REG 13
#define LR_REG 14
#define PC_REG 15
uint32_t r[16] = {0};

#define INST_DATA_PROCESSING_REGISTER 0x0
#define INST_DATA_PROCESSING_IMMEDIATE 0x1
#define INST_LOAD_STORE_IMMEDIATE 0x2
#define INST_LOAD_STORE_REGISTER 0x3
#define INST_LOAD_STORE_MULTIPLE 0x4
#define INST_BRANCH 0x5

#define MEDIA_INST_MULTIPLIES 0xE

#define COND_EQ 0x0
#define COND_NE 0x1
#define COND_CS_HS 0x2
#define COND_CC_LO 0x3
#define COND_MI 0x4
#define COND_PL 0x5
#define COND_VS 0x6
#define COND_VC 0x7
#define COND_HI 0x8
#define COND_LS 0x9
#define COND_GE 0xA
#define COND_LT 0xB
#define COND_GT 0xC
#define COND_LE 0xD
#define COND_AL 0xE

#define INST_AND 0x0
#define INST_SUB 0x2
#define INST_ADD 0x4
#define INST_TST 0x8
#define INST_TEQ 0x9
#define INST_CMP 0xA
#define INST_ORR 0xC
#define INST_MOV 0xD
#define INST_BIC 0xE
#define INST_MVN 0xF

#define INSTA_MUL 0x0

bool N = false;
bool Z = false;
bool C = false;
bool V = false;

#define BYTE_BITS 8
#define BitsCount( val ) ( sizeof( val ) * BYTE_BITS )
#define Shift( val, steps ) ( steps % BitsCount( val ) )
#define ROL( val, steps ) ( ( val << Shift( val, steps ) ) | ( val >> ( BitsCount( val ) - Shift( val, steps ) ) ) )
#define ROR( val, steps ) ( ( val >> Shift( val, steps ) ) | ( val << ( BitsCount( val ) - Shift( val, steps ) ) ) )

// Returns 1 if the subtraction specified as its parameter caused a borrow(the true result is less than 0, where the operands are treated as unsigned integers), and returns 0 in all other cases.This delivers further information about a subtractionwhich occurred earlier in the pseudo - code.The subtraction is not repeated.
bool BorrowFrom(uint32_t val)
{
    return ((val >> 31) & 0x1) ? true : false;
}

uint8_t mailbox[0x24] = {0};

uint32_t read32(uint32_t addr)
{
    if (addr >= memoryEnd)
    {
        if (addr >= 0x3F00B880 && addr <= 0x3F00B880 + 0x20)
        {
            // Mailbox
            addr -= 0x3F00B880;
            auto val = *(uint32_t*)(mailbox + addr);
            return val;
        }
        else if (addr == 0x60000000)
        {
            // Mouse position
            POINT pos;
            GetCursorPos(&pos);
            uint32_t val = pos.x;
            val <<= 16;
            val |= pos.y & 0xFFFF;
            return val;
        }
        else
        {
            assert(false);
        }
    }

    auto val = *(uint32_t*)(pMemory + addr);
    return val;
}

uint32_t read8(uint32_t addr)
{
    auto val = read32(addr);
    val &= 0xFF;
    return val;
}

void write32(uint32_t val, uint32_t addr)
{
    // 0x3F200020 - ON
    // 0x3F20002C - OFF

    // 0x00008000
    // 0000 0000 0000 0000 1000 0000 0000 0000

    if (addr >= memoryEnd)
    {
        if (addr == 0x3F200010)
        {
            if (val == 0x3F200010)
            {
                bGPIOInitialized = true;
                return;
            }
            else
            {
                assert(false);
            }
        }
        else if (addr == 0x3F200020)
        {
            if (val == 0x00008000)
            {
                bLEDOn = true;
            }
            else
            {
                assert(false);
            }
        }
        else if (addr == 0x3F20002C)
        {
            if (val == 0x00008000)
            {
                bLEDOn = false;
            }
            else
            {
                assert(false);
            }
        }
        else if (addr >= 0x3F00B880 && addr <= 0x3F00B880 + 0x20)
        {
            // Mailboox
            addr -= 0x3F00B880;
            *(uint32_t*)(mailbox + addr) = val;
        }
        else
        {
            assert(false);
        }
    }
    else
    {
        *(uint32_t*)(pMemory + addr) = val;
    }
}

uint32_t calculateRn(uint32_t Rn)
{
    int val = r[Rn];
    if (Rn == PC_REG) val += 8;
    return val;
}

uint32_t calculateOperand(uint32_t shifter_operand)
{
    if (shifter_operand & 0xFF0)
    {
        assert(false);
        return 0;
    }
    else
    {
        return calculateRn(shifter_operand);
    }
}

void armStart()
{
    r[PC_REG] = 0x8000; // Looks like we have to ignore the first instruction?
    programEnd = r[PC_REG] + programSize;
        
    while (!bDone)
    {
        // Load the instruction
        uint32_t inst = *(uint32_t*)(pMemory + r[PC_REG]);

        // Check condition
        auto cond = inst >> 28;
        bool passed = false;
        if (cond == COND_EQ)
        {
            if (Z) passed = true;
        }
        else if (cond == COND_NE)
        {
            if (!Z) passed = true;
        }
        else if (cond == COND_CS_HS)
        {
            if (C) passed = true;
        }
        else if (cond == COND_CC_LO)
        {
            if (!C) passed = true;
        }
        else if (cond == COND_MI)
        {
            if (N) passed = true;
        }
        else if (cond == COND_PL)
        {
            if (!N) passed = true;
        }
        else if (cond == COND_VS)
        {
            if (V) passed = true;
        }
        else if (cond == COND_VC)
        {
            if (!V) passed = true;
        }
        else if (cond == COND_HI)
        {
            if (C && !Z) passed = true;
        }
        else if (cond == COND_LS)
        {
            if (!C && Z) passed = true;
        }
        else if (cond == COND_GE)
        {
            if (N == V) passed = true;
        }
        else if (cond == COND_LT)
        {
            if (N != V) passed = true;
        }
        else if (cond == COND_GT)
        {
            if (Z == 0 && N == V) passed = true;
        }
        else if (cond == COND_LE)
        {
            if (Z == 1 && N != V) passed = true;
        }
        else if (cond == COND_AL)
        {
            passed = true;
        }
        else
        {
            assert(false);
        }

        if (!passed)
        {
            r[PC_REG] += 4;
            continue;
        }

        // instruction
        auto instCode = (inst >> 25) & 0x7;
        if (instCode == INST_BRANCH)
        {
            auto L = (inst >> 24) & 0x1;
            int offset = ((int)inst << 8);
            offset >>= 6;
            offset += 8;
            if (L)
            {
                r[LR_REG] = r[PC_REG];
                r[PC_REG] += offset;
                continue;
            }
            else
            {
                r[PC_REG] += offset;
                continue;
            }
        }
        else if (instCode == INST_DATA_PROCESSING_IMMEDIATE)
        {
            auto opcode = (inst >> 21) & 0xF;
            auto S = (inst >> 20) & 0x1;
            auto Rn = (inst >> 16) & 0xF;
            auto Rd = (inst >> 12) & 0xF;
            auto rotate = ((inst >> 8) & 0xF) * 2;
            auto immediate = (inst & 0xFF);

            immediate = ROR(immediate, rotate);

            if (S == 0)
            {
                if (opcode == INST_MOV)
                {
                    r[Rd] = immediate;
                }
                else if (opcode == INST_ADD)
                {
                    r[Rd] = r[Rn] + immediate;
                }
                else if (opcode == INST_AND)
                {
                    r[Rd] = r[Rn] & immediate;
                }
                else if (opcode == INST_BIC)
                {
                    // A4.1.6 BIC
                    r[Rd] = r[Rn] & ~immediate;
                }
                else if (opcode == INST_SUB)
                {
                    // A4.1.106 SUB
                    r[Rd] = r[Rn] - immediate;
                }
                else
                {
                    // A4.1.107 SWI
                    // Software interupt
                    assert(false);
                }
            }
            else
            {
                if (opcode == INST_TST)
                {
                    if (Rd != 0) assert(false);
                    auto alu_out = r[Rn] & immediate;
                    N = ((alu_out >> 31) & 0x1) ? true : false;
                    Z = alu_out == 0 ? true : false;
                    //TODO: C = shifter_carry_out
                }
                else if (opcode == INST_CMP)
                {
                    if (Rd != 0) assert(false);
                    auto alu_out = r[Rn] - immediate;
                    N = ((alu_out >> 31) & 0x1) ? true : false;
                    Z = alu_out == 0 ? true : false;
                    C = !BorrowFrom(alu_out);
                    //TODO: V = OverflowFrom(Rn - shifter_operand)
                }
                else if (opcode == INST_TEQ)
                {
                    if (Rd != 0) assert(false);
                    auto alu_out = r[Rn] ^ immediate;
                    Z = alu_out == 0 ? true : false;
                    //TODO: C = shifter_carry_out
                }
                else if (opcode == INST_SUB)
                {
                    // A4.1.106 SUB
                    r[Rd] = r[Rn] - immediate;
                    N = ((r[Rd] >> 31) & 0x1) ? true : false;
                    Z = r[Rd] == 0 ? true : false;
                    C = !BorrowFrom(r[Rd]);
                    //TODO: V = OverflowFrom(r[Rd])
                }
                else
                {
                    assert(false);
                }
            }
        }
        else if (instCode == INST_DATA_PROCESSING_REGISTER)
        {
            auto type = (inst >> 4) & 0x1;
            auto opcode = (inst >> 21) & 0xF;

            if (type)
            {
                // Data processing register shift
                if (opcode == INSTA_MUL)
                {
                    // A4.1.40 MUL
                    auto S = (inst >> 20) & 0x1;
                    auto Rd = (inst >> 16) & 0xF;
                    auto SBZ = (inst >> 12) & 0xF;
                    auto Rs = (inst >> 8) & 0xF;
                    auto Rm = inst & 0xF;

                    if (SBZ) assert(false); // Should be zero
                    r[Rd] = r[Rm] * r[Rs];
                    if (S)
                    {
                        N = ((r[Rd] >> 31) & 0x1) ? true : false;
                        Z = r[Rd] == 0 ? true : false;
                    }
                }
                else if (opcode == INST_MOV)
                {
                    auto shift = (inst >> 4) & 0xF;
                    if (shift == 3)
                    {
                        // A5.1.8 Data-processing operands - Logical shift right by register
                        auto S = (inst >> 20) & 0x1;
                        auto Rn = (inst >> 16) & 0xF;
                        auto Rd = (inst >> 12) & 0xF;
                        auto Rs = (inst >> 8) & 0xF;
                        auto Rm = inst & 0xF;

                        if (Rn == 15 || Rd == 15 || Rs == 15 || Rm == 15) assert(false); // UNPREDICTABLE

                        auto Rs7_0 = r[Rs] & 0xFF;

                        if (Rs7_0 == 0)
                        {
                            r[Rd] = r[Rm];
                            //TODO: shifter_carry_out = C Flag
                        }
                        else if (Rs7_0 < 32)
                        {
                            r[Rd] = r[Rm] >> (r[Rs] & 0xFF);
                            //TODO: shifter_carry_out = Rm[Rs[7:0] - 1]
                        }
                        else if (Rs7_0 == 32)
                        {
                            r[Rd] = 0;
                            //TODO: shifter_carry_out = Rm[31]
                        }
                        else // Rs7_0 > 32
                        {
                            r[Rd] = 0;
                            //TODO: shifter_carry_out = 0
                        }
                    }
                    else
                    {
                        assert(false);
                    }
                }
                else
                {
                    assert(false);
                }
            }
            else
            {
                if (opcode == INST_MOV)
                {
                    // A5.1.5 Data-processing operands - Logical shift left by immediate
                    auto S = (inst >> 20) & 0x1;
                    auto Rn = (inst >> 16) & 0xF;
                    auto Rd = (inst >> 12) & 0xF;
                    auto shift_imm = (inst >> 7) & 0x1F;
                    auto shift = (inst >> 5) & 0x3;
                    auto Rm = inst & 0xF;

                    if (shift == 0)
                    {
                        if (Rn == 15 || Rm == 15) assert(false);
                        if (shift_imm)
                        {
                            r[Rd] = r[Rm] << shift_imm;
                        }
                        else
                        {
                            r[Rd] = r[Rm];
                        }
                    }
                    else if (shift == 0x1)
                    {
                        // A5.1.7 Data-processing operands - Logical shift right by immediate
                        if (shift_imm)
                        {
                            r[Rd] = r[Rm] >> shift_imm;
                        }
                        else
                        {
                            r[Rd] = r[Rm];
                        }
                    }
                    else
                    {
                        assert(false);
                    }
                }
                else if (opcode == INST_MVN)
                {
                    // A4.1.41 MVN
                    auto I = (inst >> 25) & 0x1;
                    auto S = (inst >> 20) & 0x1;
                    auto SBZ = (inst >> 16) & 0xF;
                    if (SBZ) assert(false); // SBZ = Should be zero
                    auto Rd = (inst >> 12) & 0xF;
                    auto shifter_operand = inst & 0xFFF;

                    if (I == 0)
                    {
                        if (S == 0)
                        {
                            r[Rd] = ~calculateOperand(shifter_operand);
                        }
                        else
                        {
                            assert(false);
                        }
                    }
                    else
                    {
                        assert(false);
                    }
                }
                else if (opcode == INST_AND)
                {
                    // A4.1.4 AND
                    auto I = (inst >> 25) & 0x1;
                    auto S = (inst >> 20) & 0x1;
                    auto Rn = (inst >> 16) & 0xF;
                    auto Rd = (inst >> 12) & 0xF;
                    auto shifter_operand = inst & 0xFFF;

                    if (I == 0)
                    {
                        if (S == 0)
                        {
                            auto bit7 = (inst >> 7) & 0x1;
                            auto bit4 = (inst >> 4) & 0x1;

                            if (bit7 && bit4) assert(false); // ~AND

                            r[Rd] = calculateRn(Rn) & calculateOperand(shifter_operand);
                        }
                        else
                        {
                            assert(false);
                        }
                    }
                    else
                    {
                        assert(false);
                    }
                }
                else if (opcode == INST_ORR)
                {
                    // A4.1.42 ORR
                    auto I = (inst >> 25) & 0x1;
                    auto S = (inst >> 20) & 0x1;
                    auto Rn = (inst >> 16) & 0xF;
                    auto Rd = (inst >> 12) & 0xF;
                    auto shifter_operand = inst & 0xFFF;

                    if (I == 0)
                    {
                        if (S == 0)
                        {
                            auto bit7 = (inst >> 7) & 0x1;
                            auto bit4 = (inst >> 4) & 0x1;

                            if (bit7 && bit4) assert(false); // ~ORR

                            r[Rd] = calculateRn(Rn) | calculateOperand(shifter_operand);
                        }
                        else
                        {
                            assert(false);
                        }
                    }
                    else
                    {
                        assert(false);
                    }
                }
                else if (opcode == INST_ADD)
                {
                    // A4.1.3 ADD
                    auto I = (inst >> 25) & 0x1;
                    auto S = (inst >> 20) & 0x1;
                    auto Rn = (inst >> 16) & 0xF;
                    auto Rd = (inst >> 12) & 0xF;
                    auto shift_imm = (inst >> 7) & 0x1F;
                    auto shifter_operand = inst & 0xF;
                    auto Rmv = r[shifter_operand] << shift_imm;

                    if (I == 0)
                    {
                        if (S == 0)
                        {
                            auto bit7 = (inst >> 7) & 0x1;
                            auto bit4 = (inst >> 4) & 0x1;

                            if (bit7 && bit4) assert(false); // see Extending the instruction set on page A3-32 to determine which instruction it is.
                            r[Rd] = r[Rn] + Rmv;
                        }
                        else
                        {
                            assert(false);
                        }
                    }
                    else
                    {
                        assert(false);
                    }
                }
                else if (opcode == INST_TEQ)
                {
                    // A4.1.116 TEQ
                    auto I = (inst >> 25) & 0x1;
                    auto Rn = (inst >> 16) & 0xF;
                    auto SBZ = (inst >> 12) & 0xF;
                    auto shifter_operand = inst & 0xFFF;

                    if (SBZ) assert(false); // Should be zero

                    if (I == 0)
                    {
                        auto alu_out = r[Rn] ^ r[shifter_operand];
                        N = ((alu_out >> 31) & 0x1) ? true : false;
                        Z = alu_out == 0 ? true : false;
                        //TODO: C = shifter_carry_out
                    }
                    else
                    {
                        assert(false);
                    }
                }
                else if (opcode == INST_SUB)
                {
                    // A4.1.3 ADD
                    auto I = (inst >> 25) & 0x1;
                    auto S = (inst >> 20) & 0x1;
                    auto Rn = (inst >> 16) & 0xF;
                    auto Rd = (inst >> 12) & 0xF;
                    auto shift_imm = (inst >> 7) & 0x1F;
                    auto shifter_operand = inst & 0xF;
                    auto Rmv = r[shifter_operand] << shift_imm;

                    if (I == 0)
                    {
                        if (S == 0)
                        {
                            auto bit7 = (inst >> 7) & 0x1;
                            auto bit4 = (inst >> 4) & 0x1;

                            if (bit7 && bit4) assert(false); // see Extending the instruction set on page A3-32 to determine which instruction it is.
                            r[Rd] = r[Rn] - Rmv;
                        }
                        else
                        {
                            assert(false);
                        }
                    }
                    else
                    {
                        assert(false);
                    }
                }
                else if (opcode == INST_CMP)
                {
                    // A5.1.1 Encoding
                    auto I = (inst >> 25) & 0x1;
                    auto Rn = (inst >> 16) & 0xF;
                    auto SBZ = (inst >> 12) & 0xF;
                    auto shifter_operand = inst & 0xFFF;

                    auto alu_out = r[Rn] - r[shifter_operand];
                    N = ((alu_out >> 31) & 0x1) ? true : false;
                    Z = alu_out == 0 ? true : false;
                    C = !BorrowFrom(alu_out);
                }
                else
                {
                    assert(false);
                }
            }
        }
        else if (instCode == INST_LOAD_STORE_IMMEDIATE)
        {
            auto P = (inst >> 24) & 0x1;
            auto U = (inst >> 23) & 0x1;
            auto B = (inst >> 22) & 0x1;
            auto W = (inst >> 21) & 0x1;
            auto L = (inst >> 20) & 0x1;
            auto Rn = (inst >> 16) & 0xF;
            auto Rd = (inst >> 12) & 0xF;
            auto immediate = inst & 0xFFF;

            auto incAfter = P ? 0 : immediate;
            immediate = P ? immediate : 0;

            if (L)
            {
                // Load
                if (B)
                {
                    // Byte
                    if (U)
                    {
                        auto address = calculateRn(Rn) + immediate;
                        r[Rn] += incAfter;
                        r[Rd] = read8(address);
                    }
                    else
                    {
                        auto address = calculateRn(Rn) - immediate;
                        r[Rn] += incAfter;
                        r[Rd] = read8(address);
                    }
                }
                else
                {
                    // Word
                    if (U)
                    {
                        auto address = calculateRn(Rn) + immediate;
                        r[Rn] += incAfter;
                        r[Rd] = read32(address);
                    }
                    else
                    {
                        auto address = calculateRn(Rn) - immediate;
                        r[Rn] += incAfter;
                        r[Rd] = read32(address);
                    }
                }
            }
            else
            {
                // Store
                if (B)
                {
                    // Byte
                    assert(false);
                }
                else
                {
                    // Word
                    if (U)
                    {
                        auto address = calculateRn(Rn) + immediate;
                        r[Rn] += incAfter;
                        write32(r[Rd], address);
                    }
                    else
                    {
                        auto address = calculateRn(Rn) - immediate;
                        r[Rn] += incAfter;
                        if (W)
                        {
                            r[Rn] -= immediate;
                        }
                        write32(r[Rd], address);
                    }
                }
            }
        }
        else if (instCode == INST_LOAD_STORE_MULTIPLE)
        {
            // A3.12 Load and Store Multiple instructions
            // That's basically push/pop
            auto P = (inst >> 24) & 0x1;
            auto U = (inst >> 23) & 0x1;
            auto N = (inst >> 22) & 0x1;
            auto W = (inst >> 21) & 0x1;
            auto L = (inst >> 20) & 0x1;
            auto Rn = (inst >> 16) & 0xF;
            auto register_list = inst & 0xFFFF;

            if (register_list == 0x4070)
            {
                int tmp;
                tmp = 5;
            }
            if (register_list == 0x8070)
            {
                int tmp;
                tmp = 5;
            }

            if (L)
            {
                // Load
                for (int i = 15; i >= 0; --i)
                {
                    if ((0x1 << i) & register_list)
                    {
                        r[i] = read32(r[Rn]);
                        r[Rn] += 4;
                    }
                }
            }
            else
            {
                // Store
                for (int i = 0; i < 16; ++i)
                {
                    if ((0x1 << i) & register_list)
                    {
                        r[Rn] -= 4;
                        write32(r[i], r[Rn]);
                    }
                }
            }
        }
        else if (instCode == INST_LOAD_STORE_REGISTER)
        {
            auto bit4 = (inst >> 4) & 0x1;
            if (bit4)
            {
                auto mediaInst = (inst >> 23) & 0x1F;
                if (mediaInst == MEDIA_INST_MULTIPLIES)
                {
                    auto opc1 = (inst >> 20) & 0x7;
                    auto Rd = (inst >> 16) & 0xF;
                    auto Rn = (inst >> 12) & 0xF;
                    auto Rs = (inst >> 8) & 0xF;
                    auto opc2 = (inst >> 5) & 0x7;
                    auto Rm = inst & 0xF;
                    r[Rd] = r[Rm] / r[Rs];
                }
                else
                {
                    assert(false);
                }
            }
            else
            {
                assert(false);
            }
        }
        else
        {
            assert(false);
        }

        // Proceed to the next instruction normally
        r[PC_REG] += 4;
    }
}
