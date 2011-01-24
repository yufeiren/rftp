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
 * rdma.c
 * by Yufei Ren <renyufei83@gmail.com>
 * -------------------------------------------------------------------
 * An abstract class for waiting on a condition variable. If
 * threads are not available, this does nothing.
 * ------------------------------------------------------------------- */

/* standard C headers */
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <math.h>
/* required on AIX for FD_SET (requires bzero).
 * often this is the same as <string.h> */
    #include <strings.h>

/* unix headers */
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <signal.h>
    #include <unistd.h>

/** Added for daemonizing the process */
    #include <syslog.h>

/** Added for semaphore */
    #include <semaphore.h>

/** Added for PRIx64 */
    #include <inttypes.h>

    #include <netdb.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>

    #include <arpa/inet.h>   /* netinet/in.h must be before this on SunOS */

    #include <pthread.h>

#include "rdma.h"
#include "errors.h"

extern struct acptq acceptedTqh;

static int server_recv(struct rdma_cb *cb, struct ibv_wc *wc)
{
	if (wc->byte_len != sizeof(cb->recv_buf)) {
		fprintf(stderr, "Received bogus data, size %d\n", wc->byte_len);
		return -1;
	}

	cb->remote_rkey = ntohl(cb->recv_buf.rkey);
	cb->remote_addr = ntohll(cb->recv_buf.buf);
	cb->remote_len  = ntohl(cb->recv_buf.size);
	cb->remote_mode = ntohl(cb->recv_buf.mode);
	DEBUG_LOG("Received rkey %x addr %" PRIx64 " len %d from peer\n",
		  cb->remote_rkey, cb->remote_addr, cb->remote_len);
	
/*	switch ( cb->remote_mode ) {
	case MODE_RDMA_ACTRD:
		cb->trans_mode = kRdmaTrans_PasRead;
		break;
	case MODE_RDMA_ACTWR:
		cb->trans_mode = kRdmaTrans_PasWrte;
		break;
	case MODE_RDMA_PASRD:
		cb->trans_mode = kRdmaTrans_ActRead;
		break;
	case MODE_RDMA_PASWR:
		cb->trans_mode = kRdmaTrans_ActWrte;
		break;
	default:
		fprintf(stderr, "unrecognize transfer mode %d\n", \
			cb->remote_mode);
		break;
	}*/
	
/*	if (cb->state <= CONNECTED || cb->state == ACTIVE_WRITE_FIN)
		cb->state = RDMA_READ_ADV;
	else
		cb->state = RDMA_WRITE_ADV; */
	
	return 0;
}

static int client_recv(struct rdma_cb *cb, struct ibv_wc *wc)
{
	if (wc->byte_len != sizeof(cb->recv_buf)) {
		fprintf(stderr, "Received bogus data, size %d\n", wc->byte_len);
		return -1;
	}
/*
	if (cb->state == RDMA_READ_ADV)
		cb->state = RDMA_WRITE_ADV;
	else
		cb->state = RDMA_WRITE_COMPLETE;
*/
	return 0;
}

static int do_recv(struct rdma_cb *cb, struct ibv_wc *wc)
{
	struct ibv_send_wr *bad_wr;

	if (wc->byte_len != sizeof(cb->recv_buf)) {
		fprintf(stderr, "Received bogus data, size %d\n", wc->byte_len);
		return -1;
	}
	
	if (cb->recv_buf.mode == kRdmaTrans_ActWrte) {
		switch (cb->recv_buf.stat) {
		case ACTIVE_WRITE_ADV:
/*			iperf_format_send(cb, cb->rdma_sink_buf, cb->rdma_sink_mr); */
			cb->send_buf.stat = ACTIVE_WRITE_RESP;
			/* ibv_post_send(cb->qp, &cb->sq_wr, &bad_wr); */
			break;
		case ACTIVE_WRITE_RESP:
			cb->remote_rkey = ntohl(cb->recv_buf.rkey);
			cb->remote_addr = ntohll(cb->recv_buf.buf);
			cb->remote_len  = ntohl(cb->recv_buf.size);
			break;
		case ACTIVE_WRITE_FIN:
			break;
		default:
			fprintf(stderr, "unrecognized stat %d\n", cb->recv_buf.stat);
			break;
		}
	}

	DEBUG_LOG("Received rkey %x addr %" PRIx64 " len %d from peer\n",
		  cb->remote_rkey, cb->remote_addr, cb->remote_len);
	
	return 0;
}


int iperf_cma_event_handler(struct rdma_cm_id *cma_id,
				    struct rdma_cm_event *event)
{
	int ret = 0;
	struct rdma_cb *cb = cma_id->context;

