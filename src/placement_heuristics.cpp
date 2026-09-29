#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using Clock = std::chrono::steady_clock;
static constexpr double INF = std::numeric_limits<double>::infinity();
static constexpr double EPS = 1e-12;

enum class Objective { COST, RELIABILITY, ENERGY };
enum class Variant { SEQ, MIN, MAX };

static std::string objective_name(Objective q) {
    switch (q) {
        case Objective::COST: return "COST";
        case Objective::RELIABILITY: return "RELIABILITY";
        case Objective::ENERGY: return "ENERGY";
    }
    return "UNKNOWN";
}

static std::string variant_name(Variant h) {
    switch (h) {
        case Variant::SEQ: return "SEQ";
        case Variant::MIN: return "MIN";
        case Variant::MAX: return "MAX";
    }
    return "UNKNOWN";
}

struct Instance {
    int m = 0;
    int p = 0;
    uint64_t seed = 0;
    double Top = 1.0;

    std::vector<double> c;        // service CPU demand
    std::vector<double> omega;    // service criticality

    std::vector<double> C;        // node capacity
    std::vector<double> F;        // activation/setup cost
    std::vector<double> rho;      // non-energy operating rate
    std::vector<double> kappa;    // F + rho * Top
    std::vector<double> lambda;   // empirical failure rate
    std::vector<double> Pidle;
    std::vector<double> Pmax;

    std::vector<std::vector<double>> M;     // deployment cost
    std::vector<std::vector<double>> Texe;  // exposure/execution duration
};

struct Placement {
    bool feasible = false;
    std::vector<int> node_of;  // node_of[i] = j
};

struct Metrics {
    double cost = INF;
    double reliability = INF;
    double energy = INF;
    int active_nodes = 0;
};

struct HeuristicResult {
    Objective objective{};
    Variant variant{};
    Placement placement;
    Metrics metrics;
    double objective_value = INF;
    double construction_ms = 0.0;
    double refinement_ms = 0.0;
    double total_ms = 0.0;
};

struct ExactResult {
    std::string status = "NOT_RUN";
    double value = INF;
    double runtime_ms = 0.0;
    uint64_t nodes_visited = 0;
    Placement placement;
};

static double selected_metric(const Metrics& m, Objective q) {
    if (q == Objective::COST) return m.cost;
    if (q == Objective::RELIABILITY) return m.reliability;
    return m.energy;
}

static std::vector<double> compute_loads(const Instance& in, const Placement& pl) {
    std::vector<double> load(in.p, 0.0);
    if (!pl.feasible || static_cast<int>(pl.node_of.size()) != in.m) return load;
    for (int i = 0; i < in.m; ++i) {
        int j = pl.node_of[i];
        if (j >= 0 && j < in.p) load[j] += in.c[i];
    }
    return load;
}

static bool verify_feasible(const Instance& in, const Placement& pl) {
    if (static_cast<int>(pl.node_of.size()) != in.m) return false;
    std::vector<double> load(in.p, 0.0);
    for (int i = 0; i < in.m; ++i) {
        const int j = pl.node_of[i];
        if (j < 0 || j >= in.p) return false;
        load[j] += in.c[i];
        if (load[j] > in.C[j] + 1e-9) return false;
    }
    return true;
}

static Metrics evaluate(const Instance& in, const Placement& pl) {
    Metrics out;
    if (!verify_feasible(in, pl)) return out;

    const std::vector<double> load = compute_loads(in, pl);
    out.cost = 0.0;
    out.reliability = 0.0;
    out.energy = 0.0;
    out.active_nodes = 0;

    for (int j = 0; j < in.p; ++j) {
        if (load[j] > EPS) {
            ++out.active_nodes;
            out.cost += in.kappa[j];
            out.energy += in.Top *
                (in.Pidle[j] + (in.Pmax[j] - in.Pidle[j]) * load[j] / in.C[j]);
        }
    }

    for (int i = 0; i < in.m; ++i) {
        const int j = pl.node_of[i];
        out.cost += in.M[i][j];
        out.reliability += in.omega[i] *
            (1.0 - std::exp(-in.lambda[j] * in.Texe[i][j]));
    }
    return out;
}

