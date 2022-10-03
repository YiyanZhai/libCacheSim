#pragma once

#include <inttypes.h>

#include <string>

#include "../../include/libCacheSim/reader.h"

#define N_ARGS 2

/* This structure is used to communicate with parse_opt. */
struct arguments {
  /* argument from the user */
  int64_t n_req;
  char *args[N_ARGS];
  char *trace_path;
  char *ofilepath;
  char *trace_type_str;
  trace_type_e trace_type;
  char *trace_type_params;
  double sample_ratio;

  bool ignore_obj_size;
  bool output_txt;
  /* some objects may change size during the trace, this keeps the size as the
   * last size in the trace */
  bool remove_size_change;

  /* arguments generated */
  reader_t *reader;
};

namespace cli {
void parse_cmd(int argc, char *argv[], struct arguments *args);
}

namespace oracleGeneral {

void convert_to_oracleGeneral(reader_t *reader, std::string ofilepath,
                              int sample_ratio, bool output_txt,
                              bool remove_size_change);

}  // namespace oracleGeneral