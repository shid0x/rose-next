#ifndef _ROSE_CLIENT_PROCESSMEMORY_H_
#define _ROSE_CLIENT_PROCESSMEMORY_H_

namespace Rose {
namespace Client {

/// Where the client's address space goes, in MB.
///
/// The client is 32-bit, so what runs out is address space, not RAM: 2 GB without
/// /LARGEADDRESSAWARE, 4 GB with it on 64-bit Windows. Task Manager's "Memory"
/// column is the private working set, which leaves out file views, reserved
/// ranges, the driver's DLLs (the NVIDIA user-mode driver alone maps ~140 MB) and
/// paged-out commit -- which is how a client that "uses 300-500 MB" threw
/// std::bad_alloc on two testers' machines on 2026-11-01. These are the numbers
/// that predict that.
struct ProcessMemory {
    unsigned int va_used_mb;      // total - available address space
    unsigned int va_total_mb;     // 2047, or 4095 once large-address-aware (the top 64 KB is never user space)
    unsigned int private_mb;      // commit charge (PrivateUsage)
    unsigned int working_set_mb;  // resident

    // Only filled by a full sample (bWalk); 0 otherwise.
    unsigned int image_mb;        // DLLs and the exe
    unsigned int mapped_mb;       // file views (rose.vfs reads, the D3D runtime...)
    unsigned int private_va_mb;   // heap, D3D staging, stacks -- committed or reserved
    unsigned int largest_free_mb; // biggest single allocation that can still succeed
};

/// Fills `out`. Allocates nothing on the heap, so it is safe inside the crash
/// handler. bWalk adds the per-type breakdown and the largest free block, which
/// walks the whole address space with VirtualQuery (a few thousand calls).
void SampleProcessMemory(ProcessMemory& out, bool bWalk);

/// One "Memory:" line in client.log, with the session peak.
void LogProcessMemory(const char* szReason, int iZoneNo);

/// Once per frame from the main game state: logs every minute and on every zone
/// change, so a session's log shows how address space grows and where.
void TickProcessMemoryLog(int iZoneNo);

/// For the debug HUD: a cheap sample, refreshed at most once a second.
const ProcessMemory& GetHudProcessMemory(unsigned int* pPeakVaMb);

} // namespace Client
} // namespace Rose

#endif // _ROSE_CLIENT_PROCESSMEMORY_H_
