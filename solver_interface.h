#pragma once
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>

/*
 * SOLVER INTERFACE — Edit this file when Rory finishes his SAT solver
 *
 * Only two things may need changing:
 *   1. run_solver()       — how we call his executable
 *   2. parse_output()     — how we read his output format
 *
 * Everything else (fsm.h, cnf.h, bmc.h, bmc.cpp) stays untouched.
 */

// -------------------------------------------------------
// EDIT THIS when Rory tells you his solver's command syntax
// Default: ./sat_solver <cnf_file>
// -------------------------------------------------------
std::string run_solver(const std::string& cnf_file,
                       const std::string& solver_path = "./sat_solver") {
    std::string cmd = solver_path + " " + cnf_file + " 2>&1";
    printf("[SOLVER] Running: %s\n", cmd.c_str());

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        printf("[SOLVER] Could not run solver.\n");
        return "";
    }
    std::string result;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe))
        result += buf;
    pclose(pipe);
    return result;
}

// -------------------------------------------------------
// EDIT THIS when Rory tells you his output format
//
// Standard minisat output:
//   s SATISFIABLE
//   v 1 -2 3 0
//
// If Rory's output differs, adjust the parsing below.
// The function must return:
//   - sat: true/false
//   - assignments: assignment[i] = 1 (true) or -1 (false) for variable i
// -------------------------------------------------------
struct SolverOutput {
    bool sat = false;
    std::vector<int> assignments; // 1-indexed, 0 = unset
};

SolverOutput parse_output(const std::string& raw, int num_vars) {
    SolverOutput out;
    out.assignments.resize(num_vars + 1, 0);

    // --- Check SAT/UNSAT ---
    // Handles: "s SATISFIABLE", "SAT", "SATISFIABLE"
    if (raw.find("UNSAT") != std::string::npos ||
        raw.find("UNSATISFIABLE") != std::string::npos) {
        out.sat = false;
        return out;
    }
    if (raw.find("SAT") == std::string::npos &&
        raw.find("SATISFIABLE") == std::string::npos) {
        out.sat = false;
        return out;
    }
    out.sat = true;

    // --- Parse variable assignments ---
    // Handles lines starting with 'v' (standard minisat)
    // OR just lines of numbers (some solvers skip the 'v')
    std::istringstream ss(raw);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;

        // Strip leading 'v' or 'V' if present
        std::string data = line;
        if (data[0] == 'v' || data[0] == 'V')
            data = data.substr(1);

        std::istringstream ls(data);
        int lit;
        while (ls >> lit) {
            if (lit == 0) break;
            int var = std::abs(lit);
            if (var >= 1 && var <= num_vars)
                out.assignments[var] = (lit > 0) ? 1 : -1;
        }
    }
    return out;
}
