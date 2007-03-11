#ifndef WORLD_H
#define WORLD_H

/// \file world.h
/// \brief Implements World and includes pretty much every header you'll need

// must include mpi before io stuff
#include <mpi.h>
#include <iostream>
#include <cstdio>
#include <unistd.h>
#include <assert.h>
#include <algorithm>
#include <functional>
#include <list>
#include <map>
#include <vector>

#ifdef UINT64_T
typedef UINT64_T uint64_t;
#endif

#include <world/typestuff.h>
#include <world/worldhash.h>
#include <world/array.h>
#include <world/print.h>
#include <world/worldexc.h>
#include <world/sharedptr.h>
#include <world/nodefaults.h>
#include <world/worldmpi.h>
#include <world/worldser.h>
#include <world/worldtime.h>

namespace madness {

    class World;

    class uniqueidT {
        friend class World;
    private:
        unsigned long worldid;
        unsigned long objid;

        uniqueidT(unsigned long worldid, unsigned long objid) 
            : worldid(worldid), objid(objid)
        {};
        
    public:
        uniqueidT() 
        : worldid(0), objid(0)
        {};
        
        bool operator==(const uniqueidT& other) const {
            return  objid==other.objid && worldid==other.worldid;
        };

        std::size_t operator()(const uniqueidT& id) const { // for GNU hash 
            return id.objid;
        };

        operator bool() const {
            return objid!=0;
        };

        template <typename Archive>
        void serialize(Archive& ar) {
            ar & archive::wrap_opaque(*this);
        };

        unsigned long get_world_id() const {
            return worldid;
        };

        unsigned long get_obj_id() const {
            return objid;
        };
    };

    std::ostream& operator<<(std::ostream& s, const uniqueidT& id);


    extern void xterm_debug(const char* path, const char* display);

    class WorldTaskQueue;
    class WorldAmInterface;
    class WorldGopInterface;
    class World; 
   
    static void world_do_poll(World* world);
    static void world_do_run_task(World* world, bool* status);
    static WorldAmInterface* world_am_interface_factory(World* world);
    static void world_am_interface_unfactory(WorldAmInterface* am);
    static WorldGopInterface* world_gop_interface_factory(World* world);
    static void world_gop_interface_unfactory(WorldGopInterface* gop);
    static WorldTaskQueue* world_taskq_factory(World* world);
    static void world_taskq_unfactory(WorldTaskQueue* taskq);
    static void world_assign_id(World* world);
    

    /// For purpose of deferring cleanup to synchronization points
    struct DeferredCleanupInterface {
        virtual ~DeferredCleanupInterface(){};
    };

    static void error(const char *msg) {
        fprintf(stderr,"fatal error: %s\n",msg);
        MPI_Abort(MPI_COMM_WORLD,1);
    }
    
    /// A parallel world with full functionality wrapping an MPI communicator

    /// Multiple worlds with different communicators can co-exist.
    class World {
    private:
        friend class WorldAmInterface;
        friend class WorldGopInterface;
        friend void world_assign_id(World* world);

        static unsigned long idbase;        //< Base for unique world ID range for this process
        static std::list<World*> worlds;    //< Maintains list of active worlds for polling, etc.
        static std::list<void (*)()> polls; //< List of routines to invoke while polling
        static uint64_t poll_delay;//< Min. no. of instructions between calls to poll if working
        static uint64_t last_poll;//< Instruction count at last poll
        
        struct hashvoidp {
            inline std::size_t operator()(const void* p) const {
                return std::size_t(p);    // The ptr's are guaranteed to be unique
            };
        };

        typedef HASH_MAP_NAMESPACE::hash_map<uniqueidT, void *, uniqueidT>   map_id_to_ptrT;
        typedef HASH_MAP_NAMESPACE::hash_map<void *, uniqueidT, hashvoidp>   map_ptr_to_idT;
        map_id_to_ptrT map_id_to_ptr;
        map_ptr_to_idT map_ptr_to_id;


        unsigned long _id;                  //< Universe wide unique ID of this world
        unsigned long obj_id;               //< Counter to generate unique IDs within this world
        void* user_state;                   //< Holds user defined & managed local state
        std::list< SharedPtr<DeferredCleanupInterface> > deferred; //< List of stuff to delete at next sync point

        // Default copy constructor and assignment won't compile
        // (which is good) due to reference members.

        /// Does any deferred cleanup and returns true if cleaning was necessary
        bool do_deferred_cleanup() {
            if (deferred.empty()) {
                return false;
            }
            else {
                print("do_deferred_cleanup: cleaning",deferred.size(),"items");
                deferred.clear();
                return true;
            }
        };

        // Private: tries to run a task in each world
        static bool run_tasks() {
            bool status = false;
            for_each(worlds.begin(), worlds.end(), std::bind2nd(std::ptr_fun(world_do_run_task),&status));
            return status;
        };


    public:
        // Here we use Pimpl to both hide implementation details and also
        // to partition the namespace for users as world.mpi, world.am, etc.
        // We also embed a reference to this instance in the am and task
        // instances so that they have access to everything.
        //
        // The downside is we cannot do much of anything here without
        // using wrapper functions to foward the calls to the hidden
        // class methods.
        
