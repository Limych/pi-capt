/*
 *  Linux Canon CAPT driver
 *  Copyright (C) 2004 Nicolas Boichat <nicolas@boichat.ch>
 * 
 *  Some functions are copied from a printer driver for Samsung ML-85G 
 *   laser printer
 *  (C) Copyleft, 2000 Rildo Pragana <rpragana@acm.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>

#include "capt.h"

#define WAIT 8000

#undef DEBUG
#ifdef DEBUG
#define DPRINTF(fmt, args...) printf(fmt, ## args)
#else
#define DPRINTF(fmt, args...)
#endif

int fd;

struct timeval lasttv;
struct timeval newtv;

//char gname[20];

void errorexit();

unsigned char cmdbuffer[8][256];

/* Rildo Pragana constants and functions, adapted to CAPT */
FILE *bitmapf = 0;
//FILE *cbmf = 0;
int cbmbuf=0; //Current buffer
unsigned char* bmptr[2];
unsigned char* bmbuf[2] = {NULL, NULL}; 		/* two pbm bitmap lines with provision for leftskip */
int bmwidth=0, bmheight=0;

unsigned char band[65536]; /* Max band size is around 592*104 = 61568 */
unsigned char* bandptr = band;
int bsize=0;

/* the compressed bitmap, separated in packets of at most 2^16-1 bytes */
int ccbm = 0;
unsigned char* cbm[100];

unsigned char garbage[600];
unsigned char *cbmp;
int csize=0;				/* compressed line size */
int linecnt=0;
int pktcnt;
int topskip=0;
int leftskip=0;

void
bitmap_seek (int offset) {
	fprintf(stderr,  "seek = %d\n", offset);
	if (offset) {
		while (offset > sizeof(garbage)) {
			fread(bmbuf[cbmbuf],1,sizeof(garbage),bitmapf);
			offset -= sizeof(garbage);
		}
		fread(bmbuf[cbmbuf],1,offset,bitmapf);
	}	
}

void
next_page(int page) {
	/* we can't use fseek here because it may come from a pipe! */
	int skip;
	skip = (bmheight - topskip - linecnt) * bmwidth;
	fprintf(stderr,  "bmheight = %d, bmwidth = %d, leftskip = %d, topskip = %d, linecnt = %d, skip = %d\n", 
		bmheight, bmwidth, leftskip, topskip, linecnt, skip);
	if (skip>0)
		bitmap_seek(skip);
	linecnt=0;
}

/* End of Rildo Pragana constants and functions */

unsigned char* get_line() {
	cbmbuf = (cbmbuf == 1) ? 0 : 1;

	memset(bmbuf[cbmbuf],0,800);

	if (linecnt<(bmheight-topskip)) {
		if (bmwidth > 800) {
			fread(bmbuf[cbmbuf],1,800,bitmapf);
			bitmap_seek(bmwidth-800);
		}
		else {
			fread(bmbuf[cbmbuf],1,bmwidth,bitmapf);
			//fprintf(stderr, " - %d:%d (%d)\n", linecnt, r, csize);
		}
	}

	bmptr[cbmbuf] = bmbuf[cbmbuf]+(leftskip/8);
	linecnt++;
	
	/*if (linecnt > 1) {
		printf("Ok, enough.\n");
		errorexit();
	}*/

	return bmptr[cbmbuf];
}

/* Return the index (relative to bmptr[X]) of the last difference between 
 * the last line and the current one, -1 if there isn't any.
 */
int last_difference() {
	int diff = -1;
	int i = 0;

	unsigned char* ptr1 = bmptr[cbmbuf];
	unsigned char* ptr2 = bmptr[(cbmbuf == 1) ? 0 : 1];

	for (; i < LINE_SIZE; i++, ptr1++, ptr2++) {
		if (*ptr1 != *ptr2)
			diff = i;
	}

	return diff;
}

void out_packet_buf(int cnt, unsigned char* c) {
	int i;

	DPRINTF("->%d :", cnt);

	for (i = 0; i < cnt; i++) {
		*(bandptr++) = c[i];
		DPRINTF(" %x", c[i]);
	}

	DPRINTF("\n");

	bsize += cnt;

	if (bsize > 65535) {
		fprintf(stderr, "Error, band too big.\n");
		errorexit();
	}
}

