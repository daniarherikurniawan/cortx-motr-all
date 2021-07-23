/* -*- C -*- */
/*
 * Copyright (c) 2020 Seagate Technology LLC and/or its Affiliates
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * For any questions about this software or licensing,
 * please email opensource@seagate.com or cortx-questions@seagate.com.
 *
 */

/*
 * Example Motr application to create object, write to object,
 * read from objet, and then delete the object.
 */

/*
 * Please change the dir according to you development environment.
 *
 * How to build:
 * gcc -I/mnt/extra/cortx-motr -I/mnt/extra/cortx-motr/extra-libs/galois/include \
 *      -DM0_EXTERN=extern -DM0_INTERNAL= -Wno-attributes \
 *      -L/mnt/extra/cortx-motr/motr/.libs -lmotr  -lpthread example_mt_bench.c \
 *      -o example_mt_bench
 *
 * Please change the configuration according to you development environment.
 *
 * How to run:
 * LD_LIBRARY_PATH=/work/cortx-motr/motr/.libs/                              \
 * sudo ./example_mt_bench 10.52.2.250@tcp:12345:34:1 10.52.2.250@tcp:12345:33:1001 \
 *      "<0x7000000000000001:0>" "<0x7200000000000001:64>" 12349741 1 0 0 10 1000 30
 *            # args : 1        0        0          10      1000       30 
 *            #     <write?> <read?> <delete?> <layout_id> <n_req> <n_parallel>
 */

#include "motr/client.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/types.h>


typedef struct {
    int id;
    char *base_obj_id;
    /* latency in ms */
    float write_latency;
    float read_latency;
} Arg;

struct m0_config motr_conf;
struct m0_client *m0_instance = NULL;

/* updated by update_n_block() */
int N_BLOCK = 1;
const static char * OUTPUT_DIR = "out_mt_bench";
const static int MAX_PATH_LEN = 100;
const static char * USER_NAME = "daniar"; // a valid username on the server

/* 0 == false ; 1 == true ; provided by argv[6], argv[7], argv[8] */
int progress_mod = 20;
const static int ENABLE_LATENCY_LOG = 0; 
int ENABLE_WRITE = 0;
int ENABLE_READ = 0; /* erase Page Cache: free -h; sync; echo 1 > /proc/sys/vm/drop_caches; free -h */
int ENABLE_DELETE = 0;
sem_t n_thread_sem;
sem_t max_thd_queue_sem;
/* Parallelishm: number of thd allowed in a sema space */
int N_THD_SEMA = 4;
/* 1 Thread == 1 Request == has N_BLOCK (each block has BLOCK_SIZE KB) */
int N_THREAD = 0; // will be provided by argv[10]

/* provided by argv[9]*/
int LAYOUT_ID =  9,     BLOCK_SIZE =  1048576; // 1MB 
/*
 *  LAYOUT_ID               (Ideal size)
 *      1,     BLOCK_SIZE =     4096; // 4KB
 *      2,     BLOCK_SIZE =     8192; // 8KB
 *      3,     BLOCK_SIZE =    16384; // 16KB
 *      4,     BLOCK_SIZE =    32768; // 32KB
 *      5,     BLOCK_SIZE =    65536; // 64KB
 *      6,     BLOCK_SIZE =   131072; // 128KB
 *      7,     BLOCK_SIZE =   262144; // 256KB
 *      8,     BLOCK_SIZE =   524288; // 512KB
 *      9,     BLOCK_SIZE =  1048576; // 1MB
 *     10,     BLOCK_SIZE =  2097152; // 2MB
 *     11,     BLOCK_SIZE =  4194304; // 4MB
 *     12,     BLOCK_SIZE =  8388608; // 8MB
 *     13,     BLOCK_SIZE = 16777216; // 16MB
 *     14,     BLOCK_SIZE = 33554432; // 32MB (got error [be/engine.c:312:be_engine_got_tx_open]  tx=0x7f8be8027148 engine=0x7ffcd2d93830 t_prepared=(385285,115850973) t_payload_prepared=131072 bec_tx_size_max=(262144,100663296) bec_tx_payload_max=2097152)
 */

