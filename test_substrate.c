/****************************************************
 *
 * Test the functionality of Score-P
 * Checks for:
 * - increasing timestamps
 * - are locations defined?
 * - is stack ok? (ONLY MAX_STACK elements on stack allowed)
 * - are communicators defined?
 * - are asynchronous MPI send/receives finished or canceled (ONLY the first MAX_MPI_REQUESTS!)
 * - are locations only activated when being not active
 * - are locations only deactivated when being active
 * - are only defined regions entered/exited
 * - are flushes correctly used
 * - are tasks finished
 *
 ****************************************************/



#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>

#include <scorep/SCOREP_PublicTypes.h>
#include <scorep/SCOREP_PublicHandles.h>
#include <scorep/SCOREP_SubstratePlugins.h>
#include <scorep/SCOREP_SubstrateEvents.h>

/** The maximal number of locations per process */
#define MAX_LOCATIONS 1000
/** The maximal stack size */
#define MAX_STACK 100
/** The number of handles (strings/regions) that can be defined */
#define MAX_HANDLES 1000000
/** The number of asynchronous MPI communications that are checked */
#define MAX_MPI_REQUESTS 100000

/**
 * Information about a single location
 */
struct location_info
{
  /** handle */
  struct SCOREP_Location* location;

  /** current stack */
  /** see also MAX_STACK */
  SCOREP_RegionHandle   stack[MAX_STACK];
  uint16_t stack_depth;

  /** last timestamp that has been set */
  uint64_t last_timestamp;

  /** is this location enabled? */
  int enabled;

  /** is this location currently flushing? */
  int flush_enabled;

  /** number of rewind regions */
  int nr_rewind;

  /** current requests (finished requests are set to -1) */
  /** see also MAX_MPI_REQUESTS */
  SCOREP_MpiRequestId requests[MAX_MPI_REQUESTS];
  uint32_t nr_requests;

  /** location is active */
  int active;

  /** current active number of tasks */
  uint64_t nr_tasks;

};

/**
 * information about handles (per process)
 */
struct handle_info
{
  /** handle for region/string/... */
  SCOREP_AnyHandle handle;
  /** type of handle */
  SCOREP_HandleType type;
};

/** lock for locations */
pthread_mutex_t location_lock;

/** locations of this process */
/** see also MAX_LOCATIONS */
static struct location_info locations[MAX_LOCATIONS];
/** number of locations */
static uint16_t nr_infos=0;

/** lock for handles */
pthread_mutex_t handle_lock;

/** handles of this process */
/** see also MAX_HANDLES */
static struct handle_info registered_handles[MAX_HANDLES];
/** number of handles */
static uint32_t nr_defined_handles=0;

/** define default variables for events */
#define VARIABLES \
uint16_t index=0; \
uint32_t handle_index = 0; \
uint32_t request_index = 0


/** check whether a location is defined
 * sets index
 *  */
#define CHECK_LOCATION(LOCATION) \
  do \
  { \
    for (index=0;index<nr_infos;index++) \
    { \
      if (LOCATION==locations[index].location) \
      { \
        break; \
      } \
    } \
    assert(index!=nr_infos); \
  }  while (0)

/** check whether the timestamp of locations[index] is increasing
 * uses locations[index] and timestamp
 *  */
#define CHECK_TIMESTAMP \
  do{ \
    assert( locations[index].last_timestamp <= timestamp ); \
    locations[index].last_timestamp = timestamp; \
  } \
  while (0)

/** check whether a handle HANDLE is defined and of type TYPE
 * sets handle_index
 *  */
#define CHECK_HANDLE_DEFINED(TYPE,HANDLE) \
do \
{ \
  for (handle_index=0;handle_index<nr_defined_handles;handle_index++) \
  { \
    if (HANDLE==registered_handles[handle_index].handle && registered_handles[handle_index].type == TYPE) \
    { \
      break; \
    } \
  } \
  assert(handle_index!=nr_defined_handles); \
}  while (0)

