// Decompile every function of the current program into one C file.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class DecompileAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs()[0];
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(new FileWriter(out))) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                DecompileResults r = di.decompileFunction(f, 120, monitor);
                w.println("/* ==== " + f.getName() + " @ " + f.getEntryPoint() + " ==== */");
                if (r != null && r.decompileCompleted())
                    w.println(r.getDecompiledFunction().getC());
                else
                    w.println("/* decompile failed */");
            }
        }
    }
}