static double incremental_score(const Instance& in, Objective q,
                                int i, int j,
                                const std::vector<double>& residual,
                                const std::vector<char>& active) {
    if (in.c[i] > residual[j] + 1e-9) return INF;
    const double activation = active[j] ? 0.0 : 1.0;

    if (q == Objective::COST) {
        return in.M[i][j] + activation * in.kappa[j];
    }
    if (q == Objective::RELIABILITY) {
        return in.omega[i] *
            (1.0 - std::exp(-in.lambda[j] * in.Texe[i][j]));
    }
    return in.Top *
        (activation * in.Pidle[j]
         + (in.Pmax[j] - in.Pidle[j]) * in.c[i] / in.C[j]);
}

struct BestNode {
    bool exists = false;
    int j = -1;
    double score = INF;
    double residual_after = INF;
};

static BestNode best_node_for_service(const Instance& in, Objective q, int i,
                                      const std::vector<double>& residual,
                                      const std::vector<char>& active,
                                      const std::vector<char>* allowed_nodes = nullptr) {
    BestNode best;
    for (int j = 0; j < in.p; ++j) {
        if (allowed_nodes && !(*allowed_nodes)[j]) continue;
        if (in.c[i] > residual[j] + 1e-9) continue;

        const double d = incremental_score(in, q, i, j, residual, active);
        const double rem_after = residual[j] - in.c[i];
        const bool better_score = d < best.score - EPS;
        const bool tied_score = std::abs(d - best.score) <= EPS;
        const bool better_fit = rem_after < best.residual_after - EPS;
        const bool tied_fit = std::abs(rem_after - best.residual_after) <= EPS;

        if (!best.exists || better_score ||
            (tied_score && (better_fit || (tied_fit && j < best.j)))) {
            best.exists = true;
            best.j = j;
            best.score = d;
            best.residual_after = rem_after;
        }
    }
    return best;
}

static bool service_tie_prefer(const Instance& in, int candidate, int current) {
    if (current < 0) return true;
    if (in.c[candidate] > in.c[current] + EPS) return true;
    if (std::abs(in.c[candidate] - in.c[current]) <= EPS && candidate < current) return true;
    return false;
}

static bool construction_necessary_conditions(const Instance& in,
                                              const std::vector<double>& residual,
                                              const std::vector<char>& unplaced) {
    double demand_left = 0.0;
    double capacity_left = 0.0;
    for (int i = 0; i < in.m; ++i) {
        if (unplaced[i]) demand_left += in.c[i];
    }
    for (double r : residual) capacity_left += r;
    if (demand_left > capacity_left + 1e-9) return false;

    // Every remaining service must fit somewhere in the current residual state.
    for (int i = 0; i < in.m; ++i) {
        if (!unplaced[i]) continue;
        bool fits = false;
        for (int j = 0; j < in.p; ++j) {
            if (in.c[i] <= residual[j] + 1e-9) {
                fits = true;
                break;
            }
        }
        if (!fits) return false;
    }
    return true;
}

struct NodeChoice {
    int j = -1;
    double score = INF;
    double residual_after = INF;
};

static std::vector<NodeChoice> ordered_node_choices(const Instance& in,
                                                     Objective q,
                                                     int i,
                                                     const std::vector<double>& residual,
                                                     const std::vector<char>& active) {
    std::vector<NodeChoice> choices;
    choices.reserve(in.p);
    for (int j = 0; j < in.p; ++j) {
        if (in.c[i] > residual[j] + 1e-9) continue;
        choices.push_back({
            j,
            incremental_score(in, q, i, j, residual, active),
            residual[j] - in.c[i]
        });
    }
    std::sort(choices.begin(), choices.end(), [](const NodeChoice& a, const NodeChoice& b) {
        if (std::abs(a.score - b.score) > EPS) return a.score < b.score;
        if (std::abs(a.residual_after - b.residual_after) > EPS)
            return a.residual_after < b.residual_after;
        return a.j < b.j;
    });
    return choices;
}

