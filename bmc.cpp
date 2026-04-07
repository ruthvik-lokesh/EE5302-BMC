#include "bmc.h"
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <algorithm>

// ============================================================
// Constructor
// ============================================================

BMC::BMC(const FSM& fsm, int k) : fsm(fsm), k(k) {
    // Allocate all state variables in one block
    // Layout: time step 0 uses vars [var_base .. var_base+N-1]
    //         time step 1 uses vars [var_base+N .. var_base+2N-1]
    //         ...
    //         time step t uses vars [var_base + t*N .. var_base + t*N + N-1]
    var_base = cnf.new_vars(fsm.num_states * (k + 1));

    // Allocate one auxiliary variable per time step for "bad state at t"
    bad_var.resize(k + 1);
    for (int t = 0; t <= k; t++)
        bad_var[t] = cnf.new_var();
}

// ============================================================
// Variable mapping
// ============================================================

int BMC::state_var(int state, int time) const {
    // state in [0, N), time in [0, k]
    return var_base + time * fsm.num_states + state;
}

// ============================================================
// Step 1: Initial state encoding
// ============================================================
//
// At time 0, the FSM starts in one of the designated initial states.
//
// Constraint A — at least one initial state is active:
//   (s[i0][0] OR s[i1][0] OR ...)    where i0, i1, ... are initial states
//
// Constraint B — exactly one state is active (one-hot at t=0):
//   ALO: already covered by constraint A if only initials can be true
//   AMO: for all pairs (i, j) with i != j: (NOT s[i][0] OR NOT s[j][0])
//   We also need to force non-initial states to be false at t=0.
//
// Simplest correct encoding:
//   - Force each non-initial state to FALSE at t=0
//   - Add ALO over initial states
//   - Add AMO over initial states (in case there are multiple)

void BMC::encode_initial() {
    int N = fsm.num_states;

    // Force non-initial states to false at t=0
    for (int i = 0; i < N; i++) {
        if (!fsm.initial_states[i]) {
            cnf.force_false(state_var(i, 0));
        }
    }

    // ALO: at least one initial state is active at t=0
    std::vector<int> init_lits;
    for (int i = 0; i < N; i++) {
        if (fsm.initial_states[i])
            init_lits.push_back(state_var(i, 0));
    }
    if (init_lits.empty())
        throw std::runtime_error("FSM has no initial states!");
    cnf.add_alo(init_lits);

    // AMO over initial states (in case there are multiple initial states)
    cnf.add_amo(init_lits);
}

// ============================================================
// Step 2: Transition encoding for one time step t -> t+1
// ============================================================
//
// For each state i, if we are in state i at time t, then at time t+1
// we must be in one of the successors of i.
//
// Encoding per state i:
//   (NOT s[i][t]) OR (s[j0][t+1] OR s[j1][t+1] OR ...)
//   where j0, j1, ... are successors of i in the FSM.
//
// If state i has NO transitions (a deadlock state), we add:
//   (NOT s[i][t])   → we can never be in state i (or the formula is UNSAT)
//   This correctly models a stuck system.
//
// We also enforce one-hot at t+1:
//   Exactly one state is active at t+1.
//
// One-hot is encoded globally (not per-transition) for clarity.

void BMC::encode_transition(int t) {
    int N = fsm.num_states;

    // For each state i: if active at t, successor must be active at t+1
    for (int i = 0; i < N; i++) {
        const auto& succs = fsm.transitions[i];

        if (succs.empty()) {
            // Deadlock state: cannot be active (makes this branch UNSAT)
            // (NOT s[i][t])
            cnf.force_false(state_var(i, t));
        } else {
            // Build clause: (NOT s[i][t]) OR s[j0][t+1] OR s[j1][t+1] OR ...
            std::vector<int> clause;
            clause.push_back(-state_var(i, t));   // antecedent negated
            for (int j : succs)
                clause.push_back(state_var(j, t + 1));
            cnf.add_clause(clause);
        }
    }

    // One-hot constraint at t+1
    encode_one_hot(t + 1);
}

// ============================================================
// One-hot encoding at a given time step
// ============================================================
// Exactly one state variable is true at time t.

void BMC::encode_one_hot(int t) {
    int N = fsm.num_states;
    std::vector<int> vars;
    for (int i = 0; i < N; i++)
        vars.push_back(state_var(i, t));
    cnf.add_exactly_one(vars);
}

// ============================================================
// Step 3: Bad state encoding (negation of safety property)
// ============================================================
//
// We want to CHECK: "is there a path that reaches a bad state within k steps?"
// So we ADD to the formula: "some bad state IS reached at some time step"
//
// For each time step t in [0..k]:
//   bad_var[t] ↔ (s[b0][t] OR s[b1][t] OR ...)   where b0,b1,... are bad states
//
// We encode the implication both ways:
//   Forward:  if any bad state is active at t → bad_var[t] is true
//             (s[bi][t]) → bad_var[t]   i.e., (NOT s[bi][t]) OR bad_var[t]
//   Backward: if bad_var[t] is true → some bad state is active at t
//             bad_var[t] → (s[b0][t] OR s[b1][t] OR ...)
//             i.e., (NOT bad_var[t]) OR s[b0][t] OR s[b1][t] OR ...
//
// Finally, we add the clause: (bad_var[0] OR bad_var[1] OR ... OR bad_var[k])
// This forces the SAT solver to find a time step where a bad state is reached.

