/*****************************************************************************\
** ttbincnv.c                                                                **
** TTBIN file converter                                                      **
\*****************************************************************************/

#include "ttbin.h"

#include <ctype.h>
#include <getopt.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TRUNCATE_AUTO       (0)
#define TRUNCATE_LAPS       (1)
#define TRUNCATE_RACE       (2)
#define TRUNCATE_GOAL       (3)
#define TRUNCATE_INTERVAL   (4)

void do_replace_lap_list(TTBIN_FILE *ttbin, const char *laps)
{
    float distance = 0;
    float *distances = 0;
    unsigned count = 0;
    char *tlaps;
    char *token;
    const char seps[] = " ,";

    tlaps = strdup(laps);
    token = strtok(tlaps, seps);
    while (token != NULL)
    {
        sscanf(token, "%f", &distance);
        distances = (float*)realloc(distances, (count + 1) * sizeof(float));
        distances[count++] = distance;

        token = strtok(NULL, seps);
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
    printf("Usage: %s [OPTION]... [FILE]\n", argv[0]);
    printf("Modifies TomTom TTBIN files.\n");
    printf("\n");
    printf("Mandatory arguments to long options are mandatory for short options too.\n");
    printf("  -h, --help         Print this help.\n");
    printf("  -l, --laps=[LIST]  Replace the laps recorded on the watch with a list of\n");
    printf("                       alternative laps.\n");
    printf("  -o=[FILE]          Set the output file name.\n");
    printf("  -t, --truncate=[MODE] Truncate the output file.\n");
    printf("\n");
    printf("If the input file is not specified, the program will read from stdin.\n");
    printf("If the output file is not specified, the program will write to stdout.\n");
    printf("\n");
    printf("The list of laps does not have to match the distance of the activity; it will\n");
    printf("be used multiple times. For example, \"--laps=1000\" will create a lap marker\n");
    printf("every 1000m, and \"--laps=100,200,400,800,1000\" will create laps after\n");
    printf("100m, 200m, 400m, 800m, 1000m, 1100m, 1200m, 1400m, 1800m, 2000m etc...\n");
    printf("\n");
    printf("If the truncation mode is not specified (-t is specified without a parameter,\n");
    printf("then the file is truncated at one of the following points, in this order:\n");
    printf("  1. last lap (MODE = laps)\n");
    printf("  2. interval cooldown (MODE = interval)\n");
    printf("  2. race result (MODE = race)\n");
    printf("  3. goal completion (MODE = goal)\n");
    printf("In other words, the laps take precedence over the interval etc...\n");
    printf("Alternatively, one of the above parameters can be specified to truncate at\n");
    printf("the desired point.\n");
}

int main(int argc, char *argv[])
{
    int set_laps = 0;
    char *lap_definitions = 0;
    FILE *input_file = stdin;
    FILE *output_file = stdout;
    TTBIN_FILE *ttbin = 0;
    int truncate = 0;
    int truncate_mode = TRUNCATE_AUTO;

    int opt = 0;
    int option_index = 0;

    /* create the options lists */
    const struct option long_options[] =
    {
        { "help",     no_argument,       0, 'h' },
        { "laps",     required_argument, 0, 'l' },
        { "truncate", optional_argument, 0, 't' },
    };

    if (argc < 2)
    {
        help(argv);
        return 0;
    }

    /* check the command line options */
    while ((opt = getopt_long(argc, argv, "hl:o:t::", long_options, &option_index)) != -1)
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
        case 'o':   /* set the output file */
            output_file = fopen(optarg, "w");
            if (!output_file)
            {
                fprintf(stderr, "Unable to open output file: %s\n", optarg);
                return 1;
            }
            break;
        case 't':   /* truncate the file */
            truncate = 1;
            if (optarg)
            {
                if (strcasecmp(optarg, "laps") == 0)
                    truncate_mode = TRUNCATE_LAPS;
                else if (strcasecmp(optarg, "race") == 0)
                    truncate_mode = TRUNCATE_RACE;
                else if (strcasecmp(optarg, "goal") == 0)
                    truncate_mode = TRUNCATE_GOAL;
                else if (strcasecmp(optarg, "intervals") == 0)
                    truncate_mode = TRUNCATE_INTERVAL;
                else
                {
                    fprintf(stderr, "Invalid truncate mode specified: %s\n", optarg);
                    return 1;
                }
            }
            break;
        default:
            break;
        }
    }

    /* open the input file if one was specified */
    if (optind < argc)
    {
        input_file = fopen(argv[optind], "r");
        if (!input_file)
        {
            fprintf(stderr, "Unable to open input file: %s\n", argv[optind]);
            return 1;
        }
    }

    /* read the ttbin data file */
    ttbin = read_ttbin_file(input_file);
    if (input_file != stdin)
        fclose(input_file);
    if (!ttbin)
    {
        fprintf(stderr, "Unable to read and parse TTBIN file\n");
        return 1;
    }

    /* set the list of laps if we have been asked to */
    if (set_laps)
        do_replace_lap_list(ttbin, lap_definitions);

    /* truncate the file if we have been asked to */
    if (truncate)
    {
        switch (truncate_mode)
        {
        case TRUNCATE_LAPS:     truncate_laps(ttbin);      break;
        case TRUNCATE_RACE:     truncate_race(ttbin);      break;
        case TRUNCATE_GOAL:     truncate_goal(ttbin);      break;
        case TRUNCATE_INTERVAL: truncate_intervals(ttbin); break;
        default:                truncate_laps(ttbin) || truncate_intervals(ttbin) ||
                                truncate_race(ttbin) || truncate_goal(ttbin); break;
        }
    }

    /* write the output file */
    write_ttbin_file(ttbin, output_file);
    if (output_file != stdout)
        fclose(output_file);

    free_ttbin(ttbin);

    return 0;
}

