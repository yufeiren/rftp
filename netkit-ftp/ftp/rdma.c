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

#include <pthread.h>
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

#include <dirent.h>	/* for dir resolve */

#include "rdma.h"
#include "errors.h"
#include "utils.h"

/*
#include <sys/syscall.h>
pid_t gettid()
{
     return syscall(SYS_gettid);
} */

extern struct acptq acceptedTqh;

extern struct options opt;

extern pthread_mutex_t dir_mutex;

static struct rdma_cb *tmpcb;
/* static tmp; */

static int filesessionid;


static int do_recv(struct rdma_cb *cb, struct ibv_wc *wc)
{
	int ret;
	pthread_t tid;
	struct ibv_recv_wr *bad_wr;
	
	if (wc->byte_len != sizeof(cb->recv_buf)) {
		syslog(LOG_ERR, "Received bogus data, size %d\n", wc->byte_len);
		return -1;
	}
	
	/* find the recv buffer */
	RECVWR *recvwr = NULL;
	TAILQ_FOREACH(recvwr, &recvwr_tqh, entries)
		if (recvwr->wr_id == wc->wr_id)
			break;
	
	if (recvwr == NULL) {
		syslog(LOG_ERR, "can not find recv wr id, %ld", recvwr->wr_id);
		return -1;
	}
	
	struct rdma_info_blk *newbuf;
	newbuf = (struct rdma_info_blk *) malloc(sizeof(struct rdma_info_blk));
	memset(newbuf, '\0', sizeof(struct rdma_info_blk));
	memcpy(newbuf, &recvwr->recv_buf, sizeof(struct rdma_info_blk));
	
	if (recvwr->recv_buf.mode == kRdmaTrans_ActWrte) {
		switch (recvwr->recv_buf.stat) {
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
			/* take the block out */
			ret = pthread_create(&tid, NULL, recv_data, newbuf);
			if (ret != 0) {
				perror("pthread_create recv_data:");
				exit(EXIT_FAILURE);
			}
			break;
		case ACTIVE_WRITE_RQBLK:
			/* prep_blk(cb); */
			ret = pthread_create(&tid, NULL, prep_blk, cb);
			if (ret != 0) {
				perror("pthread_create sender:");
				exit(EXIT_FAILURE);
			}
			break;
		case ACTIVE_WRITE_RPBLK:
			/* acpt_blk(&recvwr->recv_buf); */
			ret = pthread_create(&tid, NULL, acpt_blk, &recvwr->recv_buf);
			if (ret != 0) {
				perror("pthread_create sender:");
				exit(EXIT_FAILURE);
			}
			break;
		case DC_QP_REQ:
			tmpcb = cb;
			ret = pthread_create(&tid, NULL, handle_qp_req, &recvwr->recv_buf);
			if (ret != 0) {
				perror("pthread_create handle_qp_req:");
				exit(EXIT_FAILURE);
			}
			break;
		case DC_QP_REP:
			ret = pthread_create(&tid, NULL, handle_qp_rep, &recvwr->recv_buf);
			if (ret != 0) {
				perror("pthread_create handle_qp_rep:");
				exit(EXIT_FAILURE);
			}
			break;
		case FILE_SESSION_ID_REQUEST:
			tmpcb = cb;
			ret = pthread_create(&tid, NULL, handle_file_session_req, &recvwr->recv_buf);
			if (ret != 0) {
				perror("pthread_create handle_file_session_req:");
				exit(EXIT_FAILURE);
			}
			break;
		case FILE_SESSION_ID_RESPONSE:
			tmpcb = cb;
			ret = pthread_create(&tid, NULL, handle_file_session_rep, &recvwr->recv_buf);
			if (ret != 0) {
				perror("pthread_create handle_file_session_rep:");
				exit(EXIT_FAILURE);
			}
			break;
		case DC_CONNECTION_REQ:
			memcpy(&opt.rcstreamnum, recvwr->recv_buf.addr, 4);
			syslog(LOG_ERR, "dc conn num is %d", opt.rcstreamnum);
			sem_post(&cb->sem);
			break;
		default:
			fprintf(stderr, "unrecognized stat %d\n", cb->recv_buf.stat);
			break;
		}
	}
	
	if (recvwr->recv_buf.stat != ACTIVE_WRITE_FIN) {
		free(newbuf);
	}
	
	/* the created thread should copy the recvbuf
	   otherwise, the recvbuf could be flushed */
	
	/* repost the RDMA_RECV work request */
	ret = ibv_post_recv(cb->qp, &recvwr->recv_wr, &bad_wr);
	if (ret) {
		syslog(LOG_ERR, "post recv error: %d\n", ret);
		return -1;
	}

/*	sem_post(&cb->sem);

	DEBUG_LOG("Received rkey %x addr %" PRIx64 " len %d from peer\n",
		  cb->remote_rkey, cb->remote_addr, cb->remote_len); */
	
	return 0;
}


void *
handle_file_session_req(void *arg)
{
	struct rdma_info_blk *recvbuf = (struct rdma_info_blk *) arg;
	struct rdma_info_blk tmpbuf;
	EVENTWR *evwr;
	struct ibv_send_wr *bad_wr;
	struct stat st;
	
	int ret;
	
	pthread_detach(pthread_self());
	
	memcpy(&tmpbuf, recvbuf, sizeof(struct rdma_info_blk));
	recvbuf = &tmpbuf;
	
	FILEINFO *item = (FILEINFO *) malloc(sizeof(FILEINFO));
	if (item == NULL) {
		syslog(LOG_ERR, "malloc fail");
		exit(EXIT_FAILURE);
	}
	
	char filename[128];
	memset(filename, '\0', 128);
	
	memcpy(filename, recvbuf->addr, 32);
	
	pthread_mutex_lock(&dir_mutex);

	parsedir(filename);
	item->sessionid = ++ filesessionid;

	pthread_mutex_unlock(&dir_mutex);
	
	/* compose file information */
	strcpy(item->lf, filename);
	
	/* /dev/null don't support O_DIRECT */
	if (   (opt.directio == true)
	    && (stat(item->lf, &st) < 0 || S_ISREG(st.st_mode)))
		item->fd = open(item->lf, O_WRONLY | O_CREAT | O_DIRECT, 0666);
	else
		item->fd = open(item->lf, O_WRONLY | O_CREAT, 0666);
	if (item->fd < 0) {
		syslog(LOG_ERR, "Open failed %s", item->lf);
		exit(EXIT_FAILURE);
	}
	
	item->seqnum = 1;
	
	memcpy(&item->fsize, recvbuf->addr + 32, 8);
	transtotallen += item->fsize;
	
	TAILQ_INIT(&item->pending_tqh);
/*	syslog(LOG_ERR, "file: %s, size: %ld, sid: %d\n", \
		filename, item->fsize, item->sessionid); */
	
	/* insert file info into finfo_tqh */
	TAILQ_LOCK(&finfo_tqh);
	TAILQ_INSERT_TAIL(&finfo_tqh, item, entries);
	TAILQ_UNLOCK(&finfo_tqh);
	
	/* compose response */
	TAILQ_LOCK(&free_evwr_tqh);

	while (TAILQ_EMPTY(&free_evwr_tqh)) {
		if (TAILQ_WAIT(&free_evwr_tqh) != 0)
			continue;	/* goto while */
	}

	evwr = TAILQ_FIRST(&free_evwr_tqh);
	TAILQ_REMOVE(&free_evwr_tqh, evwr, entries);
	
	TAILQ_UNLOCK(&free_evwr_tqh);
	
	evwr->ev_buf.mode = kRdmaTrans_ActWrte;
	evwr->ev_buf.stat = FILE_SESSION_ID_RESPONSE;
	
	strcpy(evwr->ev_buf.addr, filename);
	memcpy(evwr->ev_buf.addr + 32, &item->sessionid, 4);
	
	/* post send response */
	TAILQ_LOCK(&evwr_tqh);
	TAILQ_INSERT_TAIL(&evwr_tqh, evwr, entries);
	TAILQ_UNLOCK(&evwr_tqh);
	
	ret = ibv_post_send(tmpcb->qp, &evwr->ev_wr, &bad_wr);
	if (ret) {
		syslog(LOG_ERR, "ibv_post_send: %m");
		pthread_exit(NULL);
	}
	
	pthread_exit(NULL);
}


void *
handle_file_session_rep(void *arg)
{
	struct rdma_info_blk *recvbuf = (struct rdma_info_blk *) arg;
	struct rdma_info_blk tmpbuf;
	
	pthread_detach(pthread_self());
	
	memcpy(&tmpbuf, recvbuf, sizeof(struct rdma_info_blk));
	recvbuf = &tmpbuf;
	
	char filename[128];
	memset(filename, '\0', 128);
	
	memcpy(filename, recvbuf->addr, 32);
	
	int sessionid;
	memcpy(&sessionid, recvbuf->addr + 32, 4);
	
	/* find file id - move to scheduler list */
	FILEINFO *item;
	
	TAILQ_LOCK(&finfo_tqh);
	TAILQ_FOREACH(item, &finfo_tqh, entries)
		if (strcmp(item->lf, filename) == 0)
			break;
	
	if (item == NULL) {
		syslog(LOG_ERR, "cannot find file: %s", filename);
		TAILQ_UNLOCK(&finfo_tqh);
		pthread_exit(NULL);
	}

	TAILQ_REMOVE(&finfo_tqh, item, entries);
	item->sessionid = sessionid;
	
	TAILQ_UNLOCK(&finfo_tqh);

/*	syslog(LOG_ERR, "file: %s, size: %ld, sid: %d\n", \
		item->lf, item->fsize, item->sessionid); */
	
	TAILQ_LOCK(&schedule_tqh);
	TAILQ_INSERT_TAIL(&schedule_tqh, item, entries);
	TAILQ_UNLOCK(&schedule_tqh);
	TAILQ_SIGNAL(&schedule_tqh);
	
	sem_post(&tmpcb->sem); /* notify scheduler */
	
	pthread_exit(NULL);
}