int out_packet(int cnt, unsigned char c1, unsigned char c2, unsigned char c3, unsigned char c4) {
	if (cnt == 0) {
		if (c1 == 1) { /* band end */
			int startnext = 0;
			if (csize+bsize > 65500) { /* 36 bytes less for security */
				out_packet(0, 0, 0, 0, 0);
			}

			csize += bsize;

			fprintf(stderr, "Band flushed: %d (%d)\n", bsize, csize);

			bandptr = band; /*(unsigned char*)&*/
			while (bsize > 0) {
				*(cbmp++) = *(bandptr++);
				bsize--;
			}
			bandptr = band;
			bsize = 0;
		}
		else if (c1 == 0) { /* flush packet */
			if (c2 == 1) { /* last packet */
				*(cbmp++) = 0x42;
			}
			cbm[ccbm][2] = csize & 0x00FF;
			cbm[ccbm][3] = (csize & 0xFF00) >> 8;

			fprintf(stderr, "Packet %d flushed: %d (%d, @%x)\n", ccbm, csize, c2, cbm[ccbm]);
			
			ccbm++;
			if (ccbm >= 99) {
				fprintf(stderr, "Too much packets to send (%d). Aborting.\n", ccbm);
				errorexit();
			}
			
			if (c2 != 1) { /* Not the last packet */
				cbm[ccbm] = malloc(65536);
				cbm[ccbm+1] = NULL;
				if (cbm[ccbm] == NULL) {
					fprintf(stderr, "Failed to allocate packet %d. Aborting.\n", ccbm);
					errorexit();
				}
				cbmp = cbm[ccbm];
				csize=4;
				*(cbmp++) = 0xa0;
				*(cbmp++) = 0xc0;
				*(cbmp++) = 0x0;
				*(cbmp++) = 0x0;
			}
		}
		return 1;
	}

	switch (cnt) {
	case 4:
		*(bandptr++) = c1;
		*(bandptr++) = c2;
		*(bandptr++) = c3;
		*(bandptr++) = c4;
		break;
	case 3:
		*(bandptr++) = c1;
		*(bandptr++) = c2;
		*(bandptr++) = c3;
		break;
	case 2:
		*(bandptr++) = c1;
		*(bandptr++) = c2;
		break;
	case 1:
		*(bandptr++) = c1;
		break;
	default:
		fprintf(stderr, "Error, wrong packet size (%d)...\n", cnt);
		errorexit();
	}

	bsize += cnt;

	DPRINTF("->%d %x %x %x %x\n", cnt, c1, c2, c3, c4);

	if (bsize > 65535) {
		fprintf(stderr, "Error, band too big...\n");
		errorexit();
	}

	return 0;
}

