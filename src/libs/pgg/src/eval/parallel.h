#pragma once

// Minimal std::thread pool for per-element field/geometry loops (spec §12.4,
// N7; pgg links no Qt). parallelFor splits [0, count) into disjoint chunks
// distributed over the pool; every chunk writes only its own index range, so
// results are bit-identical to the sequential loop by construction (N1).
//
// Determinism rules honored here: no shared mutable state between chunks, no
// atomics-based order-dependent accumulation (chunk dispatch order does not
// influence any written value), reductions stay outside parallelFor.
//
// Anti-nesting: a parallelFor invoked while this thread is already executing
// parallelFor chunk code (pool worker or the dispatching caller) runs inline
// — the pool keeps a single outstanding job, and the engine's parallel loops
// never nest, so the inline path is the semantics we want anyway.
// Loops below kParallelThreshold elements also run inline (not worth the
// dispatch); tests that exercise the pool path use >= 40k elements.

#include <cstddef>
#include <functional>

namespace pgg {

inline constexpr size_t kParallelThreshold = 4096;

// Resolved lane count for a `threads` request: 0 -> hardware concurrency.
unsigned resolveThreadCount(unsigned threads);

// Chunked parallel-for over [0, count): fn(begin, end) is invoked on
// disjoint, ascending index ranges covering every element exactly once.
// threads: 0 = hardware default, 1 = forced sequential (inline). Runs inline
// when below the threshold, when threads resolves to 1, or when already on a
// pool worker (nested call).
void parallelFor(size_t count, unsigned threads, const std::function<void(size_t begin, size_t end)>& fn);

}  // namespace pgg
