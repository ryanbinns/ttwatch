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
    fprintf(file, "        <Style id=\"%s\">\r\n", id);
    fprintf(file, "            <IconStyle>\r\n");
    fprintf(file, "                <Icon>\r\n");
    fprintf(file, "                    <href>%s</href>\r\n", icon);
    fprintf(file, "                </Icon>\r\n");
    if (icon_colour & 0xff000000)
        fprintf(file, "                <color>%08x</color>\r\n", icon_colour);
    fprintf(file, "            </IconStyle>\r\n");
    fprintf(file, "            <BalloonStyle>\r\n");
    fprintf(file, "                <text>%s</text>\r\n", balloon_text);
    fprintf(file, "            </BalloonStyle>\r\n");
    if (line_colour & 0xff000000)
    {
        fprintf(file, "            <LineStyle>\r\n");
        fprintf(file, "                <color>%08x</color>\r\n", line_colour);
        fprintf(file, "                <width>%d</width>\r\n", line_width);
        fprintf(file, "            </LineStyle>\r\n");
    }
    if (poly_colour & 0xff000000)
    {
        fprintf(file, "           <PolyStyle>\r\n");
        fprintf(file, "               <color>%08x</color>\r\n", poly_colour);
        fprintf(file, "           </PolyStyle>\r\n");
    }
    fprintf(file, "        </Style>\r\n");
}

