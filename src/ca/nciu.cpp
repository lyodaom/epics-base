
/*  $Id$
 *
 *                    L O S  A L A M O S
 *              Los Alamos National Laboratory
 *               Los Alamos, New Mexico 87545
 *
 *  Copyright, 1986, The Regents of the University of California.
 *
 *  Author: Jeff Hill
 */

#include "iocinf.h"

tsFreeList < class nciu, 1024 > nciu::freeList;

struct putCvrtBuf {
    ELLNODE             node;
    unsigned long       size;
    void                *pBuf;
};

/*
 * nciu::nciu ()
 */
nciu::nciu (cac *pcac, cacChannel &chan, const char *pNameIn) :
    cacChannelIO (chan)
{
    static const caar defaultAccessRights = { false, false };
    size_t strcnt;

    strcnt = strlen (pNameIn) + 1;
    if ( strcnt > MAX_UDP - sizeof (caHdr) ) {
        throwWithLocation ( caErrorCode (ECA_STRTOBIG) );
    }
    this->pNameStr = reinterpret_cast <char *> ( malloc (strcnt) );
    if ( ! this->pNameStr ) {
        this->f_fullyConstructed = false;
        return;
    }
    strcpy ( this->pNameStr, pNameIn );

    LOCK (pcac);

    pcac->installChannel (*this);

    this->typeCode = USHRT_MAX; /* invalid initial type    */
    this->count = 0;    /* invalid initial count     */
    this->sid = UINT_MAX; /* invalid initial server id     */
    this->ar = defaultAccessRights;
    this->nameLength = strcnt;
    this->previousConn = 0;
    this->f_connected = false;

    pcac->pudpiiu->addToChanList (this);

    /*
     * reset broadcasted search counters
     */
    pcac->pudpiiu->searchTmr.reset (0.0);

    this->f_fullyConstructed = true;

    UNLOCK (pcac);
}

/*
 * nciu::~nciu ()
 */
nciu::~nciu ()
{
    netiiu *piiuCopy = this->piiu;

    if ( ! this->fullyConstructed () ) {
        return;
    }

    if ( this->f_connected ) {
        caHdr hdr;

        hdr.m_cmmd = htons ( CA_PROTO_CLEAR_CHANNEL );
        hdr.m_available = this->getId ();
        hdr.m_cid = this->sid;
        hdr.m_dataType = htons ( 0 );
        hdr.m_count = htons ( 0 );
        hdr.m_postsize = 0;

        this->piiu->pushStreamMsg (&hdr, NULL, true);
    }

    LOCK ( this->piiu->pcas );

    /*
     * remove any IO blocks still attached to this channel
     */
    tsDLIterBD <baseNMIU> iter = this->eventq.first ();
    while ( iter != iter.eol () ) {
        tsDLIterBD <baseNMIU> next = iter.itemAfter ();
        iter->destroy ();
        iter = next;
    }

    this->piiu->pcas->uninstallChannel (*this);

    this->piiu->removeFromChanList ( this );

    free ( reinterpret_cast <void *> (this->pNameStr) );

    UNLOCK ( piiuCopy->pcas ); // remove clears this->piiu
}

int nciu::read ( unsigned type, unsigned long count, cacNotify &notify )
{
    int status;
    caHdr hdr;
    ca_uint16_t type_u16;
    ca_uint16_t count_u16;

    /* 
     * fail out if channel isnt connected or arguments are 
     * otherwise invalid
     */
    if ( ! this->f_connected ) {
        return ECA_DISCONNCHID;
    }
    if ( INVALID_DB_REQ (type) ) {
        return ECA_BADTYPE;
    }
    if ( ! this->ar.read_access ) {
        return ECA_NORDACCESS;
    }
    if ( count > this->count || count > 0xffff ) {
        return ECA_BADCOUNT;
    }
    if ( count == 0 ) {
        count = this->count;
    }

    /*
     * only after range checking type and count cast 
     * them down to a smaller size
     */
    type_u16 = (ca_uint16_t) type;
    count_u16 = (ca_uint16_t) count;

    LOCK (this->piiu->pcas);
    {
        netReadNotifyIO *monix = new netReadNotifyIO ( *this, notify );
        if ( ! monix ) {
            UNLOCK (this->piiu->pcas);
            return ECA_ALLOCMEM;
        }

        hdr.m_cmmd = htons (CA_PROTO_READ_NOTIFY);
        hdr.m_dataType = htons (type_u16);
        hdr.m_count = htons (count_u16);
        hdr.m_available = monix->getId ();
        hdr.m_postsize = 0;
        hdr.m_cid = this->sid;
    }
    UNLOCK (this->piiu->pcas);

    status = this->piiu->pushStreamMsg (&hdr, NULL, true);
    if ( status != ECA_NORMAL ) {
        /*
         * we need to be careful about touching the monix
         * pointer after the lock has been released
         */
        this->piiu->pcas->safeDestroyNMIU (hdr.m_available);
    }

    return status;
}

