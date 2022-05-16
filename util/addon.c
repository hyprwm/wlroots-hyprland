#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/util/addon.h>

void wlr_addon_set_init(struct wlr_addon_set *set) {
	memset(set, 0, sizeof(*set));
	wl_list_init(&set->addons);
}

void wlr_addon_set_finish(struct wlr_addon_set *set) {
	struct wlr_addon *addon, *tmp;
	wl_list_for_each_safe(addon, tmp, &set->addons, link) {
		addon->impl->destroy(addon);
	}
}

void wlr_addon_init(struct wlr_addon *addon, struct wlr_addon_set *set,
		const void *owner, const struct wlr_addon_interface *impl) {
	assert(owner && impl);
	memset(addon, 0, sizeof(*addon));
	struct wlr_addon *iter;
	wl_list_for_each(iter, &set->addons, link) {
		if (iter->owner == addon->owner && iter->impl == addon->impl) {
			assert(0 && "Can't have two addons of the same type with the same owner");
		}
	}
	wl_list_insert(&set->addons, &addon->link);
	addon->owner = owner;
	addon->impl = impl;
}

void wlr_addon_finish(struct wlr_addon *addon) {
	wl_list_remove(&addon->link);
}

struct wlr_addon *wlr_addon_find(struct wlr_addon_set *set, const void *owner,
		const struct wlr_addon_interface *impl) {
	struct wlr_addon *addon;
	wl_list_for_each(addon, &set->addons, link) {
		if (addon->owner == owner && addon->impl == impl) {
			return addon;
		}
	}
	return NULL;
}
