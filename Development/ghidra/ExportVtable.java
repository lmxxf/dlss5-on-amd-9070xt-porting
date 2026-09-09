// Export function pointers following a named vtable symbol.
// Usage: ExportVtable.java <symbol-substring> [entry-count]
//
// @category DLSSNR

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import java.util.ArrayList;
import java.util.List;

public class ExportVtable extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            throw new IllegalArgumentException(
                "Usage: ExportVtable.java <symbol-substring> [entry-count]"
            );
        }
        String needle = args[0];
        int count = args.length > 1 ? Integer.parseInt(args[1]) : 32;

        List<Symbol> matches = new ArrayList<>();
        for (Symbol symbol : currentProgram.getSymbolTable().getAllSymbols(true)) {
            if (symbol.getName(true).contains(needle)) {
                matches.add(symbol);
            }
        }
        if (matches.isEmpty()) {
            throw new IllegalStateException("No symbol contains: " + needle);
        }

        for (Symbol symbol : matches) {
            Address table = symbol.getAddress();
            println("DLSSNR_VTABLE " + symbol.getName(true) + " " + table);
            for (int index = 0; index < count; index++) {
                Address slot = table.add(index * 8L);
                long raw = getLong(slot);
                Address target = toAddr(raw);
                Function function = getFunctionAt(target);
                println(
                    String.format(
                        "DLSSNR_VTABLE_ENTRY %02d %s %s",
                        index,
                        target,
                        function == null ? "<no-function>" : function.getName(true)
                    )
                );
            }
        }
    }
}
