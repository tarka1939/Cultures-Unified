// Headless export of a Ghidra program into greppable text files.
// Usage (via analyzeHeadless ... -postScript ExportAll.java <outdir>):
//   <outdir>/functions.tsv  entry  name  size  ncallers  callees(comma list of entry addrs)
//   <outdir>/strings.tsv    addr   referencing-function-entries(comma)  "string"
//   <outdir>/decomp.c       every function: "//@@ <entry> <name>" header + decompiled C
//   <outdir>/imports.tsv    import  thunk/ptr address  referencing-function-entries
// @category Cultures

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.StringDataInstance;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class ExportAll extends GhidraScript {

    private PrintWriter open(File dir, String name) throws IOException {
        return new PrintWriter(new BufferedWriter(new OutputStreamWriter(
            new FileOutputStream(new File(dir, name)), StandardCharsets.UTF_8)));
    }

    private String funcsReferencing(Address a) {
        TreeSet<String> s = new TreeSet<>();
        for (Reference r : currentProgram.getReferenceManager().getReferencesTo(a)) {
            Function f = getFunctionContaining(r.getFromAddress());
            s.add(f != null ? f.getEntryPoint().toString() : ("@" + r.getFromAddress()));
        }
        return String.join(",", s);
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : "export");
        out.mkdirs();
        FunctionManager fm = currentProgram.getFunctionManager();

        try (PrintWriter w = open(out, "functions.tsv")) {
            for (Function f : fm.getFunctions(true)) {
                TreeSet<String> callees = new TreeSet<>();
                for (Function c : f.getCalledFunctions(monitor)) callees.add(c.getEntryPoint().toString());
                w.printf("%s\t%s\t%d\t%d\t%s%n", f.getEntryPoint(), f.getName(),
                    f.getBody().getNumAddresses(), f.getCallingFunctions(monitor).size(), String.join(",", callees));
            }
        }

        try (PrintWriter w = open(out, "strings.tsv")) {
            for (Data d : currentProgram.getListing().getDefinedData(true)) {
                if (!StringDataInstance.isString(d)) continue;
                StringDataInstance sdi = StringDataInstance.getStringDataInstance(d);
                String v = sdi.getStringValue();
                if (v == null) continue;
                v = v.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r");
                w.printf("%s\t%s\t\"%s\"%n", d.getAddress(), funcsReferencing(d.getAddress()), v);
            }
        }

        try (PrintWriter w = open(out, "imports.tsv")) {
            SymbolTable st = currentProgram.getSymbolTable();
            for (Symbol s : st.getExternalSymbols()) {
                StringBuilder refs = new StringBuilder();
                for (Reference r : s.getReferences()) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    String ref = funcsReferencing(r.getFromAddress());
                    refs.append(f != null ? f.getEntryPoint() : r.getFromAddress()).append(",");
                    if (!ref.isEmpty()) refs.append(ref).append(",");
                }
                w.printf("%s!%s\t%s%n", s.getParentNamespace().getName(), s.getName(), refs);
            }
        }

        DecompInterface di = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        di.setOptions(opts);
        di.toggleCCode(true);
        di.toggleSyntaxTree(false);
        di.setSimplificationStyle("decompile");
        di.openProgram(currentProgram);
        int n = 0, fail = 0;
        try (PrintWriter w = open(out, "decomp.c")) {
            for (Function f : fm.getFunctions(true)) {
                if (monitor.isCancelled()) break;
                if (f.isThunk() || f.isExternal()) continue;
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                w.printf("//@@ %s %s%n", f.getEntryPoint(), f.getName());
                if (r != null && r.decompileCompleted()) {
                    w.println(r.getDecompiledFunction().getC());
                } else {
                    w.println("// decompile failed: " + (r == null ? "null" : r.getErrorMessage()));
                    fail++;
                }
                n++;
            }
        }
        di.dispose();
        println("ExportAll: " + n + " functions decompiled, " + fail + " failed -> " + out.getAbsolutePath());
    }
}
