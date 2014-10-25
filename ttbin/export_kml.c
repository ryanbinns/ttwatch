/*****************************************************************************\
** export_kml.c                                                              **
** KML export code                                                           **
\*****************************************************************************/

#include "ttbin.h"

#include <math.h>
#include <string.h>

static void make_kml_style(FILE *file, const char *id, const char *icon, uint32_t icon_colour,
    uint32_t line_colour, int line_width, uint32_t poly_colour, const char *balloon_text)
{
    fputs(        "        <Style id=\"", file);
    fputs(        id, file);
    fputs(        "\">\r\n"
                  "            <IconStyle>\r\n"
                  "                <Icon>\r\n"
                  "                    <href>", file);
    fputs(        icon, file);
    fputs(        "</href>\r\n"
                  "                </Icon>\r\n", file);
    if (icon_colour & 0xff000000)
        fprintf(file, "                <color>%08x</color>\r\n", icon_colour);
    fputs(        "            </IconStyle>\r\n"
                  "            <BalloonStyle>\r\n"
                  "                <text>", file);
    fputs(        balloon_text, file);
    fputs(        "</text>\r\n"
                  "            </BalloonStyle>\r\n", file);
    if (line_colour & 0xff000000)
    {
        fputs(        "            <LineStyle>\r\n", file);
        fprintf(file, "                <color>%08x</color>\r\n", line_colour);
        fprintf(file, "                <width>%d</width>\r\n", line_width);
        fputs(        "            </LineStyle>\r\n", file);
    }
    if (poly_colour & 0xff000000)
    {
        fputs(        "           <PolyStyle>\r\n", file);
        fprintf(file, "               <color>%08x</color>\r\n", poly_colour);
        fputs(        "           </PolyStyle>\r\n", file);
    }
    fputs(        "        </Style>\r\n", file);
}