	DEBUG_LOG("cma_event type %s cma_id %p (%s)\n",
		  rdma_event_str(event->event), cma_id,
		  (cma_id == cb->cm_id) ? "parent" : "child");

	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		cb->state = ADDR_RESOLVED;
		ret = rdma_resolve_route(cma_id, 2000);
		if (ret) {
			cb->state = STATE_ERROR;
			perror("rdma_resolve_route");
			sem_post(&cb->sem);
		}
		break;

	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		cb->state = ROUTE_RESOLVED;
		sem_post(&cb->sem);
		break;

	case RDMA_CM_EVENT_CONNECT_REQUEST:
		cb->state = CONNECT_REQUEST;
		
		TAILQ_LOCK(&acceptedTqh);
		struct wcm_id *item = \
			(struct wcm_id *) malloc(sizeof(struct wcm_id));
		if (item == NULL) {
			fprintf(stderr, "Out of Memory\n");
			exit(EXIT_FAILURE);
		}
		
		item->child_cm_id = cma_id;
				
		TAILQ_INSERT_TAIL(&acceptedTqh, item, entries);
		
		TAILQ_UNLOCK(&acceptedTqh);
		TAILQ_SIGNAL(&acceptedTqh);
		cb->child_cm_id = cma_id;
		DEBUG_LOG("child cma %p\n", cb->child_cm_id);
//		sem_post(&cb->sem);
		break;

	case RDMA_CM_EVENT_ESTABLISHED:
		DEBUG_LOG("ESTABLISHED\n");
		/*
		 * Server will wake up when first RECV completes.
		 */
		if (!cb->server) {
			cb->state = CONNECTED;
		}
		sem_post(&cb->sem);
		DEBUG_LOG("child cma %p\n", cb->child_cm_id);
		break;

	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
	case RDMA_CM_EVENT_REJECTED:
		fprintf(stderr, "cma event %s, error %d\n",
			rdma_event_str(event->event), event->status);
		sem_post(&cb->sem);
		ret = -1;
		break;

	case RDMA_CM_EVENT_DISCONNECTED:
		fprintf(stderr, "RDMA %s DISCONNECT EVENT...\n",
			cb->server ? "server" : "client");
		sem_post(&cb->sem);
		break;

	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		fprintf(stderr, "cma detected device removal!!!!\n");
		ret = -1;
		break;

	default:
		fprintf(stderr, "unhandled event: %s, ignoring\n",
			rdma_event_str(event->event));
		break;
	}

	return ret;
}

void *cm_thread(void *arg) {
	struct rdma_cb *cb = arg;
	struct rdma_cm_event *event;
	int ret;

	while (1) {
		ret = rdma_get_cm_event(cb->cm_channel, &event);
		if (ret) {
			perror("rdma_get_cm_event");
			exit(ret);
		}
		ret = iperf_cma_event_handler(event->id, event);
		rdma_ack_cm_event(event);
		if (ret)
			exit(ret);
	}
}


int iperf_cq_event_handler(struct rdma_cb *cb)
{
	struct ibv_wc wc;
	struct ibv_recv_wr *bad_wr;
	int ret;
	
	DPRINTF(("2 cq_handler cb @ %x\n", (unsigned long)cb));
	DPRINTF(("cq_handler sem_post @ %x\n", (unsigned long)&cb->sem));
	
	while ((ret = ibv_poll_cq(cb->cq, 1, &wc)) == 1) {
		ret = 0;

		if (wc.status) {
			syslog(LOG_ERR, "cq completion failed status %d", \
				wc.status);
			// IBV_WC_WR_FLUSH_ERR == 5
			if (wc.status != IBV_WC_WR_FLUSH_ERR)
				ret = -1;
			goto error;
		}

		switch (wc.opcode) {
		case IBV_WC_SEND:
			DEBUG_LOG("send completion\n");
			break;

		case IBV_WC_RDMA_WRITE:
			DEBUG_LOG("rdma write completion\n");
			cb->state = RDMA_WRITE_COMPLETE;
			sem_post(&cb->sem);
			break;

		case IBV_WC_RDMA_READ:
			DEBUG_LOG("rdma read completion\n");
			cb->state = RDMA_READ_COMPLETE;
			sem_post(&cb->sem);
			break;

		case IBV_WC_RECV:
			DEBUG_LOG("recv completion\n");
			DPRINTF(("IBV_WC_RECV cb->server %d\n", cb->server));
/*			ret = cb->server ? server_recv(cb, &wc) :
					   client_recv(cb, &wc);
*/
			ret = do_recv(cb, &wc);
			if (ret) {
				fprintf(stderr, "recv wc error: %d\n", ret);
				goto error;
			}

			ret = ibv_post_recv(cb->qp, &cb->rq_wr, &bad_wr);
			if (ret) {
				fprintf(stderr, "post recv error: %d\n", ret);
				goto error;
			}

			sem_post(&cb->sem);
			DPRINTF(("IBV_WC_RECV success\n"));
			break;

		default:
			DEBUG_LOG("unknown!!!!! completion\n");
			ret = -1;
			goto error;
		}
	}
	if (ret) {
		fprintf(stderr, "poll error %d\n", ret);
		goto error;
	}
	return 0;

error:
	cb->state = STATE_ERROR;
	sem_post(&cb->sem);
	return ret;
}