int nciu::read ( unsigned type, unsigned long count, void *pValue )
{
    int status;
    caHdr hdr;
    ca_uint16_t type_u16;
    ca_uint16_t count_u16;

    /* 
     * fail out if channel isnt connected or arguments are 
     * otherwise invalid
     */
    if ( ! this->f_connected ) {
        return ECA_DISCONNCHID;
    }
    if ( INVALID_DB_REQ ( type ) ) {
        return ECA_BADTYPE;
    }
    if ( ! this->ar.read_access ) {
        return ECA_NORDACCESS;
    }
    if ( count > this->count || count > 0xffff ) {
        return ECA_BADCOUNT;
    }
    if ( count == 0 ) {
        count = this->count;
    }

    /*
     * only after range checking type and count cast 
     * them down to a smaller size
     */
    type_u16 = ( ca_uint16_t ) type;
    count_u16 = ( ca_uint16_t ) count;

    LOCK ( this->piiu->pcas );
    {
        netReadCopyIO *monix = new netReadCopyIO ( *this, type, count, pValue, this->readSequence () );
        if ( ! monix ) {
            UNLOCK ( this->piiu->pcas );
            return ECA_ALLOCMEM;
        }

        hdr.m_cmmd = htons ( CA_PROTO_READ );
        hdr.m_dataType = htons ( type_u16 );
        hdr.m_count = htons ( count_u16 );
        hdr.m_available = monix->getId ();
        hdr.m_postsize = 0;
        hdr.m_cid = this->sid;
    }
    UNLOCK ( this->piiu->pcas );

    status = this->piiu->pushStreamMsg ( &hdr, NULL, true );
    if ( status != ECA_NORMAL ) {
        /*
         * we need to be careful about touching the monix
         * pointer after the lock has been released
         */
        this->piiu->pcas->safeDestroyNMIU ( hdr.m_available );
    }

    return status;
}

/*
 * free_put_convert()
 */
#ifdef CONVERSION_REQUIRED 
LOCAL void free_put_convert (cac *pcac, void *pBuf)
{
    struct putCvrtBuf   *pBufHdr;

    pBufHdr = (struct putCvrtBuf *)pBuf;
    pBufHdr -= 1;
    assert ( pBufHdr->pBuf == (void *) ( pBufHdr + 1 ) );

    LOCK (pcac);
    ellAdd (&pcac->putCvrtBuf, &pBufHdr->node);
    UNLOCK (pcac);

    return;
}
#endif /* CONVERSION_REQUIRED */

/*
 * check_a_dbr_string()
 */
LOCAL int check_a_dbr_string (const char *pStr, const unsigned count)
{
    unsigned i;

    for ( i = 0; i < count; i++ ) {
        unsigned int strsize = 0;
        while ( 1 ) {
            if (strsize >= MAX_STRING_SIZE ) {
                return ECA_STRTOBIG;
            }
            if ( pStr[strsize] == '\0' ) {
                break;
            }
            strsize++;
        }
        pStr += MAX_STRING_SIZE;
    }

    return ECA_NORMAL;
}

/*
 * malloc_put_convert()
 */
#ifdef CONVERSION_REQUIRED 
LOCAL void *malloc_put_convert (cac *pcac, unsigned long size)
{
    struct putCvrtBuf *pBuf;

    LOCK (pcac);
    while ( (pBuf = (struct putCvrtBuf *) ellGet(&pcac->putCvrtBuf)) ) {
        if(pBuf->size >= size){
            break;
        }
        else {
            free (pBuf);
        }
    }
    UNLOCK (pcac);

    if (!pBuf) {
        pBuf = (struct putCvrtBuf *) malloc (sizeof(*pBuf)+size);
        if (!pBuf) {
            return NULL;
        }
        pBuf->size = size;
        pBuf->pBuf = (void *) (pBuf+1);
    }

    return pBuf->pBuf;
}
#endif /* CONVERSION_REQUIRED */

/*
 * nciu::issuePut ()
 */
