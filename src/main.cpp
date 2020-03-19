#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include "configreader.h"
#include "process.h"

#include <cstdio>
#include <fstream>  //TODO testing

// Shared data for all cores
typedef struct SchedulerData {
    std::mutex mutex;
    std::condition_variable condition;
    ScheduleAlgorithm algorithm;
    uint32_t context_switch;
    uint32_t time_slice;
    std::list<Process*> ready_queue;
    std::list<Process*> io_queue;
    std::list<Process*> terminated_queue;
    bool all_terminated;
    std::ofstream log_file; //TODO delete
} SchedulerData;

void coreRunProcesses(uint8_t core_id, SchedulerData *data);
int printProcessOutput(std::vector<Process*>& processes, std::mutex& mutex);
void clearOutput(int num_lines);
uint32_t currentTime();
std::string processStateToString(Process::State state);
void sort(SchedulerData *shared_data);

int main(int argc, char **argv)
{
    // ensure user entered a command line parameter for configuration file name
    if (argc < 2)
    {
        std::cerr << "Error: must specify configuration file" << std::endl;
        exit(1);
    }

    // declare variables used throughout main
    int i;
    SchedulerData *shared_data;
    std::vector<Process*> processes;
    // read configuration file for scheduling simulation
    SchedulerConfig *config = readConfigFile(argv[1]);

    // store configuration parameters in shared data object
    uint8_t num_cores = config->cores;
    shared_data = new SchedulerData();
    shared_data->algorithm = config->algorithm;
    shared_data->context_switch = config->context_switch;
    shared_data->time_slice = config->time_slice;
    shared_data->all_terminated = false;

    std::unique_lock<std::mutex> lock(shared_data->mutex,std::defer_lock);
   
    {//TODO delete this scope
        remove("osscheduler.log");
        shared_data->log_file.open("osscheduler.log");
        shared_data->log_file << "Beginning test" << std::endl;
    }

    // create processes
    uint32_t start = currentTime(); // start is the beginning of total time tracking

    for (i = 0; i < config->num_processes; i++) // populate ready queue
    {
        Process *p = new Process(config->processes[i], start);
        processes.push_back(p);
        if (p->getState() == Process::State::Ready)
        {
            shared_data->ready_queue.push_back(p);
        }
    }

    // free configuration data from memory
    deleteConfig(config);

    sort(shared_data);  // sorts the ready queue if necessary

    // launch 1 scheduling thread per cpu core
    std::thread *schedule_threads = new std::thread[num_cores];
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i] = std::thread(coreRunProcesses, i, shared_data);
    }

    // main thread work goes here:
    int num_lines = 0;

    while (!(shared_data->all_terminated))  // main scheduler thread work
    {
        uint32_t elapsedTime = currentTime() - start;

        // clear output from previous iteration
        clearOutput(num_lines);

        // start new processes at their appropriate start time
        for(size_t i = 0;i < processes.size();i++)
        {
            if(processes[i]->getStartTime() <= elapsedTime && processes[i]->getState() == Process::NotStarted)
            {
                lock.lock();
                    shared_data->ready_queue.push_back(processes[i]);   //starts processes that haven't been started
                lock.unlock();

                processes[i]->setState(Process::State::Ready,currentTime());    //starts process and initializes its launch time
            }  
        }
        // determine when an I/O burst finishes and put the process back in the ready queue
        lock.lock();
            for(std::list<Process*>::iterator it = shared_data->io_queue.begin(); it != shared_data->io_queue.end();++it)
            {
                //test whether element is done with IO and move to ready if so
                //change process state
            }
        lock.unlock();

        // sort the ready queue (if needed - based on scheduling algorithm)
        sort(shared_data);

        // determine if all processes are in the terminated state
        lock.lock();
            shared_data->all_terminated = true;

            for(size_t i = 0;i < processes.size();i++)
                if(processes[i]->getState() != Process::Terminated)
                    shared_data->all_terminated = false;
        lock.unlock();

        // output process status table
        num_lines = printProcessOutput(processes, shared_data->mutex);

        // sleep 1/60th of a second
        usleep(16667);
    }


    // wait for threads to finish
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i].join();
    }

    //TODO need to track these statistics somehow
    // print final statistics
    //  - CPU utilization
    //  - Throughput
    //     - Average for first 50% of processes finished
    //     - Average for second 50% of processes finished
    //     - Overall average
    //  - Average turnaround time
    //  - Average waiting time


    // Clean up before quitting program
    processes.clear();

    return 0;
}

