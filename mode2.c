/*      $Id: mode2.c,v 5.19 2009/12/28 13:05:30 lirc Exp $      */

/****************************************************************************
 ** mode2.c *****************************************************************
 ****************************************************************************
 *
 * mode2 - shows the pulse/space length of a remote button
 *
 * Copyright (C) 1998 Trent Piepho <xyzzy@u.washington.edu>
 * Copyright (C) 1998 Christoph Bartelmus <lirc@bartelmus.de>
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef _CYGWIN_
#define __USE_LINUX_IOCTL_DEFS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>

#include "drivers/lirc.h"
#include "daemons/ir_remote.h"
#include "daemons/hardware.h"
#include "daemons/hw-types.h"


#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"


#ifdef DEBUG
int debug = 10;
#else
int debug = 0;
#endif
FILE *lf = NULL;
char *hostname = "";
int daemonized = 0;
char *progname;

void logprintf(int prio, char *format_str, ...)
{
	va_list ap;

	if (lf) {
		time_t current;
		char *currents;

		current = time(&current);
		currents = ctime(&current);

		fprintf(lf, "%15.15s %s %s: ", currents + 4, hostname, progname);
		va_start(ap, format_str);
		if (prio == LOG_WARNING)
			fprintf(lf, "WARNING: ");
		vfprintf(lf, format_str, ap);
		fputc('\n', lf);
		fflush(lf);
		va_end(ap);
	}
	if (!daemonized) {
		fprintf(stderr, "%s: ", progname);
		va_start(ap, format_str);
		if (prio == LOG_WARNING)
			fprintf(stderr, "WARNING: ");
		vfprintf(stderr, format_str, ap);
		fputc('\n', stderr);
		fflush(stderr);
		va_end(ap);
	}
}

void logperror(int prio, const char *s)
{
	if (s != NULL) {
		logprintf(prio, "%s: %s", s, strerror(errno));
	} else {
		logprintf(prio, "%s", strerror(errno));
	}
}

int waitfordata(unsigned long maxusec)
{
	fd_set fds;
	int ret;
	struct timeval tv;

	while (1) {
		FD_ZERO(&fds);
		FD_SET(hw.fd, &fds);
		do {
			do {
				if (maxusec > 0) {
					tv.tv_sec = maxusec / 1000000;
					tv.tv_usec = maxusec % 1000000;
					ret = select(hw.fd + 1, &fds, NULL, NULL, &tv);
					if (ret == 0)
						return (0);
				} else {
					ret = select(hw.fd + 1, &fds, NULL, NULL, NULL);
				}
			}
			while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				logprintf(LOG_ERR, "select() failed\n");
				logperror(LOG_ERR, NULL);
				continue;
			}
		}
		while (ret == -1);

		if (FD_ISSET(hw.fd, &fds)) {
			/* we will read later */
			return (1);
		}
	}
}

