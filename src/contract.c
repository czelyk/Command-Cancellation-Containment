#include "contract.h"
#include <limits.h>

static int enter(rc_gate *g) {
    return !atomic_flag_test_and_set_explicit(&g->lock, memory_order_acquire);
}
static void leave(rc_gate *g) {
    atomic_flag_clear_explicit(&g->lock, memory_order_release);
}
void rc_init(rc_gate *g) {
    atomic_flag_clear(&g->lock);
    g->generation = 1;
    g->last_operation = 0;
    g->active = 0;
    g->armed = 0;
    g->exhausted = 0;
}
rc_status rc_arm(rc_gate *g, uint64_t *generation) {
    if (!enter(g)) return RC_BUSY;
    rc_status result = RC_OK;
    if (g->exhausted) result = RC_EXHAUSTED;
    else if (g->active || g->armed) result = RC_BUSY;
    else {
        g->armed = 1;
        *generation = g->generation;
    }
    leave(g);
    return result;
}
rc_status rc_revoke(rc_gate *g) {
    if (!enter(g)) return RC_BUSY;
    g->armed = 0;
    rc_status result = RC_OK;
    if (g->generation == UINT64_MAX) {
        g->exhausted = 1;
        result = RC_EXHAUSTED;
    } else {
        g->generation++;
        g->last_operation = 0;
    }
    leave(g);
    return result;
}
rc_status rc_start(rc_gate *g, const rc_ticket *t, uint64_t now,
                   rc_start_fn start, void *context) {
    if (!enter(g)) return RC_BUSY;
    rc_status result = RC_OK;
    if (!g->armed) result = RC_DISARMED;
    else if (t->generation != g->generation) result = RC_STALE;
    else if (now > t->deadline) result = RC_EXPIRED;
    else if (t->operation <= g->last_operation) result = RC_DUPLICATE;
    else if (g->active == UINT32_MAX) result = RC_EXHAUSTED;
    else {
        g->last_operation = t->operation;
        g->active++;
        start(context);
    }
    leave(g);
    return result;
}
rc_status rc_complete(rc_gate *g) {
    if (!enter(g)) return RC_BUSY;
    rc_status result = RC_OK;
    if (!g->active) result = RC_BAD_COMPLETION;
    else g->active--;
    leave(g);
    return result;
}
int rc_contained(rc_gate *g) {
    if (!enter(g)) return 0;
    int result = !g->armed && g->active == 0;
    leave(g);
    return result;
}
uint32_t rc_gate_bytes(void) { return (uint32_t)sizeof(rc_gate); }
