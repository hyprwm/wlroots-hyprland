hwdata = dependency(
	'hwdata',
	required: 'drm' in backends,
	native: true,
	not_found_message: 'Required for the DRM backend.',
)

if not (hwdata.found() and features['session'])
	subdir_done()
endif

hwdata_dir = hwdata.get_variable(pkgconfig: 'pkgdatadir')
pnpids_c = custom_target(
	'pnpids.c',
	output: 'pnpids.c',
	input: files(hwdata_dir / 'pnp.ids'),
	feed: true,
	capture: true,
	command: files('gen_pnpids.sh'),
)
wlr_files += pnpids_c

wlr_files += files(
	'atomic.c',
	'backend.c',
	'cvt.c',
	'drm.c',
	'legacy.c',
	'monitor.c',
	'properties.c',
	'renderer.c',
	'util.c',
)

features += { 'drm-backend': true }
