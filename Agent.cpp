/*************************************************************************************************
 * @file agent.cpp
 *
 * @brief Concrete implementation of a one instance of Agent application.
 *
 *************************************************************************************************/
#include "Common.h"

#include <array>
#include <memory>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>

#define PIPE_END 2
#define CHILD 0
#define PARENT 1
#define BACKLOG 5
#define ALWAYS_TRUE 1
#define COMMAND 1
#define EXIT 2
#define JOB_TO_DO "curl -w %{time_connect} -o /dev/null -s "

using namespace std;

namespace
{
    // #region Global Variables

    int32_t port[MAX_AGENT] = {8100, 8200, 8300};
    char ip[MAX_AGENT][32] = {"127.0.0.1", "127.0.0.1", "127.0.0.1"};
    int32_t socket_fd[MAX_AGENT_WORKER][PIPE_END];
    struct pollfd poll_fd[MAX_AGENT_WORKER + 1]; // One for connection with core.

    int32_t g_worker = MAX_AGENT_WORKER;
    bool is_stop = false;

    // #endregion

    // #region Utility Methods

    bool IsNumber(const string &str)
    {
        for (char const &c : str)
        {
            if (isdigit(c) == 0)
            {
                return false;
            }
        }

        return true;
    }

    void PrintUsage()
    {
        printf("Usage: ./agent <Id>");
    }

    // #endregion
} // Anonymous namespace