static int update_n_block(){
    /* payload per request = block_size */
    return 1;
}

/* return ideal block size */
static int get_block_size(int layout_id){
    if (layout_id == 1)
        return 4096;
    else if (layout_id == 2)
        return 8192;
    else if (layout_id == 3)
        return 16384;
    else if (layout_id == 4)
        return 32768;
    else if (layout_id == 5)
        return 65536; // 64KB
    else if (layout_id == 6)
        return 131072; // 128KB
    else if (layout_id == 7)
        return 262144; // 256KB
    else if (layout_id == 8)
        return 524288; // 512KB
    else if (layout_id == 9)
        return 1048576; // 1MB
    else if (layout_id == 10)
        return 2097152; // 2MB
    else if (layout_id == 11)
        return 4194304; // 4MB
    else if (layout_id == 12)
        return 8388608; // 8MB
    else if (layout_id == 13)
        return 16777216; // 16MB
    else 
        printf("ERROR: Layout id (%d) is too big!\n", layout_id);
}

static int object_create(struct m0_container *container, struct m0_uint128 obj_id) {
	struct m0_obj     obj;
	struct m0_client *instance;
	struct m0_op     *ops[1] = {NULL};
	int               rc;

	M0_SET0(&obj);
	instance = container->co_realm.re_instance;
	m0_obj_init(&obj, &container->co_realm, &obj_id,
		    LAYOUT_ID);

	rc = m0_entity_create(NULL, &obj.ob_entity, &ops[0]);
	if (rc != 0) {
		printf("Failed to create object: %d\n", rc);
		return rc;
	}

	m0_op_launch(ops, 1);
	rc = m0_op_wait(ops[0],
			M0_BITS(M0_OS_FAILED, M0_OS_STABLE),
			M0_TIME_NEVER);
	if (rc == 0)
		rc = ops[0]->op_rc;

	m0_op_fini(ops[0]);
	m0_op_free(ops[0]);
	ops[0] = NULL;

	m0_entity_fini(&obj.ob_entity);

	// printf("Object (id=%lu) creation result: %d\n",
	//        (unsigned long)obj_id.u_lo, rc);
	return rc;
}

static int object_open(struct m0_obj *obj, struct m0_uint128 obj_id) {
	struct m0_op *ops[1] = {NULL};
	int           rc;

	rc = m0_entity_open(&obj->ob_entity, &ops[0]);
	if (rc != 0) {
		printf("Failed to open object: %d\n", rc);
		return rc;
	}

	m0_op_launch(ops, 1);
	rc = m0_op_wait(ops[0],
			M0_BITS(M0_OS_FAILED, M0_OS_STABLE),
			M0_TIME_NEVER);
	if (rc == 0)
		rc = ops[0]->op_rc;

	m0_op_fini(ops[0]);
	m0_op_free(ops[0]);
	ops[0] = NULL;

	/* obj is valid if open succeeded */
	// printf("Object (id=%lu) open result: %d\n",
	//        (unsigned long)obj_id.u_lo, rc);
	return rc;
}

static int alloc_vecs(struct m0_indexvec *ext,
		      struct m0_bufvec   *data,
		      struct m0_bufvec   *attr,
		      uint32_t            block_count,
		      uint32_t            block_size) {
	int      rc;

	rc = m0_indexvec_alloc(ext, block_count);
	if (rc != 0)
		return rc;

	/*
	 * this allocates <block_count> * <block_size>  buffers for data,
	 * and initialises the bufvec for us.
	 */

	rc = m0_bufvec_alloc(data, block_count, block_size);
	if (rc != 0) {
		m0_indexvec_free(ext);
		return rc;
	}
	rc = m0_bufvec_alloc(attr, block_count, 1);
	if (rc != 0) {
		m0_indexvec_free(ext);
		m0_bufvec_free(data);
		return rc;
	}
	return rc;
}

