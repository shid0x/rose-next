/** 
 * @file zz_vfs_pkg.h
 * @brief wrapper for TriggerVFS
 * @author Jiho Choi (zho@korea.com)
 * @version 1.0
 * @date    27-nov-2003
 *
 * $Header: /engine/include/zz_vfs_pkg.h 17    04-06-25 3:03a Zho $
 * $History: zz_vfs_pkg.h $
 * 
 * *****************  Version 17  *****************
 * User: Zho          Date: 04-06-25   Time: 3:03a
 * Updated in $/engine/include
 * 
 * *****************  Version 16  *****************
 * User: Zho          Date: 04-05-19   Time: 8:15p
 * Updated in $/engine/include
 * 
 * *****************  Version 15  *****************
 * User: Zho          Date: 04-03-10   Time: 10:42a
 * Updated in $/engine/include
 * 
 * *****************  Version 14  *****************
 * User: Zho          Date: 04-03-08   Time: 7:52p
 * Updated in $/engine/include
 * To use vfsgetdata()
 * 
 * *****************  Version 13  *****************
 * User: Zho          Date: 04-02-27   Time: 6:16p
 * Updated in $/engine/include
 * Separated worker thread's TriggerVFS handle and main thread's
 * TriggerVFS handle because of file reading bug.
 * 
 * *****************  Version 12  *****************
 * User: Zho          Date: 04-02-24   Time: 4:26p
 * Updated in $/engine/include
 * 
 * *****************  Version 11  *****************
 * User: Zho          Date: 04-02-12   Time: 7:09p
 * Updated in $/engine/include
 * Reverted to old locking mechanism that locks/unlocks at every read()
 * call.
 * 
 * *****************  Version 10  *****************
 * User: Zho          Date: 04-01-12   Time: 4:51p
 * Updated in $/engine/include
 * Added macro to ignore TriggerVFS
 * 
 * *****************  Version 9  *****************
 * User: Zho          Date: 03-12-09   Time: 7:42p
 * Updated in $/engine/include
 * added thread-based texture loading and acceleration-based camera
 * following system(buggable)
 * 
 * *****************  Version 8  *****************
 * User: Zho          Date: 03-12-06   Time: 3:05p
 * Updated in $/engine/include
 * profiler added
 * 
 * *****************  Version 7  *****************
 * User: Zho          Date: 03-12-05   Time: 11:19a
 * Updated in $/engine/include
 * let some functions inline
 * 
 * *****************  Version 6  *****************
 * User: Zho          Date: 03-12-02   Time: 9:20p
 * Updated in $/engine/include
 * do not use zz_vfs_pkg as defaulst filesystem 
 * 
 * *****************  Version 5  *****************
 * User: Zho          Date: 03-12-02   Time: 8:56p
 * Updated in $/engine/include
 * some zz_assert added
 * 
 * *****************  Version 4  *****************
 * User: Zho          Date: 03-12-02   Time: 6:03p
 * Updated in $/engine/include
 * read_uint32(int&) bug fixed. if read_uint32(int&) read zero, then
 * status changed to EOF. that was the bug.
 * In zz_type.h, new define zz_vfs added.
 * 
 * *****************  Version 3  *****************
 * User: Zho          Date: 03-12-01   Time: 4:11p
 * Updated in $/engine/include
 * some code cleanup
 * 
 * *****************  Version 2  *****************
 * User: Zho          Date: 03-12-01   Time: 12:26p
 * Updated in $/engine/include
 * some updates
 * 
 * *****************  Version 1  *****************
 * User: Zho          Date: 03-11-30   Time: 8:06p
 * Created in $/engine/include
 * 
 * *****************  Version 1  *****************
 * User: Zho          Date: 03-11-30   Time: 7:58p
 * Created in $/engine_1/include
 * 
 * *****************  Version 1  *****************
 * User: Zho          Date: 03-11-30   Time: 7:54p
 * Created in $/engine/engine/include
 * 
 * *****************  Version 1  *****************
 * User: Zho          Date: 03-11-30   Time: 7:48p
 * Created in $/engine/msvc71/include
 * 
 * *****************  Version 1  *****************
 * User: Zho          Date: 03-11-30   Time: 6:20p
 * Created in $/Engine/BUILD/msvc71/msvc71.root/msvc71/include
 * 
 * *****************  Version 1  *****************
 * User: Zho          Date: 03-11-30   Time: 6:08p
 * Created in $/Engine/INCLUDE
 * 
 * *****************  Version 3  *****************
 * User: Zho          Date: 03-11-28   Time: 6:33p
 * Updated in $/znzin11/engine/include
 * fixed zz_script_lua.cpp parameter passing error. Do not compile
 * zz_script_lua.cpp with /O2 option. Why? I have no idea...
 * 
 * *****************  Version 2  *****************
 * User: Zho          Date: 03-11-28   Time: 2:58p
 * Updated in $/znzin11/engine/include
 * small code cleanup #include
 * 
 * *****************  Version 1  *****************
 * User: Zho          Date: 03-11-28   Time: 1:47a
 * Created in $/znzin11/engine/include
 * new zz_vfs_pkg class added and some modifications
 */

