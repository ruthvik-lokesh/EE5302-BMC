#include "fsm.h"
#include "cnf.h"
#include "bmc.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <array>
#include <stdexcept>

// ============================================================
// Utility: run SAT solver and capture output
// ============================================================
// Calls Rory's SAT solver (or any DIMACS-compatible solver like minisat).
// The solver should print:
//   s SATISFIABLE / s UNSATISFIABLE
//   v <assignments> 0   (if SAT)
//
// Usage: ./sat_solver <cnf_file>   (adjust command below to match Rory's interface)

std::string run_solver(const std::string& cnf_file,
                       const std::string& solver_cmd = "./sat_solver") {
    std::string cmd = solver_cmd + " " + cnf_file + " 2>&1";
    printf("[SOLVER] Running: %s\n", cmd.c_str());

    // popen to capture output
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        // If solver not available, just return empty (user can run manually)
        printf("[SOLVER] Could not run solver. Run manually:\n");
        printf("  %s\n", cmd.c_str());
        return "";
    }

    std::string result;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe))
        result += buf;
    pclose(pipe);
    return result;
}

// ============================================================
// Print a BMC result
// ============================================================

void print_result(const BMCResult& res, const FSM& fsm, int k) {
    printf("\n============================================================\n");
    if (!res.sat) {
        printf("RESULT: UNSAT — Property HOLDS up to bound k=%d\n", k);
        printf("  No bad state is reachable within %d steps.\n", k);
    } else {
        printf("RESULT: SAT — Counterexample FOUND!\n");
        printf("  Bad state reached at step %d\n", res.counterexample_step);
        printf("\nCounterexample Trace:\n");
        for (int t = 0; t <= k; t++) {
            int s = res.state_trace[t];
            if (s == -1) {
                printf("  t=%d : (unknown)\n", t);
            } else {
                printf("  t=%d : State %d (%s)%s\n",
                    t, s,
                    fsm.state_names[s].c_str(),
                    fsm.bad_states[s] ? " *** BAD STATE ***" : "");
            }
        }
    }
    printf("  CNF file: %s\n", res.dimacs_file.c_str());
    printf("============================================================\n\n");
}

// ============================================================
// FSM 1: Toy FSM — Simple Traffic Light Controller
// ============================================================
//
// States:
//   0: RED       (initial)
//   1: GREEN
//   2: YELLOW    (bad state in broken version — should never go RED→YELLOW directly)
//
// Correct (working) transitions:
//   RED → GREEN → YELLOW → RED  (cycle)
//
// Broken transitions:
//   RED → YELLOW (skip GREEN — violation!)
//   GREEN → YELLOW
//   YELLOW → RED
//
// Property: "YELLOW state is only reached from GREEN"
// We model this by making YELLOW a bad state only in paths where
// we came from RED. For simplicity in this encoding, we model the
// BROKEN version by adding the direct RED→YELLOW transition,
// and the SAT solver will find the counterexample.

FSM make_traffic_light_working() {
    FSM fsm(3);
    fsm.set_name(0, "RED");
    fsm.set_name(1, "GREEN");
    fsm.set_name(2, "YELLOW");

    fsm.set_initial(0);           // Start at RED

    // Correct cycle
    fsm.add_transition(0, 1);    // RED → GREEN
    fsm.add_transition(1, 2);    // GREEN → YELLOW
    fsm.add_transition(2, 0);    // YELLOW → RED

    // No bad states — all states are reachable correctly
    // We'll check a DIFFERENT property: can we reach a "STUCK" state?
    // For the working FSM, we add a STUCK state that is unreachable.
    // (Alternatively: check that we never reach state 1 from state 2 directly)

    // For demo: mark a hypothetical "ERROR" state (state that doesn't exist
    // in a well-formed system). Here we just verify no bad states are hit.
    // We'll make YELLOW bad in the BROKEN version only.
    return fsm;
}

FSM make_traffic_light_broken() {
    FSM fsm(4);  // Added an ERROR state
    fsm.set_name(0, "RED");
    fsm.set_name(1, "GREEN");
    fsm.set_name(2, "YELLOW");
    fsm.set_name(3, "ERROR");

    fsm.set_initial(0);

    // Broken: RED can go directly to YELLOW (skipping GREEN)
    fsm.add_transition(0, 1);    // RED → GREEN (also possible)
    fsm.add_transition(0, 3);    // RED → ERROR (the bug! should never happen)
    fsm.add_transition(1, 2);    // GREEN → YELLOW
    fsm.add_transition(2, 0);    // YELLOW → RED
    fsm.add_transition(3, 3);    // ERROR stays in ERROR (absorbing)

    fsm.set_bad(3);               // ERROR state is bad

    return fsm;
}

// ============================================================
// FSM 2: Moderate — Simple Arbiter (2-client mutual exclusion)
// ============================================================
//
// Models a bus arbiter for 2 clients (C0 and C1).
// States encode (who holds the grant, if anyone):
//   0: IDLE       — no client holds grant
//   1: GRANT_C0   — client 0 holds grant
//   2: GRANT_C1   — client 1 holds grant
//   3: BOTH_GRANT — BUG: both clients have grant simultaneously (UNSAFE)
//
// Working arbiter: from IDLE, grant to C0 or C1; then release back to IDLE.
//   IDLE → GRANT_C0 → IDLE
//   IDLE → GRANT_C1 → IDLE
//   (Never BOTH_GRANT)
//
// Broken arbiter: from GRANT_C0 there's a path to BOTH_GRANT.

