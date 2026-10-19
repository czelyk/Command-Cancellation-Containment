#ifndef RECOVERY_CONTRACT_H
#define RECOVERY_CONTRACT_H
#include <stdint.h>
#include <stdatomic.h>

/* Experimental embedded core. All authority is assigned by the trusted host.
 * The start callback is bounded, nonblocking, non-reentrant and performs the
 * final hardware initiation while the gate is held. Never enqueue after it.
 * BUSY requires a bounded scheduler retry; no operation spins on the lock.
 */
typedef enum {
    RC_OK, RC_BUSY, RC_DISARMED, RC_STALE, RC_EXPIRED, RC_DUPLICATE,
    RC_EXHAUSTED, RC_BAD_COMPLETION
} rc_status;
typedef struct {
    uint64_t generation, operation, deadline;
} rc_ticket;
typedef struct {
    atomic_flag lock;
    uint64_t generation, last_operation;
    uint32_t active;
    uint8_t armed, exhausted;
} rc_gate;
typedef void (*rc_start_fn)(void *);
void rc_init(rc_gate *g);
rc_status rc_arm(rc_gate *g, uint64_t *generation);
rc_status rc_revoke(rc_gate *g);
rc_status rc_start(rc_gate *g, const rc_ticket *ticket, uint64_t now,
                   rc_start_fn start, void *context);
rc_status rc_complete(rc_gate *g);
int rc_contained(rc_gate *g);
uint32_t rc_gate_bytes(void);
#endif
