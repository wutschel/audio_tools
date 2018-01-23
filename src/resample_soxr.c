/*
resample_soxr.c                Copyright frankl 2018

This file is part of frankl's stereo utilities.
See the file License.txt of the distribution and
http://www.gnu.org/licenses/gpl.txt for license details.
Compile with
    gcc -o resample_soxr -O2 resample_soxr.c -lsoxr
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
#include <time.h>
#include <soxr.h>
#include "cprefresh.h"

#define LEN 10000

/* help page */
/* vim hint to remove resp. add quotes:
      s/^"\(.*\)\\n"$/\1/
      s/.*$/"\0\\n"/
*/
void usage( ) {
  fprintf(stderr,
          "resample_soxr (version %s of frankl's stereo utilities)\n\nUSAGE:\n",
          VERSION);
  fprintf(stderr,
"\n"
"  resample_soxr [options] \n"
"\n"
"  This command works as a resampling filter for stereo audio streams\n"
"  in raw double format (in some programs denoted FLOAT64_LE).\n"
"\n"
"  Here 'filter' means that input comes from stdin and output is\n"
"  written to stdout. Use pipes and 'sox' to deal with other stream\n"
"  formats. \n"
"\n"
"  The main options are the input sample rate and the output sample rate.\n"
"  The output volume can also be adjusted. \n"
"\n"
"  This program uses the 'soxr' standalone resampling library (see\n"
"  https://sourceforge.net/projects/soxr/) with the highest quality \n"
"  settings, all computations are done with 64-bit floating point \n"
"  numbers.\n"
"\n"
"  The computation is similar to using 'sox' with effect 'rate -v -I'.\n"
"  But 'sox' applies all effects internally to 32-bit signed integer\n"
"  samples (that is, the 64-bit input precision is lost).\n"
"\n"
"  OPTIONS\n"
"  \n"
"  --inrate=floatval, -i floatval\n"
"      the input sample rate as floating point number (must not be an \n"
"      integer). Default is 44100.\n"
"\n"
"  --outrate=floatval, -o floatval\n"
"      the output sample rate as floating point number (must not be an \n"
"      integer). Default is 192000.\n"
"\n"
"  --volume=floatval, -v floatval\n"
"      the volume as floating point factor (e.g., '0.5' for an attenuation\n"
"      of 6dB). A small attenuation (say, 0.9) can be useful to avoid \n"
"      intersample clipping. Default is 1.0.\n"
"\n"
"  --help, -h\n"
"      show this help.\n"
"\n"
"  --verbose, -p\n"
"      shows some information during startup and operation.\n"
"\n"
"  --version, -V\n"
"      show the version of this program and exit.\n"
"\n"
"   EXAMPLES\n"
"\n"
"   Convert a file 'musicfile' that can be read by 'sox' to a 96/32\n"
"   wav-file:\n"
"      ORIGRATE=`sox --i musicfile | grep \"Sample Rate\" | \\\n"
"                cut -d: -f2 | sed -e \"s/ //g\"`\n"
"      sox musicfile -t raw -e float -b 64 - | \\\n"
"          resample_soxr --inrate=$ORIGRATE --outrate=96000 --volume 0.9 | \\\n"
"          sox -t raw -e float -b 64 -c 2 -r 96000 - -e signed -b 32 out.wav\n"
"\n"
"   During room measurements I notice that the clocks in my DAC and my \n"
"   recording soundcard are slightly different. Before computing an \n"
"   impulse response I correct this with a command like:\n"
"      sox recfile.wav -t raw -e float -b 64 - | \\\n"
"          resample_soxr -i 96000 -o 95999.13487320 | \\\n"
"          sox -t raw -e float -b 64 -c 2 -r 96000 - -e signed -b 32 \\\n"
"          reccorrected.wav\n"
"\n"
);
}

