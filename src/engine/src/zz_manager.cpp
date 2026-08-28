/** 
 * @file zz_manager.cpp
 * @brief manager class.
 * @author Jiho Choi (zho@korea.com)
 * @version 1.0
 * @date    26-feb-2002
 *
 * $Header: /engine/src/zz_manager.cpp 34    04-09-20 7:39p Zho $
 */

#include "zz_tier0.h"
#include "zz_algebra.h"
#include "zz_mem.h"
#include "zz_manager.h"
#include "zz_node.h"
#include "zz_list.h"
#include "zz_renderer_d3d.h"
#include "zz_view_d3d.h"
#include "zz_node_type.h"
#include "zz_system.h"
#include "zz_camera.h"
#include "zz_mesh.h"
#include "zz_manager.h"
#include "zz_visible.h"
#include "zz_animatable.h"
#include "zz_script.h"
#include "zz_light.h"
#include "zz_light_omni.h"
#include "zz_light_direct.h"
#include "zz_bone.h"
#include "zz_profiler.h"

#define ZZ_DEFAULT_DELETED 0

ZZ_IMPLEMENT_DYNCREATE(zz_manager, zz_node)

zz_manager::zz_manager () :
	zz_node(),
	_current(NULL), 
	entrance_line(zz_waiting_line<zz_node>::FOR_ENTRANCE, 0),
	exit_line(zz_waiting_line<zz_node>::FOR_EXIT, 0),
	is_lazy(false),
	num_reuse(0),
	entrance_nonempty_since(0)
{
	entrance_time_accumulated = 0;
	exit_time_accumulated = 0;
}

zz_manager::~zz_manager ()
{
	// clear_deleted();
}

size_t zz_manager::release_children ()
{
	size_t count = 0;
	zz_hash_table<zz_node*>::iterator it;
	zz_hash_table<zz_node*> * nodes = this->get_hash_table();

	this->addref(); // not to be released this
	//ZZ_LOG("manager: [%s] is now releasing sub-objects.\n", this->get_name());
	if (nodes) {
		int initial_size = nodes->size();
		while ((it = nodes->begin()) != nodes->end()) {
			//ZZ_LOG("manager: %04d. [%s] were not released.(refcount=%d)\n", count, (*it)->get_name(), (*it)->get_refcount());
			// force delete
			count++;
			zz_delete (*it);
		}
	}
	if (count > 0) {
		//ZZ_LOG("manager: [%s] had total %d objects that were not released. force deleted.\n", this->get_name(), count);
	}
	zz_node * node;
	count = exit_line.size();
	while (count > 0) {
		node = exit_line.pop();
		//ZZ_LOG("manager: %04d. [%s] were not released.(refcount=%d)\n", count, node->get_name(), node->get_refcount());
		count = exit_line.size();
		zz_delete node;
	}
	//entrance_line.flush(entrance_line.size());

	// force delete
	zz_delete this;
	return count; // total release count (does not mean the number of unreleased objects)
}

// get the current node
zz_node * zz_manager::get_current ()
{
	return _current;
}

// make node to current selection
zz_node * zz_manager::set_current (const char * name_to_set)
{
	zz_node * save = _current;
	_current = find(name_to_set);
	return save;
}

// returns old current
zz_node * zz_manager::set_current (zz_node * node_to_set_current)
{
	zz_node * save = _current;
	assert(node_to_set_current);
	_current = node_to_set_current;
	return save;
}

// <-> kill()
zz_node * zz_manager::spawn (const char * baby_name, zz_node_type * node_type_to_spawn, bool do_load)
{
	zz_node * new_born = NULL;
	
	//if (!node_type_to_spawn) return NULL;
	assert(node_type_to_spawn);

	// if reuning was turned on, then get the exit_line's.
	if (num_reuse > 0) {
		if (!exit_line.empty()) { // if there are one more exiting items.
			new_born = exit_line.pop(); // get it!
			if (new_born && !baby_name) { // if has no name
				new_born->set_name(0); // reset as unique name to clear old name
			}
		}
	}

	if (!new_born) { // if not found existing instance, then create new instance.
		new_born = node_type_to_spawn->create_instance();
	}

	assert(new_born);
	
	if (baby_name) { // if has specified name, rename to it.
		new_born->set_name(baby_name);
	}
	// else, use default name

	// link to this manager. all instances should be linked to the manager.
	link_child(new_born);
	
	// set current
	_current = new_born;

	// if we load() immediately after creating.
	if (do_load) {
		load(new_born);
	}
	//ZZ_LOG("manager: %s new_born (%s::%s)\n", this->get_name(), new_born->get_node_type()->type_name, new_born->get_name());
	
	return new_born;
}

