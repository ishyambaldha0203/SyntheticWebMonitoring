/*************************************************************************************************
 * @file core.cpp
 *
 * @brief Concrete implementation of Core server application.
 *
 *************************************************************************************************/
#include "Common.h"

#include <vector>
#include <sstream>
#include <fstream>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>

#define MAX_URL_LEN 50

using namespace std;

namespace
{
    // #region Global Variables

    int32_t port[MAX_AGENT] = {8100, 8200, 8300};
    char ip[MAX_AGENT][32] = {"127.0.0.1", "127.0.0.1", "127.0.0.1"};
    struct pollfd poll_fd[MAX_AGENT];

    // #endregion

    // #region Utility Methods

    void printUsage()
    {
        printf("Usage: ./core <conf-file>");
    }

    // #endregion
} // Anonymous namespace

namespace CoreImplementation
{
    /**
     * @class JobParser
     *
     * @brief Parse one job/line from configuration file.
     */
    class JobParser
    {
    public:
        /**
         * @brief Construct a new Job Parser object.
         *
         * @param str A single line from config file to fetch all the required data.
         */
        JobParser(string str)
        {
            vector<string> internal;
            stringstream ss(str);
            string tok;

            while (ss >> tok)
            {
                internal.push_back(tok);
            }

            _agent_id = stoi(internal[0]);
            _url = internal[1];
            _frequency = stoi(internal[2]);
        }

        /**
         * @brief Destroy the Job Parser object.
         */
        ~JobParser() = default;

        /**
         * @brief Get the Agent Id to which this job is supposed to be assigned.
         *
         * @return int32_t A unique Agent identifier.
         */
        int32_t GetAgentId()
        {
            return _agent_id;
        }

        /**
         * @brief Get the under test for this specific job.
         *
         * @return string& Stringified URL.
         */
        string &GetUrl()
        {
            return _url;
        }

        /**
         * @brief Get the frequency of number of time this jib need to be run by Agent.
         *
         * @return int32_t A job frequency.
         */
        int32_t GetFrequency()
        {
            return _frequency;
        }

    private:
        int32_t _agent_id;
        string _url;
        int32_t _frequency;
    };

    /**
     * @class ConfigParser
     *
     * @brief Implements all the configuration parsing related functionalities.
     */
    class ConfigParser
    {
    public:
        /**
         * @brief Construct a new Config Parser object.
         *
         * @param name Config file name with full path.
         */
        ConfigParser(string name) : file(name)
        {
            cout << "Reading config file: " << file << endl;
        }

        /**
         * @brief Destroy the Config Parser object.
         */
        ~ConfigParser() = default;

        /**
         * @brief Parse the configuration into specified data types.
         *
         * @return int32_t Status code.
         */
        int32_t parseConfig()
        {
            ifstream conf_file(file);

            if (conf_file.is_open())
            {
                string line;
                while (getline(conf_file, line))
                {
                    if (line.size() == 0)
                    {
                        continue;
                    }

                    JobParser job(line);
                    if (job.GetUrl().size() > MAX_URL_LEN)
                    {
                        cerr << "Skipping test, URL length is exceeded limit of " << MAX_URL_LEN << " for url: "
                             << "'" << job.GetUrl() << "'" << endl;
                        continue;
                    }

                    if (job.GetAgentId() < 1 || job.GetAgentId() > MAX_AGENT)
                    {
                        cerr << "Skipping test, Invalid agent Id: " << job.GetAgentId() << endl;
                        continue;
                    }

                    jobs.push_back(job);
                    if (jobs.size() >= MAX_TEST)
                    {
                        cout << "Support maximum " << MAX_TEST << " tests, rest will be ignored." << endl;
                        break;
                    }
                }
            }
            else
            {
                cerr << "Couldn't open config file for reading." << endl;
                return -1;
            }

            cout << "Number jobs to execute:" << jobs.size() << endl;

            conf_file.close();

            return 0;
        }

