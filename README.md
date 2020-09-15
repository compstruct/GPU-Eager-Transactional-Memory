GPUs transactional memory (TM) proposals to date have relied on lazy, value-based conflict detection, assuming that GPUs can amortize the latency by executing other warps. In practice, however, concurrency must be throttled to a few warps per core to avoid high abort rates, and TM performance has remained far below that of fine-grained locks.

We trace this to the latency cost of validating transactions: two round trips across the crossbar required for most commits and aborts. With limited concurrency, the warp scheduler cannot amortize this, and leaves the core idle most of the time.

In this paper, we show that value-based validation does not scale to high thread counts, and eager conflict detection becomes more efficient as the number of threads grows. We leverage this insight to propose GETM, a GPU TM with eager conflict detection. GETM relies on a novel distributed logical clock scheme to implement eager conflict detection without the need for cache coherence or signature broadcasts.

GETM is up to 2.1 times faster than the state-of-the art prior work WarpTM (gmean 1.2 times), with 3.6 times lower silicon area overheads and 2.2 times lower power overheads.

# Paper Link

[High-Performance GPU Transactional Memory via Eager Conflict Detection](https://ieeexplore.ieee.org/abstract/document/8327012), in HPCA-2018.

# Building

This work was implemented upon [GPGPU-Sim](http://www.gpgpu-sim.org/). The original README of the GPGPU-Sim that we used can be found at [here](./README_GPGPUSim). 
