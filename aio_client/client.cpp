
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

using namespace  std;

#include "spdlog/spdlog.h"
#include <nng/nng.h>
#include <nng/protocol/reqrep0/req.h>
#include <nng/supplemental/util/platform.h>

atomic_uint64_t _g;

void
fatal(const char *func, int rv)
{
	fprintf(stderr, "%s: %s\n", func, nng_strerror(rv));
	exit(1);
}

/*  The client runs just once, and then returns. */
int
client(const char *url, const char *msecstr)
{
	nng_socket sock;
	int        rv;
	nng_msg *  msg;
	nng_time   start;
	nng_time   end;
	unsigned   msec;

	msec = atoi(msecstr);

	if ((rv = nng_req0_open(&sock)) != 0) {
		fatal("nng_req0_open", rv);
	}

	if ((rv = nng_dial(sock, url, NULL, 0)) != 0) {
		fatal("nng_dial", rv);
	}

	start = nng_clock();
	for (int i = 0; i < 100000; ++i) {
		++_g;
		if ((rv = nng_msg_alloc(&msg, 0)) != 0) {
			fatal("nng_msg_alloc", rv);
		}
		if ((rv = nng_msg_append_u32(msg, msec)) != 0) {
			fatal("nng_msg_append_u32", rv);
		}

		if ((rv = nng_sendmsg(sock, msg, 0)) != 0) {
			fatal("nng_sendmsg", rv);
		}

		if ((rv = nng_recvmsg(sock, &msg, 0)) != 0) {
			fatal("nng_recvmsg", rv);
		}
		end = nng_clock();
		nng_msg_free(msg);
	}
	nng_close(sock);

	printf("Request took %u milliseconds.\n", (uint32_t)(end - start));
	return (0);
}

int
main(int argc, char **argv)
{
	int rc;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <url> <secs>\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	vector<thread> ths;
	for (int i = 0; i < 32; ++i) 
	{
		ths.push_back(thread([&](){
			client(argv[1], argv[2]);
						}));
	}
	for (int i =0; i<ths.size();++i) {
		ths[i].join();
	}
	assert(_g.load() == 32*100000);
	SPDLOG_INFO("total send request count {}", _g.load());
	return 0;
}
