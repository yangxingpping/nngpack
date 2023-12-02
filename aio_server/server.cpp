

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <thread>

#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/supplemental/util/platform.h>

#include "concurrentqueue/blockingconcurrentqueue.h"

#ifndef PARALLEL
#define PARALLEL 128
#endif

using namespace std;

// The server keeps a list of work items, sorted by expiration time,
// so that we can use this to set the timeout to the correct value for
// use in poll.

enum State { INIT, RECV, WAIT, SEND };

struct work {
	
	State    state;
	nng_aio *aio;
	nng_msg *msg;
	nng_ctx  ctx;
};

static moodycamel::BlockingConcurrentQueue<struct work *> _queue;

void
fatal(const char *func, int rv)
{
	fprintf(stderr, "%s: %s\n", func, nng_strerror(rv));
	exit(1);
}

void
server_cb(void *arg)
{
	struct work *work = (struct work*) arg;
	nng_msg *    msg;
	int          rv;
	uint32_t     when;

	switch (work->state) {
	case INIT:
		work->state = RECV;
		nng_ctx_recv(work->ctx, work->aio);
		break;
	case RECV:
		if ((rv = nng_aio_result(work->aio)) != 0) {
			fatal("nng_ctx_recv", rv);
		}
		msg = nng_aio_get_msg(work->aio);
		if ((rv = nng_msg_trim_u32(msg, &when)) != 0) {
			// bad message, just ignore it.
			nng_msg_free(msg);
			nng_ctx_recv(work->ctx, work->aio);
			return;
		}
		work->msg   = msg;
		work->state = WAIT;
		nng_sleep_aio(when, work->aio);
		_queue.enqueue(work);
		break;
	case WAIT:
	{
		
		auto result = nng_aio_result(work->aio);
		nng_aio_set_msg(work->aio, work->msg);

		work->msg = NULL;
		work->state = SEND;
		nng_ctx_send(work->ctx, work->aio);
	}break;
	case SEND:
		if ((rv = nng_aio_result(work->aio)) != 0) {
			nng_msg_free(work->msg);
			fatal("nng_ctx_send", rv);
		}
		work->state = RECV;
		nng_ctx_recv(work->ctx, work->aio);
		break;
	default:
		fatal("bad state!", NNG_ESTATE);
		break;
	}
}

struct work *
alloc_work(nng_socket sock)
{
	struct work *w;
	int          rv;

	if ((w = (struct work*) nng_alloc(sizeof(*w))) == NULL) {
		fatal("nng_alloc", NNG_ENOMEM);
	}
	if ((rv = nng_aio_alloc(&w->aio, server_cb, w)) != 0) {
		fatal("nng_aio_alloc", rv);
	}
	if ((rv = nng_ctx_open(&w->ctx, sock)) != 0) {
		fatal("nng_ctx_open", rv);
	}
	w->state = INIT;
	return (w);
}

// The server runs forever.
int
server(const char *url)
{
	nng_socket   sock;
	struct work *works[PARALLEL];
	int          rv;
	int          i;

	/*  Create the socket. */
	rv = nng_rep0_open(&sock);
	if (rv != 0) {
		fatal("nng_rep0_open", rv);
	}

	for (i = 0; i < PARALLEL; i++) {
		works[i] = alloc_work(sock);
	}

	if ((rv = nng_listen(sock, url, NULL, 0)) != 0) {
		fatal("nng_listen", rv);
	}

	for (i = 0; i < PARALLEL; i++) {
		server_cb(works[i]); // this starts them going (INIT state)
	}

	for (;;) {
		nng_msleep(3600000); // neither pause() nor sleep() portable
	}
}

void
thread2()
{
	while (true) {
		struct work *p { nullptr };
		_queue.wait_dequeue(p);
		nng_aio_cancel(p->aio);
	}
}

int
main(int argc, char **argv)
{
	int rc;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <url>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

		thread thx([&]() { thread2(); });
	thx.detach();
	{
		
	}

	rc = server(argv[1]);
	exit(rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}


