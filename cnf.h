#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include <cassert>

/*
 * CNF Formula Builder
 *
 * A CNF formula is a conjunction (AND) of clauses.
 * Each clause is a disjunction (OR) of literals.
 * A literal is either a variable x (positive) or its negation ¬x (negative).
 *
 * DIMACS CNF format:
 *   c comment line
 *   p cnf <num_vars> <num_clauses>
 *   <lit1> <lit2> ... <litN> 0    ← one clause per line, 0 = end of clause
 *
 * Variables are numbered 1, 2, 3, ...  (DIMACS uses 1-indexed, never 0)
 * Positive literal: +var_id
 * Negative literal: -var_id
 *
 * Example clause (A OR NOT B OR C) with A=1, B=2, C=3:
 *   1 -2 3 0
 */

class CNFFormula {
public:
    // Each clause is a list of integers (DIMACS literals)
    // Positive = variable is TRUE, Negative = variable is FALSE
    std::vector<std::vector<int>> clauses;

    int num_vars;   // Total number of variables allocated so far
    int num_clauses() const { return (int)clauses.size(); }

    CNFFormula() : num_vars(0) {}

    // Allocate a fresh boolean variable, returns its DIMACS id (1-indexed)
    int new_var() {
        return ++num_vars;
    }

    // Allocate a block of variables, returns the id of the FIRST one
    // Variables are contiguous: first, first+1, ..., first+count-1
    int new_vars(int count) {
        int first = num_vars + 1;
        num_vars += count;
        return first;
    }

    // Add a clause (list of DIMACS literals)
    void add_clause(std::vector<int> lits) {
        assert(!lits.empty() && "Empty clause means UNSAT immediately");
        clauses.push_back(std::move(lits));
    }

    // Convenience: add unit clause (single literal forced true/false)
    void add_unit(int lit) {
        clauses.push_back({lit});
    }

    // Convenience: add binary clause (A OR B)
    void add_binary(int a, int b) {
        clauses.push_back({a, b});
    }

    // ---------------------------------------------------------------
    // Common CNF encoding patterns used in BMC
    // ---------------------------------------------------------------

    // Force variable v to be TRUE:   (v)
    void force_true(int v) { add_unit(v); }

    // Force variable v to be FALSE:  (NOT v)
    void force_false(int v) { add_unit(-v); }

    // Implication: if A then B  ≡  (NOT A) OR B
    void add_implication(int a, int b) {
        add_binary(-a, b);
    }

    /*
     * At-Least-One (ALO): at least one of vars is true
     * Clause: (v0 OR v1 OR ... OR vn-1)
     */
    void add_alo(const std::vector<int>& vars) {
        add_clause(vars);
    }

    /*
     * At-Most-One (AMO): at most one of vars is true
     * Pairwise encoding: for all i < j, add (NOT vi OR NOT vj)
     * O(n^2) clauses — fine for small n (number of states in FSM)
     */
    void add_amo(const std::vector<int>& vars) {
        int n = vars.size();
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++)
                add_binary(-vars[i], -vars[j]);
    }

    /*
     * Exactly-One (EO): exactly one of vars is true
     * = ALO + AMO
     */
    void add_exactly_one(const std::vector<int>& vars) {
        add_alo(vars);
        add_amo(vars);
    }

    // ---------------------------------------------------------------
    // Write to DIMACS CNF file (for Rory's SAT solver)
    // ---------------------------------------------------------------
    void write_dimacs(const std::string& filename,
                      const std::string& comment = "") const {
        std::ofstream f(filename);
        if (!f.is_open())
            throw std::runtime_error("Cannot open file: " + filename);

        // Comment lines
        if (!comment.empty()) {
            // Split by newline and prefix each line with 'c '
            std::string line;
            for (char ch : comment) {
                if (ch == '\n') {
                    f << "c " << line << "\n";
                    line.clear();
                } else {
                    line += ch;
                }
            }
            if (!line.empty()) f << "c " << line << "\n";
        }

        // Problem line
        f << "p cnf " << num_vars << " " << num_clauses() << "\n";

        // Clauses
        for (const auto& clause : clauses) {
            for (int lit : clause)
                f << lit << " ";
            f << "0\n";
        }

        f.close();
    }

    // Print formula to stdout (for debugging small formulas)
    void print_dimacs() const {
        printf("p cnf %d %d\n", num_vars, num_clauses());
        for (const auto& clause : clauses) {
            for (int lit : clause)
                printf("%d ", lit);
            printf("0\n");
        }
    }
};
