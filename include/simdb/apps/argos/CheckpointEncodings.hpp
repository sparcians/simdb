// <CheckpointEncodings.hpp> -*- C++ -*-

// clang-format off

#pragma once

/*!
 * \file CheckpointEncodings.hpp
 * \brief Reference for wire delta encodings emitted by PipelineStager checkpointers.
 *
 * The PipelineStager stage in ArgosCollector calls each collectable's checkpointer
 * (`ScalarCheckpointer`, `ContigContainerCheckpointer`, `SparseContainerCheckpointer`)
 * to turn staged checkpoints into wire records. Each record is prefixed with:
 *
 *   [uint16_t cid]     collectable ID
 *   [uint8_t action]   encoding (see Action in Checkpoint.hpp)
 *
 * Delta classification lives in CheckpointDeltas.hpp (`classifyContigChange`,
 * `classifySparseChange`). Heartbeat forcing, absent-CID refresh, and CLOSED priming
 * are handled in Checkpointer.hpp (`CollectableCheckpointer`).
 *
 * \par Encoding reference (tier 1)
 *
 * \verbatim
 * Encoding                 Applies To       Used When
 * ------------------------ ---------------- ------------------------------------------------------------------------------
 * CLOSED                   All              Collectable transitions to closed (`closeRecord`). The checkpointer may emit a
 *                                           priming FULL first when the last snapshot is outside the current heartbeat
 *                                           window.
 * FULL                     All              First data checkpoint (no prior baseline); heartbeat forces a snapshot; delta
 *                                           chain reaches `heartbeat - 1` consecutive non-FULL records; a CLOSED event
 *                                           occurred since the prior data checkpoint; scalar bytes changed; container
 *                                           change pattern is not covered by any delta; collectable did not stage this
 *                                           window but heartbeat refresh requires re-emitting state
 *                                           (`shouldRefreshAbsentCid_`).
 * CARRY                    All              Payload unchanged since the prior data checkpoint and none of the FULL forcing
 *                                           rules apply.
 * CONTAINER_SWAP           Contig, Sparse   Same occupied count; exactly one bin's value changed (same index).
 * CONTAINER_MULTI_SWAP     Contig, Sparse   Same occupied count; two or more bin values changed with no adds or removes.
 * CONTIG_ARRIVE            Contig only      Size +1; prefix unchanged; one new element appended at the tail.
 * CONTIG_DEPART            Contig only      Size -1; front pop — `curr[i] == prev[i+1]` for all remaining indices.
 * CONTIG_BOOKENDS          Contig only      Same size; shift-left with new tail — `curr[i] == prev[i+1]` for all but the
 *                                           last index (last bin is a new value).
 * CONTIG_MIMO              Contig only      FIFO MIMO: D elements depart from the front and A arrive at the tail; after
 *                                           the depart shift, prefixes match; `D + A > 1`; not a simpler single-op pattern
 *                                           (e.g. not 1 depart + 1 arrive at same size).
 * SPARSE_REMOVE            Sparse only      Exactly one bin removed; no value changes or adds elsewhere.
 * SPARSE_ADD               Sparse only      Exactly one new bin added; all overlapping bins unchanged.
 * SPARSE_MULTI_REMOVE      Sparse only      Two or more bins removed; no adds and no value changes on survivors.
 * \endverbatim
 *
 * Scalars only ever emit CLOSED, FULL, or CARRY.
 *
 * \par FULL forcing (checkpointer layer)
 *
 * These conditions override delta classification and force FULL regardless of change size:
 *
 * - \c forceSnapshot_(sim_time) — either `distance_to_snapshot_ + 1 >= heartbeat_`, or the
 *   last FULL sim-time is outside the current heartbeat window (`shouldHeartbeatRefresh_`).
 * - No prior data checkpoint — first collection for this CID.
 * - Closed since prior data checkpoint — a CLOSED lifecycle node sits between the current
 *   data checkpoint and the previous one.
 * - Absent CID refresh — collectable did not participate in this window but heartbeat
 *   policy still requires a wire record (replays latest snapshot as FULL, or CLOSED plus
 *   optional priming FULL if already closed).
 *
 * \par Wire payloads (after the action byte)
 *
 * | Action                 | Payload                                                |
 * |------------------------|--------------------------------------------------------|
 * | CLOSED                 |                                                        |
 * | CARRY                  |                                                        |
 * | FULL (scalar)          | [scalar bytes]                                         |
 * | FULL (contig)          | [count][bin bytes]..[bin bytes]                        |
 * | FULL (sparse)          | [count][bin idx][bin bytes]..[bin idx][bin bytes]      |
 * | CONTAINER_SWAP         | [bin idx][bin bytes]                                   |
 * | CONTAINER_MULTI_SWAP   | [count][bin idx][bin bytes]..[bin idx][bin bytes]      |
 * | CONTIG_ARRIVE          | [pushed bytes]                                         |
 * | CONTIG_DEPART          |                                                        |
 * | CONTIG_BOOKENDS        | [pushed bytes]                                         |
 * | CONTIG_MIMO            | [num popped][num pushed][pushed bytes]..[pushed bytes] |
 * | SPARSE_REMOVE          | [removed idx]                                          |
 * | SPARSE_ADD             | [added idx][added bytes]                               |
 * | SPARSE_MULTI_REMOVE    | [num removed][removed idx]..[removed idx]              |
 *
 * \par Related implementation files
 *
 * - Checkpoint.hpp:       Action enum, encode/decode per checkpoint type
 * - CheckpointDeltas.hpp: classifyContigChange, classifySparseChange
 * - Checkpointer.hpp:     heartbeat bookkeeping, encodeForPipeline orchestration
 * - ArgosCollector.hpp:   PipelineStager invokes checkpointer encoding
 */

// clang-format on