static void prepare_ext_vecs(struct m0_indexvec *ext,
			     struct m0_bufvec   *data,
			     struct m0_bufvec   *attr,
			     uint32_t            block_count,
			     uint32_t            block_size,
			     uint64_t           *last_index,
			     char                c1,
			     char                c2,
			     char                c3
                 ) {
	int      i;

    /* The data are composed from 3 different randomized char, this to minimize 
     * cache hit.
     */

	for (i = 0; i < block_count/3; ++i) {
		ext->iv_index[i]       = *last_index;
		ext->iv_vec.v_count[i] = block_size;
		*last_index           += block_size;

		/* Fill the buffer with all `c`. */
		memset(data->ov_buf[i], c1, data->ov_vec.v_count[i]);
		/* we don't want any attributes */
		attr->ov_vec.v_count[i] = 0;
	}

	for (; i < block_count/2; ++i) {
		ext->iv_index[i]       = *last_index;
		ext->iv_vec.v_count[i] = block_size;
		*last_index           += block_size;

		/* Fill the buffer with all `c`. */
		memset(data->ov_buf[i], c2, data->ov_vec.v_count[i]);
		/* we don't want any attributes */
		attr->ov_vec.v_count[i] = 0;
	}

	for (; i < block_count; ++i) {
		ext->iv_index[i]       = *last_index;
		ext->iv_vec.v_count[i] = block_size;
		*last_index           += block_size;

		/* Fill the buffer with all `c`. */
		memset(data->ov_buf[i], c3, data->ov_vec.v_count[i]);
		/* we don't want any attributes */
		attr->ov_vec.v_count[i] = 0;
	}
}

static void cleanup_vecs(struct m0_indexvec *ext,
			 struct m0_bufvec   *data,
			 struct m0_bufvec   *attr)
{
	/* Free bufvec's and indexvec's */
	m0_indexvec_free(ext);
	m0_bufvec_free(data);
	m0_bufvec_free(attr);
}

static int write_data_to_object(struct m0_obj      *obj,
				struct m0_indexvec *ext,
				struct m0_bufvec   *data,
				struct m0_bufvec   *attr)
{
	int          rc;
	struct m0_op *ops[1] = { NULL };

	/* Create the write request */
	m0_obj_op(obj, M0_OC_WRITE, ext, data, attr, 0, 0, &ops[0]);
	if (ops[0] == NULL) {
		printf("Failed to init a write op\n");
		return -EINVAL;
	}

	/* Launch the write request*/
	m0_op_launch(ops, 1);

	/* wait */
	rc = m0_op_wait(ops[0],
			M0_BITS(M0_OS_FAILED,
				M0_OS_STABLE),
			M0_TIME_NEVER);
	rc = rc ? : ops[0]->op_sm.sm_rc;

	/* fini and release the ops */
	m0_op_fini(ops[0]);
	m0_op_free(ops[0]);
	// printf("Object write result: %d\n", rc);
	return rc;
}

