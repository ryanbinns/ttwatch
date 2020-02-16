#!/bin/sh

# Upload a TTBIN file (after converting to TCX) to Strava.
#
# You need a Strava API v3 OAuth ACCESS_TOKEN with write
# permission for your account. (Details of the OAuth
# authorization procedure: http://strava.github.io/api/v3/oauth)
#
# My stravacli web app can generate one for you:
# https://stravacli-dlenski.rhcloud.com (this web app does NOT retain or
# store your access token in any way, but if you don't trust it then you'll
# need to get your own Strava API key).
#
# You'll also need cURL and the ttbincnv utility from ttwatch:
# http://github.com/ryanbinns/ttwatch

ACCESS_TOKEN="this should be a 40-character hex string"

# curl https://www.strava.com/api/v3/athlete -H "Authorization: Bearer $ACCESS_TOKEN"

for F in "$@"
do
    UPLOAD=$(
        ttbincnv -t < $F | gzip -c -9 |
        curl -s -X POST https://www.strava.com/api/v3/uploads \
            -H "Authorization: Bearer $ACCESS_TOKEN" \
            -F file=@- -F data_type=tcx.gz -m 60 |
        grep -oP '"id":\d+' | cut -c6-
    )
done

while [ -z "$ACTIVITY" ]
do
    OUTPUT=$(
        curl -s -X GET "https://www.strava.com/api/v3/uploads/$UPLOAD" \
             -H "Authorization: Bearer $ACCESS_TOKEN"
    )
    ACTIVITY=$(
        echo "$OUTPUT" | grep -oP '"activity_id":\d+' | cut -c15-
    )
    ERROR=$(
        echo "$OUTPUT" | grep -oP '"error":".+"' | cut -c10-
    )
    if [ -n "$ERROR" ]; then
    	break
    fi
    sleep 1
done

if [ -n "$ACTIVITY" ]; then
    echo "http://strava.com/activities/$ACTIVITY"
elif [ -n "$ERROR" ]; then
    echo "ERROR: $ERROR" 1>&2
    exit 1
elif [ -n "$UPLOAD" ]; then
    echo "UPLOAD FAILED" 1>&2
    exit 1
fi