int nciu::issuePut (ca_uint16_t cmd, unsigned id, chtype type, 
                     unsigned long count, const void *pvalue)
{ 
    int status;
    caHdr hdr;
    unsigned postcnt;
    ca_uint16_t type_u16;
    ca_uint16_t count_u16;
#   ifdef CONVERSION_REQUIRED
        void *pCvrtBuf;
#   endif /*CONVERSION_REQUIRED*/

    /* 
     * fail out if the conn is down or the arguments are otherwise invalid
     */
    if ( ! this->f_connected ) {
        return ECA_DISCONNCHID;
    }
    if ( INVALID_DB_REQ (type) ) {
        return ECA_BADTYPE;
    }
    /*
     * compound types not allowed
     */
    if ( dbr_value_offset[type] ) {
        return ECA_BADTYPE;
    }
    if ( ! this->ar.write_access ) {
        return ECA_NOWTACCESS;
    }
    if ( count > this->count || count > 0xffff || count == 0 ) {
            return ECA_BADCOUNT;
    }
    if (type==DBR_STRING) {
        status = check_a_dbr_string ( (char *) pvalue, count );
        if (status != ECA_NORMAL) {
            return status;
        }
    }
    postcnt = dbr_size_n (type,count);
    if (postcnt>0xffff) {
        return ECA_TOLARGE;
    }

    /*
     * only after range checking type and count cast 
     * them down to a smaller size
     */
    type_u16 = (ca_uint16_t) type;
    count_u16 = (ca_uint16_t) count;

    if (type == DBR_STRING && count == 1) {
        char *pstr = (char *)pvalue;

        postcnt = strlen(pstr)+1;
    }

#   ifdef CONVERSION_REQUIRED 
    {
        unsigned i;
        void *pdest;
        unsigned size_of_one;

        size_of_one = dbr_size[type];

        pCvrtBuf = pdest = malloc_put_convert (this->piiu->pcas, postcnt);
        if (!pdest) {
            return ECA_ALLOCMEM;
        }

        /*
         * No compound types here because these types are read only
         * and therefore only appropriate for gets or monitors
         *
         * I changed from a for to a while loop here to avoid bounds
         * checker pointer out of range error, and unused pointer
         * update when it is a single element.
         */
        i=0;
        while (TRUE) {
            switch (type) {
            case    DBR_LONG:
                *(dbr_long_t *)pdest = htonl (*(dbr_long_t *)pvalue);
                break;

            case    DBR_CHAR:
                *(dbr_char_t *)pdest = *(dbr_char_t *)pvalue;
                break;

            case    DBR_ENUM:
            case    DBR_SHORT:
            case    DBR_PUT_ACKT:
            case    DBR_PUT_ACKS:
#           if DBR_INT != DBR_SHORT
#               error DBR_INT != DBR_SHORT ?
#           endif /*DBR_INT != DBR_SHORT*/
                *(dbr_short_t *)pdest = htons (*(dbr_short_t *)pvalue);
                break;

            case    DBR_FLOAT:
                dbr_htonf ((dbr_float_t *)pvalue, (dbr_float_t *)pdest);
                break;

            case    DBR_DOUBLE: 
                dbr_htond ((dbr_double_t *)pvalue, (dbr_double_t *)pdest);
            break;

            case    DBR_STRING:
                /*
                 * string size checked above
                 */
                strcpy ( (char *) pdest, (char *) pvalue );
                break;

            default:
                return ECA_BADTYPE;
            }

            if (++i>=count) {
                break;
            }

            pdest = ((char *)pdest) + size_of_one;
            pvalue = ((char *)pvalue) + size_of_one;
        }

        pvalue = pCvrtBuf;
    }
#   endif /*CONVERSION_REQUIRED*/

    hdr.m_cmmd = htons (cmd);
    hdr.m_dataType = htons (type_u16);
    hdr.m_count = htons (count_u16);
    hdr.m_cid = this->sid;
    hdr.m_available = id;
    hdr.m_postsize = (ca_uint16_t) postcnt;

    status = this->piiu->pushStreamMsg (&hdr, pvalue, true);

#   ifdef CONVERSION_REQUIRED
        free_put_convert (this->piiu->pcas, pCvrtBuf);
#   endif /*CONVERSION_REQUIRED*/

    return status;
}

int nciu::write (unsigned type, unsigned long count, const void *pValue)
{
    return this->issuePut (CA_PROTO_WRITE, ~0U, type, count, pValue);
}