static int object_write(struct m0_container *container, Arg *arg, struct m0_uint128 obj_id)
{
	struct m0_obj      obj;
	struct m0_client  *instance;
    struct timeval st, et;
    int rc = 0, elapsed;

        struct m0_indexvec ext;
        struct m0_bufvec   data;
        struct m0_bufvec   attr;

	uint64_t           last_offset = 0;

    /* These values will be dynamically to minimize cache hit */
    char MY_CHAR_1 = 'W';
    char MY_CHAR_2 = 'W';
    char MY_CHAR_3 = 'W';

	M0_SET0(&obj);
	instance = container->co_realm.re_instance;
	m0_obj_init(&obj, &container->co_realm, &obj_id,
		    LAYOUT_ID);

	rc = object_open(&obj, obj_id);
	if (rc != 0) {
		printf("Failed to open object: rc=%d\n", rc);
		return rc;
	}

	/*
	 * alloc & prepare ext, data and attr. We will write 4k * 2.
	 */
	rc = alloc_vecs(&ext, &data, &attr, N_BLOCK, BLOCK_SIZE);
	if (rc != 0) {
		printf("Failed to alloc ext & data & attr: %d\n", rc);
		goto out;
	}

    srand((unsigned)time(NULL));
    MY_CHAR_1 = '/' + (rand() % 74);
    MY_CHAR_2 = '/' + (rand() % 74);
    MY_CHAR_3 = '/' + (rand() % 74);

    prepare_ext_vecs(&ext, &data, &attr, N_BLOCK, BLOCK_SIZE, &last_offset, 
        MY_CHAR_1, MY_CHAR_2, MY_CHAR_3);

    // pthread_barrier_wait(&barr_write);
    if (ENABLE_LATENCY_LOG == 1)
        printf("Thread_%d write .. obj_id %d\n", arg->id, obj_id.u_lo);
    
    /* Start to write data to object */
    gettimeofday(&st, NULL);
	rc = write_data_to_object(&obj, &ext, &data, &attr);
    gettimeofday(&et, NULL);
    
    elapsed = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
    arg->write_latency = elapsed/1000.0f;

    if (ENABLE_LATENCY_LOG == 1)
        printf("Finish writing data in : %d us or %.3f s  (%d - %d)\n", elapsed,
            elapsed/1000000.0f, st.tv_sec, et.tv_sec);

	cleanup_vecs(&ext, &data, &attr);

out:
	/* Similar to close() */
	m0_entity_fini(&obj.ob_entity);

    if (arg->id % progress_mod == 0){
	    printf("+");
        fflush(stdout);
    }

	return rc;
}

static int read_data_from_object(struct m0_obj      *obj,
				 struct m0_indexvec *ext,
				 struct m0_bufvec   *data,
				 struct m0_bufvec   *attr)
{
	int          rc;
	struct m0_op *ops[1] = { NULL };

	/* Create the read request */
	m0_obj_op(obj, M0_OC_READ, ext, data, attr, 0, 0, &ops[0]);
	if (ops[0] == NULL) {
		printf("Failed to init a read op\n");
		return -EINVAL;
	}

	/* Launch the read request*/
	m0_op_launch(ops, 1);

	/* wait */
	rc = m0_op_wait(ops[0],
			M0_BITS(M0_OS_FAILED,
				M0_OS_STABLE),
			M0_TIME_NEVER);
	rc = rc ? : ops[0]->op_sm.sm_rc;

	/* fini and release the ops */
	m0_op_fini(ops[0]);
	m0_op_free(ops[0]);
	// printf("Object read result: %d\n", rc);
	return rc;
}

static void verify_show_data(struct m0_bufvec *data,
			     char c)
{
	int i, j;
	for (i = 0; i < data->ov_vec.v_nr; ++i) {
		// printf("  Block %6d:\n", i);
		// printf("%.*s", (int)data->ov_vec.v_count[i],
		// 	       (char *)data->ov_buf[i]);
		// printf("\n");
		for (j = 0; j < data->ov_vec.v_count[i]; j++)
			if (((char*) data->ov_buf[i])[j] != c) {
				printf("verification failed at: %d:%d\n"
				       "Expected %c result %c\n",
					i, j, c, ((char*)data->ov_buf[i])[j]);
			}
	}
    // printf("Verified the correctness of %6d blocks\n", data->ov_vec.v_nr);
}