void *cq_thread(void *arg)
{
	struct rdma_cb *cb = arg;
	struct ibv_cq *ev_cq;
	void *ev_ctx;
	int ret;
	
	DEBUG_LOG("cq_thread started\n");

	while (1) {
		pthread_testcancel();

		ret = ibv_get_cq_event(cb->channel, &ev_cq, &ev_ctx);
		if (ret) {
			fprintf(stderr, "Failed to get cq event!\n");
			syslog(LOG_ERR, "cq_thread: Failed to get cq event!");
			pthread_exit(NULL);
		}
		if (ev_cq != cb->cq) {
			fprintf(stderr, "Unknown CQ!\n");
			syslog(LOG_ERR, "cq_thread: Unknown CQ!");
			pthread_exit(NULL);
		}
		ret = ibv_req_notify_cq(cb->cq, 0);
		if (ret) {
			fprintf(stderr, "Failed to set notify!\n");
			syslog(LOG_ERR, "Failed to set notify!");
			pthread_exit(NULL);
		}

		ret = iperf_cq_event_handler(cb);
		ibv_ack_cq_events(cb->cq, 1);
		if (ret)
			pthread_exit(NULL);
	}
}


int rdma_cb_init( struct rdma_cb *cb ) {
	int ret = 0;
	
//	sem_init(&cb->sem, 0, 0);

/*	cb = malloc(sizeof(*cb));
	if (!cb)
		return -ENOMEM;

	// rdma_thr->cb = cb;
*/
	cb->cm_channel = rdma_create_event_channel();
	if (!cb->cm_channel) {
		perror("rdma_create_event_channel");
		free(cb);
		return -1;
	}

	ret = rdma_create_id(cb->cm_channel, &cb->cm_id, cb, RDMA_PS_TCP);
	if (ret) {
		perror("rdma_create_id");
		rdma_destroy_event_channel(cb->cm_channel);
		free(cb);
		return -1;
	}

	if (pthread_create(&cb->cmthread, NULL, cm_thread, cb) != 0)
		perror("pthread_create");

/*	if (cb->server) {
		if (persistent_server)
			ret = rping_run_persistent_server(cb);
		else
			ret = rping_run_server(cb);
	} else
		ret = rping_run_client(cb);
*/
	// rdma_destroy_id(cb->cm_id);
	
	return 0;
}

int rdma_cb_destroy( struct rdma_cb *cb )
{
	return 0;
}


// setup queue pair
int iperf_setup_qp(struct rdma_cb *cb, struct rdma_cm_id *cm_id)
{
	int ret;

	cb->pd = ibv_alloc_pd(cm_id->verbs);
	if (!cb->pd) {
		fprintf(stderr, "ibv_alloc_pd failed\n");
		return errno;
	}
	DEBUG_LOG("created pd %p\n", cb->pd);

	cb->channel = ibv_create_comp_channel(cm_id->verbs);
	if (!cb->channel) {
		fprintf(stderr, "ibv_create_comp_channel failed\n");
		ret = errno;
		goto err1;
	}
	DEBUG_LOG("created channel %p\n", cb->channel);

	cb->cq = ibv_create_cq(cm_id->verbs, IPERF_RDMA_SQ_DEPTH * 2, cb,
				cb->channel, 0);
	if (!cb->cq) {
		fprintf(stderr, "ibv_create_cq failed\n");
		ret = errno;
		goto err2;
	}
	DEBUG_LOG("created cq %p\n", cb->cq);
	
	DEBUG_LOG("before ibv_req_notify_cq\n");
	ret = ibv_req_notify_cq(cb->cq, 0);
	if (ret) {
		fprintf(stderr, "ibv_create_cq failed\n");
		ret = errno;
		goto err3;
	}

	DPRINTF(("before iperf_create_qp\n"));
	ret = iperf_create_qp(cb);
	if (ret) {
		perror("rdma_create_qp");
		goto err3;
	}
	DEBUG_LOG("created qp %p\n", cb->qp);
	return 0;

err3:
	ibv_destroy_cq(cb->cq);
err2:
	ibv_destroy_comp_channel(cb->channel);
err1:
	ibv_dealloc_pd(cb->pd);
	return ret;
}


int iperf_create_qp(struct rdma_cb *cb)
{
	struct ibv_qp_init_attr init_attr;
	int ret;

	memset(&init_attr, 0, sizeof(init_attr));
	init_attr.cap.max_send_wr = IPERF_RDMA_SQ_DEPTH;
	init_attr.cap.max_recv_wr = 2;
	init_attr.cap.max_recv_sge = 1;
	init_attr.cap.max_send_sge = 1;
	init_attr.qp_type = IBV_QPT_RC;
	init_attr.send_cq = cb->cq;
	init_attr.recv_cq = cb->cq;

	DPRINTF(("before rdma_create_qp\n"));
	DPRINTF(("cb->server: %d\n", cb->server));
	if (cb->server) {
		ret = rdma_create_qp(cb->child_cm_id, cb->pd, &init_attr);
		if (!ret)
			cb->qp = cb->child_cm_id->qp;
	} else {
		ret = rdma_create_qp(cb->cm_id, cb->pd, &init_attr);
		if (!ret)
			cb->qp = cb->cm_id->qp;
	}
	DPRINTF(("after rdma_create_qp, ret = %d\n", ret));

	return ret;
}

