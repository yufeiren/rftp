/*--------------------------------------------------------------- 
 * Copyright (c) 2010                              
 * BNL            
 * All Rights Reserved.                                           
 *--------------------------------------------------------------- 
 * Permission is hereby granted, free of charge, to any person    
 * obtaining a copy of this software (Iperf) and associated       
 * documentation files (the "Software"), to deal in the Software  
 * without restriction, including without limitation the          
 * rights to use, copy, modify, merge, publish, distribute,        
 * sublicense, and/or sell copies of the Software, and to permit     
 * persons to whom the Software is furnished to do
 * so, subject to the following conditions: 
 *
 *     
 * Redistributions of source code must retain the above 
 * copyright notice, this list of conditions and 
 * the following disclaimers. 
 *
 *     
 * Redistributions in binary form must reproduce the above 
 * copyright notice, this list of conditions and the following 
 * disclaimers in the documentation and/or other materials 
 * provided with the distribution. 
 * 
 *     
 * Neither the names of the University of Illinois, NCSA, 
 * nor the names of its contributors may be used to endorse 
 * or promote products derived from this Software without
 * specific prior written permission. 
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE CONTIBUTORS OR COPYRIGHT 
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, 
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. 
 * ________________________________________________________________
 * National Laboratory for Applied Network Research 
 * National Center for Supercomputing Applications 
 * University of Illinois at Urbana-Champaign 
 * http://www.ncsa.uiuc.edu
 * ________________________________________________________________ 
 *
 * rdma.h
 * by Yufei Ren <renyufei83@gmail.com>
 * -------------------------------------------------------------------
 * An abstract class for waiting on a condition variable. If
 * threads are not available, this does nothing.
 * ------------------------------------------------------------------- */

#ifndef RDMA_FTP_H
#define RDMA_FTP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <rdma/rdma_cma.h>
#include <infiniband/arch.h>

#include <semaphore.h>

#include "queue.h"

extern int rdma_debug;
#define DEBUG_LOG if (rdma_debug) printf
// #define DEBUG_LOG printf

// rdma_listen backlog
#define RLISTENBACKLOG		32

// rdma transfer mode
typedef enum RdmaTransMode {
    kRdmaTrans_ActRead = 0,
    kRdmaTrans_ActWrte,
    kRdmaTrans_PasRead,
    kRdmaTrans_PasWrte,
    kRdmaTrans_Unknown,
} RdmaTransMode;

/*
 * These states are used to signal events between the completion handler
 * and the main client or server thread.
 */
enum rdma_state_mac {
	IDLE = 1,
	CONNECT_REQUEST,
	ADDR_RESOLVED,
	ROUTE_RESOLVED,
	CONNECTED,
	SEND_DATA_REQ,
	RECV_DATA_REQ,
	ACTIVE_WRITE_ADV,
	ACTIVE_WRITE_RESP,
	ACTIVE_WRITE_POST,
	ACTIVE_WRITE_FIN,
	RDMA_WRITE_COMPLETE,
	RDMA_READ_COMPLETE,
	ERROR
/*	PASSIVE_READ,
	PASSIVE_WRITE,
	ACTIVE_READ,
	TRANS_COMPLETE,
	ERROR*/
};

struct rdma_info_blk {
	uint64_t buf;
	uint32_t rkey;
	uint32_t size;
	uint32_t mode;	/* for rdma transfer mode */
};


#define MODE_RDMA_ACTRD      0x00000001
#define MODE_RDMA_ACTWR      0x00000002
#define MODE_RDMA_PASRD      0x00000003
#define MODE_RDMA_PASWR      0x00000004

/*
 * Default max buffer size for IO...
 */
#define IPERF_BUFSIZE 64*1024
#define IPERF_RDMA_SQ_DEPTH 16

/* Default string for print data and
 * minimum buffer size
 */
#define _stringify( _x ) # _x
#define stringify( _x ) _stringify( _x )

extern int PseudoSock;
extern pthread_mutex_t PseudoSockCond;

/*
 * RDMA Control block struct.
 */
