#!/bin/bash

# find all patches in patches/
PATCHES=$(find patches/ -type f -name '*.patch')

check () {
	git apply --check -q -p1 $PATCH
}

apply () {
	git apply -p1 $PATCH
}

check_applied () {
	git apply --check --reverse -q -p1 $PATCH
}

fail () {
	echo =======\> \'$PATCH\' was not applied && exit 1
}

if [[ -n "$PATCHES" ]];
then
	# check patch validity and apply, else check if already applied and report and exit on failure
	echo 'Patches found. Applying...';
	for PATCH in $PATCHES;
	do
		check && apply || check_applied || fail;
	done
else
	echo 'No patches found.'
fi
