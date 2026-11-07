#ifndef C_BUILD_CPP_MODULES_H
#define C_BUILD_CPP_MODULES_H

/* Named C++ module support stays separate from the ordinary compiler path.
 * Only targets containing .cppm files (or opting into std) require Clang. */
#include <ctype.h>

typedef struct CppModule {
    char name[C_MAX_NAME];
    char source[PATH_MAX];
    char pcm[PATH_MAX];
    C_StringList imports;
    unsigned char state;
    bool standard;
} CppModule;

typedef struct CppModuleGraph {
    CppModule *units;
    size_t count;
    C_StringList flags;
} CppModuleGraph;

static const char *cpp_module_compiler(void) {
    const char *cxx = getenv("CXX");
    return cxx && *cxx ? cxx : "clang++";
}

static bool cpp_module_source(const char *path) {
    const char *ext = strrchr(path, '.');
    return ext && !strcmp(ext, ".cppm");
}

static const char *cpp_module_word(const char *text, const char *keyword) {
    size_t length = strlen(keyword);
    if (strncmp(text, keyword, length) || !isspace((unsigned char)text[length]))
        return NULL;
    text += length;
    while (isspace((unsigned char)*text)) ++text;
    return text;
}

static bool cpp_module_name(const char *text, char out[C_MAX_NAME]) {
    size_t n = 0;
    while (isalnum((unsigned char)text[n]) || text[n] == '_' ||
           text[n] == '.' || text[n] == ':') {
        if (n + 1 >= C_MAX_NAME) die("C++ module name is too long");
        out[n] = text[n];
        ++n;
    }
    out[n] = '\0';
    return n > 0 && (text[n] == ';' || isspace((unsigned char)text[n]));
}

static void cpp_module_scan(CppModule *unit, bool provider) {
    FILE *f = fopen(unit->source, "r");
    if (!f) die("cannot scan C++ module source: %s", unit->source);
    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        char *comment = strstr(line, "//");
        if (comment) *comment = '\0';
        const char *p = line;
        while (isspace((unsigned char)*p)) ++p;
        const char *decl = cpp_module_word(p, "export");
        if (decl) {
            const char *mod = cpp_module_word(decl, "module");
            if (mod) {
                char name[C_MAX_NAME];
                if (!cpp_module_name(mod, name)) die("invalid module declaration in %s", unit->source);
                if (provider) {
                    if (unit->name[0]) die("multiple module declarations in %s", unit->source);
                    c__copy(unit->name, sizeof(unit->name), name);
                }
                continue;
            }
            p = decl;
        }
        const char *imp = cpp_module_word(p, "import");
        if (!imp || *imp == '<' || *imp == '"') continue; /* header units are compiler-managed */
        char name[C_MAX_NAME];
        if (cpp_module_name(imp, name)) c__push(&unit->imports, name);
    }
    fclose(f);
    if (provider && !unit->name[0])
        die("%s must declare an exported named module", unit->source);
}

static const char *cpp_module_find_std(const C_Target *t) {
    if (t->std_module_source[0] && strcmp(t->std_module_source, "@auto@")) return t->std_module_source;
    const char *env = getenv("C_STD_MODULE_SOURCE");
    if (env && *env) return env;
    static const char *paths[] = {
        "/opt/homebrew/opt/llvm/share/libc++/v1/std.cppm",
        "/usr/local/share/libc++/v1/std.cppm",
        "/usr/share/libc++/v1/std.cppm",
        "/usr/local/opt/llvm/share/libc++/v1/std.cppm"
    };
    for (size_t i = 0; i < C_ARRAY_LEN(paths); ++i)
        if (file_exists(paths[i])) return paths[i];
    die("libc++ std.cppm not found; set C_STD_MODULE_SOURCE or pass a path to c_std_module()");
    return NULL;
}

static ptrdiff_t cpp_module_lookup(const CppModuleGraph *graph, const char *name) {
    for (size_t i = 0; i < graph->count; ++i)
        if (!strcmp(graph->units[i].name, name)) return (ptrdiff_t)i;
    return -1;
}