static int object_read(struct m0_container *container, 
    Arg *arg, struct m0_uint128 obj_id)
{
	struct m0_obj      obj;
    struct m0_client *instance;
    struct timeval st, et;
    int rc, elapsed;
    struct m0_indexvec ext;
    struct m0_bufvec data;
    struct m0_bufvec attr;
	uint64_t           last_offset = 0;

    if (ENABLE_LATENCY_LOG == 1)
        printf("Thread_%d read .. obj_id %d\n", arg->id, obj_id.u_lo);

	M0_SET0(&obj);
	instance = container->co_realm.re_instance;
	m0_obj_init(&obj, &container->co_realm, &obj_id,
		    LAYOUT_ID);

	rc = object_open(&obj, obj_id);
	if (rc != 0) {
		printf("Failed to open object: rc=%d\n", rc);
		return rc;
	}

	/*
	 * alloc & prepare ext, data and attr. We will write 4k * 2.
	 */
    rc = alloc_vecs(&ext, &data, &attr, N_BLOCK, BLOCK_SIZE);
    if (rc != 0) {
		printf("Failed to alloc ext & data & attr: %d\n", rc);
		goto out;
	}
    prepare_ext_vecs(&ext, &data, &attr, N_BLOCK, BLOCK_SIZE, &last_offset, '\0','\0','\0');
    /* Start to read data to object */
    gettimeofday(&st, NULL);
	rc = read_data_from_object(&obj, &ext, &data, &attr);
    gettimeofday(&et, NULL);
    
    elapsed = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
    arg->read_latency = elapsed/1000.0f;
    
    if (ENABLE_LATENCY_LOG == 1)
        printf("Finish reading data in : %d us or %.3f s  (%d - %d)\n", elapsed,
                elapsed/1000000.0f, st.tv_sec, et.tv_sec);
	cleanup_vecs(&ext, &data, &attr);

out:
	/* Similar to close() */
	m0_entity_fini(&obj.ob_entity);

    if (arg->id % progress_mod == 0){
	    printf("-");
        fflush(stdout);
    }

	return rc;
}

static int object_delete(struct m0_container *container, struct m0_uint128 obj_id)
{
	struct m0_obj      obj;
	struct m0_client  *instance;
	struct m0_op      *ops[1] = { NULL };
	int                rc;

	M0_SET0(&obj);
	instance = container->co_realm.re_instance;
	m0_obj_init(&obj, &container->co_realm, &obj_id,
		    LAYOUT_ID);

	rc = object_open(&obj, obj_id);
	if (rc != 0) {
		printf("Failed to open object: rc=%d\n", rc);
		return rc;
	}

	m0_entity_delete(&obj.ob_entity, &ops[0]);
	m0_op_launch(ops, 1);
	rc = m0_op_wait(ops[0],
			M0_BITS(M0_OS_FAILED, M0_OS_STABLE),
			M0_TIME_NEVER);

	/* fini and release */
	m0_op_fini(ops[0]);
	m0_op_free(ops[0]);

	/* Similar to close() */
	m0_entity_fini(&obj.ob_entity);

	// printf("Object deletion: %d\n", rc);
	return rc;
}

static int start_each_thread(struct m0_uint128 obj_id, Arg *arg)
{
    struct m0_container motr_container;
    struct timeval st, et;
    int rc = 0, elapsed = 0;
    sem_wait(&n_thread_sem);

    m0_container_init(&motr_container, NULL, &M0_UBER_REALM, m0_instance);
    rc = motr_container.co_realm.re_entity.en_sm.sm_rc;
    if (rc != 0)
    {
        printf("error in m0_container_init: %d\n", rc);
        goto out;
    }

    if (ENABLE_WRITE == 1) {
        rc = object_create(&motr_container, obj_id);
    }
    if (rc == 0)
    {   
        if (ENABLE_WRITE == 1) {
            rc = object_write(&motr_container, arg, obj_id);
        }
        if (ENABLE_READ == 1 && rc == 0)
        {
            rc = object_read(&motr_container, arg, obj_id);
        }
        if ( ENABLE_DELETE == 1) {
            // pthread_barrier_wait(&barr_delete);
            if (ENABLE_LATENCY_LOG == 1)
                printf("Thread_%d delete .. obj_id %d\n", arg->id, obj_id.u_lo);
            object_delete(&motr_container, obj_id);
        }
    }
    sem_post(&n_thread_sem);
    sem_post(&max_thd_queue_sem);
out:
    return elapsed;
}

