// Decompile every function to one C file + write a function index.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class ExportAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outDir = getScriptArgs().length > 0 ? getScriptArgs()[0] : ".";
        DecompInterface d = new DecompInterface();
        d.toggleCCode(true);
        d.openProgram(currentProgram);
        try (PrintWriter c = new PrintWriter(new FileWriter(outDir + "/decompiled.c"));
             PrintWriter idx = new PrintWriter(new FileWriter(outDir + "/functions.tsv"))) {
            idx.println("address\tname\tsize");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            int n = 0, fail = 0;
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                idx.println(f.getEntryPoint() + "\t" + f.getName(true) + "\t" + f.getBody().getNumAddresses());
                DecompileResults r = d.decompileFunction(f, 60, monitor);
                c.println("// ==== " + f.getEntryPoint() + " " + f.getName(true));
                if (r != null && r.decompileCompleted()) c.println(r.getDecompiledFunction().getC());
                else { c.println("// decompile failed"); fail++; }
                if (++n % 2000 == 0) println("decompiled " + n);
            }
            println("done: " + n + " functions, " + fail + " failed");
        }
    }
}
