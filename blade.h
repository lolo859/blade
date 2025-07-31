#ifndef BLADE_H
#define BLADE_H
#include "dependencies/blake3.h"
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <new>
#include <cmath>
#include <array>
#include <vector>
#include <memory>
#include <thread>
#include <map>
#include <algorithm>
#include <future>
#define BLADE_OUTPUT_SIZE_BYTES 64
//* Blade namespace
//* The only function designedfor public use is blade, others like blade_core or glouton are for internal uses
//* You can use force_parallelism to force the parallelism on smaller input
//* Or block_parallelism to completely disable it
namespace blade {
    bool force_parallelism=false;
    bool block_parallelism=false;
    constexpr std::array<uint64_t,64> glouton_pow2={
        9223372036854775808ULL,
        4611686018427387904ULL,
        2305843009213693952ULL,
        1152921504606846976ULL,
        576460752303423488ULL,
        288230376151711744ULL,
        144115188075855872ULL,
        72057594037927936ULL,
        36028797018963968ULL,
        18014398509481984ULL,
        9007199254740992ULL,
        4503599627370496ULL,
        2251799813685248ULL,
        1125899906842624ULL,
        562949953421312ULL,
        281474976710656ULL,
        140737488355328ULL,
        70368744177664ULL,
        35184372088832ULL,
        17592186044416ULL,
        8796093022208ULL,
        4398046511104ULL,
        2199023255552ULL,
        1099511627776ULL,
        549755813888ULL,
        274877906944ULL,
        137438953472ULL,
        68719476736ULL,
        34359738368ULL,
        17179869184ULL,
        8589934592ULL,
        4294967296ULL,
        2147483648ULL,
        1073741824ULL,
        536870912ULL,
        268435456ULL,
        134217728ULL,
        67108864ULL,
        33554432ULL,
        16777216ULL,
        8388608ULL,
        4194304ULL,
        2097152ULL,
        1048576ULL,
        524288ULL,
        262144ULL,
        131072ULL,
        65536ULL,
        32768ULL,
        16384ULL,
        8192ULL,
        4096ULL,
        2048ULL,
        1024ULL,
        512ULL,
        256ULL,
        128ULL,
        64ULL,
        32ULL,
        16ULL,
        8ULL,
        4ULL,
        2ULL,
        1ULL
    };
    int blade_core(const uint8_t* input,size_t size_input,uint8_t* output,size_t size_output,size_t chunk_size,size_t iteration_num) {
        if (size_output!=32) {
            return 1;
        }
        if (size_input!=64) {
            return 1;
        }             
        uint8_t seed[32];
        uint8_t blake3_output[32];
        blake3_hasher hasher_seed;
        char input_seed[64];
        memcpy(input_seed,input,64);
        uint64_t temp;
        for (int i=0;i<8;++i) {
            memcpy(&temp,input_seed+i*8,8);
            temp=~(((temp<<(iteration_num))|(temp>>(64-iteration_num)))^chunk_size);
            memcpy(input_seed+i*8,&temp,8);
        }
        blake3_hasher_init(&hasher_seed);
        blake3_hasher_update(&hasher_seed,input_seed,64);
        blake3_hasher_finalize(&hasher_seed,seed,32);
        blake3_hasher hasher_main;
        blake3_hasher_init_keyed(&hasher_main,seed);
        blake3_hasher_update(&hasher_main,input,64);
        blake3_hasher_finalize(&hasher_main,blake3_output,32);
        memcpy(output,blake3_output,32);
        return 0;
    }
    int blade_batch_serial(const uint8_t* input_ptr,size_t num_couples,uint8_t* output_ptr,size_t chunk_size,size_t iteration_num) {
        for (size_t i=0;i<num_couples;++i) {
            if (blade_core(input_ptr+i*64,64,output_ptr+i*32,32,chunk_size,iteration_num)!=0) {
                return 1;
            }
        }
        return 0;
    }
    struct thread_channel {
        const uint8_t* input_ptr=nullptr;
        size_t num_couples=0;
        uint8_t* output_ptr=nullptr;
        size_t chunk_size=0;
        size_t iteration_num=0;
        bool has_work=false;
        bool stop_thread=false;
        std::mutex mutex;
        std::condition_variable condition;
        std::promise<int> promise;
    };
    static std::vector<std::unique_ptr<thread_channel>> thread_channel_list;
    static std::vector<std::thread> threads_list;
    static std::once_flag thread_init_flag;
    void thread_loop(int thread_idx) {
        thread_channel& channel=*(thread_channel_list[thread_idx].get());
        while (true) {
            std::unique_lock<std::mutex> lock(channel.mutex);
            channel.condition.wait(lock,[&]{return channel.has_work || channel.stop_thread;});
            if (channel.stop_thread && !channel.has_work) {
                return; 
            }
            channel.has_work=false;
            const uint8_t* current_input_ptr=channel.input_ptr;
            size_t current_num_couples=channel.num_couples;
            uint8_t* current_output_ptr=channel.output_ptr;
            size_t current_chunk_size=channel.chunk_size;
            size_t current_iteration_num=channel.iteration_num;
            std::promise<int> current_promise=std::move(channel.promise);
            lock.unlock();
            int result=blade_batch_serial(current_input_ptr,current_num_couples,current_output_ptr,current_chunk_size,current_iteration_num);
            current_promise.set_value(result);
        }
    }
    void initialize_thread_pool() {
        unsigned int num_threads=std::thread::hardware_concurrency();
        if (num_threads==0) num_threads=1;
        thread_channel_list.reserve(num_threads);
        for (unsigned int i=0;i<num_threads;++i) {
            thread_channel_list.push_back(std::make_unique<thread_channel>());
            threads_list.emplace_back(thread_loop,i);
        }
    }
    struct ThreadPoolExitCleanup {
        ~ThreadPoolExitCleanup() {
            for (size_t i=0;i<thread_channel_list.size();++i) {
                std::unique_lock<std::mutex> lock(thread_channel_list[i]->mutex);
                thread_channel_list[i]->stop_thread=true;
                lock.unlock();
                thread_channel_list[i]->condition.notify_one();
            }
            for (std::thread& t:threads_list) {
                if (t.joinable()) {
                    t.join();
                }
            }
        }
    };
    static ThreadPoolExitCleanup g_cleanup;
    inline uint64_t read_u64_le(const uint8_t* p) {
        return (uint64_t)p[0] |
               ((uint64_t)p[1]<<8) |
               ((uint64_t)p[2]<<16) |
               ((uint64_t)p[3]<<24) |
               ((uint64_t)p[4]<<32) |
               ((uint64_t)p[5]<<40) |
               ((uint64_t)p[6]<<48) |
               ((uint64_t)p[7]<<56);
    }
    inline void write_u64_le(uint8_t* p,uint64_t val) {
        p[0]=(uint8_t)val;
        p[1]=(uint8_t)(val>>8);
        p[2]=(uint8_t)(val>>16);
        p[3]=(uint8_t)(val>>24);
        p[4]=(uint8_t)(val>>32);
        p[5]=(uint8_t)(val>>40);
        p[6]=(uint8_t)(val>>48);
        p[7]=(uint8_t)(val>>56);
    }
    std::vector<uint64_t> glouton(size_t total_size) {
        std::vector<uint64_t> out_chunks;
        size_t local_size=total_size;
        const uint64_t min_chunk_size=128ULL;
        for (size_t i=0;i<glouton_pow2.size();++i) {
            uint64_t current_pow2=glouton_pow2[i];
            if (current_pow2<min_chunk_size) {
                break; 
            }
            if (current_pow2>local_size) {
                continue; 
            } else {
                out_chunks.push_back(current_pow2);
                local_size-=current_pow2;
            }
            if (local_size==0) {
                break;
            }
        }
        if (local_size==0) {
            return out_chunks;
        } else {
            return {}; 
        }
    }
    //* Blade public function
    //* const uint8_t* input : the pointer to the data to hash
    //* size_t input_size : must be equal or greater than 128 bytes
    //* uint8_t* output : the pointer to the output buffer
    //* size_t size_output : the size of the output, must be 64 bytes
    //* Return 0 if sucess, 1 if failure
    int blade(const uint8_t* input,size_t size_input,uint8_t* output,size_t size_output) {
        std::call_once(thread_init_flag,initialize_thread_pool);
        if (size_output!=BLADE_OUTPUT_SIZE_BYTES) {
            return 1;
        }
        if (input==nullptr && size_input>0) {
            return 1;
        }
        if (size_input==BLADE_OUTPUT_SIZE_BYTES) {
            memcpy(output,input,BLADE_OUTPUT_SIZE_BYTES);
            return 0;
        }
        if (size_input<128) {
            return 1; 
        }
        size_t padded_input_size=size_input;
        size_t bytes_to_pad=0;
        if (padded_input_size%128!=0) {
            padded_input_size=(padded_input_size/128+1)*128;
            bytes_to_pad=padded_input_size-size_input;
        }
        std::vector<uint8_t> padded_input(padded_input_size);
        memcpy(padded_input.data(),input,size_input);
        if (bytes_to_pad > 0) {
            uint8_t padding_material[128]; 
            blake3_hasher hasher_padding;
            blake3_hasher_init(&hasher_padding);
            blake3_hasher_update(&hasher_padding,input,size_input);
            blake3_hasher_finalize(&hasher_padding,padding_material,sizeof(padding_material));
            memcpy(padded_input.data()+size_input,padding_material,bytes_to_pad);
        }        
        auto chunk_sizes=glouton(padded_input_size);
        if (chunk_sizes.empty()) {
            return 1; 
        }
        char chunk_hash[64];
        std::vector<uint8_t> collected_hashes;
        collected_hashes.reserve(chunk_sizes.size()*BLADE_OUTPUT_SIZE_BYTES);
        uint64_t chunk_offset=0;
        unsigned int num_threads=threads_list.size();
        if (num_threads==0) num_threads=1;
        size_t max_chunk_size=0;
        if (!chunk_sizes.empty()) {
            max_chunk_size=*std::max_element(chunk_sizes.begin(),chunk_sizes.end());
        }
        size_t max_temp_buffer_size=max_chunk_size/2;
        auto buffer1_uptr=std::make_unique<uint8_t[]>(max_temp_buffer_size);
        auto buffer2_uptr=std::make_unique<uint8_t[]>(max_temp_buffer_size);
        if (!buffer1_uptr || !buffer2_uptr) {
            return 1;
        }
        uint8_t* buffer1=buffer1_uptr.get();
        uint8_t* buffer2=buffer2_uptr.get();
        for (uint64_t chunk_size:chunk_sizes) {
            const uint8_t* current_glouton_chunk_data=padded_input.data()+chunk_offset;
            size_t current_chunk_level_size=chunk_size;
            std::vector<uint8_t> initial_chunk_copy(chunk_size);
            memcpy(initial_chunk_copy.data(),current_glouton_chunk_data,chunk_size);
            const uint8_t* current_input_buffer=initial_chunk_copy.data();
            uint8_t* current_output_buffer;
            size_t iteration_num=0;
            while (current_chunk_level_size>BLADE_OUTPUT_SIZE_BYTES) {
                if (iteration_num%2==0) {
                    current_output_buffer=buffer1;
                } else {
                    current_output_buffer=buffer2;
                }
                size_t num_couples_in_iteration=current_chunk_level_size/64;
                size_t next_level_size=num_couples_in_iteration*32;
                size_t actual_threads=num_threads;
                if (num_couples_in_iteration==0) actual_threads=1;
                else actual_threads=std::min(actual_threads,num_couples_in_iteration);
                size_t couples_per_thread=num_couples_in_iteration/actual_threads;
                size_t remaining_couples=num_couples_in_iteration%actual_threads;
                bool use_parallel=(num_couples_in_iteration>=1 && num_threads>1 && padded_input_size>128*1024);
                if (force_parallelism || (use_parallel && !block_parallelism)) {
                    std::vector<std::future<int>> futures;
                    futures.reserve(actual_threads);
                    for (unsigned int t=0;t<actual_threads;++t) {
                        size_t start_couple_idx=t*couples_per_thread;
                        size_t num_couples_for_this_thread=couples_per_thread;
                        if (t==actual_threads-1) {
                            num_couples_for_this_thread+=remaining_couples;
                        }
                        const uint8_t* thread_input_ptr=current_input_buffer+start_couple_idx*64;
                        uint8_t* thread_output_ptr=current_output_buffer+start_couple_idx*32;
                        thread_channel& channel=*(thread_channel_list[t].get());
                        std::unique_lock<std::mutex> lock(channel.mutex);
                        channel.input_ptr=thread_input_ptr;
                        channel.num_couples=num_couples_for_this_thread;
                        channel.output_ptr=thread_output_ptr;
                        channel.chunk_size=chunk_size;
                        channel.iteration_num=iteration_num;
                        channel.has_work=true;
                        channel.promise=std::promise<int>();
                        futures.push_back(channel.promise.get_future());
                        lock.unlock();
                        channel.condition.notify_one();
                    }
                    int parallel_result_status=0;
                    for (auto& f:futures) {
                        if (f.get()!=0) {
                            parallel_result_status=1;
                            break;
                        }
                    }
                    if (parallel_result_status != 0) {
                        return parallel_result_status;
                    }
                } else {
                    if (blade_batch_serial(current_input_buffer,num_couples_in_iteration,current_output_buffer,chunk_size,iteration_num)!=0) {
                        return 1;
                    }
                }
                for (int t=0;t<11;++t) {
                    for (size_t block_pair_offset=0;block_pair_offset<=next_level_size-16;block_pair_offset+=12) {
                        uint64_t first_block=read_u64_le(current_output_buffer+block_pair_offset);
                        uint64_t second_block=read_u64_le(current_output_buffer+block_pair_offset+8);
                        int rot_amount=(t*3+1)%64;
                        uint64_t temp_first=first_block;
                        uint64_t temp_second=second_block;
                        temp_first=((temp_first<<rot_amount)|(temp_first>>(64-rot_amount)))^temp_second;
                        temp_second=((temp_second<<rot_amount)|(temp_second>>(64-rot_amount)))^temp_first;
                        write_u64_le(current_output_buffer+block_pair_offset,temp_first);
                        write_u64_le(current_output_buffer+block_pair_offset+8,temp_second);
                    }
                }
                current_input_buffer=current_output_buffer;
                current_chunk_level_size=next_level_size;
                iteration_num++;
                if (current_chunk_level_size==BLADE_OUTPUT_SIZE_BYTES) {
                    memcpy(chunk_hash,current_input_buffer,BLADE_OUTPUT_SIZE_BYTES);
                }
            }
            collected_hashes.insert(collected_hashes.end(),chunk_hash,chunk_hash+BLADE_OUTPUT_SIZE_BYTES);
            chunk_offset+=chunk_size;
        }
        return blade(collected_hashes.data(),collected_hashes.size(),output,BLADE_OUTPUT_SIZE_BYTES);
    }
}
#endif