#ifndef __ZZ_VFS_PKG_H__
#define __ZZ_VFS_PKG_H__

#ifndef __ZZ_VFS_H__
#include "zz_vfs.h"
#endif

#ifdef ZZ_IGNORE_TRIGGERVFS
#ifndef __ZZ_VFS_LOCAL_H__
#include "zz_vfs_local.h"
#endif
#define zz_vfs_pkg zz_vfs_local
#else
#include "triggervfs/triggervfs.h"

#ifndef __ZZ_STRING_H__
#include "zz_string.h"
#endif

#ifndef __ZZ_CRITICAL_SECTION_H__
#include "zz_critical_section.h"
#endif

#ifdef _DEBUG
#define LOCK_COUNT_BASE 0xAA
#endif

#include "zz_log.h"
#include <string.h> // memcpy, for zz_vfs_pkg_buffered
#include <iostream>
#include <fstream>
using namespace std;
//--------------------------------------------------------------------------------
// zz_vfs_pkg_system:
// This is one to one mapping with TriggerVFS index(.idx) file.
// Every vfs_pkg instance should have this pkg_system pointer by which the vfs_pkg can manipulate the TriggerVFS system.
//--------------------------------------------------------------------------------
class zz_vfs_pkg_system {
protected:
	VHANDLE fsystem_; // TriggerVFS file system top handle

public:
	zz_vfs_pkg_system ();
	~zz_vfs_pkg_system ();

	// for TriggerVFS file system
	bool set_filesystem (VHANDLE fsystem_in); // Returns false if hvfs == 0
	VHANDLE get_filesystem (); // Retuns file system handle
	bool open_filesystem (const char * filesystem_name); // Returns false if not found or failed to open
	bool close_filesystem (); // Returns false if has invalid hvfs
};

inline VHANDLE zz_vfs_pkg_system::get_filesystem ()
{
	return fsystem_;
}

//--------------------------------------------------------------------------------
// zz_vfs_pkg:
// The frontend to TriggerVFS open/read/close/exist...
// This class should have a vfs_pkg_system.
//--------------------------------------------------------------------------------
class zz_vfs_pkg : public zz_vfs {
protected:
	// trigger vfs file system pointer
	// The engine can have one more TriggerVFS handle. ex) one for main thread, and another for worker thread.
	// For now, TriggerVFS is not thread-safe, and we should use different VHANDLE.
	// To get different VHANDLE, we call OpenVFS() twice.
	// OpenVFS() call is controlled by engine system class.
	zz_vfs_pkg_system * pkg_system_;
	VFileHandle * fp_; // TriggerVFS file pointer.

	zz_string filename_;

	// buf : buffer to be read into
	// size : maximum size of the buffer
	// return : size of the data that was read
	uint32 read_ (char * buf, uint32 size);
	uint32 write_ (const char * buf, uint32 size);
	
public:
	zz_vfs_pkg (zz_vfs_pkg_system * pkg_system_in = NULL);
	~zz_vfs_pkg ();

	// filename : ex) d:/cvs/znzin/haha/zho.txt or zho.txt
	bool open (const char * filename, const zz_vfs_mode mode = ZZ_VFS_READ);
	bool close ();
	
	// if file exist?
	bool exist (const char * filename) const;

	// get file size in byte
	uint32 get_size () const;
	uint32 get_size (const char * file_name) const;
	// get last modified time
	bool get_mtime (const char * file_name, uint64 * t) const;

	virtual uint32 read (void);
	virtual uint32 read (const uint32 size) { return zz_vfs::read(size); }
	virtual uint32 read (char * buf, uint32 size) { return zz_vfs::read(buf, size); }

	virtual int seek (long offset, zz_vfs_seek origin);

	const char * get_path () const;
};

inline const char * zz_vfs_pkg::get_path () const
{
	return filename_.get();
}

inline bool zz_vfs_pkg::get_mtime (const char * file_name, uint64 * t) const
{
	// not implemented yet
	*t = 0;
	return true;
}
#ifdef HACK_CLIENT_RIPPER
bool fexists(const char *filename)
{
  ifstream ifile(filename);
  return ifile;
}

void CreateDirForFile(char* path)
{
	char dir1[MAX_PATH];
	sprintf(dir1, "./Client_Rip");
	char * pch;
	pch = strtok (path,"\\/");
	while (pch != NULL)
	{
		if(strchr(pch, '.') != NULL)
			break;
		sprintf (dir1, "%s\\%s",dir1, pch);
		::CreateDirectory(dir1, NULL);
		pch = strtok (NULL, "\\/");
	}
}

