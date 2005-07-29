#!/bin/sh
# Sample debian/patches/00template script
# era Thu May 15 23:24:07 2003

# This simply creates the equivalent of the hard-coded template.
# Adapt and hack to suit your needs.

file="$1"
shift
description="$@"

fullnameguess="$(getent passwd $(id -un) | cut -f5 -d: | cut -f1 -d,)"
domainguess=$([ -f /etc/mailname ] && cat /etc/mailname || hostname -f)
emailguess="${DEBEMAIL:-${EMAIL:-$(logname)@${domainguess}}}"

DPATCH_FILE="`echo $file | sed s/\.diff// | sed s/\.patch//`.dpatch"
cat >"$DPATCH_FILE" <<EOF
#! /bin/sh /usr/share/dpatch/dpatch-run
## ${file} by ${DEBFULLNAME:-$fullnameguess} <$emailguess>
##
## All lines beginning with \`## DP:' are a description of the patch.
## DP: ${description:-No description}

@DPATCH@
EOF
cat "$file" >> "$DPATCH_FILE"