        // !!! Order of declaration is important for correct order of initialization !!!
        WorldMpiInterface& mpi;  //< MPI interface
        WorldAmInterface& am;    //< AM interface
        WorldTaskQueue& taskq;   //< Task queue
        WorldGopInterface& gop;  //< Global operations

    private:
        const ProcessID me;      //< My rank ... needs to be declared after MPI
        int nprocess;            //< No. of processes ... ditto

    public:
        /// Give me a communicator and I will give you the world
        World(MPI::Intracomm& comm) 
            : obj_id(1)          //< start from 1 so that 0 is an invalid id
            , user_state(0)
            , deferred()
            , mpi(*(new WorldMpiInterface(comm)))
            , am(*world_am_interface_factory(this)) 
            , taskq(*world_taskq_factory(this))
            , gop(*world_gop_interface_factory(this))
            , me(mpi.rank())
            , nprocess(mpi.nproc())
        {
            worlds.push_back(this); 
            
            // Assign a globally (within COMM_WORLD) unique ID to this
            // world by assigning to each processor a unique range of indices
            // and broadcasting from node 0 of the current communicator.
            world_assign_id(this);  // Also acts as barrier 
            
            // Determine cost of polling and from this limit the
            // frequency with which poll_all will be run while there
            // is work in the task queue.
            uint64_t ins = cycle_count();
            for (int i=0; i<32; i++) World::poll_all();
            poll_delay = (cycle_count()-ins)>>5; // Actual cost per poll
            poll_delay = poll_delay<<3;  // *8 --> no more than 12.5% of time in polling
            ///world.mpi.Bcast(poll_delay,0); // For paranoia make sure all have same value?
            if (rank()==0) print("poll_delay",poll_delay,"cycles",int(1e6*poll_delay/cpu_frequency()),"us");
        };


        /// Sets a pointer to user-managed local state

        /// Rather than having all remotely invoked actions carry all
        /// of their data with them, they can access local state thru
        /// their world instance.  The user is responsible for
        /// consistently managing and freeing this data.
        void set_user_state(void* state) {
            user_state = state;
        };


        /// Returns pointer to user-managed state set by set_user_state()

        /// Will be NULL if set_user_state() has not been invoked.
        void* get_user_state() {
            return user_state;
        };


        /// Clears user-defined state ... same as set_user_state(0)
        void clear_user_state() {
            set_user_state(0);
        };


        /// Invokes any necessary polling for all existing worlds
        static void poll_all(bool working = false) {
            if (working  &&  (cycle_count() < last_poll+poll_delay)) return;
            for_each(worlds.begin(), worlds.end(), world_do_poll);
            last_poll = cycle_count();
        };


        /// Returns the system-wide unique integer ID of this world
        unsigned long id() const {
            return _id;
        };


        /// Returns the process rank in this world (same as MPI::Get_rank()))
        ProcessID rank() const {return me;};


        /// Returns the number of processes in this world (same as MPI::Get_size())
        ProcessID nproc() const {return nprocess;};

        /// Returns the number of processes in this world (same as MPI::Get_size())
        ProcessID size() const {return nprocess;};


        /// Returns new universe-wide unique ID for objects created in this world.  No comms.

        /// You should consider using register_ptr(), unregister_ptr(),
        /// id_from_ptr() and ptr_from_id() before using this directly.
        ///
        /// Currently relies on this being called in the same order on
        /// every process within the current world in order to avoid
        /// synchronization.  
        ///
        /// The value objid=0 is guaranteed to be invalid.
        uniqueidT unique_obj_id() {
            return uniqueidT(_id,obj_id++);
        };


        /// Associate a local pointer with a universe-wide unique id

        /// Use the routines register_ptr(), unregister_ptr(),
        /// id_from_ptr() and ptr_from_id() to map distributed data
        /// structures identified by the unique id to/from
        /// process-local data.
        ///
        /// !! The pointer will be internally cast to a (void *)
        /// so don't attempt to shove member pointers in here.
        ///
        /// !! ALL unique objects of any type within a world must
        /// presently be created in the same order on all processes so
        /// as to provide the uniquess property without global
        /// communication.
        template <typename T>
        uniqueidT register_ptr(T* ptr) {
            MADNESS_ASSERT(sizeof(T*) == sizeof(void *));
            uniqueidT id = unique_obj_id();
            map_id_to_ptr.insert(std::pair<uniqueidT,void*>(id,(void*) ptr));
            map_ptr_to_id.insert(std::pair<void*,uniqueidT>((void*) ptr,id));
            return id;
        };

        /// Unregister a unique id for a local pointer
        template <typename T>
        void unregister_ptr(T* ptr) {
            uniqueidT id = id_from_ptr(ptr);  // Will be zero if invalid
            map_id_to_ptr.erase(id);
            map_ptr_to_id.erase((void *) ptr);
        };

        /// Unregister a unique id for a local pointer based on id

        /// Same as world.unregister_ptr(world.ptr_from_id<T>(id));
        template <typename T>
        void unregister_ptr(uniqueidT id) {
            unregister_ptr(ptr_from_id<T>(id));
        };