void *handle_qp_req(void *arg)
{
	struct rdma_info_blk *recvbuf = (struct rdma_info_blk *) arg;
	struct rdma_info_blk tmpbuf;
	int i;
	int num, tmpnum;
	union ibv_gid remote_gid;
	
	memcpy(&tmpbuf, recvbuf, sizeof(struct rdma_info_blk));
	recvbuf = &tmpbuf;
	
	DATAQP *dcqp;
	
	char msg[128];
	struct ibv_qp_attr attr;
	
	pthread_detach(pthread_self());
	
	/* length */
	memcpy(&tmpnum, recvbuf->addr, 4);
	num = ntohl(tmpnum);
	
	/* remote gid */
	memset(msg, '\0', 128);
	memcpy(msg, recvbuf->addr + 4, 47);
	
	char a[3];
	for (i = 0; i < 16; i ++) {
		memset(a, '\0', 3);
		memcpy(a, msg + i * 3, 2);
		remote_gid.raw[i] = (unsigned char)strtoll(a, NULL, 16);
	}
	
	syslog(LOG_ERR, "get remote gid: %s", msg);
	
	create_dc_qp(tmpcb, num, 0);
	
	/* setup qp */
	i = 0;
	TAILQ_FOREACH(dcqp, &dcqp_tqh, entries) {
		memset(msg, '\0', 128);
		memcpy(msg, recvbuf->addr + i * 23 + 4 + 47, 23);
		syslog(LOG_ERR, "get remote qp: %s", msg);
		
		sscanf(msg, "%04x:%04x:%06x:%06x", \
			&dcqp->rem_lid, &dcqp->rem_out_reads, \
			&dcqp->rem_qpn, &dcqp->rem_psn);
		
		memset(&attr, '\0', sizeof(struct ibv_qp_attr));

		attr.qp_state = IBV_QPS_RTR;
		attr.path_mtu = IBV_MTU_1024; /* 2048; */
		/* port_attr.active_mtu;  user_parm->curr_mtu; */
		attr.dest_qp_num = dcqp->rem_qpn;
		attr.rq_psn = dcqp->rem_psn;
		attr.ah_attr.dlid = dcqp->rem_lid;
		attr.max_dest_rd_atomic = 1;
		attr.min_rnr_timer = 12;

/*		if (user_parm->gid_index < 0) { */
/* 		attr.ah_attr.is_global  = 0;
		attr.ah_attr.sl         = 0; sl */
/*		} else { */
		attr.ah_attr.is_global  = 1;
		attr.ah_attr.grh.dgid   = remote_gid; /* dest->gid; */
		attr.ah_attr.grh.sgid_index = 0; /* user_parm->gid_index; */
		attr.ah_attr.grh.hop_limit = 1;
		attr.ah_attr.sl         = 0;
/*		} */
		
		attr.ah_attr.src_path_bits = 0;
		attr.ah_attr.port_num = 1; /* user_parm->ib_port; */
	
		if (ibv_modify_qp(dcqp->qp, &attr,
				  IBV_QP_STATE              |
				  IBV_QP_AV                 |
				  IBV_QP_PATH_MTU           |
				  IBV_QP_DEST_QPN           |
				  IBV_QP_RQ_PSN             |
				  IBV_QP_MIN_RNR_TIMER      |
				  IBV_QP_MAX_DEST_RD_ATOMIC)) {
			fprintf(stderr, "Failed to modify RC QP to RTR: %d [%s]\n", errno, strerror(errno));
			syslog(LOG_ERR, "Failed to modify RC QP to RTR: %d [%s]", errno, strerror(errno));
			pthread_exit(NULL);
		}

		attr.timeout = 14; /* user_parm->qp_timeout; */
		attr.retry_cnt = 7;
		attr.rnr_retry = 7;
			
		attr.qp_state 	    = IBV_QPS_RTS;
		attr.sq_psn 	    = dcqp->loc_psn;
		attr.max_rd_atomic  = 1;
		
		attr.max_rd_atomic  = 1;
		if (ibv_modify_qp(dcqp->qp, &attr,
				  IBV_QP_STATE              |
				  IBV_QP_SQ_PSN             |
				  IBV_QP_TIMEOUT            |
				  IBV_QP_RETRY_CNT          |
				  IBV_QP_RNR_RETRY          |
				  IBV_QP_MAX_QP_RD_ATOMIC)) {
			fprintf(stderr, "Failed to modify RC QP to RTS: %d [%s]\n", errno, strerror(errno));
			syslog(LOG_ERR, "Failed to modify RC QP to RTS: %d [%s]", errno, strerror(errno));
			pthread_exit(NULL);
		}
		
		i++;
	}
	
	pthread_exit(NULL);
}


void *handle_qp_rep(void *arg)
{
	struct rdma_info_blk *recvbuf = (struct rdma_info_blk *) arg;
	struct rdma_info_blk tmpbuf;
	int i;
	int num, tmpnum;
	union ibv_gid remote_gid;
	
	memcpy(&tmpbuf, recvbuf, sizeof(struct rdma_info_blk));
	recvbuf = &tmpbuf;
	
	DATAQP *dcqp;
	
	char msg[128];
	struct ibv_qp_attr attr;
	
	pthread_detach(pthread_self());
	
	/* length */
	memcpy(&tmpnum, recvbuf->addr, 4);
	num = ntohl(tmpnum);
	
	/* remote gid */
	memset(msg, '\0', 128);
	memcpy(msg, recvbuf->addr + 4, 47);
	
	char a[3];
	for (i = 0; i < 16; i ++) {
		memset(a, '\0', 3);
		memcpy(a, msg + i * 3, 2);
		remote_gid.raw[i] = (unsigned char)strtoll(a, NULL, 16);
	}
	
	syslog(LOG_ERR, "get remote gid: %s", msg);
	
	
	/* setup qp */
	i = 0;
	TAILQ_FOREACH(dcqp, &dcqp_tqh, entries) {
		memset(msg, '\0', 128);
		memcpy(msg, recvbuf->addr + i * 23 + 4 + 47, 23);
		syslog(LOG_ERR, "get remote qp: %s", msg);
		
		sscanf(msg, "%04x:%04x:%06x:%06x", \
			&dcqp->rem_lid, &dcqp->rem_out_reads, \
			&dcqp->rem_qpn, &dcqp->rem_psn);
		
		memset(&attr, '\0', sizeof(struct ibv_qp_attr));

		attr.qp_state = IBV_QPS_RTR;
		attr.path_mtu = IBV_MTU_1024; /* 2048; */
		/* port_attr.active_mtu;  user_parm->curr_mtu; */
		attr.dest_qp_num = dcqp->rem_qpn;
		attr.rq_psn = dcqp->rem_psn;
		attr.ah_attr.dlid = dcqp->rem_lid;
		attr.max_dest_rd_atomic = 1;
		attr.min_rnr_timer = 12;

/*		if (user_parm->gid_index < 0) { */
/* 		attr.ah_attr.is_global  = 0;
		attr.ah_attr.sl         = 0; sl */
/*		} else { */
		attr.ah_attr.is_global  = 1;
		attr.ah_attr.grh.dgid   = remote_gid; /* dest->gid; */
		attr.ah_attr.grh.sgid_index = 0; /* user_parm->gid_index; */
		attr.ah_attr.grh.hop_limit = 1;
		attr.ah_attr.sl         = 0;
/*		} */
		
		attr.ah_attr.src_path_bits = 0;
		attr.ah_attr.port_num = 1; /* user_parm->ib_port; */
	
		if (ibv_modify_qp(dcqp->qp, &attr,
				  IBV_QP_STATE              |
				  IBV_QP_AV                 |
				  IBV_QP_PATH_MTU           |
				  IBV_QP_DEST_QPN           |
				  IBV_QP_RQ_PSN             |
				  IBV_QP_MIN_RNR_TIMER      |
				  IBV_QP_MAX_DEST_RD_ATOMIC)) {
			fprintf(stderr, "Failed to modify RC QP to RTR: %d [%s]\n", errno, strerror(errno));
			syslog(LOG_ERR, "Failed to modify RC QP to RTR: %d [%s]", errno, strerror(errno));
			pthread_exit(NULL);
		}

		attr.timeout = 14; /* user_parm->qp_timeout; */
		attr.retry_cnt = 7;
		attr.rnr_retry = 7;
			
		attr.qp_state 	    = IBV_QPS_RTS;
		attr.sq_psn 	    = dcqp->loc_psn;
		attr.max_rd_atomic  = 1;
		
		attr.max_rd_atomic  = 1;
		if (ibv_modify_qp(dcqp->qp, &attr,
				  IBV_QP_STATE              |
				  IBV_QP_SQ_PSN             |
				  IBV_QP_TIMEOUT            |
				  IBV_QP_RETRY_CNT          |
				  IBV_QP_RNR_RETRY          |
				  IBV_QP_MAX_QP_RD_ATOMIC)) {
			fprintf(stderr, "Failed to modify RC QP to RTS: %d [%s]\n", errno, strerror(errno));
			syslog(LOG_ERR, "Failed to modify RC QP to RTS: %d [%s]", errno, strerror(errno));
			pthread_exit(NULL);
		}
		
		i++;
	}
	
	pthread_exit(NULL);
}


void *
recv_data(void *arg)
{
	BUFDATBLK *bufblk;
	rmsgheader rhdr;
	struct rdma_info_blk *recvbuf  = (struct rdma_info_blk *) arg;
	struct rdma_info_blk tmpbuf;

	memcpy(&tmpbuf, recvbuf, sizeof(struct rdma_info_blk));
	free(arg);
	
	recvbuf = &tmpbuf;
	pthread_detach(pthread_self());
	FILEINFO *finfo;
	
	bufblk = NULL;
	long pgsz;
	
	/* event finish */
	TAILQ_LOCK(&waiting_tqh);
	
	TAILQ_FOREACH(bufblk, &waiting_tqh, entries)
		if ((uint64_t) (unsigned long)bufblk->rdma_buf == ntohll(recvbuf->buf))
			break;

	if (bufblk != NULL)
		TAILQ_REMOVE(&waiting_tqh, bufblk, entries);
	else {
		syslog(LOG_ERR, "could not find comp buf %ld\n", ntohll(recvbuf->buf));
		TAILQ_UNLOCK(&waiting_tqh);
		pthread_exit(NULL);
	}
	
	TAILQ_UNLOCK(&waiting_tqh);
	
	/* parse header */
	memcpy(&rhdr, bufblk->rdma_buf, sizeof(rmsgheader));
	
	/* find file: according session id */
	TAILQ_FOREACH(finfo, &finfo_tqh, entries)
		if (rhdr.sessionid == finfo->sessionid)
			break;
	
	if (finfo == NULL) {
		syslog(LOG_ERR, "could not find file session %d", \
			rhdr.sessionid);
		pthread_exit(NULL);
	}
	
	bufblk->fd = finfo->fd;
	bufblk->seqnum = rhdr.seqnum;
	bufblk->offset = rhdr.offset;
	bufblk->buflen = rhdr.dlen + sizeof(rhdr);
	
	/* non-odirect */
	pgsz = getpagesize();
	
	if (  (opt.directio == true)
	   && (rhdr.dlen % pgsz != 0) ) {
		/* open - lseek - read - close */
		bufblk->fd = open(finfo->lf, O_WRONLY | O_CREAT, 0666);
		if (bufblk->fd < 0) {
			syslog(LOG_ERR, "Open failed %s", finfo->lf);
			exit(EXIT_FAILURE);
		}
		
		lseek(bufblk->fd, bufblk->offset, SEEK_CUR);
	}
	
	BUFDATBLK *tmpblk;
	
	if (finfo->seqnum == rhdr.seqnum) {
		finfo->seqnum ++;
		/* insert into writer list */
		TAILQ_LOCK(&writer_tqh);
		TAILQ_INSERT_TAIL(&writer_tqh, bufblk, entries);
		TAILQ_UNLOCK(&writer_tqh);
		TAILQ_SIGNAL(&writer_tqh);	
		
TAILQ_LOCK(&finfo->pending_tqh);
		if (!TAILQ_EMPTY(&finfo->pending_tqh)) {
			for (tmpblk = TAILQ_FIRST(&finfo->pending_tqh);
			     (tmpblk != NULL) && (tmpblk->seqnum == finfo->seqnum);
			     finfo->seqnum ++)
			{
				bufblk = TAILQ_NEXT(tmpblk, entries);
				TAILQ_REMOVE(&finfo->pending_tqh, tmpblk, entries);
				
				TAILQ_LOCK(&writer_tqh);
				TAILQ_INSERT_TAIL(&writer_tqh, tmpblk, entries);
				TAILQ_UNLOCK(&writer_tqh);
				TAILQ_SIGNAL(&writer_tqh);	
				
				tmpblk = bufblk;
			}
			bufblk = TAILQ_FIRST(&finfo->pending_tqh);
		}
TAILQ_UNLOCK(&finfo->pending_tqh);
		
	} else { /* insert into pending list */
		TAILQ_LOCK(&finfo->pending_tqh);
		TAILQ_FOREACH(tmpblk, &finfo->pending_tqh, entries)
			if (tmpblk->seqnum > bufblk->seqnum) {
				TAILQ_INSERT_BEFORE(tmpblk, bufblk, entries);
				break;
			}
		
		if (tmpblk == NULL) {
			TAILQ_INSERT_TAIL(&finfo->pending_tqh, bufblk, entries);
		}
		TAILQ_UNLOCK(&finfo->pending_tqh);
	}
	
	pthread_exit(NULL);	
}

