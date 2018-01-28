/*
resample_soxr.c                Copyright frankl 2018

This file is part of frankl's stereo utilities.
See the file License.txt of the distribution and
http://www.gnu.org/licenses/gpl.txt for license details.
Compile with
    gcc -o resample_soxr -O2 resample_soxr.c -lsoxr -lsndfile -lrt
*/

#include "version.h"
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <sndfile.h>
#include <soxr.h>
#include "cprefresh.h"

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
"  By default this command works as a resampling filter for stereo audio \n"
"  streams in raw double format (in some programs denoted FLOAT64_LE).\n"
"\n"
"  Here 'filter' means that input comes from stdin and output is\n"
"  written to stdout. Use pipes and 'sox' or other programs to deal with \n"
"  other stream formats. \n"
"\n"
"  Alternatively, this program can use as input any sound file that can be \n"
"  read with 'libsndfile' (flac, wav, ogg, aiff, ...). This  can be \n"
"  a file in the file system or in shared memory (see '--file' and\n"
"  '--shmname' options). In this case it is also possible to read only a \n"
"  part of the file.\n"
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
"      integer). Default is 44100. In case of file input this value is\n"
"      overwritten by the sampling rate specified in the file (so, this\n"
"      option is not needed).\n"
"\n"
"  --outrate=floatval, -o floatval\n"
"      the output sample rate as floating point number (must not be an \n"
"      integer). Default is 192000.\n"
"\n"
"  --volume=floatval, -v floatval\n"
"      the volume as floating point factor (e.g., '0.5' for an attenuation\n"
"      of 6dB). A small attenuation (say, 0.9) can be useful to avoid \n"
"      intersample clipping. Default is 1.0, that is no volume change.\n"
"\n"
"  --channels=intval, -c intval\n"
"      number of interleaved channels in the input. Default is 2 (stereo).\n"
"      In case of input from a file this number is overwritten by the \n"
"      the number of channels in the file.\n"
"\n"
"  --buffer-length=intval, -b intval\n"
"      the size of the input buffer in number of frames. The default is\n"
"      8192 and should usually be fine.\n"
"\n"
"  --phase=floatval, -P floatval\n"
"      the phase response of the filter used during resampling; see the \n"
"      documentation of the 'rate' effect in 'sox' for more details. This\n"
"      is a number from 0 (minimum phase) to 100 (maximal phase), with \n"
"      50 (linear phase) and 25 (intermediate phase). The default is 25,\n"
"      and should usually be fine.\n"
"\n"
"  --file=fname, -f fname\n"
"      name of an input audio file (flac, wav, aiff, ...). The default\n"
"      is input from stdin.\n"
"\n"
"  --shmname=sname, -i sname\n"
"      name of an audio file in shared memory. The default\n"
"      is input from stdin.\n"
"\n"
"The follwing three option allow to read only part of the input, but this is\n"
"only possible for input from file or shared memory.\n"
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
"   wav-file using a pipe:\n"
"      ORIGRATE=`sox --i musicfile | grep \"Sample Rate\" | \\\n"
"                cut -d: -f2 | sed -e \"s/ //g\"`\n"
"      sox musicfile -t raw -e float -b 64 - | \\\n"
"          resample_soxr --inrate=$ORIGRATE --outrate=96000 --volume=0.9 | \\\n"
"          sox -t raw -e float -b 64 -c 2 -r 96000 - -e signed -b 32 out.wav\n"
"\n"
"   If 'resample_soxr' can read 'musicfile' this can also be achieved by:\n"
"      resample_soxr --file=musicfile --outrate=96000 --volume=0.9 | \\\n"
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
  /* variables for the resampler */
  double vol, inrate, outrate, phase, OLEN;
  double *inp, *out;
  int verbose, optc, fd;
  long intotal = 0, outtotal = 0, blen, mlen, check, i, nch;
  soxr_t soxr;
  soxr_error_t error;
  size_t indone, outdone;
  /* variables for optional input from sound file/shared memory */ 
  char *fnam, *memname, *mem;
  struct stat sb;
  size_t length;
  sf_count_t start, until, total;
  SNDFILE *sndfile=NULL;
  SF_INFO sfinfo;

  if (argc == 1) {
      usage();
      exit(1);
  }
  /* read command line options */
  static struct option longoptions[] = {
      {"inrate", required_argument, 0, 'i' },
      {"outrate", required_argument, 0, 'o' },
      {"phase", required_argument, 0, 'P' },
      {"volume", required_argument, 0, 'v' },
      {"channels", required_argument, 0, 'c' },
      {"file", required_argument, 0, 'f' },
      {"shmname", required_argument, 0, 'm' },
      {"start", required_argument, 0, 's' },
      {"until", required_argument, 0, 'u' },
      {"number-frames", required_argument, 0, 'n' },
      {"buffer-length", required_argument, 0, 'b' },
      {"verbose", no_argument, 0, 'p' },
      {"version", no_argument, 0, 'V' },
      {"help", no_argument, 0, 'h' },
      {0,         0,        0,   0 }
  };
  /* defaults */
  vol = 1.0;
  inrate = 44100.0;
  outrate = 192000.0;
  phase = 25.0; 
  nch = 2;
  blen = 8192;
  fnam = NULL;
  memname = NULL;
  start = 0;
  until = 0;
  total = 0;
  verbose = 0;
  while ((optc = getopt_long(argc, argv, "i:o:v:s:u:n:b:f:m:pVh",
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
      case 'P':
        phase = atof(optarg);
        if (phase < 0.0 || phase > 100.0)
           phase = 25.0;
        break;
      case 'c':
        nch = atoi(optarg);
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
        fprintf(stderr, "resample_soxr (version %s of frankl's stereo utilities)\n",
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
      fprintf(stderr, "resample_soxr: nothing to read.\n");
      exit(1);
  }

  /* open sound file if fnam or memname given */
  sfinfo.format = 0;
  if (fnam != NULL) {
      if (verbose) {
          fprintf(stderr, "resample_soxr: opening file %s.\n", fnam);
          fflush(stderr);
      }
      sndfile = sf_open(fnam, SFM_READ, &sfinfo);
      if (sndfile == NULL) {
          fprintf(stderr, "resample_soxr: cannot open file %s.\n", fnam);
          exit(2);
      }
  } else if (memname != NULL) {
      if (verbose) {
          fprintf(stderr, "resample_soxr: opening shared memory as soundfile.\n");
          fflush(stderr);
      }
      if ((fd = shm_open(memname, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) == -1){
	  fprintf(stderr, "resample_soxr: Cannot open memory %s.\n", memname);
	  exit(3);
      }
      if (fstat(fd, &sb) == -1) {
	  fprintf(stderr, "resample_soxr: Cannot stat shared memory %s.\n", memname);
	  exit(4);
      }
      length = sb.st_size;
      mem = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      if (mem == MAP_FAILED) {
	  fprintf(stderr, "resample_soxr: Cannot map shared memory.\n");
	  exit(6);
      }
      sndfile = sf_open_fd(fd, SFM_READ, &sfinfo, 1);
      if (sndfile == NULL) {
          fprintf(stderr, "resample_soxr: cannot open stdin as sound file.\n"
                  "(%s)\n", sf_strerror(NULL));
          exit(7);
      }
  }  
  if (sndfile) { 
      /* seek to start */
      if (start != 0 && sfinfo.seekable) {
          if (verbose) {
              fprintf(stderr, "resample_soxr: seeking to frame %ld.\n", (long)start);
              fflush(stderr);
          }
          sf_seek(sndfile, start, SEEK_SET);
      }
      if (verbose && total != 0) {
          fprintf(stderr, "resample_soxr: writing (up to) %ld frames.\n", (long)total);
          fflush(stderr);
      }
      nch = sfinfo.channels;
      inrate = (double)(sfinfo.samplerate);
  } else {
      /* no seeking in stdin, ignore those arguments */
      total = 0;
  }

  if (verbose) {
     fprintf(stderr, "resample_soxr: ");
     fprintf(stderr, "vol %.3f, input rate %.3f output rate %.3f.\n", 
                     (double)vol, (double)inrate, (double)outrate);
  }

  /* allocate buffer */
  inp = (double*) malloc(nch*blen*sizeof(double));
  OLEN = (long)(blen*(outrate/inrate+1.0));
  out = (double*) malloc(nch*OLEN*sizeof(double));
  /* create resampler for 64 bit floats and high quality */
  soxr_quality_spec_t  q_spec = soxr_quality_spec(0x17, 0);
  q_spec.phase_response = phase;
  q_spec.precision = 33.0;
  if (verbose) {
      fprintf(stderr, "resample_soxr: resampling with quality %.3f and phase %.3f\n", 
              q_spec.precision, q_spec.phase_response);
      fflush(stderr);
  }
  soxr_io_spec_t const io_spec = soxr_io_spec(SOXR_FLOAT64_I,SOXR_FLOAT64_I);
  soxr_runtime_spec_t const runtime_spec = soxr_runtime_spec(1);

  /* now we can create the resampler */
  soxr = soxr_create(inrate, outrate, nch, &error, 
                     &io_spec, &q_spec, &runtime_spec);
  if (error) {
    fprintf(stderr, "resample_soxr: Cannot initialize resampler.\n");
    fflush(stderr);
    exit(1);
  }
     
  /* we read from stdin or file/shared mem until eof and write to stdout */
  while (1) {
    mlen = blen;
    if (total != 0) {
        if (intotal >= total)
            mlen = 0;
        if (intotal < total && total - intotal < blen) 
            mlen = total - intotal;
    }
    /* read input block */
    memclean((char*)inp, nch*sizeof(double)*mlen);
    if (sndfile) 
        mlen = sf_readf_double(sndfile, inp, mlen);
    else
        mlen = fread((void*)inp, nch*sizeof(double), mlen, stdin);
    if (mlen == 0) { 
      /* done */
      free(inp);
      inp = NULL;
    }
    /* call resampler */
    refreshmem((char*)inp, nch*sizeof(double)*mlen);
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
    refreshmem((char*)out, nch*sizeof(double)*outdone);
    check = fwrite((void*)out, nch*sizeof(double), outdone, stdout);
    fflush(stdout);
    memclean((char*)out, nch*sizeof(double)*outdone);
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

