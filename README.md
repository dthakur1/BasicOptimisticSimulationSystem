## Boss Simulator - Object Oriented Implementation using C++ 

#### Brief information about simulation logic used to implement Boss Simulator

* Initialize required members Boss Simulator class before starting the simulator.
	* PE count from command line argument.
	* LP count for each PE.
	* Global GVT --> stop simulation once every PE's gvt exceeds this Global GVT.
	* Initialize Event Queues for each PE and prime them with same number of events as of LP count.
	* Initialize Event Queue Mutexes for each PE.
	* Initialize LP list for each PE.

* Create same number of threads as of PE count and start simulation by calling thread specific function.