static void* init_client_thread(void *vargp) {
    struct m0_uint128 obj_id = {0, 0};
    Arg *arg = (Arg *)vargp;

    /* Update 5th argument (base_obj_id) to create different object ids among the threads */
    obj_id.u_lo = atoll(arg->base_obj_id) + arg->id;
    if (obj_id.u_lo < M0_ID_APP.u_lo) {
        printf("obj_id invalid. Please refer to M0_ID_APP "
               "in motr/client.c\n");
        exit(-EINVAL);
    }

    // printf("This is thread.. %d\n", arg->id);
    start_each_thread(obj_id, arg);
    pthread_exit(NULL);
}

static void show_setup(){
    printf("====================\n");
    printf("Write/Read/Delete\n  %d  /  %d /   %d\n", ENABLE_WRITE, ENABLE_READ, ENABLE_DELETE);
    printf("N_THREAD   : %d\n", N_THREAD);
    printf("N_THD_SEMA : %d\n", N_THD_SEMA);
    printf("N_BLOCK    : %d\n", N_BLOCK);
    printf("BLOCK_SIZE : %d KB\n", BLOCK_SIZE/1024);
    printf("TOTAL_SIZE : %d KB (%.2f MB)\n", N_BLOCK*BLOCK_SIZE/1024*N_THREAD,  N_BLOCK*BLOCK_SIZE/1024/1024.0f*N_THREAD);
    printf("====================\n");
}

void show_latency_and_throughput(Arg *array_arg, double elapsed, float *avg_r, float *avg_w, float *thrpt, float *exec_time){
    double sum_read_latency = 0, sum_write_latency = 0, total_payload = 0;
    int i;
    /* Get the average latency */
    for (i = 0; i < N_THREAD; i++) {
        sum_read_latency += array_arg[i].read_latency;
        sum_write_latency += array_arg[i].write_latency;
    }

    printf("\n");
    if (ENABLE_READ == 1) {
        *avg_r = sum_read_latency/N_THREAD;
        printf("Avg read latency = %.2f/%d = %.2f ms = %.3f secs/req\n",
            sum_read_latency,N_THREAD, *avg_r, sum_read_latency/N_THREAD/1000.0f);
    }
    if (ENABLE_WRITE == 1) {
        *avg_w = sum_write_latency/N_THREAD;
        printf("Avg write latency = %.2f/%d = %.2f ms = %.3f secs/req\n",
            sum_write_latency,N_THREAD, *avg_w, sum_write_latency/N_THREAD/1000.0f);
    }

    /* Calculate total throughput (of READ + WRITE + DELETE) in MBps */
    total_payload = N_BLOCK*BLOCK_SIZE/1024/1024.0f*N_THREAD;
    *thrpt = total_payload/elapsed;
    printf("Throughput = %.2f/%.2f = %.2f MBps\n",total_payload,elapsed, *thrpt);
    *exec_time = elapsed/60;
    printf("Execution time = %.2f secs = %.2f min\n",elapsed, *exec_time);
}

void rec_mkdir(const char *dir) {
    char tmp[256];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp),"%s",dir);
    len = strlen(tmp);
    if(tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for(p = tmp + 1; *p; p++)
        if(*p == '/') {
                *p = 0;
                mkdir(tmp, S_IRWXU);
                *p = '/';
        }
    mkdir(tmp, S_IRWXU);
}

void gen_cdf_graph(char *input_trace, char * output_file) {
    char command[3*MAX_PATH_LEN];
    // Run analysis of the latency and generate CDF graph
    sprintf(command, "./analyze_lat.py -in_trace %s >> %s ", input_trace, output_file);
    printf("%s\n", command);
}

