xwayland_libs = []
xwayland_required = [
	'xcb',
	'xcb-composite',
	'xcb-icccm',
	'xcb-render',
	'xcb-res',
	'xcb-xfixes',
]
xwayland_optional = {
	'xcb-errors': 'Required for printing X11 errors.',
}

msg = []
if get_option('xwayland').enabled()
	msg += 'Install "@0@" or pass "-Dxwayland=disabled".'
endif
if not get_option('xwayland').disabled()
	msg += 'Required for Xwayland support.'
endif

xwayland = dependency('xwayland', required: false)
if not xwayland.found()
	# There's no Xwayland release with the pkg-config file shipped yet.
	xwayland_prog = find_program('Xwayland', required: false)
	if not xwayland_prog.found()
		if get_option('xwayland').enabled()
			error('\n'.join(msg).format('xwayland'))
		else
			subdir_done()
		endif
	endif
endif

foreach lib : xwayland_required
	dep = dependency(lib,
		required: get_option('xwayland'),
		not_found_message: '\n'.join(msg).format(lib),
	)
	if not dep.found()
		subdir_done()
	endif

	xwayland_libs += dep
endforeach

foreach lib, desc : xwayland_optional
	msg = []
	if get_option(lib).enabled()
		msg += 'Install "@0@" or pass "-D@0@=disabled".'
	endif
	if not get_option(lib).disabled()
		msg += desc
	endif

	dep = dependency(lib,
		required: get_option(lib),
		not_found_message: '\n'.join(msg).format(lib),
	)

	internal_features += { lib: dep.found() }
	xwayland_libs += dep
endforeach

xwayland_feature_names = [
	'listenfd',
	'no_touch_pointer_emulation',
	'force_xrandr_emulation',
	'terminate_delay',
]

xwayland_features = {}
if xwayland.found()
	xwayland_path = xwayland.get_variable('xwayland')
	foreach name : xwayland_feature_names
		have = xwayland.get_variable('have_' + name, default_value: 'false') == 'true'
		xwayland_features += { name: have }
	endforeach
else
	xwayland_path = xwayland_prog.full_path()
	foreach name : xwayland_feature_names
		xwayland_features += { name: false }
	endforeach
endif

internal_config.set_quoted('XWAYLAND_PATH', xwayland_path)
foreach name, have : xwayland_features
	internal_config.set10('HAVE_XWAYLAND_' + name.to_upper(), have)
endforeach

wlr_files += files(
	'selection/dnd.c',
	'selection/incoming.c',
	'selection/outgoing.c',
	'selection/selection.c',
	'server.c',
	'shell.c',
	'sockets.c',
	'xwayland.c',
	'xwm.c',
)
wlr_deps += xwayland_libs
features += { 'xwayland': true }

have = cc.has_function('xcb_xfixes_set_client_disconnect_mode', dependencies: xwayland_libs)
internal_config.set10('HAS_XCB_XFIXES_SET_CLIENT_DISCONNECT_MODE', have)