static int choose_service_for_state(const Instance& in,
                                    Objective q,
                                    Variant h,
                                    const std::vector<double>& residual,
                                    const std::vector<char>& active,
                                    const std::vector<char>& unplaced) {
    int chosen_i = -1;

    if (h == Variant::SEQ) {
        for (int i = 0; i < in.m; ++i) {
            if (unplaced[i]) return i;
        }
        return -1;
    }

    double chosen_b = (h == Variant::MIN) ? INF : -INF;
    for (int i = 0; i < in.m; ++i) {
        if (!unplaced[i]) continue;
        const BestNode b = best_node_for_service(in, q, i, residual, active);
        if (!b.exists) return -1;

        bool take = false;
        if (chosen_i < 0) {
            take = true;
        } else if (h == Variant::MIN) {
            if (b.score < chosen_b - EPS) take = true;
            else if (std::abs(b.score - chosen_b) <= EPS &&
                     service_tie_prefer(in, i, chosen_i)) take = true;
        } else {
            if (b.score > chosen_b + EPS) take = true;
            else if (std::abs(b.score - chosen_b) <= EPS &&
                     service_tie_prefer(in, i, chosen_i)) take = true;
        }

        if (take) {
            chosen_i = i;
            chosen_b = b.score;
        }
    }
    return chosen_i;
}

// Constructive greedy search with a feasibility safeguard.
//
// At every state the service i is selected exactly by SEQ/MIN/MAX. Candidate
// nodes j are then tried in the greedy order induced by Delta^q_ij. Normally
// the first candidate is accepted. If that choice makes completion impossible,
// the routine backtracks only far enough to try the next-best node. Thus the
// safeguard repairs capacity dead ends; it does not search for a globally
// optimal objective value. The first complete feasible construction is returned.
static bool greedy_construct_dfs(const Instance& in,
                                 Objective q,
                                 Variant h,
                                 int placed_count,
                                 Placement& pl,
                                 std::vector<double>& residual,
                                 std::vector<char>& active,
                                 std::vector<char>& unplaced) {
    if (placed_count == in.m) return true;
    if (!construction_necessary_conditions(in, residual, unplaced)) return false;

    const int i = choose_service_for_state(in, q, h, residual, active, unplaced);
    if (i < 0) return false;

    const std::vector<NodeChoice> choices = ordered_node_choices(in, q, i, residual, active);
    if (choices.empty()) return false;

    for (const NodeChoice& ch : choices) {
        const int j = ch.j;
        const bool was_active = active[j] != 0;

        pl.node_of[i] = j;
        unplaced[i] = 0;
        residual[j] -= in.c[i];
        if (residual[j] < 0.0 && residual[j] > -1e-9) residual[j] = 0.0;
        active[j] = 1;

        if (greedy_construct_dfs(in, q, h, placed_count + 1,
                                 pl, residual, active, unplaced)) {
            return true;
        }

        // Roll back and try the next-best node for the same selected service.
        residual[j] += in.c[i];
        active[j] = was_active ? 1 : 0;
        unplaced[i] = 1;
        pl.node_of[i] = -1;
    }
    return false;
}

static Placement greedy_construct(const Instance& in, Objective q, Variant h) {
    Placement pl;
    pl.node_of.assign(in.m, -1);

    std::vector<double> residual = in.C;
    std::vector<char> active(in.p, 0);
    std::vector<char> unplaced(in.m, 1);

    const bool found = greedy_construct_dfs(in, q, h, 0, pl, residual, active, unplaced);
    pl.feasible = found && verify_feasible(in, pl);
    if (!pl.feasible) {
        std::fill(pl.node_of.begin(), pl.node_of.end(), -1);
    }
    return pl;
}

static bool best_improving_relocation(const Instance& in, Objective q, Placement& pl) {
    if (!verify_feasible(in, pl)) return false;

    bool any_change = false;
    while (true) {
        const Metrics current_metrics = evaluate(in, pl);
        const double current_value = selected_metric(current_metrics, q);
        const std::vector<double> load = compute_loads(in, pl);

        double best_value = current_value;
        int best_i = -1;
        int best_j = -1;

        for (int i = 0; i < in.m; ++i) {
            const int src = pl.node_of[i];
            for (int dst = 0; dst < in.p; ++dst) {
                if (dst == src) continue;
                if (load[dst] + in.c[i] > in.C[dst] + 1e-9) continue;

                Placement cand = pl;
                cand.node_of[i] = dst;
                const Metrics cm = evaluate(in, cand);
                const double v = selected_metric(cm, q);
                if (v < best_value - EPS ||
                    (std::abs(v - best_value) <= EPS && best_i >= 0 &&
                     (i < best_i || (i == best_i && dst < best_j)))) {
                    best_value = v;
                    best_i = i;
                    best_j = dst;
                }
            }
        }

        if (best_i < 0 || best_value >= current_value - EPS) break;
        pl.node_of[best_i] = best_j;
        any_change = true;
    }
    pl.feasible = verify_feasible(in, pl);
    return any_change;
}

