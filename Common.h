/*************************************************************************************************
 * @file Common.h
 *
 * @brief Contains common data structure between Core and Agents.
 *
 * These structure is being used as data format to ease the data transfer.
 *
 *************************************************************************************************/
#ifndef _SYNTHETIC_WEB_MONITORING_COMMON_H
#define _SYNTHETIC_WEB_MONITORING_COMMON_H

#include <iostream>
#include <cstdint>
#include <cstring>

#define MAX_AGENT_WORKER 5 ///< Maximum job an agent can handle
#define MAX_AGENT 3        ///< Maximum number of Agent a Core need to manage.
#define MAX_TEST 50        ///< Maximum jobs a core can handle.

#define STRING_LENGTH 128
#define POLL_TIMEOUT_MS 1000

struct Request
{
    int32_t op;
    char url[STRING_LENGTH];
    int32_t worker;
    int32_t freq;
};

struct Response
{
    int32_t option;
    int32_t runs;
    double status;
    char message[STRING_LENGTH];
    char url[STRING_LENGTH];
};

#endif // !_SYNTHETIC_WEB_MONITORING_COMMON_H
