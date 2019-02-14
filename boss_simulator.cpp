#include "boss_simulator.hpp"

using namespace boss;

// Simulator Static variable initialization
Boss_Simulator::Stats Boss_Simulator::_stats;
std::size_t Logical_Process::_epg = 0;
std::size_t Boss_Simulator::_lp_count = 128;
std::size_t Boss_Simulator::_simulation_end_time = 10000;
std::atomic<std::size_t> Boss_Simulator::_current_pe_count;

std::size_t static print_interval = 500;
std::size_t static print_increment;

std::size_t static gvt_interval = 500;
std::size_t static gvt_counter;
pthread_barrier_t barrier;

bool static exiting = false;
bool static on_barrier = false;
pthread_mutex_t barrier_lock;

std::size_t static GVT;
pthread_mutex_t gvt_lock;

/*
 * Function to set CPU Affinity according to thread id
 */
void Boss_Simulator::set_cpu_affinity(std::size_t cpuid_rank) {

    // Local variable declaration
    cpu_set_t cpu_mask;
    pthread_t thread_id = pthread_self();

    CPU_ZERO(&cpu_mask);
    CPU_SET(cpuid_rank, &cpu_mask);

    if (CPU_ISSET(cpuid_rank, &cpu_mask) >= 0) {
        int status = pthread_setaffinity_np(thread_id, sizeof(cpu_set_t), &cpu_mask);
        if (status != 0) {
            std::cerr << "ERROR :: Calling pthread_setaffinity_np --> " << status << std::endl;
        }
    }
}

void Boss_Simulator::barrier_gvt(Parameter * me)
{
	// Synchronize to look at every PE`s event queue at the same time so that we would not miss an event
	int r = pthread_barrier_wait(&barrier); if (r != 0 && r != PTHREAD_BARRIER_SERIAL_THREAD) assert(false);
	
	// Calculate logical virtual time
	std::size_t lvt = _simulation_end_time;
        for (std::size_t lp = 0; lp < _lp_count; ++lp)
	{
                if (lvt > (static_cast<Logical_Process *>(me -> _pe_lp_list[lp])) -> _lp_lvt) 
		{
                	lvt = (static_cast<Logical_Process *>(me -> _pe_lp_list[lp])) -> _lp_lvt;
                }
        }

	// Find minimum time stamped event in PE`s event queue
	std::size_t min_ts_event = (pe_event_queue_list[me -> _pe_id] -> top())._timestamp;
	lvt = std::min(lvt, min_ts_event);

	// Min operation is not thread safe so protect it
	r = pthread_mutex_lock(&gvt_lock); assert(!r);
	GVT = std::min(GVT, lvt);
	r = pthread_mutex_unlock(&gvt_lock); assert(!r);
	
	// Syncronize again to let every PE finish updating GVT
	r = pthread_barrier_wait(&barrier); if (r != 0 && r != PTHREAD_BARRIER_SERIAL_THREAD) assert(false);
	me -> _pe_gvt = GVT;	
	
	// Fossil Collect
	for (std::size_t i = 0; i < _lp_count; ++i)
	{
		for (auto it = (static_cast<Logical_Process *>(me -> _pe_lp_list[i])) -> _lp_history_log_list.begin(); 
			  it != (static_cast<Logical_Process *>(me -> _pe_lp_list[i])) -> _lp_history_log_list.end(); 
                          ++it) 
		{
        		if (it -> get_event_ts() < GVT) 
			{
                		(static_cast<Logical_Process *>(me -> _pe_lp_list[i])) -> _lp_history_log_list.erase(it);
                		--it;
                	}
        	}
	}

	// Reset the flags
        if (me -> _pe_id == 0) 
	{
		on_barrier = false;
		gvt_counter = gvt_interval;
	}

	// Syncronize again to avoid race condition
	r = pthread_barrier_wait(&barrier); if (r != 0 && r != PTHREAD_BARRIER_SERIAL_THREAD) assert(false);	
	if (me -> _pe_id == 0) GVT = _simulation_end_time;
}