/* Based on Rildo Pragana's driver, adapted to CAPT protocol */
int compress_bitmap () {
	int band,diff;
	int state; //-1 - in a repetition, 0 - no rep
	int rep; // Number of repetitions
	unsigned char* cline; //current line
	int lpos; //current line position
	int cnt;					/* count of lines processed in a band */

	unsigned char c1, c2;
	unsigned char* c;	

	ccbm = 0;
	cbm[0] = malloc(65536);
	cbm[1] = NULL;
	cbmp = cbm[0];
	if (cbm[0] == NULL) {
		fprintf(stderr, "Failed to allocate packet 0. Aborting.\n");
		errorexit();
	}

	if (fgets(cbm[0],200,bitmapf)<=0) {
		return 0;
	}
	if (strncmp(cbm[0],"P4",2)) {
		fprintf(stderr,"Wrong file format.\n");
		fprintf(stderr,"file position: %lx\n",ftell(bitmapf));
		errorexit();
	}

	if (bmbuf[0] == NULL) {
		bmbuf[0] = malloc(1024);
		bmbuf[1] = malloc(1024);
	}

	/* bypass the comment line */
	do {
		fgets(cbm[0],200,bitmapf);
	} while (cbm[0][0] == '#');
	/* read the bitmap's dimensions */
	if (sscanf(cbm[0],"%d %d",&bmwidth,&bmheight)<2) {
		fprintf(stderr,"Bitmap file with wrong size fields.\n");
		errorexit();
	}
	bmwidth = (bmwidth+7)/8;
	/* adjust top and left margins */
	if (topskip) {
		/* we can't do seek from a pipe
		fseek(bitmapf,bmwidth*topskip,SEEK_CUR);*/
		bitmap_seek(bmwidth*topskip);
	}

	cline = get_line();
	diff = LINE_SIZE; /* The first line must be completly sent */

	csize=4;
	*(cbmp++) = 0xa0;
	*(cbmp++) = 0xc0;
	*(cbmp++) = 0x0;
	*(cbmp++) = 0x0;

	/*printf("COMPRESS : bmcnt %d bmwidth %d bmheight %d csize %d linecnt %d pktcnt %d topskip %d leftskip %d\n", 
			bmcnt, bmwidth, bmheight, csize, linecnt, pktcnt, topskip, leftskip);*/

	/* now we process the real data */
	for	( band=0 ; linecnt<LINES_BY_PAGE ; band++ ) {
		out_packet(1, 0x40, 0, 0, 0);
		pktcnt=0;
		/* setup packet size */
		if (((LINES_BY_PAGE-linecnt) > ROWS_BY_BAND)) {
			cnt = ROWS_BY_BAND;
		}
		else {
			cnt = LINES_BY_PAGE-linecnt;
		}
		
		fprintf(stderr,"cnt: %d, band: %d, linecnt: %d, diff : %d\n",cnt,band,linecnt,diff);

		while (1) {
			rep = 0;
			state = 0;
			lpos = 0;
			for(lpos = 0; lpos < diff; lpos++) {
				//DPRINTF("lpos : %d, state : %d\n", lpos, state);
				if ((cline[lpos] == cline[lpos+1]) && ((lpos+1) < diff)) {
					if (state == -1) {
						if (rep == 255) {
							DPRINTF("rep=%d ", rep);
							c1 = 0xa0 | (rep >> 3);
							c2 = 0x80 | (rep & 0x07) << 3;
							out_packet(3, c1, c2, cline[lpos], 0);
							rep = 0;
							state = 0;
						}
						else {
							rep++;
						}
					}
					else {
						state = -1;
						rep = 2;
					}
				}
				else { // lpos and lpos+1 different
					if (state == 0) {
						rep = 1;
						c = &(cline[lpos]);
						while (rep < 255) {
							if (lpos+rep+2 >= diff) { /* End of line */
								DPRINTF("A(%d, %d, %d)", diff, lpos, rep);
								rep += diff-(lpos+rep);
								break;
							}
							else if ((c[rep] != c[rep+1]) || (c[rep+1] != c[rep+2])) {
									//The three following are different, write 1 byte more
								rep++;
							}
							else {  //write 1 byte (0x08)
								DPRINTF("B");
								break;
							}
						}

						if (rep > 255) {
							rep = 255;
						}
						
						DPRINTF("single=%d ", rep);

						lpos += rep-1; /* lpos is incremented later */
						if (rep < 8) {
							c1 = rep << 3;
							out_packet(1, c1, 0, 0, 0);
						}
						else {
							c1 = 0xa0 | (rep >> 3);
							c2 = 0xc0 | ((rep & 0x07) << 3);
							out_packet(2, c1, c2, 0, 0);
						}
						out_packet_buf(rep, c);
						rep = 0;
					}
					else { //(state == -1)
						if (rep < 8) {
							if (((cline[lpos+1] != cline[lpos+2]) || ((lpos+2) >= diff)) && ((lpos+1) < diff)) {
									//The two following are different, take one in the packet (11)
								DPRINTF("repa+1=%d ", rep);
								c1 = 0xc1 | (rep << 3);
								out_packet(3, c1, cline[lpos], cline[lpos+1], 0);
								lpos++;
							}
							else {  //write a packet without following byte (01)
								DPRINTF("repa+0=%d ", rep);
								c1 = 0x40 | (rep << 3);
								out_packet(2, c1, cline[lpos], 0, 0);
							}
						}
						else {
							if (((cline[lpos+1] != cline[lpos+2]) || ((lpos+2) >= diff)) && ((lpos+1) < diff)) {
								if (((cline[lpos+2] != cline[lpos+3]) || ((lpos+3) >= diff)) && ((lpos+2) < diff)) {
										//The three following are different, take two in the packet
									DPRINTF("repb+2=%d ", rep);
									c1 = 0xa0 | (rep >> 3);
									c2 = 0x02 | ((rep & 0x07) << 3);
									out_packet(3, c1, c2, cline[lpos], 0);
									out_packet(2, cline[lpos+1], cline[lpos+2], 0, 0);
									lpos+=2;									
								}
								else {
										//The two following are different, take one in the packet
									DPRINTF("repb+1=%d ", rep);
									c1 = 0xa0 | (rep >> 3);
									c2 = 0x01 | ((rep & 0x07) << 3);
									out_packet(4, c1, c2, cline[lpos], cline[lpos+1]);
									lpos++;								
								}
							}
							else {  //write a packet without following byte
								DPRINTF("repb+0=%d ", rep);
								c1 = 0xa0 | (rep >> 3);
								c2 = 0x80 | (rep & 0x07) << 3;
								out_packet(3, c1, c2, cline[lpos], 0);
							}
						}
						state = 0;
					}
				}
			}

			if (diff < LINE_SIZE) {
				out_packet(1, 0x41, 0, 0, 0);
			}
			/*if (diff < (LINE_SIZE-255)) {
				out_packet(3, 0xbf, 0xb8, 0x00, 0);
			}*/
			/*out_packet(1, 0x41, 0, 0, 0);
			out_packet(1, 0x41, 0, 0, 0);
			out_packet(1, 0x41, 0, 0, 0);
			out_packet(1, 0x41, 0, 0, 0);
			out_packet(1, 0x41, 0, 0, 0);
			out_packet(1, 0x41, 0, 0, 0);
			out_packet(1, 0x41, 0, 0, 0);*/

			cline = get_line();

			//fprintf(stderr,"!cnt: %d, band: %d, linecnt: %d, diff : %d\n",cnt,band,linecnt,diff);

			if (cnt > 0) { /* There are line left */
				diff = last_difference()+1;
				//fprintf(stderr,"lastdiff: %d\n", diff);
			}
			else {
				diff = LINE_SIZE;
				//fprintf(stderr,"(END)lastdiff: %d\n", diff);
				break;
			}
			cnt--;
		} /* while */
		out_packet(1, 0x40, 0, 0, 0);
		out_packet(0,1,0,0,0); /* Flush band */
		//fprintf(stderr,"EOB\n");
	} /* for */
	//out_packet(1, 0x42, 0, 0, 0);
	out_packet(0,0,1,0,0); //Flush packet and set size
/*	fflush(cbmf);
	fseek(cbmf,0,SEEK_SET);*/
	return 1;
}

