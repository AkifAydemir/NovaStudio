using System.IO;
using NovaStudio.Models;
namespace NovaStudio.Services;

internal static class BytecodeDisassembler
{
    internal static IReadOnlyList<DisassemblyRow> Decode(byte[] bytecode, uint baseAddress = 0)
    {
        var rows = new List<DisassemblyRow>();
        var offset = 0;
        while (offset < bytecode.Length)
        {
            var start = offset;
            var opcode = bytecode[offset++];
            string text;
            try
            {
                text = opcode switch
                {
                    0x00 => "NOP",
                    0x01 => "HALT",
                    0x10 => $"MOV R{ReadU8(bytecode, ref offset)}, 0x{ReadU32(bytecode, ref offset):X8}",
                    0x11 => $"MOV R{ReadU8(bytecode, ref offset)}, R{ReadU8(bytecode, ref offset)}",
                    0x20 => ThreeRegister("ADD", bytecode, ref offset),
                    0x21 => ThreeRegister("SUB", bytecode, ref offset),
                    0x22 => ThreeRegister("MUL", bytecode, ref offset),
                    0x23 => ThreeRegister("DIV", bytecode, ref offset),
                    0x24 => ThreeRegister("AND", bytecode, ref offset),
                    0x25 => ThreeRegister("OR", bytecode, ref offset),
                    0x26 => ThreeRegister("XOR", bytecode, ref offset),
                    0x27 => ThreeRegister("SHL", bytecode, ref offset),
                    0x28 => ThreeRegister("SHR", bytecode, ref offset),
                    0x30 => $"CMP R{ReadU8(bytecode, ref offset)}, R{ReadU8(bytecode, ref offset)}",
                    0x40 => $"JMP 0x{ReadU32(bytecode, ref offset):X8}",
                    0x41 => $"JE 0x{ReadU32(bytecode, ref offset):X8}",
                    0x42 => $"JNE 0x{ReadU32(bytecode, ref offset):X8}",
                    0x43 => $"JL 0x{ReadU32(bytecode, ref offset):X8}",
                    0x44 => $"JLE 0x{ReadU32(bytecode, ref offset):X8}",
                    0x45 => $"JG 0x{ReadU32(bytecode, ref offset):X8}",
                    0x46 => $"JGE 0x{ReadU32(bytecode, ref offset):X8}",
                    0x50 => $"PUSH R{ReadU8(bytecode, ref offset)}",
                    0x51 => $"POP R{ReadU8(bytecode, ref offset)}",
                    0x52 => $"CALL 0x{ReadU32(bytecode, ref offset):X8}",
                    0x53 => "RET",
                    0x60 => $"STORE8 [R{ReadU8(bytecode, ref offset)}], R{ReadU8(bytecode, ref offset)}",
                    0x61 => $"LOAD8 R{ReadU8(bytecode, ref offset)}, [R{ReadU8(bytecode, ref offset)}]",
                    0x62 => $"STORE32 [R{ReadU8(bytecode, ref offset)}], R{ReadU8(bytecode, ref offset)}",
                    0x63 => $"LOAD32 R{ReadU8(bytecode, ref offset)}, [R{ReadU8(bytecode, ref offset)}]",
                    0x70 => $"ALLOC R{ReadU8(bytecode, ref offset)}, R{ReadU8(bytecode, ref offset)}",
                    0x71 => $"FREE R{ReadU8(bytecode, ref offset)}",
                    _ => $"DB 0x{opcode:X2}"
                };
            }
            catch (InvalidDataException)
            {
                offset = Math.Min(start + 1, bytecode.Length);
                text = $"DB 0x{opcode:X2} ; truncated instruction";
            }

            var size = Math.Max(1, offset - start);
            var bytes = string.Join(" ", bytecode.Skip(start).Take(size).Select(value => value.ToString("X2")));
            rows.Add(new DisassemblyRow
            {
                Address = baseAddress + (uint)start,
                Opcode = opcode,
                Size = size,
                Bytes = bytes,
                Text = text
            });
        }
        return rows;
    }
    private static string ThreeRegister(string mnemonic, byte[] code, ref int offset)
    {
        var dst = ReadU8(code, ref offset);
        var lhs = ReadU8(code, ref offset);
        var rhs = ReadU8(code, ref offset);
        return $"{mnemonic} R{dst}, R{lhs}, R{rhs}";
    }
    private static byte ReadU8(byte[] code, ref int offset)
    {
        if (offset >= code.Length)
            throw new InvalidDataException();
        return code[offset++];
    }
    private static uint ReadU32(byte[] code, ref int offset)
    {
        if (offset > code.Length - 4)
            throw new InvalidDataException();
        var value = (uint)(code[offset]
        | (code[offset + 1] << 8)
        | (code[offset + 2] << 16)
        | (code[offset + 3] << 24));
        offset += 4;
        return value;
    }
}
