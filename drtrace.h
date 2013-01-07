#pragma once

#include <stdint.h>

/** Basic block identifier. */
typedef uint32_t bb_id_t;
#define BB_ID_FMT "0x%x"

/** Basic block translation information. */
#define TYPE_BB 0x42424242
struct bb_t {
  /** Identifier. */
  bb_id_t id;

  /** Address of first instruction. */
  uintptr_t pc;

  /** Raw bytes. */
  uint8_t code[];
};

/** Basic block deletion information. */
#define TYPE_BB_DEL 0x44444444
struct bb_del_t {
  /** Identifier of deleted basic block. */
  bb_id_t bb_id;
};

/** Instruction execution information. */
#define TYPE_TRACE 0x30303030
struct trace_t {
  /** Identifier of thread that performed execution. */
  uint32_t thread_id;

  /** Basic block identifiers. */
  bb_id_t bb_id[];
};

/** Type, length and value. */
struct tlv_t {
  /** Type. */
  uint32_t type;

  /** Full size, including header. */
  uint32_t length;

  /** Value. */
  uint8_t value[];
};
