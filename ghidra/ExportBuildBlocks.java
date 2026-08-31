// Export the decompiler output for the function containing
// CCNetwork::build_blocks in the leaked DLSSNR DLL.
//
// @category DLSSNR

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

public class ExportBuildBlocks extends GhidraScript {
    private static final String BUILD_BLOCKS_ADDRESS = "180036300";

    @Override
    public void run() throws Exception {
        Address address = toAddr(BUILD_BLOCKS_ADDRESS);
        Function function = getFunctionAt(address);
        if (function == null) {
            function = getFunctionContaining(address);
        }
        if (function == null) {
            throw new IllegalStateException(
                "No function found at or containing 0x" + BUILD_BLOCKS_ADDRESS
            );
        }

        println(
            "DLSSNR_BUILD_BLOCKS_FUNCTION " + function.getName() +
            " " + function.getEntryPoint() + " " + function.getBody()
        );

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Decompiler could not open current program");
        }

        DecompileResults result = decompiler.decompileFunction(function, 600, monitor);
        if (!result.decompileCompleted()) {
            throw new IllegalStateException(
                "Decompilation failed: " + result.getErrorMessage()
            );
        }
        String cCode = result.getDecompiledFunction().getC();
        String[] args = getScriptArgs();
        if (args.length == 0) {
            println(cCode);
        } else {
            Path output = Path.of(args[0]);
            Files.writeString(output, cCode, StandardCharsets.UTF_8);
            println("DLSSNR_BUILD_BLOCKS_OUTPUT " + output);
        }
        decompiler.dispose();
    }
}