void *prep_blk(void *arg)
{
/* addr: num(4 bytes) + (buf 8 + rkey 4 + size 4) */

	int i;
	BUFDATBLK *bufblk;
	EVENTWR *evwr;
	struct ibv_send_wr *bad_wr;
	int ret;
	struct rdma_cb *cb = (struct rdma_cb *) arg;
	
	char *offset;
	int j = 0;
	int num;

	pthread_detach(pthread_self());
	
	/* get addr info */
	TAILQ_LOCK(&free_evwr_tqh);

	while (TAILQ_EMPTY(&free_evwr_tqh)) {
		if (TAILQ_WAIT(&free_evwr_tqh) != 0)
			continue;	/* goto while */
	}

	evwr = TAILQ_FIRST(&free_evwr_tqh);
	TAILQ_REMOVE(&free_evwr_tqh, evwr, entries);
	
	TAILQ_UNLOCK(&free_evwr_tqh);
	
	for (i = 0; i < 10; i ++) {
		/* get from free list */
		TAILQ_LOCK(&free_tqh);
		if (i == 0) { /* at least one block response */
			while (TAILQ_EMPTY(&free_tqh)) {
				if (TAILQ_WAIT(&free_tqh) != 0)
					continue;	/* goto while */
			}
			bufblk = TAILQ_FIRST(&free_tqh);
			TAILQ_REMOVE(&free_tqh, bufblk, entries);
		} else {
			if (TAILQ_EMPTY(&free_tqh)) {
				TAILQ_UNLOCK(&free_tqh);
				break;
			}
			
			bufblk = TAILQ_FIRST(&free_tqh);
			TAILQ_REMOVE(&free_tqh, bufblk, entries);
		}
		
		TAILQ_UNLOCK(&free_tqh);
				
		/* insert bulk into waiting list */
		TAILQ_LOCK(&waiting_tqh);
		TAILQ_INSERT_TAIL(&waiting_tqh, bufblk, entries);
		TAILQ_UNLOCK(&waiting_tqh);
		
		evwr->ev_buf.buf = htonll((uint64_t) (unsigned long)bufblk->rdma_buf);
		evwr->ev_buf.rkey = htonl(bufblk->rdma_mr->rkey);
		evwr->ev_buf.size = htonl(cb->size + sizeof(rmsgheader));
		
		offset = evwr->ev_buf.addr + i * 16 + 4;
		memcpy(offset, &(evwr->ev_buf.buf), 8);
		memcpy(offset + 8, &(evwr->ev_buf.rkey), 4);
		memcpy(offset + 12, &(evwr->ev_buf.size), 4);
		
		++ j;
		
		/* put wr into evwr_tqh list */
	}
	
	/* num */
	num = htonl(j);
	memcpy(evwr->ev_buf.addr, &num, 4);
	
	evwr->ev_buf.mode = kRdmaTrans_ActWrte;
	evwr->ev_buf.stat = ACTIVE_WRITE_RPBLK;
	
	/* post send response */
	TAILQ_LOCK(&evwr_tqh);
	TAILQ_INSERT_TAIL(&evwr_tqh, evwr, entries);
	TAILQ_UNLOCK(&evwr_tqh);
	
	ret = ibv_post_send(cb->qp, &evwr->ev_wr, &bad_wr);
	if (ret) {
		syslog(LOG_ERR, "ibv_post_send: %m");
		pthread_exit(NULL);
	}	
	
	pthread_exit(NULL);	
}


