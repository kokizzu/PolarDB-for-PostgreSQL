/*-------------------------------------------------------------------------
 *
 * monitor.c
 *
 *    Monitoring module of Postgres-XC configuration and operation tool.
 *
 * Copyright (c) 2020, Alibaba Group Holding Limited
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright (c) 2013 Postgres-XC Development Group
 *
 *-------------------------------------------------------------------------
 */
/*
 * This module is imported from /contrib/pgxc_monitor, to provide monitoring
 * feature of each pgstgres-xc components.
 */
//#include "gtm/gtm_client.h"
//#include "gtm/libpq-fe.h"

#include <stdlib.h>
#include <getopt.h>
#include "utils.h"
#include "variables.h"
/* This is an ugly hack to avoid conflict between gtm_c.h and pgxc_ctl.h */
#undef true
#undef false
#include "pgxc_ctl_log.h"
#include "varnames.h"
#include "config.h"
#include "monitor.h"

/* Define all the node types */
typedef enum
{
    NONE = 0,
    GTM,    /* GTM or GTM-proxy */
    NODE    /* Coordinator or Datanode */
} nodetype_t;

#define GetToken() (line = get_word(line, &token))
#define testToken(word) ((token != NULL) && (strcmp(token, word) == 0))
#define TestToken(word) ((token != NULL) && (strcasecmp(token, word) == 0))

static void printResult(int res, char *what, char *name)
{
    if (res == 0)
    {
        if (name)
            elog(NOTICE, "Running: %s %s\n", what, name);
        else
            elog(NOTICE, "Running: %s\n", what);
    }
    else
    {
        if (name)
            elog(NOTICE, "Not running: %s %s\n", what, name);
        else
            elog(NOTICE, "Not running: %s\n", what);
    }
}

static void monitor_gtm_master(void)
{
    if (doesExist(VAR_gtmMasterServer, 0) && doesExist(VAR_gtmMasterPort, 0))
        return(printResult(do_gtm_ping(sval(VAR_gtmMasterServer), atoi(sval(VAR_gtmMasterPort))), "gtm master", NULL));
    else
        elog(NOTICE, "GTM master not running\n");
}

static void monitor_gtm_slave(void)
{
    if (doesExist(VAR_gtmSlaveServer, 0) && doesExist(VAR_gtmSlavePort, 0))
        return(printResult(do_gtm_ping(sval(VAR_gtmSlaveServer), atoi(sval(VAR_gtmSlavePort))), "gtm slave", NULL));
}

static void monitor_gtm_proxy(char **nodeList)
{
    char **actualNodeList;
    int ii;
    int idx;

    actualNodeList = makeActualNodeList(nodeList);
    for (ii = 0; actualNodeList[ii]; ii++)
    {
        if ((idx = gtmProxyIdx(actualNodeList[ii])) < 0)
        {
            elog(ERROR, "ERROR: %s is not a gtm proxy.\n", actualNodeList[ii]);
            continue;
        }
        printResult(do_gtm_ping(aval(VAR_gtmProxyServers)[idx], atoi(aval(VAR_gtmProxyPorts)[idx])),
                    "gtm proxy", actualNodeList[ii]);
        Free(actualNodeList[ii]);
    }
    Free(actualNodeList);
}
    
    
static void monitor_coordinator_master(char **nodeList)
{
    char **actualNodeList;
    int ii;
    int idx;

    actualNodeList = makeActualNodeList(nodeList);
    for (ii = 0; actualNodeList[ii]; ii++)
    {
        if ((idx = coordIdx(actualNodeList[ii])) < 0)
        {
            elog(ERROR, "ERROR: %s is not a coordinator\n", actualNodeList[ii]);
            continue;
        }
        printResult(pingNode(aval(VAR_coordMasterServers)[idx], aval(VAR_coordPorts)[idx]), 
                    "coordinator master", actualNodeList[ii]);

        Free(actualNodeList[ii]);
    }
    Free(actualNodeList);
}

static void monitor_coordinator_slave(char **nodeList)
{
    char **actualNodeList;
    int ii;
    int idx;

    if (!isVarYes(VAR_coordSlave))
    {
        elog(ERROR, "ERROR: coordinator slave is not configured.\n");
        return;
    }
    actualNodeList = makeActualNodeList(nodeList);
    for (ii = 0; actualNodeList[ii]; ii++)
    {
        if ((idx = coordIdx(actualNodeList[ii])) < 0)
        {
            elog(ERROR, "ERROR: %s is not a coordinator\n", actualNodeList[ii]);
            continue;
        }
        /* Need to check again if the slave is configured */
        if (!doesExist(VAR_coordSlaveServers, idx) || is_none(aval(VAR_coordSlaveServers)[idx]))
            elog(ERROR, "ERROR: coordinator slave %s is not configured\n", actualNodeList[ii]);
        else
            printResult(pingNode(aval(VAR_coordSlaveServers)[idx], aval(VAR_coordSlavePorts)[idx]), 
                        "coordinator slave", actualNodeList[ii]);
        Free(actualNodeList[ii]);
    }
    Free(actualNodeList);
}