static bool try_eliminate_one_node(const Instance& in, Objective q,
                                   const Placement& base, int victim,
                                   Placement& candidate_out) {
    const std::vector<double> base_load = compute_loads(in, base);
    if (base_load[victim] <= EPS) return false;

    std::vector<int> displaced;
    for (int i = 0; i < in.m; ++i) {
        if (base.node_of[i] == victim) displaced.push_back(i);
    }
    if (displaced.empty()) return false;

    // Repack larger services first; this is deterministic and reduces avoidable dead ends.
    std::sort(displaced.begin(), displaced.end(), [&](int a, int b) {
        if (std::abs(in.c[a] - in.c[b]) > EPS) return in.c[a] > in.c[b];
        return a < b;
    });

    Placement cand = base;
    for (int i : displaced) cand.node_of[i] = -1;
    cand.feasible = true; // temporarily partial; do not call verify yet

    std::vector<double> residual = in.C;
    std::vector<char> active(in.p, 0);
    std::vector<char> allowed(in.p, 0);

    // Only nodes that were active in the base placement, excluding victim,
    // are allowed as repacking destinations.
    for (int j = 0; j < in.p; ++j) {
        if (j != victim && base_load[j] > EPS) allowed[j] = 1;
    }

    for (int i = 0; i < in.m; ++i) {
        const int j = cand.node_of[i];
        if (j >= 0) {
            residual[j] -= in.c[i];
            active[j] = 1;
        }
    }

    for (int i : displaced) {
        BestNode b = best_node_for_service(in, q, i, residual, active, &allowed);
        if (!b.exists) return false;
        cand.node_of[i] = b.j;
        residual[b.j] -= in.c[i];
        active[b.j] = 1;
    }

    cand.feasible = verify_feasible(in, cand);
    if (!cand.feasible) return false;
    candidate_out = std::move(cand);
    return true;
}

static bool active_node_elimination(const Instance& in, Objective q, Placement& pl) {
    if (q == Objective::RELIABILITY || !verify_feasible(in, pl)) return false;

    bool any_change = false;
    while (true) {
        const Metrics current_metrics = evaluate(in, pl);
        const double current_value = selected_metric(current_metrics, q);
        const std::vector<double> load = compute_loads(in, pl);

        double best_value = current_value;
        int best_victim = -1;
        Placement best_candidate;

        for (int victim = 0; victim < in.p; ++victim) {
            if (load[victim] <= EPS) continue;
            Placement cand;
            if (!try_eliminate_one_node(in, q, pl, victim, cand)) continue;
            const double v = selected_metric(evaluate(in, cand), q);
            if (v < best_value - EPS ||
                (std::abs(v - best_value) <= EPS && best_victim >= 0 && victim < best_victim)) {
                best_value = v;
                best_victim = victim;
                best_candidate = std::move(cand);
            }
        }

        if (best_victim < 0 || best_value >= current_value - EPS) break;
        pl = std::move(best_candidate);
        any_change = true;
    }
    pl.feasible = verify_feasible(in, pl);
    return any_change;
}

static HeuristicResult run_heuristic(const Instance& in, Objective q, Variant h) {
    HeuristicResult r;
    r.objective = q;
    r.variant = h;

    const auto total_start = Clock::now();
    const auto c0 = Clock::now();
    r.placement = greedy_construct(in, q, h);
    const auto c1 = Clock::now();
    r.construction_ms = std::chrono::duration<double, std::milli>(c1 - c0).count();

    const auto ref0 = Clock::now();
    if (r.placement.feasible) {
        best_improving_relocation(in, q, r.placement);
        if (q == Objective::COST || q == Objective::ENERGY) {
            active_node_elimination(in, q, r.placement);
        }
    }
    const auto ref1 = Clock::now();
    r.refinement_ms = std::chrono::duration<double, std::milli>(ref1 - ref0).count();

    r.total_ms = std::chrono::duration<double, std::milli>(Clock::now() - total_start).count();
    r.metrics = evaluate(in, r.placement);
    r.objective_value = selected_metric(r.metrics, q);
    return r;
}