INLINE void errorexit() {
#ifdef DEBUG
   int* i = 0;
   (*i)++;
#endif
/*   unlink(gname);
   if (cbmf)
      fclose(cbmf);*/
   exit(1);
}

INLINE void ssleep(const int usec) {
   gettimeofday(&lasttv, NULL);
   while (1) {
      gettimeofday(&newtv, NULL);
      if (((newtv.tv_usec - lasttv.tv_usec) + ((newtv.tv_sec - lasttv.tv_sec)*1000000)) > usec) {
         break;
      }
   }
}

void write_command_packet_buf(unsigned char one, unsigned char two, int uwait, int nread, unsigned char* buf, int len) {
	int n, i, j;
	unsigned char buffer[256];	

	buffer[0] = one;
	buffer[1] = two;
	buffer[2] = 0x04 + len;
	buffer[3] = 0x00;

	for (i = 0; i < len; i++) {
		buffer[i+4] = buf[i];
	}

	n = write(fd, buffer, 4 + len);

	printf("w: n=%d : ", n);

	for (i = 0; i < (4 + len); i++) {
		printf("%x ", (unsigned int)buffer[i]);
	}

	printf("\n");
	
	ssleep(WAIT);

	for (j = 0; j < nread; j++) {
		memset(&(cmdbuffer[j]),0,256);
		n = read(fd, &(cmdbuffer[j]), 256);
	
		printf("r: n=%d %s: ", n, (n == -1) ? strerror(errno) : "");

/*		if ((n == -1) && (errno == EAGAIN)) {
			ssleep(2000);
			j--;
		}*/
	
		for (i = 0; i < n; i++) {
			printf("%x ", (unsigned int)cmdbuffer[j][i]);
		}
	
		printf("\n");

		ssleep(WAIT);
	}

	if (uwait > 0) {
		usleep(uwait);
	}
}