/*
 * Function to be called by each thread in the simulation
 */
void Boss_Simulator::start_simulation(Parameter *pe_param) {

    // Local variables specific to each PE (thread)
    int r;
    std::vector<Event> removed_event_list;
    Logical_Process *current_lp = nullptr;
    std::size_t rand_pe, rand_lp;
    std::size_t processed_event_count = 0, empty_queue_count = 0, pe_iteration_count = 0;
    std::size_t straggler_event_count = 0, straggler_anti_event_count = 0, processed_anti_event_count = 0;
#if (DEBUG_STATUS)
    std::size_t mutex_contention_r = 0, mutex_contention_s = 0;
#endif

    // Set CPU Affinity as per the thread id
    set_cpu_affinity(pe_param->_pe_id);

    // Start the simulation time when first thread enters in this function (Using C++11 atomic_flag::test_and_set())
    if (!clock_start.test_and_set()) start = std::chrono::system_clock::now();

    /*
     * --------------- Simulation start with endless loop -- Break when PE's GVT exceeds _simulation_end_time ---------------
     */
    for (;;) 
    {
	// If a PE left and if not other PEs in the barrier, let the others break the loop
        if (exiting)
	{
		r = pthread_mutex_lock(&barrier_lock); assert(!r);
		if (!on_barrier) 
		{
			r = pthread_mutex_unlock(&barrier_lock); assert(!r);
			break;
		}
		r = pthread_mutex_unlock(&barrier_lock); assert(!r);
	}
	
	// Barrier GVT
	if (gvt_counter == 0) 
	{
		r = pthread_mutex_lock(&barrier_lock); assert(!r);
		if (!exiting) 
		{
			on_barrier = true;
			r = pthread_mutex_unlock(&barrier_lock); assert(!r);

			barrier_gvt(pe_param);
		}
		else r = pthread_mutex_unlock(&barrier_lock); assert(!r);
	}
	else if (pe_param -> _pe_id == 0) gvt_counter--;
    
	// Logic to display progress of simulation on output console
        if (pe_param -> _pe_id == 0 && pe_param -> _pe_gvt >= print_interval) 
	{
            fprintf(stderr, "%3zu%% simulation completed. (%zu)\n", print_interval / 100, pe_param -> _pe_gvt);
            print_interval += print_increment;
        }

	// End simulation for every PE once it's gvt exceeds global_GVT
        if (pe_param->_pe_gvt >= _simulation_end_time) 
	{
		r = pthread_mutex_lock(&barrier_lock); assert(!r);
		if (!on_barrier)
		{
			exiting = true;
			r = pthread_mutex_unlock(&barrier_lock); assert(!r);
			break;
		}
		r = pthread_mutex_unlock(&barrier_lock); assert(!r);
	}
 
	// Counting total iterations for each PE
        ++pe_iteration_count;
        
        /*
         * --------------------- RECEIVE EVENT MESSAGE ---------------------
         */
        Event current_event;
        // Check for empty event queue
        if (!pe_param -> _pe_event_queue -> empty()) 
	{

            // Fetching lowest timestamp event from PE event queue (priority queue)
#if (DEBUG_STATUS)
            if (event_queue_mutex_list[pe_id]->try_lock()) {        // EVENT QUEUE LOCK ACQUIRED
                current_event = pe_event_queue->top();
                pe_event_queue->pop();
                event_queue_mutex_list[pe_id]->unlock();            // EVENT QUEUE LOCK RELEASED
            } else {
                ++mutex_contention_r;
                continue;
            }
#else
            event_queue_mutex_list[pe_param -> _pe_id] -> lock();      // EVENT QUEUE LOCK ACQUIRED
            current_event = pe_param -> _pe_event_queue -> top();
            //
	    // Receiving
	    //
	    pe_param -> _pe_event_queue -> pop();
            event_queue_mutex_list[pe_param -> _pe_id] -> unlock();    // EVENT QUEUE LOCK RELEASED
#endif

        } 
	else 
	{
            ++empty_queue_count;
            //fprintf(stderr, "%zu EMPTY\n", pe_id);
            continue;
        }
        current_lp = current_event._dest_lp;


        /*
         * --------------------- PROCESS STRAGGLER ---------------------
         */
        if (current_lp->_lp_lvt > current_event._timestamp) 
	{
            ++straggler_event_count;

            // Send anti-message to everyone in history log having event.ts > current_event.ts
            for (auto r_it = current_lp->_lp_history_log_list.rbegin(); r_it != current_lp->_lp_history_log_list.rend(); ++r_it) 
	    {
                // Send anti_message to it->destination LP
                if (r_it->_timestamp > current_event._timestamp) 
		{
                    Event new_anti_event(1, (r_it->_timestamp), current_lp, r_it->_dest_lp);

                    event_queue_mutex_list[r_it->_dest_lp->_pe_id]->lock();             // EVENT QUEUE LOCK ACQUIRED
                    pe_event_queue_list[r_it->_dest_lp->_pe_id]->push(new_anti_event);
                    //
		    // Sending anti
		    //
		    event_queue_mutex_list[r_it->_dest_lp->_pe_id]->unlock();           // EVENT QUEUE LOCK RELEASED
                }
            }
        }


        /*
         * --------------------- PROCESS ANTI-EVENT ---------------------
         */
        if (current_event._event_type == 1) 
	{
            if (current_lp->_lp_lvt < current_event._timestamp) // Process anti-event which is in the event queue
	    {
                ++processed_anti_event_count;

                // Remove all events from PQ with ts > current_event.ts
                event_queue_mutex_list[pe_param->_pe_id]->lock();
                pe_param -> _pe_event_queue -> remove_ts_event(current_event._timestamp, removed_event_list);
                event_queue_mutex_list[pe_param->_pe_id]->unlock();

                processed_anti_event_count += removed_event_list.size();
                removed_event_list.clear();
                //continue;
            } 
	    else // Straggler anti-event
            {                     
		++straggler_anti_event_count;

                // Send anti-message to everyone in history log having event.ts > current_event.ts
                for (auto r_it = current_lp->_lp_history_log_list.rbegin(); r_it != current_lp->_lp_history_log_list.rend(); ++r_it) 
		{
                    // Send anti_message to it->destination LP
                    if (r_it->_timestamp > current_event._timestamp) 
		    {
                        Event new_anti_event(1, (r_it->_timestamp), current_lp, r_it->_dest_lp);

                        event_queue_mutex_list[r_it->_dest_lp->_pe_id]->lock();             // EVENT QUEUE LOCK ACQUIRED
                        pe_event_queue_list[r_it->_dest_lp->_pe_id]->push(new_anti_event);
                        //
			// Sending anti
			//
			event_queue_mutex_list[r_it->_dest_lp->_pe_id]->unlock();           // EVENT QUEUE LOCK RELEASED
                    }
                }
            }
        }


        /*
         * --------------------- PROCESS EVENT MESSAGE ---------------------
         */
        current_lp->process_event();

        // Increase count of events processed for stats
        ++processed_event_count;


        /*
         * --------------------- SEND NEW EVENT MESSAGE ---------------------
         */
        // Random Destination PE (from the active thread vector) and LP indexes
        rand_pe = rand_generator(0, _current_pe_count - 1, pe_param->_pe_id);
        rand_lp = rand_generator(0, _lp_count - 1, pe_param->_pe_id);

        // Maintain the state of the engine
        /*auto engine_iter = current_lp->_lp_engine_state_list.find(current_event._timestamp);
        if (engine_iter == current_lp->_lp_engine_state_list.end()) {
            //fprintf(stderr, "1\n");
            std::mt19937 new_generator(pe_param->_pe_id);
            // Add this random generator in LP engine list
            current_lp->_lp_engine_state_list.emplace(current_event._timestamp, new_generator);

            rand_pe = rand_generator(0, _current_pe_count - 1, new_generator);
            rand_lp = rand_generator(0, _lp_count - 1, new_generator);
        } else {
            //fprintf(stderr, "2\n");
            auto rand_engine = engine_iter->second;

            rand_pe = rand_generator(0, _current_pe_count - 1, rand_engine);
            rand_lp = rand_generator(0, _lp_count - 1, rand_engine);
        }*/

        // Increment on current LP's LVT by current event's timestamp
        // No need to make it thread safe because it is going to change only by one thread
        current_lp->_lp_lvt = current_event._timestamp;

        // Create new event with random destination
        auto random_dest_lp = pe_logical_process_list[rand_pe][rand_lp];
        Event new_event(0, (current_lp->_lp_lvt + 1), current_lp, random_dest_lp);

        // Update History Log of current LP
        /*current_lp->_lp_history_log_list.emplace_back(
            Logical_Process::History_Log(new_event._event_type, new_event._dest_lp->_lp_id, new_event._timestamp));*/

	current_lp->_lp_history_log_list.push_back(new_event);

#if (DEBUG_STATUS)
        // Spin lock logic while sending new event message
        while (true) { // TODO :: Need to think something else to replace this spin logic
            if (event_queue_mutex_list[rand_pe]->try_lock()) {      // EVENT QUEUE LOCK ACQUIRED
                // Send to random destination
                pe_event_queue_list[rand_pe]->push(new_event);
                event_queue_mutex_list[rand_pe]->unlock();          // EVENT QUEUE LOCK RELEASED
                break;
            } else {
                ++mutex_contention_s;
                continue;
            }
        }
#else
        // Send event to another random PE Event Queue
        //
	// Sending event
	//
        event_queue_mutex_list[rand_pe]->lock();            // EVENT QUEUE LOCK ACQUIRED
        pe_event_queue_list[rand_pe]->push(new_event);
        event_queue_mutex_list[rand_pe]->unlock();          // EVENT QUEUE LOCK RELEASED
#endif    
    }

    ++pe_counter;
    if (pe_counter == pe_param->_pe_cnt) end = std::chrono::system_clock::now();

    // Update the statistics object with respective stats (C++11 atomics is thread-safe)
    // Using C++11 atomics so no need of mutex or X86 atomic hardware instructions
    _stats._empty_queue_count += empty_queue_count;
    _stats._processed_event_count += processed_event_count;
    _stats._total_iteration_count += pe_iteration_count;
    _stats._processed_anti_event_count += processed_anti_event_count;
    _stats._straggler_event_count += straggler_event_count;
    _stats._straggler_anti_event_count += straggler_anti_event_count;
#if (DEBUG_STATUS)
    _stats._mutex_contention_r += mutex_contention_r;
    _stats._mutex_contention_s += mutex_contention_s;
#endif
}