void iperf_free_qp(struct rdma_cb *cb)
{
	ibv_destroy_qp(cb->qp);
	ibv_destroy_cq(cb->cq);
	ibv_destroy_comp_channel(cb->channel);
	ibv_dealloc_pd(cb->pd);
}


int iperf_setup_buffers(struct rdma_cb *cb)
{
	int ret;

	DEBUG_LOG("rping_setup_buffers called on cb %p\n", cb);

/* recv_mr */
	cb->recv_mr = ibv_reg_mr(cb->pd, &cb->recv_buf, sizeof cb->recv_buf,
				 IBV_ACCESS_LOCAL_WRITE);
	if (!cb->recv_mr) {
		fprintf(stderr, "recv_buf reg_mr failed\n");
		syslog(LOG_ERR, "iperf_setup_buffers ibv_reg_mr recv_mr");
		return errno;
	}

/* send_mr */
	cb->send_mr = ibv_reg_mr(cb->pd, &cb->send_buf, sizeof cb->send_buf, 0);
	if (!cb->send_mr) {
		fprintf(stderr, "send_buf reg_mr failed\n");
		syslog(LOG_ERR, "iperf_setup_buffers ibv_reg_mr send_mr");
		ret = errno;
		goto err1;
	}

/* rdma_sink_mr
	cb->rdma_sink_buf = malloc(cb->size + sizeof(rmsgheader));
	if (!cb->rdma_sink_buf) {
		fprintf(stderr, "rdma_sink_buf malloc failed\n");
		syslog(LOG_ERR, "iperf_setup_buffers malloc rdma_sink_buf");
		ret = -ENOMEM;
		goto err2;
	}

	cb->rdma_sink_mr = ibv_reg_mr(cb->pd, cb->rdma_sink_buf, cb->size + sizeof(rmsgheader), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
	if (!cb->rdma_sink_mr) {
		fprintf(stderr, "rdma_sink_mr reg_mr failed\n");
		syslog(LOG_ERR, "iperf_setup_buffers ibv_reg_mr rdma_sink_buf");
		ret = errno;
		goto err3;
	} */

/* rdma_source_mr
	cb->rdma_source_buf = malloc(cb->size + sizeof(rmsgheader));
	if (!cb->rdma_source_buf) {
		fprintf(stderr, "rdma_source_buf malloc failed\n");
		syslog(LOG_ERR, "iperf_setup_buffers malloc rdma_source_buf");
		ret = -ENOMEM;
		goto err4;
	}

	cb->rdma_source_mr = ibv_reg_mr(cb->pd, cb->rdma_source_buf, cb->size + sizeof(rmsgheader),
				 IBV_ACCESS_LOCAL_WRITE |
				 IBV_ACCESS_REMOTE_READ);
	if (!cb->rdma_source_mr) {
		fprintf(stderr, "rdma_source_mr reg_mr failed\n");
		syslog(LOG_ERR, "iperf_setup_buffers ibv_reg_mr rdma_source_mr");
		ret = errno;
		goto err5;
	} */

	iperf_setup_wr(cb);
	DEBUG_LOG("allocated & registered buffers...\n");
	return 0;

err5:
	free(cb->rdma_source_buf);
err4:
	ibv_dereg_mr(cb->rdma_sink_mr);
err3:
	free(cb->rdma_sink_buf);
err2:
	ibv_dereg_mr(cb->send_mr);
err1:
	ibv_dereg_mr(cb->recv_mr);
	return ret;
}


int tsf_setup_buf_list(struct rdma_cb *cb)
{
/* init rdma_mr and insert into the free list */
	int i;
	BUFDATBLK *item;
	
	for (i = 0; i < 30; i++) {
		if ( (item = (BUFDATBLK *) malloc(sizeof(BUFDATBLK))) == NULL) {
			perror("tsf_setup_buf_list: malloc");
			exit(EXIT_FAILURE);
		}
		
		memset(item, '\0', sizeof(BUFDATBLK));
		
		if ( (item->rdma_buf = (char *) malloc(cb->size + sizeof(rmsgheader))) == NULL) {
			perror("tsf_setup_buf_list: malloc 2");
			exit(EXIT_FAILURE);
		}
		
		memset(item->rdma_buf, '\0', cb->size + sizeof(rmsgheader));
		
		item->rdma_mr = ibv_reg_mr(cb->pd, item->rdma_buf, \
				cb->size + sizeof(rmsgheader), \
				IBV_ACCESS_LOCAL_WRITE
				| IBV_ACCESS_REMOTE_READ
				| IBV_ACCESS_REMOTE_WRITE);
		if (!item->rdma_mr) {
			perror("tsf_setup_buf_list: ibv_reg_mr");
			exit(EXIT_FAILURE);
		}
		
		item->buflen = cb->size + sizeof(rmsgheader);
		
		TAILQ_INSERT_TAIL(&free_tqh, item, entries);
	}
	
	return 0;
}