// -------------------- Exact branch-and-bound --------------------

class ExactSolver {
public:
    ExactSolver(const Instance& instance, Objective obj, double limit_seconds,
                const std::vector<HeuristicResult>& heuristic_results)
        : in(instance), q(obj), limit_s(limit_seconds) {
        order.resize(in.m);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            if (std::abs(in.c[a] - in.c[b]) > EPS) return in.c[a] > in.c[b];
            return a < b;
        });

        best_value = INF;
        for (const auto& h : heuristic_results) {
            if (h.objective == q && h.placement.feasible && h.objective_value < best_value) {
                best_value = h.objective_value;
                best_assignment = h.placement.node_of;
            }
        }
        load.assign(in.p, 0.0);
        assign.assign(in.m, -1);
    }

    ExactResult solve() {
        start = Clock::now();
        timed_out = false;
        nodes = 0;
        dfs(0, 0.0);
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

        ExactResult out;
        out.runtime_ms = ms;
        out.nodes_visited = nodes;
        out.value = best_value;
        if (!best_assignment.empty()) {
            out.placement.feasible = true;
            out.placement.node_of = best_assignment;
        }

        if (timed_out) {
            out.status = std::isfinite(best_value) ? "TIMEOUT_FEASIBLE" : "TIMEOUT_NO_SOLUTION";
        } else {
            out.status = std::isfinite(best_value) ? "OPTIMAL" : "INFEASIBLE";
        }
        return out;
    }

private:
    const Instance& in;
    Objective q;
    double limit_s;
    std::vector<int> order;
    std::vector<double> load;
    std::vector<int> assign;
    std::vector<int> best_assignment;
    double best_value = INF;
    bool timed_out = false;
    uint64_t nodes = 0;
    Clock::time_point start;

    bool timeout_reached() {
        if ((nodes & 0x3FFFu) != 0u) return false;
        const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        if (elapsed >= limit_s) {
            timed_out = true;
            return true;
        }
        return false;
    }

    double branch_increment(int i, int j) const {
        const bool inactive = load[j] <= EPS;
        if (q == Objective::COST) {
            return in.M[i][j] + (inactive ? in.kappa[j] : 0.0);
        }
        if (q == Objective::RELIABILITY) {
            return in.omega[i] * (1.0 - std::exp(-in.lambda[j] * in.Texe[i][j]));
        }
        return in.Top * ((inactive ? in.Pidle[j] : 0.0)
               + (in.Pmax[j] - in.Pidle[j]) * in.c[i] / in.C[j]);
    }

    double lower_bound_remaining(int depth) const {
        double lb = 0.0;
        for (int k = depth; k < in.m; ++k) {
            const int i = order[k];
            double best = INF;
            for (int j = 0; j < in.p; ++j) {
                if (load[j] + in.c[i] > in.C[j] + 1e-9) continue;
                double d = 0.0;
                if (q == Objective::COST) {
                    // Omit future activation charges: nonnegative omission keeps the bound admissible.
                    d = in.M[i][j];
                } else if (q == Objective::RELIABILITY) {
                    d = in.omega[i] *
                        (1.0 - std::exp(-in.lambda[j] * in.Texe[i][j]));
                } else {
                    // Omit future idle activation energy; retain nonnegative dynamic energy.
                    d = in.Top * (in.Pmax[j] - in.Pidle[j]) * in.c[i] / in.C[j];
                }
                if (d < best) best = d;
            }
            if (!std::isfinite(best)) return INF;
            lb += best;
        }
        return lb;
    }

    bool capacity_necessary_conditions(int depth) const {
        double remaining_demand = 0.0;
        for (int k = depth; k < in.m; ++k) remaining_demand += in.c[order[k]];
        double total_residual = 0.0;
        for (int j = 0; j < in.p; ++j) total_residual += (in.C[j] - load[j]);
        if (remaining_demand > total_residual + 1e-9) return false;

        for (int k = depth; k < in.m; ++k) {
            const int i = order[k];
            bool can_fit = false;
            for (int j = 0; j < in.p; ++j) {
                if (load[j] + in.c[i] <= in.C[j] + 1e-9) {
                    can_fit = true;
                    break;
                }
            }
            if (!can_fit) return false;
        }
        return true;
    }

    void dfs(int depth, double partial_value) {
        if (timed_out) return;
        ++nodes;
        if (timeout_reached()) return;

        if (depth == in.m) {
            if (partial_value < best_value - EPS) {
                best_value = partial_value;
                best_assignment = assign;
            }
            return;
        }

        if (!capacity_necessary_conditions(depth)) return;
        const double lb = lower_bound_remaining(depth);
        if (!std::isfinite(lb)) return;
        if (partial_value + lb >= best_value - EPS) return;

        const int i = order[depth];
        struct Choice { int j; double inc; double rem_after; };
        std::vector<Choice> choices;
        for (int j = 0; j < in.p; ++j) {
            if (load[j] + in.c[i] > in.C[j] + 1e-9) continue;
            choices.push_back({j, branch_increment(i, j), in.C[j] - load[j] - in.c[i]});
        }
        std::sort(choices.begin(), choices.end(), [](const Choice& a, const Choice& b) {
            if (std::abs(a.inc - b.inc) > EPS) return a.inc < b.inc;
            if (std::abs(a.rem_after - b.rem_after) > EPS) return a.rem_after < b.rem_after;
            return a.j < b.j;
        });

        for (const Choice& ch : choices) {
            const double next = partial_value + ch.inc;
            if (next >= best_value - EPS) continue;
            assign[i] = ch.j;
            load[ch.j] += in.c[i];
            dfs(depth + 1, next);
            load[ch.j] -= in.c[i];
            assign[i] = -1;
            if (timed_out) return;
        }
    }
};

