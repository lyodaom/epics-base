#ifndef epicsEventh
#define epicsEventh

#include "epicsAssert.h"
#include "shareLib.h"

typedef struct epicsEventOSD *epicsEventId;

typedef enum {
    epicsEventWaitOK,epicsEventWaitTimeout,epicsEventWaitError
} epicsEventWaitStatus;

typedef enum {epicsEventEmpty,epicsEventFull} epicsEventInitialState;

#ifdef __cplusplus

#include "locationException.h"

class epicsShareClass epicsEvent {
public:
    epicsEvent ( epicsEventInitialState initial = epicsEventEmpty );
    ~epicsEvent ();
    void signal ();
    void wait (); /* blocks until full */
    bool wait ( double timeOut ); /* false if empty at time out */
    bool tryWait (); /* false if empty */
    void show ( unsigned level ) const;

    class invalidSemaphore {}; /* exception */
private:
    epicsEvent ( const epicsEvent & );
    epicsEvent & operator = ( const epicsEvent & );
    epicsEventId id;
};

#endif /*__cplusplus */

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus */

epicsShareFunc epicsEventId epicsShareAPI epicsEventCreate(
    epicsEventInitialState initialState);
epicsShareFunc epicsEventId epicsShareAPI epicsEventMustCreate (
    epicsEventInitialState initialState);
epicsShareFunc void epicsShareAPI epicsEventDestroy(epicsEventId id);
epicsShareFunc void epicsShareAPI epicsEventSignal(epicsEventId id);
epicsShareFunc epicsEventWaitStatus epicsShareAPI epicsEventWait(
    epicsEventId id);
#define epicsEventMustWait(ID) assert((epicsEventWait ((ID))==epicsEventWaitOK))
epicsShareFunc epicsEventWaitStatus epicsShareAPI epicsEventWaitWithTimeout(
    epicsEventId id, double timeOut);
epicsShareFunc epicsEventWaitStatus epicsShareAPI epicsEventTryWait(
    epicsEventId id);
epicsShareFunc void epicsShareAPI epicsEventShow(
    epicsEventId id, unsigned int level);

#ifdef __cplusplus
}
#endif /*__cplusplus */

#include "osdEvent.h"

#endif /* epicsEventh */
