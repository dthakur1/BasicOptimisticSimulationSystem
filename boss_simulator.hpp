/*********************************************************************************
File name:    boss_simulator.hpp, boss_simulator.cpp, makefile
Authors:      Nitesh Mishra, Dushyant Thakur, Ali Eker
Description:  CPP Implementation for BOSS Simulatior
              (PDES -- Parallel Discrete Event Simulation)
*********************************************************************************/

#ifndef BOSS_SIMULATOR_HPP
#define BOSS_SIMULATOR_HPP

#include <assert.h>
#include <iostream>
#include <chrono>
#include <string>
#include <sstream>
#include <vector>
#include <queue>
#include <random>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <unordered_map>
#include <map>

// Macro to enable (1) or disable (0) debug status for simulator
#define DEBUG_STATUS 0

// Macro to indicate using new mutex implemented by Poojita
//#define USE_NEW_MUTEX

namespace boss {

    /*
     * Forward declarations for classes
     */
    class Event;

    class Logical_Process;

    class Boss_Simulator;

    class Comparator_Timestamp;

    class P_Mutex; // Implementation of Poojita's mutex class

    /*
     * Inherited class from standard priority_queue to implement custom methods
     */
    template<typename T, typename Container = std::vector<T>, typename Compare = Comparator_Timestamp>
    class priority_queue_custom : public std::priority_queue<T, Container, Compare> {
    public:

        // Function to find and remove respective event from priority queue (using iteration)
        bool remove(const T &event) {

            // Find the event in priority queue
            auto it = std::find(this->c.begin(), this->c.end(), event);

            // Erase event and return true if found
            if (it != this->c.end()) {
                this->c.erase(it);
                std::make_heap(this->c.begin(), this->c.end(), this->comp);
                return true;
            }

            return false;
        }

        // Function to erase events having timestamp greater than passed argument
        void remove_ts_event(const std::size_t &timestamp, std::vector<Event> &removed_list) {

            // Iterate over priority queue to erase event with timestamp greater than method parameter
            for (auto it = this->c.begin(); it != this->c.end(); ++it) {
                if (it->get_event_ts() >= timestamp) {
                    removed_list.push_back(*it);
                    this->c.erase(it);
                    --it;
                }
            }

            // Heapify after respective events are removed from the priority queue
            if (!this->c.empty()) {
                std::make_heap(this->c.begin(), this->c.end(), this->comp);
            }
        }
    };


    /*
     * Declaration of Data Type Alias used in simulator
     */
#ifdef USE_NEW_MUTEX
    using Custom_Mutex = P_Mutex;
#else
    using Custom_Mutex = std::mutex;
#endif
    using Custom_priority_queue = priority_queue_custom<Event>;
    using Custom_logical_process_list = std::vector<Logical_Process *>;
    using Second = std::chrono::duration<double, std::ratio<1, 1>>;
    using TimePoint = std::chrono::time_point<std::chrono::system_clock>;

    /*
     * Global variables to be shared between multiple threads
     */
    std::vector<std::size_t> active_threads; //Contains indexes of active PEs
    std::vector<Custom_Mutex *> event_queue_mutex_list; // List of mutex for each PE event queue
    std::vector<Custom_priority_queue *> pe_event_queue_list; // Event Queue implementation as Priority Queue
    std::vector<Custom_logical_process_list> pe_logical_process_list; // List of logical processes vector for each PE

    // Variables to measure the execution time of whole simulation
    TimePoint start, end;
    std::atomic_flag clock_start = ATOMIC_FLAG_INIT;
    std::atomic<std::size_t> pe_counter;

    // Variables to manage progress of simulation
    std::atomic<std::size_t> progress_count;
    std::size_t percent_done = 0;

    /*
     * Class to represent an Event
     */
    class Event {
    public:
        // Friend class to access private members
        friend class Comparator_Timestamp;

        friend class Boss_Simulator;

        friend bool operator==(const Event &, const Event &);

        // Default Constructor
        Event() : _event_type{0}, _timestamp{0}, _src_lp{nullptr}, _dest_lp{nullptr} {}

