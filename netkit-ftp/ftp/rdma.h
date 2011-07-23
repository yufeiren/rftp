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

#define __USE_GNU		/* for splice */
#include <fcntl.h>

#include <semaphore.h>
#include <sys/sendfile.h>

#include "queue.h"

extern int rdma_debug;
#define DEBUG_LOG if (rdma_debug) printf
// #define DEBUG_LOG printf

extern FILE *fin;

// rdma_listen backlog
#define RLISTENBACKLOG		32

// wr_id bitmask
#define WRIDEVENT	0x0100000000000000
#define WRIDBUFFER	0x0200000000000000
#define WRIDRECV	0x0400000000000000

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
	ACTIVE_WRITE_RQBLK,
	ACTIVE_WRITE_RPBLK,
	DC_QP_REQ,
	DC_QP_REP,
	DC_CONNECTION_REQ,
	FILE_SESSION_ID_REQUEST,
	FILE_SESSION_ID_RESPONSE,
	RDMA_WRITE_COMPLETE,
	RDMA_READ_COMPLETE,
	STATE_ERROR
/*	PASSIVE_READ,
	PASSIVE_WRITE,
	ACTIVE_READ,
	TRANS_COMPLETE,
	ERROR*/
};

struct rdma_info_blk {
	uint64_t wr_id;
	uint64_t buf;
	uint32_t rkey;
	uint32_t size;
	uint32_t mode;	/* for rdma transfer mode */
	uint32_t stat;  /* status */
	char addr[256];	/* support multiple addr a time */
			/* also used for qp information exchange - just for demo. otherwise this logic should be condemned */
			/* also used for file session id */
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


struct Bufdatblk {
	uint64_t		wr_id;		/* work request id */
	int			buflen;
	struct ibv_send_wr	rdma_sq_wr;	/* rdma work request record */
	struct ibv_sge		rdma_sgl;	/* rdma single SGE */
	char			*rdma_buf;	/* used as rdma sink or source*/
	struct ibv_mr 		*rdma_mr;
	int			fd;		/* in and out fd */
	struct ibv_qp 		*qp;		/* qp */
	int			seqnum;
	off_t			offset;
	
	TAILQ_ENTRY(Bufdatblk) entries;
};
typedef struct Bufdatblk BUFDATBLK;

struct Fileinfo {
	char   lf[1024];	/* local file name */
	char   rf[1024];	/* remote file name */
	int    fd;
	int    sessionid;
	off_t  offset;
	int    seqnum;		/* the next wanted sequence num */
	off_t  fsize;
	
	TAILQ_HEAD(, Bufdatblk) pending_tqh;

	TAILQ_ENTRY(Fileinfo) entries;
};
typedef struct Fileinfo FILEINFO;

struct Remoteaddr {
	uint64_t buf;
	uint32_t rkey;
	uint32_t size;
	
	TAILQ_ENTRY(Remoteaddr) entries;
};
typedef struct Remoteaddr REMOTEADDR;

/* sendeventwr is better */
struct Eventwr {
	uint64_t wr_id;
	struct ibv_send_wr ev_wr;	/* send work request record */
	struct ibv_sge ev_sgl;	/* send single SGE */
	struct rdma_info_blk ev_buf;  /* single send buf */
	struct ibv_mr *ev_mr;		/* MR associated with this buffer */
	
	TAILQ_ENTRY(Eventwr) entries;
};
typedef struct Eventwr EVENTWR;


struct Recvwr {
	uint64_t wr_id;
	struct ibv_recv_wr recv_wr;	/* recv work request record */
	struct ibv_sge recv_sgl;	/* recv single SGE */
	struct rdma_info_blk recv_buf;  /* single recv buf */
	struct ibv_mr *recv_mr;		/* MR associated with this buffer */
	
	TAILQ_ENTRY(Eventwr) entries;
};
typedef struct Recvwr RECVWR;

struct Dataqp {
	int loc_lid;
	int loc_out_reads;
	int loc_qpn;
	int loc_psn;
	union ibv_gid loc_gid;
	int rem_lid;
	int rem_out_reads;
	int rem_qpn;
	int rem_psn;
	union ibv_gid rem_gid;
	
	struct ibv_qp *qp;
	
	TAILQ_ENTRY(Dataqp) entries;
};
typedef struct Dataqp DATAQP;

struct Rcinfo {		/* reliable connection information */
	struct ibv_comp_channel *channel;
	struct ibv_pd *pd;
	struct ibv_qp *qp;
	
	struct rdma_event_channel *cm_channel;
	struct rdma_cm_id *cm_id;
	
	TAILQ_ENTRY(Rcinfo) entries;
};
typedef struct Rcinfo RCINFO;


/* res */
	/* free list, sender list, and writer list */
TAILQ_HEAD(, Bufdatblk) 	free_tqh;
TAILQ_HEAD(, Bufdatblk) 	sender_tqh;
TAILQ_HEAD(, Bufdatblk) 	writer_tqh;
TAILQ_HEAD(, Bufdatblk) 	waiting_tqh;