namespace AgentImplementation
{
    /**
     * @class Agent
     *
     * @brief Main class to accommodate all the Agent features.
     */
    class Agent
    {
    public:
        /**
         * @brief Construct a new Agent object.
         *
         * @param id A unique agent identifier.
         */
        Agent(int32_t id) : _agent_id(id)
        {
            int32_t optval_on = 1;

            if ((_sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
            {
                cerr << "socket: " << strerror(errno) << std::endl;
                exit(EXIT_FAILURE);
            }

            if (setsockopt(_sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&optval_on, sizeof(optval_on)) < 0)
            {
                cerr << "setsockopt: " << strerror(errno) << std::endl;
            }
        }

        /**
         * @brief Destroy the Agent object.
         */
        ~Agent() = default;

        /**
         * @brief Binds the Agent machine IP and port.
         *
         * @return int32_t Status code.
         */
        int32_t Bind()
        {
            struct sockaddr_in serv_addr;

            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(port[_agent_id - 1]);
            if (inet_pton(AF_INET, ip[_agent_id - 1], &serv_addr.sin_addr) <= 0)
            {
                cerr << "Invalid address:" << ip[_agent_id - 1] << std::endl;
                exit(EXIT_FAILURE);
            }

            if ((::bind(_sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) != 0)
            {
                cerr << "bind: " << strerror(errno) << std::endl;
                exit(EXIT_FAILURE);
            }
            else
            {
                cout << "Socket binding successful." << endl;
            }

            return 0;
        }

        /**
         * @brief Set the maximum listen queue it want to connect with.
         *
         * @return int32_t Status code.
         */
        int32_t Listen()
        {
            if ((::listen(_sock_fd, BACKLOG)) != 0)
            {
                cerr << "listen: " << strerror(errno) << std::endl;
                exit(EXIT_FAILURE);
            }
            else
            {
                cout << "Agent " << _agent_id << " is listening." << endl;
            }

            return 0;
        }

        /**
         * @brief To accept the connection request from the Core.
         *
         * @return int32_t Status code.
         */
        int32_t Accept()
        {
            struct sockaddr_in cli_addr;
            uint32_t cli_addr_length = sizeof(cli_addr);

            _conn_fd = ::accept(_sock_fd, (struct sockaddr *)&cli_addr, &cli_addr_length);
            if (_conn_fd < 0)
            {
                cerr << "accept: " << strerror(errno) << std::endl;
                exit(EXIT_FAILURE);
            }

            /* Add core connection fd for polling */
            poll_fd[g_worker].fd = _conn_fd;
            poll_fd[g_worker].events = POLLIN;
            fcntl(_conn_fd, F_SETFL, O_NONBLOCK);

            return 0;
        }

        /**
         * @brief Get the socket file descriptor.
         *
         * @return int32_t A unique socket file descriptor.
         */
        int32_t GetSocketFd()
        {
            return _sock_fd;
        }

        /**
         * @brief Get the new fd got created after successful connection with core.
         *
         * @return int32_t A connection fd.
         */
        int32_t GetConnectionFd()
        {
            return _conn_fd;
        }

    private:
        int32_t _agent_id;
        int32_t _sock_fd = 0;
        int32_t _conn_fd = 0;
    };

    /**
     * @class Worker
     *
     * @brief Concrete implementation worker that performs the task delegated by Agent.
     */
    class Worker
    {
    public:
        /**
         * @brief Construct a new Worker object.
         *
         * @param num Number of worker as Agent want to create.
         */
        Worker(int32_t num) : _worker_num(num)
        {
            if (socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fd[_worker_num]) < 0)
            {
                perror("opening stream socket pair");
                exit(EXIT_FAILURE);
            }

            poll_fd[_worker_num].fd = socket_fd[_worker_num][PARENT];
            poll_fd[_worker_num].events = POLLIN;
            fcntl(socket_fd[_worker_num][PARENT], F_SETFL, O_NONBLOCK);
        }

        /**
         * @brief Destroy the Worker object.
         */
        ~Worker() = default;

        /**
         * @brief Execute the CLI type command on system
         *
         * @param cmd A CLI command to be executed.
         *
         * @return string The result it has got after executing the job request.
         */
        string RunJob(const char *cmd)
        {
            array<char, 128> buffer;
            string result;

            unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
            if (!pipe)
            {
                throw runtime_error("popen() failed!");
            }

            while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
            {
                result += buffer.data();
            }

            return result;
        }

        /**
         * @brief Run the job assigned by Agent to this worker.
         *
         * @param req A job request from Core server.
         */
        void ServeRequest(Request &req)
        {
            int32_t ret;
            int32_t run_count = 0;
            string cmd;
            Response resp;

            switch (req.op)
            {
            case 1:
            {
                cmd = JOB_TO_DO;
                cmd = cmd.append(req.url);
                while (!is_stop)
                {
                    cout << "Executing job: " << cmd << endl;
                    string output = RunJob(cmd.c_str());
                    cout << "Output: " << output << endl;
                    resp.option = COMMAND;
                    resp.status = stod(output);
                    resp.runs = ++run_count;
                    strcpy(resp.url, req.url);

                    ret = write(socket_fd[req.worker - 1][CHILD], &resp, sizeof(resp));
                    if (ret < 0)
                    {
                        perror("write");
                    }

                    sleep(req.freq);
                }
            }
            break;
            case 2:
            {
                /*
                 * TODO: This block is not in use but can be used to control Agents from Core.
                 * kill all worker/process and exit.
                 */
                printf("Quit");
                resp.option = EXIT;
                ret = write(socket_fd[req.worker - 1][CHILD], &resp, sizeof(resp));
                if (ret < 0)
                {
                    perror("write");
                }
            }
            break;
            default:
                printf("Please enter the correct option..!!");
            }
        }

        /**
         * @brief Initialize the worker request handler.
         */
        void InitReqHandler()
        {
            int32_t result = 0;
            int32_t ret = 0;
            Request req_worker;

            result = fork();
            if (result == -1)
            {
                cerr << "fork failed." << endl;
            }
            else if (result == CHILD)
            {
                cout << "Worker Number: " << _worker_num + 1 << " ID: " << getpid() << endl;
                while (ALWAYS_TRUE)
                {
                    ret = read(socket_fd[_worker_num][CHILD], (Request *)&req_worker, sizeof(req_worker));
                    if (ret < 0)
                    {
                        perror("read");
                    }
                    cout << "--------------------------worker id = %d--------------------------" << getpid() << endl;

                    ServeRequest(req_worker);
                }
            }
        }

    private:
        int32_t _worker_num;
    };

    /**
     * @brief Keep polling for the Worker's activity.
     *
     * @return int32_t Status code.
     */
    static int32_t WorkerPoll()
    {
        for (int32_t index = 0; index < g_worker; index++)
        {
            if (poll_fd[index].revents != 0)
            {
                if (poll_fd[index].revents & POLLHUP)
                {
                    if (close(poll_fd[index].fd) == -1)
                    {
                        cerr << "close: " << strerror(errno) << endl;
                    }
                }
                else if ((poll_fd[index].revents & POLLIN))
                {
                    return (index + 1);
                }
                else if (!(poll_fd[index].revents & POLLNVAL))
                {
                    if (close(poll_fd[index].fd) == -1)
                    {
                        cerr << "close: " << strerror(errno) << endl;
                    }
                }
            }
        }

        return 0;
    }

    /**
     * @brief Keep polling for the Core server activity.
     *
     * @return int32_t Status code.
     */
    static int32_t CorePoll()
    {
        if (poll_fd[g_worker].revents != 0)
        {
            if (poll_fd[g_worker].revents & POLLHUP)
            {
                if (close(poll_fd[g_worker].fd) == -1)
                {
                    cerr << "close: " << strerror(errno) << endl;
                }
            }
            else if ((poll_fd[g_worker].revents & POLLIN))
            {
                return 1;
            }
            else if (!(poll_fd[g_worker].revents & POLLNVAL))
            {
                if (close(poll_fd[g_worker].fd) == -1)
                {
                    cerr << "close: " << strerror(errno) << endl;
                }
            }
        }

        return 0;
    }

    /**
     * @brief Agent will keep on running in this function until its got termination.
     *
     * This function act as a mediator between Worker processes and Core.
     *
     * @param agent An instance of Agent to be handled.
     */
    static void AgentHandler(Agent &agent)
    {
        int32_t ret = 0;
        int32_t worker_index;
        Response resp_core;
        Request req_core;

        while (1)
        {
            // Polling all input stream, That is from Core and all Worker Process.
            // - If it from Core, Forward this request to Workers.
            // - If it from Worker, Forward the response back to Core.
            ret = poll(poll_fd, g_worker + 1, POLL_TIMEOUT_MS);
            if (ret < 0)
            {
                cerr << "poll: " << strerror(errno) << std::endl;
            }

            // Check for any request from Core. If yes, Send the request to worker for processing.
            if (CorePoll())
            {
                ret = read(agent.GetConnectionFd(), (Request *)&req_core, sizeof(req_core));
                if (ret < 0)
                {
                    cerr << "read: " << strerror(errno) << std::endl;
                }
                if (ret > 0)
                {
                    if (req_core.worker <= g_worker)
                    {
                        ret = write(socket_fd[req_core.worker - 1][PARENT], (Request *)&req_core, sizeof(req_core));
                        if (ret < 0)
                        {
                            cerr << "write: " << strerror(errno) << std::endl;
                        }
                    }
                    else
                    {
                        strcpy(resp_core.message, "worker_not_present");
                        ret = write(agent.GetConnectionFd(), &resp_core, sizeof(resp_core));
                        if (ret < 0)
                        {
                            cerr << "write: " << strerror(errno) << std::endl;
                        }
                    }
                }
            }
            else
            {
                // cout<<"Poll timeout..."<<endl;
            }

            // Check for any input from worker. Read from worker and send to Core.
            if ((worker_index = WorkerPoll()))
            {
                /* Reading from worker */
                ret = read(socket_fd[worker_index - 1][PARENT], &resp_core, sizeof(resp_core));
                if (ret < 0)
                {
                    cerr << "read: " << strerror(errno) << std::endl;
                }

                if (resp_core.option == COMMAND)
                {
                    ret = write(agent.GetConnectionFd(), &resp_core, sizeof(resp_core));
                    if (ret < 0)
                    {
                        cerr << "write: " << strerror(errno) << std::endl;
                    }
                }
                else if (resp_core.option == EXIT)
                {
                    ret = write(agent.GetConnectionFd(), &resp_core, sizeof(resp_core));
                    if (ret < 0)
                    {
                        cerr << "write: " << strerror(errno) << std::endl;
                    }
                    close(agent.GetSocketFd());
                    kill(0, SIGKILL);
                }
            }
            else
            {
                // cout<<"Poll Timeout..."<<endl;
            }

            sleep(1);
        }
    }
} // namespace AgentImplementation

using namespace AgentImplementation;

/**
 * @brief Main function to start the Agent.
 *
 * @param argc A command line argument count.
 * @param argv An array of command line arguments.
 *
 * @return int32_t An application status code.
 */
int32_t main(int32_t argc, char *argv[])
{
    if (argc != 2)
    {
        cerr << "Agent must take only 1 argument, Its agent Id." << endl;
        PrintUsage();
        exit(EXIT_FAILURE);
    }

    int32_t agent_num = 0;
    if (IsNumber(argv[1]))
    {
        agent_num = stoi(argv[1]);
        if (agent_num < 1 || agent_num > MAX_AGENT)
        {
            cerr << "Invalid agent Id, It must be b/w 1, 2 or 3." << endl;
            exit(EXIT_FAILURE);
        }
        cout << "Agent " << agent_num << " is started." << endl;
    }
    else
    {
        cerr << "Invalid agent Id." << endl;
        exit(EXIT_FAILURE);
    }

    // Create an instance of Agent.
    Agent agent(agent_num);

    // Bind IP and port.
    agent.Bind();

    // Set the connection queue it want to listen on.
    agent.Listen();

    // create workers before accepting the connection from Core.
    vector<Worker> worker;
    for (int32_t worker_num = 0; worker_num < g_worker; worker_num++)
    {
        worker.push_back(Worker(worker_num));
    }

    for (Worker &work : worker)
    {
        work.InitReqHandler();
    }

    // Agent accepts the connection from Core.
    agent.Accept();

    // Agent main/parent process handler.
    AgentHandler(agent);

    cerr << "If you are seeing this, there is something is fishy!!!" << endl;

    exit(EXIT_SUCCESS);
}
