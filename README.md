# Command Cancellation and Effect Containment

> **Status: archived research experiment (NO-GO).** This repository documents a
> reproducible investigation. It does not claim a new scientific contribution,
> a previously unknown F Prime defect, or flight-qualified software.

## Overview

This experiment studied whether commands issued by a sandboxed spacecraft
sequence can still start or complete after cancellation has been acknowledged.
It combines F Prime integration tests, a small C11 generation gate, and an
exhaustive event-order model of one bounded command/cancellation scenario.

The investigation was closed as a paper topic because fencing, staged
cancellation, and irreversibility are established concepts. The observed
16-bit response-identifier wraparound behavior is also documented by F Prime in
its WasmSequencer timeout design notes. Reproducing it remains useful as a
regression and systems-integration exercise.

## Recorded results

- 218 upstream F Prime tests passed at the pinned revision.
- Six local integration experiments passed using the real WasmSequencer and
  CmdDispatcher with an explicitly simulated downstream effect sink.
- A 65,537-run test reproduced response-identifier aliasing after wraparound.
- The portable C11 gate passed native and address/undefined-behavior sanitizer
  checks on the original test host.
- The bounded model distinguishes policies that can acknowledge false
  quiescence from an atomic commit/barrier policy.

Schedule counts produced by the model are not probabilities. The C gate is a
reference experiment, not a flight-qualified driver.

## Repository layout

| Path | Contents |
|---|---|
| `src/RecoveryContractTests.hpp` | Local F Prime integration tests |
| `src/fprime-tests.patch` | Patch that wires the tests into the pinned F Prime tree |
| `src/contract.*` | Portable C11 reference gate |
| `src/test_contract.c` | Boundary assertions for the reference gate |
| `src/model_check.py` | Exhaustive model for the bounded event-order scenario |
| `evidence/` | Sanitized logs, test reports, and model output from the archived run |

## Reproduce the standalone checks

Requirements: a C11 compiler and Python 3.

```bash
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  src/contract.c src/test_contract.c -o contract_test
./contract_test
python3 src/model_check.py
```

The model rewrites `evidence/model_results.json` with deterministic output.

## Reproduce the F Prime checks

The archived run used:

- F Prime commit `76e67d0be62fe3697c27239a988ae60704fbaeaf`
- `spacewasm` 0.7.1
- `fprime-tools` 4.4.0 and `fprime-fpp` 3.4.0
- GCC 13 and Rust 1.90.0 on Linux x86-64

Clone F Prime at the pinned commit, initialize its test dependencies, apply
`src/fprime-tests.patch`, copy `src/RecoveryContractTests.hpp` into
`Svc/WasmSequencer/test/ut/`, generate the unit-test build, and run the
`RecoveryBoundaryTester.*` tests. The patch is intentionally kept separate from
the upstream source tree.

## Limitations and provenance

- The downstream effect sink is simulated; no physical spacecraft hardware was
  tested.
- The finite model covers one command and one cancellation, not arbitrary
  execution.
- Historical logs are retained as evidence and have only been sanitized to
  remove workstation-specific absolute paths.
- F Prime and its documentation remain the work of their respective authors.
- The investigation was prepared with AI assistance and archived on
  16 October 2026.

See `THIRD_PARTY_NOTICES.md` for upstream attribution and `LICENSE.md` for the
licensing status of this archive.

# Command-Cancellation-Containment
