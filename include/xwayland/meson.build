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

xwayland_config_data = configuration_data()
xwayland_config_data.set_quoted('XWAYLAND_PATH', xwayland_path)
foreach name, have : xwayland_features
	xwayland_config_data.set10('HAVE_XWAYLAND_' + name.to_upper(), have)
endforeach
configure_file(
	output: 'config.h',
	configuration: xwayland_config_data,
)
