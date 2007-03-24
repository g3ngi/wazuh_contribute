/* @(#) $Id$ */

/* Copyright (C) 2005,2006 Daniel B. Cid <dcid@ossec.net>
 * All right reserved.
 *
 * This program is a free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation
 */


#include "shared.h"
#include <pthread.h>

#include "remoted.h"
#include "os_net/os_net.h"


/* pthread send_msg mutex */
pthread_mutex_t sendmsg_mutex;



/** void *AR_Forward(void *arg) v0.1
 * Start of a new thread. Only returns
 * on unrecoverable errors.
 */
void *AR_Forward(void *arg)
{
    int i = 0;
    int arq = 0;
    int agent_id = 0;
    int ar_location = 0;
    
    char msg_to_send[OS_SIZE_1024 +1];
    
    char msg[OS_SIZE_1024 +1];
    char *location = NULL;
    char *ar_location_str = NULL;
    char *ar_agent_id = NULL;
    char *tmp_str = NULL;


    /* Creating the unix queue */
    if((arq = StartMQ(ARQUEUE, READ)) < 0)
    {
        ErrorExit(QUEUE_ERROR, ARGV0, ARQUEUE, strerror(errno));
    }

    memset(msg, '\0', OS_SIZE_1024 +1);

    /* Daemon loop */
    while(1)
    {
        if(OS_RecvUnix(arq, OS_SIZE_1024, msg))
        {
            /* Always zeroing the location */
            ar_location = 0;
            
            
            /* Getting the location */
            location = msg;

            /* Location is going to be the agent name */
            tmp_str = strchr(msg, ')');
            if(!tmp_str)
            {
                merror(EXECD_INV_MSG, ARGV0, msg);
                continue;
            }
            *tmp_str = '\0';


            /* Going after the ')' and space */
            tmp_str += 2;


            /* Extracting the source ip */
            tmp_str = strchr(tmp_str, ' ');
            if(!tmp_str)
            {
                merror(EXECD_INV_MSG, ARGV0, msg);
                continue;
            }
            tmp_str++;
            location++;


            /* Setting ar_location */
            ar_location_str = tmp_str;
            if(*tmp_str == ALL_AGENTS_C)
            {
                ar_location|=ALL_AGENTS;
            }
            tmp_str++;
            if(*tmp_str == REMOTE_AGENT_C)
            {
                ar_location|=REMOTE_AGENT;
            }
            tmp_str++;
            if(*tmp_str == SPECIFIC_AGENT_C)
            {
                ar_location|=SPECIFIC_AGENT;
            }
            
            
            /*** Extracting the active response location ***/
            tmp_str = strchr(ar_location_str, ' ');
            if(!tmp_str)
            {
                merror(EXECD_INV_MSG, ARGV0, msg);
                continue;
            }
            *tmp_str = '\0';
            tmp_str++;


            /*** Extracting the agent id */
            ar_agent_id = tmp_str;
            tmp_str = strchr(tmp_str, ' ');
            if(!tmp_str)
            {
                merror(EXECD_INV_MSG, ARGV0, msg);
                continue;
            }
            *tmp_str = '\0';
            tmp_str++;
            
            
            /*** Creating the new message ***/
            snprintf(msg_to_send, OS_SIZE_1024, "%s%s%s", 
                                             CONTROL_HEADER,
                                             EXECD_HEADER,
                                             tmp_str);

            
            /* Sending to ALL agents */
            if(ar_location & ALL_AGENTS)
            {
                for(i = 0;i< keys.keysize; i++)
                {
                    send_msg(i, msg_to_send);
                }
            }

            /* Send to the remote agent that generated the event */
            else if((ar_location & REMOTE_AGENT) && (location != NULL))
            {
                agent_id = IsAllowedName(&keys, location);
                if(agent_id < 0)
                {
                    merror(AR_NOAGENT_ERROR, ARGV0, location);
                    continue;
                }
                
                send_msg(agent_id, msg_to_send);
            }

            /* Send to a pre-defined agent */
            else if(ar_location & SPECIFIC_AGENT)
            {
                ar_location++;

                agent_id = IsAllowedID(&keys, ar_agent_id);
                
                if(agent_id < 0)
                {
                    merror(AR_NOAGENT_ERROR, ARGV0, ar_agent_id);
                    continue;
                }

                send_msg(agent_id, msg_to_send);
            }
        }
    }
}

 
void send_msg_init()
{
    /* Initializing mutex */
    pthread_mutex_init(&sendmsg_mutex, NULL);
}


/* send_msg: 
 * Send message to an agent.
 * Returns -1 on error
 */
int send_msg(int agentid, char *msg)
{
    int msg_size;
    char crypt_msg[OS_MAXSTR +1];


    /* If we don't have the agent id, ignore it */
    if(keys.rcvd[agentid] < (time(0) - (2*NOTIFY_TIME)))
    {
        return(-1);
    }

    
    msg_size = CreateSecMSG(&keys, msg, crypt_msg, agentid);
    if(msg_size == 0)
    {
        merror(SEC_ERROR,ARGV0);
        return(-1);
    }

    
    /* Locking before using */
    if(pthread_mutex_lock(&sendmsg_mutex) != 0)
    {
        merror(MUTEX_ERROR, ARGV0);
        return(-1);
    }


    /* Sending initial message */
    if(sendto(logr.sock, crypt_msg, msg_size, 0,
                         (struct sockaddr *)&keys.peer_info[agentid],
                         logr.peer_size) < 0) 
    {
        merror(SEND_ERROR,ARGV0, keys.ids[agentid]);
    }
    
    
    /* Unlocking mutex */
    if(pthread_mutex_unlock(&sendmsg_mutex) != 0)
    {
        merror(MUTEX_ERROR, ARGV0);
        return(-1);
    }
                                        

    return(0);
}



/* EOF */
