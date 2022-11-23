libseat = dependency(
	'libseat',
	version: '>=0.2.0',
	fallback: 'seatd',
	default_options: ['server=disabled', 'man-pages=disabled', 'examples=disabled'],
)
wlr_files += files('session.c')
wlr_deps += libseat