static void monitor_coordinator(char **nodeList)
{
    char **actualNodeList;
    int ii;
    int idx;

    actualNodeList = makeActualNodeList(nodeList);
    for (ii = 0; actualNodeList[ii]; ii++)
    {
        if ((idx = coordIdx(actualNodeList[ii])) < 0)
        {
            elog(ERROR, "ERROR: %s is not a coordinator\n", actualNodeList[ii]);
            continue;
        }
        printResult(pingNode(aval(VAR_coordMasterServers)[idx], aval(VAR_coordPorts)[idx]), 
                    "coordinator master", actualNodeList[ii]);
        if (doesExist(VAR_coordSlaveServers, idx) && !is_none(aval(VAR_coordSlaveServers)[idx]))
            printResult(pingNodeSlave(aval(VAR_coordSlaveServers)[idx],
                        aval(VAR_coordSlaveDirs)[idx]),
                        "coordinator slave", actualNodeList[ii]);
        Free(actualNodeList[ii]);
    }
    Free(actualNodeList);
}
static void monitor_datanode_master(char **nodeList)
{
    char **actualNodeList;
    int ii;
    int idx;

    actualNodeList = makeActualNodeList(nodeList);
    for (ii = 0; actualNodeList[ii]; ii++)
    {
        if ((idx = datanodeIdx(actualNodeList[ii])) < 0)
        {
            elog(ERROR, "ERROR: %s is not a datanode\n", actualNodeList[ii]);
            continue;
        }
        printResult(pingNode(aval(VAR_datanodeMasterServers)[idx], aval(VAR_datanodePorts)[idx]), 
                    "datanode master", actualNodeList[ii]);
        Free(actualNodeList[ii]);
    }
    Free(actualNodeList);
}

static void monitor_datanode_slave(char **nodeList)
{
    char **actualNodeList;
    int ii;
    int idx;

    if (!isVarYes(VAR_datanodeSlave))
    {
        elog(ERROR, "ERROR: datanode slave is not configured.\n");
        return;
    }
    actualNodeList = makeActualNodeList(nodeList);
    for (ii = 0; actualNodeList[ii]; ii++)
    {
        if ((idx = datanodeIdx(actualNodeList[ii])) < 0)
        {
            elog(ERROR, "ERROR: %s is not a datanode\n", actualNodeList[ii]);
            continue;
        }
        if (doesExist(VAR_datanodeSlaveServers, idx) && !is_none(aval(VAR_datanodeSlaveServers)[idx]))
            printResult(pingNodeSlave(aval(VAR_datanodeSlaveServers)[idx],
                        aval(VAR_datanodeSlaveDirs)[idx]), 
                        "datanode slave", actualNodeList[ii]);
        else
            elog(ERROR, "ERROR: datanode slave %s is not configured.\n", actualNodeList[ii]);

        Free(actualNodeList[ii]);
    }
    Free(actualNodeList);
}

static void monitor_datanode_learner(char **nodeList)
{
    char **actualNodeList;
    int ii;
    int idx;

    if (!isVarYes(VAR_datanodeSlave))
    {
        elog(ERROR, "ERROR: datanode slave is not configured.\n");
        return;
    }
    if (!isPaxosEnv())
    {
         elog(ERROR, "ERROR: datanode learner or replication number is configured error.\n");
        return;   
    }
    actualNodeList = makeActualNodeList(nodeList);
    for (ii = 0; actualNodeList[ii]; ii++)
    {
        if ((idx = datanodeIdx(actualNodeList[ii])) < 0)
        {
            elog(ERROR, "ERROR: %s is not a datanode\n", actualNodeList[ii]);
            continue;
        }
        if (doesExist(VAR_datanodeLearnerServers, idx) && !is_none(aval(VAR_datanodeLearnerServers)[idx]))
            printResult(pingNodeSlave(aval(VAR_datanodeLearnerServers)[idx],
                        aval(VAR_datanodeLearnerDirs)[idx]), 
                        "datanode learner", actualNodeList[ii]);
        else
            elog(ERROR, "ERROR: datanode learner %s is not configured.\n", actualNodeList[ii]);

        Free(actualNodeList[ii]);
    }
    Free(actualNodeList);
}

