/** 
 * @file zz_manager.h
 * @brief manager.
 * @author Jiho Choi (zho@korea.com)
 * @version 1.0
 * @date    25-feb-2002
 *
 * $Header: /engine/include/zz_manager.h 12    04-09-20 1:39p Zho $
 */

#ifndef __ZZ_MANAGER_H__
#define __ZZ_MANAGER_H__

#ifndef __ZZ_NODE_H__
#include "zz_node.h"
#endif

#ifndef __ZZ_WAITING_LINE_H__
#include "zz_waiting_line.h"
#endif

//--------------------------------------------------------------------------------
// zz_manager class :
//
// - manages all children nodes in similar manner,
// - takes charge of creating and deleting node (ex, spawn(), kill()),
// - is in charge of device restoring(ex, invalidate_device_objects()...)
//--------------------------------------------------------------------------------
class zz_manager : public zz_node {
friend class zz_system;
	typedef bool (zz_node::* zz_device_objects_func)(); // for [init/invalidate/restore/delete]_device_objects()

	// This class will be used in for_each() as parameter.
	class zz_device_objects_callback {
	private:
		zz_device_objects_func f_; // Device objects related function pointer
	public:
		// Constructor with device object funcion.
		// CAUTION: This class does not have default constructor.
		zz_device_objects_callback (zz_device_objects_func f) : f_(f) {}

		// ()operator which will be used by for_each()
		// This method calls node->xxxx_device_objects().
		bool operator() (zz_node * node) {
			if (!node) {
				return false;
			}
			return (node->*f_)(); // Internaly, suitable virtual function will be called.
		}
	};

public:
	//--------------------------------------------------------------------------------
	// Immediate-flush instrumentation.
	//
	// flush_entrance() is where a node is force-loaded *right now* instead of
	// waiting its turn in the lazy entrance line: a synchronous file read plus
	// D3D resource creation, on the main thread. Almost all of it is driven from
	// inside begin_scene() -> before_render(), i.e. at the moment an object first
	// becomes visible, by zz_terrain_block::before_render() (unconditional) and
	// zz_scene_octree's IMMEDIATE_FLUSH_DISTANCE_SQUARE test (anything within
	// 50 m of the camera).
	//
	// That makes it the prime suspect for a hitch that happens when new terrain
	// is *displayed* rather than when its chunk files are read. Counted here
	// rather than guessed at -- see doc/znzin-optimizations.md's method note.
	//
	// per_frame_* are reset at the end of each frame in zz_system::sleep().
	//
	// recent_* is a short sliding window (a bigger spike replaces it at once,
	// otherwise it expires and re-baselines). A chunk-display stall lasts a
	// single frame, which is unreadable on a HUD row that updates 130 times a
	// second -- this holds it on screen long enough to actually see.
	//
	// worst_* is the absolute peak since the last explicit reset. It saturates
	// during the zone-in burst, which is why resetImmediateFlushStats() exists
	// and is wired to a chat command: a peak you cannot re-zero tells you
	// nothing once you are standing in the world.
	// Which manager the flushed nodes came from. 256 nodes in one frame is
	// ambiguous on its own -- a map chunk is 16x16 = 256 patches AND carries
	// roughly 260 static objects -- so the breakdown is what tells terrain-block
	// creation apart from object mesh/texture loading, and therefore which fix
	// applies. Captured for the worst frame, alongside the totals.
	enum flush_kind {
		FLUSH_KIND_TERRAIN = 0, // terrain_meshes / rough_terrain_meshes
		FLUSH_KIND_MESH, // ordinary object meshes
		FLUSH_KIND_TEXTURE,
		FLUSH_KIND_MATERIAL,
		FLUSH_KIND_OTHER, // motions, skeletons, shaders, ...
		FLUSH_KIND_COUNT
	};

