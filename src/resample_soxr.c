/*
resample_soxr.c                    Copyright frankl 2018
                         Copyright Andree Buschmann 2020

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
#include <soxr.h>

/* help page */
/* vim hint to remove resp. add quotes:
      s/^"\(.*\)\\n"$/\1/
      s/.*$/"\0\\n"/
*/
void usage( ) {
    fprintf(stderr, "resample_soxr (version %s of frankl's stereo utilities", VERSION);
    fprintf(stderr, ", linked against libsoxr %s", SOXR_THIS_VERSION_STR);
    fprintf(stderr, ", reworked by Andree Buschmann");
    fprintf(stderr, ")\n\nUSAGE:\n");
    fprintf(stderr,
"\n"
"  resample_soxr [options] \n"
"\n"
"  By default this program works as a resampling filter for stereo audio \n"
"  streams in raw double format (in some programs denoted FLOAT64_LE).\n"
"  Here 'filter' means that input comes from stdin and output is\n"
"  written to stdout. Use pipes and 'sox' or other programs to deal with \n"
"  other stream formats. The main options are the input sample rate,\n"
"  the output sample rate and parameters for the resampler itself.\n"
"\n"
"  This program uses the 'soxr' standalone resampling library (see\n"
"  https://sourceforge.net/projects/soxr/) with the highest quality \n"
"  settings, all computations are done with 64-bit floating point \n"
"  numbers.\n"
"\n"
"  The computation is similar to using 'sox' with effect 'rate -v'.\n"
"  But 'sox' applies all effects internally to 32-bit signed integer\n"
"  samples (that is, the 64-bit input precision is lost).\n"
"\n"
"  OPTIONS\n"
"  \n"
"  --inrate=floatval, -i floatval\n"
"      the input sample rate in Hz as floating point number (must not be an \n"
"      integer). Default is 44100.\n"
"\n"
"  --outrate=floatval, -o floatval\n"
"      the output sample rate in Hz as floating point number (must not be an \n"
"      integer). Default is 192000.\n"
"\n"
"  --channels=intval, -c intval\n"
"      number of interleaved channels in the input. Default is 2 (stereo).\n"
"\n"
"  --phase=floatval, -p floatval\n"
"      the phase response of the filter used during resampling; see the \n"
"      documentation of the 'rate' effect in 'sox' for more details.\n"
"      0  = minimum phase (minimum pre-ringing, frequency dependent delay)\n"
"      25 = intermediate phase (trade-off between minimum/linear phase)\n"
"      50 = linear phase (constant group delay, longest pre-ringing)\n"
"      In general all values in the range 0..100 are possible. Nevertheless\n"
"      values >50 are not useful as per 'sox' documentation. Default is 25.\n"
"\n"
"  --bandwidth=floatval, -b floatval\n"
"      the band-width of the filter used during resampling; see the \n"
"      documentation of the rate effect in 'sox' for more details. The value\n"
"      is given as percentage (of the Nyquist frequency of the smaller \n"
"      sampling rate). The allowed range is 74.0..99.7, the default is 91.09\n"
"      (this filter is flat up to about 20kHz at 44.1kHz sampling rate).\n"
"\n"
"  --precision=floatval, -e floatval\n"
"      the bit precision for resampling, which is equivalent to the rejection\n"
"      of the resampling filter; higher values cause higher CPU usage. The\n"
"      valid range is 16.0..33.0, the default is 33.0 and should usually be\n"
"      fine (except lower CPU usage is essential).\n"
"\n"
"  --rolloff=intval, -R intval\n"
"      the rolloff for the resampling filter; Valid range is 0..2, default 0.\n"
"      According to 'soxr' documentation:\n"
"      0 = SOXR_ROLLOFF_SMALL  (<= 0.01 dB), which is default,\n"
"      1 = SOXR_ROLLOFF_MEDIUM (<= 0.35 dB),\n"
"      2 = SOXR_ROLLOFF_NONE   (for Chebyshev bandwidth).\n"
"\n"
"  --aliasing, -a\n"
"      allow aliasing in the resampling filter; This is switched off by default\n"
"      and not recommended, but it can be enabled to create short filters.\n"
"      Enabling aliasing sets stopband begin to fs x (2 - bandwidth), example:\n"
"      bandwidth = 0.91 results in stopband = 1.09.\n"
"\n"
"  --buffer-size=intval, -B intval\n"
"      the size of the input buffer in number of frames. The minimum value is\n"
"      1024. The default is 8192 and should usually be fine.\n"
"\n"
"  --help, -h\n"
"      show this help.\n"
"\n"
"  --verbose, -v\n"
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
"          resample_soxr --inrate=$ORIGRATE --outrate=96000 --precision=28 | \\\n"
"          sox -t raw -e float -b 64 -c 2 -r 96000 - -e signed -b 32 out.wav\n"
"\n"
);
}

