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
    ACTIVITY=$(
        ttbincnv -t < $F | gzip -c -9 |
        curl -s -X POST https://www.strava.com/api/v3/uploads \
            -H "Authorization: Bearer $ACCESS_TOKEN" \
            -F file=@- -F data_type=tcx.gz |
        grep -oP '"id":\d+' | cut -c6-
    )
done

if [ -n "$ACTIVITY" ]; then
    echo -n "http://strava.com/activities/$ACTIVITY"
else
    exit 1
fi
