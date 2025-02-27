/*	$NetBSD: btpand.c,v 1.9 2024/06/04 06:24:58 plunky Exp $	*/

/*-
 * Copyright (c) 2008-2009 Iain Hibbert
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008-2009 Iain Hibbert. All rights reserved.");
__RCSID("$NetBSD: btpand.c,v 1.9 2024/06/04 06:24:58 plunky Exp $");

#include <sys/wait.h>

#include <bluetooth.h>
#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <sdp.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "btpand.h"

/* global variables */
const char *	control_path;		/* -c <path> */
const char *	interface_name;		/* -i <ifname> */
const char *	service_type;		/* -s <service> */
uint16_t	service_class;
const char *	service_name;
const char *	service_desc;

bdaddr_t	local_bdaddr;		/* -d <addr> */
bdaddr_t	remote_bdaddr;		/* -a <addr> */
uint16_t	l2cap_psm;		/* -p <psm> */
int		l2cap_mode;		/* -m <mode> */
int		server_limit;		/* -n <limit> */

static const struct {
	const char *	type;
	uint16_t	class;
	const char *	name;
	const char *	desc;
} services[] = {
	{ "PANU", SDP_SERVICE_CLASS_PANU,
	  "Personal Ad-hoc User Service",
	  "Personal Ad-hoc User Service"
	},
	{ "NAP",  SDP_SERVICE_CLASS_NAP,
	  "Network Access Point",
	  "Personal Ad-hoc Network Service"
	},
	{ "GN",	  SDP_SERVICE_CLASS_GN,
	  "Group Ad-hoc Network",
	  "Personal Group Ad-hoc Network Service"
	},
};

__dead static void main_exit(int);
static void main_detach(void);
__dead static void usage(void);

