#ifndef _ROSE_CLIENT_CRASHHANDLER_H_
#define _ROSE_CLIENT_CRASHHANDLER_H_

namespace Rose {
namespace Client {

/// Last-resort crash capture.
///
/// The client had no unhandled-exception filter and no minidump writer, so a
/// crash left nothing to work with: the engine log (`error.txt`) is buffered and
/// still ends at the *previous* run's "log: end.", and `client.log` simply stops
/// mid-line. Diagnosing anything meant re-running under cdb
/// (scripts/debug-client-crash.ps1) and reproducing on demand -- which does not
/// work for an intermittent fault, and changes frame timing enough that a race
/// may not reproduce at all.
///
/// Installing this makes the *next* crash capture itself during ordinary play:
///   <cwd>\crash-YYYYMMDD-HHMMSS.dmp   minidump, openable in WinDbg/VS
///   <cwd>\crash-YYYYMMDD-HHMMSS.txt   one page: exception (and the thrown C++
///                                     type), faulting module+RVA, registers,
///                                     address-space use, and both clocks for
///                                     correlating against client.log
///
/// It holds 32 MB of address space in reserve and frees it at fault time, so an
/// out-of-memory crash still has room to write its dump.
///
/// Call once, as early in WinMain as possible. Configuration comes from
/// [LOG] CRASHDUMP in rose-next.ini (ROSE_CRASHDUMP overrides):
///   normal (default) - stacks, data segments, thread/handle data, the memory map
///                      (for `!address -summary`). Tens of MB.
///   full             - adds the entire address space. Hundreds of MB; use when
///                      a normal dump did not carry the memory you needed.
///   off              - do not install the filter at all.
void InstallCrashHandler(void);

} // namespace Client
} // namespace Rose

#endif // _ROSE_CLIENT_CRASHHANDLER_H_