// -------------------- Synthetic instance generation --------------------

static uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static Instance generate_instance(int m, int p, uint64_t seed,
                                  double slack_min, double slack_max) {
    if (m <= 0 || p <= 0 || m < p) throw std::runtime_error("Require m >= p > 0.");
    if (slack_min < 0.0 || slack_max < slack_min) throw std::runtime_error("Invalid slack range.");

    Instance in;
    in.m = m;
    in.p = p;
    in.seed = seed;
    in.Top = 1.0;

    const uint64_t composite = splitmix64(seed ^ (static_cast<uint64_t>(m) << 32) ^ static_cast<uint64_t>(p));
    std::mt19937_64 rng(composite);

    std::uniform_int_distribution<int> demand_dist(1, 6);
    std::uniform_real_distribution<double> criticality_dist(0.30, 1.00);
    std::uniform_real_distribution<double> slack_dist(slack_min, slack_max);
    std::uniform_real_distribution<double> F_dist(0.02, 0.15);
    std::uniform_real_distribution<double> rho_dist(0.05, 0.30);
    std::uniform_int_distribution<int> fail_dist(1, 12);
    std::uniform_real_distribution<double> obs_dist(720.0, 2160.0);
    std::uniform_real_distribution<double> idle_dist(8.0, 35.0);
    std::uniform_real_distribution<double> extra_power_dist(15.0, 70.0);
    std::uniform_real_distribution<double> deploy_dist(0.0, 0.08);
    std::uniform_real_distribution<double> eps_dist(-0.04, 0.04);

    in.c.resize(m);
    in.omega.resize(m);
    for (int i = 0; i < m; ++i) {
        in.c[i] = static_cast<double>(demand_dist(rng));
        in.omega[i] = criticality_dist(rng);
    }

    // Hidden LPT anchor packing. It guarantees at least one feasible packing,
    // but the anchor itself is never given to any solver.
    std::vector<int> idx(m);
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        if (std::abs(in.c[a] - in.c[b]) > EPS) return in.c[a] > in.c[b];
        return a < b;
    });
    std::vector<double> anchor_load(p, 0.0);
    for (int i : idx) {
        int j = static_cast<int>(std::min_element(anchor_load.begin(), anchor_load.end()) - anchor_load.begin());
        anchor_load[j] += in.c[i];
    }

    in.C.resize(p);
    in.F.resize(p);
    in.rho.resize(p);
    in.kappa.resize(p);
    in.lambda.resize(p);
    in.Pidle.resize(p);
    in.Pmax.resize(p);

    for (int j = 0; j < p; ++j) {
        const double slack = slack_dist(rng);
        in.C[j] = std::ceil(anchor_load[j] * (1.0 + slack));
        if (in.C[j] < anchor_load[j]) in.C[j] = anchor_load[j];

        in.F[j] = F_dist(rng);
        in.rho[j] = rho_dist(rng);
        in.kappa[j] = in.F[j] + in.rho[j] * in.Top;
        const int nfail = fail_dist(rng);
        const double tobs = obs_dist(rng);
        in.lambda[j] = static_cast<double>(nfail) / tobs;
        in.Pidle[j] = idle_dist(rng);
        in.Pmax[j] = in.Pidle[j] + extra_power_dist(rng);
    }

    in.M.assign(m, std::vector<double>(p, 0.0));
    in.Texe.assign(m, std::vector<double>(p, 0.0));
    for (int i = 0; i < m; ++i) {
        for (int j = 0; j < p; ++j) {
            in.M[i][j] = deploy_dist(rng);
            const double raw = 0.12 + 0.70 * in.c[i] / in.C[j] + eps_dist(rng);
            in.Texe[i][j] = std::min(in.Top, std::max(0.05, raw));
        }
    }
    return in;
}