        /// Look up local pointer from world-wide unique id.

        /// Returns NULL if the id was not found.
        template <typename T>
        T* ptr_from_id(uniqueidT id) const {
            map_id_to_ptrT::const_iterator it = map_id_to_ptr.find(id);
            if (it == map_id_to_ptr.end()) 
                return 0;
            else
                return (T*) (it->second);
        };

        /// Look up id from local pointer

        /// Returns invalid id if the ptr was not found
        template <typename T>
        const uniqueidT& id_from_ptr(T* ptr) const {
            static uniqueidT invalidid(0,0);
            map_ptr_to_idT::const_iterator it = map_ptr_to_id.find(ptr);
            if (it == map_ptr_to_id.end()) 
                return invalidid;
            else
                return it->second;
        };

        /// Returns a pointer to the world with given ID or null if not found

        /// The id will only be valid if the process calling this routine
        /// is a member of that world.  Thus a null return value does not
        /// necessarily mean the world does not exist --- it could just
        /// not include the calling process.
        static World* world_from_id(unsigned long id) {
            // This is why C++ iterators are stupid, stupid, stupid, ..., gack!
            for (std::list<World *>::iterator it=worlds.begin(); it != worlds.end(); ++it) {
                if ((*it) && (*it)->_id == id) return *it;
            }
            return 0;
        };


        // Cannot use bind_nullary here since MPI::Request::Test is non-const
        struct MpiRequestTester {
            mutable MPI::Request& r;
            MpiRequestTester(MPI::Request& r) : r(r) {};
            bool operator()() const {return r.Test();};
        };


        /// Wait for MPI request to complete while polling and processing tasks
        static void inline await(MPI::Request& request) {
            await(MpiRequestTester(request));
        };


        /// Wait for a condition to become true while polling and processing tasks

        /// Probe should be an object that when called returns the status.
        ///
        /// Ensures progress is made in all worlds.
        template <typename Probe>
        static void inline await(const Probe& probe) {
            // Critical here is that poll() is NOT called after a
            // successful test of the request since polling may
            // trigger an activity that invalidates the condition.
            bool working = false;
            while (!probe()) {
                poll_all(working);  // If working poll_all will increase polling interval
                working = run_tasks();
            }
        }


        /// Adds item to list of stuff to be deleted at next global_fence()
        void deferred_cleanup(const SharedPtr<DeferredCleanupInterface>& item) {
            deferred.push_back(item);
        };


        ~World() {
            worlds.remove(this);
            do_deferred_cleanup();
            world_taskq_unfactory(&taskq);
            world_gop_interface_unfactory(&gop);
            world_am_interface_unfactory(&am);
            delete &mpi;
        };
    };
}

// Order of these is important
#include <world/worldam.h>
#include <world/worldref.h>
#include <world/worlddep.h>
#include <world/worldfut.h>
#include <world/worldtask.h>
#include <world/worldgop.h>
#include <world/worlddc.h>

namespace madness {

    // This nonsense needs cleaning up and probably eliminating
    // now that the class interfaces have stabilized.

    void redirectio(World& world);

    static inline void world_do_poll(World* world) {
        if (world) world->am.poll();
    }
    static inline void world_do_run_task(World* world, bool* status) {
        if (world) *status = *status || world->taskq.run_next_ready_task();
    }
    static WorldAmInterface* world_am_interface_factory(World* world) {
        return new WorldAmInterface(*world);
    }
    static void world_am_interface_unfactory(WorldAmInterface* am) {
        delete am;
    }
    static WorldGopInterface* world_gop_interface_factory(World* world) {
        return new WorldGopInterface(*world);
    }
    static void world_gop_interface_unfactory(WorldGopInterface* gop) {
        delete gop;
    }
    static WorldTaskQueue* world_taskq_factory(World* world) {
        return new WorldTaskQueue(*world);
    }
    static void world_taskq_unfactory(WorldTaskQueue* taskq) {
        delete taskq;
    }
    static void world_assign_id(World* world) {
        // Each process in COMM_WORLD is given unique ids for 10K new worlds
        if (World::idbase == 0 && MPI::COMM_WORLD.Get_rank()) {
            World::idbase = MPI::COMM_WORLD.Get_rank()*10000;
        }
        // The id of a new world is taken from the unique range of ids
        // assigned to the process with rank=0 in the sub-communicator
        if (world->mpi.rank() == 0) world->_id = World::idbase++;
        world->gop.broadcast(world->_id);
        world->gop.barrier();
    }

    namespace archive {
        template <class Archive>
        struct ArchiveLoadImpl<Archive,World*> {
            static inline void load(const Archive& ar, World*& wptr) {
                unsigned long id;
                ar & id;
                wptr = World::world_from_id(id);
                MADNESS_ASSERT(wptr);
            };
        };
        
        template <class Archive>
        struct ArchiveStoreImpl<Archive,World*> {
            static inline void store(const Archive& ar, World* const & wptr) {
                ar & wptr->id();
            };
        };
    }
}




#endif