/** check whether a request is registered
 * uses index, requestId
 * sets request_index
 * TODO: do this per process, not per location?
 *  */
#define CHECK_REQUEST \
do \
{ \
  for (request_index=0;request_index<locations[index].nr_requests;request_index++) \
  { \
    if (locations[index].requests[request_index] == requestId) \
    { \
      break; \
    } \
  } \
  /* not found */ \
  assert (request_index != locations[index].nr_requests); \
} while (0)

static void test_substrate_EnableRecording(
    struct SCOREP_Location* location,
    uint64_t                timestamp,
    SCOREP_RegionHandle     regionHandle,
    uint64_t*               metricValues )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_HANDLE_DEFINED(SCOREP_HANDLE_TYPE_REGION,regionHandle);
  /* assert disabled */
  assert(!locations[index].enabled);
  /* set enabled */
  locations[index].enabled=1;
}

static void test_substrate_DisableRecording(
    struct SCOREP_Location* location,
    uint64_t                timestamp,
    SCOREP_RegionHandle     regionHandle,
    uint64_t*               metricValues )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_HANDLE_DEFINED(SCOREP_HANDLE_TYPE_REGION,regionHandle);
  /* assert enabled */
  assert(locations[index].enabled);
  /* set disabled */
  locations[index].enabled=0;
}

static void test_substrate_OnTracingBufferFlushBegin(
    struct SCOREP_Location* location,
    uint64_t                timestamp,
    SCOREP_RegionHandle     regionHandle,
    uint64_t*               metricValues )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_HANDLE_DEFINED(SCOREP_HANDLE_TYPE_REGION,regionHandle);
  /* assert disabled */
  assert(!locations[index].flush_enabled);
  /* set enabled */
  locations[index].flush_enabled=1;
}

static void test_substrate_OnTracingBufferFlushEnd(
    struct SCOREP_Location* location,
    uint64_t                timestamp,
    SCOREP_RegionHandle     regionHandle,
    uint64_t*               metricValues )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_HANDLE_DEFINED(SCOREP_HANDLE_TYPE_REGION,regionHandle);
  /* assert enabled */
  assert(locations[index].flush_enabled);
  /* set disabled */
  locations[index].flush_enabled=0;
}

static void test_substrate_EnterRegion(
    struct SCOREP_Location* location,
    uint64_t                timestamp,
    SCOREP_RegionHandle     regionHandle,
    uint64_t*               metricValues )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_HANDLE_DEFINED(SCOREP_HANDLE_TYPE_REGION,regionHandle);
  /* assert stack size not exceeded */
  assert(locations[index].stack_depth < ( MAX_STACK - 1 ) );
  /* add region to stack */
  locations[index].stack[locations[index].stack_depth] = regionHandle;
  /* increase stack size */
  locations[index].stack_depth++;

}
static void test_substrate_ExitRegion(
    struct SCOREP_Location* location,
    uint64_t                timestamp,
    SCOREP_RegionHandle     regionHandle,
    uint64_t*               metricValues )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_HANDLE_DEFINED(SCOREP_HANDLE_TYPE_REGION,regionHandle);
  /* assert exited region = region on top of stack */
  assert(locations[index].stack[locations[index].stack_depth-1] == regionHandle );
  /* decrease stack size */
  locations[index].stack_depth--;

}

static void test_substrate_EnterRewindRegion(
    struct SCOREP_Location* location,
    uint64_t                timestamp,
    SCOREP_RegionHandle     regionHandle )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_HANDLE_DEFINED(SCOREP_HANDLE_TYPE_REGION,regionHandle);
  /* increase number of rewind regions */
  locations[index].nr_rewind++;
}