static void cpp_module_build(CppModuleGraph *graph, size_t index,
                             C_Build *b, C_Target *target, DepState states[], const Options *opt) {
    CppModule *unit = &graph->units[index];
    if (unit->state == 2) return;
    if (unit->state == 1) die("C++ module import cycle involving %s", unit->name);
    unit->state = 1;
    for (size_t i = 0; i < unit->imports.count; ++i) {
        const char *name = unit->imports.items[i];
        ptrdiff_t dependency = cpp_module_lookup(graph, name);
        if (dependency >= 0) cpp_module_build(graph, (size_t)dependency, b, target, states, opt);
        else if (!strcmp(name, "std"))
            die("module %s imports std; enable c_std_module() first", unit->name);
    }

    uint64_t fingerprint = hash_string(unit->name);
    uint64_t source_hash = 0;
    if (!compiler_hash_file_uncached(unit->source, &source_hash))
        die("cannot hash module source: %s", unit->source);
    fingerprint = hash_update(fingerprint, &source_hash, sizeof(source_hash));
    uint64_t compiler_identity = compiler_tool_identity(cpp_module_compiler());
    fingerprint = hash_update(fingerprint, &compiler_identity, sizeof(compiler_identity));
    const char *standard = unit->standard ? "-std=c++23" : "-std=c++20";
    fingerprint = hash_update(fingerprint, standard, strlen(standard));
    for (size_t i = 0; i < target->cflags.count; ++i)
        fingerprint = hash_update(fingerprint, target->cflags.items[i], strlen(target->cflags.items[i]));
    for (size_t i = 0; i < target->includes.count; ++i)
        fingerprint = hash_update(fingerprint, target->includes.items[i], strlen(target->includes.items[i]));
    for (size_t i = 0; i < unit->imports.count; ++i) {
        ptrdiff_t dependency = cpp_module_lookup(graph, unit->imports.items[i]);
        if (dependency < 0) continue;
        CppModule *imported = &graph->units[dependency];
        uint64_t hash = 0;
        if (!compiler_hash_file_uncached(imported->pcm, &hash))
            die("missing compiled module %s", imported->name);
        fingerprint = hash_update(fingerprint, &hash, sizeof(hash));
    }
    char key[17], name[40], directory[PATH_MAX], depfile[PATH_MAX];
    hash_u64_hex(fingerprint, key);
    path_join(directory, "build/.modules", target->name);
    mkdir_p(directory);
    snprintf(name, sizeof(name), "%s.pcm", key);
    path_join(unit->pcm, directory, name);
    snprintf(depfile, sizeof(depfile), "%s.d", unit->pcm);

    if (!compiler_depfile_fresh(unit->pcm, depfile, unit->source)) {
        StrVec args = {0};
        vec_push(&args, cpp_module_compiler());
        vec_push(&args, standard);
        if (unit->standard) vec_push(&args, "-stdlib=libc++");
        compiler_append_target_compile_flags(&args, target, b, states, unit->source);
        for (size_t i = 0; i < unit->imports.count; ++i) {
            ptrdiff_t dependency = cpp_module_lookup(graph, unit->imports.items[i]);
            if (dependency < 0) continue;
            CppModule *imported = &graph->units[dependency];
            char flag[PATH_MAX + C_MAX_NAME + 32];
            snprintf(flag, sizeof(flag), "-fmodule-file=%s=%s", imported->name, imported->pcm);
            vec_push(&args, flag);
        }
        vec_push(&args, "-MMD");
        vec_push(&args, "-MF");
        vec_push(&args, depfile);
        vec_push(&args, "-x");
        vec_push(&args, "c++-module");
        vec_push(&args, "--precompile");
        vec_push(&args, unit->source);
        vec_push(&args, "-o");
        vec_push(&args, unit->pcm);
        note("MODULE", "%s", unit->name);
        if (run_process(&args, opt->verbose, NULL) != 0)
            die("C++ module precompile failed: %s", unit->name);
        vec_free(&args);
    }
    unit->state = 2;
}

static CppModuleGraph cpp_module_prepare(StrVec *sources, C_Build *b,
                                         C_Target *target, DepState states[], const Options *opt) {
    CppModuleGraph graph = {0};
    bool has_modules = target->std_module_source[0] != '\0';
    for (size_t i = 0; i < sources->count; ++i)
        if (cpp_module_source(sources->items[i])) has_modules = true;
    if (!has_modules) return graph;

    graph.units = calloc(sources->count + 1, sizeof(*graph.units));
    if (!graph.units) die("out of memory allocating module graph");
    if (target->std_module_source[0]) {
        CppModule *unit = &graph.units[graph.count++];
        c__copy(unit->name, sizeof(unit->name), "std");
        c__copy(unit->source, sizeof(unit->source), cpp_module_find_std(target));
        if (!file_exists(unit->source)) die("std module source not found: %s", unit->source);
        unit->standard = true;
    }
    for (size_t i = 0; i < sources->count; ++i) {
        if (!cpp_module_source(sources->items[i])) continue;
        CppModule *unit = &graph.units[graph.count++];
        c__copy(unit->source, sizeof(unit->source), sources->items[i]);
        cpp_module_scan(unit, true);
        for (size_t j = 0; j + 1 < graph.count; ++j)
            if (!strcmp(graph.units[j].name, unit->name))
                die("module %s has multiple providers", unit->name);
    }
    for (size_t i = 0; i < graph.count; ++i)
        cpp_module_build(&graph, i, b, target, states, opt);
    for (size_t i = 0; i < graph.count; ++i) {
        char flag[PATH_MAX + C_MAX_NAME + 32];
        snprintf(flag, sizeof(flag), "-fmodule-file=%s=%s",
                 graph.units[i].name, graph.units[i].pcm);
        c__push(&graph.flags, flag);
    }
    return graph;
}

static void cpp_module_append_flags(StrVec *args, const CppModuleGraph *graph,
                                    const char *source) {
    if (!compiler_cpp_source(source)) return;
    if (graph->count) {
        vec_push(args, graph->units[0].standard ? "-std=c++23" : "-std=c++20");
        if (graph->units[0].standard) vec_push(args, "-stdlib=libc++");
    }
    for (size_t i = 0; i < graph->count; ++i) {
        if (!strcmp(source, graph->units[i].source)) continue;
        char flag[PATH_MAX + C_MAX_NAME + 32];
        snprintf(flag, sizeof(flag), "-fmodule-file=%s=%s",
                 graph->units[i].name, graph->units[i].pcm);
        vec_push(args, flag);
    }
}

static void cpp_module_free(CppModuleGraph *graph) {
    for (size_t i = 0; i < graph->count; ++i)
        free_c_list(&graph->units[i].imports);
    free(graph->units);
    free_c_list(&graph->flags);
    memset(graph, 0, sizeof(*graph));
}

#endif
