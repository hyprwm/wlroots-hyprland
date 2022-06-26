hwdata = dependency('hwdata', required: false, native: true)
if hwdata.found()
	hwdata_dir = hwdata.get_variable(pkgconfig: 'pkgdatadir')
	pnp_ids = files(hwdata_dir / 'pnp.ids')
else
	pnp_ids = files('/usr/share/hwdata/pnp.ids')
endif

pnpids_c = custom_target(
	'pnpids.c',
	output: 'pnpids.c',
	input: pnp_ids,
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