int nciu::write (unsigned type, unsigned long count, const void *pValue, cacNotify &notify)
{
    netWriteNotifyIO *monix;
    unsigned id;
    int status;

    if ( ! this->f_connected ) {
        return ECA_DISCONNCHID;
    }

    if ( ! this->piiu->ca_v41_ok () )  {
        return ECA_NOSUPPORT;
    }

    /*
     * lock around io block create and list add
     * so that we are not deleted without
     * reclaiming the resource
     */
    LOCK (this->piiu->pcas);

    monix = new netWriteNotifyIO (*this, notify);
    if ( ! monix ) {
        UNLOCK (this->piiu->pcas);
        return ECA_ALLOCMEM;
    }

    id = monix->getId ();

    UNLOCK (this->piiu->pcas);

    status = this->issuePut (CA_PROTO_WRITE_NOTIFY, id, type, count, pValue);
    if ( status != ECA_NORMAL ) {
        /*
         * we need to be careful about touching the monix
         * pointer after the lock has been released
         */
        this->piiu->pcas->safeDestroyNMIU (id);
    }
    return status;
}

int nciu::subscribe (unsigned type, unsigned long count, 
                         unsigned mask, cacNotify &notify)
{
    netSubscription *pNetMon;

    LOCK (this->piiu->pcas);

    pNetMon = new netSubscription (*this, type, count, 
        static_cast <unsigned short> (mask), notify);
    if ( ! pNetMon ) {
        UNLOCK (this->piiu->pcas);
        return ECA_ALLOCMEM;
    }

    UNLOCK (this->piiu->pcas);

    pNetMon->subscriptionMsg ();

    return ECA_NORMAL;
}

void nciu::destroy ()
{
    delete this;
}

void nciu::hostName ( char *pBuf, unsigned bufLength ) const
{   
    this->piiu->hostName ( pBuf, bufLength );
}

bool nciu::ca_v42_ok () const
{
    return this->piiu->ca_v42_ok ();
}

short nciu::nativeType () const
{
    if ( this->f_connected ) {
        return static_cast <short> (this->typeCode);
    }
    else {
        return TYPENOTCONN;
    }
}

unsigned long nciu::nativeElementCount () const
{
    if ( this->f_connected ) {
        return this->count;
    }
    else {
        return 0ul;
    }
}

channel_state nciu::state () const
{
    if (this->f_connected) {
        return cs_conn;
    }
    else if (this->previousConn) {
        return cs_prev_conn;
    }
    else {
        return cs_never_conn;
    }
}

caar nciu::accessRights () const
{
    return this->ar;
}

const char *nciu::pName () const
{
    return this->pNameStr;
}

unsigned nciu::searchAttempts () const
{
    return this->retry;
}

void nciu::connect (tcpiiu &iiu, unsigned nativeType, unsigned long nativeCount, unsigned sid)
{
    LOCK ( iiu.pcas );

    if ( this->connected () ) {
        ca_printf (
            "CAC: Ignored conn resp to conn chan CID=%u SID=%u?\n",
            this->getId (), this->sid );
        UNLOCK ( iiu.pcas );
        return;
    }

    this->typeCode = nativeType;
    this->count = nativeCount;
    this->sid = sid;
    this->f_connected = true;
    this->previousConn = true;

    /*
     * if less than v4.1 then the server will never
     * send access rights and we know that there
     * will always be access and call their call back
     * here
     */
    if ( ! CA_V41 ( CA_PROTOCOL_VERSION, iiu.minor_version_number ) ) {
        this->ar.read_access = true;
        this->ar.write_access = true;
        this->accessRightsNotify ( this->ar );
    }

    this->connectNotify ();
 
    // resubscribe for monitors from this channel 
    tsDLIterBD<baseNMIU> iter = this->eventq.first ();
    while ( iter != iter.eol () ) {
        iter->subscriptionMsg ();
        iter++;
    }

    UNLOCK ( iiu.pcas );
}

void nciu::disconnect ()
{
    LOCK (this->piiu->pcas);

    this->typeCode = USHRT_MAX;
    this->count = 0u;
    this->sid = UINT_MAX;
    this->ar.read_access = false;
    this->ar.write_access = false;
    this->f_connected = false;

    char hostNameBuf[64];
    this->piiu->hostName ( hostNameBuf, sizeof (hostNameBuf) );

    /*
     * look for events that have an event cancel in progress
     */
    tsDLIterBD <baseNMIU> iter = this->eventq.first ();
    while ( iter != iter.eol () ) {
        tsDLIterBD <baseNMIU> next = iter.itemAfter ();
        iter->disconnect ( hostNameBuf );
        iter = next;
    }

    this->disconnectNotify ();
    this->accessRightsNotify (this->ar);

    UNLOCK ( this->piiu->pcas );

    this->piiu->disconnect ( this );
}

