#ifndef C_GIT_FETCH_H
#define C_GIT_FETCH_H

static int git_cache_run(StrVec *args, const Options *opt) {
#ifdef _WIN32
    return run_vec(args, opt->verbose, NULL);
#else
    return run_process(args, opt->verbose, NULL);
#endif
}

static char *git_cache_capture(StrVec *args) {
#ifdef _WIN32
    return capture_argv(args->items, NULL);
#else
    return capture_process(args, NULL);
#endif
}

static void git_cache_ref(const char *requested, char out[64]) {
    uint64_t key = hash_update(1469598103934665603ULL, requested, strlen(requested));
    snprintf(out, 64, "refs/cbuild/%016llx", (unsigned long long)key);
}

static int git_fetch_requested(const char *mirror, const char *requested, bool partial, const Options *opt) {
    if (partial) {
        const char *settings[] = {"remote.origin.promisor", "remote.origin.partialclonefilter"};
        const char *values[] = {"true", "blob:none"};
        for (size_t i = 0; i < 2; ++i) {
            StrVec config = {0}; vec_push(&config, "git"); vec_push(&config, "--git-dir"); vec_push(&config, mirror);
            vec_push(&config, "config"); vec_push(&config, settings[i]); vec_push(&config, values[i]);
            int rc = git_cache_run(&config, opt); vec_free(&config);
            if (rc) return rc;
        }
    }
    char ref[64], spec[C_MAX_NAME + 80];
    git_cache_ref(requested, ref);
    if (snprintf(spec, sizeof(spec), "+%s:%s", requested, ref) >= (int)sizeof(spec)) return 1;
    StrVec fetch = {0}; vec_push(&fetch, "git"); vec_push(&fetch, "--git-dir"); vec_push(&fetch, mirror);
    vec_push(&fetch, "fetch"); vec_push(&fetch, "--depth=1"); vec_push(&fetch, "--no-tags");
    vec_push(&fetch, "--no-recurse-submodules");
    if (partial) vec_push(&fetch, "--filter=blob:none");
    vec_push(&fetch, "origin"); vec_push(&fetch, spec);
    int rc = git_cache_run(&fetch, opt); vec_free(&fetch);
    return rc;
}

static char *git_resolve_requested(const char *mirror, const char *requested) {
    char ref[64], expression[80]; git_cache_ref(requested, ref);
    snprintf(expression, sizeof(expression), "%s^{commit}", ref);
    StrVec rev = {0}; vec_push(&rev, "git"); vec_push(&rev, "--git-dir"); vec_push(&rev, mirror);
    vec_push(&rev, "rev-parse"); vec_push(&rev, "--verify"); vec_push(&rev, expression);
    char *resolved = git_cache_capture(&rev); vec_free(&rev);
    return resolved;
}

static int git_pin_commit(const char *mirror, const char *resolved, const Options *opt) {
    char ref[128];
    if (snprintf(ref, sizeof(ref), "refs/cbuild/locked/%s", resolved) >= (int)sizeof(ref)) return 1;
    StrVec pin = {0}; vec_push(&pin, "git"); vec_push(&pin, "--git-dir"); vec_push(&pin, mirror);
    vec_push(&pin, "update-ref"); vec_push(&pin, ref); vec_push(&pin, resolved);
    int rc = git_cache_run(&pin, opt); vec_free(&pin);
    return rc;
}

/* An empty bare cache plus one explicit refspec avoids downloading every
 * branch/tag and supports commit SHA pins without cloning a default branch. */
static int git_init_requested(const char *mirror, const char *url, const char *requested, bool partial, const Options *opt) {
    StrVec init = {0}; vec_push(&init, "git"); vec_push(&init, "init"); vec_push(&init, "--bare");
    vec_push(&init, "--quiet"); vec_push(&init, mirror);
    int rc = git_cache_run(&init, opt); vec_free(&init);
    if (rc) return rc;
    StrVec remote = {0}; vec_push(&remote, "git"); vec_push(&remote, "--git-dir"); vec_push(&remote, mirror);
    vec_push(&remote, "remote"); vec_push(&remote, "add"); vec_push(&remote, "origin"); vec_push(&remote, url);
    rc = git_cache_run(&remote, opt); vec_free(&remote);
    if (rc) return rc;
    rc = git_fetch_requested(mirror, requested, partial, opt);
    if (rc) return rc;
    char *resolved = git_resolve_requested(mirror, requested);
    if (!resolved) return 1;
    StrVec head = {0}; vec_push(&head, "git"); vec_push(&head, "--git-dir"); vec_push(&head, mirror);
    vec_push(&head, "update-ref"); vec_push(&head, "--no-deref"); vec_push(&head, "HEAD"); vec_push(&head, resolved);
    rc = git_cache_run(&head, opt); vec_free(&head); free(resolved);
    return rc;
}

#endif
