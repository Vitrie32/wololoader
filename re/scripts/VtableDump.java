// For each class name arg, print vftable address and its slots.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;

public class VtableDump extends GhidraScript {
    @Override
    public void run() throws Exception {
        SymbolTable st = currentProgram.getSymbolTable();
        for (String cls : getScriptArgs()) {
            SymbolIterator it = st.getSymbols("vftable");
            while (it.hasNext()) {
                Symbol s = it.next();
                if (!s.getParentNamespace().getName(true).equals(cls)) continue;
                println("VTABLE " + cls + " @ " + s.getAddress());
                Address a = s.getAddress();
                for (int i = 0; i < 80; i++) {
                    Address slot = a.add(i * 4);
                    if (i > 0 && st.getPrimarySymbol(slot) != null && st.getPrimarySymbol(slot).getName().contains("meta")) break;
                    int v = getInt(slot);
                    Function f = getFunctionAt(toAddr(v & 0xffffffffL));
                    if (f == null) break;
                    println(String.format("  [%2d +0x%02x] %s %s", i, i * 4, f.getEntryPoint(), f.getName(true)));
                }
            }
        }
    }
}
