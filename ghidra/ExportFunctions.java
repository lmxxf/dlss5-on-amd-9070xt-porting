// Decompile a list of function entry addresses into separate C files.
// Usage: ExportFunctions.java <output-directory> <address> [address...]
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

public class ExportFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            throw new IllegalArgumentException(
                "Usage: ExportFunctions.java <output-directory> <address> [address...]"
            );
        }

        Path outputDirectory = Path.of(args[0]);
        Files.createDirectories(outputDirectory);

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Decompiler could not open current program");
        }

        for (int index = 1; index < args.length; index++) {
            Address address = toAddr(args[index]);
            Function function = getFunctionAt(address);
            if (function == null) {
                function = getFunctionContaining(address);
            }
            if (function == null) {
                throw new IllegalStateException("No function at or containing 0x" + args[index]);
            }

            DecompileResults result = decompiler.decompileFunction(function, 600, monitor);
            if (!result.decompileCompleted()) {
                throw new IllegalStateException(
                    "Decompilation failed at 0x" + args[index] + ": " + result.getErrorMessage()
                );
            }

            Path output = outputDirectory.resolve(args[index] + ".c");
            Files.writeString(
                output,
                result.getDecompiledFunction().getC(),
                StandardCharsets.UTF_8
            );
            println(
                "DLSSNR_FUNCTION_OUTPUT " + args[index] + " " +
                function.getName() + " " + function.getBody() + " " + output
            );
        }
        decompiler.dispose();
    }
}