void
tsf_free_buf_list(void)
{
	BUFDATBLK *item;
	
	/* free free list - not thread safe */
	for ( ; ; ) {
		if (TAILQ_EMPTY(&free_tqh))
			break;
		
		item = TAILQ_FIRST(&free_tqh);
		TAILQ_REMOVE(&free_tqh, item, entries);
		
		ibv_dereg_mr(item->rdma_mr);
		
		free(item->rdma_buf);
	}
	
	return;
}

void iperf_free_buffers(struct rdma_cb *cb)
{
	DEBUG_LOG("free_buffers called on cb %p\n", cb);
	ibv_dereg_mr(cb->recv_mr);
	ibv_dereg_mr(cb->send_mr);
/*	ibv_dereg_mr(cb->rdma_sink_mr);
	ibv_dereg_mr(cb->rdma_source_mr);
	free(cb->rdma_sink_buf);
	free(cb->rdma_source_buf); */
}


void iperf_setup_wr(struct rdma_cb *cb)
{
	cb->recv_sgl.addr = (uint64_t) (unsigned long) &cb->recv_buf;
	cb->recv_sgl.length = sizeof cb->recv_buf;
	cb->recv_sgl.lkey = cb->recv_mr->lkey;
	cb->rq_wr.sg_list = &cb->recv_sgl;
	cb->rq_wr.num_sge = 1;

	cb->send_sgl.addr = (uint64_t) (unsigned long) &cb->send_buf;
	cb->send_sgl.length = sizeof cb->send_buf;
	cb->send_sgl.lkey = cb->send_mr->lkey;

	cb->sq_wr.opcode = IBV_WR_SEND;
	cb->sq_wr.send_flags = IBV_SEND_SIGNALED;
	cb->sq_wr.sg_list = &cb->send_sgl;
	cb->sq_wr.num_sge = 1;
/*
	cb->rdma_sink_sgl.addr = (uint64_t) (unsigned long) cb->rdma_sink_buf;
	cb->rdma_sink_sgl.lkey = cb->rdma_sink_mr->lkey;
	cb->rdma_sink_sq_wr.send_flags = IBV_SEND_SIGNALED;
	cb->rdma_sink_sq_wr.sg_list = &cb->rdma_sink_sgl;
	cb->rdma_sink_sq_wr.num_sge = 1;
	
	cb->rdma_source_sgl.addr = (uint64_t) (unsigned long) cb->rdma_source_buf;
	cb->rdma_source_sgl.lkey = cb->rdma_source_mr->lkey;
	cb->rdma_source_sq_wr.send_flags = IBV_SEND_SIGNALED;
	cb->rdma_source_sq_wr.sg_list = &cb->rdma_source_sgl;
	cb->rdma_source_sq_wr.num_sge = 1; */
}

void tsf_setup_wr(BUFDATBLK *bufblk)
{
	bufblk->rdma_sgl.addr = (uint64_t) (unsigned long) bufblk->rdma_buf;
	bufblk->rdma_sgl.lkey = bufblk->rdma_mr->lkey;
	bufblk->rdma_sq_wr.send_flags = IBV_SEND_SIGNALED;
	bufblk->rdma_sq_wr.sg_list = &bufblk->rdma_sgl;
	bufblk->rdma_sq_wr.num_sge = 1;
}

int rdma_connect_client(struct rdma_cb *cb)
{
	struct rdma_conn_param conn_param;
	int ret;

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;
	conn_param.retry_count = 10;

	ret = rdma_connect(cb->cm_id, &conn_param);
	if (ret) {
		perror("rdma_connect");
		return ret;
	}

	sem_wait(&cb->sem);
	if (cb->state != CONNECTED) {
		fprintf(stderr, "wait for CONNECTED state %d\n", cb->state);
		return -1;
	}

	DEBUG_LOG("rdma_connect successful\n");
	return 0;
}


int iperf_accept(struct rdma_cb *cb)
{
	struct rdma_conn_param conn_param;
	int ret;

	DEBUG_LOG("accepting client connection request\n");

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;

	DPRINTF(("tid %ld, child_cm_id %p\n", pthread_self(), cb->child_cm_id));

	ret = rdma_accept(cb->child_cm_id, &conn_param);
	if (ret) {
		perror("rdma_accept");
		return ret;
	}

	sem_wait(&cb->sem);
	if (cb->state == STATE_ERROR) {
		fprintf(stderr, "wait for CONNECTED state %d\n", cb->state);
		return -1;
	}
	DPRINTF(("iperf_accept finish with state %d\n", cb->state));
	return 0;
}


