CXX := g++
CXXFLAGS := -std=c++17 -O3 -DNDEBUG -Wall -Wextra -Wpedantic
TARGET := placement_experiment
SRC := placement_experiment.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

quick: $(TARGET)
	./$(TARGET) --quick

clean:
	rm -f $(TARGET) heuristic_results.csv exact_results.csv

.PHONY: all quick clean