void zz_manager::load (zz_node * node)
{
	zz_assert(znzin);
	zz_assert(node);

	// remove first
	exit_line.remove(node);

	if (!znzin->get_rs()->use_delayed_loading || (node->get_load_weight() == 0)) { // from zz_waiting_line::flush()
		// try loading
		++s_flush_stats.load_immediate;
		if (node->load()) {
			return;
		}
		if (node->is_load_terminally_failed()) {
			return; // missing file: queueing it would only retry forever
		}
	}
	else {
		++s_flush_stats.load_queued;
	}
	// Lead-time tracking, per manager rather than per node: record when this
	// entrance line last went from empty to occupied. The age of that transition
	// at flush time is how long the oldest queued work has been waiting, which is
	// the number that decides whether draining faster could ever have helped.
	//
	// Deliberately NOT a stamp on zz_node: that is the base class of every engine
	// object, and changing its layout to measure something is disproportionate
	// risk for instrumentation (it crashed the client at startup when tried).
	if (entrance_line.empty()) {
		entrance_nonempty_since = s_frame_counter;
	}
	entrance_line.push(node);
}

void zz_manager::unload (zz_node * node)
{
	zz_assert(node);
	zz_assert(znzin);

	// remove first
	entrance_line.remove(node); // not entered yet, but have to kill

	if (!znzin->get_rs()->use_delayed_loading) { // from zz_waiting_line::flush()
		// try unloading
		if (node->unload_and_release()) {
			return;
		}
		zz_assert(0);
	}

	node->init_reuse();

	exit_line.push(node); // include release
}

// first of all, find node with same name, and spawn if not have any matching
zz_node * zz_manager::find_or_spawn (const char * baby_name, zz_node_type * node_type_to_spawn)
{
	zz_node * new_born = NULL;
	
	if (baby_name) { // omitting spawn_name is possible
		new_born = this->find(baby_name);
	}
	if (new_born) {
		return new_born;
	}
	return spawn(baby_name, node_type_to_spawn);
}

// <-> spawn()
// only if refcount is 1, really delete it
bool zz_manager::kill (zz_node * node)
{
	assert(node);

	if (!children.empty()) { // replace _current to the first child
        _current = *(children.begin());
	}

	zz_node * parent_node = node->get_parent();

	unsigned long node_ref_count = node->get_refcount();

	if (node_ref_count != 1) { // if still refered by someone, then just call release().
		//ZZ_LOG("manager: %s killed (%s::%s)\n", this->get_name(), node->get_node_type()->type_name, node->get_name());
		if (find_entrance(node)) {
			// if it is in the entrance line, flush it first. 
			// if we don not flush entrance, we get the mangled node that will be deleted by other object's releasing still in entrance line not by kill().
			flush_entrance(node);
		}
		node->release();
		return true;	
	}
	
	assert(node_ref_count != 0);

	// Someone is still refering this.
	// First, unlink, pop from delayed queue(include release) it!

	// Not only release, but also kill actually!
	// disconnect all relationship with children.
	child_type * child = (zz_node::child_type *)(&node->get_children());
	child_iterator it(child->begin());
	while (it != child->end()) {
		// CAUTION: do not use "it++" because link_child() affects _it_ !
		node->unlink_child(*it);
		it = child->begin();
	}

	// disconnect from parent
	if (parent_node) {
		parent_node->unlink_child(node);
	}
	
	//ZZ_LOG("manager: %s full-killed (%s::%s)\n", this->get_name(), node->get_node_type()->type_name, node->get_name());

	// do manager's unload()
	unload(node);
	return true;
}