/*
 * Simulate function implementation
 */
bool Boss_Simulator::simulate() {

    // Allocate heap memory for params for each PE
    auto *params = new Parameter[_pe_count];

    size_t r = pthread_barrier_init(&barrier, NULL, _pe_count); assert(!r);    
    r = pthread_mutex_init(&barrier_lock, NULL); assert(!r);
    r = pthread_mutex_init(&gvt_lock, NULL); assert(!r);

    gvt_counter = gvt_interval; 
    print_increment = print_interval;    

    GVT = _simulation_end_time;

    // Logic to create threads and start simulation
    for (std::size_t i = 0; i < _pe_count; ++i) {
        // Initialize thread specific function parameters
        params[i]._pe_id = i;
        params[i]._pe_cnt = _pe_count;
        params[i]._pe_event_queue = pe_event_queue_list[i];
        params[i]._pe_lp_list = pe_logical_process_list[i];

        // Thread function call
        _threads.emplace_back(&start_simulation, &params[i]);
    }

    // Logic to wait for all threads to complete its execution
    for (auto &thread : _threads) thread.join();

    // Calculation for total elapsed time and net event rate
    _stats._total_elapsed_time = (end - start);

    // Ali:
    // I think we should not add straggler event countr and straggler anti event count since we already count
    // processed events
    //
    _stats._net_event_rate = ((_stats._processed_event_count + _stats._processed_anti_event_count + _stats._straggler_event_count +
                               _stats._straggler_anti_event_count)
                              / _stats._total_elapsed_time.count());

    // Necessary stats to display during simulation
    std::cout.imbue(std::locale("")); // Using to format numbers -- TODO :: Check why this is giving unwanted Memory leak
    std::cout << "\n------------------------------------------------------------------------------------" << std::endl;
    std::cout << "SIMULATION STATISTICS :-\n-----------------------\n" << std::endl;
    std::cout << "Total PEs used during simulation :::::::::::::: \t\t" << _pe_count << std::endl;
    std::cout << "LPs per PE used during simulation ::::::::::::: \t\t" << _lp_count << std::endl;
    std::cout << "Simulation end time for each PE ::::::::::::::: \t\t" << _simulation_end_time << std::endl;
    std::cout << "GVT interval :::::::::::::::::::::::::::::::::: \t\t" << gvt_interval << "\n" << std::endl;
    std::cout << "Total PROCESSED EVENTS during simulation :::::: \t\t" << _stats._processed_event_count << std::endl;
    std::cout << "Total PROCESSED ANTI_EVENTS during simulation : \t\t" << _stats._processed_anti_event_count << std::endl;
    std::cout << "Total STRAGGLER EVENTS during simulation :::::: \t\t" << _stats._straggler_event_count << std::endl;
    std::cout << "Total STRAGGLER ANTI_EVENTS during simulation : \t\t" << _stats._straggler_anti_event_count << std::endl;
    std::cout << "Total simulation ELAPSED TIME in Seconds :::::: \t\t" << _stats._total_elapsed_time.count() << std::endl;
    std::cout << "Net Event Rate (Events per Second) :::::::::::: \t\t" << _stats._net_event_rate << "\n" << std::endl;
    std::cout << "Total iteration counts for whole simulation ::: \t\t" << _stats._total_iteration_count << std::endl;
    std::cout << "Total attempts to access EMPTY EVENT QUEUES ::: \t\t" << _stats._empty_queue_count << std::endl;
#if (DEBUG_STATUS)
    std::cout << "Lock contention for Receive Event Queue :::::: \t\t" << _stats._mutex_contention_r << std::endl;
    std::cout << "Lock contention for Send Event Queue ::::::::: \t\t" << _stats._mutex_contention_s << std::endl;
    std::cout << "Total Lock contention for Event Queues ::::::: \t\t" << 
		_stats._mutex_contention_r + _stats._mutex_contention_s << std::endl;
#endif
    std::cout << "\n------------------------------------------------------------------------------------" << std::endl;

    // Deallocate heap memory
    delete[] params;

    return true;
}

/*
 * Main function to start with the simulation
 * argv[0] --> Program executable name
 * argv[1] --> Number of threads
 */
int main(int argc, char *argv[]) {
    // Check for valid command line argument count
    if (argc != 2) {
        std::cout << "ERROR:: Argument count is not valid for simulator !!" << std::endl;
        return 1;
    }

    // Start simulation
    Boss_Simulator sim_obj(argc, argv);

    if (sim_obj.simulate()) {
        std::cout << "\nSimulation Result --> SUCCESS !!" << std::endl;
    } else {
        std::cout << "\nSimulation Result --> FAILED !!" << std::endl;
    }

    return 0;
}