FSM make_arbiter_working() {
    FSM fsm(4);
    fsm.set_name(0, "IDLE");
    fsm.set_name(1, "GRANT_C0");
    fsm.set_name(2, "GRANT_C1");
    fsm.set_name(3, "BOTH_GRANT");

    fsm.set_initial(0);

    fsm.add_transition(0, 1);    // IDLE → GRANT_C0
    fsm.add_transition(0, 2);    // IDLE → GRANT_C1
    fsm.add_transition(1, 0);    // GRANT_C0 → IDLE (release)
    fsm.add_transition(2, 0);    // GRANT_C1 → IDLE (release)
    // BOTH_GRANT has no incoming edges from working states

    fsm.set_bad(3);               // Mutual exclusion violation

    return fsm;
}

FSM make_arbiter_broken() {
    FSM fsm(4);
    fsm.set_name(0, "IDLE");
    fsm.set_name(1, "GRANT_C0");
    fsm.set_name(2, "GRANT_C1");
    fsm.set_name(3, "BOTH_GRANT");

    fsm.set_initial(0);

    fsm.add_transition(0, 1);    // IDLE → GRANT_C0
    fsm.add_transition(0, 2);    // IDLE → GRANT_C1
    fsm.add_transition(1, 0);    // GRANT_C0 → IDLE
    fsm.add_transition(1, 3);    // GRANT_C0 → BOTH_GRANT (BUG!)
    fsm.add_transition(2, 0);    // GRANT_C1 → IDLE
    fsm.add_transition(3, 3);    // Stays broken

    fsm.set_bad(3);

    return fsm;
}

// ============================================================
// FSM 3: Non-trivial — 3-Stage Pipeline with Hazard Detection
// ============================================================
//
// Models a simplified 3-stage instruction pipeline:
//   Stage: FETCH → DECODE → EXECUTE
//
// States encode the pipeline status:
//   0: EMPTY          — pipeline is empty (initial)
//   1: FETCHING       — instruction in fetch stage
//   2: DECODING       — instruction in decode stage
//   3: EXECUTING      — instruction in execute stage
//   4: STALL          — pipeline stalled due to hazard
//   5: HAZARD_ERROR   — data hazard not handled (BAD)
//   6: FLUSH          — pipeline flush in progress
//   7: FLUSHED        — pipeline successfully flushed (safe recovery)
//
// Working pipeline: hazard → stall → resume
//   EMPTY → FETCHING → DECODING → EXECUTING → EMPTY (cycle)
//   DECODING → STALL → FETCHING (stall resolves)
//   STALL → FLUSH → FLUSHED → EMPTY
//
// Broken pipeline: hazard reaches EXECUTE without stalling → HAZARD_ERROR

FSM make_pipeline_working() {
    FSM fsm(8);
    fsm.set_name(0, "EMPTY");
    fsm.set_name(1, "FETCHING");
    fsm.set_name(2, "DECODING");
    fsm.set_name(3, "EXECUTING");
    fsm.set_name(4, "STALL");
    fsm.set_name(5, "HAZARD_ERROR");
    fsm.set_name(6, "FLUSH");
    fsm.set_name(7, "FLUSHED");

    fsm.set_initial(0);

    // Normal pipeline flow
    fsm.add_transition(0, 1);   // EMPTY → FETCHING
    fsm.add_transition(1, 2);   // FETCHING → DECODING
    fsm.add_transition(2, 3);   // DECODING → EXECUTING
    fsm.add_transition(3, 0);   // EXECUTING → EMPTY (complete)

    // Hazard handling: DECODING detects hazard → STALL
    fsm.add_transition(2, 4);   // DECODING → STALL
    fsm.add_transition(4, 6);   // STALL → FLUSH
    fsm.add_transition(6, 7);   // FLUSH → FLUSHED
    fsm.add_transition(7, 0);   // FLUSHED → EMPTY (safe)

    // HAZARD_ERROR is unreachable in working version
    fsm.set_bad(5);              // Unhandled hazard

    return fsm;
}

FSM make_pipeline_broken() {
    FSM fsm(8);
    fsm.set_name(0, "EMPTY");
    fsm.set_name(1, "FETCHING");
    fsm.set_name(2, "DECODING");
    fsm.set_name(3, "EXECUTING");
    fsm.set_name(4, "STALL");
    fsm.set_name(5, "HAZARD_ERROR");
    fsm.set_name(6, "FLUSH");
    fsm.set_name(7, "FLUSHED");

    fsm.set_initial(0);

    // Normal pipeline flow
    fsm.add_transition(0, 1);
    fsm.add_transition(1, 2);
    fsm.add_transition(2, 3);
    fsm.add_transition(3, 0);

    // BUG: EXECUTING can directly reach HAZARD_ERROR
    // (hazard not detected at decode, propagates to execute)
    fsm.add_transition(3, 5);   // EXECUTING → HAZARD_ERROR (BUG!)
    fsm.add_transition(5, 5);   // Stays in error

    // Stall logic present but bypassed by bug
    fsm.add_transition(2, 4);
    fsm.add_transition(4, 6);
    fsm.add_transition(6, 7);
    fsm.add_transition(7, 0);

    fsm.set_bad(5);

    return fsm;
}