void zz_manager::for_each (zz_device_objects_callback callback)
{
	zz_hash_table<zz_node*>::iterator it, it_end;
	zz_hash_table<zz_node*> * nodes = get_hash_table(); // All children nodes could be accessed via name hash table.

	if (!nodes) {
		return;
	}

	//ZZ_LOG("manager: for_each(%x)", nodes);
	//ZZ_LOG("(%s)-%d\n", get_name(), nodes->size());

	for (it = nodes->begin(), it_end = nodes->end(); it != it_end; ++it) {
		callback(*it);
	}
}

// If time_to_update is zero, all accumulated time info is ignored. it means *new start*!!.
// When last frame was so heavy, we should refresh it.
// If not, we will get slow frames in a while.
void zz_manager::update (zz_time time_to_update)
{
	if ((exit_line.size() == 0) && (entrance_line.size() == 0)) return;
	
	sort_waitings(); // sort lines

	if (!znzin->get_rs()->use_delayed_loading) {
		exit_line.flush_n_pop((int)exit_line.size());
		entrance_line.flush_n_pop((int)entrance_line.size());
		entrance_time_accumulated = 0;
		exit_time_accumulated = 0;
		return;
	}
	
	if (time_to_update == 0) { // initialize acculumated
		entrance_time_accumulated = 0;
		exit_time_accumulated = 0;
	}
	else {
		// distribute time
		entrance_time_accumulated += 1 + time_to_update/10;
		exit_time_accumulated += 1 + time_to_update/10;

		// In budgeted mode the entrance accumulator is never spent (the loop
		// paces on wall clock), so cap it rather than let an unsigned counter
		// climb forever across a long session.
		if (s_load_budget_per_frame_usec > 0) {
			const zz_time kAccumulatorCap = ZZ_MSEC_TO_TIME(1000);
			if (entrance_time_accumulated > kAccumulatorCap) {
				entrance_time_accumulated = kAccumulatorCap;
			}
		}
	}
	zz_node * node;
	zz_time t;
	zz_time time_weight;

	//--------------------------------------------------------------------------------
	// for exit
	node = exit_line.back();	
	t = ZZ_TIME_TO_MSEC(exit_time_accumulated);
	time_weight = (!node) ? t : static_cast<zz_time>(node->get_load_weight());

	// not to unload reusing nodes, bound to max_flush
	int max_flush = (exit_line.size() > num_reuse) ? (exit_line.size() - num_reuse) : 0;

	try {
		while ((max_flush-- > 0) && node && (t > time_weight)) {
			//ZZ_LOG("manager: [%s]->update() exit->flush(%s)\n", get_name(), node ? node->get_name() : "(null)");
			exit_line.flush_n_pop(1);
			
			node = exit_line.back();

			if (!node) continue; // skip if no node

			exit_time_accumulated -= ZZ_MSEC_TO_TIME(t);
			t = ZZ_TIME_TO_MSEC(exit_time_accumulated);

			time_weight = static_cast<zz_time>(node->get_load_weight());
		}
	}
	catch (...) {
		if (node) {
			ZZ_LOG("manager: [%s]->update() exit->flush(%s) failed.\n", get_name(), node->get_name());
		}
		else {
			ZZ_LOG("manager: [%s]->update() exit->flush() failed.\n", get_name());
		}
		throw;
	}
	//--------------------------------------------------------------------------------

	//--------------------------------------------------------------------------------
	// for entrance
	node = entrance_line.back();
	t = ZZ_TIME_TO_MSEC(entrance_time_accumulated);
	time_weight = (!node) ? t : static_cast<zz_time>(node->get_load_weight());

	// Give every node currently queued at most one failed attempt per update.
	// The re-insert branch below does NOT decrement entrance_time_accumulated,
	// so t and time_weight are unchanged on failure: with a node that can never
	// load (its file is missing from the .vfs) back() keeps returning it and the
	// while condition stays true forever -- an infinite loop that allocates a
	// list node on every push. That hung the client on the main thread while the
	// sound thread kept playing.
	size_t failed_attempts_left = entrance_line.size();

	// See set_load_budget_per_frame_usec().
	//
	// In budgeted mode the weight accounting is bypassed **entirely** -- the loop
	// is paced by wall clock alone. Charging per-item weight instead of the whole
	// accumulator was not enough on its own, and the arithmetic says why: the
	// accumulator accrues `1 + total/10` ticks per frame (~24 at 130 fps, i.e.
	// t = 5 ms), a terrain mesh has load_weight 1, and the `t > time_weight` gate
	// therefore stops after ~4 loads/frame. CPatchManager inserts 4 patches/frame
	// from its proximity ring alone, so the queue sat exactly at break-even and
	// any burst never drained -- measured as terrain=181..256 still being
	// force-flushed with the budget active, and raising the budget changing
	// nothing because the budget was never the binding constraint.
	//
	// The weight model cannot be repaired by tuning: it is "1 ms per KB"
	// (load_byte_per_msec = 1000), and it is a flat 1 for procedurally generated
	// terrain meshes that read no file at all. Wall clock is the only honest
	// measure of what a load costs.
	const bool budgeted = (s_load_budget_per_frame_usec > 0);

	try {
		while (node && (failed_attempts_left > 0)) {
			if (budgeted) {
				if (s_load_budget_usec_left <= 0) {
					break; // spent this frame's slice; the rest waits for the next
				}
			}
			else if (!(t > time_weight)) {
				break; // historical pacing: one node per update
			}

			uint64 load_start = 0, load_end = 0;
			if (budgeted) {
				zz_os::get_ticks(load_start);
			}

			//ZZ_LOG("manager: [%s]->update() entrance->flush(%s)\n", get_name(), node ? node->get_name() : "(null)");
			if (entrance_line.flush_node(node)) {
				if (!budgeted) {
					entrance_time_accumulated -= ZZ_MSEC_TO_TIME(t);
				}
				// Budgeted mode leaves entrance_time_accumulated alone: it is not
				// read while budgeting is on, and zz_time is UNSIGNED so an
				// unguarded subtraction here could wrap into an enormous value
				// that would then hand out an infinite budget if the mode were
				// ever switched back.
				entrance_line.pop();
			}
			else {
				// Every failed attempt costs budget, terminal or not. Charging
				// only the re-insert branch would let terminal removals shrink
				// the line for free, so with n-1 missing files plus one
				// genuinely retryable node (zz_mesh::load() ends in
				// bind_device(), which can fail transiently without being
				// terminal) that one node could be retried ~n times in a single
				// update -- a frame hitch, and not the "one attempt per queued
				// node per update" bound this is meant to give.
				--failed_attempts_left;
				entrance_line.pop();

				if (!node->is_load_terminally_failed()) {
					entrance_line.push(node); // re-insert to front and try again later
				}
				// else: the file is missing, so re-queueing would retry it every
				// update forever; push() also does a linear find(), making n such
				// nodes O(n^2) per update. Dropping is safe -- the line holds raw
				// pointers and takes no reference, and kill() copes with a node
				// that is not queued (flush_entrance falls back to a direct
				// flush). A later loadMesh() spawns a fresh node, so a file that
				// reappears is still picked up.
			}

			// Charge the wall clock regardless of success/failure: a failed
			// attempt costs real time too, and letting failures run free would
			// reintroduce the unbounded-work-per-frame problem this exists to
			// stop. `continue` below must not skip this, hence it sits before
			// the node re-fetch.
			if (budgeted) {
				zz_os::get_ticks(load_end);
				if (zz_system::ticks_per_second > 0) {
					s_load_budget_usec_left -=
						(int)((load_end - load_start) * 1000000 / zz_system::ticks_per_second);
				}
			}

			node = entrance_line.back();

			if (!node) continue; // skip if no node

			t = ZZ_TIME_TO_MSEC(entrance_time_accumulated);

			time_weight = static_cast<zz_time>(node->get_load_weight());
		}
	}
	catch (...) {
		if (node) {
			ZZ_LOG("manager: [%s]->update() entrance->flush(%s) failed.\n", get_name(), node->get_name());
		}
		else {
			ZZ_LOG("manager: [%s]->update() entrance->flush() failed.\n", get_name());
		}
		 throw;
	}
	//--------------------------------------------------------------------------------
}

