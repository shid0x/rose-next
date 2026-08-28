/** 
 * @file zz_fast_reader.h
 * @brief class for quickly reading from files
 * @author Brett Lawson (brett19@gmail.com)
 * @version 1.0
 * @date 25-jan-2011
 */

#ifndef __ZZ_FAST_READER_H__
#define __ZZ_FAST_READER_H__

#ifndef	__ZZ_VFS_H__
#include "zz_vfs.h"
#endif

#include <string.h> // memset, for load_padded

/// Bulk-load a byte range once, then parse it from memory with inline pointer
/// reads. Written in 2011 and never wired up to anything; zz_motion::load is the
/// first user, because reading a motion field-by-field through the VFS costs two
/// cross-DLL calls (vftell + vfread into triggervfs) plus a virtual dispatch per
/// 4-byte float. A 30-bone, 200-frame motion is ~24,000 floats, which measured at
/// 10-26 ms per motion inside the spawn packet handler.
class zz_fast_reader {
protected:
	char* data_;
	char* cursor_;

public:
	zz_fast_reader( ) : data_(NULL), cursor_(NULL) {
	}

	~zz_fast_reader( ) {
		unload( );
	}

	bool load( zz_vfs& vfs, uint32 size ) {
		unload();
		if( size == 0 ) return true;
		data_ = zz_new char[ size ];
		if( vfs.read( data_, size ) != size ) {
			// zz_delete[]: data_ comes from zz_new char[], and the scalar form
			// here was a latent new[]/delete mismatch. Harmless for char on MSVC,
			// but this is a hot path now, so do not leave it wrong.
			zz_delete [] data_;
			data_ = cursor_ = NULL;
			assert(!"not enough data in the file");
			return false;
		}
		cursor_ = data_;
		return true;
	}

	/// Like load(), but tolerates a file that ends early: the shortfall is left
	/// zeroed and the call still succeeds.
	///
	/// Exists because the field-by-field reader this replaced could not fail. A
	/// short read there just left the last value in the destination and carried
	/// on, so a truncated asset produced a visibly wrong animation, never a
	/// missing one. Failing hard instead would turn that into an NPC that does
	/// not spawn at all -- a cosmetic fault promoted to a functional one, which
	/// is exactly the regression the engine's degrade-don't-kill rule exists to
	/// prevent. Zeros are at least deterministic, unlike the stale-value garbage
	/// the old path produced.
	bool load_padded( zz_vfs& vfs, uint32 size, uint32& read_out ) {
		unload();
		read_out = 0;
		if( size == 0 ) return true;
		data_ = zz_new char[ size ];
		if( !data_ ) return false;
		memset( data_, 0, size );
		read_out = vfs.read( data_, size );
		cursor_ = data_;
		return true;
	}

	void unload( ) {
		ZZ_SAFE_DELETE_ARRAY(data_);
		cursor_ = 0;
	}
	
	inline void read_char (char& data_out) { data_out = *(*(reinterpret_cast<char**>(&cursor_)))++; }
	inline void  read_uchar (uchar& data_out) { data_out = *(*(reinterpret_cast<uchar**>(&cursor_)))++; }
	inline void  read_float (float& data_out) { data_out = *(*(reinterpret_cast<float**>(&cursor_)))++; }

	inline void  read_uint32 (uint32& data_out) { data_out = *(*(reinterpret_cast<uint32**>(&cursor_)))++; }
	inline void  read_uint32 (int& data_out) { data_out = *(*(reinterpret_cast<int**>(&cursor_)))++; }
	inline void  read_uint16 (uint16& data_out) { data_out = *(*(reinterpret_cast<uint16**>(&cursor_)))++; }
	inline void  read_int16 (int16& data_out) { data_out = *(*(reinterpret_cast<int16**>(&cursor_)))++; }
	inline void  read_int32 (int32& data_out) { data_out = *(*(reinterpret_cast<int32**>(&cursor_)))++; }

};

#endif //__ZZ_FAST_READER_H__