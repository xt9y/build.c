#ifndef C_DEPENDENCY_PATHS_H
#define C_DEPENDENCY_PATHS_H

/* Resolve @dependency@ after Git checkouts are available. Commands receive
 * shell-quoted roots; ordinary path fields receive the literal path. */
static void dependency_path_append(char **out, size_t *length, const char *text, size_t count) {
    if (count > SIZE_MAX - *length - 1) die("dependency path is too long");
    char *next = realloc(*out, *length + count + 1);
    if (!next) die("out of memory expanding dependency path");
    memcpy(next + *length, text, count);
    *length += count;
    next[*length] = '\0';
    *out = next;
}

static char *expand_dependency_path(const char *value, C_Build *b, DepState states[], bool command) {
    char *out = NULL;
    size_t length = 0;
    const char *cursor = value;
    while (*cursor) {
        const char *open = strchr(cursor, '@');
        const char *close = open ? strchr(open + 1, '@') : NULL;
        if (!close) break;
        dependency_path_append(&out, &length, cursor, (size_t)(open - cursor));
        size_t name_length = (size_t)(close - open - 1);
        size_t index = 0;
        for (; index < b->dep_count; ++index)
            if (strlen(b->deps[index].name) == name_length &&
                !strncmp(b->deps[index].name, open + 1, name_length)) break;
        if (index == b->dep_count) die("unknown dependency path: %.*s", (int)name_length, open + 1);

        char root[PATH_MAX];
        C_Dependency *dep = &b->deps[index];
        if (dep->subdir[0]) path_join(root, states[index].source, dep->subdir);
        else c__copy(root, sizeof(root), states[index].source);
        if (command) dependency_path_append(&out, &length, "'", 1);
        for (const char *p = root; *p; ++p) {
            if (command && *p == '\'') {
#ifdef _WIN32
                dependency_path_append(&out, &length, "''", 2);
#else
                dependency_path_append(&out, &length, "'\\''", 4);
#endif
            } else dependency_path_append(&out, &length, p, 1);
        }
        if (command) dependency_path_append(&out, &length, "'", 1);
        cursor = close + 1;
    }
    dependency_path_append(&out, &length, cursor, strlen(cursor));
    return out;
}

static void expand_dependency_paths(C_Build *b, DepState states[]) {
    for (size_t i = 0; i < b->target_count; ++i) {
        C_Target *target = &b->targets[i];
        C_StringList *lists[] = {&target->sources, &target->includes,
            &target->generated_outputs, &target->generated_inputs, &target->generated_commands};
        for (size_t list = 0; list < sizeof(lists) / sizeof(lists[0]); ++list) {
            for (size_t item = 0; item < lists[list]->count; ++item) {
                char *previous = lists[list]->items[item];
                lists[list]->items[item] = expand_dependency_path(previous, b, states,
                    lists[list] == &target->generated_commands);
                free(previous);
            }
        }
    }
}

#endif
