/*****************************************************************************\
** ttbincnv.c                                                                **
** TTBIN file converter                                                      **
\*****************************************************************************/

#include "ttbin.h"

#include <getopt.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>


void help(char *argv[])
{
    printf("Usage: %s [OPTION]... [FILE]\n", argv[0]);
    printf("Converts TomTom TTBIN files to other file formats.\n");
    printf("\n");
    printf("Mandatory arguments to long options are mandatory for short options too.\n");
    printf("  -h, --help         Print this help\n");
    printf("  -c, --csv          Output a CSV (Comma Separated Values) file\n");
    printf("  -g, --gpx          Output a Garmin GPX file\n");
    printf("  -k, --kml          Output a Google KML file\n");
    printf("  -i, --pipe         Operate in pipe mode, i.e. take input from stdin and\n");
    printf("                       write the resulting file to stdout\n");
    printf("\n");
    printf("The input file must be specified unless pipe mode is used\n");
}

int main(int argc, char *argv[])
{
    int make_csv = 0;
    int make_kml = 0;
    int make_gpx = 0;
    int pipe_mode = 0;
    FILE *input_file = 0;
    int multiple = 0;
    TTBIN_FILE *ttbin = 0;

    int opt = 0;
    int option_index = 0;

    struct option long_options[] = {
        { "csv",  no_argument, 0, 'c' },
        { "kml",  no_argument, 0, 'k' },
        { "gpx",  no_argument, 0, 'g' },
        { "pipe", no_argument, 0, 'i' },
        { "help", no_argument, 0, 'h' },
    };

    /* check the command line options */
    while ((opt = getopt_long(argc, argv, "ckgp:ih", long_options, &option_index)) != -1)
    {
        switch (opt)
        {
        case 'c':
            make_csv = 1;
            break;
        case 'k':
            make_kml = 1;
            break;
        case 'g':
            make_gpx = 1;
            break;
        case 'i':
            pipe_mode = 1;
            break;
        case 'h':
            help(argv);
            return 0;
        }
    }

    /* open the input file */
    if (pipe_mode)
        input_file = stdin;
    else
    {
        if (optind < argc)
        {
            input_file = fopen(argv[optind], "r");
            if (!input_file)
            {
                fprintf(stderr, "Unable to open input file: %s\n", argv[optind]);
                return 3;
            }
        }
        else
        {
            fprintf(stderr, "Input file must be specified for non-pipe mode\n");
            help(argv);
            return 3;
        }
    }

    /* check that we actually have to do something */
    if (!make_csv && !make_kml && !make_gpx)
    {
        help(argv);
        return 4;
    }

    /* read the ttbin data file */
    ttbin = read_ttbin_file(input_file);
    if (input_file != stdin)
        fclose(input_file);
    if (!ttbin)
    {
        fprintf(stderr, "Unable to read and parse TTBIN file\n");
        return 5;
    }

    /* if we have gps data, download the elevation data */
    if (ttbin->gps_record_count)
        download_elevation_data(ttbin);
    else
    {
        /* no gps data, ergo we can't produce GPS files... */
        make_kml = 0;
        make_gpx = 0;
    }

    if (make_csv)
    {
        FILE *output_file = stdout;
        if (!pipe_mode)
        {
            const char *filename = create_filename(ttbin, "csv");
            output_file = fopen(filename, "w");
            if (!output_file)
            {
                fprintf(stderr, "Unable to create output file: %s\n", filename);
                free(ttbin);
                return 6;
            }
        }

        export_csv(ttbin, output_file);

        if (output_file != stdout)
            fclose(output_file);
    }

    if (make_kml)
    {
        FILE *output_file = stdout;
        if (!pipe_mode)
        {
            const char *filename = create_filename(ttbin, "kml");
            output_file = fopen(filename, "w");
            if (!output_file)
            {
                fprintf(stderr, "Unable to create output file: %s\n", filename);
                free(ttbin);
                return 7;
            }
        }

        export_kml(ttbin, output_file);

        if (output_file != stdout)
            fclose(output_file);
    }

    if (make_gpx)
    {
        FILE *output_file = stdout;
        if (!pipe_mode)
        {
            const char *filename = create_filename(ttbin, "gpx");
            output_file = fopen(filename, "w");
            if (!output_file)
            {
                fprintf(stderr, "Unable to create output file: %s\n", filename);
                free(ttbin);
                return 8;
            }
        }

        export_gpx(ttbin, output_file);

        if (output_file != stdout)
            fclose(output_file);
    }

    free(ttbin);
}

