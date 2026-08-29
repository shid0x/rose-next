#include "rose/common/log.h"
#include "stdafx.h"

#include "VFSManager.h"
#include "CFileSystemNormal.h"
#include "CFileSystemTriggerVFS.h"

#include "rose/io/reader.h"
#include "rose/io/stb.h"

#include <algorithm>

using namespace Rose::IO;

CVFSManager __SingletonVFSManager;

CVFSManager::CVFSManager() {
    // Construct
    m_hVFile = NULL;
    m_iVFSType = VFS_NORMAL;
}

CVFSManager::~CVFSManager() {
    ReleaseAll();
}

/// Get current file system
CFileSystem*
CVFSManager::GetNewFileSystem(int iVFSType) {
    CFileSystem* pFileSystem = NULL;

    switch (iVFSType) {
        case VFS_NORMAL: {
            pFileSystem = (CFileSystem*)new CFileSystemNormal();
        } break;
        case VFS_TRIGGER_VFS: {
            if (m_hVFile == NULL) {
                pFileSystem = (CFileSystem*)new CFileSystemNormal();
                break;
            }

            CFileSystemTriggerVFS* pFsVFS = new CFileSystemTriggerVFS();
            pFsVFS->SetVFS(m_hVFile);
            pFileSystem = (CFileSystem*)pFsVFS;

        } break;
    }

    return pFileSystem;
}

bool
CVFSManager::InitVFS(int iVFSType, int iReserveCount /*=10*/) {
    ReleaseAll();

    m_iVFSType = iVFSType;

    if (iVFSType == VFS_TRIGGER_VFS && m_hVFile == NULL) {
        if (g_pCApp)
            g_pCApp->ErrorBOX("먼저 VFS를 설정하시오", "ERROR");
        return false;
    }

    CFileSystem* pFileSystem = NULL;
    for (int i = 0; i < iReserveCount; i++) {
        pFileSystem = GetNewFileSystem(iVFSType);

        if (pFileSystem == NULL) {
            ReleaseAll();
            return false;
        }

        m_VFSList.push_back(pFileSystem);
    }

    return true;
}

/// util functio for for_each
void
ReleaseSingleVFS(CFileSystem* pFileSystem) {
    if (pFileSystem != NULL) {
        delete pFileSystem;
        pFileSystem = NULL;
    }
}

void
CVFSManager::ReleaseAll() {
    std::for_each(m_VFSList.begin(), m_VFSList.end(), ReleaseSingleVFS);
    std::for_each(m_VFSUsedList.begin(), m_VFSUsedList.end(), ReleaseSingleVFS);

    m_VFSList.clear();
    m_VFSUsedList.clear();
}

CFileSystem*
CVFSManager::GetFileSystem() {
    CFileSystem* pFileSystem = NULL;

    if (m_VFSList.empty()) {
        pFileSystem = GetNewFileSystem(m_iVFSType);
        m_VFSList.push_back(pFileSystem);
    }

    pFileSystem = m_VFSList.back();
    m_VFSList.pop_back();

    m_VFSUsedList.push_back(pFileSystem);

    return pFileSystem;
}

void
CVFSManager::ReturnToManager(CFileSystem* pFileSystem) {
    m_VFSList.push_back(pFileSystem);

    m_VFSUsedList.remove(pFileSystem);
}

bool
CVFSManager::IsExistFile(const char* pFileName) {
    if (pFileName == NULL)
        return false;

    CFileSystem* pFileSystem = GetFileSystem();
    if (pFileSystem == NULL)
        return false;

    bool res = pFileSystem->IsExist(pFileName);
    ReturnToManager(pFileSystem);
    return res;
}

bool
CVFSManager::load_stb(STBDATA& stb, const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }

    std::string filepath = path.string();
    CFileSystem* fs = this->GetFileSystem();
    if (!fs || !fs->IsExist(filepath.c_str())) {
        return false;
    }

    if (!fs->OpenFile(filepath.c_str())) {
        return false;
    }

    /// Split read from parse. STB loading measured ~72-87 ms per MB at the
    /// character-select stall -- 13 MB/s to walk length-prefixed strings that
    /// are already in memory, which is two orders of magnitude off. The inner
    /// loop (reserve + move-construct per cell) does not explain it, so the two
    /// halves are timed separately rather than guessed at again.
    const DWORD t0 = ::timeGetTime();

    if (!fs->ReadToMemory()) {
        return false;
    }

    std::byte* ptr = reinterpret_cast<std::byte*>(fs->GetData());
    const size_t byte_count = fs->GetSize();
    std::vector<std::byte> data(ptr, ptr + byte_count);

    fs->ReleaseData();
    fs->CloseFile();
    this->ReturnToManager(fs);
    const DWORD t1 = ::timeGetTime();

    BinaryReader b;
    if (!b.open(std::move(data))) {
        return false;
    }
    const bool ok = stb.load(std::move(b));
    const DWORD t2 = ::timeGetTime();

    if ((t2 - t0) >= 3) {
        LOG_INFO("STB: {} ms [read={} parse={}] {:.0f} KB {}x{} ({})",
            (unsigned)(t2 - t0), (unsigned)(t1 - t0), (unsigned)(t2 - t1),
            byte_count / 1024.0, (unsigned)stb.row_count, (unsigned)stb.col_count,
            filepath.c_str());
    }
    return ok;
}