int main(int argc, char *argv[])
{
  double vol, inrate, outrate, OLEN;
  double *inp, *out;
  int verbose, optc;
  long intotal = 0, outtotal = 0, mlen, check, i;
  soxr_t soxr;
  soxr_error_t error;
  size_t indone, outdone;

  if (argc == 1) {
      usage();
      exit(1);
  }
  /* read command line options */
  static struct option longoptions[] = {
      {"inrate", required_argument, 0,  'i' },
      {"outrate", required_argument,       0,  'o' },
      {"volume", required_argument, 0,  'v' },
      {"verbose", no_argument, 0, 'p' },
      {"version", no_argument, 0, 'V' },
      {"help", no_argument, 0, 'h' },
      {0,         0,                 0,  0 }
  };
  /* defaults */
  vol = 1.0;
  inrate = 44100.0;
  outrate = 192000.0;
  verbose = 0;
  while ((optc = getopt_long(argc, argv, "i:o:v:pVh",
          longoptions, &optind)) != -1) {
      switch (optc) {
      case 'v':
        vol = atof(optarg);
        break;
      case 'i':
        inrate = atof(optarg);
        break;
      case 'o':
        outrate = atof(optarg);
        break;
      case 'p':
        verbose = 1;
        break;
      case 'V':
        fprintf(stderr, "volrace (version %s of frankl's stereo utilities)\n",
                VERSION);
        exit(0);
      default:
        usage();
        exit(1);
      }
  }

  if (verbose) {
     fprintf(stderr, "resample_soxr:");
     fprintf(stderr, "vol %.3f, input rate %.3f output rate %.3f.\n", 
                     (double)vol, (double)inrate, (double)outrate);
  }

  /* allocate buffer */
  inp = (double*) malloc(2*LEN*sizeof(double));
  OLEN = (long)(LEN*(outrate/inrate+1.0));
  out = (double*) malloc(2*OLEN*sizeof(double));
  /* create resampler for 64 bit floats and high quality */
  soxr_quality_spec_t  q_spec = soxr_quality_spec(0x17, 0);
  soxr_io_spec_t const io_spec = soxr_io_spec(SOXR_FLOAT64_I,SOXR_FLOAT64_I);
  soxr_runtime_spec_t const runtime_spec = soxr_runtime_spec(1);

  soxr = soxr_create(inrate, outrate, 2, &error, 
                     &io_spec, &q_spec, &runtime_spec);

  if (error) {
    fprintf(stderr, "resample_soxr: Cannot initialize resampler.\n");
    fflush(stderr);
    exit(1);
  }
     
  /* we read from stdin until eof and write to stdout */
  while (1) {
    /* read input block */
    mlen = fread((void*)inp, 2*sizeof(double), LEN, stdin);
    if (mlen == 0) { 
      /* done */
      free(inp);
      inp = NULL;
    }
    /* call resampler */
    error = soxr_process(soxr, inp, mlen, &indone,
                               out, OLEN, &outdone);
    if (mlen > indone) {
      fprintf(stderr, "resample_soxr: only %ld/%ld processed.\n",(long)indone,(long)mlen);
      fflush(stderr);
    }
    if (error) {
      fprintf(stderr, "resample_soxr: error (%s).\n", soxr_strerror(error));
      fflush(stderr);
      exit(3);
    }
    /* apply volume change */
    if (vol != 1.0) {
      for(i=0; i<2*outdone; i++)
        out[i] *= vol;
    }
    /* write output */
    refreshmem((char*)out, 2*sizeof(double)*outdone);
    check = fwrite((void*)out, 2*sizeof(double), outdone, stdout);
    fflush(stdout);
    memclean((char*)out, 2*sizeof(double)*outdone);
    /* this should not happen, the whole block should be written */
    if (check < outdone) {
        fprintf(stderr, "resample_soxr: Error in write.\n");
        fflush(stderr);
        exit(2);
    }
    intotal += mlen;
    outtotal += outdone;
    if (mlen == 0)
      break;
  }
  soxr_delete(soxr);
  free(out);
  if (verbose) {
    fprintf(stderr, "resample_soxr: %ld input and %ld output samples\n", 
            (long)intotal, (long)outtotal);
  }
  return(0);
}