struct Config {
    int seeds = 30;
    double exact_limit_s = 30.0;
    double slack_min = 0.15;
    double slack_max = 0.35;
    bool quick = false;
    bool run_exact = true;
    std::string prefix;
};

static Config parse_args(int argc, char** argv) {
    Config cfg;
    for (int a = 1; a < argc; ++a) {
        const std::string x = argv[a];
        auto need = [&](const std::string& flag) -> std::string {
            if (a + 1 >= argc) throw std::runtime_error("Missing value after " + flag);
            return argv[++a];
        };
        if (x == "--quick") cfg.quick = true;
        else if (x == "--seeds") cfg.seeds = std::stoi(need(x));
        else if (x == "--exact-limit") cfg.exact_limit_s = std::stod(need(x));
        else if (x == "--slack-min") cfg.slack_min = std::stod(need(x));
        else if (x == "--slack-max") cfg.slack_max = std::stod(need(x));
        else if (x == "--prefix") cfg.prefix = need(x);
        else if (x == "--no-exact") cfg.run_exact = false;
        else if (x == "--help" || x == "-h") {
            std::cout
                << "Usage: ./placement_heuristics [options]\n"
                << "  --quick                 Run 2 seeds on (8,4) and (10,4)\n"
                << "  --seeds N               Seeds per scale (default 30)\n"
                << "  --exact-limit SEC       Exact time limit per objective (default 30)\n"
                << "  --slack-min X           Minimum capacity slack (default 0.15)\n"
                << "  --slack-max X           Maximum capacity slack (default 0.35)\n"
                << "  --prefix STR            Prefix output CSV filenames\n"
                << "  --no-exact              Skip exact solver\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + x);
        }
    }
    if (cfg.seeds <= 0) throw std::runtime_error("--seeds must be positive.");
    if (cfg.exact_limit_s <= 0.0) throw std::runtime_error("--exact-limit must be positive.");
    return cfg;
}

static std::string csv_double(double x) {
    if (!std::isfinite(x)) return "";
    std::ostringstream ss;
    ss << std::setprecision(12) << x;
    return ss.str();
}

