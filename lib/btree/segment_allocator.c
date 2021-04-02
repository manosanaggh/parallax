#include <assert.h>
#include <stdlib.h>
#include <signal.h>
#include <log.h>
#include "segment_allocator.h"
#include "conf.h"
#include "../allocator/allocator.h"

extern uint64_t MAPPED;

struct link_segments_metadata {
	level_descriptor *level_desc;
	segment_header *new_segment;
	uint64_t segment_id;
	uint64_t available_space;
	uint64_t tree_id;
	int in_mem;
};

static uint64_t link_memory_segments(struct link_segments_metadata *req)
{
	level_descriptor *level_desc = req->level_desc;
	segment_header *new_segment = req->new_segment;
	segment_header *prev_segment;
	uint64_t available_space = req->available_space;
	uint64_t segment_id = req->segment_id;
	uint8_t tree_id = req->tree_id;

	(void)prev_segment;
	if (req->level_desc->offset[req->tree_id] != 0) {
		/*chain segments*/
		prev_segment = level_desc->last_segment[tree_id];
		new_segment->next_segment = NULL;
		new_segment->prev_segment = (segment_header *)ABSOLUTE_ADDRESS(level_desc->last_segment[tree_id]);
		level_desc->last_segment[tree_id]->next_segment = (segment_header *)ABSOLUTE_ADDRESS(new_segment);
		prev_segment = level_desc->last_segment[tree_id];
		level_desc->last_segment[tree_id] = new_segment;
		level_desc->last_segment[tree_id]->segment_id = segment_id + 1;
		level_desc->offset[tree_id] += (available_space + sizeof(segment_header));
	} else {
		/*special case for the first segment for this level*/
		new_segment->next_segment = NULL;
		new_segment->prev_segment = NULL;
		level_desc->first_segment[tree_id] = new_segment;
		level_desc->last_segment[tree_id] = new_segment;
		level_desc->last_segment[tree_id]->segment_id = 1;
		level_desc->offset[tree_id] = sizeof(segment_header);
		prev_segment = NULL;
	}

	new_segment->in_mem = req->in_mem;

	return level_desc->offset[tree_id] % SEGMENT_SIZE;
}

static void set_link_segments_metadata(struct link_segments_metadata *req, segment_header *new_segment,
				       uint64_t segment_id, uint64_t available_space)
{
	req->new_segment = new_segment;
	req->segment_id = segment_id;
	req->available_space = available_space;
}

static void *get_space(volume_descriptor *volume_desc, level_descriptor *level_desc, uint8_t tree_id, uint32_t size)
{
	struct link_segments_metadata req = { .level_desc = level_desc, .tree_id = tree_id };
	segment_header *new_segment = NULL;
	node_header *node = NULL;
	uint32_t available_space;
	uint64_t offset_in_segment = 0;
	uint64_t segment_id;

	MUTEX_LOCK(&level_desc->level_allocation_lock);

	/*check if we have enough space to satisfy the request*/
	if (level_desc->offset[tree_id] % SEGMENT_SIZE == 0) {
		available_space = 0;
		segment_id = 0;
	} else {
#if 0
	/*  else if (level_desc->offset[tree_id] % SEGMENT_SIZE != 0) { */
	/* 	offset_in_segment = level_desc->offset[tree_id] % SEGMENT_SIZE; */
	/* 	available_space = SEGMENT_SIZE - offset_in_segment; */
	/* 	segment_id = level_desc->last_segment[tree_id]->segment_id; */
	/* } else { */
	/* 	available_space = 0; */
	/* 	segment_id = level_desc->last_segment[tree_id]->segment_id; */
	/* } */
#endif
		available_space = SEGMENT_SIZE - (level_desc->offset[tree_id] % SEGMENT_SIZE);
		offset_in_segment = level_desc->offset[tree_id] % SEGMENT_SIZE;
	}
	if (available_space < size) {
		/*we need to go to the actual allocator to get space*/
		if (level_desc->level_id != 0) {
			MUTEX_LOCK(&volume_desc->bitmap_lock);
			new_segment = (segment_header *)allocate(volume_desc, SEGMENT_SIZE);
			MUTEX_UNLOCK(&volume_desc->bitmap_lock);
			req.in_mem = 0;
		} else {
			if (posix_memalign((void **)&new_segment, SEGMENT_SIZE, SEGMENT_SIZE) != 0) {
				log_fatal("MEMALIGN FAILED");
				exit(EXIT_FAILURE);
			}
			req.in_mem = 1;
		}

		assert(new_segment);
		set_link_segments_metadata(&req, new_segment, segment_id, available_space);
		offset_in_segment = link_memory_segments(&req);

		/* offset_in_segment = level_desc->offset[tree_id] % SEGMENT_SIZE; */
	}
	/* if(level_desc->level_id != 0) */
	/* 	log_info("offset in segment %llu level_id %d tree_id %d available space%llu alloc_size %llu", */
	/* 		 offset_in_segment, level_desc->level_id, tree_id, available_space, size); */

	node = (node_header *)((uint64_t)level_desc->last_segment[tree_id] + offset_in_segment);
	assert(node);
	level_desc->offset[tree_id] += size;
	MUTEX_UNLOCK(&level_desc->level_allocation_lock);
	return node;
}