typedef struct rdma_cb {
	int server;			/* 0 iff client */
	pthread_t cqthread;
	pthread_t persistent_server_thread;
	struct ibv_comp_channel *channel;
	struct ibv_cq *cq;
	struct ibv_pd *pd;
	struct ibv_qp *qp;

	struct ibv_recv_wr rq_wr;	/* recv work request record */
	struct ibv_sge recv_sgl;	/* recv single SGE */
	struct rdma_info_blk recv_buf;  /* malloc'd buffer */
	struct ibv_mr *recv_mr;		/* MR associated with this buffer */

	struct ibv_send_wr sq_wr;	/* send work request record */
	struct ibv_sge send_sgl;	/* send single SGE */
	struct rdma_info_blk send_buf;  /* single send buf */
	struct ibv_mr *send_mr;		/* MR associated with this buffer */

	struct ibv_send_wr rdma_sink_sq_wr;	/* rdma work request record */
	struct ibv_sge rdma_sink_sgl;	/* rdma single SGE */
	char *rdma_sink_buf;	        /* used as rdma sink */
	struct ibv_mr *rdma_sink_mr;

	uint32_t remote_rkey;		/* remote guys RKEY */
	uint64_t remote_addr;		/* remote guys TO */
	uint32_t remote_len;		/* remote guys LEN */
	uint32_t remote_mode;		/* remote guys transfer MODE */

	struct ibv_send_wr rdma_source_sq_wr;	/* rdma work request record */
	struct ibv_sge rdma_source_sgl;	/* rdma single SGE */
	char *rdma_source_buf;		/* rdma read src */
	struct ibv_mr *rdma_source_mr;

	enum rdma_state_mac state;	/* used for cond/signalling */
	sem_t sem;

	struct sockaddr_storage sin;
	uint16_t port;			/* dst port in NBO */
	int verbose;			/* verbose logging */
	int count;			/* ping count */
	int size;			/* ping data size */
	int validate;			/* validate ping data */

	/* CM stuff */
	pthread_t cmthread;
	struct rdma_event_channel *cm_channel;
	struct rdma_cm_id *cm_id;	/* connection on client side,*/
					/* listener on server side. */
	struct rdma_cm_id *child_cm_id;	/* connection on server side */
} rdma_cb;


typedef struct wcm_id {
	struct rdma_cm_id *child_cm_id;
	
	TAILQ_ENTRY(wcm_id) entries;
} wcm_id;

TAILQ_HEAD(acptq, wcm_id);

typedef struct rmsgheader {
	uint32_t dlen;
} rmsgheader;

/* prototype - defined in rdma.c*/

int iperf_cma_event_handler(struct rdma_cm_id *cma_id,
				    struct rdma_cm_event *event);

void *cm_thread(void *arg);

int iperf_cq_event_handler(struct rdma_cb *cb);

void *cq_thread(void *arg);

int rdma_cb_init( struct rdma_cb *cb );

int rdma_cb_destroy( struct rdma_cb *cb );

int iperf_create_qp(struct rdma_cb *cb);

int iperf_setup_qp(struct rdma_cb *cb, struct rdma_cm_id *cm_id);

void iperf_free_qp(struct rdma_cb *cb);

int iperf_setup_buffers(struct rdma_cb *cb);

void iperf_free_buffers(struct rdma_cb *cb);

void iperf_setup_wr(struct rdma_cb *cb);

int rdma_connect_client(struct rdma_cb *cb);

int iperf_accept(struct rdma_cb *cb);

void iperf_format_send(struct rdma_cb *cb, char *buf, struct ibv_mr *mr);

/* data transfer method */

int cli_act_rdma_rd(struct rdma_cb *cb);
int cli_act_rdma_wr(struct rdma_cb *cb);
int cli_pas_rdma_rd(struct rdma_cb *cb);
int cli_pas_rdma_wr(struct rdma_cb *cb);

int svr_act_rdma_rd(struct rdma_cb *cb);
int svr_act_rdma_wr(struct rdma_cb *cb);
int svr_pas_rdma_rd(struct rdma_cb *cb);
int svr_pas_rdma_wr(struct rdma_cb *cb);

ssize_t	 readn(int, void *, size_t);		/* from APUE2e */
ssize_t	 writen(int, const void *, size_t);	/* from APUE2e */

#ifdef __cplusplus
} /* end extern "C" */
#endif

#endif
