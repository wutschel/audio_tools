/*
cat64.c                Copyright frankl 2018

This file is part of frankl's stereo utilities.
See the file License.txt of the distribution and
http://www.gnu.org/licenses/gpl.txt for license details.

      gcc -o cat64 -O2 cat64.c cprefresh_ass.o  cprefresh.o -llibsnd -lrt
*/

#include "version.h"
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <sndfile.h>
#include "cprefresh.h"

#define LEN 100000

/* help page */
/* vim hint to remove resp. add quotes:
      s/^"\(.*\)\\n"$/\1/
      s/.*$/"\0\\n"/
*/
void usage( ) {
  fprintf(stderr,
          "cat64 (version %s of frankl's stereo utilities)\n\nUSAGE:\n",
          VERSION);
  fprintf(stderr,
"\n"
"  cat64 [options] \n"
"\n"
"  This program reads a sound file (of any format supported by 'libsndfile'),\n"
"  either as a plain file or from shared memory, and writes the audio data\n"
"  as raw 64 bit floating point samples (FLOAT64_LE) to stdout.\n"
"\n"
"  OPTIONS\n"
"  \n"
"  --file=fname, -f fname\n"
"      name of the input audio file. If not given you must use\n"
"      the next option.\n"
"\n"
"  --shmname=sname, -m sname\n"
"      name of an audio file in shared memory.\n"
"\n"
"  --start=intval, -s intval\n"
"      number of the frame to start from. Default is 0.\n"
"  \n"
"  --until=intval, -u intval\n"
"      number of frame to stop. Must be larger than start frame.\n"
"      Default is the end of the audio file.\n"
"\n"
"  --number-frames=intval, -n intval\n"
"      number of frames (from start frame) to write.\n"
"      Default is all frames until end of the audio file.\n"
"  \n"
"  --buffer-length=intval, -b intval\n"
"      the length of the buffer in number of frames. Default is 8192\n"
"      frames which should usually be fine.\n"
"\n"
"   --help, -h\n"
"      show this help.\n"
"\n"
"   --verbose, -p\n"
"      shows some information during startup and operation.\n"
"\n"
"   --version, -V\n"
"      show the version of this program and exit.\n"
"\n"
"   EXAMPLES\n"
"\n"
"       cat64 --file=data.flac > data64.raw\n"
"       cat64 --file=data.wav --start=120300 --number-frames=22500 > part.raw\n"
"\n"
"       cptoshm --file=data.flac --shmname=/pl.flac \n"
"       cat64 --shmname=/pl.flac --until=40000 > pl.raw\n"
);
}