/*
 * nciu::searchMsg ()
 */
int nciu::searchMsg ()
{
    udpiiu      *piiu = this->piiu->pcas->pudpiiu;
    int         status;
    caHdr       msg;

    if ( this->piiu != static_cast<netiiu *> (piiu) ) {
        return ECA_INTERNAL;
    }

    if (this->nameLength > 0xffff) {
        return ECA_STRTOBIG;
    }

    msg.m_cmmd = htons (CA_PROTO_SEARCH);
    msg.m_available = this->getId ();
    msg.m_dataType = htons (DONTREPLY);
    msg.m_count = htons (CA_MINOR_VERSION);
    msg.m_cid = this->getId ();

    status = this->piiu->pushDatagramMsg (&msg, this->pNameStr, this->nameLength);
    if (status != ECA_NORMAL) {
        return status;
    }

    /*
     * increment the number of times we have tried to find this thisnel
     */
    if (this->retry<MAXCONNTRIES) {
        this->retry++;
    }

    /*
     * move the channel to the end of the list so
     * that all channels get a equal chance 
     */
    LOCK (this->piiu->pcas);
    this->piiu->chidList.remove (*this);
    this->piiu->chidList.add (*this);
    UNLOCK (this->piiu->pcas);

    return ECA_NORMAL;
}

void nciu::searchReplySetUp (unsigned sid, unsigned typeCode, unsigned long count)
{
    this->typeCode  = typeCode;      
    this->count = count;
    this->sid = sid;
}

/*
 * nciu::claimMsg ()
 */
bool nciu::claimMsg (tcpiiu *piiu)
{
    caHdr hdr;
    unsigned size;
    const char *pStr;
    int status;

    LOCK (this->piiu->pcas);


    if ( ! this->claimPending ) {
        return false;
    }

    if ( this->f_connected ) {
        return false;
    }

    hdr = cacnullmsg;
    hdr.m_cmmd = htons (CA_PROTO_CLAIM_CIU);

    if ( CA_V44 (CA_PROTOCOL_VERSION, piiu->minor_version_number) ) {
        hdr.m_cid = this->getId ();
        pStr = this->pNameStr;
        size = this->nameLength;
    }
    else {
        hdr.m_cid = this->sid;
        pStr = NULL;
        size = 0u;
    }

    hdr.m_postsize = size;

    /*
     * The available field is used (abused)
     * here to communicate the minor version number
     * starting with CA 4.1.
     */
    hdr.m_available = htonl (CA_MINOR_VERSION);

    /*
     * If we are out of buffer space then postpone this
     * operation until later. This avoids any possibility
     * of a push pull deadlock (since this is sent when 
     * parsing the UDP input buffer).
     */
    status = piiu->pushStreamMsg (&hdr, pStr, false);
    if ( status == ECA_NORMAL ) {

        /*
         * move to the end of the list once the claim has been sent
         */
        this->claimPending = FALSE;
        piiu->chidList.remove (*this);
        piiu->chidList.add (*this);

        if ( ! CA_V42 (CA_PROTOCOL_VERSION, piiu->minor_version_number) ) {
            this->connect (*piiu, this->typeCode, this->count, this->sid);
        }
    }
    else {
        piiu->claimRequestsPending = true;
    }
    UNLOCK (this->piiu->pcas);

    if ( status == ECA_NORMAL ) {
        return true;
    }
    else {
        return false;
    }
}

void nciu::installIO ( baseNMIU &io )
{
    LOCK ( this->piiu->pcas );
    this->piiu->pcas->installIO ( io );
    this->eventq.add ( io );
    UNLOCK ( this->piiu->pcas );
}

void nciu::uninstallIO ( baseNMIU &io )
{
    LOCK ( this->piiu->pcas );
    this->eventq.remove ( io );
    this->piiu->pcas->uninstallIO ( io );
    UNLOCK ( this->piiu->pcas );
}

bool nciu::connected () const
{
    return this->f_connected;
}

unsigned nciu::readSequence () const
{
    return this->piiu->pcas->readSequence ();
}

void nciu::incrementOutstandingIO ()
{
    this->piiu->pcas->incrementOutstandingIO ();
}

void nciu::decrementOutstandingIO ()
{
    this->piiu->pcas->decrementOutstandingIO ();
}

void nciu::decrementOutstandingIO ( unsigned seqNumber )
{
    this->piiu->pcas->decrementOutstandingIO ( seqNumber );
}