void coreRunProcesses(uint8_t core_id, SchedulerData *shared_data)
{
    std::unique_lock<std::mutex> lock(shared_data->mutex,std::defer_lock);
    Process* process = NULL;
    uint32_t event_time;

    while(1)
    {
        lock.lock();
            if(shared_data->all_terminated)
                break;

            if(shared_data->ready_queue.front() != NULL)
            {
                process = shared_data->ready_queue.front(); //take first item from ready queue
                shared_data->ready_queue.pop_front();
            }

        lock.unlock();

        if(process == NULL) //jumps back to start of loop if process is null
            continue;

        event_time = currentTime();
        process->pull(event_time,core_id);

        switch(shared_data->algorithm)
        {
            case(ScheduleAlgorithm::FCFS):
            {
                std::cout << "FCFS" << std::endl;
                break;
            }

            case(ScheduleAlgorithm::SJF):
            {
                lock.lock();
                    shared_data->log_file << "CORE " << (int) core_id << ": process remaining time= " << process->currentBurstRemaining() << std::endl; //TODO delete
                lock.unlock();

                while(currentTime() - event_time < process->currentBurstRemaining()){}    //waits for burst to end
                lock.lock();
                    process->updateProcess(currentTime());

                    if(process->getState() == Process::State::Terminated)
                        shared_data->terminated_queue.push_back(process);

                    else if (process->getState() == Process::State::IO)
                        shared_data->io_queue.push_back(process);
                lock.unlock();
                break;
            }

            case(ScheduleAlgorithm::RR):
            {
                std::cout << "RR" << std::endl;
                break;
            }
            case(ScheduleAlgorithm::PP):
            {
                std::cout << "PP" << std::endl;
                break;          
            }
        }

        process = NULL;
        event_time = currentTime();

        while(currentTime() - event_time < shared_data->context_switch){} //context switch wait time

        //  TODO allowing this loop to reiterate causes a weird error
    }
    // Work to be done by each core idependent of the other cores
    //  - Get process at front of ready queue
    //  - Simulate the processes running until one of the following:
    //     - CPU burst time has elapsed
    //     - RR time slice has elapsed
    //     - Process preempted by higher priority process
    //  - Place the process back in the appropriate queue
    //     - I/O queue if CPU burst finished (and process not finished)
    //     - Terminated if CPU burst finished and no more bursts remain
    //     - Ready queue if time slice elapsed or process was preempted
    //  - Wait context switching time
    //  * Repeat until all processes in terminated state
}

int printProcessOutput(std::vector<Process*>& processes, std::mutex& mutex)
{
    int i;
    int num_lines = 2;
    std::lock_guard<std::mutex> lock(mutex);
    printf("|   PID | Priority |      State | Core | Turn Time | Wait Time | CPU Time | Remain Time |\n");
    printf("+-------+----------+------------+------+-----------+-----------+----------+-------------+\n");
    for (i = 0; i < processes.size(); i++)
    {
        if (processes[i]->getState() != Process::State::NotStarted)
        {
            uint16_t pid = processes[i]->getPid();
            uint8_t priority = processes[i]->getPriority();
            std::string process_state = processStateToString(processes[i]->getState());
            int8_t core = processes[i]->getCpuCore();
            std::string cpu_core = (core >= 0) ? std::to_string(core) : "--";
            double turn_time = processes[i]->getTurnaroundTime();
            double wait_time = processes[i]->getWaitTime();
            double cpu_time = processes[i]->getCpuTime();
            double remain_time = processes[i]->getRemainingTime();
            printf("| %5u | %8u | %10s | %4s | %9.1lf | %9.1lf | %8.1lf | %11.1lf |\n", 
                   pid, priority, process_state.c_str(), cpu_core.c_str(), turn_time, 
                   wait_time, cpu_time, remain_time);
            num_lines++;
        }
    }
    return num_lines;
}

void clearOutput(int num_lines)
{
    int i;
    for (i = 0; i < num_lines; i++)
    {
        fputs("\033[A\033[2K", stdout);
    }
    rewind(stdout);
    fflush(stdout);
}

uint32_t currentTime()
{
    uint32_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    return ms;
}

std::string processStateToString(Process::State state)
{
    std::string str;
    switch (state)
    {
        case Process::State::NotStarted:
            str = "not started";
            break;
        case Process::State::Ready:
            str = "ready";
            break;
        case Process::State::Running:
            str = "running";
            break;
        case Process::State::IO:
            str = "i/o";
            break;
        case Process::State::Terminated:
            str = "terminated";
            break;
        default:
            str = "unknown";
            break;
    }
    return str;
}

void sort(SchedulerData *shared_data)
{// tests whether ready queue must be sorted, then performs the sort if needed
    std::unique_lock<std::mutex> lock(shared_data->mutex,std::defer_lock);

    if(shared_data->algorithm == ScheduleAlgorithm::PP)
    {
        lock.lock();
            shared_data->ready_queue.sort(PpComparator());
        lock.unlock();
    }
    else if(shared_data->algorithm == ScheduleAlgorithm::SJF)
    {
        lock.lock();
            shared_data->ready_queue.sort(SjfComparator());
        lock.unlock();
    }
}