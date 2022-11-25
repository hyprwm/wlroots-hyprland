allocators = get_option('allocators')
if 'auto' in allocators and get_option('auto_features').enabled()
	allocators = ['gbm']
elif 'auto' in allocators and get_option('auto_features').disabled()
	allocators = []
endif

wlr_files += files(
	'allocator.c',
	'shm.c',
	'drm_dumb.c',
)

gbm = disabler()
if 'gbm' in allocators or 'auto' in allocators
	gbm = dependency('gbm', version: '>=17.1.0', required: 'gbm' in allocators)
endif
if gbm.found()
	wlr_files += files('gbm.c')
	wlr_deps += gbm
	features += { 'gbm-allocator': true }

	has = cc.has_function('gbm_bo_get_fd_for_plane', dependencies: [gbm])
	internal_config.set10('HAVE_GBM_BO_GET_FD_FOR_PLANE', has)
endif
