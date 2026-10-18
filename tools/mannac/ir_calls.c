#include "ir_internal.h"
#include <assert.h>
#include <stdint.h>

/* Recursion (typed-IR step 10, docs/M2_PLAN.md "No recursion"): direct and mutual recursion are
 * rejected with a call-graph cycle check, after every body is built (E0235).
 *
 * The graph: one edge per call that names a function, recorded by the checker (Ctx.edges, caller
 * order). Tarjan's strongly connected components, ITERATIVE like the struct analysis (a chain of
 * functions each calling the next is as long as the input allows). A component with two or more
 * members, or a function that calls itself, is a cycle. ONE E0235 per cycle - per component, so two
 * cycles through the same function are one error - reported at the FIRST CALL, in source order, made
 * from the lowest-declaration-index function of the cycle to any function of the cycle: the call
 * to remove. A function outside the cycle that merely calls into it is silent: its own code is fine,
 * and the cycle is reported where it is. Reported in order of that lowest function. */

void ir_check_recursion(IrState *st, Ctx *cx) {
    size_t n = cx->n_fns;
    if (n == 0 || cx->n_edges == 0) return;

    /* CSR adjacency, by caller. Edges arrive in caller order (bodies are built in function order). */
    size_t *start = arena_alloc(st->arena, (n + 1) * sizeof(size_t), alignof(size_t));
    for (size_t i = 0; i <= n; i++) start[i] = 0;
    for (size_t e = 0; e < cx->n_edges; e++) start[cx->edges[e].caller + 1]++;
    for (size_t i = 0; i < n; i++) start[i + 1] += start[i];

    size_t *index = arena_alloc(st->arena, n * sizeof(size_t), alignof(size_t));
    size_t *low = arena_alloc(st->arena, n * sizeof(size_t), alignof(size_t));
    size_t *comp = arena_alloc(st->arena, n * sizeof(size_t), alignof(size_t));
    size_t *stack = arena_alloc(st->arena, n * sizeof(size_t), alignof(size_t));
    size_t *frame_node = arena_alloc(st->arena, n * sizeof(size_t), alignof(size_t));
    size_t *frame_edge = arena_alloc(st->arena, n * sizeof(size_t), alignof(size_t));
    bool *on_stack = arena_alloc(st->arena, n * sizeof(bool), alignof(bool));
    bool *cyclic = arena_alloc(st->arena, n * sizeof(bool), alignof(bool));
    size_t *lowest = arena_alloc(st->arena, n * sizeof(size_t), alignof(size_t)); /* per component id */
    for (size_t i = 0; i < n; i++) {
        index[i] = SIZE_MAX;
        on_stack[i] = false;
        cyclic[i] = false;
    }
    size_t counter = 0, sp = 0, frames = 0, n_comps = 0;

    for (size_t root = 0; root < n; root++) {
        if (index[root] != SIZE_MAX || start[root] == start[root + 1]) {
            if (index[root] == SIZE_MAX) { index[root] = counter++; comp[root] = SIZE_MAX; } /* a leaf function: no edges, never cyclic */
            continue;
        }
        index[root] = low[root] = counter++;
        stack[sp++] = root;
        on_stack[root] = true;
        frame_node[0] = root;
        frame_edge[0] = start[root];
        frames = 1;
        while (frames > 0) {
            size_t v = frame_node[frames - 1];
            if (frame_edge[frames - 1] < start[v + 1]) {
                size_t w = cx->edges[frame_edge[frames - 1]++].callee;
                if (index[w] == SIZE_MAX) {
                    index[w] = low[w] = counter++;
                    stack[sp++] = w;
                    on_stack[w] = true;
                    frame_node[frames] = w;
                    frame_edge[frames] = start[w];
                    frames++;
                } else if (on_stack[w] && index[w] < low[v]) {
                    low[v] = index[w];
                }
                continue;
            }
            frames--;
            if (frames > 0 && low[v] < low[frame_node[frames - 1]]) low[frame_node[frames - 1]] = low[v];
            if (low[v] != index[v]) continue;

            size_t base = sp;
            while (stack[base - 1] != v) base--;
            base--;
            size_t size = sp - base;
            bool self = false;
            for (size_t e = start[v]; size == 1 && e < start[v + 1]; e++) self = self || cx->edges[e].callee == v;
            size_t id = n_comps++;
            lowest[id] = stack[base];
            for (size_t k = base; k < sp; k++) {
                comp[stack[k]] = id;
                on_stack[stack[k]] = false;
                if (stack[k] < lowest[id]) lowest[id] = stack[k];
            }
            if (size > 1 || self) cyclic[lowest[id]] = true; /* mark the cycle at its lowest member */
            sp = base;
        }
    }

    /* Report in order of the lowest function of each cyclic component. */
    for (size_t f = 0; f < n; f++) {
        if (!cyclic[f]) continue;
        for (size_t e = start[f]; e < start[f + 1]; e++) {
            if (comp[cx->edges[e].callee] != comp[f]) continue;
            st->progress = cx->edges[e].span;
            ir_error(st, diag_ir_recursion(cx->edges[e].span));
            break;
        }
    }
}