int
main(int argc, char *argv[])
{
	unsigned long	ul;
	char *		ep;
	int		ch, status;

	while ((ch = getopt(argc, argv, "a:c:d:i:l:m:p:S:s:")) != -1) {
		switch (ch) {
		case 'a': /* remote address */
			if (!bt_aton(optarg, &remote_bdaddr)) {
				struct hostent  *he;

				if ((he = bt_gethostbyname(optarg)) == NULL)
					errx(EXIT_FAILURE, "%s: %s",
					    optarg, hstrerror(h_errno));

				bdaddr_copy(&remote_bdaddr, (bdaddr_t *)he->h_addr);
			}

			break;

		case 'c': /* control socket path */
			control_path = optarg;
			break;

		case 'd': /* local address */
			if (!bt_devaddr(optarg, &local_bdaddr))
				err(EXIT_FAILURE, "%s", optarg);

			break;

		case 'i': /* tap interface name */
			if (strchr(optarg, '/') == NULL) {
				asprintf(&ep, "/dev/%s", optarg);
				interface_name = ep;
			} else {
				interface_name = optarg;
			}

			break;

		case 'l': /* limit server sessions */
			ul = strtoul(optarg, &ep, 10);
			if (*optarg == '\0' || *ep != '\0' || ul == 0)
				errx(EXIT_FAILURE, "%s: invalid session limit", optarg);

			server_limit = ul;
			break;

		case 'm': /* link mode */
			if (strcasecmp(optarg, "auth") == 0)
				l2cap_mode = L2CAP_LM_AUTH;
			else if (strcasecmp(optarg, "encrypt") == 0)
				l2cap_mode = L2CAP_LM_ENCRYPT;
			else if (strcasecmp(optarg, "secure") == 0)
				l2cap_mode = L2CAP_LM_SECURE;
			else if (strcasecmp(optarg, "none"))
				errx(EXIT_FAILURE, "%s: unknown mode", optarg);

			break;

		case 'p': /* protocol/service multiplexer */
			ul = strtoul(optarg, &ep, 0);
			if (*optarg == '\0' || *ep != '\0'
			    || ul > 0xffff || L2CAP_PSM_INVALID(ul))
				errx(EXIT_FAILURE, "%s: invalid PSM", optarg);

			l2cap_psm = (uint16_t)ul;
			break;

		case 's': /* service */
		case 'S': /* service (no SDP) */
			for (ul = 0; ul < __arraycount(services); ul++) {
				if (strcasecmp(optarg, services[ul].type) == 0)
					break;
			}

			if (ul == __arraycount(services))
				errx(EXIT_FAILURE, "%s: unknown service", optarg);

			if (ch == 's') {
				service_type = services[ul].type;
				service_name = services[ul].name;
				service_desc = services[ul].desc;
			}

			service_class = services[ul].class;
			break;

		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;

	/* validate options */
	if (bdaddr_any(&local_bdaddr) || service_class == 0)
		usage();

	if (!bdaddr_any(&remote_bdaddr) && (server_limit != 0 ||
	    control_path != 0 || (service_type != NULL && l2cap_psm != 0)))
		usage();

	/* default options */
	if (interface_name == NULL)
		interface_name = "/dev/tap";

	if (l2cap_psm == 0)
		l2cap_psm = L2CAP_PSM_BNEP;

	if (bdaddr_any(&remote_bdaddr) && server_limit == 0) {
		if (service_class == SDP_SERVICE_CLASS_PANU)
			server_limit = 1;
		else
			server_limit = 7;
	}

#ifdef L2CAP_LM_MASTER
	if (server_limit > 1 && service_class != SDP_SERVICE_CLASS_PANU)
		l2cap_mode |= L2CAP_LM_MASTER;
#endif

	/*
	 * fork() now so that the setup can be done in the child process
	 * (as kqueue is not inherited) but block in the parent until the
	 * setup is finished so we can return an error if necessary.
	 */
	switch(fork()) {
	case -1: /* bad */
		err(EXIT_FAILURE, "fork() failed");
		/*NOTREACHED*/

	case 0:	/* child */
		openlog(getprogname(), LOG_NDELAY | LOG_PERROR | LOG_PID, LOG_DAEMON);

		channel_init();
		event_init();
		server_init();
		client_init();
		tap_init();

		main_detach();

		event_dispatch();
		break;

	default: /* parent */
		signal(SIGUSR1, main_exit);
		wait(&status);

		if (WIFEXITED(status))
			exit(WEXITSTATUS(status));

		break;
	}

	err(EXIT_FAILURE, "exiting");
}

static void
main_exit(int s)
{

	/* child is all grown up */
	_exit(EXIT_SUCCESS);
}

static void
main_detach(void)
{
	int fd;

	if (kill(getppid(), SIGUSR1) == -1)
		log_err("Could not signal main process: %m");

	if (setsid() == -1)
		log_err("setsid() failed");

	fd = open(_PATH_DEVNULL, O_RDWR, 0);
	if (fd == -1) {
		log_err("Could not open %s", _PATH_DEVNULL);
	} else {
		(void)dup2(fd, STDIN_FILENO);
		(void)dup2(fd, STDOUT_FILENO);
		(void)dup2(fd, STDERR_FILENO);
		close(fd);
	}
}

static void
usage(void)
{
	const char *p = getprogname();
	size_t n = strlen(p);

	fprintf(stderr,
	    "usage: %s [-i ifname] [-m mode] -a address -d device\n"
	    "       %*s {-s service | -S service [-p psm]}\n"
	    "       %s [-c path] [-i ifname] [-l limit] [-m mode] [-p psm] -d device\n"
	    "       %*s {-s service | -S service}\n"
	    "\n"
	    "Where:\n"
	    "\t-a address  remote bluetooth device\n"
	    "\t-c path     SDP server socket\n"
	    "\t-d device   local bluetooth device\n"
	    "\t-i ifname   tap interface\n"
	    "\t-l limit    limit server sessions\n"
	    "\t-m mode     L2CAP link mode\n"
	    "\t-p psm      L2CAP PSM\n"
	    "\t-S service  service name (no SDP)\n"
	    "\t-s service  service name\n"
	    "\n"
	    "Known services:\n"
	    "", p, (int)n, "", p, (int)n, "");

	for (n = 0; n < __arraycount(services); n++)
		fprintf(stderr, "\t%s\t%s\n", services[n].type, services[n].name);

	exit(EXIT_FAILURE);
}
