#include "contract.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
static void add(void *p) { (*(uint32_t *)p)++; }
static void revoke_during_start(void *p) {
    assert(rc_revoke((rc_gate *)p) == RC_BUSY);
    assert(!rc_contained((rc_gate *)p));
}
int main(void) {
    rc_gate g;
    uint64_t epoch = 0;
    uint32_t effects = 0;
    rc_init(&g);
    assert(rc_contained(&g));
    assert(rc_arm(&g, &epoch) == RC_OK);
    rc_ticket t = {epoch, 1, 100};
    assert(rc_start(&g, &t, 100, add, &effects) == RC_OK);
    assert(rc_start(&g, &t, 100, add, &effects) == RC_DUPLICATE);
    assert(rc_revoke(&g) == RC_OK);
    assert(!rc_contained(&g));
    assert(rc_start(&g, &t, 100, add, &effects) == RC_DISARMED);
    assert(rc_arm(&g, &epoch) == RC_BUSY);
    assert(rc_complete(&g) == RC_OK);
    assert(rc_contained(&g));
    assert(rc_complete(&g) == RC_BAD_COMPLETION);
    assert(rc_arm(&g, &epoch) == RC_OK);
    assert(rc_start(&g, &t, 0, add, &effects) == RC_STALE);
    t.generation = epoch;
    assert(rc_start(&g, &t, 101, add, &effects) == RC_EXPIRED);
    assert(rc_start(&g, &t, 100, revoke_during_start, &g) == RC_OK);
    assert(rc_complete(&g) == RC_OK);
    assert(effects == 1);
    g.generation = UINT64_MAX;
    assert(rc_revoke(&g) == RC_EXHAUSTED);
    assert(rc_contained(&g));
    assert(rc_arm(&g, &epoch) == RC_EXHAUSTED);
    printf("core boundary assertions passed; gate_bytes=%u\n", rc_gate_bytes());
}