void BMC::encode_bad_states() {
    int N = fsm.num_states;

    // Collect bad state indices
    std::vector<int> bad_states;
    for (int i = 0; i < N; i++)
        if (fsm.bad_states[i])
            bad_states.push_back(i);

    if (bad_states.empty())
        throw std::runtime_error("FSM has no bad states! Nothing to check.");

    for (int t = 0; t <= k; t++) {
        // Forward: s[bi][t] → bad_var[t]
        for (int bi : bad_states)
            cnf.add_implication(state_var(bi, t), bad_var[t]);

        // Backward: bad_var[t] → (s[b0][t] OR s[b1][t] OR ...)
        std::vector<int> back_clause;
        back_clause.push_back(-bad_var[t]);
        for (int bi : bad_states)
            back_clause.push_back(state_var(bi, t));
        cnf.add_clause(back_clause);
    }

    // At least one bad_var must be true (we're looking for a counterexample)
    std::vector<int> bad_lits;
    for (int t = 0; t <= k; t++)
        bad_lits.push_back(bad_var[t]);
    cnf.add_alo(bad_lits);
}

// ============================================================
// Main encode function
// ============================================================

std::string BMC::encode(const std::string& output_file) {
    printf("[BMC] Encoding FSM with %d states, bound k=%d\n",
           fsm.num_states, k);

    // Step 1: Initial states
    printf("[BMC] Step 1: Encoding initial states...\n");
    encode_initial();

    // One-hot at t=0 (initial step — force all non-initials already done,
    // but we still need AMO+ALO which encode_initial handles)
    // We add AMO/ALO for ALL states at t=0 to be safe
    encode_one_hot(0);

    // Step 2: Transitions for t = 0 to k-1
    printf("[BMC] Step 2: Encoding transitions for %d steps...\n", k);
    for (int t = 0; t < k; t++)
        encode_transition(t);

    // Step 3: Bad states
    printf("[BMC] Step 3: Encoding bad state reachability...\n");
    encode_bad_states();

    printf("[BMC] CNF formula: %d variables, %d clauses\n",
           cnf.num_vars, cnf.num_clauses());

    // Build a comment for the DIMACS file
    std::ostringstream comment;
    comment << "BMC encoding: " << fsm.num_states << " states, bound k=" << k << "\n";
    comment << "Variable layout:\n";
    for (int t = 0; t <= k; t++) {
        for (int i = 0; i < fsm.num_states; i++) {
            comment << "  var " << state_var(i, t)
                    << " = state " << i
                    << " (" << fsm.state_names[i] << ")"
                    << " at time " << t << "\n";
        }
    }
    for (int t = 0; t <= k; t++)
        comment << "  var " << bad_var[t] << " = bad_reached at time " << t << "\n";

    cnf.write_dimacs(output_file, comment.str());
    printf("[BMC] Wrote CNF to: %s\n", output_file.c_str());

    return output_file;
}

// ============================================================
// Parse SAT solver output
// ============================================================
//
// Standard DIMACS SAT solver output format:
//   s SATISFIABLE       (or s UNSATISFIABLE)
//   v 1 -2 3 -4 ... 0  (variable assignments, only if SAT)
//
// We read the assignments, then look up which state is active
// at each time step to reconstruct the trace.

BMCResult BMC::parse_sat_output(const std::string& sat_output,
                                const std::string& cnf_file) {
    BMCResult result;
    result.sat = false;
    result.counterexample_step = -1;
    result.dimacs_file = cnf_file;

    // Check SAT/UNSAT
    if (sat_output.find("SATISFIABLE") == std::string::npos &&
        sat_output.find("SAT") == std::string::npos) {
        result.sat = false;
        return result;
    }
    if (sat_output.find("UNSATISFIABLE") != std::string::npos ||
        sat_output.find("UNSAT") != std::string::npos) {
        result.sat = false;
        return result;
    }
    result.sat = true;

    // Parse variable assignments from "v ..." lines
    // assignment[v] = true if variable v is assigned TRUE
    std::vector<int> assignment(cnf.num_vars + 1, 0); // 0 = unset

    std::istringstream ss(sat_output);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty() || line[0] != 'v') continue;
        std::istringstream ls(line.substr(1));
        int lit;
        while (ls >> lit) {
            if (lit == 0) break;
            int var = std::abs(lit);
            if (var >= 1 && var <= cnf.num_vars)
                assignment[var] = (lit > 0) ? 1 : -1;
        }
    }

    // Extract state trace: for each time step, find which state is active
    result.state_trace.resize(k + 1, -1);
    for (int t = 0; t <= k; t++) {
        for (int i = 0; i < fsm.num_states; i++) {
            int var = state_var(i, t);
            if (var <= cnf.num_vars && assignment[var] == 1) {
                result.state_trace[t] = i;
                break;
            }
        }
    }

    // Find the first time step where a bad state is reached
    for (int t = 0; t <= k; t++) {
        int s = result.state_trace[t];
        if (s != -1 && fsm.bad_states[s]) {
            result.counterexample_step = t;
            break;
        }
    }

    return result;
}

// ============================================================
// Print variable map (debugging)
// ============================================================

void BMC::print_var_map() const {
    printf("[BMC] Variable Map:\n");
    for (int t = 0; t <= k; t++) {
        for (int i = 0; i < fsm.num_states; i++) {
            printf("  var %3d : state %-15s at t=%d\n",
                state_var(i, t),
                fsm.state_names[i].c_str(),
                t);
        }
    }
    for (int t = 0; t <= k; t++)
        printf("  var %3d : bad_reached at t=%d\n", bad_var[t], t);
}
