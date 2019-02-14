# CPPFLAGS = -Wall -O4 -Wextra -pedantic -std=c++17 -g -pthread -march=native
# CPPFLAGS = -pg -Wall -O4 -Wextra -pedantic -std=c++17 -g -pthread # Compile with profiler
CPPFLAGS = -Wall -O4 -Wextra -pedantic -std=c++17 -g -pthread

all: boss_simulator.o
	g++ $(CPPFLAGS) $^ -o boss_simulator_exec

boss_simulator.o: boss_simulator.cpp boss_simulator.hpp
	g++ -c $(CPPFLAGS) boss_simulator.cpp -o $@

# Command to run the program 'pe=<# of threads>' --> make run pe=2
run:
	./boss_simulator_exec $(pe)

# Command to run the program in background 'pe=<# of threads>' --> make runbg pe=2
runbg:
	./boss_simulator_exec $(pe) >& output_log.txt &

checkmem:
	valgrind --leak-check=full ./boss_simulator_exec $(pe)
	# valgrind --leak-check=full --show-leak-kinds=all ./boss_simulator_exec $(pe)

clean:
	rm -rf boss_simulator_exec boss_simulator.o output_log.txt
