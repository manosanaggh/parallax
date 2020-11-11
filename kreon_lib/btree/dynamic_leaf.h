#pragma once
#include "btree.h"
#include "delete.h"
#include "segment_allocator.h"

enum bt_dynamic_leaf_operation { DYNAMIC_LEAF_INSERT = 0, DYNAMIC_LEAF_FIND = 1 };

enum kv_entry_location { KV_INPLACE = 0, KV_INLOG = 1 };

struct find_result {
	char *kv;
	enum kv_entry_location key_type;
};

struct dl_bsearch_result {
	int middle;
	enum bsearch_status status;
	enum bt_dynamic_leaf_operation op;
};

int8_t insert_in_dynamic_leaf(struct bt_dynamic_leaf_node *leaf, bt_insert_req *req, level_descriptor *level);
struct find_result find_key_in_dynamic_leaf(const struct bt_dynamic_leaf_node *leaf, uint32_t leaf_size, void *key,
					    uint32_t key_size);
void binary_search_dynamic_leaf(const struct bt_dynamic_leaf_node *leaf, uint32_t leaf_size, struct splice *key_buf,
				struct dl_bsearch_result *result);

int check_dynamic_leaf_split(struct bt_dynamic_leaf_node *leaf, uint32_t leaf_size, uint32_t kv_size,
			     enum kv_entry_location key_type);
struct bt_rebalance_result split_dynamic_leaf(struct bt_dynamic_leaf_node *leaf, uint32_t leaf_size,
					      bt_insert_req *req);
struct bt_dynamic_leaf_slot_array *get_slot_array_offset(const struct bt_dynamic_leaf_node *leaf);
char *get_kv_offset(const struct bt_dynamic_leaf_node *leaf, uint32_t leaf_size, uint32_t kv_offset);