        // Value Constructor
        Event(std::size_t event_type, std::size_t ts, Logical_Process *src_lp, Logical_Process *dest_lp) : _event_type{event_type},
                                                                                                           _timestamp{ts},
                                                                                                           _src_lp{src_lp},
                                                                                                           _dest_lp{dest_lp} {}

        // Destructor
        ~Event() = default;

        // Getter functions
        std::size_t get_event_ts() { return _timestamp; }

        Logical_Process *get_src_lp() { return _src_lp; }

        Logical_Process *get_dest_lp() { return _dest_lp; }

    private:
        std::size_t _event_type; // 0 - Event Message, 1 - Anti Event Message
        std::size_t _timestamp;
        Logical_Process *_src_lp;
        Logical_Process *_dest_lp;
    };

    /*
     * Overloaded == operator to compare two events
     */
    bool operator==(const Event &event1, const Event &event2) {
        return (event1._event_type == event2._event_type && event1._timestamp == event2._timestamp
                && event1._src_lp == event2._src_lp && event1._dest_lp == event2._dest_lp);
    }

    /*
     * Class to represent LP
     */
    class Logical_Process {
    public:
        // Friend class to access private members
        friend class Boss_Simulator;

        friend class Comparator_Timestamp;

        /*
         * Inner class to contain History Log for respective Logical Process
         */
        class History_Log {
        public:
            // Default Constructor
            History_Log() = delete;

            // Value Constructor
            History_Log(std::size_t event_type, std::size_t dest_lp_id, std::size_t timestamp) : _event_type{event_type},
                                                                                                 _dest_lp_id{dest_lp_id},
                                                                                                 _timestamp{timestamp} {}

            std::size_t _event_type; // 0 - Event Message, 1 - Anti Event Message
            std::size_t _dest_lp_id;
            std::size_t _timestamp;
        };

        // Constructor
        Logical_Process() = delete;

        // Value Constructor
        Logical_Process(std::size_t lp_id, std::size_t pe_id,
                        std::size_t lp_lvt, Custom_priority_queue *pe_e_q) : _lp_id{lp_id}, _pe_id{pe_id},
                                                                             _lp_lvt{lp_lvt}, _pe_event_queue{pe_e_q} {}

        // Random processing in order to utilize some CPU time -- EPG based
        int process_event() {
            double result = 0;
            for (std::size_t i = 0; i < _epg; ++i) {
                result = result + (i * 315789.00) / 12345.00;
                result += 30000.00;
            }

            return 0;
        }

    private:
        // Member variables
        std::size_t _lp_id;
        std::size_t _pe_id;
        std::size_t _lp_lvt;
        static std::size_t _epg;
        Custom_priority_queue *_pe_event_queue;
        //std::vector<History_Log> _lp_history_log_list;
        std::vector<Event> _lp_history_log_list;
        std::map<std::size_t, std::mt19937> _lp_engine_state_list;
    };

    /*
     * Functor in order to prioritize Event Queue according to Timestamp or SRC LP ID of event
     */
    class Comparator_Timestamp {
    public:
        bool operator()(const Event &event1, const Event &event2) {
            if ((event1._timestamp == event2._timestamp) && (event1._src_lp == event2._src_lp)) {
                return (event1._event_type < event2._event_type);
            }
            return (event1._timestamp > event2._timestamp);
        }
    };

    /*
     * Class for Boss Simulator
     */
    class Boss_Simulator {
    public:
        /*
         * Inner class to hold essential parameters for simulation
         */
        class Parameter {
        public:
            // Default Constructor
            Parameter() : _pe_id{0}, _pe_cnt{0}, _pe_gvt{0} {}

            // Member variables
            std::size_t _pe_id;
            std::size_t _pe_cnt;
            std::size_t _pe_gvt;
            Custom_priority_queue *_pe_event_queue;
            Custom_logical_process_list _pe_lp_list;
        };

        /*
         * Inner class to record statistics of simulation
         */
        class Stats {
        public:
            // Default Constructor
            Stats() : _net_event_rate{0}, _total_elapsed_time{}, _empty_queue_count{0}, _mutex_contention_r{0},
                      _mutex_contention_s{0}, _processed_event_count{0}, _total_iteration_count{0} {}