index_node *seg_get_index_node(volume_descriptor *volume_desc, level_descriptor *level_desc, uint8_t tree_id,
			       char reason)
{
	index_node *ptr;
	IN_log_header *bh;

	ptr = (index_node *)get_space(volume_desc, level_desc, tree_id, INDEX_NODE_SIZE + KEY_BLOCK_SIZE);

	if (reason == NEW_ROOT)
		ptr->header.type = rootNode;
	else
		ptr->header.type = internalNode;

	ptr->header.epoch = volume_desc->mem_catalogue->epoch;
	ptr->header.num_entries = 0;
	ptr->header.fragmentation = 0;

	/*private key log for index nodes*/
	bh = (IN_log_header *)((uint64_t)ptr + INDEX_NODE_SIZE);
	bh->next = (void *)NULL;
	ptr->header.first_IN_log_header = (IN_log_header *)((uint64_t)bh - MAPPED);
	ptr->header.last_IN_log_header = ptr->header.first_IN_log_header;
	ptr->header.key_log_size = sizeof(IN_log_header);

	return ptr;
}

index_node *seg_get_index_node_header(volume_descriptor *volume_desc, level_descriptor *level_desc, uint8_t tree_id)
{
	return (index_node *)get_space(volume_desc, level_desc, tree_id, INDEX_NODE_SIZE);
}

IN_log_header *seg_get_IN_log_block(volume_descriptor *volume_desc, level_descriptor *level_desc, uint8_t tree_id)
{
	return (IN_log_header *)get_space(volume_desc, level_desc, tree_id, KEY_BLOCK_SIZE);
}

void seg_free_index_node_header(volume_descriptor *volume_desc, level_descriptor *level_desc, uint8_t tree_id,
				node_header *node)
{
	return;
	//leave for future use
	(void)level_desc;
	(void)tree_id;

	free_block(volume_desc, node, INDEX_NODE_SIZE);
}

void seg_free_index_node(volume_descriptor *volume_desc, level_descriptor *level_desc, uint8_t tree_id,
			 index_node *inode)
{
	return;
	//leave for future use
	(void)level_desc;
	(void)tree_id;

	if (inode->header.type == leafNode || inode->header.type == leafRootNode) {
		log_fatal("Faulty type of node!");
		exit(EXIT_FAILURE);
	}

	/*for IN, BIN, root nodes free the key log as well*/
	if (inode->header.first_IN_log_header == NULL) {
		log_fatal("NULL log for index?");
		exit(EXIT_FAILURE);
	}
	IN_log_header *curr = (IN_log_header *)(MAPPED + (uint64_t)inode->header.first_IN_log_header);
	IN_log_header *last = (IN_log_header *)(MAPPED + (uint64_t)inode->header.last_IN_log_header);
	IN_log_header *to_free;
	while ((uint64_t)curr != (uint64_t)last) {
		to_free = curr;
		curr = (IN_log_header *)((uint64_t)MAPPED + (uint64_t)curr->next);
		free_block(volume_desc, to_free, KEY_BLOCK_SIZE);
	}
	free_block(volume_desc, last, KEY_BLOCK_SIZE);
	/*finally node_header*/
	free_block(volume_desc, inode, INDEX_NODE_SIZE);
	return;
}

leaf_node *seg_get_leaf_node(volume_descriptor *volume_desc, level_descriptor *level_desc, uint8_t tree_id)
{
	leaf_node *leaf = (leaf_node *)get_space(volume_desc, level_desc, tree_id, level_desc->leaf_size);

	leaf->header.type = leafNode;
	leaf->header.epoch = volume_desc->mem_catalogue->epoch;
	leaf->header.num_entries = 0;
	leaf->header.fragmentation = 0;

	leaf->header.first_IN_log_header = NULL; /*unused field in leaves*/
	leaf->header.last_IN_log_header = NULL; /*unused field in leaves*/
	leaf->header.key_log_size = 0; /*unused also*/
	leaf->header.height = 0;

	return leaf;
}