// ============================================================
// Run one BMC check
// ============================================================

void run_bmc(const std::string& name, FSM& fsm, int k,
             const std::string& cnf_out,
             const std::string& solver_cmd = "") {
    printf("\n============================================================\n");
    printf("BMC Check: %s (k=%d)\n", name.c_str(), k);
    printf("============================================================\n");
    fsm.print();

    BMC bmc(fsm, k);
    bmc.encode(cnf_out);

    if (solver_cmd.empty()) {
        printf("\n[INFO] No solver specified. Run manually:\n");
        printf("  ./sat_solver %s\n", cnf_out.c_str());
        printf("  Then feed the output back to parse_sat_output().\n");
        return;
    }

    std::string solver_out = run_solver(cnf_out, solver_cmd);
    if (solver_out.empty()) return;

    printf("[SOLVER] Output:\n%s\n", solver_out.c_str());
    BMCResult res = bmc.parse_sat_output(solver_out, cnf_out);
    print_result(res, fsm, k);
}

// ============================================================
// Main
// ============================================================

int main(int argc, char* argv[]) {
    // Optional: pass solver command as argument
    // e.g., ./bmc_main ./sat_solver
    //        ./bmc_main minisat       (if minisat is installed)
    std::string solver_cmd = "";
    if (argc >= 2) solver_cmd = argv[1];

    printf("==========================================================\n");
    printf("  Bounded Model Checker (BMC) — EE5202 Term Project\n");
    printf("  Authors: Ruthvik Lokesh Godwa, Rory Donaghy\n");
    printf("  University of Minnesota\n");
    printf("==========================================================\n\n");

    // -------------------------------------------------------
    // FSM 1: Traffic Light — Working (expect UNSAT)
    // -------------------------------------------------------
    {
        FSM fsm = make_traffic_light_working();
        // Working FSM has no bad states — we need to add one to check
        // Let's add an unreachable "ERROR" state and verify it stays unreachable
        // Actually for the working version: verify the cycle never breaks.
        // Re-use the broken version structure but fix the transitions.
        // For a meaningful UNSAT check, we create: can we reach state 3 (ERROR)?
        // State 3 is added but no transition leads there.

        // The working FSM has 3 states with no bad states.
        // Let's make YELLOW (state 2) bad and verify it IS reachable in the
        // working version (it should be, SAT expected at step 2)
        // This demos SAT result.
        fsm.set_bad(2);  // YELLOW is "bad" in our property check
        run_bmc("Traffic Light (WORKING — YELLOW reachable at k=3?)",
                fsm, 3, "traffic_working.cnf", solver_cmd);
    }

    // -------------------------------------------------------
    // FSM 1: Traffic Light — Broken (expect SAT, ERROR reachable at step 1)
    // -------------------------------------------------------
    {
        FSM fsm = make_traffic_light_broken();
        run_bmc("Traffic Light (BROKEN — ERROR reachable?)",
                fsm, 3, "traffic_broken.cnf", solver_cmd);
    }

    // -------------------------------------------------------
    // FSM 2: Arbiter — Working (expect UNSAT)
    // -------------------------------------------------------
    {
        FSM fsm = make_arbiter_working();
        run_bmc("Arbiter (WORKING — BOTH_GRANT reachable? Expect UNSAT)",
                fsm, 5, "arbiter_working.cnf", solver_cmd);
    }

    // -------------------------------------------------------
    // FSM 2: Arbiter — Broken (expect SAT)
    // -------------------------------------------------------
    {
        FSM fsm = make_arbiter_broken();
        run_bmc("Arbiter (BROKEN — BOTH_GRANT reachable? Expect SAT)",
                fsm, 5, "arbiter_broken.cnf", solver_cmd);
    }

    // -------------------------------------------------------
    // FSM 3: Pipeline — Working (expect UNSAT)
    // -------------------------------------------------------
    {
        FSM fsm = make_pipeline_working();
        run_bmc("Pipeline (WORKING — HAZARD_ERROR reachable? Expect UNSAT)",
                fsm, 8, "pipeline_working.cnf", solver_cmd);
    }

    // -------------------------------------------------------
    // FSM 3: Pipeline — Broken (expect SAT)
    // -------------------------------------------------------
    {
        FSM fsm = make_pipeline_broken();
        run_bmc("Pipeline (BROKEN — HAZARD_ERROR reachable? Expect SAT)",
                fsm, 8, "pipeline_broken.cnf", solver_cmd);
    }

    printf("\n[BMC] All .cnf files generated. Run with your SAT solver:\n");
    printf("  %s <file>.cnf\n", solver_cmd.empty() ? "./sat_solver" : solver_cmd.c_str());

    return 0;
}