        /**
         * @brief Get the total number of jobs an Agent has to perform.
         *
         * @return int32_t Job count.
         */
        int32_t getJobCount()
        {
            return jobs.size();
        }

        /**
         * @brief Get the list of the jobs that an Agent will perform.
         *
         * @return vector<JobParser>& List of job instances.
         */
        vector<JobParser> &GetJobList()
        {
            return jobs;
        }

    private:
        vector<JobParser> jobs;
        string file;
    };

    /**
     * @class Agent
     *
     * @brief Implements all the functionalities that deals with Agents.
     */
    class Agent
    {
    public:
        /**
         * @brief Construct a new Agent object.
         *
         * @param id A unique Agent id to connect with it.
         */
        Agent(int32_t id) : agent_id(id)
        {
            running_job = 0;
            is_alive = false;
            if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
            {
                cerr << "socket: " << strerror(errno) << std::endl;

                // exit(EXIT_FAILURE);
            }

            int32_t on = 1;
            if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) < 0)
            {
                cerr << "setsockopt: " << strerror(errno) << std::endl;
            }

            if (setsockopt(sock_fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on)) < 0)
            {
                cerr << "setsockopt: " << strerror(errno) << std::endl;
            }
        }

        /**
         * @brief Destroy the Agent object.
         */
        ~Agent() = default;

        /**
         * @brief Make a connection with a specified Agent.
         *
         * @return int32_t Status code.
         */
        int32_t ConnectAgent()
        {
            struct sockaddr_in serv_addr;

            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(port[agent_id - 1]);
            if (inet_pton(AF_INET, ip[agent_id - 1], &serv_addr.sin_addr) <= 0)
            {
                cerr << "inet_pton: Invalid address/ Address not supported" << strerror(errno) << std::endl;
                return -1;
            }

            if (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
            {
                cerr << "connect: " << strerror(errno) << std::endl;
                return -1;
            }

            /* Register fd for polling */
            poll_fd[agent_id - 1].fd = sock_fd;
            poll_fd[agent_id - 1].events = POLLIN;
            fcntl(sock_fd, F_SETFL, O_NONBLOCK); // Making socket fd a non-blocking

            is_alive = true;

            return 0;
        }

        /**
         * @brief To send job details to Agent for execution.
         *
         * @param job A job instance which hold all the details.
         *
         * @return int32_t Status code.
         */
        int32_t SendReqToAgent(JobParser &job)
        {
            if (is_alive)
            {
                if (++running_job > MAX_AGENT_WORKER)
                {
                    cerr << "At a time an agent " << agent_id << " can run maximum " << MAX_AGENT_WORKER << " job." << endl;
                    return -1;
                }

                Request request;
                bzero((Request *)&request, sizeof(request));

                cout << "Sending Job request to agent: " << agent_id << endl;
                strcpy(request.url, job.GetUrl().c_str());
                request.worker = running_job;
                request.op = 1;
                request.freq = job.GetFrequency();

                write(sock_fd, &request, sizeof(request));
            }
            else
            {
                cerr << "Agent " << agent_id << " is not alive." << endl;
            }
            return 0;
        }

        /**
         * @brief Get the socket fd that is being used to connect with Agent.
         *
         * @return int32_t A socket file descriptor.
         */
        int32_t GetSocketFd()
        {
            return sock_fd;
        }

    private:
        int32_t agent_id;
        int32_t sock_fd;
        int32_t running_job; // Keep the total count of tests running on Agent.
        bool is_alive;
    };

    /**
     * @brief Method to connect with Front End.
     *
     * @param resp Response from Agent.
     * @param id Agent ID from where the response is received.
     *
     * @return int32_t Status code.
     */
    static int32_t PushDataToFrontEnd(Response &resp, int32_t &id)
    {
        if (strcmp(resp.message, "worker_not_present") == 0)
        {
            cerr << "This worker number  is not present at agent" << endl;
            return -1;
        }
        else
        {
            cout << resp.url << " " << resp.status << " (" << resp.runs << " runs)" << endl;
        }

        return 0;
    }

    /**
     * @brief Send job requests to Agents based on the agent IDs.
     *
     * @param agent List of Agent a Core is connected with.
     * @param jobs Number of jobs to be performed by core.
     *
     * @return int32_t Status code.
     */
    static int32_t PushJobRequestsToAgent(vector<Agent> &agent, vector<JobParser> jobs)
    {
        int32_t job_count = jobs.size();
        int32_t id = 0;

        for (int32_t itr = 0; itr < job_count; itr++)
        {
            id = jobs[itr].GetAgentId();

            if (id <= 0 || id > MAX_AGENT)
            {
                cerr << "Core dont know agent with Id: " << id << endl;
                continue;
            }

            if (agent[id - 1].SendReqToAgent(jobs[itr]) != 0)
            {
                cout << "Failed to send request to Agent: " << id << endl;
                continue;
            }
        }

        return 0;
    }

    /**
     * @brief Check for any events to read from Agents and identify the agent that has sent an response.
     *
     * @return int32_t Status code.
     */
    static int32_t AgentPoll()
    {
        for (int32_t index = 0; index < MAX_AGENT; index++)
        {
            if (poll_fd[index].revents != 0)
            {
                if (poll_fd[index].revents & POLLHUP)
                {
                    if (close(poll_fd[index].fd) == -1)
                    {
                        cerr << "close: " << strerror(errno) << std::endl;
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
                        cerr << "close: " << strerror(errno) << std::endl;
                    }
                }
            }
        }

        return 0;
    }

    /**
     * @brief Keeps core alive and polling for response from agents and it will spend rest of its life here.
     *
     * @param agents A list of agent core it connected with.
     */
    static void CoreHandler(vector<Agent> &agents)
    {
        int32_t ret = 0;
        int32_t agent_index = 0;
        Response response;
        bzero((Response *)&response, sizeof(response));

        while (1)
        {
            ret = poll(poll_fd, MAX_AGENT, POLL_TIMEOUT_MS);
            if (ret < 0)
            {
                cerr << "poll: " << strerror(errno) << std::endl;
            }

            // Check for any response from Agents.
            if ((agent_index = AgentPoll()))
            {
                ret = read(agents[agent_index - 1].GetSocketFd(), &response, sizeof(response));
                if (ret < 0)
                {
                    cerr << "read: " << strerror(errno) << std::endl;
                }

                // Send data to front end for printing.
                PushDataToFrontEnd(response, agent_index);
            }
            else
            {
                // cout<<"Poll timeout..."<<endl;
            }
        }
    }

} // namespace CoreImplementation