bool zz_manager::promote_entrance (zz_node * node)
{
	if (entrance_line.empty()) return false;
	return entrance_line.to_back(node);
}

bool zz_manager::promote_exit (zz_node * node)
{
	if (exit_line.empty()) return false;
	return exit_line.to_back(node);
}

zz_manager::flush_stats zz_manager::s_flush_stats = { 0 };

// Amortised-loading budget, in microseconds per frame. 0 = historical pacing.
//
// Defaults to off, and the client's [VIDEO] LOAD_BUDGET_US also defaults to 0.
// It was built to spread the 12-46 ms terrain flush spikes, and it does not:
// measurement showed those nodes never enter the entrance line at all
// (lazyterr=0 during every spike), so there is no backlog for it to drain. Kept
// because it is the correct pacing model for work that *does* queue, but it
// stays off until something measures a benefit.
int zz_manager::s_load_budget_per_frame_usec = 0;
int zz_manager::s_load_budget_usec_left = 0;
unsigned int zz_manager::s_frame_counter = 0;

void zz_manager::set_load_budget_per_frame_usec (int usec)
{
	s_load_budget_per_frame_usec = (usec < 0) ? 0 : usec;
}

int zz_manager::get_load_budget_per_frame_usec ()
{
	return s_load_budget_per_frame_usec;
}

