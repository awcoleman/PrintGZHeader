/*
* Outputs header information ( name and mtime) for each gz stream in file.
*   Exits on corrupt zstream. No substantial error checking.
*
* gcc -o printGZHeader printGZHeader.c -lz
*
* Reference material:
* http://www.zlib.net/manual.html
* https://www.ietf.org/rfc/rfc1952.txt
*
* initial author awcoleman, last update 20150110 awcoleman
*
* Copyright 2015 awcoleman
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/* mutliplier for output buffer size (outbug size = MULT_OUTBUF * input_size) */
#define OUTBUF_MULT 5
/* size of strings such as comments and original filename */
#define STR_LEN 512

int get_header_for_zstream(FILE *f, char* inname, int member_number,long filesize) {

        int err;
        char *inbuf, *outbuf;

	gz_header zhead;
	z_stream zs;
	int ret;
	char zname[STR_LEN];

	struct tm * mtimeinfo;
	time_t mtime;

	int fposreset=0;

	/* init */
	inbuf = malloc(filesize * sizeof(char));
	outbuf = malloc(OUTBUF_MULT*filesize * sizeof(char));

	/* zlib will change to default allocators, etc */
	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;
	zs.opaque = Z_NULL;

	/* for now leave input buffer and available input size null/0 */
	zs.next_in = Z_NULL;
	zs.avail_in = 0;

	/* Set output buffer */
	zs.next_out = outbuf;
	zs.avail_out = OUTBUF_MULT*filesize;

	/* initialize zlib, only accept gz streams */
	ret = inflateInit2(&zs, 16+MAX_WBITS);
	if (ret != Z_OK) {
		fprintf(stderr, "inflateInit2 returned error: %d, msg: %s\n",ret,zs.msg);
		exit(ret);
	}

	/* tell zlib to populate header structure when it can. 
           zhead.done will be 0 until structure is populated, 1 if populated, and -1 if unable to populate */
	ret = inflateGetHeader(&zs, &zhead);
	if (ret != Z_OK) {
		fprintf(stderr, "inflateGetHeader returned error: %d, msg: %s\n",ret,zs.msg);
		exit(ret);
	}

	/* Tell zlib to populate N-1 characters in the string field name, and 0 for comment,extra */
	zhead.name_max=sizeof(zname)-1;
	zhead.comm_max=0;
	zhead.extra_max=0;
	zhead.name=(Bytef*) &zname;

	/* Read some of the gz file so zlib can get the header */
        zs.avail_in = fread(inbuf, 1, filesize, f);
        if (ferror(f)) {
            ret = Z_ERRNO;
	    fprintf(stderr, "ERROR: Error reading file %s\n", inname);
	    exit(Z_ERRNO);
        }
        if (zs.avail_in == 0) {
	    fprintf(stderr, "ERROR: Issue reading file (no data) %s\n", inname);
	    exit(Z_DATA_ERROR);
        }
        zs.next_in = inbuf;

	/* inflate with Z_BLOCK right now will just populate header structure and return */
	ret = inflate(&zs, Z_BLOCK);
	if (ret != Z_OK) {
		fprintf(stderr, "inflate returned error: %d, msg: %s\n",ret,zs.msg);
		exit(ret);
	}

	/* file position has moved to past header */
	fposreset = filesize - zs.avail_in;

	/* zhead should be populated now */

        printf("GZHeader for member %d of file %s\n",member_number,inname);

	/* gz_header.time */
	mtime = (time_t)zhead.time;
	mtimeinfo = localtime(&mtime);
	char * ztime_descrip = asctime(mtimeinfo);
	ztime_descrip[strlen(ztime_descrip)-1] = '\0';
        printf("GZHeader Time Field is: %d (%s)\n",zhead.time,ztime_descrip);

	/* gz_header.name */
        printf("GZHeader Name Field is: %s\n",zhead.name);

	/* gz_header.done */
	char* zdone_descrip=  "Header is undefined. ";
	if (zhead.done==0) {
		zdone_descrip="Header is incomplete.";
	} else if (zhead.done==1) {
                zdone_descrip="Header is complete.";
	} else if (zhead.done==2) {
                zdone_descrip="Header is unavailable.";
        } else {
                zdone_descrip="Undefined";
	}
        printf("GZHeader Done Field is: %d (%s)\n",zhead.done,zdone_descrip);

	/* finish current inbuf, and grab new ones until end of zstream */
	do {
		ret = inflate(&zs, Z_NO_FLUSH);
		if (ret < 0) {
			fprintf(stderr, "inflate returned error: %d, msg: %s\n",ret,zs.msg);
			exit(ret);
		}
		zs.avail_in = fread(inbuf, 1, filesize, f);
		if (ferror(f)) {
	    		fprintf(stderr, "ERROR: Error reading file %s\n", inname);
	    		exit(Z_ERRNO);
		}
		if (zs.avail_in == 0)
			break;
		zs.next_in = inbuf;

		/* reset outbuf since we don't care about its contents */
		zs.next_out = outbuf;
		zs.avail_out = OUTBUF_MULT*filesize;

	} while (ret != Z_STREAM_END);

        printf("Decompressed size of member is: %d\n",zs.total_out);

	/* clean up */
	(void)inflateEnd(&zs);
	free(inbuf);
	free(outbuf);
	fposreset=zs.total_in;

        return fposreset;
}

int main(int argc, char **argv) {

        FILE *f;
        int err;

	char *inname;

	gz_header zhead;
	z_stream zs;
	int ret;
	char zname[STR_LEN];

	struct tm * mtimeinfo;
	time_t mtime;

	struct stat filestat;
	long filesize;
	long fposa;
	long fposb;

	inname = argv[1];
        f = fopen(inname,"r");
	if (f == NULL) {
		fprintf(stderr, "ERROR: Unable to open file %s\n", inname);
		exit(1);
	}
	int fd = fileno(f);
	fstat(fd, &filestat);
	filesize = filestat.st_size;

	fposa = ftell(f);
	fposb = fposa;

	int loopctr=1;
	while ( !feof(f) && fposb!=filesize ) {
		fposa = ftell(f);
        	printf("----------------\n");
        	printf("Byte position (0-based) of beginning of member %d is %d.\n",loopctr, ftell(f));
		ret = get_header_for_zstream(f, inname, loopctr, filesize);
		fseek(f,(fposa+ret),SEEK_SET);
		fposb = ftell(f);
        	printf("Byte position (0-based) of end of member %d is %d.\n",loopctr, (ftell(f)-1));
        	printf("----------------\n");
		loopctr++;
	}

	fclose(f);

        return ret;
}