	// Where the force-flushed nodes actually came from.
	//
	// lazyterr=0 during a 256-node terrain spike says the nodes were NOT sitting
	// in the entrance line, which contradicts the code path as read
	// (loadTerrainBlockEx -> terrain_meshes->load() -> entrance_line.push()).
	// Rather than keep guessing at the discrepancy, count the branches:
	//   load_queued    - zz_manager::load() pushed the node onto the entrance line
	//   load_immediate - zz_manager::load() loaded it inline instead (this happens
	//                    when use_delayed_loading is off, or load_weight == 0)
	//   flush_from_queue / flush_direct - whether flush_entrance() found the node
	//                    in the entrance line, or had to flush a node that was
	//                    never queued at all
	// flush_direct dominating is the proof that the amortiser never had the work.
	struct flush_stats {
		int per_frame_count;
		int per_frame_usec;
		int per_frame_kind[FLUSH_KIND_COUNT];
		int load_queued;
		int load_immediate;
		int flush_from_queue;
		int flush_direct;
		// Lead time in frames between a node being queued and being force-flushed.
		int age_sum;
		int age_samples;
		int age_max;
		int recent_count;
		int recent_usec;
		int recent_age_ms;
		int recent_kind[FLUSH_KIND_COUNT];
		int worst_usec;
		int worst_count;
		uint64 recent_stamp_ms;
		// Inside a texture flush: file read vs D3DXCreateTextureFromFileInMemoryEx.
		// Lives here rather than in the renderer because these loads happen during
		// the flush and this struct is already reset once per frame -- and because
		// the motion hunt showed that "the flush is slow" is not an answer until
		// you know which half of it. Under D3D9Ex textures cannot go in
		// D3DPOOL_MANAGED, so D3DX has to stage through SYSTEMMEM and copy to a
		// DEFAULT texture; if the create half dominates, that is the reason.
		int texture_read_usec;
		int texture_create_usec;
		int texture_load_count;
	};
	static const flush_stats& get_flush_stats () { return s_flush_stats; }
	static void add_texture_load_time (int read_usec, int create_usec);
	static void reset_flush_stats_frame ();
	static void reset_flush_stats_all ();
	flush_kind classify_flush_kind () const;

	//--------------------------------------------------------------------------------
	// Amortised-loading budget.
	//
	// The entrance line is supposed to spread resource loading over frames so
	// that nothing has to be force-loaded at first render. It could not: on a
	// successful load update() subtracted the *entire* accumulated budget rather
	// than the item's own weight, so `t` dropped to ~0, the `t > time_weight`
	// test failed, and the loop exited -- one node per manager per update, no
	// matter how much budget had accrued.
	//
	// Meanwhile CPatchManager inserts up to 4 patches/frame from its proximity
	// ring and an unbounded number from the frustum pass. 4 in, 1 out: the queue
	// could never catch up, a whole map chunk's worth of terrain meshes (255)
	// backed up, and zz_terrain_block::before_render() -- which flushes
	// unconditionally -- then paid for all of them in a single frame. Measured
	// in-game at 12-46 ms per spike.
	//
	// Fixing the accounting alone would pace by load_weight, which is
	// "1 + filesize/1000" -- one millisecond per kilobyte, a 2003 disk-rate
	// model that is orders of magnitude out against a warm cache (and is simply
	// 1 for procedurally generated terrain meshes, which have no file at all).
	// So the loop is additionally bounded by a real wall-clock budget shared
	// across all managers for the frame. That is self-calibrating: it spends a
	// bounded slice every frame instead of an unbounded one occasionally.
	//
	// Set to 0 to restore the exact previous behaviour (one node per update) --
	// this is the A/B switch, driven from [VIDEO] LOAD_BUDGET_US.
	static void set_load_budget_per_frame_usec (int usec);
	static int get_load_budget_per_frame_usec ();
	/// Called once per frame before the managers are updated.
	static void begin_load_budget ();

protected:
	zz_node * _current; // current selected node
	zz_waiting_line<zz_node> entrance_line; // created but not-loaded node list
	zz_waiting_line<zz_node> exit_line; // deleted but not-unloaded node list
	bool is_lazy; // whether it is lazy device mode or not. default is false.
	zz_time entrance_time_accumulated; // accumulated time for entrance list from last update
	zz_time exit_time_accumulated; // accumulated time for exit list from last update
	unsigned int num_reuse; // number of reusing objects. default is 0

	// Frame at which this manager's entrance line last went from empty to
	// occupied. Instrumentation only -- see flush_entrance().
	unsigned int entrance_nonempty_since;