void print_latency_to_file(Arg *array_arg, float *avg_r, float *avg_w, float *thrpt, float *exec_time) {
    struct stat st = {0};
    FILE *fptr;
    int i = 0;
    time_t t;
    char curr_dir[MAX_PATH_LEN];
    char SUB_DIR[MAX_PATH_LEN];
    char out_dir[MAX_PATH_LEN];
    char read_lat_path[MAX_PATH_LEN];
    char write_lat_path[MAX_PATH_LEN];
    char stats_file_path[MAX_PATH_LEN];
    char command[MAX_PATH_LEN];
    char *header = "obj_id, latency_ms";
    srand((unsigned) time(&t));
    int rand_value = rand() % 100000;

    if (getcwd(curr_dir, sizeof(curr_dir)) == NULL) {
        perror("getcwd() error");
        exit(-1);
    }
    sprintf(SUB_DIR, "%d-%d-%d", LAYOUT_ID, N_THREAD, N_THD_SEMA);
    sprintf(out_dir, "%s/%s/%s",curr_dir, OUTPUT_DIR, SUB_DIR);
    rec_mkdir(out_dir);
    
    // Read latency 
    if (ENABLE_READ == 1) {
        sprintf(read_lat_path, "%s/%d%s",out_dir, rand_value,"_read_lat.txt");
        fptr = fopen(read_lat_path, "w");
        fprintf(fptr, "%s\n", header);
        for(i = 0; i < N_THREAD; i++ ) {
            fprintf(fptr, "%d, %f\n", array_arg[i].id, array_arg[i].read_latency);
        }
        printf("   Latency is written to = %s\n", read_lat_path);
        fclose(fptr);
    }

    // Write latency
    if (ENABLE_WRITE == 1) {
        sprintf(write_lat_path, "%s/%d%s",out_dir, rand_value,"_write_lat.txt");
        fptr = fopen(write_lat_path, "w");
        fprintf(fptr, "%s\n", header);
        for(i = 0; i < N_THREAD; i++ ) {
            fprintf(fptr, "%d, %f\n", array_arg[i].id, array_arg[i].write_latency);
        }
        printf("   Latency is written to = %s\n", write_lat_path);
        fclose(fptr);
    }

    // Print statistics 
    sprintf(stats_file_path, "%s/%d%s",out_dir, rand_value,"_stats.txt");
    fptr = fopen(stats_file_path, "w");
    fprintf(fptr, "TOTAL REQUEST : %d\n", N_THREAD);
    fprintf(fptr, "CONCURRENCY   : %d\n", N_THD_SEMA);
    fprintf(fptr, "Size/request  : %d KB\n", BLOCK_SIZE/1024);
    fprintf(fptr, "Total size    : %d KB (%.2f MB)\n", N_BLOCK*BLOCK_SIZE/1024*N_THREAD,  N_BLOCK*BLOCK_SIZE/1024/1024.0f*N_THREAD);
    if (ENABLE_WRITE == 1) 
        fprintf(fptr, "Average write : %f ms\n", *avg_w);
    if (ENABLE_READ == 1)
        fprintf(fptr, "Average read  : %f ms\n", *avg_r);
    
    fprintf(fptr, "Throughput    : %f MBps\n", *thrpt);
    fprintf(fptr, "Exec time     : %f mins\n", *exec_time);
    fclose(fptr);

    sprintf(command, "chown -R %s %s ", USER_NAME, curr_dir);
    system(command);
    printf("   Changing the ownership of the output to user = %s\n", USER_NAME);

    // Run python analysis and generate the graph
    printf("\nRun this script to generate graphs \n");
    if (ENABLE_WRITE == 1) 
        gen_cdf_graph(write_lat_path, stats_file_path);
    if (ENABLE_READ == 1)
        gen_cdf_graph(read_lat_path, stats_file_path);

}

