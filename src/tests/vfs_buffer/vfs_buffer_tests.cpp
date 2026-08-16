// Differential test for CFileSystemTriggerVFS's sequential read-ahead buffer.
//
// WHY THIS EXISTS
// ---------------
// Read() used to issue one ::vfread per call -- a memset plus an fseek+fread
// round trip through the CRT for as little as one byte. Every typed reader
// (ReadFloat, ReadInt32, ReadByte, ...) funnels into it, and the terrain loader
// parses scalar by scalar, so a 19 KB .HIM heightfield cost ~4,900 of those
// calls (~3 ms of pure call overhead per map chunk).
//
// The fix buffers reads, which means m_lLogicalPos -- not the underlying
// VFileHandle -- became the authoritative file position. Tell(), Seek() and
// IsEOF() all had to be rewritten around it. A single off-by-one there
// corrupts every map load, so the buffered implementation is checked here
// against ground truth obtained from raw VOpenFile/vfread on the same file.
//
// It compiles the real translation unit (not a copy), so it cannot drift from
// what ships.
//
// RUNNING
// -------
// Needs a real data.idx + .vfs, so run it from a deployed game directory:
//
//     cd <game dir> && vfs_buffer_tests.exe
//     vfs_buffer_tests.exe <game dir>
//
// With no game data present it SKIPS rather than fails, so it is harmless to
// run from bin/<config> as part of a build check.

#include "stdafx.h"

#include "CFileSystemTriggerVFS.h"

#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void
Check(bool ok, const char* what, long detail = -1) {
    ++g_checks;
    if (ok) {
        return;
    }
    ++g_failures;
    if (detail >= 0) {
        printf("  FAIL: %s (at %ld)\n", what, detail);
    } else {
        printf("  FAIL: %s\n", what);
    }
}

/// Ground truth: whole file, one raw vfread, on its own handle.
bool
SlurpRaw(VHANDLE hVFS, const char* name, std::vector<unsigned char>& out) {
    VFileHandle* vf = ::VOpenFile(name, hVFS);
    if (!vf) {
        return false;
    }
    const size_t size = ::vfgetsize(vf);
    out.assign(size, 0);
    if (size > 0) {
        ::vfread(&out[0], 1, size, vf);
    }
    ::VCloseFile(vf);
    return true;
}

void
TestFile(VHANDLE hVFS, const char* name) {
    std::vector<unsigned char> truth;
    if (!SlurpRaw(hVFS, name, truth)) {
        printf("  skip (not in vfs): %s\n", name);
        return;
    }
    const long size = (long)truth.size();
    printf("  %-62s %7ld bytes\n", name, size);

    CFileSystemTriggerVFS fs;
    fs.SetVFS(hVFS);
    Check(fs.OpenFile(name), "OpenFile");
    Check(fs.Tell() == 0, "Tell()==0 after open");

    /// 1. Sequential 4-byte reads -- the .HIM heightfield access pattern, and
    ///    the one the buffer exists for.
    std::vector<unsigned char> got(size, 0xCD);
    long pos = 0;
    while (pos < size) {
        unsigned char tmp[4];
        const int want = (size - pos >= 4) ? 4 : (int)(size - pos);
        const int n = fs.Read(tmp, 4);
        Check(n == want, "scalar read count", pos);
        for (int i = 0; i < n; ++i) {
            got[pos + i] = tmp[i];
        }
        /// A short read must still leave the caller's tail zeroed, as the
        /// unbuffered version did -- some parsers rely on it for name buffers.
        for (int i = n; i < 4; ++i) {
            Check(tmp[i] == 0, "short read zero-fill", pos);
        }
        pos += n;
        Check(fs.Tell() == pos, "Tell() tracks scalar reads", pos);
        if (n == 0) {
            break;
        }
    }
    Check(got == truth, "sequential 4-byte reads match raw content");
    Check(fs.IsEOF(), "IsEOF() at end");

    /// 2. Reading past EOF yields nothing and does not move.
    unsigned char scratch[16];
    Check(fs.Read(scratch, 16) == 0, "read at EOF returns 0");
    Check(fs.Tell() == size, "Tell() unchanged at EOF");

    /// 3. One large read (exercises the bypass path for reads >= buffer size).
    Check(fs.Seek(0, FILE_POS_SET), "Seek to 0");
    Check(fs.Tell() == 0, "Tell()==0 after seek");
    if (size > 0) {
        std::vector<unsigned char> whole(size, 0xCD);
        Check(fs.Read(&whole[0], size) == size, "single large read count");
        Check(whole == truth, "single large read matches");
    }

    /// 4. Seek/read storm across all three origins. Covers seeks that land
    ///    inside the buffered window (the .IFO lump walk does
    ///    Tell -> Seek(lump) -> read -> Seek(back) for every lump) as well as
    ///    ones that invalidate it.
    unsigned long rng = 12345;
    for (int iter = 0; iter < 4000; ++iter) {
        rng = rng * 1103515245u + 12345u;
        const long at = (size > 0) ? (long)((rng >> 8) % (unsigned long)size) : 0;
        rng = rng * 1103515245u + 12345u;
        const int len = 1 + (int)((rng >> 8) % 300);

        const int mode = iter % 3;
        if (mode == 0) {
            Check(fs.Seek(at, FILE_POS_SET), "Seek SET");
        } else if (mode == 1) {
            Check(fs.Seek(0, FILE_POS_SET), "Seek SET 0");
            Check(fs.Seek(at, FILE_POS_CUR), "Seek CUR");
        } else {
            Check(fs.Seek(at - size, FILE_POS_END), "Seek END");
        }
        Check(fs.Tell() == at, "Tell() after seek", at);

        std::vector<unsigned char> buf(len, 0xCD);
        const int n = fs.Read(&buf[0], len);
        const int expect = (size - at >= len) ? len : (int)(size - at);
        Check(n == expect, "seeked read count", at);
        for (int i = 0; i < n; ++i) {
            if (buf[i] != truth[at + i]) {
                Check(false, "seeked read content", at + i);
                break;
            }
        }
        for (int i = n; i < len; ++i) {
            Check(buf[i] == 0, "seeked short-read zero-fill", at);
        }
        Check(fs.Tell() == at + n, "Tell() after seeked read", at);
    }

    /// 5. vfseek clamps out-of-range requests and reports success; Seek() must
    ///    mirror that rather than failing, or Tell() drifts from the old
    ///    behaviour on an over-seek.
    Check(fs.Seek(size + 9999, FILE_POS_SET), "over-seek reports success");
    Check(fs.Tell() == size, "over-seek clamps to size");
    Check(fs.Seek(-9999, FILE_POS_SET), "negative seek reports success");
    Check(fs.Tell() == 0, "negative seek clamps to 0");

    fs.CloseFile();

    /// 6. These objects are pooled and reused across files by CVFSManager, so
    ///    stale buffer state must not be served as the next file's content.
    Check(fs.OpenFile(name), "re-open same object");
    Check(fs.Tell() == 0, "Tell()==0 after re-open");
    if (size > 0) {
        std::vector<unsigned char> again(size, 0xCD);
        fs.Read(&again[0], size);
        Check(again == truth, "content after pooled re-open");
    }
    fs.CloseFile();
}