int main(int argc, char *argv[])
{
    /* variables for the resampler */
    double inrate, outrate, phase, bwidth, prec;
    double *iptr, *optr;
    int verbose, optc, rolloff, aliasing;
    long itotal, ototal, blen, mlen, check, nch, olen;
    size_t indone, outdone;
    /* variables for soxr */
    int qspecflags, qspecrecipe;
    soxr_quality_spec_t q_spec;
    soxr_io_spec_t io_spec;
    soxr_runtime_spec_t runtime_spec;
    soxr_t soxr;
    soxr_error_t error;

    /**********************************************************************/
    /* read and set parameters                                            */
    /**********************************************************************/
    
    /* no parameter given */
    if (argc == 1) {
        usage();
        exit(1);
    }

    /* read command line options */
    static struct option longoptions[] = {
        {"inrate",      required_argument, 0, 'i' },
        {"outrate",     required_argument, 0, 'o' },
        {"phase",       required_argument, 0, 'p' },
        {"bandwidth",   required_argument, 0, 'b' },
        {"precision",   required_argument, 0, 'e' },
        {"channels",    required_argument, 0, 'c' },
        {"buffer-size", required_argument, 0, 'B' },
        {"rolloff",     required_argument, 0, 'R' },
        {"aliasing",    no_argument,       0, 'a' },
        {"verbose",     no_argument,       0, 'v' },
        {"version",     no_argument,       0, 'V' },
        {"help",        no_argument,       0, 'h' },
        {0,             0,                 0,  0  }
    };

    /* defaults */
    inrate = 44100.0;
    outrate = 192000.0;
    phase = 25.0; 
    bwidth = 0.0;
    rolloff = SOXR_ROLLOFF_SMALL;
    aliasing = 0;
    prec = 33.0;
    nch = 2;
    blen = 8192;
    verbose = 0;
    itotal = 0;
    ototal = 0;
    
    /* read parameters */
    while ((optc = getopt_long(argc, argv, "i:o:p:b:e:c:B:R:avVh", longoptions, &optind)) != -1) {
        switch (optc) {
        case 'i':
            inrate = atof(optarg);
            break;
        case 'o':
            outrate = atof(optarg);
            break;
        case 'p':
            phase = atof(optarg);
            if (phase < 0.0 || phase > 100.0)
                phase = 25.0;
            break;
        case 'b':
            bwidth = atof(optarg);
            if (bwidth < 74.0 || bwidth > 99.7)
                bwidth = 0.0;
            break;
        case 'e':
            prec = atof(optarg);
            if (prec < 16.0 || prec > 33.0)
                prec = 33.0;
            break;
        case 'c':
            nch = atoi(optarg);
            break;
        case 'B':
            blen = atoi(optarg);
            if (blen < 1024)
                blen = 8192;
            break;
        case 'R':
            rolloff = atoi(optarg);
            if (rolloff < 0 || rolloff > 2)
                rolloff = SOXR_ROLLOFF_SMALL;
            break;
        case 'a':
            aliasing = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'V':
            fprintf(stderr, "resample_soxr (version %s of frankl's stereo utilities", VERSION);
            fprintf(stderr, ", linked against libsoxr %s", SOXR_THIS_VERSION_STR);
            fprintf(stderr, ", reworked by Andree Buschmann)\n");
            exit(2);
        default:
            usage();
            exit(3);
        }
    }

    /**********************************************************************/
    /* allocate buffers                                                   */
    /**********************************************************************/

    /* allocate input buffer */
    iptr = (double*) malloc(nch*blen*sizeof(double));

    /* allocate output buffer */
    olen = (long)(blen*(outrate/inrate+1.0));
    optr = (double*) malloc(nch*olen*sizeof(double));

    /**********************************************************************/
    /* create soxr resampler, for parameters see                          */
    /* https://sourceforge.net/p/soxr/code/ci/master/tree/src/soxr.h      */
    /**********************************************************************/

    /* use chosen rolloff and high precision for irrational ratios */
    qspecflags = rolloff | SOXR_HI_PREC_CLOCK; 

    /* intermediate phase (will later be overwritten by chosen value for phase) */
    /* precision 32 (will later be overwritten by chosen value for precision)   */ 
    qspecrecipe = SOXR_MINIMUM_PHASE | SOXR_32_BITQ;

    /* create qspec, overwrite phase/prec/passband/stopband with chosen values   */
    /* Remark: for soxr <=0.1.1 we *MUST* overwrite phase with the desired value */
    q_spec = soxr_quality_spec(qspecrecipe, qspecflags);
    q_spec.precision      = prec;
    q_spec.phase_response = phase;
    q_spec.passband_end   = (bwidth != 0.0) ? bwidth/100.0 : q_spec.passband_end;
    q_spec.stopband_begin = (aliasing == 1) ? (2.0-q_spec.passband_end) : 1.0;

    /* set io_spec for FLOAT64 precision */
    io_spec = soxr_io_spec(SOXR_FLOAT64_I,SOXR_FLOAT64_I);

    /* set runtime spec for 1 thread */
    runtime_spec = soxr_runtime_spec(1);

    /* now we can create the resampler */
    soxr = soxr_create(inrate, outrate, nch, &error, &io_spec, &q_spec, &runtime_spec);
    if (error) {
        fprintf(stderr, "resample_soxr: Error initializing soxr resampler.\n");
        fflush(stderr);
        exit(4);
    }

    /**********************************************************************/
    /* show resample_soxr configuration                                   */
    /**********************************************************************/
  
    if (verbose) {
        fprintf(stderr, "resample_soxr: Version %s\n", VERSION);
        fprintf(stderr, "resample_soxr: buffer size (in/out) %ld/%ld samples\n", 
                blen, olen);
        fprintf(stderr, "resample_soxr: input rate %.1f Hz, output rate %.1f Hz\n", 
                inrate, outrate);
        fprintf(stderr, "resample_soxr: phase %.0f, precision %.0f (rejection %.0f dB)\n", 
                q_spec.phase_response, q_spec.precision, q_spec.precision*6.02);
        fprintf(stderr, "resample_soxr: passband %.4f, stopband %.4f %s\n", 
                q_spec.passband_end, q_spec.stopband_begin, (aliasing)?"(Aliasing!)":"");
        fprintf(stderr, "resample_soxr: rolloff 0x%x, SOXR_HI_PREC_CLOCK\n",
                rolloff);
    }

    /**********************************************************************/
    /* main loop                                                          */
    /**********************************************************************/
     
    /* we read from stdin until eof and write to stdout */
    while (1) {

        /* clean buffers */
        memset(iptr, 0, nch*blen*sizeof(double));
        memset(optr, 0, nch*olen*sizeof(double));

        /**********************************************************************/
        /* read data                                                          */
        /**********************************************************************/

        /* read input block */
        mlen = fread((void*)iptr, nch*sizeof(double), blen, stdin);

        /**********************************************************************/
        /* resample data                                                      */
        /**********************************************************************/
    
        /* call resampler */
        error = soxr_process(soxr, iptr, mlen, &indone, optr, olen, &outdone);
        if (mlen > indone) {
            fprintf(stderr, "resample_soxr: only %ld/%ld processed.\n",(long)indone,(long)mlen);
            fflush(stderr);
        }
        if (error) {
            fprintf(stderr, "resample_soxr: Error calling soxr_process: (%s).\n", soxr_strerror(error));
            fflush(stderr);
            exit(5);
        }

        /**********************************************************************/
        /* write data to output                                               */
        /**********************************************************************/
 
        /* write output */
        check = fwrite((void*)optr, nch*sizeof(double), outdone, stdout);
        fflush(stdout);
    
        /* this should not happen, the whole block should be written */
        if (check < outdone) {
            fprintf(stderr, "resample_soxr: Error writing to output..\n");
            fflush(stderr);
            exit(6);
        }
    
        itotal += mlen;
        ototal += outdone;
        if (mlen == 0) /* done */
            break;
    }

    /**********************************************************************/
    /* resample_soxr end, cleanup                                         */
    /**********************************************************************/

    soxr_delete(soxr);
    free(iptr);
    free(optr);
    if (verbose) {
        fprintf(stderr, "resample_soxr: %ld input and %ld output samples\n", (long)itotal, (long)ototal);
    }
    return(0);
}