int main(int argc, char *argv[])
{
  double buf[2*LEN], vol;
  int optc, optind, fd, blen, check, mlen, nch, verbose;
  char *fnam, *memname, *mem;
  struct stat sb;
  size_t length;
  sf_count_t start, until, total, count;
  SNDFILE *sndfile;
  SF_INFO sfinfo;

  if (argc == 1) {
      usage();
      exit(1);
  }
  /* read command line options */
  static struct option longoptions[] = {
      {"file", required_argument, 0, 'f' },
      {"shmname", required_argument, 0, 'm' },
      {"start", required_argument, 0, 's' },
      {"until", required_argument, 0, 'u' },
      {"number-frames", required_argument, 0, 'n' },
      {"volume", required_argument, 0,  'v' },
      {"buffer-length", required_argument,       0,  'b' },
      {"verbose", no_argument, 0, 'p' },
      {"version", no_argument, 0, 'V' },
      {"help", no_argument, 0, 'h' },
      {0,         0,                 0,  0 }
  };
  /* defaults */
  blen = 8192;
  fnam = NULL;
  memname = NULL;
  verbose = 0;
  start = 0;
  until = 0;
  total = 0;
  vol = 1.0;
  while ((optc = getopt_long(argc, argv, "f:i:v:b:pVh",
          longoptions, &optind)) != -1) {
      switch (optc) {
      case 'v':
        vol = atof(optarg);
        vol *= vol;
        break;
      case 's':
        start = atoi(optarg);
        break;
      case 'u':
        until = atoi(optarg);
        break;
      case 'n':
        total = atoi(optarg);
        break;
      case 'b':
        blen = atoi(optarg);
        if (blen < 1024 || blen > LEN) {
           fprintf(stderr,
                   "cat64: Buffer length must be in range %d..%d, using 8192.\n",
                   1024, LEN);
           fflush(stderr);
           blen = 8192;
        }
        break;
      case 'f':
        fnam = strdup(optarg);
        break;
      case 'm':
        memname = strdup(optarg);
        break;
      case 'p':
        verbose = 1;
        break;
      case 'V':
        fprintf(stderr, "cat64 (version %s of frankl's stereo utilities)\n",
                VERSION);
        exit(0);
      default:
        usage();
        exit(1);
      }
  }
  if (until != 0) 
      total = until-start;
  if (total < 0) {
      fprintf(stderr, "cat64: nothing to play.\n");
      exit(1);
  }

  /* open sound file */
  sfinfo.format = 0;
  if (fnam != NULL) {
      if (verbose) {
          fprintf(stderr, "cat64: opening file %s.\n", fnam);
          fflush(stderr);
      }
      sndfile = sf_open(fnam, SFM_READ, &sfinfo);
      if (sndfile == NULL) {
          fprintf(stderr, "cat64: cannot open file %s.\n", fnam);
          exit(2);
      }
  } else if (memname != NULL) {
      if (verbose) {
          fprintf(stderr, "cat64: opening shared memory as soundfile.\n");
          fflush(stderr);
      }
      if ((fd = shm_open(memname, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) == -1){
	  fprintf(stderr, "cat64: Cannot open memory %s.\n", memname);
	  exit(3);
      }
      if (fstat(fd, &sb) == -1) {
	  fprintf(stderr, "cat64: Cannot stat shared memory %s.\n", memname);
	  exit(4);
      }
      length = sb.st_size;
      mem = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      if (mem == MAP_FAILED) {
	  fprintf(stderr, "cat64: Cannot map shared memory.\n");
	  exit(6);
      }
      sndfile = sf_open_fd(fd, SFM_READ, &sfinfo, 1);
      if (sndfile == NULL) {
          fprintf(stderr, "cat64: cannot open stdin as sound file.\n"
                  "(%s)\n", sf_strerror(NULL));
          exit(7);
      }
  } else {
       fprintf(stderr, "cat64: need filename or shared memory name.\n");
       exit(8);
  }

  /* seek to start */
  if (start != 0 && sfinfo.seekable) {
      if (verbose) {
          fprintf(stderr, "cat64: seeking to frame %ld.\n", (long)start);
          fflush(stderr);
      }
      sf_seek(sndfile, start, SEEK_SET);
  }
  if (verbose && total != 0) {
      fprintf(stderr, "cat64: writing (up to) %ld frames.\n", (long)total);
      fflush(stderr);
  }
  nch = sfinfo.channels;
  count = 0;
  while (1) {
      mlen = blen;
      if (total != 0) {
          if (count >= total)
              break;
          if (count < total && total - count < blen) 
              mlen = total - count;
      }
      mlen = sf_readf_double(sndfile, buf, mlen);
      if (mlen == 0)
          break;
      refreshmem((char*)buf, nch*sizeof(double)*mlen);
      refreshmem((char*)buf, nch*sizeof(double)*mlen);
      check = fwrite((void*)buf, nch*sizeof(double), mlen, stdout);
      fflush(stdout);
      /* this should not happen, the whole block should be written */
      if (check < mlen) {
          fprintf(stderr, "cat64: Error in write.\n");
          fflush(stderr);
          exit(4);
      }
      count += mlen;
  }
  sf_close(sndfile);
  return(0);
}