void export_kml(TTBIN_FILE *ttbin, FILE *file)
{
    static const char *const MONTHNAMES[] =
        { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    uint32_t i;
    char text_buf[1024];
    const char *type_text;
    time_t timestamp;
    struct tm *time;
    uint32_t initial_time;

    if (!ttbin->gps_records)
        return;

    time = gmtime(&ttbin->timestamp);

    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
          "<kml xmlns=\"http://www.opengis.net/kml/2.2\" xmlns:gx=\"http://www.google.com/kml/ext/2.2\">\r\n"
          "    <Document>\r\n"
          "        <name>", file);
    switch(ttbin->activity)
    {
    case ACTIVITY_RUNNING:   fputs("Running", file);   break;
    case ACTIVITY_CYCLING:   fputs("Cycling", file);   break;
    case ACTIVITY_SWIMMING:  fputs("Swimming", file);  break;
    case ACTIVITY_TREADMILL: fputs("Treadmill", file); break;
    case ACTIVITY_FREESTYLE: fputs("Freestyle", file); break;
    default:                 fputs("Unknown", file);   break;
    }
    fprintf(file, "_%02d:%02d:%02d_%02d-%s-%04d</name>\r\n",
        time->tm_hour, time->tm_min, time->tm_sec, time->tm_mday,
        MONTHNAMES[time->tm_mon], time->tm_year + 1900);
    fputs("        <description>TomTom GPS Watch activity</description>\r\n", file);

    switch(ttbin->activity)
    {
    case ACTIVITY_RUNNING:   type_text = "RUNNING";   break;
    case ACTIVITY_CYCLING:   type_text = "CYCLING";   break;
    case ACTIVITY_SWIMMING:  type_text = "SWIMMING";  break;
    case ACTIVITY_TREADMILL: type_text = "TREADMILL"; break;
    case ACTIVITY_FREESTYLE: type_text = "FREESTYLE"; break;
    default:                 type_text = "UNKNOWN";   break;
    }

    sprintf(text_buf, "&lt;h2&gt;%s&lt;/h2&gt;"
        "   Elapsed time  : %d&lt;br/&gt;"
        "   Total distance: %.2f&lt;br/&gt;"
        "   Total Calories: %d&lt;br/&gt;",
        type_text, ttbin->duration, ttbin->total_distance, ttbin->total_calories);

    make_kml_style(file, "track", "http://earth.google.com/images/kml-icons/track-directional/track-0.png",
        0, 0xff31d7bd, 8, 0xffbdd731, text_buf);
    make_kml_style(file, "start-track", "http://maps.google.com/mapfiles/kml/pal5/icon13.png",
        0xff007f00, 0, 0, 0, text_buf);
    make_kml_style(file, "end-track", "http://maps.google.com/mapfiles/kml/pal5/icon13.png",
        0xff0000af, 0, 0, 0, text_buf);
    make_kml_style(file, "graph", "http://maps.google.com/mapfiles/kml/shapes/placemark_circle.png",
        0, 0, 0, 0, "$[description]");

    strcpy(text_buf, "&lt;h2&gt;");
    strcat(text_buf, type_text);
    strcat(text_buf, " Distance laps&lt;/h2&gt;&lt;TABLE BORDER=\"1\"&gt;\r\n"
        "&lt;TR&gt;&lt;TH&gt;Lap&lt;/TH&gt;&lt;TH&gt;Time&lt;/TH&gt;"
        "&lt;TH&gt;Distance&lt;/TH&gt;&lt;TH&gt;Calories&lt;/TH&gt;&lt;TH&gt;Delta Time&lt;/TH&gt;"
        "&lt;TH&gt;Delta Distance&lt;/TH&gt;&lt;TH&gt;Delta Calories&lt;/TH&gt;&lt;/TR&gt;\r\n");

    initial_time = 0;
    for (i = 0; i < ttbin->lap_record_count; ++i)
    {
        LAP_RECORD *lap = &ttbin->lap_records[i];

        sprintf(text_buf + strlen(text_buf),
            "&lt;TR&gt;&lt;TH&gt;%d&lt;/TH&gt;&lt;TD&gt;%d&lt;/TD&gt;&lt;TD&gt;%.2f&lt;/TD&gt;"
            "&lt;TD&gt;%d&lt;/TD&gt;&lt;TD&gt;%d&lt;/TD&gt;&lt;TD&gt;%.2f&lt;/TD&gt;&lt;TD&gt;%d&lt;/TD&gt;&lt;/TR&gt;\r\n",
            i + 1, lap->total_time, lap->total_distance, lap->total_calories, lap->total_time - initial_time,
            lap->total_distance - ((i > 0) ? (lap - 1)->total_distance : 0.0f),
            lap->total_calories - ((i > 0) ? (lap - 1)->total_calories : 0));
        initial_time = lap->total_time;
    }
    sprintf(text_buf + strlen(text_buf), "&lt;/TABLE&gt;\r\n");

    make_kml_style(file, "laps-balloon", "http://maps.google.com/mapfiles/kml/shapes/placemark_circle.png",
        0, 0, 0, 0, text_buf);

    fputs(        "        <Schema id=\"", file);
    fputs(        type_text, file);
    fputs(        "_schema\">\r\n"
                  "            <gx:SimpleArrayField name=\"calories\" type=\"int\">\r\n"
                  "                <displayName>Calories</displayName>\r\n"
                  "            </gx:SimpleArrayField>\r\n"
                  "            <gx:SimpleArrayField name=\"distance\" type=\"float\">\r\n"
                  "                <displayName>Distance</displayName>\r\n"
                  "            </gx:SimpleArrayField>\r\n"
                  "            <gx:SimpleArrayField name=\"speed\" type=\"float\">\r\n"
                  "                <displayName>Speed</displayName>\r\n"
                  "            </gx:SimpleArrayField>\r\n"
                  "            <gx:SimpleArrayField name=\"pace\" type=\"float\">\r\n"
                  "                <displayName>Pace</displayName>\r\n"
                  "            </gx:SimpleArrayField>\r\n", file);
    if (ttbin->activity != ACTIVITY_CYCLING)
    {
        fputs(        "            <gx:SimpleArrayField name=\"steps\" type=\"int\">\r\n"
                      "                <displayName>Steps</displayName>\r\n"
                      "            </gx:SimpleArrayField>\r\n", file);
    }
    fputs(        "        </Schema>\r\n"
                  "        <Placemark>\r\n"
                  "            <name>Workout</name>\r\n"
                  "            <description>Workout</description>\r\n"
                  "            <styleUrl>#track</styleUrl>\r\n"
                  "            <gx:Track>\r\n"
                  "                <altitudeMode>clamptoground</altitudeMode>\r\n", file);
    for (i = 0; i < ttbin->gps_record_count; ++i)
    {
        if ((ttbin->gps_records[i].timestamp != 0) &&
            !((ttbin->gps_records[i].latitude == 0) && (ttbin->gps_records[i].longitude == 0)))
        {
            strftime(text_buf, sizeof(text_buf), "%FT%X.000Z", gmtime(&ttbin->gps_records[i].timestamp));
            fputs(        "                <when>", file);
            fputs(        text_buf, file);
            fputs(        "</when>\r\n", file);
            fprintf(file, "                <gx:coord>%.6f %.6f %d</gx:coord>\r\n",
                ttbin->gps_records[i].longitude, ttbin->gps_records[i].latitude, (int)ttbin->gps_records[i].elevation);
        }
    }
    fputs(        "                <ExtendedData>\r\n"
                  "                    <SchemaData schemaUrl=\"#", file);
    fputs(        type_text, file);
    fputs(        "-schema\">\r\n"
                  "                        <gx:SimpleArrayData name=\"calories\">\r\n", file);
    for (i = 0; i < ttbin->gps_record_count; ++i)
    {
        if ((ttbin->gps_records[i].timestamp != 0) &&
            !((ttbin->gps_records[i].latitude == 0) && (ttbin->gps_records[i].longitude == 0)))
            fprintf(file, "                            <gx:value>%d</gx:value>\r\n", ttbin->gps_records[i].calories);
    }
    fputs(        "                        </gx:SimpleArrayData>\r\n"
                  "                        <gx:SimpleArrayData name=\"distance\">\r\n", file);
    for (i = 0; i < ttbin->gps_record_count; ++i)
    {
        if ((ttbin->gps_records[i].timestamp != 0) &&
            !((ttbin->gps_records[i].latitude == 0) && (ttbin->gps_records[i].longitude == 0)))
        {
            fprintf(file, "                            <gx:value>%.*f</gx:value>\r\n",
                (ttbin->gps_records[i].cum_distance == 0.0f) ? 0 : (5 - (int)floor(log10(ttbin->gps_records[i].cum_distance))),
                ttbin->gps_records[i].cum_distance);
        }
    }
    fputs(        "                        </gx:SimpleArrayData>\r\n"
                  "                        <gx:SimpleArrayData name=\"speed\">\r\n", file);
    for (i = 0; i < ttbin->gps_record_count; ++i)
    {
        if ((ttbin->gps_records[i].timestamp != 0) &&
            !((ttbin->gps_records[i].latitude == 0) && (ttbin->gps_records[i].longitude == 0)))
        {
            fprintf(file, "                            <gx:value>%.2f</gx:value>\r\n", ttbin->gps_records[i].speed);
        }
    }
    fputs(        "                        </gx:SimpleArrayData>\r\n"
                  "                        <gx:SimpleArrayData name=\"pace\">\r\n", file);
    for (i = 0; i < ttbin->gps_record_count; ++i)
    {
        if ((ttbin->gps_records[i].timestamp != 0) &&
            !((ttbin->gps_records[i].latitude == 0) && (ttbin->gps_records[i].longitude == 0)))
        {
            fprintf(file, "                            <gx:value>%.2f</gx:value>\r\n", 1000.0f / (60.0f * ttbin->gps_records[i].speed));
        }
    }
    fputs(        "                        </gx:SimpleArrayData>\r\n", file);
    if (ttbin->activity != ACTIVITY_CYCLING)
    {
        fputs(        "                        <gx:SimpleArrayData name=\"steps\">\r\n", file);
        for (i = 0; i < ttbin->gps_record_count; ++i)
        {
            if ((ttbin->gps_records[i].timestamp != 0) &&
                !((ttbin->gps_records[i].latitude == 0) && (ttbin->gps_records[i].longitude == 0)))
            {
                fprintf(file, "                            <gx:value>%d</gx:value>\r\n", ttbin->gps_records[i].cycles);
            }
        }
        fputs(        "                        </gx:SimpleArrayData>\r\n", file);
    }
    fputs(        "                    </SchemaData>\r\n"
                  "                </ExtendedData>\r\n"
                  "            </gx:Track>\r\n"
                  "        </Placemark>\r\n"
                  "        <Placemark>\r\n"
                  "            <name>Start</name>\r\n", file);
    fprintf(file, "            <description>%.6f,%.6f</description>\r\n",
        ttbin->gps_records[0].longitude, ttbin->gps_records[0].latitude);
    fputs(        "            <styleUrl>#start-track</styleUrl>\r\n"
                  "            <Point>\r\n", file);
    fprintf(file, "                <coordinates>%.6f,%.6f</coordinates>\r\n",
        ttbin->gps_records[0].longitude, ttbin->gps_records[0].latitude);
    fputs(        "            </Point>\r\n"
                  "        </Placemark>\r\n"
                  "        <Placemark>\r\n"
                  "            <name>End</name>\r\n", file);
    fprintf(file, "            <description>%.6f,%.6f</description>\r\n",
        ttbin->gps_records[ttbin->gps_record_count - 1].longitude,
        ttbin->gps_records[ttbin->gps_record_count - 1].latitude);
    fputs(        "            <styleUrl>#end-track</styleUrl>\r\n"
                  "            <Point>\r\n", file);
    fprintf(file, "                <coordinates>%.6f,%.6f</coordinates>\r\n",
        ttbin->gps_records[ttbin->gps_record_count - 1].longitude,
        ttbin->gps_records[ttbin->gps_record_count - 1].latitude);
    fputs(        "            </Point>\r\n"
                  "        </Placemark>\r\n"
                  "        <Placemark>\r\n"
                  "            <name>Distance laps</name>\r\n", file);
    fprintf(file, "            <description>%.6f,%.6f</description>\r\n",
        ttbin->gps_records[ttbin->gps_record_count - 1].longitude,
        ttbin->gps_records[ttbin->gps_record_count - 1].latitude);
    fputs(        "            <styleUrl>#laps-balloon</styleUrl>\r\n"
                  "            <Point>\r\n", file);
    fprintf(file, "                <coordinates>%.6f,%.6f</coordinates>\r\n",
        ttbin->gps_records[ttbin->gps_record_count - 1].longitude,
        ttbin->gps_records[ttbin->gps_record_count - 1].latitude);
    fputs(        "            </Point>\r\n"
                  "        </Placemark>\r\n"
                  "    </Document>\r\n"
                  "</kml>\r\n", file);
}