static void monitor_datanode(char **nodeList)
{
    char **actualNodeList;
    int ii;
    int idx;

    actualNodeList = makeActualNodeList(nodeList);
    for (ii = 0; actualNodeList[ii]; ii++)
    {
        if ((idx = datanodeIdx(actualNodeList[ii])) < 0)
        {
            elog(ERROR, "ERROR: %s is not a datanode\n", actualNodeList[ii]);
            continue;
        }
        printResult(pingNode(aval(VAR_datanodeMasterServers)[idx], aval(VAR_datanodePorts)[idx]), 
                    "datanode master", actualNodeList[ii]);
        if (doesExist(VAR_datanodeSlaveServers, idx) && !is_none(aval(VAR_datanodeSlaveServers)[idx]))
        {
            printResult(pingNodeSlave(aval(VAR_datanodeSlaveServers)[idx],
                        aval(VAR_datanodeSlaveDirs)[idx]),
                        "datanode slave", actualNodeList[ii]);
            if(isPaxosEnv())
            {
                if (doesExist(VAR_datanodeLearnerServers, idx) && !is_none(aval(VAR_datanodeLearnerServers)[idx]))
                {
                    printResult(pingNodeSlave(aval(VAR_datanodeLearnerServers)[idx],
                                aval(VAR_datanodeLearnerDirs)[idx]),
                                "datanode learner", actualNodeList[ii]);     
                }
            }
        }
        Free(actualNodeList[ii]);
    }
    Free(actualNodeList);
}

static void monitor_something(char **nodeList)
{
    char **actualNodeList;
    int ii;
    char *wkNodeList[2];
    NodeType type;

    wkNodeList[1] = NULL;
    actualNodeList = makeActualNodeList(nodeList);
    for (ii = 0; actualNodeList[ii]; ii++)
    {
        if ((type = getNodeType(actualNodeList[ii])) == NodeType_GTM)
        {
            monitor_gtm_master();
            if (isVarYes(VAR_gtmSlave))
                monitor_gtm_slave();
            Free(actualNodeList[ii]);
            continue;
        }
        else if (type == NodeType_GTM_PROXY)
        {
            wkNodeList[0] = actualNodeList[ii];
            monitor_gtm_proxy(wkNodeList);
            Free(actualNodeList[ii]);
            continue;
        }
        else if (type == NodeType_COORDINATOR)
        {
            wkNodeList[0] = actualNodeList[ii];
            monitor_coordinator(wkNodeList);
            Free(actualNodeList[ii]);
            continue;
        }
        else if (type == NodeType_DATANODE)
        {
            wkNodeList[0] = actualNodeList[ii];
            monitor_datanode(wkNodeList);
            Free(actualNodeList[ii]);
            continue;
        }
        else
        {
            elog(ERROR, "ERROR: %s is not found in any node.\n", actualNodeList[ii]);
            Free(actualNodeList[ii]);
            continue;
        }
    }
    Free(actualNodeList);
}

    

