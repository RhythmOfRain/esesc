/*
  USAGE:
    1. In GProcessor.h uncomment: #define WAVESNAP_EN

    2. In TaskHandler.cpp, in TaskHandler::unplug() specify the format and the file where you
       want to dump the traces. In case wavedrom format is picked, copy and paste generated
       file into Wavedrom edittor to visualize. 
*/

#ifndef _WAVESNAP_
#define _WAVESNAP_
//functional defines
//#define REMOVE_REDUNDANT_EDGES

//signature defines
#define REGISTER_NUM 32
#define HASH_SIZE 4*65536

//node list defines
#define MAX_NODE_NUM 1000
#define MAX_EDGE_NUM 1000000
#define MAX_MOVING_GRAPH_NODES 64
#define COUNT_ALLOW 0
#define TRY_COUNT_ALLOW 10000
//dump path
#define EDGE_DUMP_PATH "dump.txt"

//encode instructions
#define ENCODING "0123456789abcdefghijklmnopqrstuvwxyzABCDEFHIJKLMNPQRSTUVWXYZ"

//misc defines
#define W_LOWER 2
#define W_UPPER 13

#include <vector>
#include <stdint.h>
#include <stdio.h>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <map>
#include "InstOpcode.h"
#include "EmuSampler.h"
#include "DInst.h"
#include "BloomFilter.h"

class wavesnap {
  private:
    class instruction_info {
      public:
        uint32_t id;
        uint32_t dst;
        uint32_t src1;
        uint32_t src2;
        uint32_t parent1_id;
        uint32_t parent2_id;
        uint32_t depth;
        bool valid;
        std::string opcode_name;
        uint32_t opcode;
        uint64_t time_stamp;

        instruction_info() {
          this->id = 0;
          this->dst = 0;
          this->src1 = 0;
          this->src2 = 0;
          this->parent1_id = 0;
          this->parent2_id = 0;
          this->depth = 0;
          this->valid = false;
          this->time_stamp = 0;
        }

        std::string dump() {
          std::stringstream result;

          result << "(";
          result << id << ", ";
          result << dst << ", ";
          result << src1 << ", ";
          result << src2 << ", ";
          result << parent1_id << ", ";
          result << parent2_id << ", ";
          result << depth << ")";
    
          return result.str();
        }
    };

    class pipeline_info {
      private:

      public:
        std::vector<uint8_t> instructions;
        std::vector<uint32_t> wait_cycles;
        std::vector<uint32_t> rename_cycles;
        std::vector<uint32_t> issue_cycles;
        std::vector<uint32_t> execute_cycles;
        uint32_t count;

        pipeline_info() {
          this->count = 0;
        }
    };

    class window_info {
      private:

      public:
        std::map<std::string, pipeline_info> p_i;
        uint32_t count;

        window_info() {
          this->count = 0;
        }
        ~window_info() {

        }
    };


    class instruction_window {
      private:
        bool check_window[MAX_MOVING_GRAPH_NODES];
        bool is_control[MAX_MOVING_GRAPH_NODES];
        uint64_t start_id;
        bool set = false;
        uint32_t size;

      public:
        DInst window[MAX_MOVING_GRAPH_NODES];
        uint64_t min_fetch_time;
        uint64_t min_rename_time;

        instruction_window() {
          this->set = false;
          this->size = 0;
          this->min_fetch_time = std::numeric_limits<uint64_t>::max();
          this->min_rename_time = std::numeric_limits<uint64_t>::max();
          for (uint32_t i=0; i<MAX_MOVING_GRAPH_NODES; i++) {
            check_window[i] = false;
            is_control[i] = false;
          }
        }
        
        ~instruction_window() {
          //nothing todo here
        }

        DInst get_first_inst() {
          return this->window[0];
        }

        bool check_if_control(uint16_t i) {
          return this->is_control[i];
        }