void iperf_format_send(struct rdma_cb *cb, char *buf, struct ibv_mr *mr)
{
	struct rdma_info_blk *info = &cb->send_buf;

	info->buf = htonll((uint64_t) (unsigned long) buf);
	info->rkey = htonl(mr->rkey);
	info->size = htonl(cb->size + sizeof(rmsgheader));
	
/*	switch ( cb->trans_mode ) {
	case kRdmaTrans_ActRead:
		info->mode = htonl(MODE_RDMA_ACTRD);
		break;
	case kRdmaTrans_ActWrte:
		info->mode = htonl(MODE_RDMA_ACTWR);
		break;
	case kRdmaTrans_PasRead:
		info->mode = htonl(MODE_RDMA_PASRD);
		break;
	case kRdmaTrans_PasWrte:
		info->mode = htonl(MODE_RDMA_PASWR);
		break;
	default:
		fprintf(stderr, "unrecognize transfer mode %d\n", \
			cb->trans_mode);
		break;
	}
*/
	DEBUG_LOG("RDMA addr %" PRIx64" rkey %x len %d\n",
		  ntohll(info->buf), ntohl(info->rkey), ntohl(info->size));
}


ssize_t             /* Read "n" bytes from a descriptor  */
readn(int fd, void *ptr, size_t n)
{
	size_t		nleft;
	ssize_t		nread;

	nleft = n;
	while (nleft > 0) {
		if ((nread = read(fd, ptr, nleft)) < 0) {
			if (nleft == n)
				return(-1); /* error, return -1 */
			else
				break;      /* error, return amount read so far */
		} else if (nread == 0) {
			break;          /* EOF */
		}
		nleft -= nread;
		ptr   += nread;
	}
	return(n - nleft);      /* return >= 0 */
}

ssize_t             /* Write "n" bytes to a descriptor  */
writen(int fd, const void *ptr, size_t n)
{
	size_t		nleft;
	ssize_t		nwritten;

	nleft = n;
	while (nleft > 0) {
		if ((nwritten = write(fd, ptr, nleft)) < 0) {
			if (nleft == n)
				return(-1); /* error, return -1 */
			else
				break;      /* error, return amount written so far */
		} else if (nwritten == 0) {
			break;
		}
		nleft -= nwritten;
		ptr   += nwritten;
	}
	return(n - nleft);      /* return >= 0 */
}



void *
sender(void *arg)
{
	off_t totallen;
	off_t currlen;
	int thislen;
	BUFDATBLK *bufblk;
	
	struct rdma_cb *cb = (struct rdma_cb *) arg;
	totallen = cb->filesize;
	
/*	for (currlen = 0; currlen < totallen; currlen += thislen) { */
	for (currlen = 0; ; currlen += thislen) {
		/* get send block */
		TAILQ_LOCK(&sender_tqh);
		while (TAILQ_EMPTY(&sender_tqh))
			if (TAILQ_WAIT(&sender_tqh) != 0)
				continue;	/* goto while */
		
		bufblk = TAILQ_FIRST(&sender_tqh);
		TAILQ_REMOVE(&sender_tqh, bufblk, entries);
		
		TAILQ_UNLOCK(&sender_tqh);
		
		/* send data */
		thislen = send_dat_blk(bufblk, cb);
		DPRINTF(("send %d bytes\n", thislen));
		
		/* insert to free list */
		TAILQ_LOCK(&free_tqh);
		TAILQ_INSERT_TAIL(&free_tqh, bufblk, entries);
		TAILQ_UNLOCK(&free_tqh);
		
		TAILQ_SIGNAL(&free_tqh);
		
		if (thislen == 0)
			break;
	}
	
	pthread_exit((void *) currlen);
}

void *
recver(void *arg)
{
	BUFDATBLK *bufblk;
	struct rdma_cb *cb = (struct rdma_cb *) arg;
	struct ibv_send_wr *bad_wr;
	int ret;
	off_t currlen;
	int thislen;
	
	for (currlen = 0; ; currlen += thislen) {
		/* get a free block */
		TAILQ_LOCK(&free_tqh);
		while (TAILQ_EMPTY(&free_tqh))
			if (TAILQ_WAIT(&free_tqh) != 0)
				continue;	/* goto while */
		
		bufblk = TAILQ_FIRST(&free_tqh);
		TAILQ_REMOVE(&free_tqh, bufblk, entries);
		
		TAILQ_UNLOCK(&free_tqh);
	
		/* recv data */
		thislen = recv_dat_blk(bufblk, cb);

		/* insert into writer list */
		TAILQ_LOCK(&writer_tqh);
		TAILQ_INSERT_TAIL(&writer_tqh, bufblk, entries);
		TAILQ_UNLOCK(&writer_tqh);
		
		TAILQ_SIGNAL(&writer_tqh);
		
		if (thislen == 0)
			break;
	}
	
	pthread_exit((void *) currlen);
}