void zz_manager::begin_load_budget ()
{
	s_load_budget_usec_left = s_load_budget_per_frame_usec;
}

// How long a spike is held on the "recent" readout before it re-baselines.
static const int ZZ_FLUSH_RECENT_WINDOW_MS = 4000;

static uint64 zz_flush_now_ms ()
{
	uint64 ticks = 0;
	zz_os::get_ticks(ticks);
	if (zz_system::ticks_per_second == 0) {
		return 0;
	}
	return ticks * 1000 / zz_system::ticks_per_second;
}

void zz_manager::add_texture_load_time (int read_usec, int create_usec)
{
	s_flush_stats.texture_read_usec += read_usec;
	s_flush_stats.texture_create_usec += create_usec;
	++s_flush_stats.texture_load_count;
}

void zz_manager::reset_flush_stats_frame ()
{
	const uint64 now_ms = zz_flush_now_ms();

	if (s_flush_stats.per_frame_usec > s_flush_stats.worst_usec) {
		s_flush_stats.worst_usec = s_flush_stats.per_frame_usec;
		s_flush_stats.worst_count = s_flush_stats.per_frame_count;
	}

	// Replace immediately on a bigger spike; otherwise let the held value expire
	// so the readout follows what is happening now rather than latching forever.
	const bool expired =
		(now_ms - s_flush_stats.recent_stamp_ms) >= (uint64)ZZ_FLUSH_RECENT_WINDOW_MS;
	if ((s_flush_stats.per_frame_usec > s_flush_stats.recent_usec) || expired) {
		s_flush_stats.recent_usec = s_flush_stats.per_frame_usec;
		s_flush_stats.recent_count = s_flush_stats.per_frame_count;
		for (int i = 0; i < FLUSH_KIND_COUNT; ++i) {
			s_flush_stats.recent_kind[i] = s_flush_stats.per_frame_kind[i];
		}
		s_flush_stats.recent_stamp_ms = now_ms;
	}
	s_flush_stats.recent_age_ms = (int)(now_ms - s_flush_stats.recent_stamp_ms);

	s_flush_stats.per_frame_usec = 0;
	s_flush_stats.per_frame_count = 0;
	s_flush_stats.load_queued = 0;
	s_flush_stats.load_immediate = 0;
	s_flush_stats.flush_from_queue = 0;
	s_flush_stats.flush_direct = 0;
	s_flush_stats.age_sum = 0;
	s_flush_stats.age_samples = 0;
	s_flush_stats.age_max = 0;
	s_flush_stats.texture_read_usec = 0;
	s_flush_stats.texture_create_usec = 0;
	s_flush_stats.texture_load_count = 0;

	++s_frame_counter;
	for (int i = 0; i < FLUSH_KIND_COUNT; ++i) {
		s_flush_stats.per_frame_kind[i] = 0;
	}
}