int main(int argc, char** argv) {
    try {
        const Config cfg = parse_args(argc, argv);
        std::vector<std::pair<int,int>> scales = {
            {8,4}, {10,4}, {12,5}, {20,8}, {40,12}, {80,20}
        };
        int seeds = cfg.seeds;
        if (cfg.quick) {
            scales = {{8,4}, {10,4}};
            seeds = 2;
        }

        const std::string hfile = cfg.prefix + "heuristic_results.csv";
        const std::string efile = cfg.prefix + "exact_results.csv";
        std::ofstream hout(hfile);
        std::ofstream eout(efile);
        if (!hout || !eout) throw std::runtime_error("Could not open output CSV files.");

        hout << "m,p,seed,objective,variant,feasible,objective_value,cost_value,reliability_value,energy_value,active_nodes,construction_ms,refinement_ms,total_ms,exact_status,exact_value,gap_pct\n";
        eout << "m,p,seed,objective,status,value,runtime_ms,nodes_visited\n";

        const std::vector<Objective> objectives = {
            Objective::COST, Objective::RELIABILITY, Objective::ENERGY
        };
        const std::vector<Variant> variants = {
            Variant::SEQ, Variant::MIN, Variant::MAX
        };

        const int total_instances = static_cast<int>(scales.size()) * seeds;
        int completed_instances = 0;

        for (auto [m,p] : scales) {
            std::cout << "Scale " << m << " services / " << p << " nodes\n";
            for (int s = 1; s <= seeds; ++s) {
                const Instance in = generate_instance(m, p, static_cast<uint64_t>(s),
                                                      cfg.slack_min, cfg.slack_max);

                std::vector<HeuristicResult> hrs;
                hrs.reserve(9);
                for (Objective q : objectives) {
                    for (Variant h : variants) {
                        HeuristicResult r = run_heuristic(in, q, h);
                        if (r.placement.feasible && !verify_feasible(in, r.placement)) {
                            throw std::runtime_error("Internal error: heuristic marked infeasible placement as feasible.");
                        }
                        hrs.push_back(std::move(r));
                    }
                }

                std::vector<ExactResult> exact(3);
                const bool small_for_exact = (m <= 12);
                if (cfg.run_exact && small_for_exact) {
                    for (int qi = 0; qi < 3; ++qi) {
                        ExactSolver solver(in, objectives[qi], cfg.exact_limit_s, hrs);
                        exact[qi] = solver.solve();
                        eout << m << ',' << p << ',' << s << ','
                             << objective_name(objectives[qi]) << ','
                             << exact[qi].status << ','
                             << csv_double(exact[qi].value) << ','
                             << csv_double(exact[qi].runtime_ms) << ','
                             << exact[qi].nodes_visited << '\n';
                    }
                }

                for (const HeuristicResult& r : hrs) {
                    int qi = (r.objective == Objective::COST ? 0 :
                              r.objective == Objective::RELIABILITY ? 1 : 2);
                    std::string exact_status = "NOT_RUN";
                    double exact_value = INF;
                    double gap = INF;
                    if (cfg.run_exact && small_for_exact) {
                        exact_status = exact[qi].status;
                        exact_value = exact[qi].value;
                        if (r.placement.feasible && exact_status == "OPTIMAL" &&
                            std::isfinite(exact_value)) {
                            if (std::abs(exact_value) <= EPS) {
                                gap = (std::abs(r.objective_value) <= EPS) ? 0.0 : INF;
                            } else {
                                gap = 100.0 * (r.objective_value - exact_value) / std::abs(exact_value);
                                if (gap < 0.0 && gap > -1e-8) gap = 0.0;
                                if (gap < -1e-7) {
                                    throw std::runtime_error("Heuristic appears better than proven exact optimum; check solver.");
                                }
                            }
                        }
                    }

                    hout << m << ',' << p << ',' << s << ','
                         << objective_name(r.objective) << ','
                         << variant_name(r.variant) << ','
                         << (r.placement.feasible ? 1 : 0) << ','
                         << csv_double(r.objective_value) << ','
                         << csv_double(r.metrics.cost) << ','
                         << csv_double(r.metrics.reliability) << ','
                         << csv_double(r.metrics.energy) << ','
                         << r.metrics.active_nodes << ','
                         << csv_double(r.construction_ms) << ','
                         << csv_double(r.refinement_ms) << ','
                         << csv_double(r.total_ms) << ','
                         << exact_status << ','
                         << csv_double(exact_value) << ','
                         << csv_double(gap) << '\n';
                }

                ++completed_instances;
                std::cout << "  completed seed " << s << '/' << seeds
                          << "  [" << completed_instances << '/' << total_instances << " instances]\n";
            }
        }

        std::cout << "Done.\n"
                  << "Heuristic results: " << hfile << '\n'
                  << "Exact results:     " << efile << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
