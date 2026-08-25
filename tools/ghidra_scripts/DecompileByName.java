// Декомпілює функції за підрядком імені й пише результат у файл.
//
// Сенс: не тягнути декомпіляцію в чат цілими модулями, а вивантажити рівно
// потрібні функції на диск і законспектувати. Правило проєкту — одна функція
// за раз (див. CLAUDE.md).
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
            println("потрібні: <файл виводу> <підрядок імені> [ще підрядки...]");
            return;
        }
        String outputPath = args[0];

        List<String> wanted = new ArrayList<>();
        for (int i = 1; i < args.length; ++i) {
            wanted.add(args[i]);
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        // 60 секунд на функцію: у великих цього вистачає, а зависнути не дає.
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
                    out.println("// декомпіляція не вдалася: " + results.getErrorMessage());
                }
                out.println();
                ++written;
            }
        }
        decompiler.dispose();
        println("функцій вивантажено: " + written + " -> " + outputPath);
    }
}
