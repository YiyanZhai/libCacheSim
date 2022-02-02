
#include "segSel.h"
#include "learned.h"
#include "segment.h"
#include "utils.h"

static inline int cmp_seg(const void *p1, const void *p2) {
  segment_t *seg1 = *(segment_t **) p1;
  segment_t *seg2 = *(segment_t **) p2;

  DEBUG_ASSERT(seg1->magic == MAGIC);

  if (seg1->utility < seg2->utility) return -1;
  else
    return 1;
}

static inline void examine_ranked_seg(cache_t *cache) {
  L2Cache_params_t *params = cache->eviction_params;
  segment_t **ranked_segs = params->seg_sel.ranked_segs;

#ifdef dump_ranked_seg_frac
  printf("curr time %ld %ld: ranked segs ", (long) params->curr_rtime,
         (long) params->curr_vtime);
  for (int d = 0; d < params->n_segs * dump_ranked_seg_frac; d++) {
    printf("%d,", ranked_segs[d]->seg_id);
  }
  printf("\n");
#endif

  if (params->learner.n_train > 0) {
    for (int i = 0; i < 20; i++) {
      print_seg(cache, ranked_segs[i], DEBUG_LEVEL);
    }
    printf("\n\n");
    for (int i = 2000; i < 2020; i++) {
      print_seg(cache, ranked_segs[i], DEBUG_LEVEL);
    }
    printf("\n\n");
    for (int i = 8000; i < 8020; i++) {
      print_seg(cache, ranked_segs[i], DEBUG_LEVEL);
    }
    printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
  }
}

void rank_segs(cache_t *cache) {
  L2Cache_params_t *params = cache->eviction_params;
  segment_t **ranked_segs = params->seg_sel.ranked_segs;
  int32_t *ranked_seg_pos_p = &(params->seg_sel.ranked_seg_pos);

  if (params->type == LOGCACHE_LEARNED) {
    inference(cache);
  }

  int n_seg = 0;
  segment_t *curr_seg;
  for (int i = 0; i < MAX_N_BUCKET; i++) {
    // int j_th_seg = 0;
    curr_seg = params->buckets[i].first_seg;
    while (curr_seg) {
      if (!is_seg_evictable(curr_seg, 1)
          || params->buckets[curr_seg->bucket_idx].n_seg < params->n_merge + 1) {
        curr_seg->utility = INT32_MAX;
      } else {
        if (params->type == LOGCACHE_LEARNED) {
          // curr_seg->utilization = 0;
        } else if (params->type == LOGCACHE_LOG_ORACLE) {
          curr_seg->utility =
              cal_seg_penalty(cache, params->obj_score_type, curr_seg, params->n_retain_per_seg,
                              params->curr_rtime, params->curr_vtime);
        } else if (params->type == LOGCACHE_BOTH_ORACLE) {
          curr_seg->utility =
              cal_seg_penalty(cache, OBJ_SCORE_ORACLE, curr_seg, params->n_retain_per_seg,
                              params->curr_rtime, params->curr_vtime);
          // if (params->n_segs < 200)
          //   printf("seg %d,%d/%d, oracle score %lf\n", i, j_th_seg++, params->n_segs, curr_seg->utility);
        } else {
          abort();
        }
      }

      ranked_segs[n_seg++] = curr_seg;
      curr_seg = curr_seg->next_seg;
    }
  }
  DEBUG_ASSERT(n_seg == params->n_segs);

  DEBUG_ASSERT(params->type == LOGCACHE_LOG_ORACLE || params->type == LOGCACHE_BOTH_ORACLE
               || params->type == LOGCACHE_LEARNED);
  qsort(ranked_segs, n_seg, sizeof(segment_t *), cmp_seg);

  params->seg_sel.last_rank_time = params->n_evictions;
  params->seg_sel.ranked_seg_pos = 0;
}

/** we need to find segments from the same bucket to merge, 
 * this is used when we do not require the merged segments to be consecutive 
 */
static inline int find_next_qualified_seg(segment_t **ranked_segs, int start_pos, int end_pos,
                                          int bucket_idx) {
  for (int i = start_pos; i < end_pos; i++) {
    if (ranked_segs[i] != NULL) {
      if (bucket_idx == -1 || ranked_segs[i]->bucket_idx == bucket_idx) {
        return i;
      }
    }
  }
  return -1;
}

/* choose the next segment to evict, use FIFO to choose bucket, 
 * and FIFO within bucket */
bucket_t *select_segs_fifo(cache_t *cache, segment_t **segs) {
  L2Cache_params_t *params = cache->eviction_params;

  int n_scanned_bucket = 0; 
  bucket_t *bucket = &params->buckets[params->curr_evict_bucket_idx];
  segment_t *seg_to_evict = bucket->next_seg_to_evict;
  if (seg_to_evict == NULL) {
    seg_to_evict = bucket->first_seg;
  }

  while (!is_seg_evictable(seg_to_evict, params->n_merge)) {
    bucket->next_seg_to_evict = bucket->first_seg;
    params->curr_evict_bucket_idx = (params->curr_evict_bucket_idx + 1) % MAX_N_BUCKET;
    bucket = &params->buckets[params->curr_evict_bucket_idx];
    seg_to_evict = bucket->next_seg_to_evict;
    if (seg_to_evict == NULL) {
      seg_to_evict = bucket->first_seg;
    }

    n_scanned_bucket += 1; 

    if (n_scanned_bucket > MAX_N_BUCKET + 1) {
      DEBUG("no evictable seg found\n");
      abort(); 
      segs[0] = NULL; 
      return NULL; 
    }
  }

  for (int i = 0; i < params->n_merge; i++) {
    DEBUG_ASSERT(seg_to_evict->bucket_idx == bucket->bucket_idx);
    segs[i] = seg_to_evict;
    seg_to_evict = seg_to_evict->next_seg;
  }

  // TODO: this is not good enough, we should not merge till the end 
  if (bucket->n_seg > params->n_merge + 1) {
    bucket->next_seg_to_evict = seg_to_evict;
  } else {
    bucket->next_seg_to_evict = NULL;
    params->curr_evict_bucket_idx = (params->curr_evict_bucket_idx + 1) % MAX_N_BUCKET;
  }

  return bucket;
}

