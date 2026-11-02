// Decompile the ARM functions that contain the given addresses.
// Args: <output file> <addr_hex> [addr_hex ...]
// Works on a raw code.bin imported with -noanalysis: walks back from each
// address to the nearest `push {..., lr}` and disassembles from there.
//@category SunnyMoon

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileAt extends GhidraScript {

    private static boolean endsFunction(int w) {
        return w == 0xE12FFF1E                       // bx lr
            || (w & 0xFFFF8000) == 0xE8BD8000         // ldmia sp!, {..., pc}
            || (w & 0x0F000000) == 0x0A000000 && (w >>> 28) == 0xE   // unconditional b (tail call)
            || (w >>> 28) == 0x0;                     // literal pool data, not an ARM instruction
    }

    // Walk back to the instruction after the previous function's return or
    // literal pool. `=addr` on the command line skips this and uses addr as-is.
    private Address findStart(Address at) throws Exception {
        for (int back = 0; back < 0x2000; back += 4) {
            Address a = at.subtract(back);
            if (endsFunction(getInt(a.subtract(4)))) {
                return a;
            }
        }
        return at;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        PrintWriter out = new PrintWriter(new FileWriter(args[0]));
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);

        for (int i = 1; i < args.length; i++) {
            boolean exact = args[i].startsWith("=");
            Address at = toAddr(Long.parseLong(exact ? args[i].substring(1) : args[i], 16));
            Address start = exact ? at : findStart(at);
            new DisassembleCommand(start, null, true).applyTo(currentProgram, monitor);
            Function f = getFunctionAt(start);
            if (f == null) {
                f = createFunction(start, null);
            }
            out.println("// ===== " + args[i] + " -> function " + start);
            if (f == null) {
                out.println("// could not create function");
                continue;
            }
            out.println("// listing:");
            for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                out.println("//   " + ins.getAddress() + "  " + ins);
            }
            DecompileResults r = dec.decompileFunction(f, 60, monitor);
            out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// decompile failed: " + r.getErrorMessage());
        }
        out.close();
    }
}
