// Print every symbol whose full name contains one of the args.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.*;
public class SymFind extends GhidraScript {
    @Override
    public void run() throws Exception {
        SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
        while (it.hasNext()) {
            Symbol s = it.next();
            String n = s.getName(true);
            for (String q : getScriptArgs()) if (n.contains(q)) { println("SYM " + s.getAddress() + " " + n); break; }
        }
    }
}