INLINE void write_command_packet(unsigned char one, unsigned char two, int uwait, int nread) {
	write_command_packet_buf(one, two, uwait, nread, NULL, 0);
}

int waitforpaper() {
	int i;
	for (i = 0; i < 120; i++) {
		write_command_packet(0xa0, 0xa0, 0, 2);
		write_command_packet(0xa1, 0xa0, 0, 2);
		//d0 00 00 02 b0 09 b3 0d 7d 00 [fd|00] 00 00 00 00 00
		
		if (cmdbuffer[1][10] == 0xfd) {
			return 1;
		}
		
		if (i == 0) {
			fprintf(stderr, "Waiting for paper...\n");
		}
		usleep(1000000);
	}
	return 0;
}

int waitforready() {
/*	int i = 0;

	write_command_packet(0xa0, 0xa0, 0, 2);

	while (1) { //(cmdbuffer[0][5] & 0x02) != 0x02) {
		ssleep(100000);
		write_command_packet(0xa0, 0xe0, 0, 1);
		i++;
		if (i > 500) {
			return 1;
		}
	}*/
	return 1;
}

int print_page(int page) {
	if (page == 0) {
		write_command_packet(0xa1, 0xa1, 0, 2);

		if ((cmdbuffer[0][0] != 0xa1) || (cmdbuffer[0][1] != 0xa1)) {
			fprintf(stderr, "Invalid printer state, printer not connected ?\n");
			return 0;		
		}
	}

	if (!waitforpaper()) {
		fprintf(stderr, "Timeout out while waiting for paper.\n");
		return 0;
	}

	if (page == 0) { /* First page initialization */
		write_command_packet(0xa0, 0xa2, 0, 1);
		write_command_packet(0xa0, 0xe0, 0, 1);
		write_command_packet(0xa1, 0xa0, 0, 2);
		write_command_packet(0xa4, 0xe0, 0, 1);

		{
			unsigned char buf[] = {0xee, 0xdb, 0xea, 0xad, 0x00, 0x00, 0x00, 0x00};
			write_command_packet_buf(0xa5, 0xe0, 0, 1, (unsigned char*)&buf, 8);
		}

		write_command_packet(0xa0, 0xe0, 0, 1);
		write_command_packet(0xa0, 0xa0, 0, 2);
		write_command_packet(0xa1, 0xa0, 0, 2);
		write_command_packet(0xa0, 0xe0, 0, 1);
	}
	
	{
		unsigned char buf[] = {
			0x00, 0x00, 0xa4, 0x01, 0x02, 0x01, 0x00, 0x00, 0x1f, 0x1f, 
			0x1f, 0x1f,	0x00, 0x11, 0x03, 0x01, 0x01, 0x01, 0x02, 0x00, 
			0x00, 0x00, 0x70, 0x00, 0x78, 0x00, 0x50, 0x02, 0x7a, 0x1a, 
			0x60, 0x13, 0x67, 0x1b};
		write_command_packet_buf(0xa0, 0xd0, 0, 0, (unsigned char*)&buf, 34);
	}

	write_command_packet(0xa0, 0xe0, 0, 1);
	write_command_packet(0xa1, 0xd0, 0, 0);
	write_command_packet(0xa0, 0xe0, 0, 1);
	write_command_packet(0xa0, 0xa0, 0, 2);
	write_command_packet(0xa1, 0xa0, 0, 2);

	unsigned int size = 0;
	int i, w, c;
	
	for (ccbm = 0; cbm[ccbm] != NULL; ccbm++) {
		write_command_packet(0xa0, 0xe0, 0, 1);

		while (((cmdbuffer[0][4] & 0x08) == 0x08) || (cmdbuffer[0][0] == 0x00)) {
			ssleep(100000);
			write_command_packet(0xa0, 0xe0, 0, 1);
		}

		ssleep(2*WAIT);

		/*printf("First ten bytes: %x %x %x %x %x %x %x %x %x %x\n", 
			cbm[ccbm][0], cbm[ccbm][1], cbm[ccbm][2], cbm[ccbm][3], cbm[ccbm][4],
			cbm[ccbm][5], cbm[ccbm][6], cbm[ccbm][7], cbm[ccbm][8], cbm[ccbm][9]);*/

		size = (((unsigned int)cbm[ccbm][3]) << 8) + (unsigned int)cbm[ccbm][2];
		printf("size: %d, index: %d\n", size, ccbm);
		for (i = 0; (i+4096) < size; i += 4096) {
			c = 0;
			do {
				w = write(fd, &(cbm[ccbm][i]), 4096);
				printf("w: n=%d/%d %s (i=%d/%d)\n", w, 4096, (w == -1) ? strerror(errno) : "", i, size);
				ssleep(2*WAIT);
				if (c > 100) {
					fprintf(stderr, "Error while sending data, timeout.\n");
					return 0;
				}
				c++;
			} while (w == -1);
		}

		c = 0;
		do {
			w = write(fd, &(cbm[ccbm][i]), size-i);
			printf("w: n=%d/%d %s (i=%d/%d)\n", w, size-i, (w == -1) ? strerror(errno) : "", i, size);
			ssleep(2*WAIT);
			if (c > 100) {
				fprintf(stderr, "Error while sending data, timeout.\n");
				return 0;
			}
			c++;
		} while (w == -1);

		/*printf("Last ten bytes: %x %x %x %x %x %x %x %x %x %x\n", 
			cbm[ccbm][size-10], cbm[ccbm][size-9], cbm[ccbm][size-8], 
			cbm[ccbm][size-7], cbm[ccbm][size-6], cbm[ccbm][size-5], 
			cbm[ccbm][size-4], cbm[ccbm][size-3], cbm[ccbm][size-2],
			cbm[ccbm][size-1]);*/

		free(cbm[ccbm]);
		cbm[ccbm] = NULL;

		ssleep(5*WAIT);
	}

	write_command_packet(0xa0, 0xe0, 0, 1);
	write_command_packet(0xa2, 0xd0, 0, 0);
	write_command_packet(0xa0, 0xe0, 0, 1);
	write_command_packet(0xa1, 0xa0, 0, 2);

	return waitforready();

	//idle(5);

	//return 1;
}