void test_substrate_ExitRewindRegion(
    struct SCOREP_Location* location,
    uint64_t                timestamp,
    SCOREP_RegionHandle     regionHandle,
    bool                    doRewind )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_HANDLE_DEFINED(SCOREP_HANDLE_TYPE_REGION,regionHandle);
  /* assert there are rewind regions */
  assert(locations[index].nr_rewind!=0);
  /* decrease number of rewind regions (at finalize they should be 0) */
  locations[index].nr_rewind--;
}
static void test_substrate_MpiSend(
    struct SCOREP_Location*          location,
    uint64_t                         timestamp,
    SCOREP_MpiRank                   destinationRank,
    SCOREP_InterimCommunicatorHandle communicatorHandle,
    uint32_t                         tag,
    uint64_t                         bytesSent )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_HANDLE_DEFINED(SCOREP_HANDLE_TYPE_INTERIM_COMMUNICATOR,communicatorHandle);
}
static void test_substrate_MpiRecv(
    struct SCOREP_Location*          location,
    uint64_t                         timestamp,
    SCOREP_MpiRank                   sourceRank,
    SCOREP_InterimCommunicatorHandle communicatorHandle,
    uint32_t                         tag,
    uint64_t                         bytesReceived )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_HANDLE_DEFINED(SCOREP_HANDLE_TYPE_INTERIM_COMMUNICATOR,communicatorHandle);
}

static void test_substrate_MpiCollectiveBegin(
    struct SCOREP_Location* location,
    uint64_t                timestamp )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
}

static void test_substrate_MpiCollectiveEnd(
    struct SCOREP_Location*          location,
    uint64_t                         timestamp,
    SCOREP_InterimCommunicatorHandle communicatorHandle,
    SCOREP_MpiRank                   rootRank,
    SCOREP_CollectiveType            collectiveType,
    uint64_t                         bytesSent,
    uint64_t                         bytesReceived )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_HANDLE_DEFINED(SCOREP_HANDLE_TYPE_INTERIM_COMMUNICATOR,communicatorHandle);
}

static void test_substrate_MpiIsendComplete(
    struct SCOREP_Location* location,
    uint64_t                timestamp,
    SCOREP_MpiRequestId     requestId )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_REQUEST;
  /* finish the request */
  locations[index].requests[request_index] = -1;
}

static void test_substrate_MpiIrecvRequest(
    struct SCOREP_Location* location,
    uint64_t                timestamp,
    SCOREP_MpiRequestId     requestId )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  /* register request */
  locations[index].requests[locations[index].nr_requests++]=requestId;
}


static void test_substrate_MpiRequestTested(
    struct SCOREP_Location* location,
    uint64_t                timestamp,
    SCOREP_MpiRequestId     requestId )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_REQUEST;
}

static void test_substrate_MpiRequestCancelled(
    struct SCOREP_Location* location,
    uint64_t                timestamp,
    SCOREP_MpiRequestId     requestId )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_REQUEST;
  /* finish request */
  locations[index].requests[request_index] = -1;
}
static void test_substrate_MpiIsend(
    struct SCOREP_Location*          location,
    uint64_t                         timestamp,
    SCOREP_MpiRank                   destinationRank,
    SCOREP_InterimCommunicatorHandle communicatorHandle,
    uint32_t                         tag,
    uint64_t                         bytesSent,
    SCOREP_MpiRequestId              requestId )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_HANDLE_DEFINED(SCOREP_HANDLE_TYPE_INTERIM_COMMUNICATOR,communicatorHandle);
  /* register request */
  locations[index].requests[locations[index].nr_requests++]=requestId;
}

static void test_substrate_MpiIrecv(
    struct SCOREP_Location*          location,
    uint64_t                         timestamp,
    SCOREP_MpiRank                   sourceRank,
    SCOREP_InterimCommunicatorHandle communicatorHandle,
    uint32_t                         tag,
    uint64_t                         bytesReceived,
    SCOREP_MpiRequestId              requestId )
{
  VARIABLES;
  CHECK_LOCATION(location);
  CHECK_TIMESTAMP;
  CHECK_HANDLE_DEFINED(SCOREP_HANDLE_TYPE_INTERIM_COMMUNICATOR,communicatorHandle);
  CHECK_REQUEST;
  /* finish request */
  locations[index].requests[request_index] = -1;
}