void client_init(char **argv){
    struct m0_idx_dix_config motr_dix_conf;
    int rc = 0;

    motr_dix_conf.kc_create_meta = false;
    motr_conf.mc_is_oostore = true;
    motr_conf.mc_is_read_verify = false;
    motr_conf.mc_ha_addr = argv[1];
    motr_conf.mc_local_addr = argv[2];
    motr_conf.mc_profile = argv[3];
    motr_conf.mc_process_fid = argv[4];
    motr_conf.mc_tm_recv_queue_min_len = M0_NET_TM_RECV_QUEUE_DEF_LEN;
    motr_conf.mc_max_rpc_msg_size = M0_RPC_DEF_MAX_RPC_MSG_SIZE;
    motr_conf.mc_idx_service_id = M0_IDX_DIX;
    motr_conf.mc_idx_service_conf = (void *)&motr_dix_conf;

    rc = m0_client_init(&m0_instance, &motr_conf, true);
    if (rc != 0)
    {
        printf("error in m0_client_init: %d\n", rc);
        exit(rc);
    }
}

void parse_main_argv(char **argv){
     /* Initiate the IO mode */
    ENABLE_WRITE = atoi(argv[6]);
    ENABLE_READ = atoi(argv[7]);
    ENABLE_DELETE = atoi(argv[8]);
    // Specify the Layout and block size
    LAYOUT_ID = atoi(argv[9]);
    N_THREAD = atoi(argv[10]);
    N_THD_SEMA = atoi(argv[11]);
}

int main(int argc, char *argv[]) {
    double elapsed = 0;
    parse_main_argv(argv);
    struct timeval st, et;
    int rc = 0, i;
    float avg_r = 0, avg_w = 0, thrpt = 0, exec_time = 0;
    pthread_t t[N_THREAD];
    Arg *array_arg = malloc(N_THREAD * sizeof(*array_arg)); 
    sem_init(&n_thread_sem, 0, N_THD_SEMA);
    /* to make sure that only 2*N_THD_SEMA threads are created at any given time */
    sem_init(&max_thd_queue_sem, 0, 2*N_THD_SEMA);

    /* To print out progress every 5 % */
    if (N_THREAD > 20)
        progress_mod = N_THREAD/20;

    /* Make it dynamic for benchmarking */
    BLOCK_SIZE = get_block_size(LAYOUT_ID); 
    N_BLOCK = update_n_block();
    show_setup();

    if (argc < (10 )) {
		printf("Need more arguments: %s HA_ADDR LOCAL_ADDR Profile_fid Process_fid obj_id\n",
		       argv[0]);
		exit(-1);
	}
    client_init(argv);

    // Let us create N threads ; each thread will send 1 request
    gettimeofday(&st, NULL);
    for (i = 0; i < N_THREAD; i++){
        sem_wait(&max_thd_queue_sem); // to avoid flooding the system with thousands of threads
        array_arg[i].id = i+1;
        array_arg[i].base_obj_id = argv[5];
        array_arg[i].read_latency = 0;
        array_arg[i].write_latency = 0;
        pthread_create(&t[i], NULL, init_client_thread, &array_arg[i]);
    }
    for (i = 0; i < N_THREAD; i++)
        pthread_join(t[i], NULL);
    gettimeofday(&et, NULL);
    elapsed = (((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec))/1000000.0f;

    sem_destroy(&n_thread_sem);
    sem_destroy(&max_thd_queue_sem);
    m0_client_fini(m0_instance, true);
    show_latency_and_throughput(array_arg, elapsed, &avg_r, &avg_w, &thrpt, &exec_time);
    print_latency_to_file(array_arg, &avg_r, &avg_w, &thrpt, &exec_time);
}

/*
 And there are multiple "m0_instance" in this program (in multiple threads).
This is not allowed. Only a single instance is allowed in a process space address.
Please share this instance in all threads.
You can refer to source code: motr/st/utils/copy_mt.c
You will find how to do multi-threaded Motr client app.

Please do this in another file: e.g. example_mt.c
And write some guide for how to program a multi-threaded Motr application.
And then create a new PR.
*/

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