int main(int argc, char** argv) {
	int c;
	//int reset_flag=1;
	//int size;
	int simulate=0;
	int page=0;
	//int reset_only=0;
	//int reset=0;
	//int tfd;

	bitmapf = stdin;
	while ((c = getopt(argc,argv,"Rrt:l:sf:")) != -1) {
		switch (c) {
/*			case 'R': {
				reset_only=1;
				reset=1;
				break;
			}
			case 'r': {
				reset=1;
				break;
			}*/
			case 't': {
				sscanf(optarg,"%d",&topskip);
				break;
			}
			case 'l': {
				sscanf(optarg,"%d",&leftskip);
				break;
			}
			case 's': {
				simulate++;
				break;
			}
			case 'f': {
				bitmapf = fopen( optarg,"r" );
				if (!bitmapf) {
					fprintf(stderr,"File not found on unreadable\n");
					errorexit();
				}
			}
		}
	}

	fd = open("/dev/usb/lp0", O_RDWR | O_NONBLOCK);
	//fd = open("/dev/lp0", O_RDWR);

//	if (!reset_only) {
		/* pages printing loop */
/*      struct timeval ltv;
      struct timeval ntv;*/
      
		while (1) {
			/* temporary file to store our results */
/*			strcpy(gname,"/tmp/lbp810-XXXXXX");
			if ((tfd=mkstemp(gname))<0) {
				fprintf(stderr,"Can't open a temporary file.\n");
				errorexit();
			}
			cbmf = fdopen(tfd,"w+");*/
			if (!compress_bitmap())
				break;
			if (!simulate) {
				if (!print_page( page )) {
				   fprintf(stderr, "Error, cannot print this page.\n");
				   errorexit();
	            }
	            //gettimeofday(&ltv, NULL);
			}
//			fclose(cbmf);
			next_page(page++);
//			unlink(gname);
		}
//	}

   fclose(bitmapf);

	close(fd);

	return 0;
}

