#pragma once

#include "../../include/libCacheSim/evictionAlgo/L2Cache.h"
#include "bucket.h"
#include "obj.h"
#include "segment.h"
#include "const.h"


/************* feature *****************/ 
void seg_feature_shift(L2Cache_params_t *params, segment_t *seg); 

void seg_hit(L2Cache_params_t *params, cache_obj_t *cache_obj); 

void update_train_y(L2Cache_params_t *params, cache_obj_t *cache_obj); 


/************* training *****************/ 
#if TRAINING_DATA_SOURCE == TRAINING_X_FROM_EVICTION
void transform_seg_to_training(cache_t *cache, bucket_t *bucket,
                                             segment_t *segment);
#endif

/************* inference *****************/ 




// void create_data_holder(cache_t *cache);

// void create_data_holder2(cache_t *cache);

void snapshot_segs_to_training_data(cache_t *cache);

void train(cache_t *cache);

void inference(cache_t *cache);