void export_kml(TTBIN_FILE *ttbin, FILE *file)
{
    static const char *const MONTHNAMES[] =
        { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    uint32_t i;
    struct tm *time;
    char text_buf[1024];
    const char *type_text;
    time_t timestamp;
    uint32_t initial_time;

    if (!ttbin->gps_records)
        return;

    time = gmtime(&ttbin->timestamp);

    fprintf(file, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n");
    fprintf(file, "<kml xmlns=\"http://www.opengis.net/kml/2.2\" xmlns:gx=\"http://www.google.com/kml/ext/2.2\">\r\n");
    fprintf(file, "    <Document>\r\n");
    fprintf(file, "        <name>");
    switch(ttbin->activity)
    {
    case ACTIVITY_RUNNING:   fprintf(file, "Running");   break;
    case ACTIVITY_CYCLING:   fprintf(file, "Cycling");   break;
    case ACTIVITY_SWIMMING:  fprintf(file, "Swimming");  break;
    case ACTIVITY_TREADMILL: fprintf(file, "Treadmill"); break;
    case ACTIVITY_FREESTYLE: fprintf(file, "Freestyle"); break;
    default:                 fprintf(file, "Unknown");   break;
    }
    fprintf(file, "_%02d:%02d:%02d_%02d-%s-%04d</name>\r\n",
        time->tm_hour, time->tm_min, time->tm_sec, time->tm_mday,
        MONTHNAMES[time->tm_mon], time->tm_year + 1900);
    fprintf(file, "        <description>TomTom GPS Watch activity</description>\r\n");

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

    sprintf(text_buf, "&lt;h2&gt;%s Distance laps&lt;/h2&gt;&lt;TABLE BORDER=\"1\"&gt;\r\n"
        "&lt;TR&gt;&lt;TH&gt;Lap&lt;/TH&gt;&lt;TH&gt;Time&lt;/TH&gt;"
        "&lt;TH&gt;Distance&lt;/TH&gt;&lt;TH&gt;Calories&lt;/TH&gt;&lt;TH&gt;Delta Time&lt;/TH&gt;"
        "&lt;TH&gt;Delta Distance&lt;/TH&gt;&lt;TH&gt;Delta Calories&lt;/TH&gt;&lt;/TR&gt;\r\n",
        type_text);

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

    fprintf(file, "        <Schema id=\"%s_schema\">\r\n", type_text);
    fprintf(file, "            <gx:SimpleArrayField name=\"calories\" type=\"int\">\r\n");
    fprintf(file, "                <displayName>Calories</displayName>\r\n");
    fprintf(file, "            </gx:SimpleArrayField>\r\n");
    fprintf(file, "            <gx:SimpleArrayField name=\"distance\" type=\"float\">\r\n");
    fprintf(file, "                <displayName>Distance</displayName>\r\n");
    fprintf(file, "            </gx:SimpleArrayField>\r\n");
    fprintf(file, "            <gx:SimpleArrayField name=\"speed\" type=\"float\">\r\n");
    fprintf(file, "                <displayName>Speed</displayName>\r\n");
    fprintf(file, "            </gx:SimpleArrayField>\r\n");
    fprintf(file, "            <gx:SimpleArrayField name=\"pace\" type=\"float\">\r\n");
    fprintf(file, "                <displayName>Pace</displayName>\r\n");
    fprintf(file, "            </gx:SimpleArrayField>\r\n");
    if (ttbin->activity != ACTIVITY_CYCLING)
    {
        fprintf(file, "            <gx:SimpleArrayField name=\"steps\" type=\"int\">\r\n");
        fprintf(file, "                <displayName>Steps</displayName>\r\n");
        fprintf(file, "            </gx:SimpleArrayField>\r\n");
    }
    fprintf(file, "        </Schema>\r\n");
    fprintf(file, "        <Placemark>\r\n");
    fprintf(file, "            <name>Workout</name>\r\n");
    fprintf(file, "            <description>Workout</description>\r\n");
    fprintf(file, "            <styleUrl>#track</styleUrl>\r\n");
    fprintf(file, "            <gx:Track>\r\n");
    fprintf(file, "                <altitudeMode>clamptoground</altitudeMode>\r\n");
    for (i = 0; i < ttbin->gps_record_count; ++i)
    {
        timestamp = ttbin->gps_records[i].timestamp;
        time = gmtime(&timestamp);
        strftime(text_buf, sizeof(text_buf), "%FT%X.000Z", gmtime(&timestamp));
        fprintf(file, "                <when>%s</when>\r\n", text_buf);
        fprintf(file, "                <gx:coord>%.6f %.6f %d</gx:coord>\r\n",
            ttbin->gps_records[i].longitude, ttbin->gps_records[i].latitude, (int)ttbin->gps_records[i].elevation);
    }
    fprintf(file, "                <ExtendedData>\r\n");
    fprintf(file, "                    <SchemaData schemaUrl=\"#%s-schema\">\r\n", type_text);
    fprintf(file, "                        <gx:SimpleArrayData name=\"calories\">\r\n");
    for (i = 0; i < ttbin->gps_record_count; ++i)
        fprintf(file, "                            <gx:value>%d</gx:value>\r\n", ttbin->gps_records[i].calories);
    fprintf(file, "                        </gx:SimpleArrayData>\r\n");
    fprintf(file, "                        <gx:SimpleArrayData name=\"distance\">\r\n");
    for (i = 0; i < ttbin->gps_record_count; ++i)
    {
        fprintf(file, "                            <gx:value>%.*f</gx:value>\r\n",
            (ttbin->gps_records[i].cum_distance == 0.0f) ? 0 : (5 - (int)floor(log10(ttbin->gps_records[i].cum_distance))),
            ttbin->gps_records[i].cum_distance);
    }
    fprintf(file, "                        </gx:SimpleArrayData>\r\n");
    fprintf(file, "                        <gx:SimpleArrayData name=\"speed\">\r\n");
    for (i = 0; i < ttbin->gps_record_count; ++i)
        fprintf(file, "                            <gx:value>%.2f</gx:value>\r\n", ttbin->gps_records[i].speed);
    fprintf(file, "                        </gx:SimpleArrayData>\r\n");
    fprintf(file, "                        <gx:SimpleArrayData name=\"pace\">\r\n");
    for (i = 0; i < ttbin->gps_record_count; ++i)
        fprintf(file, "                            <gx:value>%.2f</gx:value>\r\n", 1000.0f / (60.0f * ttbin->gps_records[i].speed));
    fprintf(file, "                        </gx:SimpleArrayData>\r\n");
    if (ttbin->activity != ACTIVITY_CYCLING)
    {
        fprintf(file, "                        <gx:SimpleArrayData name=\"steps\">\r\n");
        for (i = 0; i < ttbin->gps_record_count; ++i)
            fprintf(file, "                            <gx:value>%d</gx:value>\r\n", ttbin->gps_records[i].cycles);
        fprintf(file, "                        </gx:SimpleArrayData>\r\n");
    }
    fprintf(file, "                    </SchemaData>\r\n");
    fprintf(file, "                </ExtendedData>\r\n");
    fprintf(file, "            </gx:Track>\r\n");
    fprintf(file, "        </Placemark>\r\n");
    fprintf(file, "        <Placemark>\r\n");
    fprintf(file, "            <name>Start</name>\r\n");
    fprintf(file, "            <description>%.6f,%.6f</description>\r\n",
        ttbin->gps_records[0].longitude, ttbin->gps_records[0].latitude);
    fprintf(file, "            <styleUrl>#start-track</styleUrl>\r\n");
    fprintf(file, "            <Point>\r\n");
    fprintf(file, "                <coordinates>%.6f,%.6f</coordinates>\r\n",
        ttbin->gps_records[0].longitude, ttbin->gps_records[0].latitude);
    fprintf(file, "            </Point>\r\n");
    fprintf(file, "        </Placemark>\r\n");
    fprintf(file, "        <Placemark>\r\n");
    fprintf(file, "            <name>End</name>\r\n");
    fprintf(file, "            <description>%.6f,%.6f</description>\r\n",
        ttbin->gps_records[ttbin->gps_record_count - 1].longitude,
        ttbin->gps_records[ttbin->gps_record_count - 1].latitude);
    fprintf(file, "            <styleUrl>#end-track</styleUrl>\r\n");
    fprintf(file, "            <Point>\r\n");
    fprintf(file, "                <coordinates>%.6f,%.6f</coordinates>\r\n",
        ttbin->gps_records[ttbin->gps_record_count - 1].longitude,
        ttbin->gps_records[ttbin->gps_record_count - 1].latitude);
    fprintf(file, "            </Point>\r\n");
    fprintf(file, "        </Placemark>\r\n");
    fprintf(file, "        <Placemark>\r\n");
    fprintf(file, "            <name>Distance laps</name>\r\n");
    fprintf(file, "            <description>%.6f,%.6f</description>\r\n",
        ttbin->gps_records[ttbin->gps_record_count - 1].longitude,
        ttbin->gps_records[ttbin->gps_record_count - 1].latitude);
    fprintf(file, "            <styleUrl>#laps-balloon</styleUrl>\r\n");
    fprintf(file, "            <Point>\r\n");
    fprintf(file, "                <coordinates>%.6f,%.6f</coordinates>\r\n",
        ttbin->gps_records[ttbin->gps_record_count - 1].longitude,
        ttbin->gps_records[ttbin->gps_record_count - 1].latitude);
    fprintf(file, "            </Point>\r\n");
    fprintf(file, "        </Placemark>\r\n");
    fprintf(file, "    </Document>\r\n");
    fprintf(file, "</kml>\r\n");
}