	/* remote addr infor addr */
TAILQ_HEAD(, Remoteaddr)	free_rmtaddr_tqh;
TAILQ_HEAD(, Remoteaddr)	rmtaddr_tqh;

	/* event work request addr */
TAILQ_HEAD(, Eventwr)		free_evwr_tqh;
TAILQ_HEAD(, Eventwr)		evwr_tqh;

	/* event work request addr */
TAILQ_HEAD(, Recvwr)		recvwr_tqh;

	/* queue pair list */
TAILQ_HEAD(, Dataqp)		dcqp_tqh;

	/* RC stream list */
TAILQ_HEAD(, Rcinfo)		rcif_tqh;

	/* multiple file list */
TAILQ_HEAD(, Fileinfo) 		schedule_tqh;
TAILQ_HEAD(, Fileinfo)		finfo_tqh;

/*
 * RDMA Control block struct.
 */
typedef struct rdma_cb {
	int server;			/* 0 if client */
	pthread_t cqthread;
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
	
	int	fd;			/* in and out fd */
	off_t	filesize;		/* filesize */
} rdma_cb;


typedef struct wcm_id {
	struct rdma_cm_id *child_cm_id;
	
	TAILQ_ENTRY(wcm_id) entries;
} wcm_id;

TAILQ_HEAD(acptq, wcm_id);

typedef struct rmsgheader {
	uint32_t sessionid;
	uint32_t seqnum;
	uint64_t offset;
	uint32_t dlen;
	char blank[4076];	/* getpagesize minus 24 = 4096 - 20 */
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

int tsf_setup_buf_list(struct rdma_cb *cb);

void tsf_waiting_to_free(void);

void tsf_free_buf_list(void);

void iperf_free_buffers(struct rdma_cb *cb);

void iperf_setup_wr(struct rdma_cb *cb);

void tsf_setup_wr(BUFDATBLK *bufblk);

int rdma_connect_client(struct rdma_cb *cb);

int iperf_accept(struct rdma_cb *cb);

void iperf_format_send(struct rdma_cb *cb, char *buf, struct ibv_mr *mr);

/* data transfer method

int cli_act_rdma_rd(struct rdma_cb *cb);
int cli_act_rdma_wr(struct rdma_cb *cb);
int cli_pas_rdma_rd(struct rdma_cb *cb);
int cli_pas_rdma_wr(struct rdma_cb *cb);

int svr_act_rdma_rd(struct rdma_cb *cb);
int svr_act_rdma_wr(struct rdma_cb *cb);
int svr_pas_rdma_rd(struct rdma_cb *cb);
int svr_pas_rdma_wr(struct rdma_cb *cb); */

ssize_t	 readn(int, void *, size_t);		/* from APUE2e */
ssize_t	 writen(int, const void *, size_t);	/* from APUE2e */

/* from 
 * http://blog.superpat.com/2010/06/01/zero-copy-in-linux-with-sendfile-and-splice/ */

ssize_t sendfilen(int out_fd, int in_fd, off_t offset, size_t count);

/* file to socket */
ssize_t fs_splice(int out_fd, int in_fd, off_t offset, size_t count);
/* socket to file */
ssize_t sf_splice(int out_fd, int in_fd, off_t offset, size_t count);

/* start_routine for thread */

void	*sender(void *);
void	*recver(void *);
void	*reader(void *);
void	*writer(void *);

void	*scheduler(void *);

/* for tcp */
void	*tcp_sender(void *);
void	*tcp_recver(void *);

int load_dat_blk(BUFDATBLK *);

int offload_dat_blk(BUFDATBLK *);

int send_dat_blk(BUFDATBLK *, struct rdma_cb *, struct Remoteaddr *);

int recv_dat_blk(BUFDATBLK *, struct rdma_cb *);

void *prep_blk(void *);
void *acpt_blk(void *);
void *notify_blk(void *);

void *handle_qp_req(void *);
void *handle_qp_rep(void *);

void *recv_data(void *);

void handle_wr(struct rdma_cb *, uint64_t wr_id);
/* void *handle_wr(void *); */

void create_dc_qp(struct rdma_cb *, int, int);

void create_dc_stream_server(struct rdma_cb *cb, int num);
void create_dc_stream_client(struct rdma_cb *cb, int num, struct sockaddr_in *dest);

void *handle_file_session_req(void *);
void *handle_file_session_rep(void *);


int get_next_channel_event(struct rdma_event_channel *channel, enum rdma_cm_event_type event);

/* data sink */
void parsedir(const char *dir);

/* data source */
void parsepath(const char *path);

void dc_conn_req(struct rdma_cb *cb);

#ifdef __cplusplus
} /* end extern "C" */
#endif

#endif
