#!/bin/bash
#
# Upload a TTBIN file (after converting to TCX) to the 
# TomTom MySports website, using http://www.mapmyfitness.com/
# This mimics the upload behaviour of the Android app, and makes 
# your activities available on the app.
#
# It requires a MapMyFitness account, linked to your MySports account.
# 
# Set these two variables to log in to the MapMyFitness site
USERNAME="username@mapmyfitness"
PASSWORD="password@mapmyfitness"
TTBIN_DIR=~/ttwatch
COOKIE_JAR=~/ttwatch/mapmyfitness-cookies.txt
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

if [ ! -d "${TTBIN_DIR}"/ ]; then
	echo "The directory ${TTBIN_DIR} does not exist."
        exit;
fi

if [ ! "${device}" ] && [ $( ls -d "${TTBIN_DIR}"/*/ | wc -l ) -ne 1 ]; then
	echo "Multiple devices; please pass one using the -d <watch name> option"
	exit;
elif [ ! -d "${TTBIN_DIR}/${device}" ]; then
	echo "Invalid device name ${device}"
	exit;
else
	TTBIN_DIR="$(ls -d ${TTBIN_DIR}/*/)"
fi

for dir in "${TTBIN_DIR}"/*; do 
	if [ -d "${dir}" ]; then 
		for ttbin_file in "${dir}"/*.ttbin; do 
			file=$(basename ${ttbin_file} .ttbin)
			tcx_file=${file}.tcx
			if [ ! -f "${dir}/.${file}-uploaded_to_mysports" ]; then
				if [ ! -f "${dir}/${file}.tcx" ]; then
					[ ${verbose} -eq 1 ] && echo "Converting ${file}.ttbin to ${file}.tcx..."
					( cd "${dir}" && ttbincnv -t "${dir}/${file}.ttbin" )
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
					echo $(date ) > "${dir}/.${file}-uploaded_to_mysports"
				fi
			else
				[ "${verbose}" -eq 1 ] && echo "${tcx_file} has already been uploaded from $(basename $dir)"
			fi
		done
	fi
done

exit 0;
