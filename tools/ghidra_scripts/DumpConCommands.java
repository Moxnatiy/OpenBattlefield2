// Витягує з BF2.exe рядки, схожі на команди мови .con ("ціль.метод"), і
// зводить їх у файл. Потрібно, щоб побачити, які команди рушій узагалі знає,
// а не лише ті, що трапилися у файлах гри.
//
// Запуск (headless):
//   analyzeHeadless ghidra_projects OpenBF2 -process BF2.exe -noanalysis \
//     -scriptPath tools/ghidra_scripts -postScript DumpConCommands.java out.txt
//
// @category OpenBF2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.PrintWriter;
import java.util.Map;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class DumpConCommands extends GhidraScript {

    // "ObjectTemplate.createComponent", "renderer.waterColor", "game.listPlayers"
    private static final Pattern COMMAND =
        Pattern.compile("^[A-Za-z][A-Za-z0-9_]{2,31}(\\.[A-Za-z][A-Za-z0-9_]{1,31}){1,2}$");

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outputPath = args.length > 0 ? args[0] : "con_commands.txt";

        int functionCount = 0;
        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
        while (functions.hasNext() && !monitor.isCancelled()) {
            functions.next();
            ++functionCount;
        }

        // Ключ — команда у нижньому регістрі, значення — як вона написана
        // в бінарі: регістр знадобиться, коли писатимемо власні обробники.
        Map<String, String> commands = new TreeMap<>();
        int stringCount = 0;

        DataIterator data = currentProgram.getListing().getDefinedData(true);
        while (data.hasNext() && !monitor.isCancelled()) {
            Data item = data.next();
            if (!item.hasStringValue()) {
                continue;
            }
            ++stringCount;

            StringDataInstance instance = StringDataInstance.getStringDataInstance(item);
            String value = instance.getStringValue();
            if (value == null) {
                continue;
            }
            value = value.trim();

            Matcher matcher = COMMAND.matcher(value);
            if (matcher.matches()) {
                commands.putIfAbsent(value.toLowerCase(), value);
            }
        }

        try (PrintWriter out = new PrintWriter(outputPath, "UTF-8")) {
            out.println("# Команди .con, знайдені у BF2.exe");
            out.println("# функцій у програмі: " + functionCount);
            out.println("# рядків проаналізовано: " + stringCount);
            out.println("# кандидатів у команди: " + commands.size());
            for (Map.Entry<String, String> entry : commands.entrySet()) {
                out.println(entry.getValue());
            }
        }

        println("функцій: " + functionCount + ", рядків: " + stringCount
                + ", команд: " + commands.size() + " -> " + outputPath);
    }
}
