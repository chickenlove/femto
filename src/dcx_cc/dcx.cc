/*
  (*) 2008-2014 Michael Ferguson <michaelferguson@acm.org>

    * This is a work of the United States Government and is not protected by
      copyright in the United States.

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  femto/src/dcx_cc/dcx.cc
*/
#include "dcx.hh"

extern "C" {
#include "processors.h"
}

int64_t dcx_g_cur_disk_usage;
int64_t dcx_g_max_disk_usage;
MPI_handler* dcx_g_handler;

void add_dcx_disk_usage(int64_t sz)
{
  //std::cout << "add_disk_usage " << sz << " cur=" << cur_disk_usage << " max=" << max_disk_usage << std::endl;
  dcx_g_cur_disk_usage += sz;
  if( dcx_g_cur_disk_usage > dcx_g_max_disk_usage ) dcx_g_max_disk_usage = dcx_g_cur_disk_usage;
}
void sub_dcx_disk_usage(int64_t sz)
{
  //std::cout << "sub_disk_usage " << sz << " cur=" << cur_disk_usage << " max=" << max_disk_usage << std::endl;
  dcx_g_cur_disk_usage -= sz;
}

unsigned long dcx_g_mem_per_bin = 2L*1024L*1024L*1024L;
unsigned long dcx_g_max_mem_per_bin = 2L*1024L*1024L*1024L;

void setup_mem_per_bin(long sort_memory)
{
  error_t err;
  long long mem_avail = 0;
  err = get_phys_mem(&mem_avail);
  die_if_err(err);
  //long long int pages = sysconf(_SC_PHYS_PAGES);
  //long long int page_size = sysconf(_SC_PAGE_SIZE);
  //long long int mem_avail = pages * page_size;

  long procs = 2;
  if( SORT_PROCS_PER_BIN > procs ) procs = SORT_PROCS_PER_BIN;
  if( PERMUTE_PROCS_PER_BIN > procs ) procs = PERMUTE_PROCS_PER_BIN;

  mem_avail -= 1024L*1024L*1024L; // leave some room for system.
  // Round down to GB.
  long long int mem_avail_gb = mem_avail / (1024L*1024L*1024L);
  if( mem_avail_gb > 0 ) mem_avail = mem_avail_gb * (1024L*1024L*1024L);

  // Don't allow sort_memory > available memory / 1.5
  if( sort_memory > 2 * mem_avail / 3 ) sort_memory = 2 * mem_avail / 3;
  if( sort_memory == 0 ) sort_memory = mem_avail / 2;

  // In previous experiments, on 16 GB free machines,
  // we had 2GB per bin, but that was too much for a 12GB machine...
  // We're definately going to use 4*n + space for buffers (~2GB)
  //
  // 16 / 8 = 2 is previous setting.
  // We aim to use half of memory in sort buffers, and we might
  // need to have 2 different sorting buffers open at once.
  long mem_per_bin = sort_memory / (2*procs);

  if( (unsigned long ) mem_per_bin > dcx_g_max_mem_per_bin )
    mem_per_bin = dcx_g_max_mem_per_bin;

  dcx_g_mem_per_bin = mem_per_bin;
}

// Compute the factor by which to multiply the number of files
// per bin in order to keep the number of open files under
// the current maximum number of open files (from getrlimit).
// The n_files value should be multiplied by the result;
// this function returns 1.0 or a number < 1.0.
double compute_splitter_factor(int64_t total_files)
{
  
  int rc;
  struct rlimit rlim;
  rc = getrlimit(RLIMIT_NOFILE, &rlim);
  if( rc < 0 ) {
    throw error(ERR_IO_STR("getrlimit failed"));
  }

  if( rlim.rlim_cur == RLIM_INFINITY ) return 1.0;

  int64_t max_files = rlim.rlim_cur / 2;
  // make sure max_files is reasonable.
  if( max_files < 8 ) {
    fprintf(stderr, "Warning - getrlimit returned unreasonably small maximum number of open files, using 8\n");
    max_files = 8;
  }
  if( max_files > 1000000 ) {
    fprintf(stderr, "Warning - getrlimit returned unreasonably large maximum number of open files, using 1M\n");
    max_files = 1000000;
  }
  if( total_files >= max_files ) {
    return ( (double) max_files ) / (double) total_files;
  }
  // otherwise, return 1.0
  return 1.0;
}