static int test_early_init( void )
{
  /** initialize muteces for arrays */
  pthread_mutex_init(&location_lock,NULL);
  pthread_mutex_init(&handle_lock,NULL);
  /** return no problems */
  return 0;
}

static void test_late_init( void )
{
}

static void test_finalize( void )
{
  VARIABLES;
  for (index = 0; index < nr_infos; index ++)
  {
    /* location should be deleted */
    assert(locations[index].location == SCOREP_INVALID_LOCATION);
    /* location should have stack size 0 */
    assert(locations[index].stack_depth == 0);
    /* location should be disabled */
    assert(!locations[index].enabled);
    /* location should be inactive */
    assert(!locations[index].active);
    /* location should be not flushing */
    assert(!locations[index].flush_enabled);
    /* location should have finished all tasks */
    assert(locations[index].nr_tasks == 0);
    /* location should have finished all requests */
    for (request_index=0;request_index<locations[index].nr_requests;request_index++)
      assert(locations[index].requests[request_index]==-1);
    /* location should have no open rewinds */
    assert(locations[index].nr_rewind == 0);
  }
}


static void test_create_location( struct SCOREP_Location* location,
                            struct SCOREP_Location* parentLocation )
{
  VARIABLES;
  /* check if parent is registered or root of locations */
  if (parentLocation != SCOREP_INVALID_LOCATION)
    CHECK_LOCATION(parentLocation);
  /* register location */
  pthread_mutex_lock(&location_lock);
  locations[nr_infos++].location=location;
  pthread_mutex_unlock(&location_lock);
}


static void test_delete_location( struct SCOREP_Location* location )
{
  VARIABLES;
  CHECK_LOCATION(location);

  /* unregister location */
  pthread_mutex_lock(&location_lock);
  locations[index].location=SCOREP_INVALID_LOCATION;
  pthread_mutex_unlock(&location_lock);
}

static void test_activate_location( struct SCOREP_Location* location,
                              struct SCOREP_Location* parentLocation,
                              uint32_t                forkSequenceCount )
{
  VARIABLES;
  CHECK_LOCATION(location);

  /* check and set active */
  assert(!locations[index].active);
  locations[index].active=1;
}

static void test_deactivate_location( struct SCOREP_Location* location,
                                struct SCOREP_Location* parentLocation )
{
  VARIABLES;
  CHECK_LOCATION(location);
  /* check and set inactive */
  assert(locations[index].active);
  locations[index].active=0;
}

static void test_core_task_create( struct SCOREP_Location* location,
                             SCOREP_TaskHandle       taskHandle  )
{
  VARIABLES;
  CHECK_LOCATION(location);
  locations[index].nr_tasks++;
}

static void test_core_task_complete( struct SCOREP_Location* location,
                               SCOREP_TaskHandle       taskHandle  )
{
  VARIABLES;
  CHECK_LOCATION(location);
  assert(locations[index].nr_tasks != 0);
  locations[index].nr_tasks--;
}


static void test_define_handle( SCOREP_AnyHandle handle,
                          SCOREP_HandleType type )
{
  assert(handle);
  pthread_mutex_lock(&handle_lock);
  registered_handles[nr_defined_handles].handle=handle;
  registered_handles[nr_defined_handles].type=type;
  nr_defined_handles++;
  pthread_mutex_unlock(&handle_lock);

}


