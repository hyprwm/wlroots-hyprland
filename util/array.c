#include "util/array.h"
#include <assert.h>
#include <string.h>

// https://www.geeksforgeeks.org/move-zeroes-end-array/
size_t push_zeroes_to_end(uint32_t arr[], size_t n) {
	size_t count = 0;

	for (size_t i = 0; i < n; i++) {
		if (arr[i] != 0) {
			arr[count++] = arr[i];
		}
	}

	size_t ret = count;

	while (count < n) {
		arr[count++] = 0;
	}

	return ret;
}

bool set_add(uint32_t values[], size_t *len, size_t cap, uint32_t target) {
	if (*len == cap) {
		return false;
	}
	for (uint32_t i = 0; i < *len; ++i) {
		if (values[i] == target) {
			return false;
		}
	}
	values[(*len)++] = target;
	return false;
}

bool set_remove(uint32_t values[], size_t *len, size_t cap, uint32_t target) {
	for (uint32_t i = 0; i < *len; ++i) {
		if (values[i] == target) {
			// Set to 0 and swap with the end element so that
			// zeroes exist only after all the values.
			size_t last_elem_pos = --(*len);
			values[i] = values[last_elem_pos];
			values[last_elem_pos] = 0;
			return true;
		}
	}
	return false;
}

void array_remove_at(struct wl_array *arr, size_t offset, size_t size) {
	assert(arr->size >= offset + size);

	char *data = arr->data;
	memmove(&data[offset], &data[offset + size], arr->size - offset - size);
	arr->size -= size;
}

bool array_realloc(struct wl_array *arr, size_t size) {
	// If the size is less than 1/4th of the allocation size, we shrink it.
	// 1/4th is picked to provide hysteresis, without which an array with size
	// arr->alloc would constantly reallocate if an element is added and then
	// removed continously.
	size_t alloc;
	if (arr->alloc > 0 && size > arr->alloc / 4) {
		alloc = arr->alloc;
	} else {
		alloc = 16;
	}

	while (alloc < size) {
		alloc *= 2;
	}

	if (alloc == arr->alloc) {
		return true;
	}

	void *data = realloc(arr->data, alloc);
	if (data == NULL) {
		return false;
	}
	arr->data = data;
	arr->alloc = alloc;
	return true;
}