struct bt_dynamic_leaf_node *seg_get_dynamic_leaf_node(volume_descriptor *volume_desc, level_descriptor *level_desc,
						       uint8_t tree_id)
{
	/*Pass tree_id in get_space*/
	struct bt_dynamic_leaf_node *leaf = get_space(volume_desc, level_desc, tree_id, level_desc->leaf_size);

	leaf->header.type = leafNode;
	leaf->header.epoch = volume_desc->mem_catalogue->epoch;
	leaf->header.num_entries = 0;
	leaf->header.fragmentation = 0;

	leaf->header.first_IN_log_header = NULL; /*unused field in leaves*/
	leaf->header.last_IN_log_header = NULL; /*unused field in leaves*/
	leaf->header.leaf_log_size = 0;
	leaf->header.height = 0;

	return leaf;
}

leaf_node *seg_get_leaf_node_header(volume_descriptor *volume_desc, level_descriptor *level_desc, uint8_t tree_id)
{
	struct bt_dynamic_leaf_node *leaf = get_space(volume_desc, level_desc, tree_id, level_desc->leaf_size);
	leaf->header.type = leafNode;
	leaf->header.epoch = volume_desc->mem_catalogue->epoch;
	leaf->header.num_entries = 0;
	leaf->header.fragmentation = 0;

	leaf->header.first_IN_log_header = NULL; /*unused field in leaves*/
	leaf->header.last_IN_log_header = NULL; /*unused field in leaves*/
	leaf->header.leaf_log_size = 0;
	leaf->header.height = 0;

	return (leaf_node *)leaf;
}

void seg_free_leaf_node(volume_descriptor *volume_desc, level_descriptor *level_desc, uint8_t tree_id, leaf_node *leaf)
{
	return;
	//leave for future use
	(void)level_desc;
	(void)tree_id;
	free_block(volume_desc, leaf, level_desc->leaf_size);
}

segment_header *seg_get_raw_log_segment(volume_descriptor *volume_desc)
{
	segment_header *sg;
	MUTEX_LOCK(&volume_desc->bitmap_lock);
	sg = (segment_header *)allocate(volume_desc, SEGMENT_SIZE);
	MUTEX_UNLOCK(&volume_desc->bitmap_lock);
	return sg;
}

void free_raw_segment(volume_descriptor *volume_desc, segment_header *segment)
{
	free_block(volume_desc, segment, SEGMENT_SIZE);
	return;
}

void *get_space_for_system(volume_descriptor *volume_desc, uint32_t size, int lock)
{
	void *addr;
	if (size % 4096 != 0) {
		log_fatal("faulty size %lu not a multiple of 4KB", size);
		raise(SIGINT);
		exit(EXIT_FAILURE);
	}

	segment_header *new_segment = NULL;
	segment_header *first_sys_segment;
	segment_header *last_sys_segment;
	uint64_t available_space;
	uint64_t offset_in_segment = 0;
	uint64_t segment_id;

	if (lock)
		MUTEX_LOCK(&volume_desc->bitmap_lock);

	first_sys_segment = (segment_header *)(MAPPED + volume_desc->mem_catalogue->first_system_segment);
	last_sys_segment = (segment_header *)(MAPPED + volume_desc->mem_catalogue->last_system_segment);
	/*check if we have enough space to satisfy the request*/

	if (volume_desc->mem_catalogue->offset == 0) {
		available_space = 0;
		segment_id = 0;
	} else if (volume_desc->mem_catalogue->offset % SEGMENT_SIZE != 0) {
		offset_in_segment = volume_desc->mem_catalogue->offset % SEGMENT_SIZE;
		available_space = SEGMENT_SIZE - offset_in_segment;
		segment_id = last_sys_segment->segment_id;
	} else {
		available_space = 0;
		segment_id = last_sys_segment->segment_id;
	}
	//log_info("available %llu volume offset %llu", available_space, volume_desc->mem_catalogue->offset);
	if (available_space < size) {
		/*we need to go to the actual allocator to get space*/

		new_segment = (segment_header *)allocate(volume_desc, SEGMENT_SIZE);

		if (segment_id) {
			/*chain segments*/
			new_segment->next_segment = NULL;
			new_segment->prev_segment = (segment_header *)((uint64_t)last_sys_segment - MAPPED);
			last_sys_segment->next_segment = (segment_header *)((uint64_t)new_segment - MAPPED);
			last_sys_segment = new_segment;
			last_sys_segment->segment_id = segment_id + 1;
			volume_desc->mem_catalogue->offset += (available_space + sizeof(segment_header));
		} else {
			/*special case for the first segment for this level*/
			new_segment->next_segment = NULL;
			new_segment->prev_segment = NULL;
			first_sys_segment = new_segment;
			last_sys_segment = new_segment;
			last_sys_segment->segment_id = 1;
			volume_desc->mem_catalogue->offset = sizeof(segment_header);
		}
		offset_in_segment = volume_desc->mem_catalogue->offset % SEGMENT_SIZE;
		/*serialize the updated info of first, last system segments*/
		volume_desc->mem_catalogue->first_system_segment = (uint64_t)first_sys_segment - MAPPED;
		volume_desc->mem_catalogue->last_system_segment = (uint64_t)last_sys_segment - MAPPED;
	}

	addr = (void *)(uint64_t)last_sys_segment + offset_in_segment;
	volume_desc->mem_catalogue->offset += size;

	if (lock)
		MUTEX_UNLOCK(&volume_desc->bitmap_lock);
	return addr;
}

