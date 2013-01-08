#define _LARGEFILE64_SOURCE
#define TRACE_FILE "./trace.out"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>

#include "drtrace.h"

class fd_t {
  int value_;
public:
  fd_t(int value) : value_(value) {}
  ~fd_t() {
    if(value_ != -1 && close(value_) == -1) {
      perror("warning: close() failed");
    }
  }
  operator int() { return value_; }
};

class mmap_t {
  void* value_;
  size_t size_;
public:
  mmap_t(void* value, size_t size) : value_(value), size_(size) {}
  ~mmap_t() {
    if(value_ != MAP_FAILED && munmap(value_, size_) == -1) {
      perror("warning: munmap() failed");
    }
  }

  template<typename T>
  operator T*() { return (T*)value_; }
};

void dump(void* p, size_t size) {
  if(size == 0) { return; }
  fprintf(stderr, "%.2x", ((unsigned char*)p)[0]);
  for(size_t i = 1; i < size; i++) {
    fprintf(stderr, " %.2x", ((unsigned char*)p)[i]);
  }
  fprintf(stderr, "\n");
}

#define container_of(ptr, type, member) \
    ((type*)((char*)(ptr) - offsetof(type, member)))

void dump_code(struct bb_t* bb) {
  struct tlv_t* tlv = container_of(bb, struct tlv_t, value);
  void* end = (char*)tlv + tlv->length;
  for(struct code_chunk_t* chunk = (struct code_chunk_t*)bb->chunks;
      chunk < end;
      chunk = (struct code_chunk_t*)&chunk->code[chunk->size]) {
    fprintf(stderr, "%p: ", (void*)chunk->pc);
    dump(chunk->code, chunk->size);
  }
}

size_t trace_count(struct trace_t* trace) {
  struct tlv_t* tlv = container_of(trace, struct tlv_t, value);
  size_t bytes = tlv->length - ((char*)trace->bb_id - (char*)tlv);
  return bytes / sizeof(trace->bb_id[0]);
}

struct bb_entry_t {
  struct bb_t* bb;
};

int main(int argc, char** argv) {
  bb_id_t track_bb = -1;
  for(int i = 1; i < argc; i++) {
    char* arg = argv[i];
    if(strcmp(arg, "--track-bb") == 0) {
      i++;
      if(i >= argc) {
        fprintf(stderr, "fatal: missing --track-bb value\n");
        return 1;
      }
      sscanf(argv[i], BB_ID_FMT, &track_bb);
      fprintf(stderr, "info: tracking bb " BB_ID_FMT "\n", track_bb);
    }
  }

  fd_t fd(open(TRACE_FILE, O_RDONLY | O_LARGEFILE));
  if(fd == -1) {
    perror("fatal: open() failed");
    return 1;
  }

  struct stat s;
  int stat_rc = fstat(fd, &s);
  if(stat_rc == -1) {
    perror("fatal: stat() failed");
    return 1;
  }
  size_t size = s.st_size;
  fprintf(stderr, "info: size = %zu\n", size);

  mmap_t p(mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0), size);
  if(p == MAP_FAILED) {
    perror("fatal: mmap() failed");
    return 1;
  }

  struct tlv_t* previous_tlv = NULL;
  void* last = (char*)p + size;
  size_t tlv_count = 0;
  std::unordered_map<bb_id_t, bb_entry_t> bbs;
  size_t bbs_executed = 0;
  for(struct tlv_t* tlv = (tlv_t*)p;
      tlv < last;
      previous_tlv = tlv,
      tlv = (struct tlv_t*)((char*)tlv + tlv->length),
      tlv_count++) {
    switch(tlv->type) {
    case TYPE_BB: {
      bb_entry_t entry;
      entry.bb = (bb_t*)tlv->value;
      auto it = bbs.find(entry.bb->id);
      if(it == bbs.end()) {
        bbs[entry.bb->id] = entry;
        if(entry.bb->id == track_bb) {
          fprintf(stderr,
                  "info: basic block " BB_ID_FMT " created, "
                  "TLV offset is 0x%tx\n",
                  entry.bb->id,
                  (char*)tlv - (char*)p);
          dump_code(entry.bb);
        }
      } else {
        fprintf(stderr,
                "warning: duplicate basic block " BB_ID_FMT " created\n",
                entry.bb->id);
      }
      break;
    }
    case TYPE_BB_DEL: {
      struct bb_del_t* bb_del = (struct bb_del_t*)tlv->value;
      auto it = bbs.find(bb_del->bb_id);
      if(it == bbs.end()) {
        fprintf(stderr,
                "warning: non-existent basic block " BB_ID_FMT " deleted, "
                "TLV offset is 0x%tx\n",
                bb_del->bb_id,
                (char*)tlv - (char*)p);
      } else {
        if(bb_del->bb_id == track_bb) {
          fprintf(stderr,
                  "info: basic block " BB_ID_FMT " deleted, "
                  "TLV offset is 0x%tx\n",
                  bb_del->bb_id,
                  (char*)tlv - (char*)p);
        }
        bbs.erase(it);
      }
      break;
    }
    case TYPE_TRACE: {
      struct trace_t* trace = (struct trace_t*)tlv->value;
      size_t count = trace_count(trace);
      for(size_t i = 0; i < count; i++) {
        auto it = bbs.find(trace->bb_id[i]);
        if(it == bbs.end()) {
          fprintf(stderr,
                  "warning: non-existent basic block " BB_ID_FMT " executed, "
                  "TLV offset is 0x%tx\n",
                  trace->bb_id[i],
                  (char*)tlv - (char*)p);
        }
        bbs_executed++;
      }
      break;
    }
    default:
      fprintf(stderr,
              "fatal: unexpected TLV type at offset 0x%tx: 0x%zx\n",
              (char*)tlv - (char*)p,
              (size_t)tlv->type);
      if(previous_tlv) {
        fprintf(stderr,
                "info: previous TLV is at offset 0x%tx\n",
                (char*)previous_tlv - (char*)p);
      } else {
        fprintf(stderr, "info: there is no previous TLV\n");
      }
      return 1;
    }
  }
  fprintf(stderr, "info: tlv count = %zu\n", tlv_count);
  fprintf(stderr, "info: bb count = %zu\n", bbs.size());
  fprintf(stderr, "info: bbs executed = %zu\n", bbs_executed);

  return 0;
}