double
NowMs() {
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

/// Quantify the buffer by replaying the .HIM access pattern (one 4-byte read
/// per float) both ways on a warm cache. Informational -- never fails the run.
void
Benchmark(VHANDLE hVFS, const char* name) {
    VFileHandle* probe = ::VOpenFile(name, hVFS);
    if (!probe) {
        return;
    }
    const long size = (long)::vfgetsize(probe);
    ::VCloseFile(probe);

    const int kReps = 20;
    unsigned char tmp[4];

    double t0 = NowMs();
    for (int r = 0; r < kReps; ++r) {
        VFileHandle* vf = ::VOpenFile(name, hVFS);
        for (long p = 0; p + 4 <= size; p += 4) {
            memset(tmp, 0, 4);
            ::vfread(tmp, 1, 4, vf);
        }
        ::VCloseFile(vf);
    }
    const double unbuffered = (NowMs() - t0) / kReps;

    t0 = NowMs();
    for (int r = 0; r < kReps; ++r) {
        CFileSystemTriggerVFS fs;
        fs.SetVFS(hVFS);
        fs.OpenFile(name);
        for (long p = 0; p + 4 <= size; p += 4) {
            fs.Read(tmp, 4);
        }
        fs.CloseFile();
    }
    const double buffered = (NowMs() - t0) / kReps;

    printf("  %-58s %5ld reads  %6.3f -> %6.3f ms  (%.1fx)\n",
        name,
        size / 4,
        unbuffered,
        buffered,
        (buffered > 0.0) ? unbuffered / buffered : 0.0);
}

const char* kFiles[] = {
    "3DDATA\\MAPS\\JUNON\\JG01\\32_32.HIM", // 19 KB, the scalar-read hot spot
    "3DDATA\\MAPS\\JUNON\\JG01\\32_32.TIL", // 1.8 KB, smaller than one buffer
    "3DDATA\\MAPS\\JUNON\\JZ01_1\\33_32.IFO", // 22 KB, seek-heavy lump walk
    "3DDATA\\MAPS\\JUNON\\JG01\\32_32\\LightMap\\ObjectLightMapData.lit", // 22 KB
    "3DDATA\\MAPS\\ELDEON\\EJ01\\31_31\\LightMap\\BuildingLightMapData.lit", // 25 B
    "3DDATA\\MAPS\\JUNON\\JG01\\32_32\\32_32_PlaneLightingMap.dds", // 350 KB, > buffer
};

} // namespace

int
main(int argc, char** argv) {
    /// Optional game-directory argument; otherwise use the working directory.
    /// CVFS resolves the .vfs path relative to the CWD, so we must actually
    /// chdir rather than just pass a longer name.
    if (argc > 1 && !::SetCurrentDirectoryA(argv[1])) {
        printf("could not enter game dir: %s\n", argv[1]);
        return 2;
    }

    VHANDLE hVFS = ::OpenVFS("data.idx", "r");
    if (!hVFS) {
        printf("SKIP: no data.idx in the working directory.\n");
        printf("      Run from a deployed game dir, or pass one:\n");
        printf("      vfs_buffer_tests.exe <game dir>\n");
        return 0;
    }

    for (int i = 0; i < (int)(sizeof(kFiles) / sizeof(kFiles[0])); ++i) {
        TestFile(hVFS, kFiles[i]);
    }

    printf("\nscalar-read benchmark (warm cache, 20 passes each):\n");
    Benchmark(hVFS, kFiles[0]);
    Benchmark(hVFS, kFiles[1]);
    Benchmark(hVFS, kFiles[3]);

    ::CloseVFS(hVFS);

    if (g_failures == 0) {
        printf("\nvfs_buffer_tests passed (%d checks)\n", g_checks);
        return 0;
    }
    printf("\nvfs_buffer_tests FAILED: %d of %d checks\n", g_failures, g_checks);
    return 1;
}
