// Minimal stand-in for the client's stdafx.h.
//
// This test compiles the REAL src/client/util/cfilesystemtriggervfs.cpp
// translation unit rather than a copy of its logic, so it needs the handful of
// headers that TU expects from the client's precompiled header. CFileSystem is
// a dependency-free abstract base, so nothing else from the client is required.
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <assert.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>
