#!/bin/sh -eu

# find all patches in patches/
PATCHES=$(find patches/ -type f -name '*.patch')

apply () {
	patch -p1 -r /dev/null -N -s < $PATCH >/dev/null 2>&1 && echo NEW: \'$PATCH\'
}

check_applied () {
	patch -p1 -r /dev/null --dry-run -R -s < $PATCH >/dev/null 2>&1 && echo OK: \'$PATCH\'
}

fail () {
	echo FAILED: \'$PATCH\' NOT APPLICABLE && exit 1
}

if [ -n "$PATCHES" ];
then
	# check patch validity and apply, else check if already applied and report and exit on failure
	echo 'Patches found. Applying...';
	for PATCH in $PATCHES;
	do
		apply || check_applied || fail;
	done
else
	echo 'No patches found.'
fi