void seg_free_level(db_handle *handle, uint8_t level_id, uint8_t tree_id)
{
	segment_header *curr_segment = handle->db_desc->levels[level_id].first_segment[tree_id];
	segment_header *temp_segment;
	db_descriptor *db_desc = handle->db_desc;
	uint64_t space_freed = 0;

	log_info("Freeing up level %u for db %s", level_id, handle->db_desc->db_name);

	if (level_id != 0)
		for (; curr_segment->next_segment != NULL; curr_segment = REAL_ADDRESS(curr_segment->next_segment)) {
			free_raw_segment(handle->volume_desc, curr_segment);
			space_freed += SEGMENT_SIZE;
		}
	else {
		curr_segment = handle->db_desc->inmem_medium_log_head[tree_id];
		segment_header *next_segment;
		if (curr_segment) {
			while (curr_segment->next_segment) {
				next_segment = REAL_ADDRESS(curr_segment->next_segment);
				free(curr_segment);
				curr_segment = next_segment;
			}

			free(curr_segment);
			segment_header *head = malloc(SEGMENT_SIZE);
			//(segment_header *)mspace_memalign(db_desc->inmem_msp, SEGMENT_SIZE, SEGMENT_SIZE);
			assert(head != NULL);
			head->next_segment = NULL;
			head->prev_segment = NULL;
			head->segment_id = 0;
			db_desc->inmem_medium_log_head[tree_id] = head;
			db_desc->inmem_medium_log_tail[tree_id] = head;
			db_desc->inmem_medium_log_size[tree_id] = sizeof(segment_header);
		}

		curr_segment = handle->db_desc->levels[level_id].first_segment[tree_id];
		temp_segment = REAL_ADDRESS(curr_segment->next_segment);
		int flag = 0;
		/* log_info("Level id to free %d %d", level_id,curr_segment->in_mem); */

		if (curr_segment->next_segment) {
			while (curr_segment != NULL) {
				/* log_info("COUNT  %d %llu", curr_segment->segment_id, curr_segment->next_segment); */
				assert(curr_segment->in_mem);
				free(curr_segment);
				curr_segment = temp_segment;

				if (temp_segment->next_segment == NULL) {
					flag = 1;
					break;
				}
				temp_segment = REAL_ADDRESS(temp_segment->next_segment);
				space_freed += SEGMENT_SIZE;
			}
		} else
			free(curr_segment);

		if (flag) {
			/* log_info("COUNT %d %llu", curr_segment->segment_id, curr_segment->next_segment); */
			free(temp_segment);
		}
#if 0
		log_info("HELLO");
		if(!run){
			for(;curr_segment->next_segment != NULL;curr_segment = REAL_ADDRESS(temp_segment)){
				temp_segment = curr_segment->next_segment;
				free(curr_segment);
				space_freed += SEGMENT_SIZE;
			}
		}
#endif
	}
	log_info("Freed space %llu MB from db:%s level tree [%u][%u]", space_freed / (1024 * 1024),
		 handle->db_desc->db_name, level_id, tree_id);
	/*assert check
						if(db_desc->spilled_keys != db_desc->total_keys[spill_req->src_tree_id]){
						printf("[%s:%s:%d] FATAL keys missing --- spilled keys %llu actual %llu spiller id %d\n",__FILE__,__func__,__LINE__,(LLU)db_desc->spilled_keys,(LLU)db_desc->total_keys[spill_req->src_tree_id], spill_req->src_tree_id);
						exit(EXIT_FAILURE);
						}*/
	/*buffered tree out*/
	handle->db_desc->levels[level_id].level_size[tree_id] = 0;
	handle->db_desc->levels[level_id].first_segment[tree_id] = NULL;
	handle->db_desc->levels[level_id].last_segment[tree_id] = NULL;
	handle->db_desc->levels[level_id].offset[tree_id] = 0;
	handle->db_desc->levels[level_id].root_r[tree_id] = NULL;
	handle->db_desc->levels[level_id].root_w[tree_id] = NULL;
	/* if (curr_segment->in_mem == 0) */
	/* 	free_raw_segment(handle->volume_desc, curr_segment); */
	/* if (level_id == 0) */
	/* 	if (RWLOCK_UNLOCK(&db_desc->levels[0].guard_of_level.rx_lock)) { */
	/* 		exit(EXIT_FAILURE); */
	/* 	} */
	log_info("Freed space %llu MB from db:%s level %u", space_freed / MB(1), handle->db_desc->db_name, level_id);
}