static uint32_t test_get_event_functions(
    SCOREP_Substrates_Mode       mode,
    SCOREP_Substrates_Callback** functions )
{
  SCOREP_Substrates_Callback* ret_fun=calloc(SCOREP_SUBSTRATES_NUM_EVENTS,
      sizeof(SCOREP_Substrates_Callback));
  ret_fun[SCOREP_EVENT_ENABLE_RECORDING] = (SCOREP_Substrates_Callback)test_substrate_EnableRecording;
  ret_fun[SCOREP_EVENT_DISABLE_RECORDING] = (SCOREP_Substrates_Callback)test_substrate_DisableRecording;
  ret_fun[SCOREP_EVENT_ON_TRACING_BUFFER_FLUSH_BEGIN] = (SCOREP_Substrates_Callback)test_substrate_OnTracingBufferFlushBegin;
  ret_fun[SCOREP_EVENT_ON_TRACING_BUFFER_FLUSH_END] = (SCOREP_Substrates_Callback)test_substrate_OnTracingBufferFlushEnd;
  ret_fun[SCOREP_EVENT_ENTER_REGION] = (SCOREP_Substrates_Callback)test_substrate_EnterRegion;
  ret_fun[SCOREP_EVENT_EXIT_REGION] = (SCOREP_Substrates_Callback)test_substrate_ExitRegion;
  ret_fun[SCOREP_EVENT_ENTER_REWIND_REGION] = (SCOREP_Substrates_Callback)test_substrate_EnterRewindRegion;
  ret_fun[SCOREP_EVENT_EXIT_REWIND_REGION] = (SCOREP_Substrates_Callback)test_substrate_ExitRewindRegion;
  ret_fun[SCOREP_EVENT_MPI_SEND] = (SCOREP_Substrates_Callback)test_substrate_MpiSend;
  ret_fun[SCOREP_EVENT_MPI_RECV] = (SCOREP_Substrates_Callback)test_substrate_MpiRecv;
  ret_fun[SCOREP_EVENT_MPI_COLLECTIVE_BEGIN] = (SCOREP_Substrates_Callback)test_substrate_MpiCollectiveBegin;
  ret_fun[SCOREP_EVENT_MPI_COLLECTIVE_END] = (SCOREP_Substrates_Callback)test_substrate_MpiCollectiveEnd;
  ret_fun[SCOREP_EVENT_MPI_ISEND_COMPLETE] = (SCOREP_Substrates_Callback)test_substrate_MpiIsendComplete;
  ret_fun[SCOREP_EVENT_MPI_IRECV_REQUEST] = (SCOREP_Substrates_Callback)test_substrate_MpiIrecvRequest;
  ret_fun[SCOREP_EVENT_MPI_REQUEST_TESTED] = (SCOREP_Substrates_Callback)test_substrate_MpiRequestTested;
  ret_fun[SCOREP_EVENT_MPI_REQUEST_CANCELLED] = (SCOREP_Substrates_Callback)test_substrate_MpiRequestCancelled;
  ret_fun[SCOREP_EVENT_MPI_ISEND] = (SCOREP_Substrates_Callback)test_substrate_MpiIsend;
  ret_fun[SCOREP_EVENT_MPI_IRECV] = (SCOREP_Substrates_Callback)test_substrate_MpiIrecv;
  *functions=ret_fun;
  return SCOREP_SUBSTRATES_NUM_EVENTS;
}


SCOREP_SUBSTRATE_PLUGIN_ENTRY(test_substrate)
{
  SCOREP_Substrate_Plugin_Info info;
  memset(&info,0,sizeof(SCOREP_Substrate_Plugin_Info));
  info.early_init=test_early_init;
  info.late_init=test_late_init;
  info.finalize=test_finalize;
  info.create_location=test_create_location;
  info.delete_location=test_delete_location;
  info.activate_location=test_activate_location;
  info.deactivate_location=test_deactivate_location;
  info.core_task_create=test_core_task_create;
  info.core_task_complete=test_core_task_complete;
  info.define_handle=test_define_handle;
  info.get_event_functions=test_get_event_functions;
  return info;
}