using namespace CoreImplementation;

/**
 * @brief Main function to start the Core application.
 *
 * @param argc A command line argument count.
 * @param argv An array of command line arguments.
 *
 * @return int32_t An application status code.
 */
int32_t main(int32_t argc, char *argv[])
{
    // Checks for Command line arguments.
    if (argc != 2)
    {
        cerr << "Core must take only 1 argument, Its Configuration file path." << endl;
        printUsage();
        exit(EXIT_FAILURE);
    }

    // Create object for configuration to access conf data.
    ConfigParser conf_data(argv[1]);

    // Parse config file for jobs to run on agents.
    if (conf_data.parseConfig() != 0)
    {
        cerr << "Configuration file parsing failed." << endl;
        exit(EXIT_FAILURE);
    }

    // Create instances for Agents.
    vector<Agent> agents;
    for (int32_t agent_num = 1; agent_num <= MAX_AGENT; agent_num++)
    {
        agents.push_back(Agent(agent_num));
    }

    // Create connection with all agents.
    for (Agent &agent : agents)
    {
        agent.ConnectAgent();
    }

    // Send jobs to respective agents.
    PushJobRequestsToAgent(agents, conf_data.GetJobList());

    // Core process handler.
    CoreHandler(agents);

    cerr << "If you are seeing this, there is something is fishy!!!" << endl;

    exit(EXIT_SUCCESS);
}
