# SyntheticWebMonitoring
Ensure website performance with automated user simulation.

## Brief of project,
**The SyntheticWebMonitoring is more of a pure C language-style project to demonstrate low socket programming.**
- The system is having 2 major components, Core and Agent.
    - Agent: Executes HTTP/s request on the URL that was received as part of configurations/settings from Core using libcurl and returns the connect (TCP + SSL Handshake) time.
    - Core: Core has two jobs – distribute the task to each Agent and aggregate results from all.

## High-Level Design
**NOTE: Whole architecture is based on socket programming.**
```
                                 ----------
                                 |Worker-1|[One worker do one job for agent as Agent has to run multiple job]
                              / ----------
                  ---------   /  ----------
                  |Agent 1| -->  |Worker-2|
                 /---------   \  ----------
                /              \ ----------
               /                 |Worker-N|
              /                  ----------
             /   
            /                    ----------  
           /                     |Worker-1|
          /                    / ----------
 ------  /       ---------    /  ----------
 |Core| -------> |Agent 1| -->   |Worker-2|
 ------  \       ---------    \  ----------
          \                    \ ----------
           \                     |Worker-N|
            \                    ----------
             \                   ----------
              \                  |Worker-1|
               \                /----------
                \ ---------    / ---------
                  |Agent 1| -->  |Worker-2|
                  ---------    \ ----------
                                \----------
                                 |Worker-N|
                                 ----------   
```

## Directory Structure
```
project/
├── Makefile 
├── README
├── Agent.cpp
├── Common.h
├── config.txt [File where the user needs to provide the configuration]
└── Core.cpp
```

## Generate the executable binary(core and agent)
1. Change the directory to `SyntheticWebMonitoring`.
2. Update the "config.txt". Where each line will be like, <Agent-ID[integer] URL[string] Frequency [integer]>
   - Agent-ID[integer] – The ID of the Agent process which should run this test. Min value:1, max value:3.
   - URL[string] – The target URL to execute the test  (Max length supported:50 characters).
   - Frequency[integer] – Number of seconds between consecutive test runs.
   - Example: (Note: Test config file is already provided within the same directory `config.txt`.)
     ```
     "1 www.google.com 5"
     "2 www.example.com 3"
     ```
3. Execute make to build the project($ make).
4. Start all 3 Agents with agent ID as argument in separate terminals ($ ./agent 1, $ ./agent 2, $ ./agent 3).
    NOTE: It is mandatory to start agents first as agents are going to run as servers.
5. Start Core with a config file as an argument in another terminal($ ./core config.txt).
6. Observe the log where Core is executing, It should print the url, time to connect, and number of runs a test has been at an agent.
```
    - Example logs,
        www.google.com 0.004956 (1 runs)
        www.cnn.com 0.26181 (1 runs)
        www.microsoft.com 0.515837 (1 runs)
        www.example.com 0.515738 (1 runs)
```

## Limitation
1. Agent reconnect logic is not there. It means, if the connection with an agent is dropped and someone has restarted the agent again then the core is not going to reconnect again. For this POC, I need to stop all 3 agents and restart it and then restart the core again.
2. Validation on the type of data is not fastened while parsing the configuration file. Like, 1st field is integer or not, 2nd field is string or not, 3rd field is integer or not. [Keeping the faith in the user, that they will write the config.txt with case :)]
3. For now, Added a limit of the first 50 tests/jobs the Core will be going to execute from "config.txt" in total(here, It's summing up all the Agents).
4. At each Agent, a maximum of 5 jobs can be run simultaneously.

## Future scope
1. Worker creation logic can be optimized. Instead of creating all workers at initialization, they can be created at run time based on the request.
2. Class declarations and definitions can be separated. Developed this project as POC so the whole source code is written in the same file.
3. Common class can be created for all socket-related operations.
4. Agent's IP and Ports are hardcoded in the source code but that can be made configurable by using some means of configuration file.

## Known Issue/Bug
NOTE: None for now. 
