/*****************************************************************************\
** ttbincnv.c                                                                **
** TTBIN file converter                                                      **
\*****************************************************************************/

#include "ttbin.h"

#include <getopt.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void do_replace_lap_list(TTBIN_FILE *ttbin, const char *laps)
{
    float distance = 0;
    float *distances = 0;
    unsigned count = 0;

    while (*laps)
    {
        /* find the first lap distance */
        if (sscanf(laps, "%f", &distance) != 1)
            return;

        distances = (float*)realloc(distances, (count + 1) * sizeof(float));
        distances[count++] = distance;

        /* scan the next lap distance */
        while (*laps && (*laps != ','))
            ++laps;
    }

    replace_lap_list(ttbin, distances, count);
}

char *toupper_s(const char *str)
{
    char *ptr = malloc(strlen(str) + 1);
    char *data = ptr;
    while (*str)
        *data++ = toupper(*str++);
    *data++ = 0;
    return ptr;
}

void help(char *argv[])
{
    unsigned i;
    printf("Usage: %s [OPTION]... [FILE]\n", argv[0]);
    printf("Converts TomTom TTBIN files to other file formats.\n");
    printf("\n");
    printf("Mandatory arguments to long options are mandatory for short options too.\n");
    printf("  -h, --help         Print this help.\n");
    printf("  -l, --laps=[list]  Replace the laps recorded on the watch with a list of\n");
    printf("                       alternative laps.\n");
    printf("  -a, --all          Output all supported file formats.\n");
    for (i = 0; i < OFFLINE_FORMAT_COUNT; ++i)
    {
        if (OFFLINE_FORMATS[i].producer)
        {
            char *str = toupper_s(OFFLINE_FORMATS[i].name);
            printf("  -%c, --%-13sOutput a %s file.\n", OFFLINE_FORMATS[i].name[0],
                OFFLINE_FORMATS[i].name, str);
            free(str);
        }
    }
    printf("\n");
    printf("If the input file is not specified, the program will operate in pipe mode,\n");
    printf("taking input from stdin, and writing the output to stdout. Only one output\n");
    printf("format may be specified in this mode.\n");
    printf("\n");
    printf("The list of laps does not have to match the distance of the activity; it will\n");
    printf("be used multiple times. For example, \"--laps=1000\" will create a lap marker\n");
    printf("every 1000m, and \"--laps=100,200,400,800,1000\" will create laps after\n");
    printf("100m, 200m, 400m, 800m, 1000m, 1100m, 1200m, 1400m, 1800m, 2000m etc...\n");
}

int main(int argc, char *argv[])
{
    uint32_t formats = 0;
    int pipe_mode = 0;
    int set_laps = 0;
    char *lap_definitions = 0;
    FILE *input_file = 0;
    TTBIN_FILE *ttbin = 0;
    unsigned i;

    int opt = 0;
    int option_index = 0;

    /* create the options lists */
    #define OPTION_COUNT    (OFFLINE_FORMAT_COUNT + 4)
    struct option long_options[OPTION_COUNT] =
    {
        { "help", no_argument, 0, 'h' },
        { "all",  no_argument, 0, 'a' },
        { "laps", required_argument, 0, 'l' },
    };
    char short_options[OPTION_COUNT + 1] = "hl:a";

    opt = 3;
    for (i = 0; i < OFFLINE_FORMAT_COUNT; ++i)
    {
        if (OFFLINE_FORMATS[i].producer)
        {
            long_options[opt].name    = OFFLINE_FORMATS[i].name;
            long_options[opt].has_arg = no_argument;
            long_options[opt].flag    = 0;
            long_options[opt].val     = OFFLINE_FORMATS[i].name[0];

            short_options[opt++ + 1]  = OFFLINE_FORMATS[i].name[0];
        }
    }
    while (opt < OPTION_COUNT)
    {
        memset(&long_options[opt], 0, sizeof(struct option));
        short_options[opt++ + 1] = 0;
    }

    /* check the command line options */
    while ((opt = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1)
    {
        switch (opt)
        {
        case 'h':   /* help */
            help(argv);
            return 0;
        case 'l':   /* set lap list */
            set_laps = 1;
            lap_definitions = optarg;
            break;
        case 'a':   /* all supported formats */
            formats = 0xffffffff;
            break;
        default:
            for (i = 0; i < OFFLINE_FORMAT_COUNT; ++i)
            {
                if (opt == OFFLINE_FORMATS[i].name[0])
                {
                    formats |= OFFLINE_FORMATS[i].mask;
                    break;
                }
            }
            break;
        }
    }

    /* check that we actually have to do something */
    if (!formats)
    {
        help(argv);
        return 0;
    }

    pipe_mode = (optind >= argc);

    /* make sure we've only got one output format specified if we're operating as a pipe */
    if (pipe_mode && (formats & (formats - 1)))
    {
        fprintf(stderr, "Only one output format can be specified in pipe mode\n");
        return 4;
    }

    /* open the input file */
    if (!pipe_mode)
    {
        input_file = fopen(argv[optind], "r");
        if (!input_file)
        {
            fprintf(stderr, "Unable to open input file: %s\n", argv[optind]);
            return 3;
        }
    }
    else
        input_file = stdin;

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

    /* set the list of laps if we have been asked to */
    if (set_laps)
        do_replace_lap_list(ttbin, lap_definitions);

    /* write the output files */
    for (i = 0; i < OFFLINE_FORMAT_COUNT; ++i)
    {
        if ((formats & OFFLINE_FORMATS[i].mask) && OFFLINE_FORMATS[i].producer)
        {
            if (!OFFLINE_FORMATS[i].requires_gps || ttbin->gps_record_count)
            {
                FILE *output_file = stdout;
                if (!pipe_mode)
                {
                    const char *filename = create_filename(ttbin, OFFLINE_FORMATS[i].name);
                    output_file = fopen(filename, "w");
                    if (!output_file)
                        fprintf(stderr, "Unable to create output file: %s\n", filename);
                }
                if (output_file)
                {
                    (*OFFLINE_FORMATS[i].producer)(ttbin, output_file);

                    if (output_file != stdout)
                        fclose(output_file);
                }
            }
            else
                fprintf(stderr, "Unable to process output format: %s\n", OFFLINE_FORMATS[i].name);
        }
    }

    free_ttbin(ttbin);

    return 0;
}

