#pragma once
#include "fsm.h"
#include "cnf.h"
#include <vector>
#include <string>

/*
 * Bounded Model Checker (BMC)
 *
 * Given FSM M and bound k, checks the safety property:
 *   "The FSM never reaches a bad state within k steps"
 *
 * BMC Algorithm:
 * --------------
 * 1. Encode the INITIAL condition:
 *      At least one initial state is active at t=0
 *      Exactly one state is active at t=0
 *
 * 2. Encode TRANSITIONS for each step t = 0 .. k-1:
 *      For each state i at time t, if s[i][t] is true,
 *      then some successor s[j][t+1] must be true
 *      (and exactly one state is active at t+1)
 *
 * 3. Encode the BAD PROPERTY (negated):
 *      At SOME step t in [0..k], a bad state is active
 *      (we're looking for a counterexample)
 *
 * 4. If SAT → counterexample exists → bug found
 *    If UNSAT → no bad state reachable within k steps → property holds up to k
 *
 * Variable Layout:
 * ----------------
 * For an FSM with N states and bound k:
 * - We have (k+1) time steps: t = 0, 1, ..., k
 * - At each time step t, we have N boolean variables: s[0][t], s[1][t], ..., s[N-1][t]
 * - s[i][t] is TRUE iff the FSM is in state i at time step t
 * - Total state variables: N * (k+1)
 * - We also add auxiliary variables for the "bad state reached" disjunction
 *
 * Variable IDs (DIMACS, 1-indexed):
 *   state_var(i, t) = t * N + i + 1    for i in [0, N), t in [0, k]
 */

struct BMCResult {
    bool sat;                          // true = counterexample found
    int  counterexample_step;          // which step the bad state is reached (-1 if UNSAT)
    std::vector<int> state_trace;      // state index at each time step (if SAT)
    std::string dimacs_file;           // path to the generated CNF file
};

class BMC {
public:
    const FSM& fsm;
    int k;           // bound
    CNFFormula cnf;  // the formula we build

    // var_base: first DIMACS variable for state variables
    // state_var(i, t) = var_base + t * fsm.num_states + i
    int var_base;

    // One auxiliary variable per time step t in [0..k]:
    // bad_var[t] is TRUE iff a bad state is active at time t
    // We use these to build the "exists bad state" clause
    std::vector<int> bad_var;

    BMC(const FSM& fsm, int k);

    // Main entry point: build the full BMC formula and write DIMACS
    // Returns path to the generated .cnf file
    std::string encode(const std::string& output_file);

    // Parse the SAT solver's output and extract the counterexample trace
    // sat_output: raw stdout from the SAT solver
    // Returns a filled BMCResult
    BMCResult parse_sat_output(const std::string& sat_output,
                               const std::string& cnf_file);

private:
    // Returns the DIMACS variable id for "FSM is in state i at time t"
    int state_var(int state, int time) const;

    // Step 1: Encode initial states
    void encode_initial();

    // Step 2: Encode transition relation for one time step
    void encode_transition(int t);

    // Step 3: Encode "bad state reached at some step" (negation of property)
    void encode_bad_states();

    // Encode "exactly one state is active at time t"
    void encode_one_hot(int t);

    // Print variable mapping for debugging
    void print_var_map() const;
};