void *
reader(void *arg)
{
	off_t totallen;
	off_t currlen;
	int thislen;
	BUFDATBLK *bufblk;
	rmsgheader rhdr;
	
	struct rdma_cb *cb = (struct rdma_cb *) arg;
	totallen = cb->filesize;
	
	for (currlen = 0; currlen < totallen; currlen += thislen) {
		/* get free block */
		TAILQ_LOCK(&free_tqh);
		while (TAILQ_EMPTY(&free_tqh))
			if (TAILQ_WAIT(&free_tqh) != 0)
				continue;	/* goto while */
		
		bufblk = TAILQ_FIRST(&free_tqh);
		TAILQ_REMOVE(&free_tqh, bufblk, entries);
		
		TAILQ_UNLOCK(&free_tqh);
		
		/* load data */
		bufblk->fd = cb->fd;
		thislen = load_dat_blk(bufblk);
		
		DPRINTF(("load %d bytes\n", thislen));
		
		if (thislen <= 0) {
			TAILQ_LOCK(&free_tqh);
			TAILQ_INSERT_TAIL(&free_tqh, bufblk, entries);
			TAILQ_UNLOCK(&free_tqh);
			break;
		}
		
		rhdr.dlen = thislen;
		memcpy(bufblk->rdma_buf, &rhdr, sizeof(rmsgheader));
		
		/* insert to sender list */
		TAILQ_LOCK(&sender_tqh);
		TAILQ_INSERT_TAIL(&sender_tqh, bufblk, entries);
		TAILQ_UNLOCK(&sender_tqh);
		
		TAILQ_SIGNAL(&sender_tqh);
	}
	
	/* generate a zero package */
	/* get free block */
	TAILQ_LOCK(&free_tqh);
	while (TAILQ_EMPTY(&free_tqh))
		if (TAILQ_WAIT(&free_tqh) != 0)
			continue;	/* goto while */
	
	bufblk = TAILQ_FIRST(&free_tqh);
	TAILQ_REMOVE(&free_tqh, bufblk, entries);
	
	TAILQ_UNLOCK(&free_tqh);
	
	rhdr.dlen = 0;
	memcpy(bufblk->rdma_buf, &rhdr, sizeof(rmsgheader));
	
	/* insert to sender list */
	TAILQ_LOCK(&sender_tqh);
	TAILQ_INSERT_TAIL(&sender_tqh, bufblk, entries);
	TAILQ_UNLOCK(&sender_tqh);
	
	TAILQ_SIGNAL(&sender_tqh);
	
	/* data read finished */
	pthread_exit((void *) currlen);
}

void *
writer(void *arg)
{
	BUFDATBLK *bufblk;
	rmsgheader rhdr;
	off_t currlen;
	int thislen;
	
	struct rdma_cb *cb = (struct rdma_cb *) arg;

	for (currlen = 0 ; ; currlen += thislen) {
		/* get write block */
		TAILQ_LOCK(&writer_tqh);
		while (TAILQ_EMPTY(&writer_tqh))
			if (TAILQ_WAIT(&writer_tqh) != 0)
				continue;	/* goto while */
		
		bufblk = TAILQ_FIRST(&writer_tqh);
		TAILQ_REMOVE(&writer_tqh, bufblk, entries);
		
		TAILQ_UNLOCK(&writer_tqh);
		
		/* offload data */
		bufblk->fd = cb->fd;
		memcpy(&rhdr, bufblk->rdma_buf, sizeof(rmsgheader));
		bufblk->buflen = rhdr.dlen + sizeof(rmsgheader);
		thislen = rhdr.dlen;
		
		offload_dat_blk(bufblk);
		
		/* insert to free list */
		TAILQ_LOCK(&free_tqh);
		TAILQ_INSERT_TAIL(&free_tqh, bufblk, entries);
		TAILQ_UNLOCK(&free_tqh);
		
		TAILQ_SIGNAL(&free_tqh);
		
		if (thislen == 0)
			break;
	}
	
	pthread_exit((void *) currlen);
}

int
load_dat_blk(BUFDATBLK *bufblk)
{
	return readn(bufblk->fd, bufblk->rdma_buf + sizeof(rmsgheader), bufblk->buflen - sizeof(rmsgheader));
}

int
offload_dat_blk(BUFDATBLK *bufblk)
{
	return writen(bufblk->fd, bufblk->rdma_buf + sizeof(rmsgheader), bufblk->buflen - sizeof(rmsgheader));
}