	// Shared across every manager instance: the HUD wants one "how much was
	// force-loaded this frame" number, not one per manager.
	static flush_stats s_flush_stats;

	// Wall-clock budget for amortised loading, shared by all managers so the
	// per-frame total is bounded rather than per-manager.
	static int s_load_budget_per_frame_usec;
	static int s_load_budget_usec_left;

	// Monotonic frame index, for entrance-line lead-time measurement.
	static unsigned int s_frame_counter;

	virtual void sort_waitings () // sort waitings
	{
	}

public:
	// constructor/destructor
	zz_manager ();
	virtual ~zz_manager ();

	virtual zz_node * get_current (); // get currently selected node. 
	virtual zz_node * set_current (zz_node * node_to_set_current); // select a node
	virtual zz_node * set_current (const char * name_to_set); // select a node by name

	//--------------------------------------------------------------------------------
	// Node lifetime :
	// spawn() -> (setting up...) -> load() -> (do something...) -> kill()
	// 
	//--------------------------------------------------------------------------------
	// Creates a new instance of class with type *node_type_to_spawn*
	// If *do_load* is true, the new object will be auto-loaded in device or memory after creation.
	zz_node * spawn (const char * baby_name, zz_node_type * node_type_to_spawn, bool do_load = true);
	
	// Find node by name, and if has any, then return it or create new one.
	zz_node * find_or_spawn (const char * baby_name, zz_node_type * node_type_to_spawn);
	
	// Removes a node by its pointer.
	bool kill (zz_node * node);

	// Load node into device or memory from file.
	// Normally, it reads a file and upload it to graphics device.
	void load (zz_node * node);

	// Unload node from device or memory and free instance.
	void unload (zz_node * node);

	// Frees all children's memory by delete operation.
	size_t release_children ();

	// For each child node, this method calls xxxx_device_objects() in system::xxxx_device_objects()
	void for_each (zz_device_objects_callback callback);

	// Updates all children nodes with update time.
	// This will causes entrance_line->flush() or exit_line->flush().
	void update (zz_time time_to_update);

	//--------------------------------------------------------------------------------
	// entrance_line : list, containing nodes which are waiting to enter into the device.
	// exit_line : list, contaning nodes which are waiting to exit from the device.
	//--------------------------------------------------------------------------------
	// adjust or flush entrance/exit line
	bool promote_entrance (zz_node * node);
	bool promote_exit (zz_node * node);
	bool flush_entrance (zz_node * node);
	bool flush_exit (zz_node * node);
	bool flush_entrance_all (); // flush entrance line
	bool flush_exit_all (); // flush exit line
	size_t get_entrance_size ();
	size_t get_exit_size ();
	bool remove_exit (zz_node * node);
	bool remove_entrance (zz_node * node);

	bool find_entrance (zz_node * node);
	bool find_exit (zz_node * node);

	// for lazy device update mode
	void set_lazy (size_t num_size_in);
	bool get_lazy ();

	// Set/get num_reuse.
	// The manager reuses already spawned node when new request come.
	// If num_reuse is zero, no reusing is done.
	// In every update, num_reuse nodes are not unloaded.
	// set_num_reuse() is called in zz_system instance at initializing phase.
	// the default value of num_reuse is zero.
	void set_num_reuse (unsigned int num_reuse_in);
	unsigned int get_num_reuse ();

	ZZ_DECLARE_DYNAMIC(zz_manager);
};

inline void zz_manager::set_num_reuse (unsigned int num_reuse_in)
{
	num_reuse = num_reuse_in;
}

inline unsigned int zz_manager::get_num_reuse ()
{
	return num_reuse;
}

inline void zz_manager::set_lazy (size_t num_size_in)
{
	if (num_size_in > 0) {
		is_lazy = true;
		entrance_line.set(zz_waiting_line<zz_node>::FOR_ENTRANCE, num_size_in);
		exit_line.set(zz_waiting_line<zz_node>::FOR_EXIT, num_size_in);
	}
	else {
		is_lazy = false;
		entrance_line.flush_n_pop((unsigned int)entrance_line.size());
		exit_line.flush_n_pop((unsigned int)exit_line.size());
	}
}

inline bool zz_manager::get_lazy ()
{
	return is_lazy;
}

#endif // __ZZ_MANAGER_H__