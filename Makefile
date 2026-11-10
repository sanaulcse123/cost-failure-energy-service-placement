CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -DNDEBUG -Wall -Wextra -Wpedantic
BIN := build/placement_heuristics
SRC := src/placement_heuristics_stronger_lb.cpp

.PHONY: all verify quick reproduce figures stats clean
all: $(BIN)

$(BIN): $(SRC)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN)

# Validate the published, author-verified 2026-11-10 CSV snapshot.
verify:
	python3 scripts/verify_results.py --prefix results/strong_final_

# Two-seed smoke test; writes only under ignored runs/.
quick: $(BIN)
	mkdir -p runs
	./$(BIN) --quick --prefix runs/quick_
	python3 scripts/verify_results.py --quick --prefix runs/quick_

# Full 180-instance reproduction; NEVER overwrites published CSVs.
# Runtimes depend on your system and are expected to differ.
reproduce: $(BIN)
	mkdir -p runs
	./$(BIN) --seeds 30 --slack-min 0.15 --slack-max 0.35 --exact-limit 30 --prefix runs/repro_
	python3 scripts/verify_results.py --prefix runs/repro_

# Regenerate the two PNG plots from published CSVs.
figures:
	python3 scripts/make_paper_figures.py

# Optional; requires scipy (apt install python3-scipy).
stats:
	python3 scripts/statistical_tests.py

clean:
	rm -rf build runs