void do_monitor_command(char *line)
{
    char *token;

    if (!GetToken())
    {
        elog(ERROR, "ERROR: no monitor command options found.\n");
        return;
    }
    
    if (TestToken("gtm"))
    {
        if (!GetToken() || TestToken("all"))
        {
            /* Ping GTM */
            monitor_gtm_master();
            if (isVarYes(VAR_gtmSlave))
                monitor_gtm_slave();
        }
        else if (TestToken("master"))
            monitor_gtm_master();
        else if (TestToken("slave"))
        {
            if (isVarYes(VAR_gtmSlave))
                monitor_gtm_slave();
            else
                elog(ERROR, "ERROR: gtm slave is not configured.\n");
        }
        else
            elog(ERROR, "Invalid monitor gtm command option.\n");
        return;
    }
    else if (TestToken("gtm_proxy"))
    {
        if (!GetToken() || TestToken("all"))
            monitor_gtm_proxy(aval(VAR_gtmProxyNames));
        else
        {
            char **nodeList = NULL;
            do
                AddMember(nodeList, token);
            while (GetToken());
            monitor_gtm_proxy(nodeList);
            CleanArray(nodeList);
        }
        return;
    }
    else if (TestToken("coordinator"))
    {
        if (!GetToken() || TestToken("all"))
        {
            monitor_coordinator_master(aval(VAR_coordNames));
            if (isVarYes(VAR_coordSlave))
                monitor_coordinator_slave(aval(VAR_coordNames));
            return;
        }
        else if (TestToken("master"))
        {
            if (!GetToken() || TestToken("all"))
                monitor_coordinator_master(aval(VAR_coordNames));
            else
            {
                char **nodeList = NULL;
                do
                    AddMember(nodeList, token);
                while (GetToken());
                monitor_coordinator_master(nodeList);
                CleanArray(nodeList);
            }
        }
        else if (TestToken("slave"))
        {
            if (!isVarYes(VAR_coordSlave))
                elog(ERROR, "ERROR: coordinator slave is not configured.\n");
            else
                if (!GetToken() || TestToken("all"))
                    monitor_coordinator_slave(aval(VAR_coordNames));
                else
                {
                    char **nodeList = NULL;
                    do
                        AddMember(nodeList, token);
                    while (GetToken());
                    monitor_coordinator_slave(nodeList);
                    CleanArray(nodeList);
                }
        }
        else
        {
            char **nodeList= NULL;
            do
                AddMember(nodeList, token);
            while(GetToken());
            monitor_coordinator(nodeList);
            CleanArray(nodeList);
        }
    }
    else if (TestToken("datanode"))
    {
        if (!GetToken() || TestToken("all"))
        {
            monitor_datanode_master(aval(VAR_datanodeNames));
            if (isVarYes(VAR_datanodeSlave))
            {
                monitor_datanode_slave(aval(VAR_datanodeNames));
                if(isPaxosEnv())
                    monitor_datanode_learner(aval(VAR_datanodeNames));
            }
        }
        else if (TestToken("master"))
        {
            if (!GetToken() || TestToken("all"))
                monitor_datanode_master(aval(VAR_datanodeNames));
            else
            {
                char **nodeList = NULL;
                do
                    AddMember(nodeList, token);
                while (GetToken());
                monitor_datanode_master(nodeList);
                CleanArray(nodeList);
            }
        }
        else if (TestToken("slave"))
        {
            if (!isVarYes(VAR_datanodeSlave))
                elog(ERROR, "ERROR: datanode slave is not configured.\n");
            else
                if (!GetToken() || TestToken("all"))
                    monitor_datanode_slave(aval(VAR_coordNames));
                else
                {
                    char **nodeList = NULL;
                    do
                        AddMember(nodeList, token);
                    while (GetToken());
                    monitor_datanode_slave(nodeList);
                    CleanArray(nodeList);
                }
        }
        else if (TestToken("learner"))
        {
            if (!isVarYes(VAR_datanodeSlave))
                elog(ERROR, "ERROR: datanode slave is not configured.\n");
            else
                if (!GetToken() || TestToken("all"))
                    monitor_datanode_learner(aval(VAR_coordNames));
                else
                {
                    char **nodeList = NULL;
                    do
                        AddMember(nodeList, token);
                    while (GetToken());
                    monitor_datanode_learner(nodeList);
                    CleanArray(nodeList);
                }
        }
        else
        {
            char **nodeList= NULL;
            do
                AddMember(nodeList, token);
            while(GetToken());
            monitor_datanode(nodeList);
            CleanArray(nodeList);
        }
    }
    else if (TestToken("all"))
    {
        if (isVarYes(VAR_standAlone))
        {
            monitor_datanode(aval(VAR_datanodeNames));
        }
        else
        {
            monitor_gtm_master();
            if (isVarYes(VAR_gtmSlave))
                monitor_gtm_slave();
            if (isVarYes(VAR_gtmProxy))
                monitor_gtm_proxy(aval(VAR_gtmProxyNames));
            monitor_coordinator(aval(VAR_coordNames));
            monitor_datanode(aval(VAR_datanodeNames));
        }
    }
    else
    {
        char **nodeList = NULL;
        do
            AddMember(nodeList, token);
        while (GetToken());
        monitor_something(nodeList);
        CleanArray(nodeList);
    }
    return;
}

/*
 * Ping a given GTM or GTM-proxy
 */
int
do_gtm_ping(char *host, int port)
{
    /*char connect_str[MAXPATH+1];
    GTM_Conn *conn;

    if (host == NULL)
    {
        elog(ERROR, "ERROR: no hostname is specified.\n");
        return -1;
    }
    if (port <= 0)
    {
        elog(ERROR, "ERROR: Invalid port number, %d.\n", port);
        return -1;
    }
    // Use 60s as connection timeout 
    sprintf(connect_str, "host=%s port=%d node_name=%s remote_type=%d postmaster=0 connect_timeout=60",
            host, port, myName, GTM_NODE_COORDINATOR);
    if ((conn = PQconnectGTM(connect_str)) == NULL || GTMPQstatus(conn) == CONNECTION_BAD)
    {
        elog(DEBUG3, "DEBUG3: Could not connect to %s, %d\n", host, port);
        return -1;
    }
    GTMPQfinish(conn);*/
    return 0;
}