        std::vector<int32_t> get_pipeline_info(uint32_t n) {
          std::vector<int32_t> result;
          int32_t w,r,i,e;
          if (n>=0 && n<=MAX_MOVING_GRAPH_NODES) {
            w = window[n].getFetchedTime()-this->min_fetch_time;
            r = window[n].getRenamedTime()-window[n].getFetchedTime();
            i = window[n].getIssuedTime()-window[n].getRenamedTime();
            e = window[n].getExecutedTime()-window[n].getIssuedTime();

            if (w<0 || r<0 || i<0 || e<0) {
              std::cout << n << " ";
              std::cout << window[n].getFetchedTime() << " ";
              std::cout << this->min_fetch_time << " ";
              std::cout << window[n].getInst()->getOpcode() << " ";
              std::cout << window[n].getID() << " ";
              std::cout << w << "w ";
              std::cout << r << "r ";
              std::cout << i << "i ";
              std::cout << e << "e ";
              std::cout << std::endl;
            }
            result.push_back(w);
            result.push_back(r);
            result.push_back(i);
            result.push_back(e);
            result.push_back(window[n].getInst()->getOpcode());
          } else {
            std::cout << "error: DInst get_pipeline_info: out of window" << std::endl;
          }
          return result;
        }

        bool is_set() {
          return this->set;
        }

        uint32_t get_size() {
          return this->size;
        }

        void set_start_id(uint64_t start_id) {
          this->start_id = start_id;
          this->set = true;
        }
  
        void update(DInst* dinst) {
          //check if this instruction is allowed in this window
          int32_t window_index = dinst->getID()-(this->start_id);
          if (window_index>=MAX_MOVING_GRAPH_NODES || window_index<0) {
            std::cout << "this instruction should not be in this window (" << window_index << "," << this->start_id << ")" << std::endl;
          } else {
            window[window_index] = *dinst;
            if (check_window[window_index] == false) {
              check_window[window_index] = true;
              if (dinst->getInst()->isControl()) {
                is_control[window_index] = true;
              }
              size++;
              if (min_fetch_time>dinst->getFetchTime()) {
                min_fetch_time = dinst->getFetchTime();
              }
              if (min_rename_time>dinst->getRenamedTime()) {
                min_rename_time = dinst->getRenamedTime();
              }
            }
          }
        }
    
        void dump() {
          for (uint32_t i=0; i<MAX_MOVING_GRAPH_NODES; i++) {
            std::cout << window[i].getFetchedTime() << " ";
            if (window[i].getFetchedTime() == 0) {
              std::cout << window[i].getInst()->getOpcode();
            }
            std::cout << std::endl;
          }
          std::cout <<  "**------------**" << std::endl;
        }
    };

    std::map<uint64_t, instruction_window> windows;

  public:
    wavesnap();
    ~wavesnap();
    void test_uncompleted();
    void update_window();
    void add_instruction(DInst* dinst); 
    bool first_window_completed;
    std::vector<DInst*> working_window;
    uint64_t curr_min_time;
    std::vector<DInst*> wait_buffer; 
    uint64_t window_pointer;
    uint64_t try_count;

    void full_ipc_update(DInst* dinst);
    void add_to_RAT(DInst* dinst);
    void merge();
    void calculate_ipc();
    void calculate_full_ipc();
    void dump_signatures(std::string fileName);
    void dump_address(std::string fileName);
    uint64_t total_count;
    std::string dump_w();
    std::map<std::string, pipeline_info> window_sign_info;
    std::map<uint64_t, window_info> window_address_info;
    std::map<uint64_t, instruction_info> RAT;
    std::map<uint8_t, std::vector<float>> IPCs;


    std::map<uint64_t, uint32_t> full_fetch_ipc;
    std::map<uint64_t, uint32_t> full_rename_ipc;
    std::map<uint64_t, uint32_t> full_issue_ipc;
    std::map<uint64_t, uint32_t> full_execute_ipc;
};
#endif
