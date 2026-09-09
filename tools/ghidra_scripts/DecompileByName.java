// Decompiles the functions matching a name substring and writes the result out.
//
// The point: not to pull whole modules of decompilation into the chat but to
// dump exactly the functions wanted onto disk and take notes. The project's
// rule is one function at a time (see CLAUDE.md).
//
//   analyzeHeadless ghidra_projects OpenBF2 -process bf2 -noanalysis \
//     -scriptPath tools/ghidra_scripts -postScript DecompileByName.java \
//     out.txt "BitStream::writeCompressedVector" "BitStream::readCompressedVector"
//
// @category OpenBF2

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class DecompileByName extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("needed: <output file> <name substring> [more substrings...]");
            return;
        }
        String outputPath = args[0];

        List<String> wanted = new ArrayList<>();
        for (int i = 1; i < args.length; ++i) {
            wanted.add(args[i]);
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        // 60 seconds per function: enough for the large ones, and it cannot hang.
        decompiler.setSimplificationStyle("decompile");

        int written = 0;
        try (PrintWriter out = new PrintWriter(outputPath, "UTF-8")) {
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext() && !monitor.isCancelled()) {
                Function function = functions.next();
                String name = function.getName(true);

                boolean matches = false;
                for (String needle : wanted) {
                    if (name.contains(needle)) {
                        matches = true;
                        break;
                    }
                }
                if (!matches) {
                    continue;
                }

                out.println("// ==== " + name + " @ " + function.getEntryPoint() + " ====");
                DecompileResults results = decompiler.decompileFunction(function, 60, monitor);
                if (results.decompileCompleted() && results.getDecompiledFunction() != null) {
                    out.println(results.getDecompiledFunction().getC());
                } else {
                    out.println("// decompilation failed: " + results.getErrorMessage());
                }
                out.println();
                ++written;
            }
        }
        decompiler.dispose();
        println("functions dumped: " + written + " -> " + outputPath);
    }
}