/* choose bucket with probability being the size of the bucket, 
 * uses FIFO to choose within bucket */
bucket_t *select_segs_weighted_fifo(cache_t *cache, segment_t **segs) {
  L2Cache_params_t *params = cache->eviction_params;

  segment_t *seg_to_evict = NULL;
  bucket_t *bucket = NULL;

  int n_checked_seg = 0;
  // TODO:

  return bucket;
}

/* select segment randomly, we choose bucket in fifo order, 
 * and randomly within the bucket */
bucket_t *select_segs_rand(cache_t *cache, segment_t **segs) {
  L2Cache_params_t *params = cache->eviction_params;

  bucket_t *bucket = NULL;
  segment_t *seg_to_evict = NULL;

  int n_checked_seg = 0;
  while (!is_seg_evictable(seg_to_evict, params->n_merge)) {
    params->curr_evict_bucket_idx = (params->curr_evict_bucket_idx + 1) % MAX_N_BUCKET;
    bucket = &params->buckets[params->curr_evict_bucket_idx];

    int n_th = rand() % (bucket->n_seg - params->n_merge);
    seg_to_evict = bucket->first_seg;
    for (int i = 0; i < n_th; i++) seg_to_evict = seg_to_evict->next_seg;

    n_checked_seg += 1;
    DEBUG_ASSERT(n_checked_seg <= params->n_segs * 2);
  }

  for (int i = 0; i < params->n_merge; i++) {
    segs[i] = seg_to_evict;
    seg_to_evict = seg_to_evict->next_seg;
  }
  bucket->next_seg_to_evict = seg_to_evict;

  return bucket;
}

/* choose which segment to evict, segs store the segments to evict */
bucket_t *select_segs_learned(cache_t *cache, segment_t **segs) {
  L2Cache_params_t *params = cache->eviction_params;
  seg_sel_t *ss = &params->seg_sel;

  bool array_resized = false;

  /* setup function */
  if (unlikely(ss->ranked_seg_size < params->n_segs)) {
    if (ss->ranked_segs != NULL) {
      my_free(sizeof(segment_t *) * ranked_seg_size, ss->ranked_segs);
    }
    ss->ranked_seg_size = params->n_segs * 2;
    ss->ranked_segs = my_malloc_n(segment_t *, params->n_segs * 2);
    array_resized = true;
  }

  segment_t **ranked_segs = ss->ranked_segs;
  int32_t *ranked_seg_pos_p = &(ss->ranked_seg_pos);

  /* rerank every rerank_intvl_evictions evictions */
  int rerank_intvl_evictions = params->n_segs * params->rank_intvl;
  rerank_intvl_evictions = MAX(rerank_intvl_evictions, 1); 
  if (params->n_evictions - ss->last_rank_time >= rerank_intvl_evictions || array_resized) {
    rank_segs(cache);
  }

  assert(params->type != SEGCACHE);
  int i, j;
start:
  i = 0;
  // TODO: use per bucket ranked segments
  j = find_next_qualified_seg(ranked_segs, *ranked_seg_pos_p, params->n_segs / 4 + 1, -1);
  DEBUG_ASSERT(j != -1);

  segs[i++] = ranked_segs[j];
  ranked_segs[j] = NULL;
  *ranked_seg_pos_p = j + 1;

  if (j < params->n_segs / 4) {
    while (i < params->n_merge) {
      j = find_next_qualified_seg(ranked_segs, j + 1, params->n_segs / 2 + 1,
                                  segs[0]->bucket_idx);
      if (j == -1) {
        goto start;
      }
      segs[i++] = ranked_segs[j];
      ranked_segs[j] = NULL;
    }
    DEBUG_ASSERT(i == params->n_merge);
    DEBUG_ASSERT(segs[0]->bucket_idx == segs[1]->bucket_idx);
    DEBUG_ASSERT(segs[0]->next_seg != NULL);
    DEBUG_ASSERT(segs[1]->next_seg != NULL);

    return &params->buckets[segs[0]->bucket_idx];
  } else {
    INFO("cache size %ld MiB, %d segs, current ranked pos %d, hard find a matching seg, "
         "please increase cache size or reduce segment size\n",
         (long) (cache->cache_size / MiB), params->n_segs, *ranked_seg_pos_p);
    print_bucket(cache);
    for (int m = 0; m < params->n_segs; m++) {
      if (ranked_segs[m])
        printf("seg %d utility %lf, mean obj size %ld\n", m, ranked_segs[m]->utility,
               ranked_segs[m]->n_byte / ranked_segs[m]->n_obj);
      else
        printf("seg %d NULL\n", m);
    }
    ss->last_rank_time = 0;
    return select_segs_learned(cache, segs);
  }
  // } else {
  //   /* it is non-trivial to determine the bucket of segcache */
  //   segs[0] = ss->ranked_segs[ss->ranked_seg_pos];
  //   ss->ranked_segs[ss->ranked_seg_pos] = NULL;
  //   ss->ranked_seg_pos++;

  //   ERROR("do not support\n");
  //   abort();
  // }
}