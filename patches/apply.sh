#!/bin/sh -eu

# find all patches in patches/
PATCHES=$(find patches/ -type f -name '*.patch')

apply () {
	git apply $PATCH
}

check_applied () {
	git apply --check --reverse -q $PATCH
}

fail () {
	echo =======\> \'$PATCH\' was not applied && exit 1
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