int main(int argc, char **argv)
{
	int fd;
	char buffer[sizeof(ir_code)];
	lirc_t data;
	__u32 mode;
	char *device = LIRC_DRIVER_DEVICE;
	struct stat s;
	int dmode = 0;
	__u32 code_length;
	size_t count = sizeof(lirc_t);
	int i;
	int use_raw_access = 0;
	int have_device = 0;

	progname = "mode2";
	hw_choose_driver(NULL);
	while (1) {
		int c;
		static struct option long_options[] = {
			{"help", no_argument, NULL, 'h'},
			{"version", no_argument, NULL, 'v'},
			{"device", required_argument, NULL, 'd'},
			{"driver", required_argument, NULL, 'H'},
			{"mode", no_argument, NULL, 'm'},
			{"raw", no_argument, NULL, 'r'},
			{0, 0, 0, 0}
		};
		c = getopt_long(argc, argv, "hvd:H:mr", long_options, NULL);
		if (c == -1)
			break;
		switch (c) {
		case 'h':
			printf("Usage: %s [options]\n", progname);
			printf("\t -h --help\t\tdisplay usage summary\n");
			printf("\t -v --version\t\tdisplay version\n");
			printf("\t -d --device=device\tread from given device\n");
			printf("\t -H --driver=driver\t\tuse given driver\n");
			printf("\t -m --mode\t\tenable alternative display mode\n");
			printf("\t -r --raw\t\taccess device directly\n");
			return (EXIT_SUCCESS);
		case 'H':
			if (hw_choose_driver(optarg) != 0) {
				fprintf(stderr, "Driver `%s' not supported.\n", optarg);
				hw_print_drivers(stderr);
				exit(EXIT_FAILURE);
			}
			break;
		case 'v':
			printf("%s %s\n", progname, VERSION);
			return (EXIT_SUCCESS);
		case 'd':
			device = optarg;
			have_device = 1;
			break;
		case 'm':
			dmode = 1;
			break;
		case 'r':
			use_raw_access = 1;
			break;
		default:
			printf("Usage: %s [options]\n", progname);
			return (EXIT_FAILURE);
		}
	}
	if (optind < argc) {
		fprintf(stderr, "%s: too many arguments\n", progname);
		return (EXIT_FAILURE);
	}
	if (strcmp(device, LIRCD) == 0) {
		fprintf(stderr, "%s: refusing to connect to lircd socket\n", progname);
		return EXIT_FAILURE;
	}

	if (use_raw_access) {
		fd = open(device, O_RDONLY);
		if (fd == -1) {
			fprintf(stderr, "%s: error opening %s\n", progname, device);
			perror(progname);
			exit(EXIT_FAILURE);
		};

		if ((fstat(fd, &s) != -1) && (S_ISFIFO(s.st_mode))) {
			/* can't do ioctls on a pipe */
		} else if ((fstat(fd, &s) != -1) && (!S_ISCHR(s.st_mode))) {
			fprintf(stderr, "%s: %s is not a character device\n", progname, device);
			fprintf(stderr, "%s: use the -d option to specify the correct device\n", progname);
			close(fd);
			exit(EXIT_FAILURE);
		} else if (ioctl(fd, LIRC_GET_REC_MODE, &mode) == -1) {
			printf("This program is only intended for receivers supporting the pulse/space layer.\n");
			printf("Note that this is no error, but this program "
			       "simply makes no sense for your\n" "receiver.\n");
			printf("In order to test your setup run lircd with "
			       "the --nodaemon option and \n" "then check if the remote works with the irw tool.\n");
			close(fd);
			exit(EXIT_FAILURE);
		}
	} else {
		if (have_device)
			hw.device = device;
		if (!hw.init_func()) {
			return EXIT_FAILURE;
		}
		fd = hw.fd;	/* please compiler */
		mode = hw.rec_mode;
		if (mode != LIRC_MODE_MODE2) {
			if (strcmp(hw.name, "default") == 0) {
				printf("Please use the --raw option to access "
				       "the device directly instead through\n" "the abstraction layer.\n");
			} else {
				printf("This program does not work for this hardware yet\n");
			}
			exit(EXIT_FAILURE);
		}

	}
	if (mode == LIRC_MODE_LIRCCODE) {
		if (use_raw_access) {
			if (ioctl(fd, LIRC_GET_LENGTH, &code_length) == -1) {
				fprintf(stderr, "%s: could not get code length\n", progname);
				perror(progname);
				close(fd);
				exit(EXIT_FAILURE);
			}
		} else {
			code_length = hw.code_length;
		}
		if (code_length > sizeof(ir_code) * CHAR_BIT) {
			fprintf(stderr, "%s: cannot handle %u bit codes\n", progname, code_length);
			close(fd);
			exit(EXIT_FAILURE);
		}
		count = (code_length + CHAR_BIT - 1) / CHAR_BIT;
	}
	int bits=0;
	int counter = 0;
	int bytes = 0;
	char orgline[1000];
	char prevline[1000]={0};
	char *line = orgline;
	double markSum=0;
	int markCount=0;
	double spaceSum=0;
	double spaceCount=0;
	printf("           1          2          3           4          5          6          7           8          9     \n");
	printf("01234567 89012345 67890123 45678901 23456789 01234567 89012345 67890123 45678901 23456789 01234567 89012345\n");
	while (1) {
		int result;

		if (use_raw_access) {
			
			result = read(fd, (mode == LIRC_MODE_MODE2 ? (void *)&data : buffer), count);
			if (result != count) {
				fprintf(stderr, "read() failed\n");
				break;
			}
		} else {
			if (mode == LIRC_MODE_MODE2) {
				data = hw.readdata(0);
				if (data == 0) {
					fprintf(stderr, "readdata() failed\n");
					break;
				}
			} else {
				/* not implemented yet */
			}
		}

		if (mode != LIRC_MODE_MODE2) {
			printf("code: 0x");
			for (i = 0; i < count; i++) {
				printf("%02x", (unsigned char)buffer[i]);
			}
			printf("\n");
			fflush(stdout);
			continue;
		}

	
		if (!dmode) {
			uint length = (data & PULSE_MASK);
			int isShort = length >= 450 && length <= 650;
			int isLong = length >= 1500 && length <= 1750;
			int preamble = length >=5000 && length <= 8000;
			int ignore = length > 10000;
			int isPulse = (data & PULSE_BIT);
			// printf("%s:%d, ",isPulse?"P":"S",length);
			// fflush(stdout);
			// continue;
			if (ignore){
			 	fflush(stdout);
				continue;
			}
			if (isPulse){
				// printf("Pulse: %s - ",isShort?"short":"XXX");
				if (!isShort && !isLong && !preamble){

					printf("P %d ",length);
				}
				// if (isShort){
				// 	markSum+=length;
				// 	markCount++;
				// }
				if (preamble){
					// printf("pre mark: %d\n", length);
				}				
				continue;
			} else {
				if (isShort){
					sprintf(line++,"0");
					bits++;
				} else if (isLong) {
					sprintf(line++,"1");
					bits++;
				} else if (preamble){
					// printf("pre space: %d\n", length);
				}

				if (!isShort&& !isLong && !preamble){
					printf("S %d ",length);
				}
			}
			if (bits%8 == 0 && bits!=0){
				bytes++;
				sprintf(line++," ");
				// printf("bytes:%d",bytes);
				if (bytes>12){
					counter=0;
					bits=0;
					bytes=0;
					if (strlen(orgline)==strlen(prevline)){
						for (int i=0;i<strlen(orgline);i++){
							if (orgline[i]==prevline[i]){
								printf("%c",orgline[i]);
							}
							else{
								printf("%s%c%s",KRED,orgline[i],KNRM);
							}
						}
						printf("\n");
					}
					else{
					  printf("%s\n",orgline);
					}
					strcpy(prevline,orgline);
					line=orgline;
					// printf("shortMark: %f\n",(markSum/markCount));
				}
			}
			// if (counter == 0 || counter == 198){
			// 	printf("%00d\n",counter++);
			// }
			// else {
			// 	printf("%00d: %s %u\n",counter++, (data & PULSE_BIT) ? "pulse" : "space", (__u32) (data & PULSE_MASK));
			// }
		} else {
			static int bitno = 1;

			/* print output like irrecord raw config file data */
			printf(" %8u", (__u32) data & PULSE_MASK);
			++bitno;
			if (data & PULSE_BIT) {
				if ((bitno & 1) == 0) {
					/* not in expected order */
					printf("-pulse");
				}
			} else {
				if (bitno & 1) {
					/* not in expected order */
					printf("-space");
				}
				if (((data & PULSE_MASK) > 50000) || (bitno >= 6)) {
					/* real long space or more
					   than 6 codes, start new line */
					printf("\n");
					if ((data & PULSE_MASK) > 50000)
						printf("\n");
					bitno = 0;
				}
			}
		}
		fflush(stdout);
	};
	return (EXIT_SUCCESS);
}