int
send_dat_blk(BUFDATBLK *bufblk, struct rdma_cb *dc_cb)
{
	struct ibv_send_wr *bad_wr;
	int ret;
	rmsgheader rhdr;
	
	memcpy(&rhdr, bufblk->rdma_buf, sizeof(rmsgheader));
	
	/* setup wr */
	tsf_setup_wr(bufblk);
	
	/* talk to peer what type of transfer to use */
	dc_cb->send_buf.mode = kRdmaTrans_ActWrte;
	dc_cb->send_buf.stat = ACTIVE_WRITE_ADV;
	ret = ibv_post_send(dc_cb->qp, &dc_cb->sq_wr, &bad_wr);
	if (ret) {
		fprintf(stderr, "post send error %d\n", ret);
		return 0;
	}
	dc_cb->state = ACTIVE_WRITE_ADV;
	
	/* wait the peer tell me where should i write to */
	sem_wait(&dc_cb->sem);
/*	if (child_dc_cb->state != ACTIVE_WRITE_RESP) {
		fprintf(stderr, \
			"wait for ACTIVE_WRITE_RESP state %d\n", \
			child_dc_cb->state);
		return;
	} */
	
	/* start data transfer using RDMA_WRITE */
	bufblk->rdma_sq_wr.opcode = IBV_WR_RDMA_WRITE;
	bufblk->rdma_sq_wr.wr.rdma.rkey = dc_cb->remote_rkey;
	bufblk->rdma_sq_wr.wr.rdma.remote_addr = dc_cb->remote_addr;
	bufblk->rdma_sq_wr.sg_list->length = rhdr.dlen + sizeof(rmsgheader);
	
	DPRINTF(("start data transfer using RDMA_WRITE\n"));
	DEBUG_LOG("rdma write from lkey %x laddr %x len %d\n",
		  bufblk->rdma_sq_wr.sg_list->lkey,
		  bufblk->rdma_sq_wr.sg_list->addr,
		  bufblk->rdma_sq_wr.sg_list->length);
	
	ret = ibv_post_send(dc_cb->qp, &bufblk->rdma_sq_wr, &bad_wr);
	if (ret) {
		fprintf(stderr, "post send error %d\n", ret);
		return 0;
	}
	dc_cb->state != ACTIVE_WRITE_POST;
	DPRINTF(("send_dat_blk: ibv_post_send finish\n"));
	/* wait the finish of RDMA_WRITE */
	sem_wait(&dc_cb->sem);
	DPRINTF(("sem_wait finish of RDMA_WRITE success\n"));
/*			if (child_dc_cb->state != ACTIVE_WRITE_FIN) {
		fprintf(stderr, \
			"wait for ACTIVE_WRITE_FIN state %d\n", \
			child_dc_cb->state);
		return;
	} */
	
	/* tell the peer transfer finished */
	dc_cb->send_buf.mode = kRdmaTrans_ActWrte;
	dc_cb->send_buf.stat = ACTIVE_WRITE_FIN;
	ret = ibv_post_send(dc_cb->qp, &dc_cb->sq_wr, &bad_wr);
	if (ret) {
		fprintf(stderr, "post send error %d\n", ret);
		return 0;
	}
	DPRINTF(("send_dat_blk: ibv_post_send finish 2\n"));
	
/*	if (tick && (bytes >= hashbytes)) {
		printf("\rBytes transferred: %ld", bytes);
		(void) fflush(stdout);
		while (bytes >= hashbytes)
			hashbytes += TICKBYTES;
	} */
	
	/* wait the client to notify next round data transfer */
	sem_wait(&dc_cb->sem);
	DPRINTF(("wait notify next round data transfer success\n"));
	
	return rhdr.dlen;
}

int
recv_dat_blk(BUFDATBLK *bufblk, struct rdma_cb *cb)
{
	int ret;
	struct ibv_send_wr *bad_wr;
	rmsgheader rhdr;
	
	/* wait for the client send ADV - READ? WRITE? */
	sem_wait(&cb->sem);
	
	/* tell the peer where to write */
	iperf_format_send(cb, bufblk->rdma_buf, bufblk->rdma_mr);
	cb->send_buf.mode = kRdmaTrans_ActWrte;
	cb->send_buf.stat = ACTIVE_WRITE_RESP;
	tsf_setup_wr(bufblk);
	
	ret = ibv_post_send(cb->qp, &cb->sq_wr, &bad_wr);
	if (ret) {
		syslog(LOG_ERR, "ibv_post_send: %m");
		return -1;
	}
	
	/* wait the finish of rdma write */
	sem_wait(&cb->sem);
	
	/* get package data len */
	memcpy(&rhdr, bufblk->rdma_buf, sizeof(rmsgheader));
	
	/* notify the client to go on */
	cb->send_buf.mode = kRdmaTrans_ActWrte;
	cb->send_buf.stat = ACTIVE_WRITE_FIN;
	ret = ibv_post_send(cb->qp, &cb->sq_wr, &bad_wr);
	if (ret) {
		syslog(LOG_ERR, "ibv_post_send: %m");
		return -1;
	}
	
	return rhdr.dlen;
}
