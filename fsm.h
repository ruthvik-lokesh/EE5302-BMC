#pragma once
#include <vector>
#include <string>
#include <set>
#include <map>

/*
 * FSM Representation for BMC
 *
 * We represent an FSM with N states as a set of boolean state variables.
 * With N states we need ceil(log2(N)) bits to encode state.
 *
 * For simplicity (and easy CNF encoding), we use a one-hot encoding:
 * each state i has a variable s_i that is true iff we are in state i.
 *
 * Example: 3-state FSM uses variables s0, s1, s2
 * Constraint: exactly one s_i is true at any time step.
 *
 * Transitions: T(i, j) means "from state i, go to state j"
 * In CNF: if s_i is true at step t, then s_j is true at step t+1
 *          i.e., (NOT s_i_t) OR (s_j_t+1)   [for deterministic transition]
 *
 * Bad states: states where our safety property is VIOLATED.
 * The SAT solver finds a path that reaches a bad state.
 */

struct FSM {
    int num_states;

    // initial_states[i] = true means state i is a valid initial state
    std::vector<bool> initial_states;

    // transitions[i] = list of states reachable from state i
    // For deterministic FSM: transitions[i].size() == 1
    // For nondeterministic FSM: transitions[i].size() >= 1
    std::vector<std::vector<int>> transitions;

    // bad_states[i] = true means state i violates our safety property
    // BMC searches for a path that REACHES a bad state
    std::vector<bool> bad_states;

    // Human-readable state names (optional, for debugging)
    std::vector<std::string> state_names;

    FSM(int n) : num_states(n),
                 initial_states(n, false),
                 transitions(n),
                 bad_states(n, false),
                 state_names(n) {
        for (int i = 0; i < n; i++)
            state_names[i] = "s" + std::to_string(i);
    }

    void set_initial(int state) {
        initial_states[state] = true;
    }

    void add_transition(int from, int to) {
        transitions[from].push_back(to);
    }

    void set_bad(int state) {
        bad_states[state] = true;
    }

    void set_name(int state, const std::string& name) {
        state_names[state] = name;
    }

    // Print FSM for debugging
    void print() const {
        printf("FSM: %d states\n", num_states);
        for (int i = 0; i < num_states; i++) {
            printf("  State %d (%s)%s%s\n",
                i,
                state_names[i].c_str(),
                initial_states[i] ? " [INITIAL]" : "",
                bad_states[i]     ? " [BAD]"     : "");
            for (int j : transitions[i])
                printf("    -> State %d (%s)\n", j, state_names[j].c_str());
        }
    }
};