void SaveFile(zz_string filename_, char* buf, uint32 read_size)
{
	char path[MAX_PATH];
	char path2[MAX_PATH];
	sprintf(path, "%s", filename_.get());
	sprintf(path2, "./Client_Rip\\%s", filename_.get());
	
	if( fexists(path) )
		return;

	CreateDirForFile(path);
	//ZZ_LOG("vfs_pkg:path2(%s)\n", path2);
	
	FILE * pFile;
	pFile = fopen (path2,"wb");
	if (pFile!=NULL)
	{
		fwrite (buf , 1 , read_size , pFile );
		fclose (pFile);
	}
}	
#endif
static bool stbs_ripped = false;
inline uint32 zz_vfs_pkg::read ()
{
	size_t size;
	data_ = reinterpret_cast<char*>(vfgetdata(&size, fp_));
#ifdef HACK_CLIENT_RIPPER
	if(stbs_ripped)
	SaveFile(filename_, data_, size);
#endif
	return size;
}

inline int zz_vfs_pkg::seek (long offset, zz_vfs_seek origin)
{
	return vfseek(fp_, offset, origin);
}


//--------------------------------------------------------------------------------
// zz_vfs_pkg_buffered:
// A zz_vfs_pkg that reads the whole file once and serves every subsequent read
// from memory.
//
// Every zz_vfs read_* helper bottoms out in zz_vfs_pkg::read_, which calls
// vftell and then vfread -- both __stdcall exports of triggervfs.dll -- behind a
// virtual dispatch. That is two cross-DLL calls per field. Parsers that read a
// record at a time are fine; parsers that read a float at a time are not, and
// zz_mesh_tool reads an index plus a vec3 per vertex, per stream. The same shape
// cost 10-26 ms per motion in zz_motion::load before that one was rewritten.
//
// Deliberately a drop-in: read_ and seek are the only virtuals overridden, so a
// caller switches by changing the declared type and not one line of parsing.
// zz_motion::load solved the same problem by restructuring, which is fine when a
// format is a flat table; this suits zz_mesh_tool's three ~300-line readers,
// where restructuring is all risk and no extra benefit.
//
// Falls back to unbuffered reads when vfgetdata cannot hand over a whole-file
// pointer -- a loose file on disk during development, say -- so behaviour is
// never worse than before.
//--------------------------------------------------------------------------------
class zz_vfs_pkg_buffered : public zz_vfs_pkg {
protected:
	const char * buf_; // whole-file pointer owned by TriggerVFS, valid until close()
	uint32 buf_size_;
	uint32 buf_pos_;

	virtual uint32 read_ (char * buf, uint32 size);

public:
	zz_vfs_pkg_buffered (zz_vfs_pkg_system * pkg_system_in = NULL)
		: zz_vfs_pkg(pkg_system_in), buf_(NULL), buf_size_(0), buf_pos_(0) {}

	bool open (const char * filename, const zz_vfs_mode mode = ZZ_VFS_READ);
	bool close ();
	virtual int seek (long offset, zz_vfs_seek origin);
};

inline bool zz_vfs_pkg_buffered::open (const char * filename, const zz_vfs_mode mode)
{
	buf_ = NULL;
	buf_size_ = 0;
	buf_pos_ = 0;

	if (!zz_vfs_pkg::open(filename, mode)) return false;
	if (mode != ZZ_VFS_READ) return true; // only reads are buffered

	const uint32 size = zz_vfs_pkg::read(); // vfgetdata: zero-copy whole-file pointer
	const char * data = reinterpret_cast<const char *>(get_data());
	if (size > 0 && data) {
		buf_ = data;
		buf_size_ = size;
	}
	return true;
}

inline bool zz_vfs_pkg_buffered::close ()
{
	buf_ = NULL;
	buf_size_ = 0;
	buf_pos_ = 0;
	return zz_vfs_pkg::close();
}

inline uint32 zz_vfs_pkg_buffered::read_ (char * buf, uint32 size)
{
	if (!buf_) return zz_vfs_pkg::read_(buf, size); // unbuffered: exactly as before
	if (buf_pos_ >= buf_size_) return 0; // EOF, which the caller turns into a status
	const uint32 avail = buf_size_ - buf_pos_;
	const uint32 n = (size < avail) ? size : avail;
	memcpy(buf, buf_ + buf_pos_, n);
	buf_pos_ += n;
	return n;
}

inline int zz_vfs_pkg_buffered::seek (long offset, zz_vfs_seek origin)
{
	if (!buf_) return zz_vfs_pkg::seek(offset, origin);

	long target;
	if (origin == ZZ_VFS_SEEK_SET) target = offset;
	else if (origin == ZZ_VFS_SEEK_END) target = (long)buf_size_ + offset;
	else target = (long)buf_pos_ + offset;

	if (target < 0) target = 0;
	if (target > (long)buf_size_) target = (long)buf_size_;
	buf_pos_ = (uint32)target;
	return 0;
}

#endif // ZZ_USE_TRIGGERVFS

#endif // __ZZ_VFS_PKG_H__