            // Member variables
            double _net_event_rate;
            Second _total_elapsed_time;
            std::atomic<std::size_t> _empty_queue_count;
            std::atomic<std::size_t> _mutex_contention_r;
            std::atomic<std::size_t> _mutex_contention_s;
            std::atomic<std::size_t> _processed_event_count;
            std::atomic<std::size_t> _processed_anti_event_count;
            std::atomic<std::size_t> _straggler_event_count;
            std::atomic<std::size_t> _straggler_anti_event_count;
            std::atomic<std::size_t> _total_iteration_count;
        };

        // Default Constructor
        Boss_Simulator() = delete;

        // Value constructor to initialize respective data structures before starting actual simulation
        Boss_Simulator(int arg_cnt, char *arg_vals[]) : _arg_count{arg_cnt} {

	    //_break_simulation = 0;

            // Initialize statistics member variables
            _stats._empty_queue_count = 0;
            _stats._mutex_contention_r = 0;
            _stats._mutex_contention_s = 0;
            _stats._processed_event_count = 0;
            _stats._processed_anti_event_count = 0;
            _stats._straggler_anti_event_count = 0;
            _stats._straggler_event_count = 0;
            _stats._total_iteration_count = 0;

            // Initialize class member variables
            _pe_count = std::strtoul(arg_vals[1], nullptr, 0);
            _current_pe_count = _pe_count;
            for (int i = 0; i < arg_cnt; ++i) {
                _arg_vals.emplace_back(arg_vals[i]);
            }

            // Logic to implement 'Strong Scaling' by managing same workload with different threads
            //_simulation_end_time = _simulation_end_time / _pe_count;

            // Initialize Event Queues, Logical Process and Event Queue Mutexes
            for (std::size_t pe = 0; pe < _pe_count; ++pe) {
                // Initialize active threads
                active_threads.push_back(pe);
                pe_event_queue_list.push_back(new Custom_priority_queue);
                event_queue_mutex_list.push_back(new Custom_Mutex);

                // Initialize Logical Process List for each PE
                Custom_logical_process_list temp_list;
                for (std::size_t lp = 0; lp < _lp_count; ++lp) {
                    temp_list.push_back(new Logical_Process(lp, pe, 0, pe_event_queue_list[pe]));
                }
                pe_logical_process_list.push_back(temp_list);
            }

            // Logic to prime PE event queue before starting simulation
            for (std::size_t i = 0; i < 1; ++i) {
                for (std::size_t pe = 0; pe < _pe_count; ++pe) {
                    for (std::size_t lp = 0; lp < _lp_count; ++lp) {
                        pe_event_queue_list[pe]->push(Event(0, i, nullptr, pe_logical_process_list[pe][lp]));
                    }
                }
            }
        }

        // Destructor
        ~Boss_Simulator() {
            // Deallocate heap memories
            for (std::size_t pe = 0; pe < _pe_count; ++pe) {
                delete pe_event_queue_list[pe];
                delete event_queue_mutex_list[pe];

                for (std::size_t lp = 0; lp < _lp_count; ++lp) {
                    delete pe_logical_process_list[pe][lp];
                }
            }
        }

        // Member functions
        bool simulate();

        static void start_simulation(Parameter *);

        static void set_cpu_affinity(std::size_t);

	static void barrier_gvt(Parameter *);
        
	// Function to generate random number
        static std::size_t rand_generator(std::size_t min, std::size_t max, std::size_t seed) {
            static thread_local std::mt19937 generator(seed);
            std::uniform_int_distribution<int> distribution((int) min, (int) max);
            return (std::size_t) distribution(generator);
        }

        // Function to generate random number - Overloaded
        static std::size_t rand_generator(std::size_t min, std::size_t max, std::mt19937 generator) {
            std::uniform_int_distribution<int> distribution((int) min, (int) max);
            return (std::size_t) distribution(generator);
        }

    private:
        // Member variables
        int _arg_count;
        static Stats _stats;
        std::size_t _pe_count;
        static std::size_t _lp_count;
        std::vector<std::thread> _threads;
        std::vector<std::string> _arg_vals;
        static std::size_t _simulation_end_time;
        static std::atomic<std::size_t> _current_pe_count;
        static std::atomic<std::size_t> _break_simulation;
    };
}

#endif