void *acpt_blk(void *arg)
{
	REMOTEADDR *rmtaddr;
	pthread_detach(pthread_self());
	struct rdma_info_blk tmpbuf;
	struct rdma_info_blk *recvbuf = (struct rdma_info_blk *) arg;
	int i;
	int num, tmpnum;
	
	memcpy(&tmpbuf, recvbuf, sizeof(struct rdma_info_blk));
	recvbuf = &tmpbuf;
	
	memcpy(&tmpnum, recvbuf->addr, 4);
	num = ntohl(tmpnum);
	
	pthread_detach(pthread_self());
	
	for (i = 0 ; i < num; i ++) {
		/* get a free remote address info */
		TAILQ_LOCK(&free_rmtaddr_tqh);
		
		while (TAILQ_EMPTY(&free_rmtaddr_tqh))
			if (TAILQ_WAIT(&free_rmtaddr_tqh) != 0)
				continue;	/* goto while */
		
		rmtaddr = TAILQ_FIRST(&free_rmtaddr_tqh);
		TAILQ_REMOVE(&free_rmtaddr_tqh, rmtaddr, entries);
		
		TAILQ_UNLOCK(&free_rmtaddr_tqh);
		
		/* set value */
		
		memcpy(&recvbuf->buf, recvbuf->addr + i * 16 + 4, 8);
		memcpy(&recvbuf->rkey, recvbuf->addr + i * 16 + 12, 4);
		memcpy(&recvbuf->size, recvbuf->addr + i * 16 + 16, 4);
		
		rmtaddr->buf = ntohll(recvbuf->buf);
		rmtaddr->rkey = ntohl(recvbuf->rkey);
		rmtaddr->size = ntohl(recvbuf->size);
		
		/* insert into addr ready list */
		TAILQ_LOCK(&rmtaddr_tqh);
		TAILQ_INSERT_TAIL(&rmtaddr_tqh, rmtaddr, entries);
		TAILQ_UNLOCK(&rmtaddr_tqh);
		TAILQ_SIGNAL(&rmtaddr_tqh);
	}
	
	pthread_exit(NULL);
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
		syslog(LOG_ERR, "accept a new connection: %p", cma_id);
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
		syslog(LOG_ERR, "RDMA %s DISCONNECT EVENT...\n",
			cb->server ? "server" : "client");
		sem_post(&cb->sem);
		break;

	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		fprintf(stderr, "cma detected device removal!!!!\n");
		ret = -1;
		break;

	default:
		syslog(LOG_ERR, "unhandled event: %s, ignoring\n",
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
	int ret;
	int compevnum = 0;
	char *errmsg;
	
	while ((ret = ibv_poll_cq(cb->cq, 1, &wc)) == 1) {
		ret = 0;
		compevnum ++;

/*
enum ibv_wc_status {
        IBV_WC_SUCCESS,
        IBV_WC_LOC_LEN_ERR,
        IBV_WC_LOC_QP_OP_ERR,
        IBV_WC_LOC_EEC_OP_ERR,
        IBV_WC_LOC_PROT_ERR,
        IBV_WC_WR_FLUSH_ERR,
        IBV_WC_MW_BIND_ERR,
        IBV_WC_BAD_RESP_ERR,
        IBV_WC_LOC_ACCESS_ERR,
        IBV_WC_REM_INV_REQ_ERR,
        IBV_WC_REM_ACCESS_ERR,
        IBV_WC_REM_OP_ERR,
        IBV_WC_RETRY_EXC_ERR,
        IBV_WC_RNR_RETRY_EXC_ERR,
        IBV_WC_LOC_RDD_VIOL_ERR,
        IBV_WC_REM_INV_RD_REQ_ERR,
        IBV_WC_REM_ABORT_ERR,
        IBV_WC_INV_EECN_ERR,
        IBV_WC_INV_EEC_STATE_ERR,
        IBV_WC_FATAL_ERR,
        IBV_WC_RESP_TIMEOUT_ERR,
        IBV_WC_GENERAL_ERR
};

*/
		if (wc.status) {
			if (wc.wr_id & WRIDEVENT)
				errmsg = "SEND buffer";
			else if (wc.wr_id & WRIDRECV)
				errmsg = "RECV buffer";
			else if (wc.wr_id & WRIDBUFFER)
				errmsg = "RDMA buffer";
			else
				errmsg = "uknow buffer type";
                        syslog(LOG_ERR, "cq completion failed status %d(%s), opcode: %d, wr type: %s", \
                                wc.status, ibv_wc_status_str(wc.status), wc.opcode, errmsg);

			// IBV_WC_WR_FLUSH_ERR == 5
			if (wc.status != IBV_WC_WR_FLUSH_ERR)
				ret = -1;
			goto error;
		}

		switch (wc.opcode) {
		case IBV_WC_SEND:
			/* cb->sq_wr.wr_id = wc.wr_id;  not safe */
/* 			ret = pthread_create(&tid, NULL, handle_wr, cb);
			if (ret != 0) {
				syslog(LOG_ERR, "pthread_create handle_wr:");
				exit(EXIT_FAILURE);
			} */
			handle_wr(cb, wc.wr_id);
			break;

		case IBV_WC_RDMA_WRITE:
			DEBUG_LOG("rdma write completion\n");
			cb->state = RDMA_WRITE_COMPLETE;
			/* cb->sq_wr.wr_id = wc.wr_id; not safe */
			/* ret = pthread_create(&tid, NULL, handle_wr, cb);
			if (ret != 0) {
				syslog(LOG_ERR, "pthread_create handle_wr:");
				exit(EXIT_FAILURE);
			} */
			handle_wr(cb, wc.wr_id);
			break;

		case IBV_WC_RDMA_READ:
			DEBUG_LOG("rdma read completion\n");
			cb->state = RDMA_READ_COMPLETE;
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
	return compevnum;
/*	return 0; */

error:
	cb->state = STATE_ERROR;
	sem_post(&cb->sem);
	return compevnum;
}

void
handle_wr(struct rdma_cb *cb, uint64_t wr_id)
{
	int ret;
	BUFDATBLK *item;
	EVENTWR *evwr;
	
	pthread_t tid;
	
	if ((wr_id & WRIDEVENT) > 0) {
		/* event finish */
		TAILQ_LOCK(&evwr_tqh);
		
		TAILQ_FOREACH(evwr, &evwr_tqh, entries)
			if (evwr->wr_id == wr_id)
				break;
		
		if (evwr != NULL)
			TAILQ_REMOVE(&evwr_tqh, evwr, entries);
		else {
			syslog(LOG_ERR, "can not find event %ld\n", wr_id);
			TAILQ_UNLOCK(&evwr_tqh);
			return;
		}
		
		TAILQ_UNLOCK(&evwr_tqh);
		
		/* insert into free_evwr_tqh */
		TAILQ_LOCK(&free_evwr_tqh);
		TAILQ_INSERT_TAIL(&free_evwr_tqh, evwr, entries);
		TAILQ_UNLOCK(&free_evwr_tqh);
		TAILQ_SIGNAL(&free_evwr_tqh);
		
	} else if ((wr_id & WRIDBUFFER) > 0) {
		/* rdma rw finish */
		/* move out of waiting list */
		TAILQ_LOCK(&waiting_tqh);
		
		TAILQ_FOREACH(item, &waiting_tqh, entries) {
			if (item->wr_id == wr_id) {
				break;
			}
		}
		if (item != NULL)
			TAILQ_REMOVE(&waiting_tqh, item, entries);
		else {
			syslog(LOG_ERR, "can not find buf %ld\n", wr_id);
			TAILQ_UNLOCK(&waiting_tqh);
			return;
		}
		
		TAILQ_UNLOCK(&waiting_tqh);
		
		item->qp = cb->qp;
		
		ret = pthread_create(&tid, NULL, notify_blk, item);
		if (ret != 0) {
			syslog(LOG_ERR, "pthread_create notify_blk fail");
			exit(EXIT_FAILURE);
		}
	}
	
	return;
}

void *notify_blk(void *arg)
{
	EVENTWR *evwr;
	struct ibv_send_wr *bad_wr;
	int ret;
	BUFDATBLK *item = (BUFDATBLK *) arg;
		
	pthread_detach(pthread_self());

	/* notify peer side the WHICH rdma_write finish */
	TAILQ_LOCK(&free_evwr_tqh);
	while (TAILQ_EMPTY(&free_evwr_tqh))
		if (TAILQ_WAIT(&free_evwr_tqh) != 0)
			continue;	/* goto while */
	
	evwr = TAILQ_FIRST(&free_evwr_tqh);
	TAILQ_REMOVE(&free_evwr_tqh, evwr, entries);
	
	TAILQ_UNLOCK(&free_evwr_tqh);
	
	evwr->ev_buf.buf = htonll(item->rdma_sq_wr.wr.rdma.remote_addr);
	evwr->ev_buf.mode = kRdmaTrans_ActWrte;
	evwr->ev_buf.stat = ACTIVE_WRITE_FIN;

	TAILQ_LOCK(&evwr_tqh);
	TAILQ_INSERT_TAIL(&evwr_tqh, evwr, entries);
	TAILQ_UNLOCK(&evwr_tqh);
	
	ret = ibv_post_send(item->qp, &evwr->ev_wr, &bad_wr);
	if (ret) {
		syslog(LOG_ERR, "post send error %d\n", ret);
		TAILQ_LOCK(&free_evwr_tqh);
		TAILQ_INSERT_TAIL(&free_evwr_tqh, evwr, entries);
		TAILQ_UNLOCK(&free_evwr_tqh);
		TAILQ_SIGNAL(&free_evwr_tqh);
		pthread_exit(NULL);
	}
	
	TAILQ_LOCK(&free_tqh);
	TAILQ_INSERT_TAIL(&free_tqh, item, entries);
	TAILQ_UNLOCK(&free_tqh);
	TAILQ_SIGNAL(&free_tqh);
	
	pthread_exit(NULL);
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
		ibv_ack_cq_events(cb->cq, ret);
/*		if (ret) {
			syslog(LOG_ERR, "!! iperf_cq_event_handler %m");
			pthread_exit(NULL);
		}
*/	}
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
	cb->server = 0;
	return 0;
}


// setup queue pair
int iperf_setup_qp(struct rdma_cb *cb, struct rdma_cm_id *cm_id)
{
	int ret;

	cb->pd = ibv_alloc_pd(cm_id->verbs);
	if (!cb->pd) {
		fprintf(stderr, "ibv_alloc_pd failed\n");
		syslog(LOG_ERR, "ibv_alloc_pd failed: %m");
		return errno;
	}
	DEBUG_LOG("created pd %p\n", cb->pd);

	cb->channel = ibv_create_comp_channel(cm_id->verbs);
	if (!cb->channel) {
		fprintf(stderr, "ibv_create_comp_channel failed\n");
		ret = errno;
		syslog(LOG_ERR, "ibv_create_comp_channel failed: %m");
		goto err1;
	}
	DEBUG_LOG("created channel %p\n", cb->channel);

	cb->cq = ibv_create_cq(cm_id->verbs, opt.rdma_cq_depth, cb,
				cb->channel, 0);
	if (!cb->cq) {
		syslog(LOG_ERR, "ibv_create_cq failed: %m");
		ret = errno;
		goto err2;
	}
	DEBUG_LOG("created cq %p\n", cb->cq);
	
	DEBUG_LOG("before ibv_req_notify_cq\n");
	ret = ibv_req_notify_cq(cb->cq, 0);
	if (ret) {
		fprintf(stderr, "ibv_req_notify_cq failed\n");
		syslog(LOG_ERR, "ibv_req_notify_cq failed: %m");
		ret = errno;
		goto err3;
	}

	DPRINTF(("before iperf_create_qp\n"));
	ret = iperf_create_qp(cb);
	if (ret) {
		perror("rdma_create_qp");
		syslog(LOG_ERR, "iperf_create_qp failed: %m");
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
	init_attr.cap.max_send_wr = opt.rdma_qp_sq_depth;
	init_attr.cap.max_recv_wr = opt.rdma_qp_rq_depth;
	init_attr.cap.max_recv_sge = 4;
	init_attr.cap.max_send_sge = 4;
	init_attr.qp_type = IBV_QPT_RC;
	init_attr.send_cq = cb->cq;
	init_attr.recv_cq = cb->cq;

	DPRINTF(("before rdma_create_qp\n"));
	DPRINTF(("cb->server: %d\n", cb->server));
	if (cb->server) {
		ret = rdma_create_qp(cb->child_cm_id, cb->pd, &init_attr);
		if (!ret)
			cb->qp = cb->child_cm_id->qp;
		else
			syslog(LOG_ERR, "rdma_create_qp fail: %d", ret);
	} else {
		ret = rdma_create_qp(cb->cm_id, cb->pd, &init_attr);
		if (!ret)
			cb->qp = cb->cm_id->qp;
		else
			syslog(LOG_ERR, "rdma_create_qp fail: %d", ret);
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
	int i;
	REMOTEADDR *rmtaddritem;
	EVENTWR *evwritem;
	RECVWR *recvitem;

	struct ibv_recv_wr *bad_recv_wr;
	int ret;
	
	for (i = 0; i < opt.evbufnum; i++) {
		if ( (evwritem = (EVENTWR *) malloc(sizeof(EVENTWR))) == NULL) {
			syslog(LOG_ERR, "iperf_setup_buffers: malloc");
			exit(EXIT_FAILURE);
		}
		
		memset(evwritem, '\0', sizeof(EVENTWR));
		
		evwritem->wr_id = (uint64_t) (i + 1) | WRIDEVENT;
		
		evwritem->ev_mr = ibv_reg_mr(cb->pd, &evwritem->ev_buf, sizeof(struct rdma_info_blk), 0);
		if (!evwritem->ev_mr) {
			syslog(LOG_ERR, "evwritem->ev_mr ibv_reg_mr send_mr");
			exit(EXIT_FAILURE);
		}
		
		evwritem->ev_sgl.addr = (uint64_t) (unsigned long) &evwritem->ev_buf;
		evwritem->ev_sgl.length = sizeof evwritem->ev_buf;
		evwritem->ev_sgl.lkey = evwritem->ev_mr->lkey;

		evwritem->ev_wr.opcode = IBV_WR_SEND;
		evwritem->ev_wr.send_flags = IBV_SEND_SIGNALED;
		evwritem->ev_wr.sg_list = &evwritem->ev_sgl;
		evwritem->ev_wr.num_sge = 1;
		
		evwritem->ev_wr.wr_id = evwritem->wr_id;
		TAILQ_LOCK(&free_evwr_tqh);
		TAILQ_INSERT_TAIL(&free_evwr_tqh, evwritem, entries);
		TAILQ_UNLOCK(&free_evwr_tqh);
	}
	
	for (i = 0; i < opt.rmtaddrnum; i++) {
		if ( (rmtaddritem = (REMOTEADDR *) malloc(sizeof(REMOTEADDR))) == NULL) {
			syslog(LOG_ERR, "tsf_setup_buf_list: malloc");
			exit(EXIT_FAILURE);
		}
		
		memset(rmtaddritem, '\0', sizeof(REMOTEADDR));
		
		TAILQ_LOCK(&free_rmtaddr_tqh);
		TAILQ_INSERT_TAIL(&free_rmtaddr_tqh, rmtaddritem, entries);
		TAILQ_UNLOCK(&free_rmtaddr_tqh);
	}
	
	/* untagged recv buf list */
	for (i = 0; i < opt.recvbufnum; i++) {
		if ( (recvitem = (RECVWR *) malloc(sizeof(RECVWR))) == NULL) {
			syslog(LOG_ERR, "tsf_setup_buf_list: malloc");
			exit(EXIT_FAILURE);
		}
		
		memset(recvitem, '\0', sizeof(RECVWR));
		
		recvitem->wr_id = (uint64_t) (i + 1) | WRIDRECV;
		
		recvitem->recv_mr = ibv_reg_mr(cb->pd, &recvitem->recv_buf, sizeof(struct rdma_info_blk), IBV_ACCESS_LOCAL_WRITE);
		if (!recvitem->recv_mr) {
			syslog(LOG_ERR, "recvitem->recv_mr ibv_reg_mr recv_mr");
			exit(EXIT_FAILURE);
		}
		
		TAILQ_INSERT_TAIL(&recvwr_tqh, recvitem, entries);
		
		recvitem->recv_sgl.addr = (uint64_t) (unsigned long) &recvitem->recv_buf;
		recvitem->recv_sgl.length = sizeof recvitem->recv_buf;
		recvitem->recv_sgl.lkey = recvitem->recv_mr->lkey;
		recvitem->recv_wr.sg_list = &recvitem->recv_sgl;
		recvitem->recv_wr.num_sge = 1;
		
		recvitem->recv_wr.wr_id = recvitem->wr_id;
		
		ret = ibv_post_recv(cb->qp, &recvitem->recv_wr, &bad_recv_wr);
		if (ret) {
			syslog(LOG_ERR, "ibv_post_recv fail: %m");
			exit(EXIT_FAILURE);
		}
	}
	
	return 0;
}


int tsf_setup_buf_list(struct rdma_cb *cb)
{
/* init rdma_mr and insert into the free list */
	int i;
	BUFDATBLK *item;
	REMOTEADDR *rmtaddritem;
	EVENTWR *evwritem;
	
	RECVWR *recvitem;
	struct ibv_recv_wr *bad_recv_wr;
	int ret;
	
	for (i = 0; i < opt.cbufnum; i++) {
		if ( (item = (BUFDATBLK *) malloc(sizeof(BUFDATBLK))) == NULL) {
			syslog(LOG_ERR, "tsf_setup_buf_list: malloc fail");
			exit(EXIT_FAILURE);
		}
		
		memset(item, '\0', sizeof(BUFDATBLK));
		
		item->wr_id = (uint64_t) (i + 1) | WRIDBUFFER;
		
		if (opt.directio != true) {
			if ( (item->rdma_buf = (char *) malloc(cb->size + sizeof(rmsgheader))) == NULL) {
				syslog(LOG_ERR, "tsf_setup_buf_list: malloc 2");
				exit(EXIT_FAILURE);
			}
		} else {
			if ( posix_memalign(&item->rdma_buf, getpagesize(), cb->size + sizeof(rmsgheader)) != 0 ) {
			        syslog(LOG_ERR, "tsf_setup_buf_list: memalign");
			        exit(EXIT_FAILURE);
			}
		}
		
		memset(item->rdma_buf, '\0', cb->size + sizeof(rmsgheader));
		
		item->rdma_mr = ibv_reg_mr(cb->pd, item->rdma_buf, \
				cb->size + sizeof(rmsgheader), \
				IBV_ACCESS_LOCAL_WRITE
				| IBV_ACCESS_REMOTE_READ
				| IBV_ACCESS_REMOTE_WRITE);
		if (!item->rdma_mr) {
			syslog(LOG_ERR, "tsf_setup_buf_list: ibv_reg_mr");
			exit(EXIT_FAILURE);
		}
		
		item->buflen = cb->size + sizeof(rmsgheader);
		
		TAILQ_INSERT_TAIL(&free_tqh, item, entries);
	}
	

	return 0;
}

void
tsf_waiting_to_free(void)
{
	/* because of pre-post block in waiting list, 
	   the app should recycle the block from waiting list to free list */
	BUFDATBLK *item;
	
	while (!TAILQ_EMPTY(&waiting_tqh)) {
		TAILQ_LOCK(&waiting_tqh);
		item = TAILQ_FIRST(&waiting_tqh);
		TAILQ_REMOVE(&waiting_tqh, item, entries);
		TAILQ_UNLOCK(&waiting_tqh);
		
		TAILQ_LOCK(&free_tqh);
		TAILQ_INSERT_TAIL(&free_tqh, item, entries);
		TAILQ_UNLOCK(&free_tqh);
	}

	return;
}

void
tsf_free_buf_list(void)
{
	BUFDATBLK *item;
	
	/* free free list - not thread safe */
	int i;
	for (i = 0; i < opt.cbufnum; i ++) {
		TAILQ_LOCK(&free_tqh);
		while ( TAILQ_EMPTY(&free_tqh) )
			if ( TAILQ_WAIT(&free_tqh) != 0)
				syslog(LOG_ERR, "TAILQ_WAIT free_tqh");
		
		item = TAILQ_FIRST(&free_tqh);
		TAILQ_REMOVE(&free_tqh, item, entries);
		
		TAILQ_UNLOCK(&free_tqh);
		
		ibv_dereg_mr(item->rdma_mr);
		
		free(item->rdma_buf);
	}
	
	return;
}

void iperf_free_buffers(struct rdma_cb *cb)
{
	return;
}


void iperf_setup_wr(struct rdma_cb *cb)
{
	return;
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

ssize_t
sendfilen(int out_fd, int in_fd, off_t offset, size_t count)
{
	ssize_t bytes_sent;
	size_t total_bytes_sent = 0;
	
	while (total_bytes_sent < count) {
		if ((bytes_sent = sendfile(out_fd, in_fd, &offset,
		count - total_bytes_sent)) <= 0) {
			if (errno == EINTR || errno == EAGAIN) {
				// Interrupted system call/try again
				// Just skip to the top of the loop and try again
				continue;
			}
			perror("sendfile");
			return -1;
		}
		total_bytes_sent += bytes_sent;
	}
	
	return total_bytes_sent;
}


ssize_t
fs_splice(int out_fd, int in_fd, off_t offset, size_t count)
{
	int pipefd[2];
	
	ssize_t bytes, bytes_sent, bytes_in_pipe;
	size_t total_bytes_sent = 0;

	if ( pipe(pipefd) < 0 ) {
		syslog(LOG_ERR, "pipe fail");
		return -1;
	}
	
	size_t splice_block_size = 16384;	/* 16K */
	
	// Splice the data from in_fd into the pipe
	while (total_bytes_sent < count) {
		if ((bytes_sent = splice(in_fd, NULL, pipefd[1], NULL,
			splice_block_size,
			SPLICE_F_MOVE)) <= 0) {
			if (errno == EINTR || errno == EAGAIN) {
				// Interrupted system call/try again
				// Just skip to the top of the loop and try again
				continue;
			}
			syslog(LOG_ERR, "splice file2pipe fail: %d[%s]", \
				errno, strerror(errno));
			close(pipefd[0]);
			close(pipefd[1]);
			return -1;
		}
		
		// Splice the data from the pipe into out_fd
		bytes_in_pipe = bytes_sent;
		while (bytes_in_pipe > 0) {
			if ((bytes = splice(pipefd[0], NULL, out_fd, NULL, bytes_in_pipe,
				SPLICE_F_MORE | SPLICE_F_MOVE)) <= 0) {
				if (errno == EINTR || errno == EAGAIN) {
					// Interrupted system call/try again
					// Just skip to the top of the loop and try again
					continue;
				}
				syslog(LOG_ERR, \
					"splice pipe2sock fail: %d[%s]", \
					errno, strerror(errno));
				close(pipefd[0]);
				close(pipefd[1]);
				return -1;
			}
			bytes_in_pipe -= bytes;
		}
	
		total_bytes_sent += bytes_sent;
	}
	
	close(pipefd[0]);
	close(pipefd[1]);
	return total_bytes_sent;
}


ssize_t
sf_splice(int out_fd, int in_fd, off_t offset, size_t count)
{
	int pipefd[2];
	
	ssize_t bytes, bytes_recv, bytes_in_pipe;
	size_t total_bytes_recv = 0;

	if ( pipe(pipefd) < 0 ) {
		syslog(LOG_ERR, "pipe fail");
		return -1;
	}
	
	size_t splice_block_size = 16384;	/* 16KB */
	
	// Splice the data from in_fd into the pipe
	while ((count == 0) || (total_bytes_recv < count)) {
		if ((bytes_recv = splice(in_fd, NULL, pipefd[1], NULL,
			splice_block_size,
			SPLICE_F_MOVE)) < 0) {
			if (errno == EINTR || errno == EAGAIN) {
				// Interrupted system call/try again
				// Just skip to the top of the loop and try again
				continue;
			}
			syslog(LOG_ERR, "splice sock2pipe fail: %d[%s]", \
				errno, strerror(errno));
			close(pipefd[0]);
			close(pipefd[1]);
			return -1;
		} else if (bytes_recv == 0)
			break;
		
		// Splice the data from the pipe into out_fd
		bytes_in_pipe = bytes_recv;
		while (bytes_in_pipe > 0) {
			if ((bytes = splice(pipefd[0], NULL, out_fd, NULL, bytes_in_pipe,
				SPLICE_F_MOVE)) <= 0) {
				if (errno == EINTR || errno == EAGAIN) {
					// Interrupted system call/try again
					// Just skip to the top of the loop and try again
					continue;
				}
				syslog(LOG_ERR, \
					"splice pipe2file fail: %d[%s]", \
					errno, strerror(errno));
				close(pipefd[0]);
				close(pipefd[1]);
				return -1;
			}
			/* offset += bytes; */
			bytes_in_pipe -= bytes;
		}
	
		total_bytes_recv += bytes_recv;
	}
	
	close(pipefd[0]);
	close(pipefd[1]);
	return total_bytes_recv;
}

void
create_dc_stream_client(struct rdma_cb *cb, int num, struct sockaddr_in *dest)
{
	struct Rcinfo *rcinfo;
	int i;
	struct ibv_qp_init_attr init_attr;
	struct rdma_conn_param conn_param;
	int ret;
	
	for (i = 0; i < num; i ++) {
		rcinfo = (RCINFO *) malloc(sizeof(RCINFO));
		if (rcinfo == NULL) {
			syslog(LOG_ERR, "malloc fail");
			exit(EXIT_FAILURE);
		}
	
		/* create channel */
		rcinfo->cm_channel = rdma_create_event_channel();
		if (!rcinfo->cm_channel) {
			syslog(LOG_ERR, "rdma_create_event_channel fail: %m");
			exit(EXIT_FAILURE);
		}

		ret = rdma_create_id(rcinfo->cm_channel, &rcinfo->cm_id, rcinfo, RDMA_PS_TCP);
		if (ret) {
			syslog(LOG_ERR, "rdma_create_id fail: %m");
			rdma_destroy_event_channel(rcinfo->cm_channel);
			exit(EXIT_FAILURE);
		}
	
		/* resolve addr */
		ret = rdma_resolve_addr(rcinfo->cm_id, NULL, \
			(struct sockaddr *) dest, 2000);
		if (ret) {
			syslog(LOG_ERR, "rdma_resolve_addr: %m");
			exit(EXIT_FAILURE);
		}
		
		ret = get_next_channel_event(rcinfo->cm_channel, RDMA_CM_EVENT_ADDR_RESOLVED);
		if (ret) {
			syslog(LOG_ERR, "get_next_channel_event");
			exit(EXIT_FAILURE);
		}
		
		/* resolve route */
		ret = rdma_resolve_route(rcinfo->cm_id, 2000);
		if (ret) {
			syslog(LOG_ERR, "rdma_resolve_route: %m");
			exit(EXIT_FAILURE);
		}
		
		ret = get_next_channel_event(rcinfo->cm_channel, RDMA_CM_EVENT_ROUTE_RESOLVED);
		if (ret) {
			syslog(LOG_ERR, "get_next_channel_event");
			exit(EXIT_FAILURE);
		}
		
		/* create pd ????????????? */
		rcinfo->pd = ibv_alloc_pd(rcinfo->cm_id->verbs);
		if (!rcinfo->pd) {
			syslog(LOG_ERR, "ibv_alloc_pd failed\n");
			exit(EXIT_FAILURE);
		}
		
		/* create qp */
		memset(&init_attr, 0, sizeof(init_attr));
		init_attr.cap.max_send_wr = IPERF_RDMA_SQ_DEPTH;
		init_attr.cap.max_recv_wr = 4;
		init_attr.cap.max_recv_sge = 4;
		init_attr.cap.max_send_sge = 4;
		init_attr.qp_type = IBV_QPT_RC;
		init_attr.send_cq = cb->cq;
		init_attr.recv_cq = cb->cq;
		
		ret = rdma_create_qp(rcinfo->cm_id, cb->pd, &init_attr);
		if (!ret)
			rcinfo->qp = rcinfo->cm_id->qp;
		else {
			syslog(LOG_ERR, "rdma_create_qp fail\n");
			exit(EXIT_FAILURE);
		}
		
		/* connect cm_id */
		memset(&conn_param, 0, sizeof conn_param);
		conn_param.responder_resources = 1;
		conn_param.initiator_depth = 1;
		conn_param.retry_count = 10;
		
		ret = rdma_connect(rcinfo->cm_id, &conn_param);
		if (ret) {
			syslog(LOG_ERR, "rdma_connect");
			exit(EXIT_FAILURE);
		}
		
		ret = get_next_channel_event(rcinfo->cm_channel, RDMA_CM_EVENT_ESTABLISHED);
		if (ret) {
			syslog(LOG_ERR, "get_next_channel_event");
			exit(EXIT_FAILURE);
		}
		
		syslog(LOG_ERR, "new connection: cma_id %p", rcinfo->cm_id);
		
		TAILQ_INSERT_TAIL(&rcif_tqh, rcinfo, entries);
	}
	
syslog(LOG_ERR, "established %d connections", i);
	return;
}

int
get_next_channel_event(struct rdma_event_channel *channel, enum rdma_cm_event_type wait_event)
{
	int ret;
	struct rdma_cm_event *event;
	
	ret = rdma_get_cm_event(channel, &event);
	if (ret) {
		syslog(LOG_ERR, "rdma_get_cm_event %m");
		exit(ret);
	}
	
	if (event->event != wait_event) {
		syslog(LOG_ERR, "event is %d instead of %d\n", \
			event->event, wait_event);
		return 1;
	}
	
	rdma_ack_cm_event(event);
	
	return 0;
}

void create_dc_stream_server(struct rdma_cb *cb, int num)
{
	struct Rcinfo *rcinfo;
	int i;
	struct wcm_id *item;
	struct ibv_qp_init_attr init_attr;
	struct rdma_conn_param conn_param;
	int ret;

	/* wait for RC streams connection */
	TAILQ_LOCK(&acceptedTqh);
	for (i = 0; i < num; i ++) {
		while ( TAILQ_EMPTY(&acceptedTqh) )
			if ( TAILQ_WAIT(&acceptedTqh) != 0)
				fprintf(stderr, "TAILQ_WAIT acceptedTqh\n");
		
		item = TAILQ_FIRST(&acceptedTqh);
		TAILQ_REMOVE(&acceptedTqh, item, entries);
		
		rcinfo = (RCINFO *) malloc(sizeof(RCINFO));
		if (rcinfo == NULL) {
			perror("malloc fail:");
			exit(EXIT_FAILURE);
		}
		
		memset(rcinfo, '\0', sizeof(RCINFO));
		
		rcinfo->cm_id = item->child_cm_id;
		
		/* create channel */
		rcinfo->cm_channel = rdma_create_event_channel();
		if (!rcinfo->cm_channel) {
			perror("rdma_create_event_channel");
			exit(EXIT_FAILURE);
		}
		
		/* create pd */
		rcinfo->pd = ibv_alloc_pd(rcinfo->cm_id->verbs);
		if (!rcinfo->pd) {
			fprintf(stderr, "ibv_alloc_pd failed\n");
			exit(EXIT_FAILURE);
		}

		/* create qp */		
		DPRINTF(("before iperf_create_qp\n"));

		memset(&init_attr, 0, sizeof(init_attr));
		init_attr.cap.max_send_wr = IPERF_RDMA_SQ_DEPTH;
		init_attr.cap.max_recv_wr = 4;
		init_attr.cap.max_recv_sge = 4;
		init_attr.cap.max_send_sge = 4;
		init_attr.qp_type = IBV_QPT_RC;
		init_attr.send_cq = cb->cq;
		init_attr.recv_cq = cb->cq;

		DPRINTF(("before rdma_create_qp\n"));
		ret = rdma_create_qp(rcinfo->cm_id, cb->pd, &init_attr);
		if (!ret)
			rcinfo->qp = rcinfo->cm_id->qp;
		else {
			fprintf(stderr, "rdma_create_qp fail\n");
			exit(EXIT_FAILURE);
		}
		
		DPRINTF(("after rdma_create_qp, ret = %d\n", ret));
		DEBUG_LOG("accepting client connection request\n");

		
		/* accept new connection */
		memset(&conn_param, 0, sizeof conn_param);
		conn_param.responder_resources = 1;
		conn_param.initiator_depth = 1;
		
		ret = rdma_accept(rcinfo->cm_id, &conn_param);
		if (ret) {
			perror("rdma_accept");
			syslog(LOG_ERR, "rdma_accept fail: %m");
			return;
		}
		
/* 		syslog(LOG_ERR, "before wait for a-accept success");
		sem_wait(&cb->sem);
		syslog(LOG_ERR, "finish wait for a-accept success"); */
		
		/* wait for establish
		ret = get_next_channel_event(rcinfo->cm_channel, RDMA_CM_EVENT_ESTABLISHED);
		if (ret) {
			syslog(LOG_ERR, "get_next_channel_event");
			exit(EXIT_FAILURE);
		} */
		
		TAILQ_INSERT_TAIL(&rcif_tqh, rcinfo, entries);
		
		free(item);
		
	}
	
	TAILQ_UNLOCK(&acceptedTqh);
	
	/* create new channel and QP */
	TAILQ_FOREACH(rcinfo, &rcif_tqh, entries) {

		
/*
	ret = rdma_create_id(cb->cm_channel, &cb->cm_id, cb, RDMA_PS_TCP);
	if (ret) {
		perror("rdma_create_id");
		rdma_destroy_event_channel(cb->cm_channel);
		free(cb);
		return -1;
	}

	if (pthread_create(&cb->cmthread, NULL, cm_thread, cb) != 0)
		perror("pthread_create"); */
		
		/* create pd ?????????????  */
		

/*	cb->channel = ibv_create_comp_channel(cm_id->verbs);
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
	} */


/*	sem_wait(&cb->sem);
	if (cb->state == STATE_ERROR) {
		fprintf(stderr, "wait for CONNECTED state %d\n", cb->state);
		return -1;
	}
	DPRINTF(("iperf_accept finish with state %d\n", cb->state)); */
	}
		
	return;
}


void create_dc_qp(struct rdma_cb *cb, int num, int isrequest)
{
	int i, ret;
	EVENTWR *evwr;
	struct ibv_send_wr *bad_wr;
	struct ibv_qp_init_attr init_attr;
	struct ibv_qp_attr attr;
	struct ibv_port_attr port_attr;
	int flags;
	
	DATAQP *item;
	
	struct ibv_context *context;
	struct ibv_device  *ib_dev = NULL;
	
	int num_of_device;
	struct ibv_device **dev_list;
	char *ib_devname = NULL;
	
	char msg[128];
	
	ib_devname = opt.ib_devname;
	syslog(LOG_ERR, "device name is %s", ib_devname);
	
	/* get device */
	dev_list = ibv_get_device_list(&num_of_device);

	if (num_of_device <= 0) {
		fprintf(stderr," Did not detect devices \n");
		fprintf(stderr," If device exists, check if driver is up\n");
		return;
	}

	if (!ib_devname) {
		ib_dev = dev_list[0];
		if (ib_dev == NULL)
			fprintf(stderr, "No IB devices found\n");
	} else {
		for (; (ib_dev = *dev_list); ++dev_list)
			if (!strcmp(ibv_get_device_name(ib_dev), ib_devname))
				break;
		if (!ib_dev)
			fprintf(stderr, "IB device %s not found\n", ib_devname);
	}
	
	/* get context */
	context = ibv_open_device(ib_dev);
	if (!context) {
		fprintf(stderr, "Couldn't get context for %s\n",
			ibv_get_device_name(ib_dev));
		return;
	}
	
	/* get gid */
	union ibv_gid temp_gid;
	if (ibv_query_gid(context, 1, 0, &temp_gid)) {
		fprintf(stderr, "ibv_query_gid fail");
		return;
	}
	
/*	if (ibv_query_port(context, port, &port_attr) != 0) { */
	if (ibv_query_port(context, 1, &port_attr) != 0) {
		syslog(LOG_ERR, "ibv_query_port: %m");
		exit(EXIT_FAILURE);
	}
	
	/* get send work request */
	TAILQ_LOCK(&free_evwr_tqh);

	while (TAILQ_EMPTY(&free_evwr_tqh)) {
		if (TAILQ_WAIT(&free_evwr_tqh) != 0)
			continue;	/* goto while */
	}

	evwr = TAILQ_FIRST(&free_evwr_tqh);
	TAILQ_REMOVE(&free_evwr_tqh, evwr, entries);
	
	TAILQ_UNLOCK(&free_evwr_tqh);
	
	srand48(getpid() * time(NULL));
	
/* len(4) + (lid 2 out_reads 2 qpn 3 psn 3) */
	for (i = 0; i < num; i ++) {
		/* create qp */
		item = (DATAQP *) malloc(sizeof(DATAQP));
		if (item == NULL) {
			syslog(LOG_ERR, "malloc %m");
			exit(EXIT_FAILURE);
		}
		
		memset(&init_attr, 0, sizeof(init_attr));
		
		init_attr.cap.max_send_wr = IPERF_RDMA_SQ_DEPTH;
		init_attr.cap.max_recv_wr = 4;
		init_attr.cap.max_recv_sge = 4;
		init_attr.cap.max_send_sge = 4;
		init_attr.qp_type = IBV_QPT_RC;
		init_attr.send_cq = cb->cq;
		init_attr.recv_cq = cb->cq;
		
		item->qp = ibv_create_qp(cb->pd, &init_attr);
		if (item->qp == NULL) {
			syslog(LOG_ERR, "can not create qp: %m");
			exit(EXIT_FAILURE);
		}
		
		item->loc_lid = port_attr.lid;
		item->loc_qpn = item->qp->qp_num;
		item->loc_psn = lrand48() & 0xffffff;
		item->loc_out_reads = 4;
		
		/* assemble qp information */
		memset(msg, '\0', 128);
		
		sprintf(msg, "%04x:%04x:%06x:%06x", \
			item->loc_lid, item->loc_out_reads, \
			item->loc_qpn, item->loc_psn);
		
		syslog(LOG_ERR, "new loc qp attr: %s", msg);
		
		memcpy(evwr->ev_buf.addr + i * 23 + 4 + 47, msg, 23);
		
		/* modify qp to init */
		flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT;
		memset(&attr, 0, sizeof(struct ibv_qp_attr));
		
		attr.qp_state        = IBV_QPS_INIT;
		attr.pkey_index      = 0;
		attr.port_num        = 1; /* param->ib_port; */
		attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE |
				       IBV_ACCESS_LOCAL_WRITE;

		flags |= IBV_QP_ACCESS_FLAGS;
		    
		if (ibv_modify_qp(item->qp, &attr, flags))  {
			fprintf(stderr, "Failed to modify QP to INIT\n");
			syslog(LOG_ERR, "Failed to modify QP to INIT\n");
			return;
		}
		
		/* set loc gid */
		memcpy(item->loc_gid.raw, temp_gid.raw, 16);
		
		TAILQ_INSERT_TAIL(&dcqp_tqh, item, entries);
	}
	
	/* num */
	num = htonl(i);
	memcpy(evwr->ev_buf.addr, &num, 4);
	
	/* gid */
	memset(msg, '\0', 128);
	sprintf(msg, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x", \
			temp_gid.raw[0],temp_gid.raw[1],
			temp_gid.raw[2],temp_gid.raw[3],
			temp_gid.raw[4],temp_gid.raw[5],
			temp_gid.raw[6],temp_gid.raw[7],
			temp_gid.raw[8],temp_gid.raw[9],
			temp_gid.raw[10],temp_gid.raw[11],
			temp_gid.raw[12],temp_gid.raw[13],
			temp_gid.raw[14],temp_gid.raw[15]);
	memcpy(evwr->ev_buf.addr + 4, msg, 47);
	
	syslog(LOG_ERR, "request for %d qp\n", i);
	syslog(LOG_ERR, "loc gid: %s\n", msg);
	
	evwr->ev_buf.mode = kRdmaTrans_ActWrte;
	
	if (isrequest == 1)
		evwr->ev_buf.stat = DC_QP_REQ;
	else
		evwr->ev_buf.stat = DC_QP_REP;

	/* post send request */
	TAILQ_LOCK(&evwr_tqh);
	TAILQ_INSERT_TAIL(&evwr_tqh, evwr, entries);
	TAILQ_UNLOCK(&evwr_tqh);

	ret = ibv_post_send(cb->qp, &evwr->ev_wr, &bad_wr);
	if (ret) {
		syslog(LOG_ERR, "ibv_post_send: %m");
		exit(EXIT_FAILURE);
	}
	
	return;
}

void *
sender(void *arg)
{
	off_t totallen;
	off_t currlen;
	int thislen;
	BUFDATBLK *bufblk;
	REMOTEADDR *rmtaddr;
	EVENTWR *evwr;
	int ret;
	int isuse;
	
	struct ibv_send_wr *bad_wr;
	
	struct rdma_cb *cb = (struct rdma_cb *) arg;
	totallen = cb->filesize;

/*
	DPRINTF(("start create_dc_qp\n"));
	create_dc_qp(cb, 4, 1);
	DPRINTF(("finish create_dc_qp\n")); */
	
	/* establish new RC connection
	DPRINTF(("start create_dc_stream_server\n"));
	create_dc_stream_server(child_dc_cb, 1);
	DPRINTF(("finish create_dc_stream_server\n")); */

/*	for (currlen = 0; currlen < totallen; currlen += thislen) { */
	for (currlen = 0; transcurrlen < transtotallen; currlen += thislen) {
		/* check if the remote addr is available */
		isuse = 0;
		TAILQ_LOCK(&free_evwr_tqh);
		while (TAILQ_EMPTY(&free_evwr_tqh)) {
			if (TAILQ_WAIT(&free_evwr_tqh) != 0)
				continue;	/* goto while */
		}
		evwr = TAILQ_FIRST(&free_evwr_tqh);
		TAILQ_REMOVE(&free_evwr_tqh, evwr, entries);
		
		TAILQ_UNLOCK(&free_evwr_tqh);

		rmtaddr = NULL;
		TAILQ_LOCK(&rmtaddr_tqh);
/*		
		if (!TAILQ_EMPTY(&rmtaddr_tqh)) {
			rmtaddr = TAILQ_FIRST(&rmtaddr_tqh);
			TAILQ_REMOVE(&rmtaddr_tqh, rmtaddr, entries);
		} */
		while (TAILQ_EMPTY(&rmtaddr_tqh)) {
			evwr->ev_buf.mode = kRdmaTrans_ActWrte;
			evwr->ev_buf.stat = ACTIVE_WRITE_RQBLK;
			
			TAILQ_LOCK(&evwr_tqh);
			TAILQ_INSERT_TAIL(&evwr_tqh, evwr, entries);
			TAILQ_UNLOCK(&evwr_tqh);

			ret = ibv_post_send(cb->qp, &evwr->ev_wr, &bad_wr);
			if (ret) {
				fprintf(stderr, "post send error %d\n", ret);
				return 0;
			}
			
			isuse = 1;
			DPRINTF(("sender: post request resource success\n"));
	
			if (TAILQ_WAIT(&rmtaddr_tqh) != 0)
				continue;
		}

		rmtaddr = TAILQ_FIRST(&rmtaddr_tqh);
		TAILQ_REMOVE(&rmtaddr_tqh, rmtaddr, entries);
		
		TAILQ_UNLOCK(&rmtaddr_tqh);

		/* get send block */
		TAILQ_LOCK(&sender_tqh);
		while (TAILQ_EMPTY(&sender_tqh)) {
			if (TAILQ_WAIT(&sender_tqh) != 0)
				continue;	/* goto while */
		}
		
		bufblk = TAILQ_FIRST(&sender_tqh);
		TAILQ_REMOVE(&sender_tqh, bufblk, entries);
		
		TAILQ_UNLOCK(&sender_tqh);

		/* send data */
		thislen = send_dat_blk(bufblk, cb, rmtaddr);
		DPRINTF(("send %d bytes\n", thislen));
		
		/* insert to waiting list */
		TAILQ_LOCK(&waiting_tqh);
		TAILQ_INSERT_TAIL(&waiting_tqh, bufblk, entries);
		TAILQ_UNLOCK(&waiting_tqh);
		
		TAILQ_SIGNAL(&waiting_tqh);

		TAILQ_LOCK(&free_rmtaddr_tqh);
		TAILQ_INSERT_TAIL(&free_rmtaddr_tqh, rmtaddr, entries);
		TAILQ_UNLOCK(&free_rmtaddr_tqh);
		
		TAILQ_SIGNAL(&free_rmtaddr_tqh);
		
		if (isuse == 0) {
			TAILQ_LOCK(&free_evwr_tqh);
			TAILQ_INSERT_TAIL(&free_evwr_tqh, evwr, entries);
			TAILQ_UNLOCK(&free_evwr_tqh);
			
			TAILQ_SIGNAL(&free_evwr_tqh);
		}
		
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
	off_t leftlen;
	int thislen;
	BUFDATBLK *bufblk;
	rmsgheader rhdr;
	
	struct rdma_cb *cb = (struct rdma_cb *) arg;
	totallen = cb->filesize;
	
	FILEINFO *item;
	
	TAILQ_HEAD(, Bufdatblk) 	inner_tqh;
	TAILQ_INIT(&inner_tqh);
	int innersize;
	int seqnum;
	
	long pgsz;
	
	struct stat st;
	
	thislen = 0;
	currlen = 0;
	
	pgsz = getpagesize();
	
	for ( ; ; ) {
		/* get file info block */
		TAILQ_LOCK(&schedule_tqh);
		if (TAILQ_EMPTY(&schedule_tqh)) {
			TAILQ_UNLOCK(&schedule_tqh);
			break;
		}
		
		item = TAILQ_FIRST(&schedule_tqh);
		TAILQ_REMOVE(&schedule_tqh, item, entries);
		
		TAILQ_UNLOCK(&schedule_tqh);
		
		if (   (opt.directio == true)
		    && (stat(item->lf, &st) < 0 || S_ISREG(st.st_mode)))
			item->fd = open(item->lf, O_RDONLY | O_DIRECT);
		else
			item->fd = open(item->lf, O_RDONLY);
		if (item->fd < 0) {
			syslog(LOG_ERR, "Open failed %s", item->lf);
			exit(EXIT_FAILURE);
		}
		
		currlen = seqnum = 0;
		
		/* deal with one file with the size item->fsize */
	do {
		if (currlen >= item->fsize) /* /dev/zero doesn't have EOF */
			break;
		
		/* get free block as much as possible */
		if (TAILQ_EMPTY(&inner_tqh)) {
			TAILQ_LOCK(&free_tqh);
			while (TAILQ_EMPTY(&free_tqh))
				if (TAILQ_WAIT(&free_tqh) != 0)
					continue;

			innersize = 0;
			while (!TAILQ_EMPTY(&free_tqh) && ++innersize < 10) {
				bufblk = TAILQ_FIRST(&free_tqh);
				TAILQ_REMOVE(&free_tqh, bufblk, entries);
				
				TAILQ_INSERT_TAIL(&inner_tqh, bufblk, entries);
			}
			
			TAILQ_UNLOCK(&free_tqh);
		}
		
		while (!TAILQ_EMPTY(&inner_tqh)) {
			bufblk = TAILQ_FIRST(&inner_tqh);
			TAILQ_REMOVE(&inner_tqh, bufblk, entries);
			
			if ((leftlen = item->fsize - currlen) <= 0) {
				TAILQ_INSERT_TAIL(&inner_tqh, bufblk, entries);
				thislen = 0;
				break;
			}
			
			if (  (opt.directio == true)
			   && (leftlen < (bufblk->buflen - sizeof(rmsgheader)))
			   && (leftlen % pgsz != 0) ) {
				/* open - lseek - read - close */
				bufblk->fd = open(item->lf, O_RDONLY);
				if (bufblk->fd < 0) {
					syslog(LOG_ERR, "can not open %s", \
						item->lf);
					exit(EXIT_FAILURE);
				}
				
				lseek(bufblk->fd, currlen, SEEK_CUR);
				
				thislen = readn(bufblk->fd, bufblk->rdma_buf + sizeof(rmsgheader), leftlen);
				
				close(bufblk->fd);
			} else {
				bufblk->fd = item->fd;
				thislen = load_dat_blk(bufblk);
			}
			DPRINTF(("load %d bytes\n", thislen));
			
			if (thislen <= 0) {
				TAILQ_INSERT_TAIL(&inner_tqh, bufblk, entries);
				break;
			}
			
			rhdr.sessionid = item->sessionid;
			rhdr.seqnum = ++ seqnum;
			rhdr.offset = currlen;
			rhdr.dlen = thislen;
			
			currlen += thislen;

			memcpy(bufblk->rdma_buf, &rhdr, sizeof(rmsgheader));
			
			/* insert to sender list */
			TAILQ_LOCK(&sender_tqh);
			TAILQ_INSERT_TAIL(&sender_tqh, bufblk, entries);
			TAILQ_UNLOCK(&sender_tqh);
			
			TAILQ_SIGNAL(&sender_tqh);
		}
	} while (thislen > 0);

	}
	
	/* return inner block to free list */
	while (!TAILQ_EMPTY(&inner_tqh)) {
		bufblk = TAILQ_FIRST(&inner_tqh);
		TAILQ_REMOVE(&inner_tqh, bufblk, entries);
	
		TAILQ_LOCK(&free_tqh);
		TAILQ_INSERT_TAIL(&free_tqh, bufblk, entries);
		TAILQ_UNLOCK(&free_tqh);
	}
	
	/* data read finished */
	pthread_exit((void *) currlen);
}


void *
writer(void *arg)
{
	BUFDATBLK *bufblk;
	off_t currlen;
	int thislen;
	
	TAILQ_HEAD(, Bufdatblk) 	inner_tqh;
	TAILQ_INIT(&inner_tqh);
	
	for (currlen = 0 ; transcurrlen < transtotallen; currlen += thislen) {
		/* get write block */
		TAILQ_LOCK(&writer_tqh);
		while (TAILQ_EMPTY(&writer_tqh))
			if (TAILQ_WAIT(&writer_tqh) != 0)
				continue;	/* goto while */
		
		bufblk = TAILQ_FIRST(&writer_tqh);
		TAILQ_REMOVE(&writer_tqh, bufblk, entries);
		
		TAILQ_UNLOCK(&writer_tqh);
		
		thislen = offload_dat_blk(bufblk);
		
		/* insert to free list */
		TAILQ_LOCK(&free_tqh);
		TAILQ_INSERT_TAIL(&free_tqh, bufblk, entries);
		TAILQ_UNLOCK(&free_tqh);
		
		TAILQ_SIGNAL(&free_tqh);
		
		if (thislen == 0)
			break;
		
		transcurrlen += thislen;
	}
	
	pthread_exit((void *) currlen);
}

void *
scheduler(void *arg)
{
	struct rdma_cb *cb = (struct rdma_cb *) arg;
	
	int ret;
	
	FILEINFO *item;
	FILEINFO *next;
	EVENTWR *evwr;
	struct ibv_send_wr *bad_wr;
	
	item = TAILQ_FIRST(&finfo_tqh);
	
	do {
		next = TAILQ_NEXT(item, entries);

		/* get a send buf */
		TAILQ_LOCK(&free_evwr_tqh);
		while (TAILQ_EMPTY(&free_evwr_tqh))
			if (TAILQ_WAIT(&free_evwr_tqh) != 0)
				continue;	/* goto while */
		
		evwr = TAILQ_FIRST(&free_evwr_tqh);
		TAILQ_REMOVE(&free_evwr_tqh, evwr, entries);
		
		TAILQ_UNLOCK(&free_evwr_tqh);
		
		/* compose send request - FILE_SESSION_ID_REQUEST */
		evwr->ev_buf.mode = kRdmaTrans_ActWrte;
		evwr->ev_buf.stat = FILE_SESSION_ID_REQUEST;
		strncpy(evwr->ev_buf.addr, item->rf, 32);
		memcpy(evwr->ev_buf.addr + 32, &item->fsize, 8);

		TAILQ_LOCK(&evwr_tqh);
		TAILQ_INSERT_TAIL(&evwr_tqh, evwr, entries);
		TAILQ_UNLOCK(&evwr_tqh);
		
		ret = ibv_post_send(cb->qp, &evwr->ev_wr, &bad_wr);
		if (ret)
			syslog(LOG_ERR, "post send error %d\n", ret);
		
		item = next;
		
		/* wait this request finish */
		sem_wait(&cb->sem);
	} while (item != NULL);
	
	pthread_exit(NULL);
}

void *
tcp_sender(void *arg)
{
	off_t totallen;
	off_t currlen;
	off_t leftlen;
	int thislen;
	BUFDATBLK *bufblk;
	rmsgheader rhdr;
	
	int conn = *((int *) arg);
	
	FILEINFO *item;
	
	long pgsz;
	
	struct stat st;
	
	thislen = 0;
	currlen = 0;
	
	pgsz = getpagesize();
	
	char buf[16*1024];
	off_t filelen;
	off_t sendsize;
	off_t n;
	char *bufp;
	register int c, d;
	
	for ( ; ; ) {
		/* get file info block */
		TAILQ_LOCK(&finfo_tqh);
		if (TAILQ_EMPTY(&finfo_tqh)) {
			TAILQ_UNLOCK(&finfo_tqh);
			break;
		}
		
		item = TAILQ_FIRST(&finfo_tqh);
		TAILQ_REMOVE(&finfo_tqh, item, entries);
		
		TAILQ_UNLOCK(&finfo_tqh);
		
		if (   (opt.directio == true)
		    && (stat(item->lf, &st) < 0 || S_ISREG(st.st_mode)))
			item->fd = open(item->lf, O_RDONLY | O_DIRECT);
		else
			item->fd = open(item->lf, O_RDONLY);
		if (item->fd < 0) {
			syslog(LOG_ERR, "Open failed %s", item->lf);
			exit(EXIT_FAILURE);
		}
		
		/* file information(1032) = file path(1024) + file size (8) */
		memset(buf, '\0', 16 * 1024);
		memcpy(buf, item->rf, strlen(item->rf));
		filelen = htonll(item->fsize);
		memcpy(buf + 1024, &filelen, 8);
		if (writen(conn, buf, 1032) != 1032) {
			syslog(LOG_ERR, "writen fail");
			break;
		}
		syslog(LOG_ERR, "start file: %s, size: %ld", \
			item->rf, item->fsize);
		
		/* deal with one file with the size item->fsize */
		if (opt.usesplice == true) {
		printf("use splice to transfer data\n");
			off_t offset;
			offset = 0;
			syslog(LOG_ERR, "fs_splice from disk to socket");
			sendsize = fs_splice(conn, item->fd, offset, item->fsize);
			syslog(LOG_ERR, "fs_splice %s %ld bytes", \
				item->rf, sendsize);
		} else if (opt.usesendfile == true) { /* sendfile */
		printf("use sendfile to transfer data\n");
			off_t offset;
			offset = 0;
			sendfilen(conn, item->fd, offset, item->fsize);
		} else {
		printf("use read/wrtie to transfer data\n");
			while ((c = read(item->fd, buf, sizeof (buf))) > 0) {
				if ((item->fsize -= c) < 0)
					break;
				for (bufp = buf; c > 0; c -= d, bufp += d)
					if ((d = write(conn, bufp, c)) <= 0)
						break;
			}
		}
		
		close(item->fd);
	}
	
	/* close the connection */
	close(conn);
	
	/* data read finished */
	pthread_exit((void *) NULL);
}

void *
tcp_recver(void *arg)
{
	off_t totallen;
	off_t currlen;
	off_t leftlen;
	int thislen;
	BUFDATBLK *bufblk;
	rmsgheader rhdr;
	
	int conn = *((int *) arg);
	
	FILEINFO *item;
	
	int innersize;
	int seqnum;
	
	long pgsz;
	
	struct stat st;
	
	thislen = 0;
	currlen = 0;
	
	pgsz = getpagesize();
	
	char buf[16*1024];
	char filename[1024];
	off_t filesize;
	off_t recvsize;
	int cnt;
	int fd;
	
	for ( ; ; ) {
		/* recv header */
		if (readn(conn, buf, 1032) != 1032)
			break;
		
		memset(filename, '\0', 1024);

		memcpy(filename, buf, 1024);
		memcpy(&filesize, buf + 1024, 8);
		filesize = ntohll(filesize);
		syslog(LOG_ERR, "a new file: %s, size: %ld", \
			filename, filesize);
		
		pthread_mutex_lock(&dir_mutex);
		parsedir(filename);
		pthread_mutex_unlock(&dir_mutex);
		
		/* open file */
		if (   (opt.directio == true)
		    && (stat(filename, &st) < 0 || S_ISREG(st.st_mode)))
			fd = open(filename, O_WRONLY | O_CREAT| O_DIRECT, 0666);
		else
			fd = open(filename, O_WRONLY | O_CREAT, 0666);
		if (fd < 0) {
			syslog(LOG_ERR, "Open failed %s", filename);
			exit(EXIT_FAILURE);
		}
		
		/* recv payload */
		if (opt.usesplice == true) {
			off_t offset;
			offset = 0;
			syslog(LOG_ERR, "sf_splice from socket to disk");
			recvsize = sf_splice(fd, conn, offset, filesize);
			syslog(LOG_ERR, "sf_splice %s %ld bytes", \
				filename, recvsize);
		} else
		do {
			(void) alarm ((unsigned) 900);
			cnt = read(conn, buf, sizeof(buf));
			(void) alarm (0);

			if (cnt > 0) {
				if (writen(fd, buf, cnt) != cnt)
					break;
			}
		} while (cnt > 0 && (filesize -= cnt) > 0);
		
		close(fd);
	}
	
	/* close the connection */
	close(conn);
	
	/* data read finished */
	pthread_exit((void *) currlen);
}

void
parsedir(const char *dir)
{
	char *head;
	char *tail;
	
	char curpath[256];
	
	struct stat st;
	
	head = dir;
	
	while ((tail = strchr(head, '/')) != NULL) {
		memset(curpath, '\0', 256);
		memcpy(curpath, dir, tail - dir);
		
		if (stat(curpath, &st) != 0) {	/* create dir */
			if (mkdir(curpath, S_IRWXU|S_IRGRP|S_IXGRP) != 0)
				syslog(LOG_ERR, "mkdir %s fail: %d(%s)", \
					dir, errno, strerror(errno));
		}
		
		head = tail + 1;
	}
	
	return;
}

void
parsepath(const char *path)
{
	/* recursively resolve files or folders */
	FILEINFO *item;
	struct stat st;
	DIR *dp;
	struct dirent *entry;
	
	if (stat(path, &st) != 0) {
		syslog(LOG_ERR, "lstat fail: %d(%s)", errno, strerror(errno));
		return;
	}
	
	if (S_ISDIR(st.st_mode)) {
		char newpath[1024];
		
		if((dp = opendir(path)) == NULL) {
			syslog(LOG_ERR, "cannot open directory: %s\n", path);
			return;
		}
		
		while((entry = readdir(dp)) != NULL) {
			memset(newpath, '\0', 1024);
			
			/* ignore . and .. */
			if (strcmp(".", entry->d_name) == 0 || 
				strcmp("..", entry->d_name) == 0)
				continue;
			
			if (*(path + strlen(path) - 1) == '/')
				snprintf(newpath, 1024, "%s%s", path, entry->d_name);
			else
				snprintf(newpath, 1024, "%s/%s", path, entry->d_name);
			
			parsepath(newpath);
		}
		
		closedir(dp);
	} else if (S_ISREG(st.st_mode)) {
		item = (FILEINFO *) malloc(sizeof(FILEINFO));
		if (item == NULL) {
			fprintf(stderr, "malloc fail");
			exit(0);
		}
		memset(item, '\0', sizeof(FILEINFO));
	
		strcpy(item->lf, path);
		strcpy(item->rf, path);
		item->offset = 0;
		
		item->fsize = st.st_size;
		
		transtotallen += item->fsize;
		
		TAILQ_INSERT_TAIL(&finfo_tqh, item, entries);
	} else if (S_ISCHR(st.st_mode)) {
		/* suppose this is /dev/zero */
		item = (FILEINFO *) malloc(sizeof(FILEINFO));
		if (item == NULL) {
			fprintf(stderr, "malloc fail");
			exit(0);
		}
		memset(item, '\0', sizeof(FILEINFO));
	
		strcpy(item->lf, path);
		strcpy(item->rf, path);
		item->offset = 0;
		
		item->fsize = opt.devzerosiz;
		
		transtotallen += item->fsize;
		
		TAILQ_INSERT_TAIL(&finfo_tqh, item, entries);
	} else if (S_ISBLK(st.st_mode)) {
		syslog(LOG_ERR, "IS BLK\n");
	} else if (S_ISLNK(st.st_mode)) {
		syslog(LOG_ERR, "IS LNK\n");
	} else if (S_ISSOCK(st.st_mode)) {
		syslog(LOG_ERR, "IS SOCK\n");
	} else {
		syslog(LOG_ERR, "unknown file type\n");
	}
	
	return;
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
send_dat_blk(BUFDATBLK *bufblk, struct rdma_cb *dc_cb, struct Remoteaddr *rmt)
{
	struct ibv_send_wr *bad_wr;
	int ret;
	rmsgheader rhdr;
	
	memcpy(&rhdr, bufblk->rdma_buf, sizeof(rmsgheader));
	
	/* setup wr */
	tsf_setup_wr(bufblk);
	
	/* start data transfer using RDMA_WRITE */
	bufblk->rdma_sq_wr.opcode = IBV_WR_RDMA_WRITE;
	bufblk->rdma_sq_wr.wr.rdma.rkey = rmt->rkey;
	bufblk->rdma_sq_wr.wr.rdma.remote_addr = rmt->buf;
	bufblk->rdma_sq_wr.sg_list->length = rhdr.dlen + sizeof(rmsgheader);
	bufblk->rdma_sq_wr.wr_id = bufblk->wr_id;
	
/*	bufblk->rdma_sq_wr.send_flags = IBV_SEND_SIGNALED;
        bufblk->rdma_sq_wr.next = NULL; */
	
	DPRINTF(("start data transfer using RDMA_WRITE\n"));
/*	DEBUG_LOG("rdma write from lkey %x laddr %x len %d\n",
		  bufblk->rdma_sq_wr.sg_list->lkey,
		  bufblk->rdma_sq_wr.sg_list->addr,
		  bufblk->rdma_sq_wr.sg_list->length);
*/
	
	RCINFO *item;
	item = TAILQ_FIRST(&rcif_tqh);
	TAILQ_REMOVE(&rcif_tqh, item, entries);
	TAILQ_INSERT_TAIL(&rcif_tqh, item, entries);
	
/*	ret = ibv_post_send(dc_cb->qp, &bufblk->rdma_sq_wr, &bad_wr); */
	ret = ibv_post_send(item->qp, &bufblk->rdma_sq_wr, &bad_wr);
	if (ret) {
		fprintf(stderr, "post send error %d\n", ret);
		return 0;
	}
	
	transcurrlen += rhdr.dlen;
	
/*	dc_cb->state != ACTIVE_WRITE_POST; -> Todo */
	DPRINTF(("send_dat_blk: ibv_post_send finish\n"));
	/* wait the finish of RDMA_WRITE
	sem_wait(&dc_cb->sem); */
	DPRINTF(("sem_wait finish of RDMA_WRITE success\n"));
	
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


void
dc_conn_req(struct rdma_cb *cb)
{
	EVENTWR *evwr;
	struct ibv_send_wr *bad_wr;
	int ret;
	
	/* get a send buf */
	TAILQ_LOCK(&free_evwr_tqh);
	while (TAILQ_EMPTY(&free_evwr_tqh))
		if (TAILQ_WAIT(&free_evwr_tqh) != 0)
			continue;	/* goto while */
	
	evwr = TAILQ_FIRST(&free_evwr_tqh);
	TAILQ_REMOVE(&free_evwr_tqh, evwr, entries);
	
	TAILQ_UNLOCK(&free_evwr_tqh);
	
	/* compose send request - FILE_SESSION_ID_REQUEST */
	evwr->ev_buf.mode = kRdmaTrans_ActWrte;
	evwr->ev_buf.stat = DC_CONNECTION_REQ;
	strncpy(evwr->ev_buf.addr, &opt.rcstreamnum, 4);
	
	TAILQ_LOCK(&evwr_tqh);
	TAILQ_INSERT_TAIL(&evwr_tqh, evwr, entries);
	TAILQ_UNLOCK(&evwr_tqh);
	
	ret = ibv_post_send(cb->qp, &evwr->ev_wr, &bad_wr);
	if (ret)
		syslog(LOG_ERR, "post send error %d\n", ret);
	
	return;
}
