#include "process.h"

// Process class methods
Process::Process(ProcessDetails details, uint32_t current_time)
{
    int i;
    pid = details.pid;
    start_time = details.start_time;
    num_bursts = details.num_bursts;
    current_burst = 0;
    burst_times = new uint32_t[num_bursts];
    for (i = 0; i < num_bursts; i++)
    {
        burst_times[i] = details.burst_times[i];
    }
    priority = details.priority;
    state = (start_time == 0) ? State::Ready : State::NotStarted;
    if (state == State::Ready)
    {
        launch_time = current_time;
    }
    core = -1;
    turn_time = 0;
    wait_time = 0;
    cpu_time = 0;
    remain_time = 0;
    for (i = 0; i < num_bursts; i+=2)
    {
        remain_time += burst_times[i];
    }

    switch_time = current_time;
}

Process::~Process()
{
    delete[] burst_times;
}

uint16_t Process::getPid() const
{
    return pid;
}

uint32_t Process::getStartTime() const
{
    return start_time;
}

uint8_t Process::getPriority() const
{
    return priority;
}

Process::State Process::getState() const
{
    return state;
}

int8_t Process::getCpuCore() const
{
    return core;
}

double Process::getTurnaroundTime() const
{
    return (double)turn_time / 1000.0;
}

double Process::getWaitTime() const
{
    return (double)wait_time / 1000.0;
}

double Process::getCpuTime() const
{
    return (double)cpu_time / 1000.0;
}

double Process::getRemainingTime() const
{
    return (double)(remain_time - cpu_time) / 1000.0;
}

uint32_t Process::getSwitchTime() const
{
    return switch_time;
}

uint32_t Process::getRemainingTimeLong() const
{
    return remain_time;
}

void Process::setState(State new_state, uint32_t current_time)
{
    if (state == State::NotStarted && new_state == State::Ready)
    {
        launch_time = current_time;
        switch_time = current_time;
    }

    state = new_state;
}

void Process::setCpuCore(int8_t core_num)
{
    core = core_num;
}

void Process::updateProcess(uint32_t current_time,int8_t core)
{
    turn_time = current_time - launch_time;

    if(state == Process::State::Running)
    {
        stopRunning(current_time);
    }

    else if(state == Process::State::IO)
    {
        stopIO(current_time);
    }

    else if (state == Process::State::Ready)
    {
        startRunning(current_time,core);
    }

    switch_time = current_time;

    // cpu_time += time_update;                    //adds time of cpu burst to total cpu time
    // remain_time -= time_update;                 //reduces remaining time
    // turn_time = current_time - launch_time;     //updates turnaround time
    // burst_times[current_burst] -= time_update;

    // if(burst_times[current_burst] <= 0)
    // {
    //     current_burst++;

    //     if(current_burst > num_bursts)  //end of the array
    //         setState(Process::State::Terminated);

    //     else
    //         setState(Process::State::IO);
    // }
    // use `current_time` to update turnaround time, wait time, burst times, 
    // cpu time, and remaining time
}

void Process::startRunning(uint32_t current_time,int8_t new_core)
{
    core = new_core;

    wait_time += (current_time - switch_time);

    setState(Process::State::Running);
}

void Process::stopRunning(uint32_t current_time)
{
    core = -1;

    cpu_time += (current_time - switch_time);

    burst_times[current_burst] -= (current_time - switch_time);

    if(burst_times[current_burst] <= 0)
    {
        current_burst++;

        setState(Process::State::IO);

        if(current_burst >= num_bursts)
            setState(Process::State::Terminated);
    }

    else
    {
        setState(Process::State::Ready);
    }
}

void Process::stopIO(uint32_t current_time)
{
    current_burst++;
    setState(Process::State::Ready);
}

void Process::updateBurstTime(int burst_idx, uint32_t new_time)
{
    burst_times[burst_idx] = new_time;
}


// Comparator methods: used in std::list sort() method
// No comparator needed for FCFS or RR (ready queue never sorted)

// SJF - comparator for sorting read queue based on shortest remaining CPU time
bool SjfComparator::operator ()(const Process *p1, const Process *p2)
{
    return p1->getRemainingTime() < p2->getRemainingTime();
}

// PP - comparator for sorting read queue based on priority
bool PpComparator::operator ()(const Process* p1, const Process* p2)
{
    return p1->getPriority() < p2->getPriority();
}

uint32_t Process::currentBurstRemaining() const
{
    return burst_times[current_burst];
}

void Process::pull(uint32_t current_time,uint8_t core)
{
    wait_time += (current_time - switch_time);
    switch_time = current_time;
    setState(Process::State::Running);
    core = core;
}