void zz_manager::reset_flush_stats_all ()
{
	s_flush_stats.per_frame_usec = 0;
	s_flush_stats.per_frame_count = 0;
	s_flush_stats.recent_usec = 0;
	s_flush_stats.recent_count = 0;
	s_flush_stats.recent_age_ms = 0;
	s_flush_stats.recent_stamp_ms = zz_flush_now_ms();
	s_flush_stats.worst_usec = 0;
	s_flush_stats.worst_count = 0;
	for (int i = 0; i < FLUSH_KIND_COUNT; ++i) {
		s_flush_stats.per_frame_kind[i] = 0;
		s_flush_stats.recent_kind[i] = 0;
	}
}

/// Classify by which manager owns the node being flushed.
zz_manager::flush_kind zz_manager::classify_flush_kind () const
{
	if (!znzin) {
		return FLUSH_KIND_OTHER;
	}
	if (this == (const zz_manager *)znzin->terrain_meshes
		|| this == (const zz_manager *)znzin->rough_terrain_meshes
		|| this == (const zz_manager *)znzin->ocean_meshes) {
		return FLUSH_KIND_TERRAIN;
	}
	if (this == znzin->meshes) {
		return FLUSH_KIND_MESH;
	}
	if (this == (const zz_manager *)znzin->textures) {
		return FLUSH_KIND_TEXTURE;
	}
	if (this == znzin->materials) {
		return FLUSH_KIND_MATERIAL;
	}
	return FLUSH_KIND_OTHER;
}

bool zz_manager::flush_entrance (zz_node * node)
{
	// Instrumented: this is the single chokepoint for every forced synchronous
	// load (zz_texture/zz_mesh/zz_material::flush_device(true) all route here).
	// The timer pair is negligible next to a file read + D3D resource create.
	uint64 start = 0, end = 0;
	zz_os::get_ticks(start);

	bool ret;
	if (promote_entrance(node)) {
		// Lead time: how many frames this manager's queue has been continuously
		// occupied before something rendered its contents and forced the load.
		// Small (0-2) means the amortiser never had a chance and draining faster
		// cannot help; large means it simply was not draining fast enough.
		const unsigned int age = s_frame_counter - entrance_nonempty_since;
		s_flush_stats.age_sum += (int)age;
		++s_flush_stats.age_samples;
		if ((int)age > s_flush_stats.age_max) {
			s_flush_stats.age_max = (int)age;
		}

		entrance_line.flush_n_pop(1);
		++s_flush_stats.flush_from_queue;
		ret = true;
	}
	else {
		// Not in the entrance line at all -- so no amount of amortised draining
		// could ever have pre-loaded it. Counted separately because that
		// distinction decides whether the fix belongs in the queue or upstream
		// of it.
		++s_flush_stats.flush_direct;
		ret = entrance_line.flush_node(node); // direct flush
	}

	zz_os::get_ticks(end);
	if (zz_system::ticks_per_second > 0) {
		s_flush_stats.per_frame_usec +=
			(int)((end - start) * 1000000 / zz_system::ticks_per_second);
	}
	++s_flush_stats.per_frame_count;
	++s_flush_stats.per_frame_kind[classify_flush_kind()];

	return ret;
}

bool zz_manager::flush_exit (zz_node * node)
{
	if (promote_exit(node)) {
		exit_line.flush_n_pop(1);
		return true;
	}
	// else not in exit line
	return exit_line.flush_node(node); // direct flush
}

size_t zz_manager::get_entrance_size ()
{
	return entrance_line.size();
}

size_t zz_manager::get_exit_size ()
{
	return exit_line.size();
}

bool zz_manager::flush_entrance_all ()
{
	return entrance_line.flush_all(); // flush all
}

bool zz_manager::flush_exit_all ()
{
	return exit_line.flush_all();
}

bool zz_manager::remove_exit (zz_node * node)
{
	if (exit_line.remove(node)) {
		return true;
	}
	return false;
}

bool zz_manager::remove_entrance (zz_node * node)
{
	if (entrance_line.remove(node)) {
		return true;
	}
	return false;
}

bool zz_manager::find_entrance (zz_node * node)
{
	return entrance_line.find(node);
}

bool zz_manager::find_exit (zz_node * node)
{
	return exit_line.find(node);
}

