#!/bin/sh

#
# Upload a TTBIN file (after converting to TCX) to the 
# TomTom MySports website, using http://www.mapmyfitness.com/
# 
# Set the USERNAME and PASSWORD variables to your MapMyFitness
# credentials to log in to the MapMyFitness site
USERNAME="username@mapmyfitness"
PASSWORD="password@mapmyfitness"
#
# Set TTWATCH_DIR to the absolute location of your ttwatch data. This
# directory should contain a directory named for your device (one
# directory per device), which in turn should contain directories of the
# form "YYYY-MM-DD", which should hold tcx or ttbin files
TTWATCH_DIR=~/ttwatch
COOKIE_JAR=${TTWATCH_DIR}/mapmyfitness-cookies.txt
#
LOGIN_PAGE=https://www.mapmyfitness.com/auth/login/
UPLOAD_PAGE=https://www.mapmyfitness.com/device/file-upload

verbose=0
OPTIND=1 # Reset in case getopts has been used previously in the shell.
while getopts "d:v?h" opt; do
    case "$opt" in
    h|\?)
    echo "Usage: $0 [-v] [-d <watch name>]"
        exit 0
        ;;
    v)  verbose=1
        curl_args="-v"
        ;;
    d)  device=$OPTARG
        ;;
    esac
done

if [ ! -d "${TTWATCH_DIR}"/ ]; then
    echo "The directory ${TTWATCH_DIR} does not exist."
        exit;
fi

if [ "${device}" ] && [ -d "${TTWATCH_DIR}/${device}" ]; then
    DEVICE_DIR="${TTWATCH_DIR}/${device}"
elif [ "${device}" ];  then
    echo "Invalid device name ${device}"
    exit;
elif [ "$( find "${TTWATCH_DIR}" -mindepth 1 -maxdepth 1 -type d | wc -l )" -eq 1 ]; then
    DEVICE_DIR="$( find "${TTWATCH_DIR}" -mindepth 1 -maxdepth 1 -type d )"
else 
    echo "Multiple devices; please pass one using the -d <watch name> option"
    exit;
fi

for dir in "${DEVICE_DIR}"/*/; do
    for ttbin_file in "${dir}"/*.ttbin; do
        file=$(basename "${ttbin_file}" .ttbin)
        tcx_file=${file}.tcx
        if [ ! -f "${dir}/.${file}-uploaded_to_mysports" ]; then
            if [ ! -f "${dir}/${file}.tcx" ]; then
                [ ${verbose} -eq 1 ] && echo "Converting ${file}.ttbin to ${file}.tcx..."
                ( cd "${dir}" && cat "${file}.ttbin" | ttbincnv -t >"${file}.tcx" )
            fi
            # Initialise the cookie, as the MapMyFitness website will return 400 otherwise
            curl -c ${COOKIE_JAR} -b ${COOKIE_JAR} -s -o /dev/null \
                ${LOGIN_PAGE}
            [ $? -eq 0 ] || exit;
            curl ${curl_args} -c ${COOKIE_JAR} -b ${COOKIE_JAR} --max-redirs 10 \
                --form-string "csrfmiddlewaretoken=" \
                --form-string "email=${USERNAME}" \
                --form-string "password=${PASSWORD}" \
                ${LOGIN_PAGE}
            [ $? -eq 0 ] || exit;
            curl ${curl_args} -c ${COOKIE_JAR} -b ${COOKIE_JAR} --max-redirs 10 \
                --form "file_to_upload=@\"${dir}/${tcx_file}\"" \
                ${UPLOAD_PAGE}
            if [ $? -eq 0 ]; then
                [ ${verbose} -eq 1 ] && echo "Creating ${dir}/.${file}-uploaded_to_mysports"
                date > "${dir}/.${file}-uploaded_to_mysports"
            fi
        else
            [ "${verbose}" -eq 1 ] && echo "${tcx_file} has already been uploaded from $(basename "$dir")"
        fi
    done
done

exit 0;
